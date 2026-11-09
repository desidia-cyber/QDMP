// qdsequence.h -- cutscene / sequencer coordination (SYNC-GAPS §4).
//
// THE PROBLEM
// -----------
// A cutscene in this game is Blueprint-driven, not a manager with a replicated flag:
// a `CutsceneManagerBase_C` subclass starts a `LevelSequenceActor` on the machine whose
// player triggered it.  The other machine keeps playing the game, which is the failure:
// one player is watching an ending while the other walks around, and when the first
// finishes the two are in different narrative states.
//
// WHAT THIS CAN AND CANNOT DO
// ---------------------------
// There is no host-side switch that makes the other machine play a sequence.  The peer
// must call the same player-side entry:
//
//   FS_PlayerChar.BeginCutsceneWithManager(manager)   -- measured, 770-export class
//   FS_PlayerChar.EndManagerCutscene()
//   FS_PlayerChar.LockInputForLoadingOrCutscene()
//   FS_PlayerChar.UnlockInputForLoadingOrCutscene()
//
// The manager is a placed actor in the same cooked world, so it can be resolved by its
// short object name.  When it cannot be resolved (the cutscene is in a map the peer has
// not loaded, or the manager is spawned at runtime), the honest fallback is to HOLD the
// peer consistently -- input locked and the game's own cutscene mode entered -- until
// the sender reports ENDED.  Playing it "approximately" would desynchronise the
// narrative; holding is visible and reversible.
//
// The board is per-slot latest-wins by `seq`, because a lost datagram must cost a beat
// rather than leave a player held for ever.  A sender silent for kStaleMs is treated as
// not in a cutscene, so a crash cannot lock the partner's input.
#pragma once
#include <stdint.h>

namespace qd {
namespace sequence {

static const int      kMaxNameLen = 96;
static const int      kSlots      = 2;
static const uint32_t kStaleMs    = 5000;    // senders republish at 1 Hz

// What the caller should do this frame.
enum Verdict {
    CV_NONE = 0,     // nothing to change
    CV_PLAY,         // resolve the named manager and call BeginCutsceneWithManager
    CV_HOLD,         // no manager to resolve: enter cutscene mode / lock input
    CV_RELEASE,      // the peer's cutscene ended: exit cutscene mode / unlock input
    CV_WAIT          // the peer is still playing; stay held
};

struct SlotState {
    bool     present;
    uint32_t seq;
    bool     playing;
    uint8_t  slot;
    char     name[kMaxNameLen];
    uint32_t rx_ms;
};

class CutsceneBoard {
public:
    CutsceneBoard() { Reset(); }

    void Reset()
    {
        for (int i = 0; i < kSlots; ++i) {
            s_[i].present = false;
            s_[i].seq = 0;
            s_[i].playing = false;
            s_[i].slot = 0;
            s_[i].name[0] = 0;
            s_[i].rx_ms = 0;
        }
        held_ = false;
        heldName_[0] = 0;
        released_ = false;
        releasedSlot_ = 0;
        releasedSeq_ = 0;
    }

    // ---- remote publish ---------------------------------------------------
    // Returns true when the state changed (a new seq).  A repeat still refreshes
    // liveness, which is what the 1 Hz republish is for.  `ended` is explicit: the two
    // machines' managers do not finish on the same frame.
    bool Accept(uint8_t slot, uint32_t seq, bool playing, bool ended,
                const char* name, uint32_t now_ms)
    {
        if (slot >= kSlots) return false;
        SlotState& t = s_[slot];
        if (t.present && (int32_t)(seq - t.seq) <= 0) {
            if (seq == t.seq) t.rx_ms = now_ms;
            return false;
        }
        bool wasPlaying = t.present && t.playing;
        t.present = true;
        t.seq = seq;
        t.playing = playing && !ended;
        t.slot = slot;
        Copy(t.name, kMaxNameLen, name);
        t.rx_ms = now_ms;
        (void)wasPlaying;
        return true;                                    // a new seq is news
    }

    void Forget(uint8_t slot)
    {
        if (slot < kSlots) s_[slot].present = false;
    }

    // ---- decision ---------------------------------------------------------
    // `localPlaying` / `localName` are this machine's own observation.  The result is
    // what the caller should DO, not what is true: a hold must be acted on exactly
    // once, so `held_` is owned here.
    //
    // A FINISHED CUTSCENE IS NOT RE-PLAYED.  MEASURED (Session 16): the sequence ended on
    // both machines, the holder released, and then each side's next publish still named the
    // last sequence it played -- so `Decide` saw "the partner is playing X" and called
    // `Play` again, and the two machines restarted each other's cutscene for as long as the
    // lever kept publishing.  The board therefore remembers the (slot, seq) it released and
    // refuses to act on that same publication twice; a genuine second cutscene is a NEW seq,
    // which is what the sender bumps on every state change.
    Verdict Decide(uint8_t selfSlot, bool localPlaying, const char* localName,
                   uint32_t now_ms)
    {
        int other = (selfSlot == 0) ? 1 : 0;
        const SlotState& p = s_[other];
        bool peerPlaying = p.present && p.playing && !Stale(p, now_ms);

        if (!peerPlaying) {
            if (held_) {
                held_ = false;
                heldName_[0] = 0;
                // Record the publication that was released, so the same naming cannot start
                // it again.  `releasedSeq_` is per-slot so the two directions do not collide.
                releasedSlot_ = (uint8_t)other;
                releasedSeq_ = p.seq;
                released_ = true;
                return CV_RELEASE;
            }
            return CV_NONE;
        }
        // The partner is naming a cutscene whose publication this machine already released.
        if (released_ && releasedSlot_ == (uint8_t)other && releasedSeq_ == p.seq) {
            if (held_) { held_ = false; heldName_[0] = 0; return CV_RELEASE; }
            return CV_NONE;
        }

        // Already playing the same sequence: nothing to do, and nothing to release.
        if (localPlaying && localName && localName[0] && Same(localName, p.name)) {
            held_ = false;
            return CV_NONE;
        }
        // In a DIFFERENT cutscene: do not interrupt it.  The two machines cannot both
        // be right; the first one to finish releases the hold and the divergence is
        // logged by the caller.
        if (localPlaying) return CV_WAIT;

        if (p.name[0]) {
            if (!held_) {
                held_ = true;
                Copy(heldName_, kMaxNameLen, p.name);
                return CV_PLAY;          // act once; a repeat frame is CV_WAIT
            }
            return CV_WAIT;
        }
        if (!held_) { held_ = true; heldName_[0] = 0; return CV_HOLD; }
        return CV_WAIT;
    }

    // The caller could not resolve / play the manager; fall back to a consistent hold.
    void OnPlayFailed() { held_ = true; heldName_[0] = 0; }

    // ---- queries ----------------------------------------------------------
    bool PeerPlaying(uint32_t now_ms, char* out, int cap) const
    {
        int i = 0;
        // The caller knows its own slot; expose both and let it pick.
        for (; i < kSlots; ++i) {
            if (!s_[i].present || !s_[i].playing || Stale(s_[i], now_ms)) continue;
            Copy(out, cap, s_[i].name);
            return true;
        }
        return false;
    }
    bool AnyPeerPlaying(uint32_t now_ms) const
    {
        for (int i = 0; i < kSlots; ++i)
            if (s_[i].present && s_[i].playing && !Stale(s_[i], now_ms)) return true;
        return false;
    }
    bool Held() const { return held_; }
    const char* HeldName() const { return heldName_; }

    // Is the partner currently naming this exact sequence?  The caller uses it to decide
    // whether it is the PLAYER of a cutscene or its HOLDER: a machine that is already playing
    // what the partner names is not holding anything, and acting on that publication is what
    // made two machines replay each other's sequence for ever (Session 16).
    bool PeerNameIs(const char* name, uint32_t now_ms) const
    {
        for (int i = 0; i < kSlots; ++i) {
            if (!s_[i].present || !s_[i].playing || Stale(s_[i], now_ms)) continue;
            if (s_[i].name[0] && Same(s_[i].name, name)) return true;
        }
        return false;
    }

    static bool Same(const char* a, const char* b)
    {
        int i = 0;
        for (; a[i] || b[i]; ++i) if (a[i] != b[i]) return false;
        return true;
    }
    static void Copy(char* d, int cap, const char* s)
    {
        if (!d || cap <= 0) return;
        int i = 0;
        if (s) for (; s[i] && i < cap - 1; ++i) d[i] = s[i];
        d[i] = 0;
    }

private:
    static bool Stale(const SlotState& t, uint32_t now_ms)
    {
        uint32_t age = (now_ms > t.rx_ms) ? now_ms - t.rx_ms : 0;
        return age > kStaleMs;
    }

    SlotState s_[kSlots];
    bool      held_;
    char      heldName_[kMaxNameLen];
    // The (slot, seq) publication this machine already released (see Decide).
    bool      released_;
    uint8_t   releasedSlot_;
    uint32_t  releasedSeq_;
};

} // namespace sequence
} // namespace qd
