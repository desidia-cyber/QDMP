// qdactorstate.h -- the one-shot world-object state lane (SavableActorStates).
//
// WHAT THIS SYNCS, AND WHY IT IS NOT THE FLAG BANK
// ------------------------------------------------
// `SavableActorStates` is `TMap<FGuid,int32>` on `FS_SaveSlot_C` (measured: the class
// tag list declares a MapProperty of a 16-byte struct key and a 4-byte int value;
// SYNC-RESEARCH section A.8).  Its entries are chests, doors, breakable barricades and
// `ActivatableGroup` setters -- exactly the objects whose two independent copies make
// "both players looted the same chest" and "the door is open for me and shut for you".
//
// The flag bank next door is monotonic ("never lower") because progression only rises.
// This lane is NOT: a chest that has been opened stays open, but a door that has been
// opened can be closed again.  So the peer applies the host's value in either
// direction.  That rule is deliberately explicit -- `AbsoluteAccept` below -- rather
// than an accidental `if (v < old) skip`: the failure it is choosing against is a door
// that cannot be re-closed, and the failure it accepts is that a stale peer value can
// be reverted by the authority.
//
// THE SECOND RULE: A LOCAL CHANGE MUST NOT BE UNDONE BY A HEARTBEAT
// ----------------------------------------------------------------
// The host re-publishes the whole set at 1 Hz even when nothing changed, so a body
// arriving while the peer's intent is still in flight looks exactly like an answer --
// same GUID, same old value, a newer revision.  Treating "a newer body with a different
// value" as an answer would drop the peer's change about a second after it was made,
// which is why `IntentQueue::OnHostEntry` HOLDS the local value while an intent is
// pending.  The host either confirms it (the body carries the asked value) or the
// bounded retry gives up and the authority wins.  Both outcomes are explicit.
//
// WHAT IS PURE HERE
// -----------------
// No socket, no UObject, no allocation.  The caller supplies the clock, performs the
// six-byte map read/write and the send.  The split is the same one qdinventory.h and
// qdpending.h use, and for the same reason: the parts that go wrong silently are the
// chunk cursor, the revision, the echo and the retry, and all four can be exercised
// with no game at all (test/test_actorstate.cpp).
#pragma once
#include <stdint.h>
#include <string.h>

#include "qdwire.h"

namespace qd {
namespace actorstate {

// `wire::ActorState` is { uint32_t guid[4]; int32_t value; } -- exactly the body entry
// WIRE.md section 3.7 defines, so there is no second shape to keep in step.
typedef wire::ActorState Row;

// 512 entries.  The operator's measured save holds 7 (04-sync-surface section 2.4), but
// that is one save in one area; a level with every chest, door and barricade loaded can
// legitimately hold more.  The table is fixed (the DLL is /MT and must not allocate on
// the game thread), so the bound is a memory price: 512 x 20 B = 10 KB in the module,
// which is cheaper than a silently truncated absolute set.  Overflow is REPORTED, never
// truncated in silence.
static const int kMaxEntries = 512;

// The host refreshes the whole set at 1 Hz even when it has not changed.  A peer with no
// memory at all converges within a second, and a lost datagram costs a second rather
// than a session (WIRE.md section 4: state, not events).
static const uint32_t kHeartbeatMs = 1000;

// The container is re-read at 4 Hz on the publishing side.  A chest opening is an event,
// and waiting for the 1 Hz heartbeat would deliver the partner's state up to a second
// late; the walk is a few dozen hash-bucket reads and writes nothing.
static const uint32_t kObserveMs = 250;

inline bool GuidEq(const uint32_t a[4], const uint32_t b[4])
{
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

inline bool GuidZero(const uint32_t g[4])
{
    return (g[0] | g[1] | g[2] | g[3]) == 0;
}

inline void GuidCopy(uint32_t dst[4], const uint32_t src[4])
{
    dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = src[3];
}

// ---------------------------------------------------------------------------
// The absolute set: one row per GUID and its value
// ---------------------------------------------------------------------------
// Order is insertion order, deliberately.  The engine map's iteration order is a hash
// implementation detail; a body that depended on it would make a two-machine diff
// noisy and a golden vector meaningless.  `SameAs` is order-independent so change
// detection does not care either.
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

    // False when the table is full.  The caller must report it: the alternative is a
    // guid that exists on the host and never reaches the peer.
    bool Set(const uint32_t guid[4], int32_t value)
    {
        int i = Find(guid);
        if (i >= 0) {
            e_[i].value = value;
            return true;
        }
        if (n_ >= kMaxEntries) {
            overflow_ = true;
            return false;
        }
        GuidCopy(e_[n_].guid, guid);
        e_[n_].value = value;
        ++n_;
        return true;
    }

    int Find(const uint32_t guid[4]) const
    {
        for (int i = 0; i < n_; ++i) {
            if (GuidEq(e_[i].guid, guid)) return i;
        }
        return -1;
    }

    const Row* At(int i) const
    {
        if (i < 0 || i >= n_) return nullptr;
        return &e_[i];
    }

    const Row* Data() const { return e_; }

    // Order-independent equality.  O(n^2) is intentional: n is a few dozen and this
    // avoids a sort buffer (and therefore an allocation) on the game thread.
    bool SameAs(const Snapshot& o) const
    {
        if (n_ != o.n_) return false;
        for (int i = 0; i < n_; ++i) {
            int j = o.Find(e_[i].guid);
            if (j < 0 || o.e_[j].value != e_[i].value) return false;
        }
        return true;
    }

private:
    Row  e_[kMaxEntries];
    int  n_;
    bool overflow_;
};

// ---------------------------------------------------------------------------
// Host side: the absolute body, split across ticks, with its own revision
// ---------------------------------------------------------------------------
// Deliberately the K_WORLD_FLAGS / inventory shape, because that encoding has already
// paid for two mistakes:
//
//   * There is NO "produced" count.  The header's `n` is the size of the whole
//     container, and a receiver reads entries until the body ends.  An extra count
//     would have to be patched after the entries were written, and a patched count can
//     disagree with the bytes beside it -- exactly the failure the flag encoder was
//     rewritten to remove.
//   * A continuation is a CURSOR ACROSS TICKS, never a fragment.  A datagram either
//     carries whole entries or it does not go out; a truncated absolute body looks like
//     a delete.
class HostPublisher {
public:
    struct Chunk {
        int  entries;   // entries written into this datagram
        int  bytes;     // bytes written, including the 6-byte header
        bool blocked;   // the next entry cannot fit even in an empty datagram
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
    // elapsed, or a pass is only part-sent.  The last case is the inventory lane's
    // refinement of HostPushFlags' timing: "1 Hz" is the rate at which the whole set is
    // REFRESHED, while a body already in flight should finish at tick rate, or a large
    // set would take a second per chunk to reach a newly joined peer.
    bool Due(uint32_t now_ms) const
    {
        if (dirty_) return true;
        if (cursor_ > 0) return true;
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
        // apply is a no-op for it because this lane never removes.
        wire::WriteActorStatesHeader(w, rev_, (uint16_t)set_.Count());
        if (!w.ok) { c.blocked = true; return c; }

        int next = wire::WriteActorStatesChunk(w, set_.Data(), set_.Count(), cursor_);
        if (!w.ok) { c.blocked = true; c.bytes = 0; return c; }
        c.entries = next - cursor_;
        c.bytes = w.len;
        if (next < set_.Count() && c.entries == 0) c.blocked = true;
        return c;
    }

    // Commit a successful send.  Returns true when that send completed a whole pass, so
    // the caller can log it and so the revision only names a body that went out in full
    // (the flag lane's convention).
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
// Peer side: the local changes waiting to become INTENT kind 6
// ---------------------------------------------------------------------------
// Three failures shape this, and each is exactly qdpending.h's list with one twist:
//
//   * ECHO.  Applying the host's body writes the peer's own container, so the local
//     detector sees the host's value a few hundred milliseconds later exactly as if the
//     peer had produced it.  A value equal to what the host last published is therefore
//     never sent back.  The twist is that this lane's values toggle: an echoed value is
//     not "spent", it is "the host's current value", so the table is updated on every
//     body entry and a local change back to the host's value is treated as an echo AND
//     cancels any pending intent for that guid -- otherwise a peer that toggled twice
//     would leave a stale intent behind and the machines would trade the two values.
//
//   * RETRY.  The host's acceptance gate is `based_on_rev >= the revision it last
//     published`.  An intent can cross a broadcast already in flight and arrive one
//     revision stale, and the host then refuses it.  A refused intent stays queued and
//     is re-sent at 1 Hz with the freshest revision until the host echoes it or the
//     bounded attempt count says it will not.
//
//   * BURST.  Opening a door can touch a chest and a group setter in the same frame.
//     A key is coalesced to its latest value and the send rate is bounded per tick and
//     per second.
class IntentQueue {
public:
    // Sixteen simultaneous unacknowledged guids is several times one frame of
    // interaction, and small enough that a host which stops answering cannot grow the
    // module.  A full queue is REPORTED (kOverflow), never silently dropped.
    static const int kMaxPending = 16;

    // One echo record per guid the host can push.  The container is bounded by
    // kMaxEntries, so a table of the same size cannot forget a live guid and let its
    // echo come back as an intent.
    static const int kMaxEcho = kMaxEntries;

    // Five attempts at 1 s: a live round trip on the relay is tens of milliseconds, so
    // five consecutive refusals mean the host is not applying this guid at all (wrong
    // save, a gate this peer cannot satisfy, an actor the host has not loaded) and more
    // trying only spams.  After the give-up the host's next body wins, which is the
    // documented outcome of a lost intent.
    static const int kMaxAttempts = 5;

    static const uint32_t kRetryMs = 1000;

    // Four per second sustained, with a burst of four.  Interaction is not latency
    // sensitive, and this keeps the intent lane far below the state lanes' budget.
    static const uint32_t kRefillMs = 250;
    static const int      kBucket   = 4;

    // And no more than two datagrams in any one frame: a burst is spread over frames
    // rather than emitted as a clump.
    static const int kMaxPerTick = 2;

    struct Out {
        uint32_t guid[4];
        int32_t  value;
        int      attempt;
    };

    enum Observed {
        kQueued,      // a new intent
        kCoalesced,   // a queued guid changed again before it was confirmed
        kDuplicate,   // the same value is already waiting
        kEcho,        // the host's current value: not a local change
        kOverflow,    // the pending list is full; the caller must log the drop
    };

    enum Resolve {
        kNoPending,   // the host value may be applied
        kConfirmed,   // the host now holds the value we asked for
        kHeld,        // an intent is in flight and this body does not answer it
    };

    IntentQueue() { Reset(); }

    void Reset()
    {
        for (int i = 0; i < kMaxPending; ++i) {
            p_[i].used = false;
            p_[i].guid[0] = p_[i].guid[1] = p_[i].guid[2] = p_[i].guid[3] = 0;
            p_[i].value = 0;
            p_[i].attempts = 0;
            p_[i].lastSendMs = 0;
        }
        for (int i = 0; i < kMaxEcho; ++i) {
            e_[i].used = false;
            e_[i].guid[0] = e_[i].guid[1] = e_[i].guid[2] = e_[i].guid[3] = 0;
            e_[i].value = 0;
        }
        echoNext_ = 0;
        tokens_ = kBucket;
        refillMs_ = 0;
        refillInit_ = false;
        overflowPending_ = false;
        overflowEcho_ = false;
    }

    // ---- what the host last published -------------------------------------
    // Remembered rather than inferred: the detector sees the write, not the writer.
    void RememberEcho(const uint32_t guid[4], int32_t value)
    {
        for (int i = 0; i < kMaxEcho; ++i) {
            if (e_[i].used && GuidEq(e_[i].guid, guid)) {
                e_[i].value = value;
                return;
            }
        }
        for (int i = 0; i < kMaxEcho; ++i) {
            if (!e_[i].used) {
                e_[i].used = true;
                GuidCopy(e_[i].guid, guid);
                e_[i].value = value;
                return;
            }
        }
        // Full.  Cannot happen while the table matches the container bound, but the
        // newest value must still win if it does: dropping it is the ping-pong.
        Echo& e = e_[echoNext_++ % kMaxEcho];
        e.used = true;
        GuidCopy(e.guid, guid);
        e.value = value;
        overflowEcho_ = true;
    }

    bool EchoMatches(const uint32_t guid[4], int32_t value) const
    {
        for (int i = 0; i < kMaxEcho; ++i) {
            if (e_[i].used && e_[i].value == value && GuidEq(e_[i].guid, guid)) return true;
        }
        return false;
    }

    // ---- what the local game did -------------------------------------------
    // The caller has already established that the container value changed (the local
    // mirror keeps the previous sample).  Echo suppression happens here because only
    // this table knows which values the host put there.
    Observed Observe(const uint32_t guid[4], int32_t value)
    {
        if (EchoMatches(guid, value)) {
            // The peer is back at the host's value.  Any queued intent for this guid is
            // now stale: sending it would set the host back to a value the peer no
            // longer holds.  This is the toggle case the flag lane never had to handle.
            Cancel(guid);
            return kEcho;
        }

        int i = FindPending(guid);
        if (i >= 0) {
            if (p_[i].value == value) return kDuplicate;
            // A second value arrived before the first was confirmed: send the latest,
            // on a fresh attempt budget, rather than the one already overtaken.
            p_[i].value = value;
            p_[i].attempts = 0;
            p_[i].lastSendMs = 0;
            return kCoalesced;
        }

        i = FreePending();
        if (i < 0) { overflowPending_ = true; return kOverflow; }
        Pending& p = p_[i];
        p.used = true;
        GuidCopy(p.guid, guid);
        p.value = value;
        p.attempts = 0;
        p.lastSendMs = 0;
        return kQueued;
    }

    // ---- what the host's body now says ------------------------------------
    // Called for every entry of every body.  `shouldApply` tells the caller whether the
    // host's value may be written into the peer's container:
    //
    //   * no pending intent      -> apply.  ABSOLUTE, in either direction: this is the
    //                               door-can-close rule made explicit.
    //   * host value == asked    -> apply (it is the same value) and settle.
    //   * host value differs     -> HOLD.  This body is indistinguishable from a 1 Hz
    //                               heartbeat that left the host before the intent
    //                               arrived, so applying it would silently undo the
    //                               peer's change.  The retry resolves it: confirmation
    //                               settles it here, the attempt cap hands authority
    //                               back to the host.
    Resolve OnHostEntry(const uint32_t guid[4], int32_t value, bool* shouldApply)
    {
        if (shouldApply) *shouldApply = true;
        int i = FindPending(guid);
        if (i < 0) return kNoPending;
        if (value == p_[i].value) {
            p_[i].used = false;
            return kConfirmed;
        }
        if (shouldApply) *shouldApply = false;
        return kHeld;
    }

    // ---- what may go on the wire now ---------------------------------------
    // Fills up to `cap` (never more than kMaxPerTick) intents that are due, spending the
    // rate-limit bucket as it goes.  Each returned entry has already had its attempt
    // counted and its retry clock started, so a caller that fails to send still cannot
    // emit it twice in one tick.
    //
    // Work that has never been sent goes before a retry: a local change made while the
    // host is behind on an older guid must not wait behind that guid's retries.
    int Due(uint32_t now_ms, Out* out, int cap)
    {
        Refill(now_ms);
        if (cap > kMaxPerTick) cap = kMaxPerTick;
        int n = 0;
        while (n < cap && tokens_ > 0) {
            int i = PickFresh();
            if (i < 0) i = PickRetry(now_ms);
            if (i < 0) break;

            Pending& p = p_[i];
            ++p.attempts;
            p.lastSendMs = now_ms;
            GuidCopy(out[n].guid, p.guid);
            out[n].value = p.value;
            out[n].attempt = p.attempts;
            ++n;
            --tokens_;
        }
        return n;
    }

    // Drop an intent that has spent its attempts.  Separate from Due so the caller can
    // log the give-up with the GUID it just failed to have confirmed.
    bool GiveUp(const uint32_t guid[4])
    {
        int i = FindPending(guid);
        if (i < 0) return false;
        p_[i].used = false;
        return true;
    }

    int  Count() const
    {
        int n = 0;
        for (int i = 0; i < kMaxPending; ++i) if (p_[i].used) ++n;
        return n;
    }
    bool Empty() const { return Count() == 0; }
    bool PendingOverflowed() const { return overflowPending_; }
    bool EchoOverflowed() const { return overflowEcho_; }

private:
    struct Pending {
        bool     used;
        uint32_t guid[4];
        int32_t  value;
        int      attempts;
        uint32_t lastSendMs;
    };

    struct Echo {
        bool     used;
        uint32_t guid[4];
        int32_t  value;
    };

    bool Cancel(const uint32_t guid[4])
    {
        int i = FindPending(guid);
        if (i < 0) return false;
        p_[i].used = false;
        return true;
    }

    int FindPending(const uint32_t guid[4]) const
    {
        for (int i = 0; i < kMaxPending; ++i) {
            if (p_[i].used && GuidEq(p_[i].guid, guid)) return i;
        }
        return -1;
    }

    int FreePending() const
    {
        for (int i = 0; i < kMaxPending; ++i) if (!p_[i].used) return i;
        return -1;
    }

    int PickFresh() const
    {
        for (int i = 0; i < kMaxPending; ++i) {
            if (p_[i].used && p_[i].attempts == 0) return i;
        }
        return -1;
    }

    int PickRetry(uint32_t now_ms) const
    {
        for (int i = 0; i < kMaxPending; ++i) {
            if (!p_[i].used || p_[i].attempts == 0) continue;
            if (p_[i].attempts >= kMaxAttempts) continue;      // spent; the caller reaps it
            uint32_t age = now_ms - p_[i].lastSendMs;
            if (age >= kRetryMs) return i;
        }
        return -1;
    }

    void Refill(uint32_t now_ms)
    {
        if (!refillInit_) { refillInit_ = true; refillMs_ = now_ms; tokens_ = kBucket; return; }
        uint32_t elapsed = now_ms - refillMs_;
        if (elapsed < kRefillMs) return;
        uint32_t add = elapsed / kRefillMs;
        refillMs_ += add * kRefillMs;
        if (add >= (uint32_t)kBucket) tokens_ = kBucket;
        else {
            tokens_ += (int)add;
            if (tokens_ > kBucket) tokens_ = kBucket;
        }
    }

    Pending  p_[kMaxPending];
    Echo     e_[kMaxEcho];
    int      echoNext_;
    int      tokens_;
    uint32_t refillMs_;
    bool     refillInit_;
    bool     overflowPending_;
    bool     overflowEcho_;
};

// ---------------------------------------------------------------------------
// Peer side: the local detector
// ---------------------------------------------------------------------------
// The container is the only signal a peer's own action produces -- the game writes its
// map, not a message.  So the peer keeps the values it saw last, and every value that
// moved is handed to the queue.  An entry that disappeared is NOT an intent: this lane
// never removes on either side, and a removal observed here is the game rebuilding the
// container, not a player action.
class LocalDetector {
public:
    struct Stats {
        int changed;
        int echoes;
        int queued;
        int coalesced;
        int duplicates;
        int overflow;
    };

    LocalDetector() { Reset(); }

    // The last value this machine observed for a GUID.  The apply path asks this before
    // writing: the host sends its whole set every second, and rewriting seven unchanged
    // values per second through the game's own SavableActor setter is seven needless
    // Map_Add / Array_Set calls per second on the peer (measured on the rig: applied=7
    // every second, indefinitely).
    bool Known(const uint32_t g[4], int32_t* out) const
    {
        if (!have_) return false;
        int j = seen_.Find(g);
        if (j < 0) return false;
        if (out) *out = seen_.At(j)->value;
        return true;
    }

    void Reset()
    {
        seen_.Clear();
        have_ = false;
    }

    void Observe(const Snapshot& current, IntentQueue& q, Stats* st)
    {
        if (st) { st->changed = st->echoes = st->queued = 0; st->coalesced = st->duplicates = st->overflow = 0; }
        if (!have_) {
            // The first sample is the baseline, never a burst of intents: at join time
            // the peer's container predates the host's body, and everything in it would
            // otherwise be offered as a change.
            seen_ = current;
            have_ = true;
            return;
        }

        for (int i = 0; i < current.Count(); ++i) {
            const Row* r = current.At(i);
            int j = seen_.Find(r->guid);
            if (j >= 0 && seen_.At(j)->value == r->value) continue;
            if (st) ++st->changed;
            IntentQueue::Observed o = q.Observe(r->guid, r->value);
            switch (o) {
            case IntentQueue::kEcho:      if (st) ++st->echoes; break;
            case IntentQueue::kQueued:    if (st) ++st->queued; break;
            case IntentQueue::kCoalesced: if (st) ++st->coalesced; break;
            case IntentQueue::kDuplicate: if (st) ++st->duplicates; break;
            case IntentQueue::kOverflow:  if (st) ++st->overflow; break;
            }
            seen_.Set(r->guid, r->value);
        }
    }

    // After the caller writes a host body value into the container, record it so the
    // next 4 Hz poll does not have to discover it.  Not required for correctness -- the
    // echo table would suppress it -- but it keeps the poll's counts honest.
    //
    // Before the first sample this is deliberately a no-op.  The first suspect sample is
    // the BASELINE; turning one host-applied entry into the whole baseline would make
    // every other entry in the container look like a local change and offer the host's
    // own values back as intents.
    void Note(const uint32_t guid[4], int32_t value)
    {
        if (!have_) return;
        seen_.Set(guid, value);
    }

private:
    Snapshot seen_;
    bool     have_;
};

// ---------------------------------------------------------------------------
// The monotonicity rule, named so it cannot be mistaken for an accident
// ---------------------------------------------------------------------------
// A chest that has been opened stays open; a door that has been opened can be shut
// again.  This lane therefore applies the host's value ABSOLUTELY, in either direction.
// It does not copy the flag bank's "never lower" rule, and the one protection it takes
// -- holding a local value while its intent is pending -- is in
// `IntentQueue::OnHostEntry`, not here.
inline bool AbsoluteAccept(int32_t hostValue, int32_t localValue)
{
    (void)hostValue;
    (void)localValue;
    return true;
}

} // namespace actorstate
} // namespace qd
