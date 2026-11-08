// qdinventory.h -- the host -> peer inventory mirror, as two pure state machines.
//
// WHY INVENTORY IS A SEPARATE HEADER AND NOT A COROUTINE IN proxy.cpp
// ------------------------------------------------------------------
// Two things here are easy to get subtly wrong and impossible to watch inside a live
// game: WHICH datagram carries which entry when the whole set does not fit, and WHAT
// happens to a grant that the game refuses.  Both are timing state, so both live here
// with no socket, no UObject and no clock of their own; the caller supplies `now` and
// performs the send/grant.  test/test_inventory.cpp drives the same code.
//
// THE AUTHORITY RULE THIS IMPLEMENTS
// ----------------------------------
// SYNC-DESIGN section 2.4: inventory is HOST-OWNED and SHARED.  The host's container is
// the truth; the peer receives the complete set and may only ADD.  It never removes and
// never lowers, because this game's items are not fungible (a key is a key, but counts
// are per-pickup) and because a local pickup the host has not seen yet must not be
// taken away while the round trip is in flight.  A key granted twice is harmless; a key
// the peer lacks locks that player out of the region alone -- that is the whole reason
// this lane exists.
//
// WHAT IS DELIBERATELY NOT HERE
// -----------------------------
// Peer -> host item changes are NOT in this header.  They are the other half of the
// lane and live in `qdinvintent.h`, because the payload and the idempotency rule are
// different: an item path is far longer than a flag name, and an additive grant needs
// a cumulative value plus a host-side ledger rather than the flag queue's absolute
// assignment.  Keeping them apart means this host publisher stays the proven, tested
// one-way mirror, and the new direction cannot regress it.  test/test_invintent.cpp
// drives the sibling; test/test_inventory.cpp still drives this one.
#pragma once
#include <stdint.h>
#include <string.h>

#include "qdwire.h"

namespace qd {
namespace inventory {

// The longest soft object path this lane carries.  SYNC-DESIGN section 2.4's measured
// example is `/Game/Items/KeyItems/AquaDoorKey/AquaDoorKey_2.AquaDoorKey_2` (58 bytes);
// 200 leaves room for the deepest cooked asset without approaching qdwire's 255-byte
// `str` limit.  A path longer than this is REFUSED (and reported) rather than truncated:
// a truncated path aliases a different asset, which would grant the wrong item.
static const int kMaxPath = 200;

// 512 entries.  The design's inventory universe is 503 key-item assets in 26 families
// plus ordinary items; a live save holds far fewer, but the snapshot must not silently
// lose one, so this is provisioned for the whole measured universe.  Entry is 204 bytes,
// so the fixed table costs about 100 KB in a /MT module -- the price of never allocating
// on the game thread.
static const int kMaxEntries = 512;

// One unacknowledged grant per inventory entry the host has and the peer does not.  The
// two containers have the same bound because a fresh peer can legitimately owe every
// entry of the host's set; a smaller table would turn "joined a long session" into a
// silent partial mirror.  At two grants per tick this drains in about 8 s at 30 fps.
static const int kMaxPending = kMaxEntries;

// A grant is a ProcessEvent plus a native read-back.  Two per tick is the measured
// ceiling the design asks for: several per frame is a visible hitch, and the lane is
// absolute state, so there is nothing to lose by taking two extra ticks.
static const int kMaxPerTick = 2;

// The host republishes the whole set at 1 Hz even when nothing changed.  A peer with no
// memory converges in one second, and a lost datagram costs a second rather than a
// session (SYNC-DESIGN rule 2: state, not events).
static const uint32_t kHeartbeatMs = 1000;

// A refused grant is retried only after a body has had a chance to arrive with a newer
// count.  Retrying every frame would hammer a writer that is refusing for a structural
// reason (asset not loaded, save not ready) and turn one WARN into a log flood.
static const uint32_t kRetryMs = 1000;

inline bool PathEq(const char* a, const char* b)
{
    if (!a || !b) return false;
    for (int i = 0; a[i] || b[i]; ++i) if (a[i] != b[i]) return false;
    return true;
}

// Returns the number of bytes copied, including the terminator, or 0 when the path does
// not fit.  A caller must treat 0 as "refuse this entry", never as "empty path".
inline int PathCopy(char* dst, int cap, const char* src)
{
    if (!dst || cap <= 0) return 0;
    dst[0] = 0;
    if (!src || !src[0]) return 0;
    int i = 0;
    for (; src[i]; ++i) {
        if (i >= cap - 1) { dst[0] = 0; return 0; }
        dst[i] = src[i];
    }
    dst[i] = 0;
    return i + 1;
}

// ---------------------------------------------------------------------------
// The absolute set: one entry per soft object path and its count
// ---------------------------------------------------------------------------
// Order is insertion order, deliberately.  The map the host reads is a TSet, whose
// iteration order is an implementation detail; a wire body that depended on it would
// make a golden vector meaningless and a two-machine diff noisy.  `SameAs` is
// order-independent so change detection does not care either.
struct Entry {
    char    path[kMaxPath];
    int32_t count;
};

class Snapshot {
public:
    Snapshot() { Clear(); }

    void Clear()
    {
        n_ = 0;
        overflow_ = false;
    }

    int  Count() const { return n_; }
    bool Full() const { return n_ >= kMaxEntries; }
    bool Overflowed() const { return overflow_; }

    // False when the path is empty, too long or the table is full.  A caller that gets
    // false must report it: the alternative is a key that exists on the host and never
    // reaches the peer.
    bool Set(const char* path, int32_t count)
    {
        char p[kMaxPath];
        if (PathCopy(p, kMaxPath, path) <= 0) { overflow_ = true; return false; }

        int i = Find(p);
        if (i >= 0) {
            e_[i].count = count;
            return true;
        }
        if (n_ >= kMaxEntries) { overflow_ = true; return false; }
        memcpy(e_[n_].path, p, sizeof(p));
        e_[n_].count = count;
        ++n_;
        return true;
    }

    int Find(const char* path) const
    {
        if (!path) return -1;
        for (int i = 0; i < n_; ++i) {
            if (PathEq(e_[i].path, path)) return i;
        }
        return -1;
    }

    const Entry* At(int i) const
    {
        if (i < 0 || i >= n_) return nullptr;
        return &e_[i];
    }

    // Order-independent equality, and the reason a second enumeration of an unchanged
    // container does not bump the revision.  O(n^2) is intentional: n is a few dozen
    // and this avoids a sort buffer (and therefore an allocation) on the game thread.
    bool SameAs(const Snapshot& o) const
    {
        if (n_ != o.n_) return false;
        for (int i = 0; i < n_; ++i) {
            int j = o.Find(e_[i].path);
            if (j < 0 || o.e_[j].count != e_[i].count) return false;
        }
        return true;
    }

private:
    Entry e_[kMaxEntries];
    int   n_;
    bool  overflow_;
};

// ---------------------------------------------------------------------------
// Host side: the absolute body, split across ticks, with its own revision
// ---------------------------------------------------------------------------
// This is deliberately the K_WORLD_FLAGS shape (HostPushFlags), because that lane has
// already paid for the two mistakes:
//
//   * There is NO "produced" count.  The header's `n` is the size of the whole
//     container, and a receiver reads entries until the body ends.  An extra count
//     would have to be patched after the entries were written, and a patched count can
//     disagree with the bytes beside it -- which is exactly the failure the flag
//     encoder was rewritten to remove.
//   * A continuation is a CURSOR ACROSS TICKS, never a fragment.  A datagram either
//     carries whole entries or it does not go out; a truncated absolute body looks
//     like a delete.
class HostPublisher {
public:
    struct Chunk {
        int  entries;    // entries written into this datagram
        int  bytes;      // bytes written, including the 6-byte header
        bool blocked;    // the next entry cannot fit even an empty datagram
    };

    HostPublisher() { Reset(); }

    void Reset()
    {
        set_.Clear();
        rev_ = 1;
        dirty_ = true;
        cursor_ = 0;
        lastSendMs_ = 0;
    }

    uint32_t Revision() const { return rev_; }
    bool     Dirty() const { return dirty_; }
    int      Cursor() const { return cursor_; }
    int      Size() const { return set_.Count(); }
    bool     Overflowed() const { return set_.Overflowed(); }

    // Re-send the whole set at the next pump without changing it.  Used after a suspend
    // load on the peer: the container here is still the truth, but the peer's copy was
    // replaced by a snapshot, so the absolute body must go out again.
    void ForceDirty() { dirty_ = true; cursor_ = 0; }

    // Adopt a freshly-enumerated container.  Returns true when it differs from the set
    // being published, which is the "on change" half of the rate rule.  A change
    // restarts the pass at entry 0: the entries already sent belong to the old set, and
    // a mixed body would not be the absolute state the header promises.
    bool Observe(const Snapshot& s)
    {
        if (set_.SameAs(s)) return false;
        set_ = s;
        dirty_ = true;
        cursor_ = 0;
        return true;
    }

    // True when a datagram is due: a change is outstanding, the 1 Hz heartbeat has
    // elapsed, or a pass is only part-sent.  The last case is a deliberate refinement
    // of HostPushFlags' timing, not of its encoding: there, a heartbeat pass advances
    // one chunk per second, which for a 40-entry inventory would leave a newly joined
    // peer without a key for many seconds.  "1 Hz" is the rate at which the whole set is
    // REFRESHED; a body already in flight should finish at tick rate.  The cursor and
    // the no-produced-count header are still exactly the flag lane's.
    bool Due(uint32_t now_ms) const
    {
        if (dirty_) return true;
        if (cursor_ > 0) return true;             // a pass is in progress
        return (uint32_t)(now_ms - lastSendMs_) >= kHeartbeatMs;
    }

    // Build one datagram from the cursor.  Does NOT advance the cursor: the caller only
    // commits after the socket accepts it, so a refused datagram is retried rather than
    // skipped (HostPushFlags' rule).
    Chunk Peek(uint8_t* body, int cap) const
    {
        Chunk c = {0, 0, false};
        wire::Writer w(body, cap);
        // `n` is the whole container size, not this datagram's share.  An empty set is
        // still a valid body: it is how the lane proves it is alive, and the peer's
        // apply is a no-op for it because it never removes.
        wire::WriteInventoryHeader(w, rev_, (uint16_t)set_.Count());
        if (!w.ok) { c.blocked = true; c.bytes = 0; return c; }

        int i = cursor_;
        while (i < set_.Count()) {
            const Entry* e = set_.At(i);
            int plen = 0;
            while (e->path[plen]) ++plen;
            // The dry size of one entry: 2-byte length + path + 4-byte count.
            int need = 2 + plen + 4;
            if (w.len + need > w.cap) break;
            wire::WriteInventoryEntry(w, e->path, e->count);
            if (!w.ok) { c.blocked = true; c.entries = 0; c.bytes = 0; return c; }
            ++i;
            ++c.entries;
        }
        c.bytes = w.len;
        if (i < set_.Count() && c.entries == 0) c.blocked = true;
        return c;
    }

    // Commit a successful send.  Returns true when that send completed a whole pass, so
    // the caller can log it and so the revision only names a body that actually went
    // out in full (the flag lane's convention).
    bool Commit(int entries, uint32_t now_ms)
    {
        if (entries < 0) return false;
        lastSendMs_ = now_ms;
        cursor_ += entries;
        if (cursor_ >= set_.Count()) {
            cursor_ = 0;
            dirty_ = false;
            ++rev_;
            if (rev_ == 0) rev_ = 1;
            return true;
        }
        return false;
    }

private:
    Snapshot set_;
    uint32_t rev_;
    bool     dirty_;
    int      cursor_;
    uint32_t lastSendMs_;
};

// ---------------------------------------------------------------------------
// Peer side: the grants the host's body implies, remembered until they land
// ---------------------------------------------------------------------------
// Why the target is stored as an ABSOLUTE count rather than the difference computed
// when the body arrived: the peer's own count can change between the body and the
// grant (a local pickup, or a grant already applied this tick).  Recomputing
// `target - local` at grant time makes a stale difference a no-op instead of an
// over-grant.  It is the same reason the wire carries absolute state and not deltas.
//
// A refusal is KEPT, not dropped.  SYNC-DESIGN's acceptance test for this lane names
// "the key is lost" as the failure, and a refused grant (asset not loaded yet, save
// object not ready, writer not resolved) is usually transient.  The item is retried
// once a second; `Failed` exists only to record the attempt so the log can distinguish
// "waiting" from "landed", never to forget.  The only ways out of the queue are
// `Done` (success or provably unnecessary) and Reset.
class GrantQueue {
public:
    // The bounds live at namespace scope with their reasons; these aliases keep the
    // call sites in the same shape as qdpending.h's IntentQueue (Queue::kMaxPerTick).
    static const int      kMaxPending = inventory::kMaxPending;
    static const int      kMaxPerTick = inventory::kMaxPerTick;
    static const uint32_t kRetryMs    = inventory::kRetryMs;

    struct Out {
        char    path[kMaxPath];
        int32_t target;    // the host's absolute count, not the difference
        int     attempt;
    };

    enum Observed {
        kQueued,     // a new grant
        kRaised,     // the host's count rose while this grant was waiting
        kLowered,    // the host's count fell; the grant is still kept and re-evaluated
        kSame,       // the same target arrived again; the retry schedule is unchanged
        kDropped,    // target <= 0: there is nothing to grant
        kOverflow,   // the table is full; the caller must log the loss
    };

    GrantQueue() { Reset(); }

    void Reset()
    {
        for (int i = 0; i < kMaxPending; ++i) {
            p_[i].used = false;
            p_[i].path[0] = 0;
            p_[i].target = 0;
            p_[i].attempt = 0;
            p_[i].lastTryMs = 0;
        }
        overflow_ = false;
    }

    // The caller has already decided the peer is probably behind (or could not read its
    // own count).  Keeping the entry unconditional in that second case is what makes
    // "cannot resolve the asset yet" a retry instead of a silent loss.
    Observed Observe(const char* path, int32_t target, uint32_t now_ms)
    {
        (void)now_ms;
        if (!path || !path[0] || target <= 0) {
            // A zero target means the host no longer holds the item.  Cancelling a
            // pending GRANT is safe; this never touches the peer's own container.
            if (path) Done(path);
            return kDropped;
        }

        int i = Find(path);
        if (i >= 0) {
            if (target > p_[i].target) {
                p_[i].target = target;
                // New work: due now rather than at the old retry deadline.
                p_[i].attempt = 0;
                p_[i].lastTryMs = 0;
                return kRaised;
            }
            if (target < p_[i].target) {
                // The host consumed some of it.  Lowering the target cannot lower the
                // peer's container; at grant time it simply becomes a smaller
                // difference, or no difference at all.
                p_[i].target = target;
                return kLowered;
            }
            return kSame;
        }

        i = Free();
        if (i < 0) { overflow_ = true; return kOverflow; }
        Pending& p = p_[i];
        p.used = true;
        if (PathCopy(p.path, kMaxPath, path) <= 0) {
            p.used = false;
            overflow_ = true;
            return kOverflow;
        }
        p.target = target;
        p.attempt = 0;
        p.lastTryMs = 0;
        return kQueued;
    }

    // Fill up to `cap` (never more than kMaxPerTick) grants that are due, counting the
    // attempt now so a caller that fails to send still cannot emit it twice in one tick.
    //
    // Work that has never been tried goes before a retry, for the same reason
    // qdpending.h's IntentQueue does: a table full of failing items must not consume
    // every slot for the rest of the session and starve the key that just arrived.  At
    // two per tick and a one-second retry window, an item that keeps failing would
    // otherwise be picked again ahead of every fresh entry behind it.
    int Due(uint32_t now_ms, Out* out, int cap)
    {
        if (!out || cap <= 0) return 0;
        if (cap > kMaxPerTick) cap = kMaxPerTick;

        int n = 0;
        for (int i = 0; i < kMaxPending && n < cap; ++i) {
            if (p_[i].used && p_[i].attempt == 0 && Take(i, now_ms, &out[n])) ++n;
        }
        for (int i = 0; i < kMaxPending && n < cap; ++i) {
            Pending& p = p_[i];
            if (!p.used || p.attempt == 0) continue;
            if ((uint32_t)(now_ms - p.lastTryMs) < kRetryMs) continue;
            if (Take(i, now_ms, &out[n])) ++n;
        }
        return n;
    }

    // The grant landed, or a fresh local count proved it is no longer needed.
    bool Done(const char* path)
    {
        int i = Find(path);
        if (i < 0) return false;
        p_[i].used = false;
        return true;
    }

    // The writer refused it.  Keep the item and its target; only restart the retry
    // clock so the next attempt is a second away rather than the next frame.
    void Failed(const char* path, uint32_t now_ms)
    {
        int i = Find(path);
        if (i < 0) return;
        p_[i].lastTryMs = now_ms;
    }

    int  Count() const
    {
        int n = 0;
        for (int i = 0; i < kMaxPending; ++i) if (p_[i].used) ++n;
        return n;
    }

    bool Empty() const { return Count() == 0; }
    bool Overflowed() const { return overflow_; }

private:
    struct Pending {
        bool     used;
        char     path[kMaxPath];
        int32_t  target;
        int      attempt;
        uint32_t lastTryMs;
    };

    bool Take(int i, uint32_t now_ms, Out* o)
    {
        Pending& p = p_[i];
        ++p.attempt;
        p.lastTryMs = now_ms;
        if (PathCopy(o->path, kMaxPath, p.path) <= 0) return false;
        o->target = p.target;
        o->attempt = p.attempt;
        return true;
    }

    int Find(const char* path) const
    {
        if (!path) return -1;
        for (int i = 0; i < kMaxPending; ++i) {
            if (p_[i].used && PathEq(p_[i].path, path)) return i;
        }
        return -1;
    }

    int Free() const
    {
        for (int i = 0; i < kMaxPending; ++i) if (!p_[i].used) return i;
        return -1;
    }

    Pending p_[kMaxPending];
    bool    overflow_;
};

} // namespace inventory
} // namespace qd
