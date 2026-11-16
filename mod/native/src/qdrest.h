// qdrest.h -- what a rest is, and why it is a WORLD event rather than one player's act.
//
// THE MEASUREMENT THIS IS BUILT ON
// -------------------------------
// `Checkpoint_C` (LD45/Blueprints/Checkpoint) imports exactly one enemy-persistence call:
// `ClearAllEnemySaveStates` (`0x00E27460`).  Disassembled and confirmed: it is
// `TMap::Reset` on `save+0x270` -- the whole `EnemySaveStates` container.  So **a rest
// clears every enemy record in that machine's save**, and every spawner that has no record
// spawns its enemy again.  That is the game's own design and it is not a bug
// (analysis/14-combat-save-answers.md D4-p7).
//
// THE PROBLEM PER-PLAYER REST CREATES
// -----------------------------------
// The death lane (`qdrespawn.h`) made death and respawn per-player: a rest is what clears
// a player's death stacks and moves THEIR respawn point.  So one player can rest while the
// other is mid-dungeon.  Before that, two players always rested at the same time and the
// two saves agreed by accident.  Now:
//
//   * the resting machine clears its table and its enemies come back;
//   * the other machine's table still holds those deaths, so its enemies stay dead --
//     and it still re-broadcasts those dead rows (KillLog), so the resting machine is told
//     to kill its freshly respawned copies again;
//   * a **boss** whose death exists only as a record (the Shrine Boss writes no content
//     flag at all -- COVERAGE-MATRIX D6, `analysis` §4 item 8) revives on one machine and
//     stays dead on the other.
//
// THE RULE (implemented here, host-authoritative as the brief recommends)
// ----------------------------------------------------------------------
//   * **A rest by either player applies the same enemy reset on BOTH machines.** A rest is
//     a statement about the world, so the machine that rested announces it and every
//     machine performs the same reset locally (its own `ClearAllEnemySaveStates`, its own
//     kill log dropped).
//   * **Bosses and one-time enemies never reset.** Before the table is cleared, the set of
//     keys this machine owes a death to is snapshotted; every key classified as a boss (the
//     boss lane's own table) or as a one-time/flag-gated enemy is written back with
//     `StoreEnemySaveState(guid, -1, 0)` **after** the clear, so the rest cannot revive it
//     on either machine.
//   * **The host is authoritative on the episode.** The peer's rest becomes a request; the
//     host applies its own reset and re-broadcasts the episode, and the peer applies on
//     that answer. A duplicate or a retransmission is applied once (`seq`).
//
// WHY THE HOST AND NOT "WHOEVER RESTS"
// ------------------------------------
// Two rests a second apart (two checkpoints, or a tap on the same one) must be one episode,
// not two resets racing. One authority with a monotone sequence is the same shape the flag
// bank uses for the same reason, and it makes "a rest arrived while my reset was running"
// an ordering question rather than a race.
//
// Header-only and dependency-free; `test_gapworld.cpp` drives every rule below.
#pragma once
#include <stdint.h>
#include <string.h>

#include "qdwire.h"

namespace qd {
namespace rest {

static const int kNameCap = 64;

// How long a rest episode stays "the current one" for de-duplication. A rest cannot happen
// twice inside this window (the game's own rest animation is seconds long), and a
// retransmission of the same episode lands well inside it.
static const uint32_t kEpisodeMs = 8000;

// The bound on the deaths a reset carries across: every boss key plus every one-time or
// flag-gated key this machine holds. The enemy table itself is bounded at 1024, and the
// bosses in one world are a handful, so this never truncates in practice; a truncation is
// visible in the apply log ("restored N of M").
static const int kMaxKeep = 256;

// ---------------------------------------------------------------------------
// The exemption rule -- "bosses and one-time enemies never reset"
// ---------------------------------------------------------------------------
// `isBoss`      the key is in the boss lane's table (`qdboss.h` / the spawner's own data);
// `oneTime`     the spawner is a one-time placement (it has no respawn: the record's
//               `RespawnTime` is negative, which is what a "never comes back" spawner
//               stores -- analysis/04-sync-surface.md decodes the record's fields);
// `flagGated`   the spawner waits on a `<Something>Dead` NamedSaveData flag
//               (`RequireNamedSaveDataToSpawn` + `NamedSaveDataToCheck`), so the flag bank
//               -- which never lowers -- is the real gate and clearing the record cannot
//               revive it.
//
// Any one of the three is enough to keep the death.  The rule is deliberately generous:
// failing to revive an ordinary enemy costs one walk back to the spawner; reviving a boss
// the pair already killed is an un-completable fight.
inline bool ResetExempt(bool isBoss, bool oneTime, bool flagGated)
{
    return isBoss || oneTime || flagGated;
}

// ---------------------------------------------------------------------------
// Episode -- one rest, deduplicated, host-authoritative
// ---------------------------------------------------------------------------
enum Verdict {
    RV_NONE = 0,      // nothing to do
    RV_REQUEST,       // peer: this is my rest; ask the host to make it a world event
    RV_APPLY,         // host: apply the reset locally (and broadcast) / peer: apply it
    RV_DUPLICATE,     // this episode was already applied
    RV_STALE,         // an older episode arrived after a newer one
    RV_REFUSED        // malformed
};

class Episode {
public:
    Episode() { Reset(); }

    void Reset()
    {
        have_ = false;
        last_ = 0;
        lastAt_ = 0;
        epStart_ = 0;
        point_[0] = 0;
        applied_ = 0;
    }

    // ---- local side -------------------------------------------------------
    // The local player rested.  Returns the sequence to announce (never 0).  A rest inside
    // `kEpisodeMs` of the EPISODE'S START is the same episode: a checkpoint's own refresh
    // fires `PlayerRested` more than once and each refresh must not extend the window, or a
    // player standing at a checkpoint would never produce a second episode.  Two resets for
    // one rest would double-apply the boss re-statements.
    uint32_t NoteLocal(uint32_t now, const char* point)
    {
        bool sameEpisode = have_ && (uint32_t)(now - epStart_) < kEpisodeMs;
        if (!sameEpisode) {
            ++last_;
            if (last_ == 0) last_ = 1;
            have_ = true;
            epStart_ = now;
        }
        lastAt_ = now;
        CopyName(point_ ? point_ : "", point);
        return last_;
    }

    // ---- receive side -----------------------------------------------------
    // `isHost` decides what an accepted episode means: the host APPLIES it (it is the
    // authority); the peer only applies an ORDER, which the host sends with the same shape.
    // A peer's own request echoed back to it is an ORDER and applies once.
    Verdict OnPeer(uint32_t seq, const char* point, bool isHost, uint32_t now, bool isOrder)
    {
        if (seq == 0) return RV_REFUSED;
        if (have_) {
            int8_t age = (int8_t)(seq - last_);
            if (age == 0) return RV_DUPLICATE;
            if (age < 0) return RV_STALE;
        }
        last_ = seq;
        have_ = true;
        lastAt_ = now;
        CopyName(point_ ? point_ : "", point);
        ++applied_;
        // A peer announces a rest as a REQUEST; the host announces one as an ORDER. The
        // host never treats an order it receives as news (a peer cannot order the host),
        // and a peer never acts on a request (the host has not agreed yet).
        if (!isOrder && !isHost) return RV_REQUEST;
        return RV_APPLY;
    }

    // The host has applied `seq` and is broadcasting it as an order.  Recorded so a
    // re-delivery of the same number here is a DUPLICATE rather than a second reset.
    void NoteAppliedOrder(uint32_t seq, uint32_t now) { OnPeer(seq, point_, true, now, true); }

    bool     Have() const { return have_; }
    uint32_t Last() const { return last_; }
    uint32_t At() const { return lastAt_; }
    int      Applied() const { return applied_; }
    const char* Point() const { return point_; }

private:
    static void CopyName(char* dst, const char* src)
    {
        if (!dst) return;
        int i = 0;
        if (src) for (; src[i] && i < kNameCap - 1; ++i) dst[i] = src[i];
        dst[i] = 0;
    }

    bool     have_;
    uint32_t last_;
    uint32_t lastAt_;
    uint32_t epStart_;    // when this episode began (the window is measured from here)
    int      applied_;
    char     point_[kNameCap];
};

// ---------------------------------------------------------------------------
// The wire (K_REST_WORLD 0x61)
// ---------------------------------------------------------------------------
// Body: seq(u32), flags(u8 -- bit0 = this is an ORDER from the host, bit1 = the sender's
// player was the one who rested), px/py/pz (i32, 1/8 cm, the rest point for the log),
// str(point name).  31 bytes + name.
//
// The position and the name are EVIDENCE, not behaviour: no machine is moved by this body
// (a rest does not teleport the partner -- item 1's rule is that the respawn point stays
// per-player).  They are what makes a live log say two machines reset for the same rest.
static const uint8_t kRestOrder = 0x01;
static const uint8_t kRestLocal = 0x02;

struct Body {
    uint32_t seq;
    uint8_t  flags;
    float    px, py, pz;
    char     name[kNameCap];
};

inline bool Encode(wire::Writer& w, const Body& b)
{
    if (b.seq == 0) return false;
    if (b.flags > (uint8_t)(kRestOrder | kRestLocal)) return false;   // symmetric with Decode
    if (!(b.px == b.px) || !(b.py == b.py) || !(b.pz == b.pz)) return false;
    if (b.px > 1.0e8f || b.px < -1.0e8f) return false;
    if (b.py > 1.0e8f || b.py < -1.0e8f) return false;
    if (b.pz > 1.0e8f || b.pz < -1.0e8f) return false;
    w.U32(b.seq);
    w.U8(b.flags);
    w.I32(wire::QuantizePos(b.px));
    w.I32(wire::QuantizePos(b.py));
    w.I32(wire::QuantizePos(b.pz));
    w.Str(b.name);
    return w.ok;
}

inline bool Decode(wire::Reader& r, Body* b)
{
    memset(b, 0, sizeof(*b));
    b->seq = r.U32();
    b->flags = r.U8();
    b->px = wire::DequantizePos(r.I32());
    b->py = wire::DequantizePos(r.I32());
    b->pz = wire::DequantizePos(r.I32());
    if (r.Str(b->name, kNameCap) < 0) return false;
    if (!r.ok) return false;
    return b->seq != 0 && b->flags <= (kRestOrder | kRestLocal);
}

// ---------------------------------------------------------------------------
// The reset plan -- what the engine half must do, decided without the engine
// ---------------------------------------------------------------------------
// One place decides, so the two halves of the lane cannot disagree: the keys it keeps, the
// keys it lets die, and whether the reset runs at all.
struct Plan {
    bool     clear;          // run the game's own ClearAllEnemySaveStates
    int      keep;           // keys to re-state as dead AFTER the clear
    int      release;        // keys the rest brings back
    uint32_t seq;
};

inline Plan MakePlan(bool accepted, int exemptKeys, int ordinaryKeys, uint32_t seq)
{
    Plan p;
    p.clear = accepted;
    p.keep = accepted ? (exemptKeys < 0 ? 0 : exemptKeys) : 0;
    p.release = accepted ? (ordinaryKeys < 0 ? 0 : ordinaryKeys) : 0;
    p.seq = seq;
    return p;
}

} // namespace rest
} // namespace qd
