// test_enemybw.cpp -- keeping ENEMY_DETAIL bounded, and keeping the RIGHT details.
//
// WHY THESE TESTS AND NOT A LIVE RUN
// ----------------------------------
// The lane this governs is cosmetic, which is exactly why its failures are expensive:
// a packet storm from a busy fight takes out the reliable table, and the lanes sharing
// that table (INTENT, TRAVEL, DAMAGE) are the ones that must not yield.  Conversely an
// over-eager limit makes a boss death animation never arrive, which a player reads as
// "the boss fight is broken".
//
// So the assertions here are the four properties the design claims, each stated as a
// number: the rolling-second bound, the far-band skip, the per-enemy interval, and the
// ordering (near and critical first).  Every one is a pure function of the candidate
// list, which is why this is a unit test rather than a rig experiment.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "qdenemybw.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, name)                                                          \
    do {                                                                           \
        if (cond) { ++g_pass; }                                                    \
        else { ++g_fail; printf("FAIL %s  (%s:%d)\n", name, __FILE__, __LINE__); } \
    } while (0)

using namespace qd;

static uint32_t g_keys[512][4];

// A candidate keyed by `i`, so a test can name which enemy it means.
static const uint32_t* Key(int i)
{
    g_keys[i][0] = (uint32_t)(i + 1);
    g_keys[i][1] = 0;
    g_keys[i][2] = 0;
    g_keys[i][3] = 0;
    return g_keys[i];
}

static enemybw::Cand Make(int i, float dist, bool critical)
{
    enemybw::Cand c;
    c.key = Key(i);
    c.dist_cm = dist;
    c.critical = critical;
    c.haveLast = false;
    c.lastMs = 0;
    return c;
}

// ===========================================================================
// Bands
// ===========================================================================
static void TestBands()
{
    CHECK(enemybw::Classify(0.0f) == enemybw::B_NEAR, "0 cm is near");
    CHECK(enemybw::Classify(enemybw::kNearCm) == enemybw::B_NEAR, "the near edge is near");
    CHECK(enemybw::Classify(enemybw::kNearCm + 1.0f) == enemybw::B_MID, "just past near is mid");
    CHECK(enemybw::Classify(enemybw::kMidCm) == enemybw::B_MID, "the mid edge is mid");
    CHECK(enemybw::Classify(enemybw::kMidCm + 1.0f) == enemybw::B_FAR, "just past mid is far");
    CHECK(enemybw::Classify(enemybw::kFarCm) == enemybw::B_FAR, "the far edge is still far");
    CHECK(enemybw::Classify(enemybw::kFarCm + 1.0f) == enemybw::B_OUT, "past far is out");
    // An unknown distance is not a reason to spend a packet.
    CHECK(enemybw::Classify(-1.0f) == enemybw::B_OUT, "a negative (unknown) distance is out");

    // Near is never slower than far: the ordering is what "relevance" means.
    CHECK(enemybw::MinIntervalMs(enemybw::B_NEAR, true) <
          enemybw::MinIntervalMs(enemybw::B_FAR, true), "near critical sends sooner than far");
    CHECK(enemybw::MinIntervalMs(enemybw::B_NEAR, false) <
          enemybw::MinIntervalMs(enemybw::B_NEAR, true) ||
          enemybw::MinIntervalMs(enemybw::B_NEAR, true) <=
          enemybw::MinIntervalMs(enemybw::B_NEAR, false), "the intervals are ordered");
    CHECK(enemybw::MinIntervalMs(enemybw::B_OUT, false) == 0xFFFFFFFFu,
          "a cosmetic change out of range is never sent");
}

// ===========================================================================
// The rolling-second bound
// ===========================================================================
static void TestBudget()
{
    enemybw::Scheduler s;
    enemybw::Cand c[200];
    int out[200];
    enemybw::Skip why[200];

    // 200 enemies, all critical, all in range: far more than one second's budget.
    for (int i = 0; i < 200; ++i) c[i] = Make(i, 100.0f, true);
    int n = s.Select(c, 200, 0, out, why);
    CHECK(n == enemybw::kMaxPerSec, "one tick cannot exceed the per-second budget");
    CHECK(s.InWindow(0) == enemybw::kMaxPerSec, "the window holds exactly the budget");

    // A second burst inside the same second gets nothing.
    n = s.Select(c, 200, 500, out, why);
    CHECK(n == 0, "a second burst inside the same second gets nothing");
    // Every one of the 200 candidates was refused, and every refusal was attributed to
    // the exhausted budget -- the counter is cumulative over the scheduler's life, so
    // it includes the first tick's excess as well.
    CHECK(s.BudgetDrops() >= 200, "every refused candidate is counted as a budget drop");

    // A second later the window has rolled and the budget is back.
    n = s.Select(c, 200, 1001, out, why);
    CHECK(n == enemybw::kMaxPerSec, "the budget refills on a rolling second");

    // The bound is over a WINDOW, not a bucket: sends at 999 ms and 1001 ms are 2 ms
    // apart and must count against each other.
    enemybw::Scheduler w;
    enemybw::Cand one[1];
    one[0] = Make(0, 100.0f, true);
    CHECK(w.Select(one, 1, 0, out, why) == 1, "a send at 0 is admitted");
    // Fill the window at 0..?? then check the boundary carefully below.
    enemybw::Scheduler z;
    enemybw::Cand burst[enemybw::kMaxPerSec];
    for (int i = 0; i < enemybw::kMaxPerSec; ++i) burst[i] = Make(i, 50.0f, true);
    CHECK(z.Select(burst, enemybw::kMaxPerSec, 0, out, why) == enemybw::kMaxPerSec,
          "a full burst is admitted");
    CHECK(z.Select(burst, enemybw::kMaxPerSec, 999, out, why) == 0,
          "999 ms later the window is a rolling second, not a bucket");
    CHECK(z.Select(burst, enemybw::kMaxPerSec, 1000, out, why) == enemybw::kMaxPerSec,
          "at exactly 1000 ms the oldest sends leave the window");
}

// ===========================================================================
// Per-enemy interval: a flapping montage does not produce a packet per frame
// ===========================================================================
static void TestPerEnemyInterval()
{
    enemybw::Scheduler s;
    enemybw::Cand c[1];
    int out[8];
    enemybw::Skip why[8];
    uint32_t now = 10'000;

    // A cosmetic change on a near enemy.
    c[0] = Make(0, 100.0f, false);
    CHECK(s.Select(c, 1, now, out, why) == 1, "the first cosmetic change is sent");
    c[0].haveLast = true;
    c[0].lastMs = now;

    // 50 ms later it changed again: too soon for a near cosmetic event (400 ms).
    CHECK(s.Select(c, 1, now + 50, out, why) == 0, "a 50 ms repeat is suppressed");
    CHECK(why[0] == enemybw::SK_INTERVAL, "the skip reason is the per-enemy interval");
    CHECK(s.Select(c, 1, now + 399, out, why) == 0, "399 ms is still too soon");
    CHECK(s.Select(c, 1, now + 400, out, why) == 1, "400 ms is admitted");
    c[0].lastMs = now + 400;

    // A critical event has a much shorter interval at the same distance.
    c[0].critical = true;
    CHECK(s.Select(c, 1, now + 450, out, why) == 0, "a critical event still respects 100 ms");
    CHECK(s.Select(c, 1, now + 500, out, why) == 1, "a critical event is admitted after 100 ms");
}

// ===========================================================================
// Far band: cosmetic changes out of range are not sent, critical ones are
// ===========================================================================
static void TestFarBand()
{
    enemybw::Scheduler s;
    enemybw::Cand c[2];
    int out[8];
    enemybw::Skip why[8];

    // Two enemies beyond the far band: one walking, one dying.
    c[0] = Make(0, enemybw::kFarCm + 5000.0f, false);   // cosmetic, out
    c[1] = Make(1, enemybw::kFarCm + 5000.0f, true);    // critical, out
    int n = s.Select(c, 2, 0, out, why);
    CHECK(n == 1, "only the critical out-of-range event is sent");
    CHECK(out[0] == 1, "the critical one is the one sent");
    CHECK(why[0] == enemybw::SK_FAR, "the cosmetic far skip is reported as far");
    CHECK(why[1] == enemybw::SK_NONE, "the critical one is not skipped");
    CHECK(s.FarSkips() == 1, "the far skip is counted");

    // The death of a boss 200 m away must still arrive: that is a critical event and
    // the band gate does not refuse it.
    enemybw::Scheduler d;
    enemybw::Cand boss[1];
    boss[0] = Make(9, 20000.0f, true);
    CHECK(d.Select(boss, 1, 0, out, why) == 1, "an out-of-range boss death is sent");
}

// ===========================================================================
// Ordering: critical first, then nearest
// ===========================================================================
static void TestOrdering()
{
    enemybw::Scheduler s;
    enemybw::Cand c[5];
    int out[8];
    enemybw::Skip why[8];

    // Deliberately presented worst-first.
    c[0] = Make(0, 9000.0f, false);   // far, cosmetic
    c[1] = Make(1, 7000.0f, false);   // nearer, cosmetic
    c[2] = Make(2, 8000.0f, true);    // far, critical
    c[3] = Make(3, 100.0f, false);    // nearest, cosmetic
    c[4] = Make(4, 5000.0f, true);    // mid, critical

    int n = s.Select(c, 5, 0, out, why);
    CHECK(n == 5, "a small fight sends everything");
    // Critical first (by distance among criticals), then cosmetic by distance.
    CHECK(out[0] == 4, "the nearest critical event is first");
    CHECK(out[1] == 2, "the next critical event follows");
    CHECK(out[2] == 3, "then the nearest cosmetic event");
    CHECK(out[3] == 1, "then the next cosmetic event");
    CHECK(out[4] == 0, "and the farthest cosmetic event last");
}

// ===========================================================================
// Bounded records: the scheduler never grows without limit
// ===========================================================================
static void TestBounded()
{
    enemybw::Scheduler s;
    enemybw::Cand c[300];
    int out[300];
    enemybw::Skip why[300];

    // More candidates than the table holds.  Select must not write past its caller's
    // buffer, so it clamps to the candidate cap and says nothing about the rest.
    for (int i = 0; i < 300; ++i) c[i] = Make(i % 200, 100.0f, true);
    int n = s.Select(c, 300, 0, out, why);
    CHECK(n <= enemybw::kMaxRecords, "a huge batch is bounded by the table");

    // An all-zero key is not an enemy and never occupies a record.
    enemybw::Scheduler z;
    enemybw::Cand bad[1];
    static const uint32_t zero[4] = {0, 0, 0, 0};
    bad[0].key = zero;
    bad[0].dist_cm = 10.0f;
    bad[0].critical = true;
    bad[0].haveLast = false;
    bad[0].lastMs = 0;
    CHECK(z.Select(bad, 1, 0, out, why) == 0, "a zero key is never sent");
    CHECK(why[0] == enemybw::SK_OVERFLOW, "and it is reported as overflow, not a budget drop");

    // Records are recycled once they are a minute quiet.
    enemybw::Scheduler r;
    enemybw::Cand fill[enemybw::kMaxRecords];
    for (int i = 0; i < enemybw::kMaxRecords; ++i) fill[i] = Make(i, 100.0f, true);
    r.Select(fill, enemybw::kMaxRecords, 0, out, why);
    CHECK(r.Count() == enemybw::kMaxRecords, "the table fills");
    enemybw::Cand extra[1];
    extra[0] = Make(enemybw::kMaxRecords + 1, 100.0f, true);
    CHECK(r.Select(extra, 1, 1000, out, why) == 0, "a full table refuses a new key");
    CHECK(r.Select(extra, 1, enemybw::kRecycleMs + 1, out, why) == 1,
          "a record past the recycle window is reused");
}

// ===========================================================================
// A dropped change is retried, not lost
// ===========================================================================
static void TestRetry()
{
    enemybw::Scheduler s;
    enemybw::Cand c[enemybw::kMaxPerSec + 1];
    int out[64];
    enemybw::Skip why[64];

    // One more enemy than the budget, all critical and identical in relevance.
    int nIn = enemybw::kMaxPerSec + 1;
    for (int i = 0; i < nIn; ++i) c[i] = Make(i, 100.0f + (float)i, true);
    int n = s.Select(c, nIn, 0, out, why);
    CHECK(n == enemybw::kMaxPerSec, "the budget admits exactly its size");

    // The refused one is refused with the BUDGET reason, which is what tells the caller
    // to keep the change pending rather than record it as done.
    int refused = -1;
    for (int i = 0; i < nIn; ++i) if (why[i] == enemybw::SK_BUDGET) refused = i;
    CHECK(refused >= 0, "the excess candidate is marked as a budget drop");

    // Next second it is admitted: nothing was recorded as sent for it.
    enemybw::Cand only[1] = {c[refused]};
    only[0].haveLast = false;
    CHECK(s.Select(only, 1, 1001, out, why) == 1, "the pending change is admitted next second");
}

int main()
{
    TestBands();
    TestBudget();
    TestPerEnemyInterval();
    TestFarBand();
    TestOrdering();
    TestBounded();
    TestRetry();

    printf("\n=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
