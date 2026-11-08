// test_cutscene.cpp -- cutscene / sequencer coordination (SYNC-GAPS §4).
//
// What these tests exist for:
//   * a cutscene the partner is playing must be played here too ONCE, not once per
//     frame (a repeated BeginCutsceneWithManager would restart the sequence);
//   * when the manager cannot be resolved, the local player is HELD consistently, and
//     released exactly once when the partner reports ENDED;
//   * a sender that goes silent must not leave the partner's input locked for ever;
//   * an older seq is discarded, so a reordered datagram cannot un-end a cutscene.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "qdsequence.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, name)                                                          \
    do {                                                                           \
        if (cond) { ++g_pass; }                                                    \
        else { ++g_fail; printf("FAIL %s  (%s:%d)\n", name, __FILE__, __LINE__); } \
    } while (0)

using namespace qd;
using namespace qd::sequence;

static void TestPlayOnce()
{
    CutsceneBoard b;
    CHECK(b.Decide(0, false, "", 1000) == CV_NONE, "idle/no peer is nothing");
    CHECK(b.Accept(1, 1, true, false, "SipehaTransitionCutsceneManager", 1000),
          "play/a playing state is accepted");
    CHECK(b.Decide(0, false, "", 1100) == CV_PLAY, "play/the manager is played once");
    CHECK(b.Held(), "play/the player is marked held");
    CHECK(b.Decide(0, false, "", 1200) == CV_WAIT,
          "play/a second frame does not replay the sequence");
    CHECK(b.Decide(0, true, "SipehaTransitionCutsceneManager", 1300) == CV_NONE,
          "play/already in the same sequence is nothing to do");
}

static void TestHoldAndRelease()
{
    CutsceneBoard b;
    // An unnamed / unresolvable manager is held, not guessed at.
    CHECK(b.Accept(1, 1, true, false, "", 1000), "hold/an unnamed playing state is accepted");
    CHECK(b.Decide(0, false, "", 1100) == CV_HOLD, "hold/no manager means a consistent hold");
    CHECK(b.Decide(0, false, "", 1200) == CV_WAIT, "hold/the hold is not re-applied");
    b.OnPlayFailed();
    CHECK(b.Held(), "hold/a failed play falls back to the hold");
    CHECK(b.Accept(1, 2, false, true, "", 1300), "release/the ended state is accepted");
    CHECK(b.Decide(0, false, "", 1400) == CV_RELEASE, "release/ended releases exactly once");
    CHECK(!b.Held(), "release/the hold is cleared");
    CHECK(b.Decide(0, false, "", 1500) == CV_NONE, "release/a second frame is nothing");
}

static void TestStaleSenderReleases()
{
    CutsceneBoard b;
    CHECK(b.Accept(1, 1, true, false, "Ending1CutsceneManager", 1000), "stale/playing accepted");
    CHECK(b.Decide(0, false, "", 1100) == CV_PLAY, "stale/played");
    // The sender crashes: no more datagrams.  After kStaleMs the state is not trusted,
    // so the partner's input is released rather than locked for the session.
    CHECK(b.Decide(0, false, "", 1000 + kStaleMs + 1) == CV_RELEASE,
          "stale/a silent sender releases the hold");
}

static void TestSeqOrdering()
{
    CutsceneBoard b;
    CHECK(b.Accept(1, 5, true, false, "A", 1000), "seq/new state accepted");
    CHECK(!b.Accept(1, 4, true, false, "B", 1100), "seq/an older seq is discarded");
    CHECK(!b.Accept(1, 5, false, true, "A", 1200), "seq/the same seq does not end it");
    // A repeat refreshes liveness, so a held partner stays held while the heartbeats
    // keep arriving.
    CHECK(b.Decide(0, false, "", 1000 + kStaleMs - 100) == CV_PLAY,
          "seq/a repeat keeps the sender live");
    CHECK(b.Accept(1, 6, false, true, "A", 2000), "seq/a newer ended state is accepted");
    CHECK(b.Decide(0, false, "", 2100) == CV_RELEASE, "seq/and releases the hold");
}

static void TestDifferentCutscene()
{
    CutsceneBoard b;
    // Both machines are in different cutscenes.  Interrupting the local one would be a
    // worse divergence than waiting for the first to finish.
    CHECK(b.Accept(1, 1, true, false, "Ending1CutsceneManager", 1000), "different/peer playing");
    CHECK(b.Decide(0, true, "IntroCutscene", 1100) == CV_WAIT,
          "different/a local cutscene is not interrupted");
}

int main()
{
    printf("=== qd cutscene tests ===\n");
    TestPlayOnce();
    TestHoldAndRelease();
    TestStaleSenderReleases();
    TestSeqOrdering();
    TestDifferentCutscene();
    printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
