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
// A consumption LOWERS L.  It is sent as its own cumulative counter (`C`, the total the
// peer has used since the baseline) in `INTENT kind 3` (I_CONSUME_ITEM), and the host
// removes only the increase over what it has already removed (ConsumeLedger).  The
// earlier design sent nothing, which made the next host body grant the consumed item
// back and the count drift upward on every use.  The two counters are independent and
// both monotone, so a retransmit, a reorder and a lost result body all apply at most
// once:
//
//   * P (I_GRANT_ITEM) -- total the peer picked up beyond what the host gave it.
//   * C (I_CONSUME_ITEM) -- total the peer has consumed; a fall in L is the only thing
//     that raises it, and a host grant raises L and the accounting together, so a grant
//     is never mistaken for a consumption.
//
// The host owns the bank, so a peer's use lowers the shared count by exactly that many
// units.  Its own consumption is still per-player (it lowers H and is not sent); the
// peer keeps its copy until it uses it.  That remaining asymmetry is already stated in
// SYNC-DESIGN section 2.4 and is not changed here.
//
// THE GRANT TARGET WHILE A CONSUMPTION IS IN FLIGHT
// -------------------------------------------------
// H is published at 1 Hz, so the body that crosses a peer consumption on the wire can
// still advertise the pre-consumption count.  Granting `H - L` from that body would put
// the consumed item straight back.  The peer therefore grants up to
//
//     target = H - outstandingConsumption
//
// until the host's own body shows the fall (or the bounded retry gives up).  The grant
// and the consumption both proceed; the target is the count the host will hold once it
// applies the intent.  This is what makes the two sides converge exactly instead of
// suppressing grants for a whole retry window.
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
        kAbsorbed,        // the first pass after a host view: held items are host-derived
        kNoChange,        // neither contribution rose
        kLowered,         // a fall covered by an unsent pickup; nothing is sent
        kQueued,          // a new peer pickup contribution
        kCoalesced,       // a larger pickup overtook the one already waiting
        kConsumeQueued,   // a new peer consumption
        kConsumeCoalesced,// a larger consumption overtook the one already waiting
        kDropped,         // a pending intent was cancelled because the peer no longer holds it
        kNoBaseline,      // no complete host set has been seen yet
        kOverflow         // the item table or the pending table is full
    };

    // What one host entry did to a pending path.
    enum HostResult {
        kHostNew,             // first time this path has been seen from the host
        kHostUpdated,         // the host's high-water count rose
        kHostUnchanged,       // nothing about this path changed
        kHostConfirmed,       // the host reached exactly the count the peer's intent asked for
        kHostSuperseded,      // the host is already ahead; the intent is settled and dropped
        kHostConsumeConfirmed,// the host's count shows the peer's consumption landed
        kHostOverflow         // the per-path table is full
    };

    struct Out {
        char     path[kMaxPath];
        int32_t  count;       // cumulative pickup (P) or cumulative consumption (C)
        uint32_t basedOnRev;  // the host revision this peer is acting on (diagnostics)
        int      attempt;     // 1-based send count, for the caller's log
        uint8_t  intentKind;  // wire::I_GRANT_ITEM or wire::I_CONSUME_ITEM
    };

    PickupQueue() { Reset(); }

    void Reset()
    {
        for (int i = 0; i < kMaxItems; ++i) {
            Item& it = it_[i];
            it.used = false;
            it.path[0] = 0;
            it.local = 0;
            it.lastLocal = 0;
            it.hostGiven = 0;
            it.own = 0;
            it.hostFloor = 0;
            it.hostNow = 0;
            it.seenHost = false;
            it.confirmedOwn = 0;
            it.expectedHost = 0;
            it.pending = false;
            it.attempts = 0;
            it.lastSendMs = 0;
            it.lastRev = 0;
            it.passRev = 0;
            it.inLocal = false;
            it.inHostPass = false;
            it.consumed = 0;
            it.consumeConfirmed = 0;
            it.consumePending = false;
            it.consumeAttempts = 0;
            it.consumeLastSendMs = 0;
            it.consumeLastRev = 0;
            it.consumeSent = 0;
            it.hostAtConsume = 0;
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
    // and in-flight intents name a container that no longer exists.  The consumption
    // total is reset with the host ledger: replaying a use into a fresh host stock would
    // remove items the host never had.
    void ResetHostView()
    {
        for (int i = 0; i < kMaxItems; ++i) {
            if (!it_[i].used) continue;
            it_[i].hostFloor = 0;
            it_[i].hostNow = 0;
            it_[i].seenHost = false;
            it_[i].confirmedOwn = 0;
            it_[i].expectedHost = 0;
            it_[i].consumed = 0;
            it_[i].consumeConfirmed = 0;
            it_[i].hostAtConsume = 0;
            if (it_[i].pending) {
                it_[i].pending = false;
                --pendingCount_;
            }
            if (it_[i].consumePending) {
                it_[i].consumePending = false;
                --pendingCount_;
            }
            it_[i].attempts = 0;
            it_[i].lastSendMs = 0;
            it_[i].lastRev = 0;
            it_[i].consumeAttempts = 0;
            it_[i].consumeLastSendMs = 0;
            it_[i].consumeLastRev = 0;
            it_[i].consumeSent = 0;
            it_[i].passRev = 0;
            it_[i].inHostPass = false;
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
            // A new pass: a path not visited by the time this one completes is absent
            // from the host's container, which is how a zero count is expressed.
            for (int i = 0; i < n_; ++i) it_[i].inHostPass = false;
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

        // The body is also the only acknowledgement the consumption has.  `hostAtConsume`
        // is the host count the peer last saw before it used the item; a count at or below
        // that base minus the outstanding use is the host showing the fall that was asked
        // for.  A host that has not applied it yet keeps publishing the old count, so the
        // consumption stays pending and GrantTarget keeps subtracting it.
        it->hostNow = count;
        it->seenHost = true;

        if (passSet_ && bodyCounts_) {
            // Distinct paths seen in the current pass, for the completeness test.
            it->inHostPass = true;
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

        // A consumption is acknowledged by the host's count FALLING to (or below) the
        // count it held when the peer used the item, minus what is still outstanding.
        // Requiring a send first keeps a never-sent intent from being settled by the
        // body that predates it.
        bool consumeConfirmedNow = false;
        if (it->consumePending && it->consumeSent > 0) {
            int32_t outstanding = ClampCount((int64_t)it->consumed - (int64_t)it->consumeConfirmed);
            if (outstanding > 0 && count <= it->hostAtConsume - outstanding) {
                it->consumeConfirmed = it->consumed;
                it->consumePending = false;
                --pendingCount_;
                consumeConfirmedNow = true;
            }
        }
        if (consumeConfirmedNow &&
            (r == kHostNew || r == kHostUpdated || r == kHostConfirmed ||
             r == kHostSuperseded)) {
            // Both settled in one body.  The pickup news is the more informative line;
            // the consumption is still applied internally.
            return r;
        }
        if (consumeConfirmedNow) return kHostConsumeConfirmed;
        return r;
    }

    // Returns true on the call that completes the FIRST full pass.  `entries` is the
    // number of entries in this body; it is not needed for completeness (unique paths
    // are counted as they arrive) but is kept so a caller can log a body it parsed.
    //
    // A complete pass also settles any consumption for a path it did not carry: the
    // publisher never sends a zero entry, so absence from the whole set means the host
    // holds none, which is exactly what the use asked for.  Without this the last unit
    // of a consumable would retry to the cap and log a false give-up.
    bool EndHostBody(int entries)
    {
        (void)entries;
        if (!passSet_ || !bodyCounts_) return false;
        if (passTotal_ != 0 && passUnique_ < (int)passTotal_) return false;

        bool first = !haveBaseline_;
        haveBaseline_ = true;
        for (int i = 0; i < n_; ++i) {
            Item& it = it_[i];
            if (!it.used || it.inHostPass || !it.consumePending) continue;
            if (it.consumeSent <= 0) continue;
            it.consumeConfirmed = it.consumed;
            it.consumePending = false;
            --pendingCount_;
        }
        return first;
    }

    bool     HaveHostBaseline() const { return haveBaseline_; }
    uint32_t HostRevision() const { return hostRev_; }
    bool     HostOverflowed() const { return overflowHost_; }

    // ---- local view --------------------------------------------------------
    // Enumerate the peer's own InventoryContents and hand every positive entry here.
    // A path that disappears between passes is not visited; EndLocalPass feeds it a
    // zero so the last unit is seen as a consumption rather than silently ignored.
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
        return ApplyLocalChange(*it, local);
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
            if (!it.used) continue;
            if (!it.inLocal) {
                // The path vanished: its remaining units were consumed.  This is the
                // only place the LAST unit of a consumable can be seen, because the
                // enumerator hands over positive counts only.
                LocalResult r = ApplyLocalChange(it, 0);
                if (r == kDropped) ++dropped;
            }
            // A pickup for an item the peer no longer holds is pointless; the existing
            // rule reaps it.  A consumption is NOT reaped: it is exactly what a vanished
            // item means and it still has to reach the host.
            if (!it.inLocal && it.pending) {
                it.pending = false;
                --pendingCount_;
                ++dropped;
            }
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
    // the echo; advancing local/lastLocal by the same amount is what keeps the next
    // pass from reading that rise as a peer pickup.  WrGrantItem has already read the
    // container back, so the rise is a fact, not an assumption.
    void HostGranted(const char* path, int32_t amount)
    {
        if (!path || !path[0] || amount <= 0) return;
        Item* it = Find(path);
        if (!it) {
            it = Insert(path);
            if (!it) { overflowItem_ = true; return; }
        }
        it->hostGiven = ClampCount((int64_t)it->hostGiven + amount);
        it->local     = ClampCount((int64_t)it->local + amount);
        it->lastLocal = ClampCount((int64_t)it->lastLocal + amount);
    }

    // ---- what may go on the wire now ---------------------------------------
    // Fills up to `cap` (never more than kMaxPerTick) intents that are due.  Fresh
    // work goes before retries so a table full of unconfirmed items cannot starve the
    // key that was just picked up (qdpending.h's rule, and the reason a chest burst
    // still drains in order).  Each returned entry has already spent its attempt and
    // started its retry clock, so a caller whose send fails cannot emit it twice in one
    // tick.
    //
    // Pickups and consumptions are separate cumulative counters on separate intents.
    // A single pass that both picked up and used an item therefore emits two datagrams;
    // the host's two ledgers make their order irrelevant, because the net change is
    // +P - C either way.
    int Due(uint32_t now_ms, Out* out, int cap)
    {
        if (!out || cap <= 0) return 0;
        if (!haveBaseline_) return 0;              // no host view: nothing to compare to
        if (!baselineTaken_) return 0;             // the baseline pass is still running
        Refill(now_ms);
        if (cap > kMaxPerTick) cap = kMaxPerTick;

        int n = 0;
        // Fresh pickups first, then fresh consumptions: a key just picked up is the
        // latency-sensitive one, and a use is usually a step behind it.
        for (int i = 0; i < n_ && n < cap && tokens_ > 0; ++i) {
            Item& it = it_[i];
            if (!it.used || !it.pending || it.attempts != 0) continue;
            if (it.own <= it.confirmedOwn) continue;
            if (EmitPickup(it, now_ms, &out[n])) { ++n; --tokens_; }
        }
        for (int i = 0; i < n_ && n < cap && tokens_ > 0; ++i) {
            Item& it = it_[i];
            if (!it.used || !it.consumePending || it.consumeAttempts != 0) continue;
            if (it.consumed <= it.consumeConfirmed) continue;
            if (EmitConsume(it, now_ms, &out[n])) { ++n; --tokens_; }
        }
        // Then retries, in the same order.
        for (int i = 0; i < n_ && n < cap && tokens_ > 0; ++i) {
            Item& it = it_[i];
            if (!it.used || !it.pending || it.attempts == 0) continue;
            if (it.attempts >= kMaxAttempts) continue;     // spent; the caller reaps it
            if ((uint32_t)(now_ms - it.lastSendMs) < kRetryMs) continue;
            if (it.own <= it.confirmedOwn) continue;
            if (EmitPickup(it, now_ms, &out[n])) { ++n; --tokens_; }
        }
        for (int i = 0; i < n_ && n < cap && tokens_ > 0; ++i) {
            Item& it = it_[i];
            if (!it.used || !it.consumePending || it.consumeAttempts == 0) continue;
            if (it.consumeAttempts >= kMaxAttempts) continue;
            if ((uint32_t)(now_ms - it.consumeLastSendMs) < kRetryMs) continue;
            if (it.consumed <= it.consumeConfirmed) continue;
            if (EmitConsume(it, now_ms, &out[n])) { ++n; --tokens_; }
        }
        return n;
    }

    // Drop an intent that has spent its attempts.  Separate from Due so the caller can
    // log the give-up with the path and count it failed to have confirmed.  The default
    // is the pickup lane; pass the intent kind from Out for a consumption.
    bool GiveUp(const char* path) { return GiveUp(path, wire::I_GRANT_ITEM); }

    bool GiveUp(const char* path, uint8_t intentKind)
    {
        Item* it = Find(path);
        if (!it) return false;
        if (intentKind == wire::I_CONSUME_ITEM) {
            if (!it->consumePending) return false;
            it->consumePending = false;
            // The bounded retry has done all it can.  Settle the ledger against the
            // current cumulative value so the grant target stops subtracting a use the
            // host was told about five times; the reliable lane is what delivered it.
            it->consumeConfirmed = it->consumed;
            --pendingCount_;
            return true;
        }
        if (!it->pending) return false;
        it->pending = false;
        --pendingCount_;
        return true;
    }

    int  PendingCount() const { return pendingCount_; }
    bool Empty() const { return pendingCount_ == 0; }
    bool PendingOverflowed() const { return overflowPending_; }
    bool ItemOverflowed() const { return overflowItem_; }
    bool PathOverflowed() const { return overflowPath_; }

    // Test and diagnostic access.  `PeerOwn` returns the current pickup contribution
    // for a path (0 for an unknown path).
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

    // What the peer has used that the host has not yet shown in a body.  The grant
    // target is reduced by this, which is the whole fix for the replenishment drift.
    int32_t OutstandingConsume(const char* path) const
    {
        const Item* it = Find(path);
        if (!it) return 0;
        return ClampCount((int64_t)it->consumed - (int64_t)it->consumeConfirmed);
    }
    int32_t ConsumedTotal(const char* path) const
    {
        const Item* it = Find(path);
        return it ? it->consumed : 0;
    }
    bool ConsumePending(const char* path) const
    {
        const Item* it = Find(path);
        return it && it->consumePending;
    }
    // The count the peer should grant up to for this path: the host's published count,
    // less a use the host has not shown yet.  Callers recompute it at grant time so a
    // queued grant cannot undo a use made after the body that queued it.
    int32_t GrantTarget(const char* path, int32_t hostCount) const
    {
        return ClampCount((int64_t)hostCount - (int64_t)OutstandingConsume(path));
    }
    int Size() const { return n_; }

private:
    struct Item {
        bool     used;
        char     path[kMaxPath];
        int32_t  local;         // the last local count read
        int32_t  lastLocal;     // the count the last change was computed from
        int32_t  hostGiven;     // cumulative amount of `local` the HOST put there
        int32_t  own;           // clamp(local - hostGiven, 0) -- the pickup contribution
        int32_t  hostFloor;     // highest host count ever published for this path
        int32_t  hostNow;       // the host count in the newest body
        bool     seenHost;      // hostNow names a real body
        int32_t  confirmedOwn;  // `own` at the last confirmation
        int32_t  expectedHost;  // hostFloor at last confirmation + unconfirmed own
        bool     pending;
        int      attempts;
        uint32_t lastSendMs;
        uint32_t lastRev;
        uint32_t passRev;       // the host pass this item was last counted in
        bool     inLocal;       // seen in the current local pass
        bool     inHostPass;    // seen in the current (possibly chunked) host pass

        int32_t  consumed;        // cumulative units the peer has used (C)
        int32_t  consumeConfirmed;// C the host's body has acknowledged
        bool     consumePending;
        int      consumeAttempts;
        uint32_t consumeLastSendMs;
        uint32_t consumeLastRev;
        int32_t  consumeSent;     // the C value last handed to the sender
        int32_t  hostAtConsume;   // hostNow when the outstanding use began
    };

    // The one place a local count change is interpreted, shared by a present path and
    // by EndLocalPass's vanished path (local = 0).
    LocalResult ApplyLocalChange(Item& it, int32_t local)
    {
        int32_t previous = it.lastLocal;
        it.local = local;
        it.lastLocal = local;

        // No complete host set yet: any positive value here is either the peer's own
        // pre-existing inventory or an item whose host entry is still in a later chunk.
        // Neither is a pickup, and a fall is not a use yet either.
        if (!haveBaseline_) {
            it.own = 0;
            return kNoBaseline;
        }

        // The first local pass after the host view fixes the BASELINE: whatever the
        // peer already holds is treated as host-derived.  Without this, joining a
        // session would upload the peer's entire save as a burst of grants and double
        // every item the host also remembers.  Only changes made after this instant are
        // the peer's own contribution.
        if (!baselineTaken_) {
            if (local > it.hostGiven) it.hostGiven = local;
            it.own = 0;
            it.confirmedOwn = 0;
            it.consumed = 0;
            it.consumeConfirmed = 0;
            it.consumePending = false;
            return kAbsorbed;
        }

        LocalResult pickupResult = kNoChange;
        LocalResult consumeResult = kNoChange;

        int32_t fall = previous > local ? previous - local : 0;
        int32_t oldOwn = it.own;
        int32_t want = ClampCount((int64_t)local - (int64_t)it.hostGiven);

        // --- consumption: a fall that an UN SENT pickup does not already explain ---
        // Reducing a contribution the host has not been told about would remove an item
        // it never received, so that part of the fall cancels the pending pickup instead
        // (handled by the want < oldOwn branch below).  Once a pickup has been sent --
        // even if its result body was lost -- the use is a real removal on the host, and
        // the two cumulative ledgers make its arrival order irrelevant.
        if (fall > 0) {
            int32_t unsent = 0;
            if (it.pending && it.attempts == 0) {
                unsent = ClampCount((int64_t)it.own - (int64_t)it.confirmedOwn);
                if (unsent > fall) unsent = fall;
            }
            int32_t used = fall - unsent;
            if (used > 0) {
                it.consumed = ClampCount((int64_t)it.consumed + used);
                consumeResult = QueueConsume(it);
            }
        }

        if (want > oldOwn) {
            it.own = want;
            // The host count that proves this contribution landed.  Adding it to the
            // floor captured at the last confirmation (not to the current floor) keeps
            // two pickups from each expecting the other's proof.
            it.expectedHost = ClampCount((int64_t)it.hostFloor +
                                          (int64_t)(it.own - it.confirmedOwn));
            if (!it.pending) {
                if (pendingCount_ >= kMaxPending) {
                    overflowPending_ = true;
                    pickupResult = kOverflow;
                } else {
                    it.pending = true;
                    ++pendingCount_;
                    it.attempts = 0;
                    it.lastSendMs = 0;
                    it.lastRev = 0;
                    pickupResult = kQueued;
                }
            } else {
                // A larger contribution overtook the queued one.  Because the payload is
                // CUMULATIVE, the newer value contains the older; the host's ledger turns
                // the whole thing into one increment.  Mark it fresh so it does not wait
                // out the old retry clock.
                it.attempts = 0;
                it.lastSendMs = 0;
                it.lastRev = 0;
                pickupResult = kCoalesced;
            }
        } else if (want < oldOwn) {
            // The contribution shrank.  If that leaves nothing beyond what the host has
            // already confirmed, the intent has no work left and is cancelled.
            it.own = want;
            if (it.pending) {
                if (want <= it.confirmedOwn) {
                    it.pending = false;
                    --pendingCount_;
                    pickupResult = kDropped;
                } else {
                    it.expectedHost = ClampCount((int64_t)it.hostFloor +
                                                 (int64_t)(it.own - it.confirmedOwn));
                    pickupResult = kLowered;
                }
            } else {
                pickupResult = kLowered;
            }
        }

        // A use is the event the caller most needs to log, so it wins when both lanes
        // changed in one pass; both intents are queued either way.
        if (consumeResult == kConsumeQueued || consumeResult == kConsumeCoalesced ||
            consumeResult == kOverflow) {
            return consumeResult;
        }
        return pickupResult != kNoChange ? pickupResult : consumeResult;
    }

    LocalResult QueueConsume(Item& it)
    {
        if (it.consumed <= it.consumeConfirmed) return kNoChange;
        if (!it.consumePending) {
            if (pendingCount_ >= kMaxPending) {
                overflowPending_ = true;
                // There is no slot to remember the use.  Settle it against the current
                // value instead of leaving OutstandingConsume permanently non-zero: a
                // grant target silently reduced for the rest of the session is worse
                // than a reported loss.
                it.consumeConfirmed = it.consumed;
                return kOverflow;
            }
            it.consumePending = true;
            ++pendingCount_;
            it.consumeAttempts = 0;
            it.consumeLastSendMs = 0;
            it.consumeLastRev = 0;
            it.consumeSent = 0;
            // The count the host held when this use began is the base its acknowledgement
            // is measured against.
            it.hostAtConsume = it.seenHost ? it.hostNow : 0;
            return kConsumeQueued;
        }
        // A larger use overtook the queued one.  The payload is cumulative, so the newer
        // value contains the older; mark it fresh so it does not wait out the old clock.
        it.consumeAttempts = 0;
        it.consumeLastSendMs = 0;
        it.consumeLastRev = 0;
        return kConsumeCoalesced;
    }

    bool EmitPickup(Item& it, uint32_t now_ms, Out* o)
    {
        if (inventory::PathCopy(o->path, kMaxPath, it.path) <= 0) return false;
        o->count = it.own;
        o->basedOnRev = hostRev_;
        o->attempt = ++it.attempts;
        o->intentKind = wire::I_GRANT_ITEM;
        it.lastSendMs = now_ms;
        it.lastRev = hostRev_;
        return true;
    }

    bool EmitConsume(Item& it, uint32_t now_ms, Out* o)
    {
        if (inventory::PathCopy(o->path, kMaxPath, it.path) <= 0) return false;
        o->count = it.consumed;
        o->basedOnRev = hostRev_;
        o->attempt = ++it.consumeAttempts;
        o->intentKind = wire::I_CONSUME_ITEM;
        it.consumeLastSendMs = now_ms;
        it.consumeLastRev = hostRev_;
        it.consumeSent = it.consumed;
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
        it.lastLocal = 0;
        it.hostGiven = 0;
        it.own = 0;
        it.hostFloor = 0;
        it.hostNow = 0;
        it.seenHost = false;
        it.confirmedOwn = 0;
        it.expectedHost = 0;
        it.pending = false;
        it.attempts = 0;
        it.lastSendMs = 0;
        it.lastRev = 0;
        it.passRev = 0;
        it.inLocal = false;
        it.inHostPass = false;
        it.consumed = 0;
        it.consumeConfirmed = 0;
        it.consumePending = false;
        it.consumeAttempts = 0;
        it.consumeLastSendMs = 0;
        it.consumeLastRev = 0;
        it.consumeSent = 0;
        it.hostAtConsume = 0;
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
//
// THE SECOND AXIS: WHAT THE PEER USED
// -----------------------------------
// The same entry also remembers the highest cumulative consumption (`C`) applied for
// the path.  `NeedConsume` returns only the increase, so a retransmit, a reorder and a
// logical retry are exactly-once for a removal too.  One table holds both axes: a path
// that was only ever used still needs a slot, and the bound is the same 512 paths.
class GrantLedger {
public:
    enum Result {
        kGranted,       // *need > 0: apply that many
        kAlreadyApplied,// the same cumulative value arrived again
        kBelowApplied,  // a lower value: refuse, never lower or re-grant
        kLedgerFull     // no path slot: refuse rather than apply untracked
    };

    enum ConsumeResult {
        kRemove,              // *remove > 0: remove that many
        kConsumeAlreadyApplied,// the same cumulative use arrived again
        kConsumeBelowApplied, // a lower value: refuse, never remove twice
        kConsumeLedgerFull    // no path slot: refuse rather than remove untracked
    };

    GrantLedger() { Reset(); }

    void Reset()
    {
        for (int i = 0; i < kMaxItems; ++i) {
            e_[i].used = false;
            e_[i].path[0] = 0;
            e_[i].applied = 0;
            e_[i].appliedConsumed = 0;
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

    // The consumption axis.  `peerConsumed` is the peer's cumulative use for the path.
    ConsumeResult NeedConsume(const char* path, int32_t peerConsumed, int32_t* remove)
    {
        if (remove) *remove = 0;
        if (!path || !path[0]) return kConsumeLedgerFull;
        if (peerConsumed <= 0) return kConsumeBelowApplied;

        Entry* e = Find(path);
        int32_t applied = e ? e->appliedConsumed : 0;
        if (peerConsumed == applied) return kConsumeAlreadyApplied;
        if (peerConsumed < applied) return kConsumeBelowApplied;

        if (!e) {
            e = Insert(path);
            if (!e) { overflow_ = true; return kConsumeLedgerFull; }
        }
        if (remove) *remove = peerConsumed - applied;
        return kRemove;
    }

    void CommitConsume(const char* path, int32_t peerConsumed)
    {
        if (!path || !path[0] || peerConsumed <= 0) return;
        Entry* e = Find(path);
        if (!e) {
            e = Insert(path);
            if (!e) { overflow_ = true; return; }
        }
        if (peerConsumed > e->appliedConsumed) e->appliedConsumed = peerConsumed;
    }

    int32_t Applied(const char* path) const
    {
        const Entry* e = Find(path);
        return e ? e->applied : 0;
    }
    int32_t AppliedConsume(const char* path) const
    {
        const Entry* e = Find(path);
        return e ? e->appliedConsumed : 0;
    }
    bool Overflowed() const { return overflow_; }
    int  Size() const { return n_; }

private:
    struct Entry {
        bool     used;
        char     path[kMaxPath];
        int32_t  applied;          // highest cumulative peer pickup applied
        int32_t  appliedConsumed;  // highest cumulative peer consumption applied
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
        e.appliedConsumed = 0;
        return &e_[n_++];
    }

    Entry e_[kMaxItems];
    int   n_;
    bool  overflow_;
};

} // namespace invintent
} // namespace qd
