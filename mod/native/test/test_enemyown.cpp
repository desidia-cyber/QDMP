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
    enemyown::OwnerSet h(true), p(false);
    const float hostNear[3] = {100.0f, 0.0f, 0.0f};
    const float peerFar[3]  = {5000.0f, 0.0f, 0.0f};
    uint32_t now = 0;
    // One second: the peer prefers to own it but the hold has not elapsed.
    for (int i = 0; i < 60; ++i) { now += 16; Step(h, p, now, hostNear, peerFar, true, true); }
    CHECK(p.State(kGuid) == enemyown::O_LOCAL,
          "own/a preference younger than the hold does not switch");
    // Past the hold, it does.
    for (int i = 0; i < 120; ++i) { now += 16; Step(h, p, now, hostNear, peerFar, true, true); }
    CHECK(p.State(kGuid) == enemyown::O_FROZEN, "own/the switch happens once the hold passes");
}

static void TestMarginPreventsFlapping()
{
    // Both players are inside the margin of each other, so neither preference resolves
    // and both keep the current owner.  This is the case the margin exists for; it is
    // also why both may simulate at once instead of one freezing for no reason.
    enemyown::OwnerSet h(true), p(false);
    const float hostPos[3] = {1000.0f, 0.0f, 0.0f};
    const float peerPos[3] = {1500.0f, 0.0f, 0.0f};
    uint32_t now = 0;
    int flips = 0;
    int prev = Kind(h);
    for (int i = 0; i < 5000; ++i) {
        now += 16;
        Step(h, p, now, hostPos, peerPos, true, true);
        if (Kind(h) != prev) { ++flips; prev = Kind(h); }
    }
    CHECK(flips == 0, "own/inside the margin nothing switches");
    CHECK(h.State(kGuid) == enemyown::O_LOCAL && p.State(kGuid) == enemyown::O_LOCAL,
          "own/inside the margin both keep their AI");
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
    enemyown::OwnerSet h(true), p(false);
    const float hostNear[3] = {100.0f, 0.0f, 0.0f};
    const float peerFar[3]  = {5000.0f, 0.0f, 0.0f};
    uint32_t now = 0;
    p.OnRemoteRow(kGuid, false, now);           // the host has this enemy
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
    enemyown::OwnerSet p(false);
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
    TestMarginPreventsFlapping();
    TestExactTieGoesToTheHost();
    TestClaimIsHonouredBeforeDistanceAgrees();
    TestHandoffKeepsSimulatingUntilConfirmed();
    TestUnconfirmedHandoffAborts();
    TestSilentOwnerIsReleased();
    TestNeverHandOffToAMachineThatHasNotShownTheEnemy();
    TestNoLocalActorMeansFrozen();
    TestMutualHandoffSettles();
    TestZeroGuidIsIgnored();
    TestClassifyAnim();
    TestStatusMask();
    printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
