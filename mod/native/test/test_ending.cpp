// test_ending.cpp -- the four endings as a driven fact: the verb map, the choice transmitter,
// and the wake-up's staging.
//
// WHY EACH IS A UNIT TEST AND NOT A LIVE RUN:
//   * the VERB MAP is the whole mechanism.  If `Banish` and `Eat` were swapped, both players
//     would watch a valid ending and the run would look perfect; only the wrong one would be
//     in the save.  A table test is the only cheap way to pin that.
//   * the TRANSMITTER's repair window decides whether a partner who joined a second late still
//     gets the choice.  A live run shows "one player saw the ending", not the clock rule.
//   * the WAKE-UP is a sequence of gates with a 20 s level-management hold, a load-lock cancel,
//     a rescue call and three retries -- all inside 25 s, so a live run cannot walk the order.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "qdending.h"
#include "qdwire.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, name)                                                          \
    do {                                                                           \
        if (cond) { ++g_pass; }                                                    \
        else { ++g_fail; printf("FAIL %s  (%s:%d)\n", name, __FILE__, __LINE__); } \
    } while (0)

using namespace qd;
using namespace qd::ending;

// ---------------------------------------------------------------------------
// The verb map
// ---------------------------------------------------------------------------
static void TestVerbMap()
{
    CHECK(!strcmp(EndingVerb(kEndQueen1), "Ending1Complete"), "verb/ending 1 is the Queen's Ending1Complete");
    CHECK(!strcmp(EndingVerb(kEndQueen2), "Ending2CutsceneEnded"), "verb/ending 2 is Ending2CutsceneEnded");
    CHECK(!strcmp(EndingVerb(kEndSipeha1), "Banish"), "verb/ending 3 is the Sipeha NPC's Banish");
    CHECK(!strcmp(EndingVerb(kEndSipeha2), "Eat"), "verb/ending 4 is the Sipeha NPC's Eat");
    CHECK(EndingVerb(kEndNone)[0] == 0, "verb/no ending has no verb");
    CHECK(EndingVerb(9)[0] == 0, "verb/an out-of-range ending has no verb");

    // The observation side must be the exact inverse, or a published body would name an ending
    // the sender did not take.
    for (uint8_t e = kEndQueen1; e <= kEndSipeha2; ++e)
        CHECK(EndingFromVerb(EndingVerb(e)) == e, "verb/EndingFromVerb is the inverse of EndingVerb");

    CHECK(EndingFromVerb("CustomEvent") == kEndNone, "verb/a common event name is not a choice");
    CHECK(EndingFromVerb("Ending3Complete") == kEndNone, "verb/the FLAG name is not a verb");
    CHECK(EndingFromVerb("") == kEndNone, "verb/an empty name is not a choice");
    CHECK(EndingFromVerb(nullptr) == kEndNone, "verb/a null name is not a choice");
}

// ---------------------------------------------------------------------------
// The flag fallback
// ---------------------------------------------------------------------------
static void TestFlagMap()
{
    CHECK(EndingFromFlag("Ending1Complete", 1) == kEndQueen1, "flag/Ending1Complete -> 1");
    CHECK(EndingFromFlag("Ending2Complete", 1) == kEndQueen2, "flag/Ending2Complete -> 2");
    CHECK(EndingFromFlag("Ending3Complete", 1) == kEndSipeha1, "flag/Ending3Complete -> 3");
    CHECK(EndingFromFlag("Ending4Complete", 1) == kEndSipeha2, "flag/Ending4Complete -> 4");
    CHECK(EndingFromFlag("Ending1Complete", 0) == kEndNone, "flag/a zero value is not a choice");
    CHECK(EndingFromFlag("EndingSeen", 1) == kEndNone, "flag/EndingSeen alone names no branch");
    CHECK(EndingFromFlag("BridgeLordDead", 1) == kEndNone, "flag/a boss flag is not an ending");
}

// ---------------------------------------------------------------------------
// Branch and class
// ---------------------------------------------------------------------------
static void TestBranch()
{
    CHECK(EndingBranch(kEndQueen1) == kBranchQueen, "branch/ending 1 is the Queen's");
    CHECK(EndingBranch(kEndQueen2) == kBranchQueen, "branch/ending 2 is the Queen's");
    CHECK(EndingBranch(kEndSipeha1) == kBranchSipeha, "branch/ending 3 is Sipeha's");
    CHECK(EndingBranch(kEndSipeha2) == kBranchSipeha, "branch/ending 4 is Sipeha's");
    CHECK(EndingBranch(kEndNone) == kBranchNone, "branch/no ending has no branch");

    CHECK(ClassInBranch(kBranchQueen, "Queen_BP_C"), "class/Queen_BP_C carries a Queen ending");
    CHECK(ClassInBranch(kBranchQueen, "Queen_BP_Super_C"), "class/a Queen subclass matches by substring");
    CHECK(!ClassInBranch(kBranchQueen, "Sipeha_NPC_C"), "class/a Sipeha NPC cannot carry a Queen ending");
    CHECK(ClassInBranch(kBranchSipeha, "Sipeha_NPC_C"), "class/Sipeha_NPC_C carries a Sipeha ending");
    CHECK(ClassInBranch(kBranchSipeha, "Sipeha_BP_C"), "class/the Sipeha fight body also matches");
    CHECK(!ClassInBranch(kBranchSipeha, "Queen_BP_C"), "class/the Queen cannot carry a Sipeha ending");
    CHECK(!ClassInBranch(kBranchNone, "Queen_BP_C"), "class/no branch matches nothing");
    CHECK(!ClassInBranch(kBranchQueen, nullptr), "class/a null class matches nothing");
    CHECK(!ClassInBranch(kBranchQueen, ""), "class/an empty class matches nothing");
}

// ---------------------------------------------------------------------------
// Should the receiver drive its own copy?
// ---------------------------------------------------------------------------
static void TestDriveDecision()
{
    CHECK(ShouldDriveLocalEnding(false, true) == kDriveNow, "drive/not chosen yet + a live copy -> drive");
    CHECK(ShouldDriveLocalEnding(true, true) == kDriveSkipChosen, "drive/already chosen -> do nothing (never run an ending twice)");
    CHECK(ShouldDriveLocalEnding(true, false) == kDriveSkipChosen, "drive/already chosen wins over a missing copy");
    CHECK(ShouldDriveLocalEnding(false, false) == kDriveNoCopy, "drive/no copy -> report, never invent one");
}

// ---------------------------------------------------------------------------
// The choice transmitter
// ---------------------------------------------------------------------------
static void TestChoiceTx()
{
    ChoiceTx tx;
    tx.Reset();
    CHECK(!tx.ShouldSend(1000), "tx/nothing chosen -> nothing sent");
    CHECK(!tx.Begin(kEndNone, 1, 1000), "tx/ending 0 is refused");
    CHECK(!tx.Begin(9, 1, 1000), "tx/an out-of-range ending is refused");
    CHECK(tx.Begin(kEndSipeha1, 2, 1000), "tx/a real choice is recorded");
    CHECK(tx.chosen && tx.ending == kEndSipeha1 && tx.who == 2, "tx/the body carries the ending and who chose");
    CHECK(tx.ShouldSend(1000), "tx/the first publication is immediate");
    CHECK(tx.NextRev() == 1 && tx.rev == 1, "tx/rev 1 is the first revision");
    tx.lastSentAt = 1000;

    CHECK(!tx.ShouldSend(3000), "tx/no repair before the repair clock");
    CHECK(tx.ShouldSend(6000), "tx/a repair after 5 s");
    tx.lastSentAt = 46000;
    CHECK(!tx.ShouldSend(46001), "tx/no repairs after the 45 s window");
    CHECK(tx.Repairing(), "tx/a sent choice reports itself as a repair source");

    // A SECOND, DIFFERENT ending cannot be this machine's own.  The first is kept -- a
    // broadcast that overwrote it would let a late duplicate change the branch.
    CHECK(!tx.Begin(kEndSipeha2, 2, 47000), "tx/a different ending is refused once one is chosen");
    CHECK(tx.ending == kEndSipeha1, "tx/the first chosen ending is kept");
    CHECK(tx.Begin(kEndSipeha1, 2, 48000), "tx/the SAME ending is idempotent");

    // Rev never returns to 0 (0 means "no revision known").
    ChoiceTx t2; t2.Reset(); t2.rev = 0xFFFFFFFFu;
    CHECK(t2.NextRev() == 1, "tx/a wrapped revision returns to 1");
}

// ---------------------------------------------------------------------------
// The wake-up
// ---------------------------------------------------------------------------
static void TestWakeRules()
{
    WakeRules w;
    w.Reset();
    CHECK(w.Step(1000, false) == kWakeNone, "wake/before any ending there is nothing to do");
    CHECK(w.LevelDriveAllowed(1000), "wake/level management is the game's when no wake is pending");

    w.OnEndingEnded(10000);
    CHECK(w.Pending(10000), "wake/a pending wake is open");
    CHECK(!w.LevelDriveAllowed(10000), "wake/the mod STOPS driving levels while the wake is pending");
    CHECK(!w.LevelDriveAllowed(29999), "wake/the hold lasts 20 s");
    CHECK(w.LevelDriveAllowed(30000), "wake/after the hold the game's manager is left alone again");

    // The game's own chain is given 8 s before the mod touches anything.
    CHECK(w.Step(12000, false) == kWakeNone, "wake/nothing is done in the first 8 s (the game may be loading)");
    CHECK(w.Step(18000, false) == kWakeCancelLock, "wake/the load lock is cancelled at 8 s");
    CHECK(w.Step(19000, false) == kWakePostWake, "wake/PostEndingWakeOnBeach is called at 9 s");
    CHECK(w.posts == 1, "wake/the first post-wake is counted");
    CHECK(w.Step(20000, false) == kWakeNone, "wake/one call, then wait a retry interval");
    CHECK(w.Step(22000, false) == kWakeNone, "wake/no retry before the retry clock (4 s after the first call)");
    CHECK(w.Step(23000, false) == kWakePostWake, "wake/the first retry, 4 s after the first call");
    CHECK(w.Step(27000, false) == kWakePostWake, "wake/the second retry");
    CHECK(w.posts == 3, "wake/three posts in total, never a storm");
    CHECK(w.Step(30000, false) == kWakeNone, "wake/no fourth post");
    CHECK(w.Step(35000, false) == kWakeStopFade, "wake/the held fade is stopped at 25 s");
    CHECK(w.Step(40000, false) == kWakeNone, "wake/stop-fade happens once");
    CHECK(w.Pending(35000), "wake/the wake is still open until it is seen or given up on");
    int gaveUp = 0;
    for (uint32_t t = 36000; t < 60000; t += 1000) { if (!w.Pending(t)) { gaveUp = 1; break; } }
    CHECK(gaveUp, "wake/the rules give up at 45 s rather than holding level management for ever");

    CHECK(!strcmp(w.Who(), "after this module ran PostEndingWakeOnBeach"),
          "wake/the log names the rescue when the module had to run it");

    // A WAKE THAT PLAYS CLOSES THE RULES AND IS CREDITED TO THE GAME when the module never posted.
    WakeRules g;
    g.Reset();
    g.OnEndingEnded(1000);
    CHECK(g.Step(2000, true) == kWakeSeen, "wake/a played wake-up is reported as seen");
    CHECK(!g.Pending(2000), "wake/seen closes the rules");
    CHECK(g.LevelDriveAllowed(2000), "wake/level driving is restored as soon as the wake is seen");
    CHECK(!strcmp(g.Who(), "the game's own"), "wake/the log credits the game's own wake-up when nothing was posted");
    CHECK(g.Step(3000, true) == kWakeNone, "wake/a second observation after closing does nothing");

    // A second ending line while the rules are open must not restart the hold.
    WakeRules d;
    d.Reset();
    d.OnEndingEnded(1000);
    d.OnEndingEnded(5000);
    CHECK(d.endedAt == 1000, "wake/a second ending does not restart the hold");
    CHECK(d.Step(18000, false) == kWakeCancelLock, "wake/the stage still advances from the FIRST ending");

    // THE GATE IS PER-MACHINE: a machine whose wake is pending still answers the PARTNER.
    // (That is structural: the gate is only consulted by the local level pump.)
    WakeRules b;
    b.Reset();
    b.OnEndingEnded(0);
    CHECK(b.endedAt == 1, "wake/time zero is normalised (0 means 'no ending')");
    CHECK(!b.Pending(100000), "wake/a never-ending is not pending");
}

// ---------------------------------------------------------------------------
// The body is the wire's, and its size is the contract
// ---------------------------------------------------------------------------
static void TestBody()
{
    wire::BossEndBody b;
    memset(&b, 0, sizeof(b));
    b.guid[0] = 0xC88FFC0C; b.guid[1] = 1; b.guid[2] = 2; b.guid[3] = 3;
    b.rev = 7;
    b.ending = kEndSipeha2;
    b.who = wire::kBossPlayerPeer;
    uint8_t buf[64];
    wire::Writer w(buf, sizeof(buf));
    wire::WriteBossEnd(w, b);
    CHECK(w.ok, "body/writes");
    CHECK(w.len == wire::kBossEndBytes, "body/is exactly the documented 22 bytes");
    CHECK(wire::kBossEndBytes == 22, "body/the documented size is 22 bytes");
    wire::Reader r(buf, w.len);
    wire::BossEndBody c;
    CHECK(wire::ReadBossEnd(r, &c), "body/reads back");
    CHECK(c.rev == 7 && c.ending == kEndSipeha2 && c.who == wire::kBossPlayerPeer,
          "body/round-trips every field");
    CHECK(c.guid[0] == 0xC88FFC0C && c.guid[3] == 3, "body/round-trips the key");
}

int main()
{
    TestVerbMap();
    TestFlagMap();
    TestBranch();
    TestDriveDecision();
    TestChoiceTx();
    TestWakeRules();
    TestBody();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
