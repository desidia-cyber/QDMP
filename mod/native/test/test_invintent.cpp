// test_invintent.cpp -- the peer -> host item lane's two pure state machines.
//
// These test the failures that are invisible in a live session and expensive to
// diagnose there:
//
//   baseline   the peer sends its whole pre-existing inventory on join, doubling every
//              item the host already remembers
//   phantom    a host body still arriving in chunks makes an item the host already
//              holds look absent, and the peer grants it back
//   echo       a host grant is read as a local pickup and the item ping-pongs
//   double     a retried additive grant is applied twice, so one pickup becomes two
//   re-grant   a consumed item is granted again because a count fell
//   starve     a burst of pickups queues behind a retry backlog and the key the player
//              is standing at the door with never goes out
//   lower      a lower cumulative contribution is applied as a negative grant
//
// Nothing here touches the game, a socket or a clock.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "qdinvintent.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, name)                                                          \
    do {                                                                           \
        if (cond) { ++g_pass; }                                                    \
        else { ++g_fail; printf("FAIL %s  (%s:%d)\n", name, __FILE__, __LINE__); } \
    } while (0)

#define CHECK_MSG(cond, name, detail)                                              \
    do {                                                                           \
        if (cond) { ++g_pass; }                                                    \
        else { ++g_fail; printf("FAIL %s  %s  (%s:%d)\n", name, detail, __FILE__, __LINE__); } \
    } while (0)

using namespace qd;
typedef invintent::PickupQueue Q;
typedef invintent::GrantLedger Ledger;

// ===========================================================================
// helpers
// ===========================================================================
struct Row {
    const char* path;
    int32_t     count;
};

// One complete host body: header, every entry, then the end.  A single body whose
// advertised total equals its entry count is what the publisher produces when the set
// fits one datagram.
static void HostBody(Q& q, uint32_t rev, const Row* rows, int n, bool* completed = nullptr)
{
    q.BeginHostBody(rev, (uint16_t)n);
    for (int i = 0; i < n; ++i) q.HostItem(rows[i].path, rows[i].count);
    bool done = q.EndHostBody(n);
    if (completed) *completed = done;
}

// Establish the host view (one empty body) and take the first local baseline pass.
// The order matches proxy.cpp: the body is processed, then the container is walked.
static void EstablishEmpty(Q& q)
{
    q.BeginHostBody(1, 0);
    CHECK(q.EndHostBody(0), "setup/an empty host body completes the baseline");
    q.BeginLocalPass();
    CHECK(q.EndLocalPass() == 0, "setup/an empty local pass is quiet");
    CHECK(q.Due(0, nullptr, 0) == 0, "setup/nothing is due with no local work");
}

// Establish the host view with `rows`, then absorb the same values as the local
// baseline (the normal state right after joining: the peer already holds what the host
// just told it to hold).
static void EstablishWith(Q& q, uint32_t rev, const Row* rows, int n)
{
    HostBody(q, rev, rows, n);
    q.BeginLocalPass();
    for (int i = 0; i < n; ++i) q.LocalItem(rows[i].path, rows[i].count);
    q.EndLocalPass();
}

static Q::Out g_out[8];

// ===========================================================================
// baseline -- nothing is a pickup until there is a host view and a local baseline
// ===========================================================================
static void TestBaselineGating()
{
    Q q;
    q.BeginLocalPass();
    CHECK(q.LocalItem("/Game/A.A", 3) == Q::kNoBaseline,
          "baseline/a local item before any host body is not a pickup");
    q.EndLocalPass();
    CHECK(q.Due(0, g_out, 8) == 0, "baseline/nothing is due before the host view");

    q.BeginHostBody(1, 0);
    CHECK(q.EndHostBody(0), "baseline/a complete empty body establishes the view");
    CHECK(q.HaveHostBaseline(), "baseline/the view is visible");
    CHECK(q.HostRevision() == 1, "baseline/the revision is carried");
}

static void TestPreExistingIsAbsorbed()
{
    Q q;
    q.BeginHostBody(1, 0);
    q.EndHostBody(0);

    // The peer joins with three of its own.  Uploading them would double whatever the
    // host already holds, so the FIRST local pass after the host view treats them as
    // host-derived.  (No empty baseline pass here: that is the point of the test.)
    q.BeginLocalPass();
    CHECK(q.LocalItem("/Game/A.A", 3) == Q::kAbsorbed, "join/a held item is absorbed");
    q.EndLocalPass();
    CHECK(q.Empty(), "join/absorption queues nothing");
    CHECK(q.Due(0, g_out, 8) == 0, "join/absorption sends nothing");
    CHECK(q.PeerOwn("/Game/A.A") == 0, "join/own is zero after absorption");

    // Only a later rise is the peer's contribution.
    q.BeginLocalPass();
    CHECK(q.LocalItem("/Game/A.A", 4) == Q::kQueued, "join/a later rise is a pickup");
    q.EndLocalPass();
    int n = q.Due(0, g_out, 8);
    CHECK(n == 1, "join/one pickup is due");
    CHECK(strcmp(g_out[0].path, "/Game/A.A") == 0, "join/the path is carried");
    CHECK(g_out[0].count == 1, "join/only the increase is sent");
}

// ===========================================================================
// echo -- what the host granted is never sent back
// ===========================================================================
static void TestEchoSuppressed()
{
    Row rows[] = { {"/Game/Items/Key.A", 5} };
    Q q;
    EstablishWith(q, 1, rows, 1);
    CHECK(q.HostGiven("/Game/Items/Key.A") == 5, "echo/the baseline is host-derived");

    // The host's body rises to 6 and the peer grants itself the difference.  The local
    // count rises by exactly the amount the host put there.
    q.HostGranted("/Game/Items/Key.A", 1);
    q.BeginLocalPass();
    CHECK(q.LocalItem("/Game/Items/Key.A", 6) == Q::kNoChange,
          "echo/a granted rise is not a pickup");
    q.EndLocalPass();
    CHECK(q.Empty(), "echo/no intent is queued");
    CHECK(q.Due(0, g_out, 8) == 0, "echo/nothing is sent back");

    // A real pickup on top of the grant is still seen.
    q.BeginLocalPass();
    CHECK(q.LocalItem("/Game/Items/Key.A", 7) == Q::kQueued,
          "echo/a pickup beyond the grant is queued");
    q.EndLocalPass();
    CHECK(q.Due(0, g_out, 8) == 1 && g_out[0].count == 1,
          "echo/only the genuine surplus is sent");
}

// A host grant the peer is still WAITING for (it has not been applied yet) must not
// change the local accounting at all.
static void TestWaitingGrantIsNotAPickup()
{
    Row rows[] = { {"/Game/Items/Key.A", 1} };
    Q q;
    EstablishWith(q, 1, rows, 1);

    // The host body says 2; PeerApplyInventory has queued a grant but nothing has been
    // written yet, so the local count is still 1.
    q.HostItem("/Game/Items/Key.A", 2);
    q.BeginLocalPass();
    CHECK(q.LocalItem("/Game/Items/Key.A", 1) == Q::kNoChange,
          "waiting/a pending host grant is not a peer pickup");
    q.EndLocalPass();
    CHECK(q.Due(0, g_out, 8) == 0, "waiting/nothing is sent");

    // The grant lands.
    q.HostGranted("/Game/Items/Key.A", 1);
    q.BeginLocalPass();
    CHECK(q.LocalItem("/Game/Items/Key.A", 2) == Q::kNoChange,
          "waiting/the applied grant is an echo");
    q.EndLocalPass();
    CHECK(q.Due(0, g_out, 8) == 0, "waiting/still nothing is sent");
}

// ===========================================================================
// consumption -- never sent, never re-granted
// ===========================================================================
static void TestPeerConsumptionNotSent()
{
    Row rows[] = { {"/Game/Items/Potion.Potion", 5} };
    Q q;
    EstablishWith(q, 1, rows, 1);

    q.BeginLocalPass();
    CHECK(q.LocalItem("/Game/Items/Potion.Potion", 4) == Q::kNoChange,
          "consume/a peer consumption never raises own (it is already zero)");
    q.EndLocalPass();
    CHECK(q.Empty(), "consume/nothing is queued");
    CHECK(q.Due(0, g_out, 8) == 0, "consume/nothing is sent");

    // The next host body replenishes it through GrantQueue, not through this lane.  The
    // grant accounting must not turn that into a new contribution.
    q.HostGranted("/Game/Items/Potion.Potion", 1);
    q.BeginLocalPass();
    CHECK(q.LocalItem("/Game/Items/Potion.Potion", 5) == Q::kNoChange,
          "consume/the replenishment is an echo");
    q.EndLocalPass();
    CHECK(q.Due(0, g_out, 8) == 0, "consume/the replenishment is not sent");
}

static void TestHostConsumptionNotRegranted()
{
    Row rows[] = { {"/Game/Items/Potion.Potion", 5} };
    Q q;
    EstablishWith(q, 1, rows, 1);

    // The host consumed one and publishes 4.  The floor is a high-water mark, so the
    // peer's unchanged count is not a surplus.
    CHECK(q.HostItem("/Game/Items/Potion.Potion", 4) != Q::kHostOverflow,
          "hostconsume/a lower host count is accepted");
    CHECK(q.HostFloor("/Game/Items/Potion.Potion") == 5,
          "hostconsume/the floor does not fall");
    q.BeginLocalPass();
    CHECK(q.LocalItem("/Game/Items/Potion.Potion", 5) == Q::kNoChange,
          "hostconsume/the host's consumption is not a peer pickup");
    q.EndLocalPass();
    CHECK(q.Empty(), "hostconsume/nothing is queued");
    CHECK(q.Due(0, g_out, 8) == 0, "hostconsume/the consumed item is not re-granted");
}

// ===========================================================================
// additive conflict -- both gains are kept
// ===========================================================================
static void TestAdditiveConflict()
{
    // The ledger counts only the PEER's contributions.  The host's own count is
    // irrelevant, so an item both players picked up is added twice -- once by each
    // player's own action.
    Ledger g;
    int32_t need = 0;
    CHECK(g.Need("/Game/Items/Key.A", 1, &need) == Ledger::kGranted && need == 1,
          "conflict/the peer's one pickup is granted once");
    g.Commit("/Game/Items/Key.A", 1);

    // A retry of the same cumulative value is a no-op.  This is what makes an additive
    // wire field safe on a reliable lane whose retransmission is a new datagram.
    CHECK(g.Need("/Game/Items/Key.A", 1, &need) == Ledger::kAlreadyApplied,
          "conflict/a retry of the same value is a no-op");

    // The peer picks up a second one.  The host's own pickup is not in this ledger
    // anywhere, so the peer's second gain is applied in full.
    CHECK(g.Need("/Game/Items/Key.A", 2, &need) == Ledger::kGranted && need == 1,
          "conflict/the second peer pickup is a separate increment");
    g.Commit("/Game/Items/Key.A", 2);

    // A later, lower value is a refusal, never a negative grant.
    CHECK(g.Need("/Game/Items/Key.A", 1, &need) == Ledger::kBelowApplied,
          "conflict/a lower cumulative value is refused");
    CHECK(g.Applied("/Game/Items/Key.A") == 2, "conflict/the applied value never falls");

    // A write that was refused is NOT committed; the retry must still be offered.
    Ledger h;
    CHECK(h.Need("/Game/Items/Lamp.Lamp", 3, &need) == Ledger::kGranted && need == 3,
          "conflict/a failed write is offered once");
    CHECK(h.Need("/Game/Items/Lamp.Lamp", 3, &need) == Ledger::kGranted && need == 3,
          "conflict/an uncommitted write is offered again");
    h.Commit("/Game/Items/Lamp.Lamp", 3);
    CHECK(h.Need("/Game/Items/Lamp.Lamp", 3, &need) == Ledger::kAlreadyApplied,
          "conflict/commit settles it");
}

// A walk that fails after beginning a pass must not consume the baseline.  Consuming it
// empty is how a join uploads the peer's whole save on the next walk.
static void TestAbortedPassKeepsBaseline()
{
    Q q;
    q.BeginHostBody(1, 0);
    q.EndHostBody(0);                       // the host view is complete

    q.BeginLocalPass();
    q.LocalItem("/Game/A.A", 3);            // would be absorbed if the pass ended here
    q.AbortLocalPass();                     // the container walk failed

    q.BeginLocalPass();
    CHECK(q.LocalItem("/Game/A.A", 3) == Q::kAbsorbed,
          "abort/the baseline survived the failed walk");
    q.EndLocalPass();
    CHECK(q.Empty(), "abort/nothing is uploaded after the failed walk");
    CHECK(q.Due(0, g_out, 8) == 0, "abort/nothing is sent after the failed walk");
}

// ===========================================================================
// retry -- cumulative payloads make a logical retry exactly-once
// ===========================================================================
static void TestRetryIsExactlyOnce()
{
    Q q;
    EstablishEmpty(q);

    q.BeginLocalPass();
    q.LocalItem("/Game/Items/Key.A", 1);
    q.EndLocalPass();

    CHECK(q.Due(0, g_out, 8) == 1, "retry/the first attempt goes out");
    CHECK(g_out[0].count == 1 && g_out[0].attempt == 1, "retry/the cumulative value");

    Ledger g;
    int32_t need = 0;
    CHECK(g.Need("/Game/Items/Key.A", g_out[0].count, &need) == Ledger::kGranted && need == 1,
          "retry/the host applies it once");
    g.Commit("/Game/Items/Key.A", g_out[0].count);

    // The host's result body was lost, so the peer retries the same cumulative value.
    CHECK(q.Due(Q::kRetryMs - 1, g_out, 8) == 0, "retry/not before the retry window");
    CHECK(q.Due(Q::kRetryMs, g_out, 8) == 1, "retry/the retry is due");
    CHECK(g_out[0].count == 1, "retry/the same cumulative value is retried");
    CHECK(g.Need("/Game/Items/Key.A", g_out[0].count, &need) == Ledger::kAlreadyApplied,
          "retry/the host applies nothing the second time");
}

// ===========================================================================
// confirmation and supersede
// ===========================================================================
static void TestConfirmation()
{
    Q q;
    EstablishEmpty(q);

    q.BeginLocalPass();
    q.LocalItem("/Game/Items/Key.A", 1);
    q.EndLocalPass();
    CHECK(q.Due(0, g_out, 8) == 1, "confirm/the intent is sent");
    CHECK(q.PendingCount() == 1, "confirm/it is pending");

    // The host applied it and published the result.
    Q::HostResult h = q.HostItem("/Game/Items/Key.A", 1);
    CHECK(h == Q::kHostConfirmed, "confirm/the host reaching the asked count confirms");
    CHECK(q.Empty(), "confirm/the pending intent is dropped");
    CHECK(q.Due(1000, g_out, 8) == 0, "confirm/nothing is retried after confirmation");

    // A second pickup expects the host floor to rise by one again.
    q.BeginLocalPass();
    q.LocalItem("/Game/Items/Key.A", 2);
    q.EndLocalPass();
    CHECK(q.Due(1000, g_out, 8) == 1 && g_out[0].count == 2,
          "confirm/the second contribution is cumulative");

    // The host is already ahead (it picked one up itself): the intent is subsumed.
    h = q.HostItem("/Game/Items/Key.A", 5);
    CHECK(h == Q::kHostSuperseded, "confirm/a host count above the ask supersedes it");
    CHECK(q.Empty(), "confirm/supersede drops the intent");
}

// ===========================================================================
// coalescing and bursts
// ===========================================================================
static void TestCoalescePerPath()
{
    Q q;
    EstablishEmpty(q);

    q.BeginLocalPass();
    CHECK(q.LocalItem("/Game/Items/Key.A", 1) == Q::kQueued, "coalesce/first pickup");
    CHECK(q.LocalItem("/Game/Items/Key.A", 3) == Q::kCoalesced,
          "coalesce/a larger pickup overtakes the queued one");
    CHECK(q.PendingCount() == 1, "coalesce/one pending intent per path");
    q.EndLocalPass();

    CHECK(q.Due(0, g_out, 8) == 1, "coalesce/one datagram");
    CHECK(g_out[0].count == 3, "coalesce/the cumulative value wins");
}

static void TestBurstIsBoundedAndFair()
{
    Q q;
    EstablishEmpty(q);

    char paths[5][inventory::kMaxPath];
    q.BeginLocalPass();
    for (int i = 0; i < 5; ++i) {
        snprintf(paths[i], sizeof(paths[i]), "/Game/Items/Chest/Chest_%d.Chest_%d", i, i);
        CHECK(q.LocalItem(paths[i], 1) == Q::kQueued, "burst/five pickups queue");
    }
    q.EndLocalPass();
    CHECK(q.PendingCount() == 5, "burst/all five wait");

    // Two datagrams per tick, and no more than the token bucket.
    CHECK(q.Due(0, g_out, 8) == Q::kMaxPerTick, "burst/at most kMaxPerTick per tick");
    CHECK(q.Due(1, g_out, 8) == Q::kMaxPerTick, "burst/the next tick takes two more");
    CHECK(q.Due(2, g_out, 8) == 0, "burst/the bucket stops a clump");
    CHECK(q.Due(250, g_out, 8) == 1, "burst/the bucket refills and the fifth goes");
    CHECK(q.PendingCount() == 5, "burst/all five are still pending (none confirmed)");

    // Every path is still held at 1.  One pass visits all five (the real walk does a
    // single enumeration; five separate passes would reap the four each one does not
    // visit).  Nothing new is queued.
    q.BeginLocalPass();
    for (int i = 0; i < 5; ++i) (void)q.LocalItem(paths[i], 1);
    q.EndLocalPass();
    CHECK(q.PendingCount() == 5, "burst/a quiet pass keeps all five pending");

    // At the retry deadline, add a fresh pickup.  The pass still visits every held
    // item (the real walk does), and the new one must go before the retries.
    q.BeginLocalPass();
    for (int i = 0; i < 5; ++i) (void)q.LocalItem(paths[i], 1);
    CHECK(q.LocalItem("/Game/Items/Chest/New.New", 1) == Q::kQueued,
          "burst/a fresh pickup joins a retry backlog");
    q.EndLocalPass();

    int n = q.Due(Q::kRetryMs, g_out, 2);
    CHECK(n == 2, "burst/two go at the retry deadline");
    CHECK(strcmp(g_out[0].path, "/Game/Items/Chest/New.New") == 0,
          "burst/fresh work is not starved by the retry backlog");
}

static void TestGiveUp()
{
    Q q;
    EstablishEmpty(q);
    q.BeginLocalPass();
    q.LocalItem("/Game/Items/Key.A", 1);
    q.EndLocalPass();

    for (int i = 1; i <= Q::kMaxAttempts; ++i) {
        CHECK(q.Due((uint32_t)(i - 1) * Q::kRetryMs, g_out, 8) == 1,
              "giveup/every attempt before the cap is sent");
        CHECK(g_out[0].attempt == i, "giveup/the attempt number climbs");
    }
    // The next window sends nothing: the attempts are spent and the caller reaps it.
    CHECK(q.Due((uint32_t)(Q::kMaxAttempts) * Q::kRetryMs, g_out, 8) == 0,
          "giveup/a spent intent is not retried again");
    CHECK(q.GiveUp("/Game/Items/Key.A"), "giveup/the caller drops it");
    CHECK(q.Empty(), "giveup/the queue is empty");
    CHECK(!q.GiveUp("/Game/Items/Key.A"), "giveup/dropping twice is a no-op");
}

// ===========================================================================
// chunked host bodies
// ===========================================================================
static void TestChunkedBaseline()
{
    Q q;
    q.BeginHostBody(1, 3);
    q.HostItem("/Game/A.A", 5);
    q.HostItem("/Game/B.B", 5);
    CHECK(!q.EndHostBody(2), "chunk/two of three entries is not a complete view");
    CHECK(!q.HaveHostBaseline(), "chunk/the baseline is not established yet");

    q.BeginLocalPass();
    CHECK(q.LocalItem("/Game/A.A", 5) == Q::kNoBaseline,
          "chunk/no delta is computed from a partial view");
    q.EndLocalPass();
    CHECK(q.Due(0, g_out, 8) == 0, "chunk/nothing is sent from a partial view");

    q.BeginHostBody(1, 3);
    q.HostItem("/Game/C.C", 5);
    CHECK(q.EndHostBody(1), "chunk/the third entry completes the pass");
    CHECK(q.HaveHostBaseline(), "chunk/the view is complete");

    q.BeginLocalPass();
    CHECK(q.LocalItem("/Game/A.A", 5) == Q::kAbsorbed, "chunk/the baseline absorbs A");
    CHECK(q.LocalItem("/Game/B.B", 5) == Q::kAbsorbed, "chunk/the baseline absorbs B");
    CHECK(q.LocalItem("/Game/C.C", 5) == Q::kAbsorbed, "chunk/the baseline absorbs C");
    q.EndLocalPass();
    CHECK(q.Empty(), "chunk/nothing is queued");
}

static void TestOldBodyDoesNotCompleteNewPass()
{
    Q q;
    // rev 1 carries two of a three-entry set; the third chunk was lost.
    q.BeginHostBody(1, 3);
    q.HostItem("/Game/A.A", 1);
    q.HostItem("/Game/B.B", 1);
    CHECK(!q.EndHostBody(2), "oldpass/rev 1 is incomplete");

    // rev 2 begins and carries one entry.  The new pass has seen one of its three.
    q.BeginHostBody(2, 3);
    q.HostItem("/Game/C.C", 1);
    CHECK(!q.EndHostBody(1), "oldpass/rev 2 has one of three");

    // A late rev-1 datagram arrives with D.  Its count is a valid floor, but it must
    // not stand in for a rev-2 entry that has not been seen.
    q.BeginHostBody(1, 3);
    q.HostItem("/Game/D.D", 1);
    CHECK(!q.EndHostBody(1), "oldpass/a stale body does not stand in for the new tail");
    CHECK(!q.HaveHostBaseline(), "oldpass/the baseline is still not established");

    // The rest of rev 2 arrives.
    q.BeginHostBody(2, 3);
    q.HostItem("/Game/A.A", 1);
    q.HostItem("/Game/B.B", 1);
    CHECK(q.EndHostBody(2), "oldpass/rev 2 completes with its own entries");
    CHECK(q.HaveHostBaseline(), "oldpass/the baseline is now established");
}

static void TestPartialBodyDoesNotForget()
{
    Row rows[] = {
        {"/Game/A.A", 5}, {"/Game/B.B", 5}, {"/Game/C.C", 5},
    };
    Q q;
    EstablishWith(q, 1, rows, 3);

    // A later pass arrives as one chunk that happens to carry only A.  B and C must
    // keep their floors; otherwise the peer's unchanged copies look like pickups.
    q.BeginHostBody(2, 1);
    CHECK(q.HostItem("/Game/A.A", 5) == Q::kHostUnchanged, "partial/only A is in the body");
    q.EndHostBody(1);
    CHECK(q.HostFloor("/Game/B.B") == 5, "partial/B's floor survives a body without B");
    CHECK(q.HostFloor("/Game/C.C") == 5, "partial/C's floor survives a body without C");

    q.BeginLocalPass();
    CHECK(q.LocalItem("/Game/A.A", 5) == Q::kNoChange, "partial/A is unchanged");
    CHECK(q.LocalItem("/Game/B.B", 5) == Q::kNoChange, "partial/B is not a phantom pickup");
    CHECK(q.LocalItem("/Game/C.C", 5) == Q::kNoChange, "partial/C is not a phantom pickup");
    q.EndLocalPass();
    CHECK(q.Empty(), "partial/no phantom intent is queued");

    // A genuinely new local item is still a pickup.
    q.BeginLocalPass();
    CHECK(q.LocalItem("/Game/D.D", 1) == Q::kQueued, "partial/a new local item is queued");
    q.EndLocalPass();
}

// ===========================================================================
// revision handling and bounds
// ===========================================================================
static void TestActionRevision()
{
    Q q;
    EstablishEmpty(q);

    q.BeginLocalPass();
    q.LocalItem("/Game/Items/Key.A", 1);
    q.EndLocalPass();
    CHECK(q.Due(0, g_out, 8) == 1 && g_out[0].basedOnRev == 1,
          "rev/acts on the revision it saw");

    // A new host body advances the revision; a retry must carry the newest one.
    q.BeginHostBody(2, 0);
    q.EndHostBody(0);
    CHECK(q.Due(Q::kRetryMs, g_out, 8) == 1, "rev/the retry is due");
    CHECK(g_out[0].basedOnRev == 2, "rev/the retry carries the freshest revision");
}

static void TestPathLengthBound()
{
    // The intent body's name buffer is 192 bytes and its reader truncates.  A path of
    // exactly 191 bytes is carried; one more is refused (a truncated grant would resolve
    // a different asset).
    Q a;
    EstablishEmpty(a);
    char fits[inventory::kMaxPath + 4];
    memset(fits, 'a', sizeof(fits));
    fits[invintent::kMaxIntentPath] = 0;
    a.BeginLocalPass();
    CHECK(a.LocalItem(fits, 1) == Q::kQueued, "path/the largest intent path is accepted");
    a.EndLocalPass();

    Q b;
    EstablishEmpty(b);
    char tooLong[inventory::kMaxPath + 4];
    memset(tooLong, 'b', sizeof(tooLong));
    tooLong[invintent::kMaxIntentPath + 1] = 0;
    b.BeginLocalPass();
    CHECK(b.LocalItem(tooLong, 1) == Q::kOverflow, "path/an over-long path is refused");
    b.EndLocalPass();
    CHECK(b.PathOverflowed(), "path/the refusal is reported");
    CHECK(b.Empty(), "path/the refused path is not sent");
}

static void TestPendingBound()
{
    Q q;
    EstablishEmpty(q);

    char path[inventory::kMaxPath];
    q.BeginLocalPass();
    for (int i = 0; i < Q::kMaxPending; ++i) {
        snprintf(path, sizeof(path), "/Game/Items/Fill/Fill_%d.Fill_%d", i, i);
        CHECK(q.LocalItem(path, 1) == Q::kQueued, "pending/fill the table");
    }
    CHECK(q.LocalItem("/Game/Items/Overflow.Overflow", 1) == Q::kOverflow,
          "pending/overflow is reported");
    CHECK(q.PendingOverflowed(), "pending/the overflow flag is set");
    CHECK(q.PendingCount() == Q::kMaxPending, "pending/the count stays at the cap");
    q.EndLocalPass();
}

static void TestItemBound()
{
    Q q;
    EstablishEmpty(q);

    char path[inventory::kMaxPath];
    for (int i = 0; i < invintent::kMaxItems; ++i) {
        snprintf(path, sizeof(path), "/Game/Items/Host/Host_%d.Host_%d", i, i);
        CHECK(q.HostItem(path, 1) != Q::kHostOverflow, "items/fill the host table");
    }
    CHECK(q.HostItem("/Game/Items/OneTooMany.OneTooMany", 1) == Q::kHostOverflow,
          "items/the item table is bounded");
    CHECK(q.HostOverflowed(), "items/the overflow is reported");
}

static void TestLedgerBound()
{
    Ledger g;
    char path[inventory::kMaxPath];
    int32_t need = 0;
    for (int i = 0; i < invintent::kMaxItems; ++i) {
        snprintf(path, sizeof(path), "/Game/Items/L/L_%d.L_%d", i, i);
        CHECK(g.Need(path, 1, &need) == Ledger::kGranted, "ledger/fill");
        g.Commit(path, 1);
    }
    CHECK(g.Need("/Game/Items/OneTooMany.OneTooMany", 1, &need) == Ledger::kLedgerFull,
          "ledger/an untracked path is refused rather than applied");
    CHECK(g.Overflowed(), "ledger/the overflow is reported");
}

// A peer consumption that happens after an intent was queued must lower the pending
// contribution, not keep the stale one.
static void TestConsumptionReducesPending()
{
    Q q;
    EstablishEmpty(q);
    q.BeginLocalPass();
    Q::LocalResult r1 = q.LocalItem("/Game/Items/Key.A", 2);
    Q::LocalResult r2 = q.LocalItem("/Game/Items/Key.A", 1);
    q.EndLocalPass();
    CHECK(r1 == Q::kQueued, "reduce/the pickup is queued");
    CHECK(r2 == Q::kLowered, "reduce/the consumption lowers own");
    CHECK(q.PendingCount() == 1, "reduce/the intent is still worth sending");
    CHECK(q.Due(0, g_out, 8) == 1, "reduce/it is sent");
    CHECK(g_out[0].count == 1, "reduce/only the remaining contribution is sent");
}

// An item that vanishes from the container entirely is not visited by the walk, so
// EndLocalPass is what reaps the intent.
static void TestVanishedCancelsPending()
{
    Q q;
    EstablishEmpty(q);
    q.BeginLocalPass();
    Q::LocalResult r = q.LocalItem("/Game/Items/Key.A", 2);
    q.EndLocalPass();
    CHECK(r == Q::kQueued, "vanish/the pickup is queued");

    q.BeginLocalPass();                 // the item is gone: not visited
    int dropped = q.EndLocalPass();
    CHECK(dropped == 1, "vanish/end of pass reaps the intent");
    CHECK(q.Empty(), "vanish/the queue is empty");
    CHECK(q.Due(0, g_out, 8) == 0, "vanish/nothing stale is sent");
}

// ===========================================================================
int main()
{
    printf("=== qd inventory intent tests ===\n");
    TestBaselineGating();
    TestPreExistingIsAbsorbed();
    TestEchoSuppressed();
    TestWaitingGrantIsNotAPickup();
    TestPeerConsumptionNotSent();
    TestHostConsumptionNotRegranted();
    TestAdditiveConflict();
    TestAbortedPassKeepsBaseline();
    TestRetryIsExactlyOnce();
    TestConfirmation();
    TestCoalescePerPath();
    TestBurstIsBoundedAndFair();
    TestGiveUp();
    TestChunkedBaseline();
    TestOldBodyDoesNotCompleteNewPass();
    TestPartialBodyDoesNotForget();
    TestActionRevision();
    TestPathLengthBound();
    TestPendingBound();
    TestItemBound();
    TestLedgerBound();
    TestConsumptionReducesPending();
    TestVanishedCancelsPending();
    printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
