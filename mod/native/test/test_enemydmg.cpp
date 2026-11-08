// test_enemydmg.cpp -- the host's damage lane: range, de-duplication, rate bound.
//
// WHY THESE TESTS AND NOT A LIVE RUN
// ----------------------------------
// Every rule in qdenemydmg.h fails in a way that looks like something else in a game:
// a range test with the wrong operator makes the partner invulnerable (G4 again) or
// lets a monster hit across a room; a de-duplication bug is double damage that reads as
// "enemies hit too hard"; an unbounded producer is a reliable-lane flood that shows up
// as lag on both machines.  Each assertion below pins one of those to a number, which is
// the only kind of evidence available without a second machine.
#include <stdint.h>
#include <stdio.h>

#include "qdenemydmg.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, name)                                                          \
    do {                                                                           \
        if (cond) { ++g_pass; }                                                    \
        else { ++g_fail; printf("FAIL %s  (%s:%d)\n", name, __FILE__, __LINE__); } \
    } while (0)

using namespace qd;

static const uint32_t kGuid[4] = {0x11223344u, 0x55667788u, 0x99aabbccu, 0xddeeff00u};

// All conditions satisfied, the peer well inside the reach.
static enemydmg::Context GoodContext()
{
    enemydmg::Context c;
    c.host_owns      = true;
    c.peer_in_world  = true;
    c.worlds_agree   = true;
    c.have_enemy_pos = true;
    c.have_peer_pos  = true;
    c.range_cm       = 200.0f;
    return c;
}

static const float kEnemy[3] = {0.0f, 0.0f, 0.0f};

static void TestEveryConditionIsRequired()
{
    const float peerIn[3] = {100.0f, 0.0f, 0.0f};
    CHECK(enemydmg::ShouldSend(GoodContext(), kEnemy, peerIn), "range/all conditions sends");

    enemydmg::Context c = GoodContext(); c.host_owns = false;
    CHECK(!enemydmg::ShouldSend(c, kEnemy, peerIn), "range/a machine that does not own it does not send");

    c = GoodContext(); c.peer_in_world = false;
    CHECK(!enemydmg::ShouldSend(c, kEnemy, peerIn), "range/a partner in a menu world is not hit");

    c = GoodContext(); c.worlds_agree = false;
    CHECK(!enemydmg::ShouldSend(c, kEnemy, peerIn), "range/a partner in another map is not hit");

    c = GoodContext(); c.have_enemy_pos = false;
    CHECK(!enemydmg::ShouldSend(c, kEnemy, peerIn), "range/an unknown enemy position does not send");

    c = GoodContext(); c.have_peer_pos = false;
    CHECK(!enemydmg::ShouldSend(c, kEnemy, peerIn), "range/a frozen partner body does not send");

    c = GoodContext(); c.range_cm = 0.0f;
    CHECK(!enemydmg::ShouldSend(c, kEnemy, peerIn), "range/an unresolved range is not a reach of zero");

    c = GoodContext(); c.range_cm = -1.0f;
    CHECK(!enemydmg::ShouldSend(c, kEnemy, peerIn), "range/a negative range is refused");
}

static void TestDistanceBoundary()
{
    enemydmg::Context c = GoodContext();
    const float atEdge[3] = {200.0f, 0.0f, 0.0f};
    const float inSlack[3] = {249.0f, 0.0f, 0.0f};
    const float outside[3] = {251.0f, 0.0f, 0.0f};
    CHECK(enemydmg::ShouldSend(c, kEnemy, atEdge), "range/exactly at range is a hit");
    CHECK(enemydmg::ShouldSend(c, kEnemy, inSlack), "range/inside the render-delay slack is a hit");
    CHECK(!enemydmg::ShouldSend(c, kEnemy, outside), "range/past range and slack is not a hit");

    // 3-D distance, not a per-axis test: on either axis alone (200) this body is at the
    // edge, but its real distance (283) is past range plus slack.
    const float diag[3] = {200.0f, 200.0f, 0.0f};
    CHECK(!enemydmg::ShouldSend(c, kEnemy, diag), "range/distance is three-dimensional");
}

static void TestCenti()
{
    CHECK(enemydmg::Centi(67.0f) == 6700, "centi/a whole amount scales by 100");
    CHECK(enemydmg::Centi(0.005f) == 1, "centi/a sub-unit amount rounds up");
    CHECK(enemydmg::Centi(0.0f) == 0, "centi/zero is no hit");
    CHECK(enemydmg::Centi(-3.0f) == 0, "centi/negative is no hit");
    CHECK(enemydmg::Centi(20000.0f) == enemydmg::kMaxCenti, "centi/an absurd amount is clamped");
}

static void TestOnePerSwing()
{
    enemydmg::SwingGate g;
    CHECK(g.Admit(kGuid, 1, 0), "gate/the first swing is described");
    CHECK(!g.Admit(kGuid, 1, 0), "gate/the same swing twice is refused");
    CHECK(g.RepeatDrops() == 1, "gate/the repeat is counted");
    CHECK(g.Admit(kGuid, 2, 10), "gate/a new montage is a new swing");

    // A different enemy with the same sequence number is a different swing.
    uint32_t other[4] = {kGuid[0], kGuid[1], kGuid[2], kGuid[3] + 1};
    CHECK(g.Admit(other, 1, 20), "gate/the key is (guid, seq), not seq alone");

    // The sequence wraps 255 -> 1 in the game; a new montage always changes the value,
    // so the wrap must not look like a repeat.
    CHECK(g.Admit(kGuid, 255, 30), "gate/seq 255 is a swing");
    CHECK(g.Admit(kGuid, 1, 40), "gate/seq wrapping to 1 is a new swing");
}

static void TestZeroGuidIsNotAnEnemy()
{
    enemydmg::SwingGate g;
    const uint32_t zero[4] = {0, 0, 0, 0};
    CHECK(!g.Admit(zero, 1, 0), "gate/a zero guid describes nobody");
    CHECK(g.Count() == 0, "gate/a zero guid allocates no record");
}

static void TestRateBound()
{
    enemydmg::SwingGate g;
    // kMaxPerSec distinct swings inside the window: all admitted.
    for (int i = 0; i < enemydmg::kMaxPerSec; ++i) {
        uint32_t guid[4] = {(uint32_t)(i + 1), 0, 0, 0};
        CHECK(g.Admit(guid, 1, 0), "rate/inside the bound is admitted");
    }
    uint32_t extra[4] = {0xffffffffu, 0, 0, 0};
    CHECK(!g.Admit(extra, 1, 999), "rate/the bound refuses a 31st send inside the second");
    CHECK(g.RateDrops() == 1, "rate/the refusal is counted");
    // The window is rolling, not a bucket: at 1000 ms the first burst has aged out.
    CHECK(g.Admit(extra, 1, 1000), "rate/the bound clears after the rolling second");
}

static void TestRecordOverflow()
{
    enemydmg::SwingGate g;
    // 40 ms apart keeps the rolling-window count (25) under the bound while every
    // record stays younger than the 60 s recycle age, so the table is what runs out.
    for (int i = 0; i < enemydmg::kMaxRecords; ++i) {
        uint32_t guid[4] = {(uint32_t)(i + 1), 0, 0, 0};
        CHECK(g.Admit(guid, 1, (uint32_t)(i * 40)), "overflow/records fill without a rate drop");
    }
    uint32_t extra[4] = {0xffffffffu, 0, 0, 0};
    CHECK(!g.Admit(extra, 1, (uint32_t)(enemydmg::kMaxRecords * 40)),
          "overflow/a full table refuses rather than growing");
    CHECK(g.Overflows() == 1, "overflow/the overflow is counted");
}

static void TestReset()
{
    enemydmg::SwingGate g;
    CHECK(g.Admit(kGuid, 1, 0), "reset/a swing is admitted");
    g.Reset();
    CHECK(g.Count() == 0, "reset/the record table is cleared");
    CHECK(g.RepeatDrops() == 0 && g.RateDrops() == 0 && g.Overflows() == 0,
          "reset/the counters are cleared");
    CHECK(g.Admit(kGuid, 1, 0), "reset/the same swing is new after a world/session change");
}

int main()
{
    printf("=== qd enemy-damage tests ===\n");
    TestEveryConditionIsRequired();
    TestDistanceBoundary();
    TestCenti();
    TestOnePerSwing();
    TestZeroGuidIsNotAnEnemy();
    TestRateBound();
    TestRecordOverflow();
    TestReset();
    printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
