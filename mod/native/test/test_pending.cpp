// test_pending.cpp -- the peer's outbound flag-intent queue.
//
// These test RULES, and each one maps to a way the two saves diverge permanently:
//
//   echo       a value the host applied is sent back as an intent, and the machines
//              ping-pong for the rest of the session
//   retry      an intent refused as one revision stale is dropped, and the peer's
//              barricade is open on one machine only
//   limits     a stalled host is answered with an unbounded stream, or a full list
//              silently loses a rise
//   coalesce   one datagram per flag per frame on a lane the relay treats as reliable
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "qdpending.h"
#include "qdflags.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, name)                                                          \
    do {                                                                           \
        if (cond) { ++g_pass; }                                                    \
        else { ++g_fail; printf("FAIL %s  (%s:%d)\n", name, __FILE__, __LINE__); } \
    } while (0)

using namespace qd;
typedef pending::IntentQueue Queue;

// ===========================================================================
// observe -> due: a rise becomes exactly one datagram, then waits to be answered
// ===========================================================================
static void TestObserveAndDue()
{
    Queue q;
    Queue::Out out[Queue::kMaxPerTick];

    CHECK(q.Count() == 0, "pending/starts empty");
    CHECK(q.Observe("BrokeBarricade", 1) == Queue::kQueued, "pending/a rise is queued");
    CHECK(q.Count() == 1, "pending/count after a rise");

    int n = q.Due(1000, out, Queue::kMaxPerTick);
    CHECK(n == 1, "pending/one intent is due");
    CHECK(strcmp(out[0].name, "BrokeBarricade") == 0, "pending/the key is carried");
    CHECK(out[0].value == 1, "pending/the value is carried");
    CHECK(out[0].attempt == 1, "pending/the first send is attempt 1");

    // A sent intent is not re-sent inside the retry window.
    CHECK(q.Due(1000 + Queue::kRetryMs - 1, out, Queue::kMaxPerTick) == 0,
          "pending/no retry before kRetryMs");
    n = q.Due(1000 + Queue::kRetryMs, out, Queue::kMaxPerTick);
    CHECK(n == 1 && out[0].attempt == 2, "pending/a retry is due after kRetryMs");
}

// ===========================================================================
// echo suppression -- the host's own value must never come back
// ===========================================================================
static void TestEcho()
{
    Queue q;
    Queue::Out out[Queue::kMaxPerTick];

    q.RememberEcho("BrokeBarricade", 1);
    CHECK(q.Observe("BrokeBarricade", 1) == Queue::kEcho,
          "pending/a value the host applied is suppressed");
    CHECK(q.Count() == 0, "pending/suppressed echo queued nothing");
    CHECK(q.Due(1000, out, Queue::kMaxPerTick) == 0, "pending/suppressed echo sends nothing");

    // A repeat is still the host's value, not a new rise.
    CHECK(q.Observe("BrokeBarricade", 1) == Queue::kEcho,
          "pending/a repeat of the host value is suppressed");

    // A genuine rise past the echo is an intent.
    q.RememberEcho("CheckpointUnlockCount", 1);
    CHECK(q.Observe("CheckpointUnlockCount", 2) == Queue::kQueued,
          "pending/a value above the echo is a local rise");

    // The table is per key: remembering one key's echo does not suppress another's rise.
    CHECK(q.Observe("DragonLordDead", 2) == Queue::kQueued,
          "pending/a key with no echo is still an intent");
    CHECK(q.Observe("BrokeBarricade", 1) == Queue::kEcho,
          "pending/another key's echo does not leak");

    // A newer host value replaces the remembered one: the old value is no longer an
    // echo, the new one is.
    q.RememberEcho("EndingSeen", 1);
    q.RememberEcho("EndingSeen", 2);
    CHECK(q.EchoMatches("EndingSeen", 2), "pending/the newest echo is remembered");
    CHECK(!q.EchoMatches("EndingSeen", 1), "pending/the replaced echo is forgotten");
}

// ===========================================================================
// resolution -- equal confirms, higher supersedes, lower is not an answer
// ===========================================================================
static void TestResolution()
{
    Queue q;
    Queue::Out out[Queue::kMaxPerTick];
    int32_t asked = 0;

    CHECK(q.Applied("Nothing", 1, &asked) == Queue::kNoPending, "pending/no pending for an unknown key");

    q.Observe("FlyingSwordPickedUp", 1);
    q.Due(1000, out, Queue::kMaxPerTick);
    CHECK(q.Applied("FlyingSwordPickedUp", 1, &asked) == Queue::kConfirmed,
          "pending/an equal host value confirms");
    CHECK(asked == 1, "pending/the confirmed value is reported");
    CHECK(q.Count() == 0, "pending/a confirmed intent leaves the queue");
    CHECK(q.Applied("FlyingSwordPickedUp", 1, &asked) == Queue::kNoPending,
          "pending/confirmation is not repeatable");

    // The host already holds more than we asked: our value is implied, so the intent is
    // settled rather than retried for ever.
    asked = 0;
    q.Observe("DragonLordDead", 2);
    q.Due(2000, out, Queue::kMaxPerTick);
    CHECK(q.Applied("DragonLordDead", 3, &asked) == Queue::kSuperseded,
          "pending/a higher host value supersedes");
    CHECK(asked == 2, "pending/the superseded value is reported");
    CHECK(q.Count() == 0, "pending/a superseded intent leaves the queue");
}

// ===========================================================================
// a host that is behind does not settle the intent; it must be retried
// ===========================================================================
static void TestHostBehind()
{
    Queue q;
    Queue::Out out[Queue::kMaxPerTick];
    int32_t asked = 0;

    q.Observe("BrokeBarricade", 1);
    CHECK(q.Applied("BrokeBarricade", 0, &asked) == Queue::kNoPending,
          "pending/a lower host value is not an answer");
    CHECK(q.Count() == 1, "pending/the intent survives a lower host value");
    CHECK(q.Due(1000, out, Queue::kMaxPerTick) == 1, "pending/it is sent after the lower value");
}

// ===========================================================================
// give-up is bounded and reported; a later rise is a fresh intent
// ===========================================================================
static void TestGiveUp()
{
    Queue q;
    Queue::Out out[Queue::kMaxPerTick];

    q.Observe("Stuck", 1);
    uint32_t t = 1000;
    for (int k = 0; k < Queue::kMaxAttempts; ++k) {
        int n = q.Due(t, out, Queue::kMaxPerTick);
        CHECK(n == 1, "pending/an unconfirmed intent is retried");
        CHECK(out[0].attempt == k + 1, "pending/the attempt count rises by one");
        if (out[0].attempt >= Queue::kMaxAttempts) q.GiveUp(out[0].name);
        t += Queue::kRetryMs;
    }
    CHECK(q.Count() == 0, "pending/give-up removes the intent");
    CHECK(q.Due(t, out, Queue::kMaxPerTick) == 0, "pending/a given-up intent is not re-sent");

    // The key rising again is new information, so it gets its own budget.
    CHECK(q.Observe("Stuck", 2) == Queue::kQueued, "pending/a later rise is a fresh intent");
    CHECK(q.Due(t, out, Queue::kMaxPerTick) == 1 && out[0].attempt == 1,
          "pending/a fresh intent starts at attempt 1");
}

// ===========================================================================
// coalescing -- one entry per key, latest value wins, fresh budget
// ===========================================================================
static void TestCoalesce()
{
    Queue q;
    Queue::Out out[Queue::kMaxPerTick];

    q.Observe("CheckpointUnlockCount", 1);
    CHECK(q.Observe("CheckpointUnlockCount", 2) == Queue::kCoalesced,
          "pending/a second rise of one key coalesces");
    CHECK(q.Count() == 1, "pending/coalescing keeps one entry");
    CHECK(q.Observe("CheckpointUnlockCount", 2) == Queue::kDuplicate,
          "pending/the same value twice is not a new rise");
    CHECK(q.Due(1000, out, Queue::kMaxPerTick) == 1 && out[0].value == 2,
          "pending/the latest value is the one sent");

    // A rise that overtakes an already-sent intent replaces it and resets the attempts:
    // the host has never seen this value, so it deserves a full retry budget.
    Queue r;
    r.Observe("K", 1);
    r.Due(1000, out, Queue::kMaxPerTick);
    CHECK(r.Observe("K", 2) == Queue::kCoalesced, "pending/overtaking a sent intent coalesces");
    int n = r.Due(1000, out, Queue::kMaxPerTick);
    CHECK(n == 1 && out[0].value == 2 && out[0].attempt == 1,
          "pending/the overtaking value is sent immediately on a fresh budget");

    // A queued absolute intent is never lowered: the mirror's previous sample can sit
    // below it after a host write landed in between.
    Queue s;
    s.Observe("Lower", 5);
    CHECK(s.Observe("Lower", 3) == Queue::kDuplicate, "pending/a queued intent is never lowered");
    CHECK(s.Due(1000, out, Queue::kMaxPerTick) == 1 && out[0].value == 5,
          "pending/the queued higher value survives");
}

// ===========================================================================
// bounds -- the list is capped and a full list is reported, never silent
// ===========================================================================
static void TestBounds()
{
    Queue q;
    char name[32];
    for (int i = 0; i < Queue::kMaxPending; ++i) {
        snprintf(name, sizeof(name), "Flag%02d", i);
        CHECK(q.Observe(name, 1) == Queue::kQueued, "pending/the list fills");
    }
    CHECK(q.Count() == Queue::kMaxPending, "pending/the list is exactly kMaxPending");
    CHECK(q.Observe("OneTooMany", 1) == Queue::kOverflow,
          "pending/a full list reports the drop");
    CHECK(q.PendingOverflowed(), "pending/the overflow is visible");
    CHECK(q.Count() == Queue::kMaxPending, "pending/an overflow does not grow the list");

    // A key already waiting is still coalesced when the list is full: it is not a new
    // entry.
    CHECK(q.Observe("Flag00", 2) == Queue::kCoalesced,
          "pending/a full list can still update a key it holds");

    Queue r;
    CHECK(r.PendingOverflowed() == false, "pending/overflow flag starts clear");
}

// ===========================================================================
// rate -- a burst is spread over ticks and over time
// ===========================================================================
static void TestRate()
{
    Queue q;
    Queue::Out out[8];

    // Eight keys rise in one frame (the mirror's tick).
    for (int i = 0; i < 8; ++i) {
        char name[32];
        snprintf(name, sizeof(name), "Burst%d", i);
        q.Observe(name, 1);
    }

    // The caller may ask for more than the tick cap; it is clamped.
    int n1 = q.Due(1000, out, 8);
    CHECK(n1 == Queue::kMaxPerTick, "rate/a tick sends at most kMaxPerTick");
    int n2 = q.Due(1000, out, 8);
    CHECK(n2 == Queue::kMaxPerTick, "rate/the bucket allows a short burst");
    CHECK(q.Due(1000, out, 8) == 0, "rate/the burst stops at the bucket");

    // One token per kRefillMs after that.
    CHECK(q.Due(1000 + Queue::kRefillMs - 1, out, 8) == 0, "rate/no token before kRefillMs");
    CHECK(q.Due(1000 + Queue::kRefillMs, out, 8) == 1, "rate/one token at kRefillMs");
    CHECK(q.Due(1000 + Queue::kRefillMs * 2, out, 8) == 1, "rate/one token per kRefillMs");
}

// ===========================================================================
// ordering -- never-sent work goes before a retry
// ===========================================================================
static void TestOrdering()
{
    Queue q;
    Queue::Out out[2];

    q.Observe("Old", 1);
    CHECK(q.Due(1000, out, 2) == 1, "order/the first intent goes");
    q.Observe("New", 1);

    // Both are eligible at +kRetryMs; the fresh one must not wait behind the retry.
    int n = q.Due(1000 + Queue::kRetryMs, out, 2);
    CHECK(n == 2, "order/both are eligible");
    CHECK(strcmp(out[0].name, "New") == 0, "order/fresh work goes before a retry");
    CHECK(strcmp(out[1].name, "Old") == 0, "order/the retry follows");
}

// ===========================================================================
// reset -- a new active save must not inherit intents or echoes
// ===========================================================================
static void TestReset()
{
    Queue q;
    Queue::Out out[Queue::kMaxPerTick];

    q.Observe("K", 1);
    q.RememberEcho("E", 1);
    q.Reset();
    CHECK(q.Count() == 0, "reset/clears pending intents");
    CHECK(q.Observe("E", 1) == Queue::kQueued, "reset/clears the echo table");
    CHECK(q.Due(1000, out, Queue::kMaxPerTick) == 1, "reset/accepts a new intent");

    // The rate bucket is full again after a reset, not empty from the old session.
    Queue r;
    for (int i = 0; i < 4; ++i) {
        char name[16];
        snprintf(name, sizeof(name), "R%d", i);
        r.Observe(name, 1);
    }
    r.Reset();
    CHECK(r.Due(5000, out, Queue::kMaxPerTick) == 0, "reset/nothing pending after a reset");
    r.Observe("After", 1);
    CHECK(r.Due(5000, out, Queue::kMaxPerTick) == 1, "reset/the bucket is usable immediately");
}

// ===========================================================================
// the host revision a peer can actually be based on
// ===========================================================================
// HostPushFlags stamps a body with `g_flagsRevision` and bumps the counter only once
// the body is FULLY sent, so after any body has gone out the counter already names the
// NEXT revision.  A gate fed the counter therefore refuses every intent from a peer
// that has applied the latest body -- it is always exactly one behind.  What the proxy
// passes instead is the counter minus one, and these assertions pin that arithmetic.
static void TestHostRevisionGate()
{
    flags::FlagBank host;
    host.SetLocal("Seed", 1, true);

    uint32_t published = host.Revision();
    uint32_t counter = published + 1;      // what the host's counter becomes after a send

    CHECK(host.ApplyIntent("BrokeBarricade", 1, true, published, published),
          "rev/a peer based on the published revision is accepted");
    CHECK(!host.ApplyIntent("OneBehind", 1, true, published - 1, published),
          "rev/a peer one body behind is refused");
    CHECK(!host.ApplyIntent("CounterFed", 1, true, published, counter),
          "rev/the bumped counter would refuse a current peer (the off-by-one)");
}

// ===========================================================================
int main()
{
    printf("=== qd pending-intent tests ===\n");
    TestObserveAndDue();
    TestEcho();
    TestResolution();
    TestHostBehind();
    TestGiveUp();
    TestCoalesce();
    TestBounds();
    TestRate();
    TestOrdering();
    TestReset();
    TestHostRevisionGate();
    printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
