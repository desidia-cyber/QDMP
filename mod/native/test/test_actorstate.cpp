// test_actorstate.cpp -- the one-shot world-object state lane's pure state machines.
//
// These test the four ways this lane can converge to the WRONG world, none of which is
// visible from a log line during a live run:
//
//   chunk      a set too large for one datagram is truncated instead of continued, so
//              the missing tail looks like objects the host has never seen
//   revision   a revision names a body that was never fully sent, so a peer that missed
//              a chunk believes it is current
//   echo       a value the host wrote is offered back as an intent, and the machines
//              toggle the same door for the rest of the session
//   held       a 1 Hz heartbeat arriving before the intent overwrites the peer's local
//              change, so the door the peer just opened silently closes again
//   absolute   a chest is treated as monotonic and a door can never be closed, or the
//              flag bank's never-lower rule is copied into a lane that must not have it
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "qdactorstate.h"

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
typedef actorstate::Snapshot     Snap;
typedef actorstate::HostPublisher Pub;
typedef actorstate::IntentQueue   Q;
typedef actorstate::LocalDetector Det;

// A deterministic GUID: a, b, c zero and d the index, so a failure names the entry.
static void GuidOf(uint32_t g[4], uint32_t i)
{
    g[0] = 0; g[1] = 0; g[2] = 0; g[3] = 0xC0DE0000u + i;
}

struct Decoded {
    uint32_t rev;
    uint16_t n;
    int      entries;
    uint32_t guid[64][4];
    int32_t  value[64];
};

// Read the body the way a receiver must: `n` is the sender's container size, and the
// number of entries actually in this datagram is whatever parses before the body ends.
static bool Decode(const uint8_t* body, int len, Decoded* d)
{
    wire::Reader r(body, len);
    d->rev = r.U32();
    d->n   = r.U16();
    d->entries = 0;
    while (r.ok && r.pos < r.len) {
        uint32_t g[4];
        r.Guid(g);
        int32_t v = r.I32();
        if (!r.ok) return false;
        if (d->entries < 64) {
            memcpy(d->guid[d->entries], g, sizeof(g));
            d->value[d->entries] = v;
        }
        ++d->entries;
    }
    return r.ok;
}

// ===========================================================================
// the set itself
// ===========================================================================
static void TestSnapshot()
{
    Snap s;
    uint32_t a[4], b[4];
    GuidOf(a, 1);
    GuidOf(b, 2);

    CHECK(s.Count() == 0, "snap/starts empty");
    CHECK(s.Set(a, 0), "snap/insert");
    CHECK(s.Set(b, 1), "snap/insert second");
    CHECK(s.Count() == 2, "snap/count");
    CHECK(s.Set(a, 5), "snap/update replaces, does not append");
    CHECK(s.Count() == 2, "snap/count unchanged by update");
    CHECK(s.At(s.Find(a))->value == 5, "snap/updated value");
    CHECK(s.Find(b) >= 0, "snap/find second");

    Snap t;
    t.Set(b, 1);
    t.Set(a, 5);
    CHECK(s.SameAs(t), "snap/equality is order independent");

    t.Set(a, 4);
    CHECK(!s.SameAs(t), "snap/a changed value is a change");
}

static void TestOverflow()
{
    Snap s;
    uint32_t g[4];
    bool all = true;
    for (int i = 0; i < actorstate::kMaxEntries; ++i) {
        GuidOf(g, (uint32_t)i);
        if (!s.Set(g, i)) all = false;
    }
    CHECK(all, "snap/fills to kMaxEntries");
    CHECK(s.Full(), "snap/reports full");
    GuidOf(g, 9999);
    CHECK(!s.Set(g, 1), "snap/refuses an entry past the cap");
    CHECK(s.Overflowed(), "snap/reports the loss rather than truncating silently");
}

// ===========================================================================
// the host publisher: chunking, revision, heartbeat
// ===========================================================================
static void TestPublisherEmpty()
{
    Pub p;
    uint8_t body[64];
    Pub::Chunk c = p.Peek(body, sizeof(body));
    CHECK(c.entries == 0 && c.bytes == 6 && !c.blocked, "pub/empty set is a valid 6-byte body");

    Decoded d;
    CHECK(Decode(body, c.bytes, &d), "pub/empty body decodes");
    CHECK(d.rev == 1 && d.n == 0 && d.entries == 0, "pub/empty header");
}

static void TestPublisherChunking()
{
    Pub p;
    Snap s;
    uint32_t g[4];
    // Ten entries and a 100-byte datagram: 6 header + 4 x 20 = 86 fits, a fifth does not.
    for (uint32_t i = 0; i < 10; ++i) { GuidOf(g, i); s.Set(g, (int32_t)i); }
    CHECK(p.Observe(s), "pub/observing a new set marks it dirty");
    CHECK(p.Due(1000), "pub/a change is due immediately");

    uint8_t body[100];
    Pub::Chunk c1 = p.Peek(body, sizeof(body));
    CHECK(c1.entries == 4, "pub/first chunk holds four whole entries");
    CHECK(!c1.blocked, "pub/first chunk is not blocked");

    Decoded d;
    CHECK(Decode(body, c1.bytes, &d), "pub/first chunk decodes");
    CHECK(d.n == 10, "pub/header n is the WHOLE container, not the chunk");
    CHECK(d.entries == 4, "pub/receiver learns the chunk size from the body end");

    // Peek must not advance: a datagram the socket refuses is retried, not skipped.
    Pub::Chunk c1b = p.Peek(body, sizeof(body));
    CHECK(c1b.entries == 4 && c1b.bytes == c1.bytes, "pub/Peek does not move the cursor");

    // Complete the pass.  The revision names only a body that went out in full.
    CHECK(!p.Commit(c1.entries, 1000), "pub/a partial pass does not bump the revision");
    CHECK(p.Revision() == 1, "pub/revision after a partial pass");
    CHECK(p.Due(1000), "pub/an in-flight pass is due at tick rate");
    int total = c1.entries;
    while (p.Cursor() != 0 || p.Dirty()) {
        Pub::Chunk c = p.Peek(body, sizeof(body));
        if (c.entries == 0) break;
        total += c.entries;
        p.Commit(c.entries, 1000);
        if (total > 100) break;
    }
    CHECK(total == 10, "pub/the pass carries every entry exactly once");
    CHECK(p.Revision() == 2, "pub/completing the pass mints the next revision");
    CHECK(!p.Dirty(), "pub/a completed pass is clean");

    CHECK(!p.Due(1000), "pub/nothing is due inside the heartbeat window");
    CHECK(p.Due(1000 + actorstate::kHeartbeatMs), "pub/the 1 Hz heartbeat is due");
}

static void TestPublisherChangeRestartsPass()
{
    Pub p;
    Snap s;
    uint32_t g[4];
    for (uint32_t i = 0; i < 10; ++i) { GuidOf(g, i); s.Set(g, (int32_t)i); }
    p.Observe(s);

    uint8_t body[100];
    Pub::Chunk c = p.Peek(body, sizeof(body));
    p.Commit(c.entries, 1);
    CHECK(p.Cursor() == 4, "pub/a partial pass leaves a cursor");

    // A change mid-pass must restart at entry 0: the first chunk belongs to the old set,
    // and mixing the two is not the absolute state the header promises.
    GuidOf(g, 5);
    s.Set(g, 500);
    CHECK(p.Observe(s), "pub/a change mid-pass is seen");
    CHECK(p.Cursor() == 0, "pub/a change restarts the pass");
}

// ===========================================================================
// peer intents: observe, retry, coalesce, give-up
// ===========================================================================
static void TestIntentObserveAndRetry()
{
    Q q;
    Q::Out out[Q::kMaxPerTick];
    uint32_t g[4];
    GuidOf(g, 7);

    CHECK(q.Count() == 0, "intent/starts empty");
    CHECK(q.Observe(g, 1) == Q::kQueued, "intent/a local change is queued");
    CHECK(q.Count() == 1, "intent/count");

    int n = q.Due(1000, out, Q::kMaxPerTick);
    CHECK(n == 1, "intent/one is due");
    CHECK(actorstate::GuidEq(out[0].guid, g), "intent/the guid is carried");
    CHECK(out[0].value == 1 && out[0].attempt == 1, "intent/the value and attempt are carried");

    CHECK(q.Due(1000 + Q::kRetryMs - 1, out, Q::kMaxPerTick) == 0,
          "intent/no retry before kRetryMs");
    n = q.Due(1000 + Q::kRetryMs, out, Q::kMaxPerTick);
    CHECK(n == 1 && out[0].attempt == 2, "intent/a retry is due after kRetryMs");

    // Spend the budget.
    uint32_t t = 1000 + Q::kRetryMs;
    for (int i = 2; i < Q::kMaxAttempts; ++i) {
        t += Q::kRetryMs;
        q.Due(t, out, Q::kMaxPerTick);
    }
    CHECK(q.Due(t + Q::kRetryMs, out, Q::kMaxPerTick) == 0, "intent/no retry past kMaxAttempts");
    CHECK(q.GiveUp(g), "intent/GiveUp drops the spent intent");
    CHECK(q.Empty(), "intent/queue empty after give-up");
}

static void TestIntentCoalesce()
{
    Q q;
    Q::Out out[Q::kMaxPerTick];
    uint32_t g[4];
    GuidOf(g, 3);

    q.Observe(g, 1);
    CHECK(q.Observe(g, 1) == Q::kDuplicate, "intent/the same value is not queued twice");
    CHECK(q.Observe(g, 2) == Q::kCoalesced, "intent/a newer value coalesces");
    CHECK(q.Count() == 1, "intent/coalescing does not grow the queue");
    int n = q.Due(1000, out, Q::kMaxPerTick);
    CHECK(n == 1 && out[0].value == 2, "intent/the latest value is sent");

    // A coalesce after a send gets a fresh attempt budget, so the overtaken value cannot
    // consume the retries of the value that replaced it.
    CHECK(q.Observe(g, 3) == Q::kCoalesced, "intent/coalesce after a send");
    n = q.Due(1000, out, Q::kMaxPerTick);
    CHECK(n == 1 && out[0].value == 3 && out[0].attempt == 1,
          "intent/a coalesced value starts a fresh attempt budget");
}

static void TestIntentOverflow()
{
    Q q;
    uint32_t g[4];
    bool ok = true;
    for (int i = 0; i < Q::kMaxPending; ++i) {
        GuidOf(g, (uint32_t)i);
        if (q.Observe(g, 1) != Q::kQueued) ok = false;
    }
    CHECK(ok, "intent/fills to kMaxPending");
    GuidOf(g, 999);
    CHECK(q.Observe(g, 1) == Q::kOverflow, "intent/overflow is reported");
    CHECK(q.PendingOverflowed(), "intent/overflow flag");
}

// ===========================================================================
// echo suppression and the Held rule
// ===========================================================================
static void TestEchoSuppresses()
{
    Q q;
    Q::Out out[Q::kMaxPerTick];
    uint32_t g[4];
    GuidOf(g, 11);

    q.RememberEcho(g, 1);
    CHECK(q.Observe(g, 1) == Q::kEcho, "echo/the host's value is suppressed");
    CHECK(q.Empty(), "echo/nothing is queued");
    CHECK(q.Due(1000, out, Q::kMaxPerTick) == 0, "echo/nothing is sent");

    // A different local value is a real change.
    CHECK(q.Observe(g, 0) == Q::kQueued, "echo/a different local value is queued");
}

static void TestEchoCancelsPendingOnReturn()
{
    Q q;
    uint32_t g[4];
    GuidOf(g, 12);

    // The host's value is 0.  The peer opens the door (1), then closes it again before
    // the intent is sent.  The second value equals the host's, so the queued intent is
    // now stale and must be cancelled -- otherwise the peer would tell the host to
    // close a door the peer itself already closed, and the two would trade it.
    q.RememberEcho(g, 0);
    CHECK(q.Observe(g, 1) == Q::kQueued, "cancel/a local change is queued");
    CHECK(q.Count() == 1, "cancel/one pending");
    CHECK(q.Observe(g, 0) == Q::kEcho, "cancel/return to the host's value is an echo");
    CHECK(q.Empty(), "cancel/returning to the host's value cancels the pending intent");
}

static void TestHeldWhileIntentPending()
{
    Q q;
    uint32_t g[4];
    GuidOf(g, 21);

    q.Observe(g, 1);                 // peer opened the door

    // A heartbeat with the host's old value must NOT be applied: it left the host before
    // the intent arrived, and applying it would undo the peer's change silently.
    bool apply = true;
    CHECK(q.OnHostEntry(g, 0, &apply) == Q::kHeld, "held/a disagreeing body is held");
    CHECK(!apply, "held/the host value is not written");
    CHECK(q.Count() == 1, "held/the intent survives");

    // The host applies it and publishes the same value: that is the confirmation.
    apply = false;
    CHECK(q.OnHostEntry(g, 1, &apply) == Q::kConfirmed, "held/an equal body confirms");
    CHECK(apply, "held/the confirmed value may be applied");
    CHECK(q.Empty(), "held/confirmation settles the intent");
}

static void TestAbsoluteApply()
{
    Q q;
    uint32_t g[4];
    GuidOf(g, 30);

    // No intent in flight: the host's value is applied in EITHER direction.  This is the
    // door-can-close rule; the flag bank's never-lower rule must not leak in here.
    bool apply = false;
    CHECK(q.OnHostEntry(g, 0, &apply) == Q::kNoPending, "absolute/no pending is the no-pending case");
    CHECK(apply, "absolute/a lower host value is still applied");
    CHECK(actorstate::AbsoluteAccept(0, 1), "absolute/the named rule accepts a lowering");
    CHECK(actorstate::AbsoluteAccept(1, 0), "absolute/the named rule accepts a rise");
}

// ===========================================================================
// the local detector
// ===========================================================================
static void TestLocalDetector()
{
    Det det;
    Q q;
    Det::Stats st;
    Snap s;
    uint32_t g[4];
    GuidOf(g, 40);

    s.Set(g, 0);
    det.Observe(s, q, &st);                 // first sample is the baseline
    CHECK(q.Empty(), "detector/the first sample queues nothing");
    CHECK(st.changed == 0, "detector/first sample is not a change");

    s.Set(g, 1);
    det.Observe(s, q, &st);
    CHECK(st.changed == 1 && st.queued == 1, "detector/a changed value is queued");
    CHECK(q.Count() == 1, "detector/the queue holds the change");

    // The same sample again is not a change.
    det.Observe(s, q, &st);
    CHECK(st.changed == 0, "detector/an unchanged sample is not a change");

    // A host apply is recorded with Note() so the next poll cannot mistake it.
    q.RememberEcho(g, 1);
    det.Note(g, 1);
    det.Observe(s, q, &st);
    CHECK(st.changed == 0, "detector/a host-applied value noted first is not a change");
}

static void TestLocalDetectorEcho()
{
    Det det;
    Q q;
    Det::Stats st;
    Snap s;
    uint32_t g[4];
    GuidOf(g, 41);

    s.Set(g, 0);
    det.Observe(s, q, &st);

    // The host pushes 1 into our container without us noting it; the detector sees the
    // change, but the echo table says it is the host's value, so nothing is queued.
    q.RememberEcho(g, 1);
    s.Set(g, 1);
    det.Observe(s, q, &st);
    CHECK(st.changed == 1 && st.echoes == 1 && st.queued == 0,
          "detector/a host echo is seen and suppressed");
    CHECK(q.Empty(), "detector/no intent from an echo");
}

int main()
{
    TestSnapshot();
    TestOverflow();
    TestPublisherEmpty();
    TestPublisherChunking();
    TestPublisherChangeRestartsPass();
    TestIntentObserveAndRetry();
    TestIntentCoalesce();
    TestIntentOverflow();
    TestEchoSuppresses();
    TestEchoCancelsPendingOnReturn();
    TestHeldWhileIntentPending();
    TestAbsoluteApply();
    TestLocalDetector();
    TestLocalDetectorEcho();

    printf("actorstate: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
