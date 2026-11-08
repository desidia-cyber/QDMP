// qdinvintent.h -- the peer -> host item lane: a pickup the peer makes becomes an
// `INTENT kind 2` (I_GRANT_ITEM), and the host applies the part of it it has not
// already applied.
//
// WHY THIS IS A SIBLING OF qdpending.h AND NOT A REUSE OF IT
// ---------------------------------------------------------
// qdpending.h solves three problems for progression flags: ECHO suppression, bounded
// retry after a stale refusal, and BURST coalescing.  The problems are the same here,
// but two of the three mechanisms cannot be shared:
//
//   * An item is identified by a soft object path of up to kMaxPath (200) bytes.
//     qdpending's IntentQueue stores `char name[80]` because the widest flag name in
//     the chain is 80.  Forcing an item path into that buffer truncates it, and a
//     truncated path resolves to a DIFFERENT asset -- WrGrantItem would then grant the
//     wrong item, which is worse than not granting at all.
//
//   * A flag intent is ABSOLUTE (`mode 1`), so a retry is naturally idempotent: the
//     host assigns the same value again and nothing changes.  A grant is ADDITIVE:
//     WrGrantItem(SaveFunctions_C.AddItemToInventory) only adds.  Re-sending a delta
//     is therefore not idempotent, and qdpending's "re-send the queued value" would
//     double an item on the second attempt.
//
// So the payload is re-modelled as a CUMULATIVE peer contribution, and the host keeps
// a monotone ledger per path (GrantLedger, the second half of this header).  A retry
// carries the same cumulative number, the host grants only the increase, and every
// copy after the first is a no-op.  That is the one structural difference from the
// flag lane, and it is the reason this file exists.
//
// THE DELTA RULE
// --------------
// Let L be the peer's local count for a path and G the amount of L that the HOST put
// there (host -> peer grants the peer has applied).  The peer's own contribution is
//
//     own = clamp(L - G, 0)
//
// SYNC-DESIGN section 2.4's "host-owned and shared" still holds: the host is the only
// writer of the shared bank.  `own` is only what the peer ADDED beyond what the host
// itself supplied.  The peer sends `own` when it RISES, and never sends a fall.
//
// Why subtract G and not the host's published count H: H already contains the host's
// own pickups.  If the peer subtracted H, an item both players independently picked up
// would cancel to zero and one of the two gains would silently disappear.  `own` is
// additive, so both gains are kept: the peer's rise is sent once, the host applies it
// on top of whatever it already holds.  This is the policy the design does not state,
// and it is deliberately the only policy implemented here.
//
// WHAT HAPPENS ON A CONSUMABLE
// ----------------------------
// A consumption LOWERS L, so `own` falls and nothing is sent -- WrGrantItem could not
// express a removal even if the host wanted one, and the host must never re-grant an
// item it or the peer already consumed.  Two consequences, stated because a live run
// will show them:
//
//   * The peer uses an item: the host does not learn.  The peer's count is now lower
//     than the host's; the next host body makes PeerApplyInventory grant the
//     difference back, so the peer is REPLENISHED rather than reduced.  Per-player
//     consumption is not modelled, by design (SYNC-DESIGN section 2.4).
//
//   * The host uses an item: the host's count falls and the peer's does not.  The
//     peer does not send anything, because `own` is measured from G (what the host
//     gave this peer) and not from H, and G does not change when the host's own stock
//     falls.  The shared count therefore stays at the host's lower value; the peer
//     keeps its copy until it uses it.  Re-granting the host's consumed item is the
//     one thing this lane must never do.
//
// ECHO
// ----
// A host grant raises L and G by the SAME amount (PeerGrantPump calls HostGranted after
// a successful WrGrantItem), so `own` is unchanged and nothing is sent back.  A body
// that arrives while the local enumeration is between the grant and its read is also
// harmless: HostGranted raises G from the grant itself, not from the next enumeration.
// And a host grant the peer is still WAITING for (still in GrantQueue) has touched
// neither L nor G, so it cannot look like a new pickup.
//
// CONFIRMATION, AND WHAT IT CANNOT SEE
// ------------------------------------
// The host's next body is the only acknowledgement.  The peer asks the host to reach
// `hostFloor (at queue time) + unconfirmed own` and treats that count as proof.  It is
// proof in the normal case.  It is NOT proof when the host consumes the same item at
// the same time: the host's published total can then never reach the expectation even
// though the grant landed.  The intent is retried to the cap and the give-up is logged,
// but the DATA is still correct -- the host's ledger applies each increase exactly
// once, so a false give-up costs a misleading log line, never a lost or doubled item.
// A count that the host reaches on its own (an independent pickup by both players) is
// indistinguishable from the peer's contribution arriving, so it settles the intent;
// that is the one subsumed gain the additive policy accepts.
#pragma once
#include <stdint.h>
#include <string.h>

#include "qdinventory.h"   // kMaxPath, kMaxEntries, PathCopy, PathEq

namespace qd {
namespace invintent {

using inventory::kMaxPath;
using inventory::kMaxEntries;

// The longest path the INTENT body can carry.  qdwire.h's `IntentBody::name` is
// `char name[192]` and its reader TRUNCATES rather than failing (Reader::Str returns
// the real length even when it did not fit), so a longer path would arrive on the host
// as a valid, truncated, DIFFERENT asset -- exactly the aliasing the inventory header
// refuses by never truncating.  191 is the longest NUL-terminated string that buffer
// holds.  The item table still stores up to inventory::kMaxPath because a host entry of
// 200 bytes is a valid floor; only SENDING is refused.
static const int kMaxIntentPath = 191;

// The peer's per-path table.  It mirrors the host publisher's cap (kMaxEntries = 512)
// so the peer can remember every path the host could ever publish; a smaller table
// would forget a path and let its echo come back as a pickup.  The cost is about
// 512 x 250 bytes (~125 KB) in the /MT module -- the same order as the inventory
// snapshot already held for the host half, and paid once at load rather than on the
// game thread.
static const int kMaxItems = kMaxEntries;

// A chest burst is a handful of items; 64 simultaneous UNCONFIRMED intents is dozens
// of times anything the game produces in one frame.  The cap exists so a host that
// stops answering cannot grow the module's memory or its log, and a full table is
// REPORTED (never silently dropped).
static const int kMaxPending = 64;

// One intent datagram per item per tick, at most two.  The grant lane already measured
// that two ProcessEvents per frame is the hitch ceiling; here the cost is on the HOST
// (a ProcessEvent plus a read-back), so the same bound keeps a burst from arriving as
// one clump that stalls the authority's frame.
static const int kMaxPerTick = 2;

// Five attempts at 1 s.  A live round trip on the relay is tens of milliseconds and the
// host republishes at 1 Hz, so five consecutive unconfirmed copies mean the host is not
// applying this path at all (writer unavailable, save replaced, ledger refused).  More
// tries only spam; the give-up is logged so the loss is visible.
static const int kMaxAttempts = 5;

// One retry per second, matching the host's republish rate.  Any faster cannot learn
// anything new and would spend the reliable lane's slots on a path the host may be
// refusing for a structural reason.
static const uint32_t kRetryMs = 1000;

// Four per second sustained, with a burst of four: the same shape qdpending.h uses
// because the lane is not latency critical (the pickup already happened; the host's
// next body is the authority).  It keeps a session's item intents far below the state
// lanes' budget.
static const uint32_t kRefillMs = 250;
static const int      kBucket   = 4;

inline int32_t ClampCount(int64_t v)
{
    if (v < 0) return 0;
    // A count is a map value the game itself keeps in an int32; an arithmetic overflow
    // here would mean the container is already corrupt.  Saturate rather than wrap.
    if (v > 0x7FFFFFFF) return 0x7FFFFFFF;
    return (int32_t)v;
}

// ---------------------------------------------------------------------------
// Peer side: what the host has put here, what the peer has added, what is in flight
// ---------------------------------------------------------------------------
// The caller owns the clock and the socket.  The lifecycle per tick is:
//
//   BeginHostBody(rev,n) / HostItem(path,count) / EndHostBody(...)   for each body
//   HostGranted(path,amount)                                        after a grant
//   BeginLocalPass() / LocalItem(path,local) / EndLocalPass()        for the container
//   Due(now,...)                                                     then send
//
// The order matters and is the caller's contract: process the host body and the
// host -> peer grants BEFORE enumerating the local container, so `own` is computed
// from a local count that already includes everything the host has just been seen to
// hold.  provider (proxy.cpp) does exactly that.
class PickupQueue {
public:
    // Aliases so the call sites read like qdpending's IntentQueue (Queue::kMaxPerTick).
    static const int      kMaxPerTick = invintent::kMaxPerTick;
    static const int      kMaxPending = invintent::kMaxPending;
    static const int      kMaxAttempts = invintent::kMaxAttempts;
    static const uint32_t kRetryMs    = invintent::kRetryMs;

    // What the local pass decided for one path.
    enum LocalResult {
        kAbsorbed,    // the first pass after a host view: held items are host-derived
        kNoChange,    // own did not rise
        kLowered,     // own fell (consumption, or a host gain); nothing is sent
        kQueued,      // a new peer contribution
        kCoalesced,   // a larger contribution overtook the one already waiting
        kDropped,     // a pending intent was cancelled because the peer no longer holds it
        kNoBaseline,  // no complete host set has been seen yet
        kOverflow     // the item table or the pending table is full
    };

    // What one host entry did to a pending path.
    enum HostResult {
        kHostNew,         // first time this path has been seen from the host
        kHostUpdated,     // the host's high-water count rose
        kHostUnchanged,   // nothing about this path changed
        kHostConfirmed,   // the host reached exactly the count the peer's intent asked for
        kHostSuperseded,  // the host is already ahead; the intent is settled and dropped
        kHostOverflow     // the per-path table is full
    };

    struct Out {
        char     path[kMaxPath];
        int32_t  count;       // the peer's cumulative own contribution, NOT a delta
        uint32_t basedOnRev;  // the host revision this peer is acting on (diagnostics)
        int      attempt;     // 1-based send count, for the caller's log
    };

    PickupQueue() { Reset(); }

    void Reset()
    {
        for (int i = 0; i < kMaxItems; ++i) {
            Item& it = it_[i];
            it.used = false;
            it.path[0] = 0;
            it.local = 0;
            it.hostGiven = 0;
            it.own = 0;
            it.hostFloor = 0;
            it.confirmedOwn = 0;
            it.expectedHost = 0;
            it.pending = false;
            it.attempts = 0;
            it.lastSendMs = 0;
            it.lastRev = 0;
            it.passRev = 0;
            it.inLocal = false;
        }
        n_ = 0;
        haveBaseline_ = false;
        baselineTaken_ = false;
        hostRev_ = 0;
        hostRevSet_ = false;
        passRev_ = 0;
        passSet_ = false;
        passTotal_ = 0;
        passUnique_ = 0;
        bodyCounts_ = false;
        overflowHost_ = false;
        overflowItem_ = false;
        overflowPending_ = false;
        overflowPath_ = false;
        pendingCount_ = 0;
        tokens_ = kBucket;
        refillMs_ = 0;
        refillInit_ = false;
    }

    // The host's inventory publisher restarts from a new save (its rev goes back to 1).
    // The peer keeps its local/hostGiven accounting, but the host's floors, confirmations
    // and in-flight intents name a container that no longer exists.
    void ResetHostView()
    {
        for (int i = 0; i < kMaxItems; ++i) {
            if (!it_[i].used) continue;
            it_[i].hostFloor = 0;
            it_[i].confirmedOwn = 0;
            it_[i].expectedHost = 0;
            if (it_[i].pending) {
                it_[i].pending = false;
                --pendingCount_;
            }
            it_[i].attempts = 0;
            it_[i].lastSendMs = 0;
            it_[i].lastRev = 0;
            it_[i].passRev = 0;
        }
        haveBaseline_ = false;
        hostRev_ = 0;
        hostRevSet_ = false;
        passRev_ = 0;
        passSet_ = false;
        passTotal_ = 0;
        passUnique_ = 0;
        bodyCounts_ = false;
    }

    // ---- host view ---------------------------------------------------------
    // A body advertises the WHOLE container size `total` and may carry only a slice of
    // it (WIRE.md 3.8: no produced count, chunks continue on later ticks).  The peer
    // must not decide a local item is a pickup until it has seen a complete pass:
    // otherwise an item that is merely in a later chunk looks absent from the host and
    // a phantom delta goes out.  `passUnique_` counts distinct paths per revision, so a
    // mid-pass restart (the publisher restarts its cursor on a change) does not make
    // the same entries look like progress towards completeness.
    void BeginHostBody(uint32_t rev, uint16_t total)
    {
        if (!passSet_ || (int32_t)(rev - passRev_) > 0) {
            passRev_ = rev;
            passSet_ = true;
            passUnique_ = 0;
            passTotal_ = total;
            bodyCounts_ = true;
        } else if (rev == passRev_) {
            // A later slice of the same pass.  Keep the largest advertised size: a set
            // that grew mid-pass needs its new tail before it can be called complete.
            if (total > passTotal_) passTotal_ = total;
            bodyCounts_ = true;
        } else {
            // An older, out-of-order datagram.  Its entries are still valid facts about
            // the host's floors, but they must not count towards the CURRENT pass's
            // completeness: that would let a stale chunk stand in for the new set's
            // tail and declare a baseline before it has been seen.
            bodyCounts_ = false;
        }
        if (!hostRevSet_ || (int32_t)(rev - hostRev_) > 0) {
            hostRev_ = rev;
            hostRevSet_ = true;
        }
    }

    // One entry of the host's absolute body.  `count` is the host's count for the path.
    // The floor is a HIGH-WATER MARK, deliberately: the host may consume an item and
    // publish a lower count, and a peer that treated the lower count as the reference
    // would see its unchanged local copy as a surplus and grant the consumed item back.
    // Never forgetting a higher count is what makes "a consumed item is not re-granted"
    // true in both directions.  The price: a peer pickup that merely restores a count
    // the host previously held is not forwarded -- correct, because the host never
    // removed the item from the shared bank it is keeping for this peer.
    HostResult HostItem(const char* path, int32_t count)
    {
        if (!path || !path[0]) return kHostOverflow;
        Item* it = Find(path);
        HostResult r = kHostUnchanged;
        if (!it) {
            it = Insert(path);
            if (!it) { overflowHost_ = true; return kHostOverflow; }
            r = kHostNew;
        }

        if (passSet_ && bodyCounts_) {
            // Distinct paths seen in the current pass, for the completeness test.
            if (it->passRev != passRev_) {
                it->passRev = passRev_;
                ++passUnique_;
            }
        }

        if (count > it->hostFloor) {
            it->hostFloor = count;
            if (r != kHostNew) r = kHostUpdated;
        }

        // The host's body is also the answer to an intent: once the floor reaches the
        // count the peer asked for, the item is settled.  A floor ABOVE the expectation
        // means the host was already ahead (its own pickup, or a later grant from this
        // same peer) and the intent is subsumed -- log that distinctly, because it is
        // the one case where the peer's contribution does not add to the total.
        if (it->pending && it->hostFloor >= it->expectedHost) {
            it->pending = false;
            --pendingCount_;
            it->confirmedOwn = it->own;
            r = (it->hostFloor == it->expectedHost) ? kHostConfirmed : kHostSuperseded;
        }
        return r;
    }

    // Returns true on the call that completes the FIRST full pass.  `entries` is the
    // number of entries in this body; it is not needed for completeness (unique paths
    // are counted as they arrive) but is kept so a caller can log a body it parsed.
    bool EndHostBody(int entries)
    {
        (void)entries;
        if (haveBaseline_) return false;
        if (passTotal_ == 0 || passUnique_ >= (int)passTotal_) {
            haveBaseline_ = true;
            return true;
        }
        return false;
    }

    bool     HaveHostBaseline() const { return haveBaseline_; }
    uint32_t HostRevision() const { return hostRev_; }
    bool     HostOverflowed() const { return overflowHost_; }

    // ---- local view --------------------------------------------------------
    // Enumerate the peer's own InventoryContents and hand every positive entry here.
    // A path that disappears between passes is simply not visited; EndLocalPass reaps
    // any intent that is now pointless.
    void BeginLocalPass()
    {
        for (int i = 0; i < n_; ++i) it_[i].inLocal = false;
    }

    LocalResult LocalItem(const char* path, int32_t local)
    {
        if (!path || !path[0]) return kOverflow;
        // A path the intent body would truncate is refused here, not sent.  A lost
        // pickup is visible in the log; a grant of the wrong asset is not.
        int plen = 0;
        while (path[plen]) ++plen;
        if (plen > kMaxIntentPath) {
            overflowPath_ = true;
            return kOverflow;
        }
        Item* it = Find(path);
        if (!it) {
            it = Insert(path);
            if (!it) { overflowItem_ = true; return kOverflow; }
        }
        it->inLocal = true;
        it->local = local;

        // No complete host set yet: any positive value here is either the peer's own
        // pre-existing inventory or an item whose host entry is still in a later chunk.
        // Neither is a pickup.  Hold the count and send nothing.
        if (!haveBaseline_) {
            it->own = 0;
            return kNoBaseline;
        }

        // The first local pass after the host view fixes the BASELINE: whatever the
        // peer already holds is treated as host-derived.  Without this, joining a
        // session would upload the peer's entire save as a burst of grants and double
        // every item the host also remembers.  Only gains made after this instant are
        // the peer's own contribution.
        if (!baselineTaken_) {
            if (local > it->hostGiven) it->hostGiven = local;
            it->own = 0;
            it->confirmedOwn = 0;
            return kAbsorbed;
        }

        int32_t want = ClampCount((int64_t)local - (int64_t)it->hostGiven);
        if (want == it->own) return kNoChange;

        if (want > it->own) {
            it->own = want;
            // The host count that proves this contribution landed.  Adding it to the
            // floor captured at the last confirmation (not to the current floor) keeps
            // two pickups from each expecting the other's proof.
            it->expectedHost = ClampCount((int64_t)it->hostFloor +
                                          (int64_t)(it->own - it->confirmedOwn));
            if (!it->pending) {
                if (pendingCount_ >= kMaxPending) {
                    overflowPending_ = true;
                    return kOverflow;
                }
                it->pending = true;
                ++pendingCount_;
                it->attempts = 0;
                it->lastSendMs = 0;
                it->lastRev = 0;
                return kQueued;
            }
            // A larger contribution overtook the queued one.  Because the payload is
            // CUMULATIVE, the newer value contains the older; the host's ledger turns
            // the whole thing into one increment.  Mark it fresh so it does not wait
            // out the old retry clock.
            it->attempts = 0;
            it->lastSendMs = 0;
            it->lastRev = 0;
            return kCoalesced;
        }

        // want < own: the peer consumed some (or the host gave it some).  Never send a
        // fall.  If that leaves nothing beyond what the host has already confirmed,
        // the intent has no work left and is cancelled rather than retried.
        it->own = want;
        if (it->pending) {
            if (want <= it->confirmedOwn) {
                it->pending = false;
                --pendingCount_;
                return kDropped;
            }
            it->expectedHost = ClampCount((int64_t)it->hostFloor +
                                          (int64_t)(it->own - it->confirmedOwn));
        }
        return kLowered;
    }

    // Returns the number of pending intents cancelled because the peer no longer holds
    // the item at all.  The caller logs it; the queue has already settled them.
    int EndLocalPass()
    {
        int dropped = 0;
        if (haveBaseline_ && !baselineTaken_) {
            baselineTaken_ = true;
            return 0;
        }
        for (int i = 0; i < n_; ++i) {
            Item& it = it_[i];
            if (!it.used || !it.pending || it.inLocal) continue;
            it.pending = false;
            --pendingCount_;
            ++dropped;
        }
        return dropped;
    }

    // The container walk failed (the active save was replaced mid-pass).  Do not end
    // the pass: doing so on the first pass after the host view would take an EMPTY
    // baseline, and the peer's pre-existing items would then be uploaded as pickups on
    // the next successful walk.  Nothing is reaped and the baseline stays untaken.
    void AbortLocalPass()
    {
        for (int i = 0; i < n_; ++i) it_[i].inLocal = false;
    }

    // A host -> peer grant landed: `amount` units of this path are now in the peer's
    // container because the host put them there.  Raising hostGiven is what suppresses
    // the echo.  The local count is re-read on the next local pass (the caller applies
    // grants before enumerating), so it is not touched here.
    void HostGranted(const char* path, int32_t amount)
    {
        if (!path || !path[0] || amount <= 0) return;
        Item* it = Find(path);
        if (!it) {
            it = Insert(path);
            if (!it) { overflowItem_ = true; return; }
        }
        it->hostGiven = ClampCount((int64_t)it->hostGiven + amount);
    }

    // ---- what may go on the wire now ---------------------------------------
    // Fills up to `cap` (never more than kMaxPerTick) intents that are due.  Fresh
    // work goes before retries so a table full of unconfirmed items cannot starve the
    // key that was just picked up (qdpending.h's rule, and the reason a chest burst
    // still drains in order).  Each returned entry has already spent its attempt and
    // started its retry clock, so a caller whose send fails cannot emit it twice in one
    // tick.
    int Due(uint32_t now_ms, Out* out, int cap)
    {
        if (!out || cap <= 0) return 0;
        if (!haveBaseline_) return 0;              // no host view: nothing to compare to
        if (!baselineTaken_) return 0;             // the baseline pass is still running
        Refill(now_ms);
        if (cap > kMaxPerTick) cap = kMaxPerTick;

        int n = 0;
        for (int i = 0; i < n_ && n < cap && tokens_ > 0; ++i) {
            Item& it = it_[i];
            if (!it.used || !it.pending || it.attempts != 0) continue;
            if (it.own <= it.confirmedOwn) continue;
            if (Emit(it, now_ms, &out[n])) { ++n; --tokens_; }
        }
        for (int i = 0; i < n_ && n < cap && tokens_ > 0; ++i) {
            Item& it = it_[i];
            if (!it.used || !it.pending || it.attempts == 0) continue;
            if (it.attempts >= kMaxAttempts) continue;     // spent; the caller reaps it
            if ((uint32_t)(now_ms - it.lastSendMs) < kRetryMs) continue;
            if (it.own <= it.confirmedOwn) continue;
            if (Emit(it, now_ms, &out[n])) { ++n; --tokens_; }
        }
        return n;
    }

    // Drop an intent that has spent its attempts.  Separate from Due so the caller can
    // log the give-up with the path and count it failed to have confirmed.
    bool GiveUp(const char* path)
    {
        Item* it = Find(path);
        if (!it || !it->pending) return false;
        it->pending = false;
        --pendingCount_;
        return true;
    }

    int  PendingCount() const { return pendingCount_; }
    bool Empty() const { return pendingCount_ == 0; }
    bool PendingOverflowed() const { return overflowPending_; }
    bool ItemOverflowed() const { return overflowItem_; }
    bool PathOverflowed() const { return overflowPath_; }

    // Test and diagnostic access.  `PeerOwn` returns the current cumulative
    // contribution for a path (0 for an unknown path).
    int32_t PeerOwn(const char* path) const
    {
        const Item* it = Find(path);
        return it ? it->own : 0;
    }
    int32_t HostFloor(const char* path) const
    {
        const Item* it = Find(path);
        return it ? it->hostFloor : 0;
    }
    int32_t HostGiven(const char* path) const
    {
        const Item* it = Find(path);
        return it ? it->hostGiven : 0;
    }
    int Size() const { return n_; }

private:
    struct Item {
        bool     used;
        char     path[kMaxPath];
        int32_t  local;         // the last local count read
        int32_t  hostGiven;     // cumulative amount of `local` the HOST put there
        int32_t  own;           // clamp(local - hostGiven, 0)
        int32_t  hostFloor;     // highest host count ever published for this path
        int32_t  confirmedOwn;  // `own` at the last confirmation
        int32_t  expectedHost;  // hostFloor at last confirmation + unconfirmed own
        bool     pending;
        int      attempts;
        uint32_t lastSendMs;
        uint32_t lastRev;
        uint32_t passRev;       // the host pass this item was last counted in
        bool     inLocal;       // seen in the current local pass
    };

    bool Emit(Item& it, uint32_t now_ms, Out* o)
    {
        if (inventory::PathCopy(o->path, kMaxPath, it.path) <= 0) return false;
        o->count = it.own;
        o->basedOnRev = hostRev_;
        o->attempt = ++it.attempts;
        it.lastSendMs = now_ms;
        it.lastRev = hostRev_;
        return true;
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

    Item* Find(const char* path)
    {
        if (!path) return nullptr;
        for (int i = 0; i < n_; ++i) {
            if (it_[i].used && inventory::PathEq(it_[i].path, path)) return &it_[i];
        }
        return nullptr;
    }
    const Item* Find(const char* path) const
    {
        if (!path) return nullptr;
        for (int i = 0; i < n_; ++i) {
            if (it_[i].used && inventory::PathEq(it_[i].path, path)) return &it_[i];
        }
        return nullptr;
    }

    Item* Insert(const char* path)
    {
        if (n_ >= kMaxItems) return nullptr;
        Item& it = it_[n_];
        it.used = true;
        if (inventory::PathCopy(it.path, kMaxPath, path) <= 0) {
            it.used = false;
            overflowItem_ = true;
            return nullptr;
        }
        it.local = 0;
        it.hostGiven = 0;
        it.own = 0;
        it.hostFloor = 0;
        it.confirmedOwn = 0;
        it.expectedHost = 0;
        it.pending = false;
        it.attempts = 0;
        it.lastSendMs = 0;
        it.lastRev = 0;
        it.passRev = 0;
        it.inLocal = false;
        return &it_[n_++];
    }

    Item     it_[kMaxItems];
    int      n_;
    bool     haveBaseline_;
    bool     baselineTaken_;
    uint32_t hostRev_;
    bool     hostRevSet_;
    uint32_t passRev_;
    bool     passSet_;
    bool     bodyCounts_;
    uint16_t passTotal_;
    int      passUnique_;
    bool     overflowHost_;
    bool     overflowItem_;
    bool     overflowPending_;
    bool     overflowPath_;
    int      pendingCount_;
    int      tokens_;
    uint32_t refillMs_;
    bool     refillInit_;
};

// ---------------------------------------------------------------------------
// Host side: how much of a peer's cumulative contribution has already been added
// ---------------------------------------------------------------------------
// The wire's second field is the peer's CUMULATIVE own count for the path, not the
// increment to add.  This ledger remembers the highest cumulative value already
// applied and grants only the increase, which makes the lane exactly-once against any
// retry, any reordering, and any revision:
//
//   * a retransmitted datagram carries the same value -> 0 more,
//   * a logical retry after a lost body carries the same or a larger value -> only the
//     increase,
//   * an ADDITIVE gain on top of the host's own stock is still applied in full, because
//     `applied` only ever counts this peer's contributions.
//
// A lower value is a stale or declined intent (the peer consumed something after
// sending).  WrGrantItem cannot lower anything, and granting nothing is the correct
// response: the host's stock must not be raised again for an item the peer no longer
// counts.
class GrantLedger {
public:
    enum Result {
        kGranted,       // *need > 0: apply that many
        kAlreadyApplied,// the same cumulative value arrived again
        kBelowApplied,  // a lower value: refuse, never lower or re-grant
        kLedgerFull     // no path slot: refuse rather than apply untracked
    };

    GrantLedger() { Reset(); }

    void Reset()
    {
        for (int i = 0; i < kMaxItems; ++i) {
            e_[i].used = false;
            e_[i].path[0] = 0;
            e_[i].applied = 0;
        }
        n_ = 0;
        overflow_ = false;
    }

    // Peek only: the caller commits with `Commit` after WrGrantItem returns success, so
    // a refused write is retried rather than mistaken for an applied grant.
    Result Need(const char* path, int32_t peerOwn, int32_t* need)
    {
        if (need) *need = 0;
        if (!path || !path[0]) return kLedgerFull;
        if (peerOwn <= 0) return kBelowApplied;

        Entry* e = Find(path);
        int32_t applied = e ? e->applied : 0;
        if (peerOwn == applied) return kAlreadyApplied;
        if (peerOwn < applied) return kBelowApplied;

        if (!e) {
            e = Insert(path);
            if (!e) { overflow_ = true; return kLedgerFull; }
        }
        if (need) *need = peerOwn - applied;
        return kGranted;
    }

    // The write landed.  Record the cumulative value the peer asked for.  A value lower
    // than the recorded one never lowers it: the peer's own count fell after it sent.
    void Commit(const char* path, int32_t peerOwn)
    {
        if (!path || !path[0] || peerOwn <= 0) return;
        Entry* e = Find(path);
        if (!e) {
            e = Insert(path);
            if (!e) { overflow_ = true; return; }
        }
        if (peerOwn > e->applied) e->applied = peerOwn;
    }

    int32_t Applied(const char* path) const
    {
        const Entry* e = Find(path);
        return e ? e->applied : 0;
    }
    bool Overflowed() const { return overflow_; }
    int  Size() const { return n_; }

private:
    struct Entry {
        bool     used;
        char     path[kMaxPath];
        int32_t  applied;
    };

    Entry* Find(const char* path)
    {
        if (!path) return nullptr;
        for (int i = 0; i < n_; ++i) {
            if (e_[i].used && inventory::PathEq(e_[i].path, path)) return &e_[i];
        }
        return nullptr;
    }
    const Entry* Find(const char* path) const
    {
        if (!path) return nullptr;
        for (int i = 0; i < n_; ++i) {
            if (e_[i].used && inventory::PathEq(e_[i].path, path)) return &e_[i];
        }
        return nullptr;
    }
    Entry* Insert(const char* path)
    {
        if (n_ >= kMaxItems) return nullptr;
        Entry& e = e_[n_];
        e.used = true;
        if (inventory::PathCopy(e.path, kMaxPath, path) <= 0) {
            e.used = false;
            overflow_ = true;
            return nullptr;
        }
        e.applied = 0;
        return &e_[n_++];
    }

    Entry e_[kMaxItems];
    int   n_;
    bool  overflow_;
};

} // namespace invintent
} // namespace qd
