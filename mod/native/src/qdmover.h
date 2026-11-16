// qdmover.h -- owner-authoritative moving world objects (SYNC-GAPS §4).
//
// THE FAILURE
// -----------
// An elevator, a moving platform or an `Activatable` mover is simulated by the machine
// whose player is near it.  A player riding one is therefore shown floating next to a
// stationary platform on the other machine, and if the other player steps on it they
// are on different surfaces.  The transform is the only state that matters; the mover's
// own logic (a timeline, a Blueprint event) must NOT be replayed, because two replays of
// a one-shot mover desynchronise.
//
// THE RULE
// --------
// Exactly one machine OWNS a mover: the one whose player is nearer, because that is the
// machine whose game actually moves it (the other machine's copy is idle).  The owner
// publishes absolute transforms; the follower applies them by writing the actor
// transform, never by starting the mover.  Ownership follows the same rule as enemies
// (nearer player, host breaks an exact tie) so a co-located pair cannot both drive one
// object.
//
// Identity is the placed actor's path hash (the `DIALOGUE` scheme): `UFS_*` movers
// expose no authored GUID the native side can read without an oracle, while a placed
// actor's object / level / world names are identical on both machines because both run
// the same cooked package.
//
// Everything here is pure; `test_mover.cpp` drives the ownership and echo cases.
#pragma once
#include <stdint.h>

namespace qd {
namespace mover {

static const int kMaxMovers = 48;
static const int kIdLen     = 16;

// The row form matches WIRE.md §3.17 (33 bytes).
struct Sample {
    uint8_t id[kIdLen];
    int32_t x, y, z;        // 1/8 cm
    int16_t yaw, pitch;     // 1/65536 turn
    uint8_t flags;          // bit0 moving, bit1 owner
};

static const uint8_t kMoving = 0x01;
static const uint8_t kOwner  = 0x02;

// One mover's state on this machine.
struct Record {
    bool     used;
    uint8_t  id[kIdLen];
    bool     owner;          // this machine runs it
    uint32_t rev;            // the revision of the last row applied / published
    bool     haveRemote;
    Sample   remote;         // the owner's last published transform
    uint32_t remote_ms;
    bool     haveLocal;
    Sample   local;          // what this machine's game last showed
    uint32_t local_ms;
    bool     dirty;          // the local transform changed and must be published
};

class Board {
public:
    Board() { Reset(); }

    void Reset()
    {
        for (int i = 0; i < kMaxMovers; ++i) rec_[i].used = false;
        for (int i = 0; i < kMaxMovers; ++i) rec_[i].dirty = false;
        overflow_ = false;
        lastRev_ = 0;
    }

    // ---- local observation -------------------------------------------------
    // The local game's transform for `id`.  `moving` is the mover's own "is moving"
    // signal, not the delta (a platform resting between waypoints is still owned).
    // Returns true when the change is worth publishing (this machine owns it and the
    // transform moved by more than `eps` wire units).
    bool ObserveLocal(const uint8_t id[kIdLen], const Sample& s, bool moving, int32_t eps)
    {
        Record* r = Get(id, true);
        if (!r) return false;
        bool changed = !r->haveLocal || !Close(r->local, s, eps);
        r->local = s;
        r->local_ms = 0;
        r->haveLocal = true;
        if (!r->owner) return false;
        if (changed || moving) {
            if (changed) r->dirty = true;
            return changed;
        }
        return false;
    }

    // ---- ownership ---------------------------------------------------------
    // Nearest player wins; an exact tie goes to the host.  Returns true when the owner
    // changed.  `localDist` / `peerDist` are squared distances or any monotone metric;
    // `isHost` breaks the tie.
    bool ResolveOwner(const uint8_t id[kIdLen], int64_t localDist, int64_t peerDist,
                      bool isHost)
    {
        Record* r = Get(id, true);
        if (!r) return false;
        bool want = (localDist < peerDist) || (localDist == peerDist && isHost);
        if (r->owner == want && r->used) return false;
        r->owner = want;
        r->haveLocal = false;      // the follower's stale local copy is not a fact
        r->dirty = want;
        return true;
    }

    // ---- inbound -----------------------------------------------------------
    // Apply the owner's row.  Returns true when the caller should write the transform.
    // A row from a NON-owner is refused: two machines must never drive one mover.
    bool Accept(const Sample& s, uint32_t rev, uint32_t now_ms)
    {
        Record* r = Get(s.id, true);
        if (!r) return false;
        if (!(s.flags & kOwner)) return false;       // a follower's observation
        if (r->haveRemote && (int32_t)(rev - r->rev) <= 0) return false;
        r->haveRemote = true;
        r->remote = s;
        r->remote_ms = now_ms;
        r->rev = rev;
        return true;
    }

    // ---- queries ----------------------------------------------------------
    bool IsOwner(const uint8_t id[kIdLen]) const
    {
        const Record* r = Find(id);
        return r && r->owner;
    }
    bool Has(const uint8_t id[kIdLen]) const { return Find(id) != nullptr; }
    // The owner's last published transform, for the follower to apply.
    bool Remote(const uint8_t id[kIdLen], Sample* out) const
    {
        const Record* r = Find(id);
        if (!r || !r->haveRemote) return false;
        if (out) *out = r->remote;
        return true;
    }
    bool Overflowed() const { return overflow_; }
    uint32_t Revision() const { return lastRev_; }
    void BumpRevision() { ++lastRev_; }

    // Rows this machine should publish (owners only, changed since the last publish).
    int Snapshot(Sample* out, int cap, bool clearDirty)
    {
        int n = 0;
        for (int i = 0; i < kMaxMovers && n < cap; ++i) {
            Record& r = rec_[i];
            if (!r.used || !r.owner || !r.haveLocal) continue;
            Sample s = r.local;
            s.flags |= kOwner;
            if (clearDirty) r.dirty = false;
            out[n++] = s;
        }
        return n;
    }

    // Rows that became dirty since the last publish (the steady-state body).
    int SnapshotDirty(Sample* out, int cap)
    {
        int n = 0;
        for (int i = 0; i < kMaxMovers && n < cap; ++i) {
            Record& r = rec_[i];
            if (!r.used || !r.owner || !r.haveLocal || !r.dirty) continue;
            Sample s = r.local;
            s.flags |= kOwner | kMoving;
            r.dirty = false;
            out[n++] = s;
        }
        return n;
    }

    // Read-only enumeration for the desync auditor (qdaudit.h).  It exposes what THIS
    // machine's game shows (`local`) rather than the owner's row: the follower APPLIES the
    // owner's transform, so its own game reads back the same value -- which is the fact
    // worth comparing.  `flags` is deliberately not exposed: who owns a mover differs by
    // design, so a flag comparison would report a desync on a correct session.
    bool PeekLocal(int index, uint8_t outId[kIdLen], Sample* out) const
    {
        if (index < 0) return false;
        int seen = 0;
        for (int k = 0; k < kMaxMovers; ++k) {
            if (!rec_[k].used || !rec_[k].haveLocal) continue;
            if (seen != index) { ++seen; continue; }
            for (int j = 0; j < kIdLen; ++j) outId[j] = rec_[k].id[j];
            if (out) *out = rec_[k].local;
            return true;
        }
        return false;
    }

    // ---- helpers ----------------------------------------------------------
    static bool SameId(const uint8_t* a, const uint8_t* b)
    {
        for (int i = 0; i < kIdLen; ++i) if (a[i] != b[i]) return false;
        return true;
    }
    // FNV-1a over the actor's path, two lanes -- the same stable identity the dialogue
    // lane uses, so a placed mover is the same object on both machines.
    static void IdFromPath(const char* path, uint8_t out[kIdLen])
    {
        uint64_t h1 = 1469598103934665603ULL, h2 = 0x84222325CBF29CE4ULL;
        for (int i = 0; path && path[i]; ++i) {
            h1 = (h1 ^ (uint8_t)path[i]) * 1099511628211ULL;
            h2 = (h2 ^ (uint8_t)path[i]) * 1099511628211ULL;
            h2 ^= h2 >> 29;
        }
        for (int i = 0; i < 8; ++i) {
            out[i] = (uint8_t)(h1 >> (8 * i));
            out[8 + i] = (uint8_t)(h2 >> (8 * i));
        }
    }

private:
    Record* Find(const uint8_t id[kIdLen])
    {
        for (int i = 0; i < kMaxMovers; ++i)
            if (rec_[i].used && SameId(rec_[i].id, id)) return &rec_[i];
        return nullptr;
    }
    const Record* Find(const uint8_t id[kIdLen]) const
    {
        for (int i = 0; i < kMaxMovers; ++i)
            if (rec_[i].used && SameId(rec_[i].id, id)) return &rec_[i];
        return nullptr;
    }
    Record* Get(const uint8_t id[kIdLen], bool create)
    {
        Record* r = Find(id);
        if (r || !create) return r;
        for (int i = 0; i < kMaxMovers; ++i) {
            if (rec_[i].used) continue;
            r = &rec_[i];
            r->used = true;
            for (int k = 0; k < kIdLen; ++k) r->id[k] = id[k];
            r->owner = false;
            r->rev = 0;
            r->haveRemote = r->haveLocal = false;
            r->remote_ms = r->local_ms = 0;
            r->dirty = false;
            return r;
        }
        overflow_ = true;
        return nullptr;
    }
    static bool Close(const Sample& a, const Sample& b, int32_t eps)
    {
        int32_t dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        if (dz < 0) dz = -dz;
        return dx <= eps && dy <= eps && dz <= eps &&
               a.yaw == b.yaw && a.pitch == b.pitch;
    }

    Record  rec_[kMaxMovers];
    bool    overflow_;
    uint32_t lastRev_;
};

} // namespace mover
} // namespace qd
