// test_playerbody.cpp -- the rules the player-class proxy body turns on.
//
// WHY THESE TESTS AND NOT A LIVE RUN
// ----------------------------------
// Each rule in qdplayerbody.h fails in a way that reads as a different bug in a game:
// a target that flaps between two players looks like broken aggro; a second description
// of a swing that already landed looks like "enemies hit too hard"; an adopted HP that
// goes UP looks like a healing bug; an adoption from a stale row resurrects an enemy the
// partner already killed.  None of those can be told apart from the real thing without a
// second machine, and all four are arithmetic here.
#include <stdint.h>
#include <stdio.h>

#include "qdplayerbody.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, name)                                                          \
    do {                                                                           \
        if (cond) { ++g_pass; }                                                    \
        else { ++g_fail; printf("FAIL %s  (%s:%d)\n", name, __FILE__, __LINE__); } \
    } while (0)

using namespace qd;

// ---------------------------------------------------------------------------
// 1. Target selection
// ---------------------------------------------------------------------------
static void TestOnlyAPlayerTargetIsRedirected()
{
    // The AI picked nothing: perception has not found a player, so the mod must not
    // invent a target for it.
    CHECK(playerbody::ChooseTarget(true, true, playerbody::CUR_NONE, 100.0f, 50.0f) ==
              playerbody::TGT_LEAVE,
          "target/no target is left alone");
    // The AI picked an NPC (or a corpse, or a projectile).  Redirecting this is what
    // destroys an enemy's aggro behaviour, so it is explicitly refused.
    CHECK(playerbody::ChooseTarget(true, true, playerbody::CUR_OTHER, 100.0f, 50.0f) ==
              playerbody::TGT_LEAVE,
          "target/a target that is not a player is never rewritten");
    // A machine that does not run the enemy does not steer it.
    CHECK(playerbody::ChooseTarget(false, true, playerbody::CUR_LOCAL, 5000.0f, 50.0f) ==
              playerbody::TGT_LEAVE,
          "target/a frozen follower does not retarget");
}

static void TestSwitchFollowsTheNearerPlayer()
{
    // Remote genuinely nearer: switch.
    CHECK(playerbody::ChooseTarget(true, true, playerbody::CUR_LOCAL, 1000.0f, 300.0f) ==
              playerbody::TGT_TO_REMOTE,
          "target/the nearer remote body is taken");
    // Local genuinely nearer: leave it.
    CHECK(playerbody::ChooseTarget(true, true, playerbody::CUR_LOCAL, 300.0f, 1000.0f) ==
              playerbody::TGT_LEAVE,
          "target/the nearer local player is kept");
    // The other direction: the mod already moved it, and now the local player is nearer.
    CHECK(playerbody::ChooseTarget(true, true, playerbody::CUR_REMOTE, 300.0f, 1000.0f) ==
              playerbody::TGT_TO_LOCAL,
          "target/the target comes back when the local player is nearer");
    CHECK(playerbody::ChooseTarget(true, true, playerbody::CUR_REMOTE, 1000.0f, 300.0f) ==
              playerbody::TGT_LEAVE,
          "target/the remote body that is still nearer is kept");
}

static void TestHysteresis()
{
    // Exactly at the margin: no switch.  The comparison is strict, so the boundary is a
    // hold, which is the safe side for a value that is measured.
    const float d = playerbody::kSwitchCm;
    CHECK(playerbody::ChooseTarget(true, true, playerbody::CUR_LOCAL, 400.0f, 400.0f - d) ==
              playerbody::TGT_LEAVE,
          "hysteresis/exactly at the margin holds");
    CHECK(playerbody::ChooseTarget(true, true, playerbody::CUR_LOCAL, 400.0f, 400.0f - d - 1.0f) ==
              playerbody::TGT_TO_REMOTE,
          "hysteresis/one centimetre past the margin switches");
    // Two players 100 cm apart cannot make one enemy flap: neither is past the margin.
    CHECK(playerbody::ChooseTarget(true, true, playerbody::CUR_LOCAL, 1000.0f, 950.0f) ==
              playerbody::TGT_LEAVE,
          "hysteresis/co-located players never flap");
    CHECK(playerbody::ChooseTarget(true, true, playerbody::CUR_REMOTE, 950.0f, 1000.0f) ==
              playerbody::TGT_LEAVE,
          "hysteresis/co-located players never flap back");
}

static void TestUnmeasuredDistancesHold()
{
    CHECK(playerbody::ChooseTarget(true, true, playerbody::CUR_LOCAL, -1.0f, 100.0f) ==
              playerbody::TGT_LEAVE,
          "unmeasured/an unreadable local distance holds");
    CHECK(playerbody::ChooseTarget(true, true, playerbody::CUR_LOCAL, 100.0f, -1.0f) ==
              playerbody::TGT_LEAVE,
          "unmeasured/an unreadable remote distance holds");
}

static void TestDeadBodyIsRepaired()
{
    // The body was destroyed (peer left, level change): whatever the mod pointed at is a
    // dangling handle, and the enemy's own game logic would fault on it.  Restoration is
    // a repair, not a preference, so it does not need distances.
    CHECK(playerbody::ChooseTarget(true, false, playerbody::CUR_REMOTE, -1.0f, -1.0f) ==
              playerbody::TGT_TO_LOCAL,
          "repair/a target on a destroyed body is restored even unmeasured");
    CHECK(playerbody::ChooseTarget(true, false, playerbody::CUR_LOCAL, 100.0f, 50.0f) ==
              playerbody::TGT_LEAVE,
          "repair/a local target with no body to point at is left alone");
}

// ---------------------------------------------------------------------------
// 2. The described-damage lane yields to the real hit path
// ---------------------------------------------------------------------------
static void TestSwingIsNotDescribedTwice()
{
    CHECK(playerbody::SwingAlreadyReal(true, true, true),
          "double/a swing aimed at the proxy is not described again");
    CHECK(!playerbody::SwingAlreadyReal(true, false, true),
          "double/a swing at the local player is still described");
    CHECK(!playerbody::SwingAlreadyReal(false, false, true),
          "double/with no proxy the old lane is the only lane");
    // A stale target bit with no live body cannot suppress the described lane: the
    // partner would then be hit by nothing at all.
    CHECK(!playerbody::SwingAlreadyReal(false, true, true),
          "double/a target on a dead body does not suppress the description");
    // R74: the enemy aimed at the proxy and the game's own hit path never landed a hit on
    // it (collision off, a trace-based melee).  Suppressing the description here is the
    // exact bug that left the partner hit by nothing, so it takes one landed hit first.
    CHECK(!playerbody::SwingAlreadyReal(true, true, false),
          "double/a real path that has never landed does not suppress the description");
}

// ---------------------------------------------------------------------------
// 3. Adoption across a re-created enemy (G5)
// ---------------------------------------------------------------------------
static void TestAdoptNeverHeals()
{
    // The partner has taken the enemy down to 40 %: the fresh actor must start there.
    CHECK(playerbody::AdoptedHpFrac(65535, true, 26214, true, true) == 26214,
          "adopt/a lower partner fraction wins");
    // The partner's row is HIGHER than the local state (it has not seen a hit yet): the
    // local, lower value is kept.  Raising it would be a heal.
    CHECK(playerbody::AdoptedHpFrac(20000, true, 40000, true, true) == 20000,
          "adopt/a higher partner fraction never heals");
    // Equal: nothing to do either way.
    CHECK(playerbody::AdoptedHpFrac(30000, true, 30000, true, true) == 30000,
          "adopt/an equal fraction is a no-op");
    // A dead row is the strongest downward statement there is.
    CHECK(playerbody::AdoptedHpFrac(65535, true, 0, true, true) == 0,
          "adopt/a dead partner row kills the fresh actor");
}

static void TestAdoptNeedsAFreshRow()
{
    // A stale row describes an enemy that was somewhere else: adopting from it would
    // resurrect a state the other machine has already left.
    CHECK(playerbody::AdoptedHpFrac(65535, true, 1000, true, false) == 65535,
          "adopt/a stale row is ignored");
    // No row at all (the partner never saw this enemy): there is nothing to adopt.
    CHECK(playerbody::AdoptedHpFrac(65535, true, 1000, false, true) == 65535,
          "adopt/the absence of a row is not a fraction");
    // A partner that HAS a row and a local HP that cannot be read: the row is still the
    // best statement available.
    CHECK(playerbody::AdoptedHpFrac(0, false, 12345, true, true) == 12345,
          "adopt/an unreadable local HP defers to a fresh row");
    CHECK(playerbody::AdoptedHpFrac(0, false, 12345, false, true) == 0,
          "adopt/nothing known at all is zero");
}

// ---------------------------------------------------------------------------
// 4. Distance helper (the both-players numbers in the log)
// ---------------------------------------------------------------------------
static void TestDistance()
{
    const float a[3] = {0, 0, 0};
    const float b[3] = {300.0f, 400.0f, 0.0f};
    CHECK(playerbody::Dist3(a, b) > 499.9f && playerbody::Dist3(a, b) < 500.1f,
          "distance/a 3-4-5 triangle is 500 cm");
    CHECK(playerbody::Dist3(a, a) == 0.0f, "distance/a point to itself is zero");
}

int main()
{
    printf("=== qd player-body tests ===\n");
    TestOnlyAPlayerTargetIsRedirected();
    TestSwitchFollowsTheNearerPlayer();
    TestHysteresis();
    TestUnmeasuredDistancesHold();
    TestDeadBodyIsRepaired();
    TestSwingIsNotDescribedTwice();
    TestAdoptNeverHeals();
    TestAdoptNeedsAFreshRow();
    TestDistance();
    printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
