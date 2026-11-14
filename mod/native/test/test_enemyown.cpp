// test_enemyown.cpp -- ownership-by-proximity: who runs an enemy, and how it settles.
//
// WHY THESE TESTS AND NOT A LIVE RUN
// ----------------------------------
// The rule is a distributed decision.  Its failure modes are invisible in a code
// review and expensive in a game run: two machines both simulating (double damage,
// double loot), neither simulating (a frozen statue that never attacks), or both
// handing the enemy back and forth (an enemy that teleports between two owners).  Each
// assertion below pins one of those to a number.
//
// The harness drives two OwnerSets with the exchange a real session performs: every
// step each machine ticks its own decision, then publishes a row if it is simulating,
// and the other machine receives it.  A machine's own position is exact; the other's is
// whatever the harness feeds it, which is how stale data is modelled.
#include <stdint.h>
#include <stdio.h>

#include "qdenemyown.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, name)                                                          \
    do {                                                                           \
        if (cond) { ++g_pass; }                                                    \
        else { ++g_fail; printf("FAIL %s  (%s:%d)\n", name, __FILE__, __LINE__); } \
    } while (0)

using namespace qd;

static const uint32_t kGuid[4] = {0x5ae3b37bu, 0x4f721b25u, 0xbd55a3b5u, 0x958ab1c2u};

static const float kEnemy[3] = {0.0f, 0.0f, 0.0f};

// Step both machines once: decide, then exchange whichever rows each publishes.
static void Step(enemyown::OwnerSet& h, enemyown::OwnerSet& p, uint32_t now,
                 const float hostPos[3], const float peerPos[3],
                 bool hostHas, bool peerHas)
{
    h.Tick(kGuid, hostHas, kEnemy, true, hostPos, true, peerPos, true, now);
    p.Tick(kGuid, peerHas, kEnemy, true, peerPos, true, hostPos, true, now);
    enemyown::Publish hp = h.Publishing(kGuid);
    enemyown::Publish pp = p.Publishing(kGuid);
    if (hp != enemyown::PUB_NONE) p.OnRemoteRow(kGuid, hp == enemyown::PUB_HANDOFF, now);
    if (pp != enemyown::PUB_NONE) h.OnRemoteRow(kGuid, pp == enemyown::PUB_HANDOFF, now);
}

// 0 local, 1 hand-off, 2 frozen -- a value that can be compared tick to tick.
static int Kind(const enemyown::OwnerSet& s)
{
    return (int)s.State(kGuid);
}

// ===========================================================================
// Distance decides, and it settles
// ===========================================================================
static void TestNearerMachineWins()
{
    enemyown::OwnerSet h(true), p(false);
    const float hostNear[3] = {100.0f, 0.0f, 0.0f};
    const float peerFar[3]  = {5000.0f, 0.0f, 0.0f};
    uint32_t now = 0;
    for (int i = 0; i < 400; ++i) { now += 16; Step(h, p, now, hostNear, peerFar, true, true); }
    CHECK(h.State(kGuid) == enemyown::O_LOCAL, "own/the nearer (host) machine runs it");
    CHECK(p.State(kGuid) == enemyown::O_FROZEN, "own/the farther (peer) machine follows");
    CHECK(p.Publishing(kGuid) == enemyown::PUB_NONE, "own/a frozen copy publishes nothing");
    CHECK(h.Publishing(kGuid) == enemyown::PUB_OWN, "own/the owner publishes its rows");

    // Swap the players.  The hand-off must reverse, and settle with the peer owning.
    const float hostFar[3] = {5000.0f, 0.0f, 0.0f};
    const float peerNear[3] = {100.0f, 0.0f, 0.0f};
    for (int i = 0; i < 400; ++i) { now += 16; Step(h, p, now, hostFar, peerNear, true, true); }
    CHECK(p.State(kGuid) == enemyown::O_LOCAL, "own/a swap reverses ownership");
    CHECK(h.State(kGuid) == enemyown::O_FROZEN, "own/the old owner follows after the swap");
}

static void TestHoldPreventsAnEarlySwitch()
{
    // The HOST is the far machine (the peer never yields while it is clearly nearer, so
    // the host's own distance test is what moves the enemy -- after the hold).
    enemyown::OwnerSet h(true), p(false);
    const float hostFar[3]  = {5000.0f, 0.0f, 0.0f};
    const float peerNear[3] = {100.0f, 0.0f, 0.0f};
    uint32_t now = 0;
    // One second: the host prefers to hand it over but the hold has not elapsed.
    for (int i = 0; i < 60; ++i) { now += 16; Step(h, p, now, hostFar, peerNear, true, true); }
    CHECK(h.State(kGuid) == enemyown::O_LOCAL,
          "own/a preference younger than the hold does not switch");
    // Past the hold, it does.
    for (int i = 0; i < 120; ++i) { now += 16; Step(h, p, now, hostFar, peerNear, true, true); }
    CHECK(h.State(kGuid) == enemyown::O_FROZEN, "own/the switch happens once the hold passes");
    CHECK(p.State(kGuid) == enemyown::O_LOCAL, "own/the nearer peer runs it after the hold");
}

// ===========================================================================
// A double run always ends with one owner (Session 29, run 33: two Queens)
// ===========================================================================
static void TestDoubleRunIsResolved()
{
    // Both machines start their own copy and both say "I run it" -- the peer's real copy
    // replaced a frozen constructed one.  A STICKY boss (no distance hand-off at all) is
    // the worst case: before the fix both ran it for the whole fight.
    enemyown::OwnerSet h(true), p(false);
    const float hostPos[3] = {350.0f, 0.0f, 0.0f};
    const float peerPos[3] = {250.0f, 0.0f, 0.0f};
    uint32_t now = 0;
    int bothRunning = 0;
    for (int i = 0; i < 400; ++i) {
        now += 16;
        h.Tick(kGuid, true, kEnemy, true, hostPos, true, peerPos, true, now, true);
        p.Tick(kGuid, true, kEnemy, true, peerPos, true, hostPos, true, now, true);
        enemyown::Publish hp = h.Publishing(kGuid), pp = p.Publishing(kGuid);
        if (hp != enemyown::PUB_NONE) p.OnRemoteRow(kGuid, hp == enemyown::PUB_HANDOFF, now);
        if (pp != enemyown::PUB_NONE) h.OnRemoteRow(kGuid, pp == enemyown::PUB_HANDOFF, now);
        if (i > 100 && h.Running(kGuid) && p.Running(kGuid)) ++bothRunning;
    }
    CHECK(bothRunning == 0, "double/a sticky boss run on both machines ends within 1.6 s");
    CHECK(h.State(kGuid) == enemyown::O_LOCAL, "double/the host keeps it");
    CHECK(p.State(kGuid) == enemyown::O_FROZEN, "double/the peer yields and follows");
    CHECK(p.Yields() == 1, "double/the peer yielded exactly once");

    // Rows arrive only every ~1.9 s (a busy machine's row cursor): still resolved.
    enemyown::OwnerSet h2(true), p2(false);
    now = 0;
    for (int i = 0; i < 800; ++i) {
        now += 16;
        h2.Tick(kGuid, true, kEnemy, true, hostPos, true, peerPos, true, now, true);
        p2.Tick(kGuid, true, kEnemy, true, peerPos, true, hostPos, true, now, true);
        if (i % 120 == 0) {
            enemyown::Publish hp = h2.Publishing(kGuid), pp = p2.Publishing(kGuid);
            if (hp != enemyown::PUB_NONE) p2.OnRemoteRow(kGuid, hp == enemyown::PUB_HANDOFF, now);
            if (pp != enemyown::PUB_NONE) h2.OnRemoteRow(kGuid, pp == enemyown::PUB_HANDOFF, now);
        }
    }
    CHECK(h2.Running(kGuid) != p2.Running(kGuid), "double/sparse rows: exactly one owner");
}

static void TestCastleSizedTableKeepsTheQueen()
{
    // Run 36: ~600 enemies filled the 256-record table and the Queen, the newest, got no
    // record -- both machines ran her for the whole fight.  700 enemies, the Queen last.
    static enemyown::OwnerSet h(true), p(false);
    h.Reset(); p.Reset();
    const float hostPos[3] = {350.0f, 0.0f, 0.0f};
    const float peerPos[3] = {250.0f, 0.0f, 0.0f};
    uint32_t now = 1000;
    uint32_t g[4] = {0, 0x11111111u, 0x22222222u, 0x33333333u};
    for (uint32_t k = 1; k <= 700; ++k) {
        g[0] = k * 7919u;
        h.Tick(g, true, kEnemy, true, hostPos, true, peerPos, true, now);
        p.Tick(g, true, kEnemy, true, peerPos, true, hostPos, true, now);
    }
    CHECK(!h.Overflowed() && !p.Overflowed(), "table/700 enemies fit");
    const uint32_t queen[4] = {0xC88FFC0Cu, 0x4A90A87Au, 0xF30BA198u, 0x5A3A2863u};
    for (int i = 0; i < 200; ++i) {
        now += 16;
        h.Tick(queen, true, kEnemy, true, hostPos, true, peerPos, true, now);
        p.Tick(queen, true, kEnemy, true, peerPos, true, hostPos, true, now);
        enemyown::Publish hp = h.Publishing(queen), pp = p.Publishing(queen);
        if (hp != enemyown::PUB_NONE) p.OnRemoteRow(queen, hp == enemyown::PUB_HANDOFF, now);
        if (pp != enemyown::PUB_NONE) h.OnRemoteRow(queen, pp == enemyown::PUB_HANDOFF, now);
    }
    CHECK(h.Running(queen) != p.Running(queen), "table/the 701st enemy has one owner");
    CHECK(h.Count() == 701 && p.Count() == 701, "table/every enemy has a record");
    // Every earlier key is still found (the index survives growth).
    int lost = 0;
    for (uint32_t k = 1; k <= 700; ++k) {
        g[0] = k * 7919u;
        int ds, dp; uint32_t ca, ra; bool sr;
        if (!h.Debug(g, now, &ds, &dp, &ca, &ra, &sr)) ++lost;
    }
    CHECK(lost == 0, "table/earlier keys are still found by the index");
}

static void TestDoubleRunNearerPeerEndsOwningIt()
{
    // Double run, and the peer is clearly the nearer machine (a grab boss's small margin).
    // The peer does not yield first: the host hands it over by distance, and the peer owns.
    enemyown::OwnerSet h(true), p(false);
    const float hostPos[3] = {900.0f, 0.0f, 0.0f};
    const float peerPos[3] = {100.0f, 0.0f, 0.0f};
    uint32_t now = 0;
    int peerFlips = 0, prev = Kind(p);
    for (int i = 0; i < 600; ++i) {
        now += 16;
        h.Tick(kGuid, true, kEnemy, true, hostPos, true, peerPos, true, now, false, 300.0f);
        p.Tick(kGuid, true, kEnemy, true, peerPos, true, hostPos, true, now, false, 300.0f);
        enemyown::Publish hp = h.Publishing(kGuid), pp = p.Publishing(kGuid);
        if (hp != enemyown::PUB_NONE) p.OnRemoteRow(kGuid, hp == enemyown::PUB_HANDOFF, now);
        if (pp != enemyown::PUB_NONE) h.OnRemoteRow(kGuid, pp == enemyown::PUB_HANDOFF, now);
        if (Kind(p) != prev) { ++peerFlips; prev = Kind(p); }
    }
    CHECK(p.State(kGuid) == enemyown::O_LOCAL, "double/the clearly nearer peer ends up running it");
    CHECK(h.State(kGuid) == enemyown::O_FROZEN, "double/the far host follows");
    CHECK(peerFlips == 0, "double/the nearer peer's model never flips");
    CHECK(p.Yields() == 0, "double/the nearer peer did not yield");
}

static void TestMarginBreaksByRole()
{
    // Both players are inside the margin of each other, so no distance resolves.  This is
    // the ordinary co-op case (players stand together), and the rule is the role: the host
    // owns, the peer freezes.  Before this, both machines kept their own AI for every
    // enemy in the area -- measured on the rig as zero hand-offs with 66-160 enemies
    // alive, i.e. two independent simulations of the same fight.
    enemyown::OwnerSet h(true), p(false);
    const float hostPos[3] = {1000.0f, 0.0f, 0.0f};
    const float peerPos[3] = {1500.0f, 0.0f, 0.0f};
    uint32_t now = 0;
    int peerFlips = 0;
    int prev = Kind(p);
    int flipsAfterSettling = 0;
    for (int i = 0; i < 5000; ++i) {
        now += 16;
        Step(h, p, now, hostPos, peerPos, true, true);
        if (Kind(p) != prev) { ++peerFlips; prev = Kind(p); }
        if (i > 4000 && Kind(p) != prev) ++flipsAfterSettling;
    }
    CHECK(h.State(kGuid) == enemyown::O_LOCAL, "own/inside the margin the host runs it");
    CHECK(p.State(kGuid) == enemyown::O_FROZEN, "own/inside the margin the peer follows");
    CHECK(peerFlips <= 3, "own/inside the margin it settles in a few transitions");
    CHECK(flipsAfterSettling == 0, "own/inside the margin it does not flap afterwards");
}

static void TestExactTieGoesToTheHost()
{
    // Exactly equidistant.  The role breaks it, and both machines compute the same
    // answer, so they cannot both yield or both claim.
    enemyown::OwnerSet h(true), p(false);
    const float hostPos[3] = {1000.0f, 0.0f, 0.0f};
    uint32_t now = 0;
    for (int i = 0; i < 400; ++i) {
        now += 16;
        // Each machine's own player sits exactly 1000 cm away; the other machine reads
        // the same distance.
        Step(h, p, now, hostPos, hostPos, true, true);
    }
    CHECK(h.State(kGuid) == enemyown::O_LOCAL, "own/a tie keeps the host running it");
    CHECK(p.State(kGuid) == enemyown::O_FROZEN, "own/a tie freezes the peer");
}

// ===========================================================================
// The claim, and "release only after both agree"
// ===========================================================================
static void TestClaimIsHonouredBeforeDistanceAgrees()
{
    enemyown::OwnerSet h(true), p(false);
    const float hostNear[3] = {100.0f, 0.0f, 0.0f};
    const float peerFar[3]  = {5000.0f, 0.0f, 0.0f};
    uint32_t now = 0;
    for (int i = 0; i < 400; ++i) { now += 16; Step(h, p, now, hostNear, peerFar, true, true); }
    CHECK(p.State(kGuid) == enemyown::O_FROZEN, "claim/precondition: the peer is frozen");

    // The peer's own distance now says IT is nearer, which would keep it frozen until
    // the hold elapsed.  An explicit claim must not wait for that.
    now += 16;
    p.Tick(kGuid, true, kEnemy, true, peerFar, true, hostNear, true, now);
    CHECK(p.State(kGuid) == enemyown::O_FROZEN, "claim/a contradicting distance alone does not wake it");
    p.OnRemoteRow(kGuid, true, now);
    CHECK(p.State(kGuid) == enemyown::O_LOCAL, "claim/an explicit hand-over is honoured at once");
    CHECK(p.Running(kGuid), "claim/the receiver takes over simulation");
}

static void TestHandoffKeepsSimulatingUntilConfirmed()
{
    // The machine under test is the HOST: a peer that runs an enemy the host also runs now
    // yields at once (TestDoubleRunIsResolved), so the hand-off paths are the host's.
    enemyown::OwnerSet p(true);
    const float hostNear[3] = {100.0f, 0.0f, 0.0f};
    const float peerFar[3]  = {5000.0f, 0.0f, 0.0f};
    uint32_t now = 0;
    p.OnRemoteRow(kGuid, false, now);           // the other machine has this enemy
    // Drive the peer to its own hand-off without ever delivering the host's rows.
    for (int i = 0; i < 120; ++i) { now += 16; p.Tick(kGuid, true, kEnemy, true, peerFar, true, hostNear, true, now); }
    CHECK(p.State(kGuid) == enemyown::O_HANDOFF, "claim/the hand-off state is entered");
    CHECK(p.Publishing(kGuid) == enemyown::PUB_HANDOFF, "claim/a hand-off claims ownership of the hand-off");
    CHECK(p.Running(kGuid), "claim/a hand-off keeps simulating until the other confirms");

    // Still unconfirmed one second later: still simulating (not a statue).
    now += 1000;
    p.Tick(kGuid, true, kEnemy, true, peerFar, true, hostNear, true, now);
    CHECK(p.Running(kGuid), "claim/an unanswered claim keeps simulating");

    // The other machine's own rows (claim clear) complete the release.
    p.OnRemoteRow(kGuid, false, now);
    CHECK(p.State(kGuid) == enemyown::O_FROZEN, "claim/confirmation completes the release");
    CHECK(!p.Running(kGuid), "claim/a confirmed release stops simulating");
}

static void TestUnconfirmedHandoffAborts()
{
    // The machine under test is the HOST: a peer that runs an enemy the host also runs now
    // yields at once (TestDoubleRunIsResolved), so the hand-off paths are the host's.
    enemyown::OwnerSet p(true);
    const float hostNear[3] = {100.0f, 0.0f, 0.0f};
    const float peerFar[3]  = {5000.0f, 0.0f, 0.0f};
    uint32_t now = 0;
    p.OnRemoteRow(kGuid, false, now);
    for (int i = 0; i < 120; ++i) { now += 16; p.Tick(kGuid, true, kEnemy, true, peerFar, true, hostNear, true, now); }
    CHECK(p.State(kGuid) == enemyown::O_HANDOFF, "abort/precondition: hand-off");
    // Nothing confirms it.  After kHandoffMs the sender must take its AI back rather
    // than leave the enemy simulated by nobody.
    for (int i = 0; i < 200 && p.State(kGuid) == enemyown::O_HANDOFF; ++i) {
        now += 16;
        p.Tick(kGuid, true, kEnemy, true, peerFar, true, hostNear, true, now);
    }
    CHECK(p.State(kGuid) == enemyown::O_LOCAL, "abort/an unconfirmed hand-off resumes the AI");
}

// Session 26, measured: a frozen host took the Green Dragon back on its own preference while
// the peer kept running it -- two simulations of one boss.  A follower that is still hearing
// its owner must never start simulating on its own.
static void TestFollowerNeverTakesUnilaterally()
{
    enemyown::OwnerSet h(true);
    const float hostPos[3] = {0.0f, 0.0f, 0.0f};
    const float peerFar[3] = {6000.0f, 0.0f, 0.0f};
    uint32_t now = 1000;
    // The host only ever sees the peer's OWN rows (claim bit clear): it is frozen.
    h.OnRemoteRow(kGuid, false, now);
    h.Tick(kGuid, false, kEnemy, true, hostPos, true, peerFar, true, now);   // no actor yet
    CHECK(h.State(kGuid) == enemyown::O_FROZEN, "unilateral/precondition: the host follows");
    // Now the host holds its own actor and is far nearer.  The peer keeps publishing.
    for (int i = 0; i < 300; ++i) {
        now += 16;
        h.OnRemoteRow(kGuid, false, now);
        h.Tick(kGuid, true, kEnemy, true, hostPos, true, peerFar, true, now);
    }
    CHECK(h.State(kGuid) == enemyown::O_FROZEN,
          "unilateral/a follower that hears its owner does not start simulating");
    // The owner hands it over (claim bit): now the host takes it.
    h.OnRemoteRow(kGuid, true, now);
    CHECK(h.State(kGuid) == enemyown::O_LOCAL, "unilateral/the owner's claim hands it over");
}

// A boss never changes owner by distance: an owner with `sticky` keeps it however far its
// player walks.
static void TestBossIsSticky()
{
    // The machine under test is the HOST: a peer that runs an enemy the host also runs now
    // yields at once (TestDoubleRunIsResolved), so the hand-off paths are the host's.
    enemyown::OwnerSet p(true);
    const float peerFar[3] = {6000.0f, 0.0f, 0.0f};
    const float hostNear[3] = {10.0f, 0.0f, 0.0f};
    uint32_t now = 1000;
    for (int i = 0; i < 400; ++i) {
        now += 16;
        p.OnRemoteRow(kGuid, false, now);   // the host is present (it has a copy)
        p.Tick(kGuid, true, kEnemy, true, peerFar, true, hostNear, true, now, true);
    }
    CHECK(p.Publishing(kGuid) == enemyown::PUB_OWN, "sticky/a boss owner never hands off");
    // The same enemy without `sticky` would have been handed to the nearer host.
    // (Unconfirmed, a hand-off cycles back after kHandoffMs, so ask whether one STARTED.)
    enemyown::OwnerSet q(true);
    bool handedOff = false;
    for (int i = 0; i < 400; ++i) {
        now += 16;
        q.OnRemoteRow(kGuid, false, now);
        q.Tick(kGuid, true, kEnemy, true, peerFar, true, hostNear, true, now, false);
        if (q.Publishing(kGuid) == enemyown::PUB_HANDOFF) handedOff = true;
    }
    CHECK(handedOff, "sticky/an ordinary enemy is handed to the nearer machine");
}

static void TestSilentOwnerIsReleased()
{
    enemyown::OwnerSet h(true), p(false);
    const float hostNear[3] = {100.0f, 0.0f, 0.0f};
    const float peerFar[3]  = {5000.0f, 0.0f, 0.0f};
    uint32_t now = 0;
    for (int i = 0; i < 400; ++i) { now += 16; Step(h, p, now, hostNear, peerFar, true, true); }
    CHECK(p.State(kGuid) == enemyown::O_FROZEN, "silence/precondition: the peer follows");

    // The owner stops sending (crash, level unload).  The copy must not stay frozen.
    // Stop at the first change: it must be FROZEN -> LOCAL, not straight to a claim.
    for (int i = 0; i < 200; ++i) {
        now += 16;
        p.Tick(kGuid, true, kEnemy, true, peerFar, true, hostNear, true, now);
        if (p.State(kGuid) != enemyown::O_FROZEN) break;
    }
    CHECK(p.State(kGuid) == enemyown::O_LOCAL, "silence/a frozen copy takes its AI back");
}

// ===========================================================================
// Guards that stop a hand-off to nowhere
// ===========================================================================
static void TestNeverHandOffToAMachineThatHasNotShownTheEnemy()
{
    enemyown::OwnerSet p(false);
    const float hostNear[3] = {100.0f, 0.0f, 0.0f};
    const float peerFar[3]  = {5000.0f, 0.0f, 0.0f};
    uint32_t now = 0;
    // No OnRemoteRow is ever delivered: the other machine has never published this GUID,
    // so it may not hold a live copy.  Handing over would freeze it on both machines.
    for (int i = 0; i < 400; ++i) {
        now += 16;
        p.Tick(kGuid, true, kEnemy, true, peerFar, true, hostNear, true, now);
    }
    CHECK(p.State(kGuid) == enemyown::O_LOCAL, "guard/no evidence the other holds it: keep running");
}

static void TestNoLocalActorMeansFrozen()
{
    enemyown::OwnerSet p(false);
    const float hostNear[3] = {100.0f, 0.0f, 0.0f};
    const float peerFar[3]  = {5000.0f, 0.0f, 0.0f};
    uint32_t now = 0;
    p.OnRemoteRow(kGuid, false, now);
    for (int i = 0; i < 200; ++i) {
        now += 16;
        p.Tick(kGuid, false, kEnemy, false, peerFar, true, hostNear, true, now);
    }
    CHECK(p.State(kGuid) == enemyown::O_FROZEN, "guard/no local actor: this machine follows");
    CHECK(p.Publishing(kGuid) == enemyown::PUB_NONE, "guard/no local actor: it publishes nothing");
}

// ===========================================================================
// Termination: the pathological mutual hand-off settles
// ===========================================================================
static void TestMutualHandoffSettles()
{
    // Both machines believe the OTHER is nearer -- the one situation only stale player
    // positions can produce.  Both reach hand-off, then each receives the other's
    // claim.  The role tie-break must leave exactly one owner, and the dispute latch
    // must stop them passing it back for ever.
    enemyown::OwnerSet h(true), p(false);
    const float hostSees[3] = {1000.0f, 0.0f, 0.0f};   // host's own player
    const float hostSeesPeer[3] = {0.0f, 0.0f, 0.0f};  // the host's stale view of the peer
    const float peerSees[3] = {2000.0f, 0.0f, 0.0f};   // the peer's own player
    const float peerSeesHost[3] = {1000.0f, 0.0f, 0.0f}; // the peer's stale view of the host

    uint32_t now = 0;
    h.OnRemoteRow(kGuid, false, now);
    p.OnRemoteRow(kGuid, false, now);
    for (int i = 0; i < 200; ++i) {
        now += 16;
        h.Tick(kGuid, true, kEnemy, true, hostSees, true, hostSeesPeer, true, now);
        p.Tick(kGuid, true, kEnemy, true, peerSees, true, peerSeesHost, true, now);
    }
    CHECK(h.State(kGuid) == enemyown::O_HANDOFF, "dispute/both machines hand it over");
    CHECK(p.State(kGuid) == enemyown::O_HANDOFF, "dispute/both machines hand it over (peer)");

    // Both claims cross in flight.
    now += 16;
    h.OnRemoteRow(kGuid, true, now);
    p.OnRemoteRow(kGuid, true, now);
    CHECK(h.State(kGuid) == enemyown::O_LOCAL, "dispute/the host wins a mutual hand-off");
    CHECK(p.State(kGuid) == enemyown::O_FROZEN, "dispute/the peer yields");
    CHECK(h.Running(kGuid) != p.Running(kGuid), "dispute/exactly one machine simulates");

    // Keep exchanging rows for a long time and count every ownership change.  A rule
    // that flapped would count hundreds here; a rule that terminates counts a handful.
    int flips = 0;
    int prev = Kind(h);
    int quiet = 0;
    for (int i = 0; i < 20000; ++i) {
        now += 16;
        h.Tick(kGuid, true, kEnemy, true, hostSees, true, hostSeesPeer, true, now);
        p.Tick(kGuid, true, kEnemy, true, peerSees, true, peerSeesHost, true, now);
        enemyown::Publish hp = h.Publishing(kGuid);
        enemyown::Publish pp = p.Publishing(kGuid);
        if (hp != enemyown::PUB_NONE) p.OnRemoteRow(kGuid, hp == enemyown::PUB_HANDOFF, now);
        if (pp != enemyown::PUB_NONE) h.OnRemoteRow(kGuid, pp == enemyown::PUB_HANDOFF, now);
        if (Kind(h) != prev) { ++flips; prev = Kind(h); quiet = 0; }
        else ++quiet;
    }
    CHECK(flips <= 4, "dispute/ownership settles, it does not flap");
    CHECK(quiet > 1000, "dispute/the last second is quiet");
    CHECK(h.Running(kGuid) != p.Running(kGuid), "dispute/exactly one owner at the end");
}

static void TestZeroGuidIsIgnored()
{
    enemyown::OwnerSet s(true);
    const uint32_t zero[4] = {0, 0, 0, 0};
    s.OnRemoteRow(zero, true, 100);
    s.Tick(zero, true, kEnemy, true, kEnemy, true, kEnemy, true, 200);
    CHECK(s.Count() == 0, "guid/a zero GUID is not an enemy");
    CHECK(s.State(zero) == enemyown::O_LOCAL, "guid/a zero GUID reads as local");
}

// ===========================================================================
// Animation and status classification
// ===========================================================================
static void TestClassifyAnim()
{
    // The helpers promise case-insensitivity on BOTH sides.  The first version folded
    // only the haystack, so a needle with an uppercase letter never matched -- measured
    // live as a level-visibility guard that always answered "no" (Session 6).
    CHECK(enemyown::HasCI("VideoConfigMap", "videoconfig"), "hasci/lowercase needle");
    CHECK(enemyown::HasCI("VideoConfigMap", "VideoConfig"), "hasci/uppercase needle");
    CHECK(enemyown::HasCI("videoConfigMap", "VIDEOCONFIG"), "hasci/all-caps needle");
    CHECK(!enemyown::HasCI("VideoConfigMap", "ShaderCompile"), "hasci/absent needle");
    CHECK(enemyown::EndsCI("Rat_Die", "die"), "endsci/lowercase tail");
    CHECK(enemyown::EndsCI("Rat_Die", "Die"), "endsci/uppercase tail");
    CHECK(!enemyown::EndsCI("Rat_Die", "Dead"), "endsci/absent tail");

    CHECK(enemyown::ClassifyAnim("Enemy_Attack_01") == wire::E_ATTACK, "anim/attack");
    CHECK(enemyown::ClassifyAnim("Spider_Slash_Right") == wire::E_ATTACK, "anim/slash");
    CHECK(enemyown::ClassifyAnim("Grunt_Hurt_React") == wire::E_HURT, "anim/hurt");
    CHECK(enemyown::ClassifyAnim("Grunt_HitReact_Front") == wire::E_HURT, "anim/hit reaction");
    CHECK(enemyown::ClassifyAnim("Rat_Die") == wire::E_DIE, "anim/die");
    CHECK(enemyown::ClassifyAnim("Boss_Death") == wire::E_DIE, "anim/death");
    CHECK(enemyown::ClassifyAnim("Boss_Roar") == wire::E_ROAR, "anim/roar");
    CHECK(enemyown::ClassifyAnim("Mage_Cast_A") == wire::E_CAST, "anim/cast");
    CHECK(enemyown::ClassifyAnim("Toad_Spit") == wire::E_CAST, "anim/spit is a ranged cast");
    CHECK(enemyown::ClassifyAnim("Grunt_Walk") == wire::E_MOVE, "anim/walk");
    CHECK(enemyown::ClassifyAnim("Grunt_Idle") == wire::E_NONE, "anim/idle is no kind");
    CHECK(enemyown::ClassifyAnim("Soldier_Walk") == wire::E_MOVE,
          "anim/'soldier' is not read as a death");
    CHECK(enemyown::ClassifyAnim(nullptr) == wire::E_NONE, "anim/null is no kind");
}

static void TestStatusMask()
{
    CHECK(enemyown::StatusMask(false, false) == 0, "status/nothing set");
    CHECK(enemyown::StatusMask(true, false) == (uint16_t)wire::ST_POISON, "status/poison");
    CHECK(enemyown::StatusMask(false, true) == (uint16_t)wire::ST_HEAVY, "status/stagger is heavy");
    CHECK(enemyown::StatusMask(true, true) == (uint16_t)(wire::ST_POISON | wire::ST_HEAVY),
          "status/both bits pack");
}

// ===========================================================================
int main()
{
    printf("=== qd enemy-ownership tests ===\n");
    TestNearerMachineWins();
    TestHoldPreventsAnEarlySwitch();
    TestDoubleRunIsResolved();
    TestDoubleRunNearerPeerEndsOwningIt();
    TestCastleSizedTableKeepsTheQueen();
    TestMarginBreaksByRole();
    TestExactTieGoesToTheHost();
    TestClaimIsHonouredBeforeDistanceAgrees();
    TestHandoffKeepsSimulatingUntilConfirmed();
    TestUnconfirmedHandoffAborts();
    TestSilentOwnerIsReleased();
    TestFollowerNeverTakesUnilaterally();
    TestBossIsSticky();
    TestNeverHandOffToAMachineThatHasNotShownTheEnemy();
    TestNoLocalActorMeansFrozen();
    TestMutualHandoffSettles();
    TestZeroGuidIsIgnored();
    TestClassifyAnim();
    TestStatusMask();
    printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
