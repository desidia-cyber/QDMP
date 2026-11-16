// qdpuzzle.h -- the intermediate state of a multi-step puzzle, for the props that keep it
// in their TRANSFORM and nowhere else.
//
// WHAT THE COOKED CONTENT ACTUALLY SAYS (decoded, not guessed)
// ------------------------------------------------------------
// The 287 `Lib_Book_00..04_BP_C` placements (and `Lib_Book_Open_BP_C`, and the RoS copies)
// are the puzzle the campaign brief calls out. Their blueprint packages were decoded
// (`analysis/pak/code/blueprints.py` on
// `RuinsLevel/Library/props/Lib_Book_00_BP.uasset`): 47 names, 6 exports, every export a
// `BlueprintGeneratedClass` / `SceneComponent` / `SCS_Node` / `SimpleConstructionScript` /
// `StaticMeshComponent`. The parent is `FS_StaticMeshActor_C` and the blueprint declares
// **no state property and no function of its own** -- not a bool, not an int, nothing that
// `ACTOR_STATES` or `OBJECTS` could carry. A book's progress IS its rotation, which the
// puzzle manager reads out of the world.
//
// So the earlier survey's hope ("if the class exposes one property, the existing lanes
// carry it") is answered: it does not. This lane carries the transform.
//
// THE REST OF THE PUZZLE ACTOR SET IS ALREADY COVERED, and this header records that so the
// next pass does not re-open it. Decoded parents:
//
//   AQR_KeyRecepticle_BP_C, AQ_OfferingPoint_BP_C, RuneButton_C, SipehaRune_BP_C,
//   Jigeki_Rune_BP_C, Puwema_Rune_BP_C, Wodaso_Rune_BP_C, PTSeal_SipehaRune_BP_C
//                                  -> SavableActor_C
//        a GUID-keyed `SavableActorStates` entry, i.e. `qdactorstate.h` (0x12) already
//        carries it, and a rune is exactly the kind of one-shot the lane was measured on.
//   Library_RotateBridge_C        -> SavableActor_C + Elevator_Base_C
//        its element is a MOVER (`qdmover.h`, 0x26) and its one-shot bit is the actor lane.
//   Library_LowerBridge_C         -> Activatable_C
//        `Activate`/`Deactivate`/`ActivateByLoad`, i.e. `qdobjectstate.h` SH_ACTIVATABLE.
//   SG_Door_Beepee_C, AqrStatuePuzzleDoor_C
//        -> Door_Base_C / the door family, i.e. `qdinteract.h` (open/close with the class'
//        own animation) -- measured LIVE for 52 door families.
//
// THE RULE
// --------
//   * The unit is ONE PLACED PROP, keyed exactly as every other placed actor here is keyed
//     (`<world>/<level>/<name>` hashed by `qdenemyid.h`'s PLACED rule, own seed), because
//     both machines run the same cooked world and the same placement names.
//   * **Either player may advance a step**, so this is LAST-WRITER-WINS by a per-key
//     sequence, not an owner-authoritative lane: the pair is standing in the same room
//     turning the same books, and refusing the partner's turn would be worse than a
//     one-frame rollback.
//   * The COMPLETION flag stays on the flag lane (`LibBookPuzzleComplete`,
//     `ROSBookPuzzleComplete`, `StoneGardenPuzzleFinished` -- the flag bank already carries
//     them and never lowers). This lane only makes the intermediate state agree, which is
//     what lets either player finish a puzzle the other started.
//   * The rotation is quantised to 1/65536 of a turn, the same unit `wire::QuantizeAngle`
//     uses for yaw everywhere else, and a change below one unit is not news.
//
// Header-only and dependency-free; `test_gapworld.cpp` drives the ordering rules.
#pragma once
#include <stdint.h>
#include <string.h>

#include "qdwire.h"

namespace qd {
namespace puzzle {

// A puzzle prop's intermediate state is a rotation plus one opaque byte.  The byte is for a
// class that also exposes a step ordinal; the books do not, so it stays 0 for them and is
// carried anyway so a future puzzle does not need a new kind.
struct Row {
    uint32_t key[4];
    int16_t  yaw;      // 1/65536 turn, wire::QuantizeAngle's unit
    int16_t  pitch;
    uint8_t  state;
    uint8_t  pad;
    uint32_t seq;      // per-KEY, monotone on the sender
};

static const int kRowBytes = 16 + 2 + 2 + 1 + 1 + 4;   // 26
static const int kMaxRows  = wire::kMaxBody / kRowBytes;

inline bool SameKey(const uint32_t a[4], const uint32_t b[4])
{
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}
inline bool ZeroKey(const uint32_t g[4]) { return (g[0] | g[1] | g[2] | g[3]) == 0; }

// A change smaller than this in the quantised unit is float noise, not a turn.  One unit is
// 0.0055 degrees; the books snap between authored stations.
static const int kYawEpsilon = 1;

inline bool SamePose(const Row& a, const Row& b)
{
    int dy = (int)a.yaw - (int)b.yaw;
    if (dy < 0) dy = -dy;
    int dp = (int)a.pitch - (int)b.pitch;
    if (dp < 0) dp = -dp;
    return dy <= kYawEpsilon && dp <= kYawEpsilon && a.state == b.state;
}

// ---------------------------------------------------------------------------
// Board -- the local picture, and the ordering rule for the partner's rows
// ---------------------------------------------------------------------------
// Bounded: a room holds a few dozen books, a loaded library a few hundred.  512 rows is
// above the 287 measured placements in the whole game, so a resident area cannot overflow
// and an overflow is still reported rather than silently dropping a prop (a dropped prop
// is a book that stays turned on one screen only).
static const int kMaxProps = 512;

enum Apply {
    P_OK = 0,        // apply this row locally
    P_DUPLICATE,     // the same (key, seq) arrived again
    P_STALE,         // an older row for that key: refuse, the newer pose stands
    P_REFUSED        // malformed: zero key
};

class Board {
public:
    Board() { Reset(); }

    void Reset()
    {
        n_ = 0;
        overflow_ = 0;
        for (int i = 0; i < kMaxProps; ++i) { p_[i].used = false; p_[i].published = 0; }
    }

    int  Count() const { int k = 0; for (int i = 0; i < kMaxProps; ++i) k += p_[i].used ? 1 : 0; return k; }
    int  Overflowed() const { return overflow_; }

    // The local player turned a book.  Returns true when the pose differs enough from what
    // this machine last PUBLISHED that it is news; `out` then carries the row to send.
    bool ObserveLocal(const uint32_t key[4], int16_t yaw, int16_t pitch, uint8_t state,
                      uint32_t* seq, Row* out)
    {
        if (ZeroKey(key)) return false;
        Slot* s = Touch(key);
        if (!s) { ++overflow_; return false; }
        s->yaw = yaw; s->pitch = pitch; s->state = state;
        Row want;
        memset(&want, 0, sizeof(want));
        for (int k = 0; k < 4; ++k) want.key[k] = key[k];
        want.yaw = yaw; want.pitch = pitch; want.state = state;
        if (s->published && SamePose(want, s->lastPub)) return false;
        want.seq = ++s->localSeq;
        s->lastPub = want;
        s->published = 1;
        if (seq) *seq = want.seq;
        if (out) *out = want;
        return true;
    }

    // The partner's row.  Last-writer-wins per key: a row whose seq is not newer than the
    // one this machine already holds is refused (a retransmission, or a turn that the
    // partner then undid).  `Adopt` records what was applied so the local observer does not
    // immediately echo it back as a local change.
    Apply OnPeer(const Row& r)
    {
        if (ZeroKey(r.key)) return P_REFUSED;
        if (r.seq == 0) return P_REFUSED;
        Slot* s = Touch(r.key);
        if (!s) { ++overflow_; return P_STALE; }
        if (s->havePeer) {
            if (r.seq == s->peerSeq) return P_DUPLICATE;
            if ((int32_t)(r.seq - s->peerSeq) < 0) return P_STALE;
        }
        s->peerSeq = r.seq;
        s->havePeer = true;
        s->yaw = r.yaw; s->pitch = r.pitch; s->state = r.state;
        // The pose the receiver now holds is the one it must NOT republish: publishing it
        // would be an echo, and two machines echoing one book is a ping-pong.
        s->lastPub = r;
        s->published = 1;
        return P_OK;
    }

    // What this machine holds for a key (for the apply step and for the log).
    bool Get(const uint32_t key[4], Row* out) const
    {
        const Slot* s = Find(key);
        if (!s || !s->used) return false;
        if (out) {
            memset(out, 0, sizeof(*out));
            for (int k = 0; k < 4; ++k) out->key[k] = key[k];
            out->yaw = s->yaw; out->pitch = s->pitch; out->state = s->state;
            out->seq = s->localSeq;
        }
        return true;
    }

    // The `i`-th live prop, so the rig can dump the set for a per-key diff.
    bool At(int i, Row* out) const
    {
        if (i < 0) return false;
        int seen = 0;
        for (int k = 0; k < kMaxProps; ++k) {
            if (!p_[k].used) continue;
            if (seen++ != i) continue;
            if (out) {
                memset(out, 0, sizeof(*out));
                for (int x = 0; x < 4; ++x) out->key[x] = p_[k].key[x];
                out->yaw = p_[k].yaw; out->pitch = p_[k].pitch; out->state = p_[k].state;
                out->seq = p_[k].localSeq;
            }
            return true;
        }
        return false;
    }

private:
    struct Slot {
        bool     used;
        uint32_t key[4];
        int16_t  yaw, pitch;
        uint8_t  state;
        uint32_t localSeq;
        uint32_t peerSeq;
        bool     havePeer;
        Row      lastPub;
        uint8_t  published;
    };

    Slot* Find(const uint32_t key[4]) const
    {
        for (int i = 0; i < kMaxProps; ++i)
            if (p_[i].used && SameKey(p_[i].key, key)) return const_cast<Slot*>(&p_[i]);
        return nullptr;
    }

    Slot* Touch(const uint32_t key[4])
    {
        if (Slot* s = Find(key)) return s;
        for (int i = 0; i < kMaxProps; ++i) {
            if (p_[i].used) continue;
            p_[i].used = true;
            for (int k = 0; k < 4; ++k) p_[i].key[k] = key[k];
            p_[i].yaw = p_[i].pitch = 0;
            p_[i].state = 0;
            p_[i].localSeq = 0;
            p_[i].peerSeq = 0;
            p_[i].havePeer = false;
            p_[i].published = 0;
            memset(&p_[i].lastPub, 0, sizeof(p_[i].lastPub));
            return &p_[i];
        }
        return nullptr;
    }

    Slot p_[kMaxProps];
    int  n_;
    int  overflow_;
};

// ---------------------------------------------------------------------------
// The wire (K_PUZZLE 0x62)
// ---------------------------------------------------------------------------
// Body: rev(u32, the sender's pass counter for the rig's log), n(u8 = rows in THIS body),
// then n x { key(16), yaw(i16), pitch(i16), state(u8), pad(u8), seq(u32) }.  One row is
// 26 bytes, so 46 rows fit the 1200-byte body; anything larger continues on the next tick
// with its own cursor -- never a fragment.
inline bool WriteHeader(wire::Writer& w, uint32_t rev, uint8_t n)
{
    w.U32(rev);
    w.U8(n);
    return w.ok;
}

inline int WriteRows(wire::Writer& w, const Row* rows, int n, int first)
{
    int i = first < 0 ? 0 : first;
    for (; i < n; ++i) {
        if (!w.Room(kRowBytes - 0)) break;
        w.Guid(rows[i].key);
        w.I16(rows[i].yaw);
        w.I16(rows[i].pitch);
        w.U8(rows[i].state);
        w.U8(0);
        w.U32(rows[i].seq);
    }
    return i;
}

inline bool ReadHeader(wire::Reader& r, uint32_t* rev, uint8_t* n)
{
    *rev = r.U32();
    *n = r.U8();
    return r.ok && *n <= (uint8_t)kMaxRows;
}

inline int ReadRows(wire::Reader& r, Row* out, int max)
{
    int n = 0;
    while (n < max && r.ok && r.pos < r.len) {
        r.Guid(out[n].key);
        out[n].yaw = r.I16();
        out[n].pitch = r.I16();
        out[n].state = r.U8();
        out[n].pad = r.U8();
        out[n].seq = r.U32();
        if (!r.ok) return n;
        if (ZeroKey(out[n].key) || out[n].seq == 0) return n;
        ++n;
    }
    return n;
}

} // namespace puzzle
} // namespace qd
