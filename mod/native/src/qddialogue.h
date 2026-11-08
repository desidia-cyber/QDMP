// qddialogue.h -- the cross-machine dialogue lock.
//
// THE FAILURE BEING DESIGNED AGAINST
// ----------------------------------
// The previous project's dialogue bug: both machines ran the same talk Blueprint and a
// race decided which choice took effect.  The fix that worked was not "replicate the
// choice" but "publish a lock with a sequence number", because:
//
//   * a lock published as STATE means a lost message costs a beat, never the session;
//   * a sequence number means a stale publish is discarded rather than applied
//     backwards;
//   * the first answer decides for both, so the two machines cannot diverge on a
//     branch that changes progression.
//
// Two further facts from this game's analysis shape the API:
//
//   * D3: a LOCAL lock already exists -- `RunningDialogue` (player), `PlayerIsTalkingTo`
//     (NPC_Base) and `DisableAllPlayerInput`.  The mod therefore does not invent the
//     lock; it publishes the game's state as one replicated `{npcGuid, slot, seq}`.
//   * D2: dialogue branches are CONDITIONAL on `NamedSaveData` and inventory, so the
//     option list need not be transmitted -- it is derivable from state that is already
//     synced.  Only the answer goes on the wire.
//
// And the rule that costs a player a quest step if ignored: "a dialogue left open at
// death produces an invisible unanswered choice", so a death MUST release the lock.
#pragma once
#include <stdint.h>

namespace qd {
namespace dialogue {

static const uint8_t kSlotFree = 0xFF;

struct DialogueState {
    uint32_t seq;
    uint8_t  npc[16];
    uint8_t  slot;      // 0, 1, or kSlotFree
    uint16_t line;      // dialogue cursor: sequence index
    uint16_t entry;     // dialogue cursor: entry index
};

// A decided (line, entry) -> option.  Bounded and fixed-size: the DLL must not
// allocate, and a cap that is reached is REPORTED rather than silently growing.
struct Decision {
    bool     used;
    uint16_t line;
    uint16_t entry;
    uint16_t option;
};

class DialogueLock {
public:
    static const int      kMaxDecisions = 64;
    static const uint32_t kAcquireTimeoutMs = 120000;   // 2 min: a stuck lock must expire

    DialogueLock() { Reset(); }

    void Reset()
    {
        state_.seq = 0;
        state_.slot = kSlotFree;
        state_.line = 0;
        state_.entry = 0;
        for (int i = 0; i < 16; ++i) state_.npc[i] = 0;
        for (int i = 0; i < kMaxDecisions; ++i) dec_[i].used = false;
        acquireMs_ = 0;
        ended_ = false;
        overflow_ = false;
    }

    // ---- local arbitration -------------------------------------------------
    // Returns true if this slot may start talking to this NPC.
    bool TryAcquire(uint8_t slot, const uint8_t npc[16], uint32_t now_ms)
    {
        if (!CanStart(slot, npc)) return false;
        state_.slot = slot;
        for (int i = 0; i < 16; ++i) state_.npc[i] = npc ? npc[i] : 0;
        acquireMs_ = now_ms;
        ended_ = false;
        ++state_.seq;      // a local acquire is a state change and must be published
        return true;
    }

    bool CanStart(uint8_t slot, const uint8_t npc[16]) const
    {
        if (state_.slot == kSlotFree) return true;
        if (state_.slot == slot) return true;              // re-entrant for the holder
        // A different slot holds a conversation.  Same NPC or not, it is one at a
        // time: the game itself gates a second start on DisableAllPlayerInput, and
        // reproducing that gate is what stops the race.
        (void)npc;
        return false;
    }

    void Release(uint8_t slot)
    {
        if (state_.slot != slot) return;
        state_.slot = kSlotFree;
        ended_ = true;
        ++state_.seq;
    }

    // ---- replicated state --------------------------------------------------
    // Publish as state.  The number must increase; an older publish is discarded by
    // the RECEIVER, so a sender that cannot observe its own ordering is still safe.
    void Publish(const DialogueState& s)
    {
        if (s.seq <= state_.seq && s.seq != 0) {
            // Discard, do not apply: applying a stale cursor would jump the dialogue
            // backwards and, worse, could un-decide a branch.
            return;
        }
        state_ = s;
    }

    bool Accept(const DialogueState& s)
    {
        if (s.seq == 0) return false;
        if (s.seq <= state_.seq) return false;
        state_ = s;
        // A state that reports the lock free is also an "ended" signal, which is how a
        // death-driven release propagates without a second message type.
        if (s.slot == kSlotFree) ended_ = true;
        else { ended_ = false; acquireMs_ = 0; }
        return true;
    }

    DialogueState Current() const { return state_; }
    bool IsFree() const { return state_.slot == kSlotFree; }
    uint8_t Holder() const { return state_.slot; }
    bool Overflowed() const { return overflow_; }

    // ---- first answer decides ----------------------------------------------
    // Returns false if this (line, entry) was already answered -- the caller then
    // uses Decided() to learn the answer that won rather than asking the player.
    bool RecordAnswer(uint16_t line, uint16_t entry, uint16_t option)
    {
        for (int i = 0; i < kMaxDecisions; ++i) {
            if (dec_[i].used && dec_[i].line == line && dec_[i].entry == entry) return false;
        }
        for (int i = 0; i < kMaxDecisions; ++i) {
            if (!dec_[i].used) {
                dec_[i].used = true;
                dec_[i].line = line;
                dec_[i].entry = entry;
                dec_[i].option = option;
                return true;
            }
        }
        // Full.  Reported, not silently dropped: a silent drop here is a dialogue that
        // can be answered twice, which is the bug this class exists to prevent.
        overflow_ = true;
        return false;
    }

    bool Decided(uint16_t line, uint16_t entry, uint16_t* option_out) const
    {
        for (int i = 0; i < kMaxDecisions; ++i) {
            if (dec_[i].used && dec_[i].line == line && dec_[i].entry == entry) {
                if (option_out) *option_out = dec_[i].option;
                return true;
            }
        }
        return false;
    }

    // A death must end the conversation.  This is the fix for the measured failure
    // "a dialogue left open at death produces an invisible unanswered choice".
    void OnPlayerDeath(uint8_t slot)
    {
        if (state_.slot == slot) {
            state_.slot = kSlotFree;
            ended_ = true;
            ++state_.seq;
        }
    }

    bool Ended() const { return ended_; }

    // ---- time --------------------------------------------------------------
    void Tick(uint32_t now_ms)
    {
        if (state_.slot == kSlotFree) return;
        // A bracket needs its own expiry: a lock held with no traffic would otherwise
        // freeze both players out of every NPC for the rest of the session.
        uint32_t held = (now_ms > acquireMs_) ? (now_ms - acquireMs_) : 0;
        if (acquireMs_ != 0 && held > kAcquireTimeoutMs) {
            state_.slot = kSlotFree;
            ended_ = true;
            acquireMs_ = 0;
            ++state_.seq;
        }
    }

private:
    DialogueState state_;
    Decision      dec_[kMaxDecisions];
    uint32_t      acquireMs_;
    bool          ended_;
    mutable bool  overflow_;
};

// ---------------------------------------------------------------------------
// TalkBoard -- who is talking to which NPC, per player slot.
// ---------------------------------------------------------------------------
// SYNC-DESIGN 2.7 (owner's decision, 24-quest-closeout 5a/5b): there is NO global
// NPC lock; both players may talk at once, progression is host-authoritative.  What
// must not happen is two players in the SAME conversation, because both machines then
// run the same talk Blueprint and its one-time actions (turn-ins, gifts) fire twice.
// So the published state is per slot, and the only enforcement is local: the NPC the
// partner is talking to is made non-interactable on this machine with the game's own
// switch.  That switch is not a function pair: `BlockInteract` is a BOOLEAN PROPERTY on
// NPC_Base_C (+0x0613, SYNC-RESEARCH 4), and only `ReenableInteract` is a UFunction
// (0 arguments).  A name-resolved call to `BlockInteract` finds nothing, which is why
// the block half did nothing until it was written as a property.
//
// A slot's state is latest-wins by `seq` (a lost datagram costs a beat), and it goes
// stale: a sender that stops publishing (crash, disconnect) must not leave an NPC
// blocked for the rest of the session.
struct TalkState {
    bool     present;
    uint32_t seq;
    bool     talking;
    uint8_t  npc[16];
    uint16_t line;
    uint32_t rx_ms;
};

class TalkBoard {
public:
    static const int      kSlots = 2;
    static const uint32_t kStaleMs = 5000;    // senders republish at 1 Hz

    TalkBoard() { Reset(); }
    void Reset()
    {
        for (int i = 0; i < kSlots; ++i) { s_[i].present = false; s_[i].seq = 0; s_[i].talking = false; s_[i].rx_ms = 0; }
    }

    // Remote publish.  Returns false (discarded) for an older or repeated seq; a repeat
    // still refreshes liveness, which is what the 1 Hz republish is for.
    bool Accept(uint8_t slot, uint32_t seq, bool talking, const uint8_t npc[16],
                uint16_t line, uint32_t now_ms)
    {
        if (slot >= kSlots) return false;
        TalkState& t = s_[slot];
        if (t.present && (int32_t)(seq - t.seq) <= 0) {
            if (seq == t.seq) t.rx_ms = now_ms;
            return false;
        }
        t.present = true;
        t.seq = seq;
        t.talking = talking;
        for (int i = 0; i < 16; ++i) t.npc[i] = npc ? npc[i] : 0;
        t.line = line;
        t.rx_ms = now_ms;
        return true;
    }

    void Forget(uint8_t slot) { if (slot < kSlots) s_[slot].present = false; }

    // Is a live slot other than `self` talking to this NPC?
    bool OtherTalkingTo(uint8_t self, const uint8_t npc[16], uint32_t now_ms) const
    {
        for (int i = 0; i < kSlots; ++i) {
            if (i == self) continue;
            const TalkState& t = s_[i];
            if (!t.present || !t.talking || Stale(t, now_ms)) continue;
            if (SameId(t.npc, npc)) return true;
        }
        return false;
    }

    // The NPC a live remote slot is talking to.
    bool RemoteNpc(uint8_t slot, uint8_t out[16], uint32_t now_ms) const
    {
        if (slot >= kSlots) return false;
        const TalkState& t = s_[slot];
        if (!t.present || !t.talking || Stale(t, now_ms)) return false;
        for (int i = 0; i < 16; ++i) out[i] = t.npc[i];
        return true;
    }

    static bool SameId(const uint8_t* a, const uint8_t* b)
    {
        for (int i = 0; i < 16; ++i) if (a[i] != b[i]) return false;
        return true;
    }

private:
    static bool Stale(const TalkState& t, uint32_t now_ms)
    {
        uint32_t age = (now_ms > t.rx_ms) ? now_ms - t.rx_ms : 0;
        return age > kStaleMs;
    }
    TalkState s_[kSlots];
};

// Stable 128-bit id for an actor placed in a cooked level: FNV-1a over its path
// ("<world>/<level>/<object name>"), two lanes.  NPC_Base exposes no authored GUID the
// native side can read without an oracle, but a placed object's name and outer chain
// come from the same cooked package on both machines, so they agree without any
// negotiation.  (A runtime-spawned NPC would get a per-machine name; none is known.)
inline void StableId(const char* path, uint8_t out[16])
{
    uint64_t h1 = 1469598103934665603ULL, h2 = 0x84222325CBF29CE4ULL;
    for (int i = 0; path && path[i]; ++i) {
        h1 = (h1 ^ (uint8_t)path[i]) * 1099511628211ULL;
        h2 = (h2 ^ (uint8_t)path[i]) * 1099511628211ULL;
        h2 ^= h2 >> 29;
    }
    for (int i = 0; i < 8; ++i) { out[i] = (uint8_t)(h1 >> (8 * i)); out[8 + i] = (uint8_t)(h2 >> (8 * i)); }
}

} // namespace dialogue
} // namespace qd
