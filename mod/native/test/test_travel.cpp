// test_travel.cpp -- the travel FOLLOW board (SYNC-GAPS G6, the "make the partner
// follow" half).
//
// What these tests exist for, in the order the failures would bite:
//
//   * `dest` has to be a name both machines can resolve.  A malformed or truncated
//     name resolves to a different actor (or to none), so the split/format is tested
//     before any state is.
//   * A heartbeat repeats the same (seq, dest) every 2 s.  Applying it twice is two
//     teleports; the board must act once.
//   * A follow must not fight the player: a local teleport, a menu, a cutscene or a
//     conversation skips it.  The skip is not remembered, so the follow can still
//     happen once the player is free.
//   * A destination that fails to resolve must not be retried every heartbeat for the
//     rest of the session -- that is the "spin" the acceptance criteria forbid.
//   * A follower's own pawn then reports the same destination.  Announcing it would
//     make the partner teleport back; the echo guard stops it after one hop.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "qdtravel.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, name)                                                          \
    do {                                                                           \
        if (cond) { ++g_pass; }                                                    \
        else { ++g_fail; printf("FAIL %s  (%s:%d)\n", name, __FILE__, __LINE__); } \
    } while (0)

using namespace qd;
using namespace qd::travel;

// ===========================================================================
// "<actor>/<world>" -- the only identity both machines can spell the same way
// ===========================================================================
static void TestDestinationForm()
{
    DestParts p;
    CHECK(SplitDest("Teleporter_500_AUT_02/100_GR", &p), "dest/accepts the wire form");
    CHECK(SameText(p.actor, "Teleporter_500_AUT_02"), "dest/splits the actor");
    CHECK(SameText(p.world, "100_GR"), "dest/splits the world");

    CHECK(!SplitDest(nullptr, &p), "dest/refuses null");
    CHECK(!SplitDest("", &p), "dest/refuses empty");
    CHECK(!SplitDest("Teleporter_500_AUT_02", &p), "dest/refuses a missing world");
    CHECK(!SplitDest("/100_GR", &p), "dest/refuses an empty actor");
    CHECK(SplitDest("Teleporter/100_GR", &p), "dest/accepts one slash");
    CHECK(!SplitDest("A/B/C", &p), "dest/refuses two slashes");
    CHECK(!SplitDest("Teleporter_500_AUT_02/", &p), "dest/refuses an empty world");
    CHECK(!SplitDest("Teleporter 500/100_GR", &p), "dest/refuses a space in the actor");
    CHECK(!SplitDest("Teleporter/10 0_GR", &p), "dest/refuses a space in the world");
    CHECK(!SplitDest("Teleporter/Game/Maps", &p), "dest/refuses a whole path as a world");

    char out[kDestCap];
    CHECK(MakeDest("Teleporter_500_AUT_02", "100_GR", out, sizeof(out)),
          "dest/formats an actor and a world");
    CHECK(SameText(out, "Teleporter_500_AUT_02/100_GR"), "dest/formats '<actor>/<world>'");
    CHECK(SplitDest(out, &p) && SameText(p.actor, "Teleporter_500_AUT_02") &&
          SameText(p.world, "100_GR"), "dest/round-trips");

    CHECK(!MakeDest(nullptr, "100_GR", out, sizeof(out)), "dest/refuses a null actor");
    CHECK(!MakeDest("", "100_GR", out, sizeof(out)), "dest/refuses an empty actor");
    CHECK(!MakeDest("Checkpoint_2", "", out, sizeof(out)), "dest/refuses an empty world");
    CHECK(!MakeDest("Checkpoint_2", "/Game/Maps/100_GR", out, sizeof(out)),
          "dest/refuses a whole path as a world");

    // The wire field is 96 bytes.  A name that cannot fit is refused, never cut: a
    // truncated actor name is a different actor.
    char big[200];
    for (int i = 0; i < (int)sizeof(big) - 1; ++i) big[i] = 'A';
    big[sizeof(big) - 1] = 0;
    char small[16];
    CHECK(!MakeDest(big, "100_GR", small, sizeof(small)), "dest/refuses an over-long name");
}

// ===========================================================================
// The happy path, and the heartbeat that must not fire twice
// ===========================================================================
static void TestFollowOnce()
{
    Board b;
    CHECK(b.OnPeer(1, "Teleporter_1/100_GR", "100_GR", false) == V_FOLLOW,
          "follow/a matching world follows");
    CHECK(b.HasActed() && b.LastSeq() == 1, "follow/the attempt is remembered");
    b.OnFollowResult(true, 0);
    CHECK(b.Following(), "follow/success arms the echo guard");

    // The 2 s heartbeat repeats (seq, dest) exactly.
    CHECK(b.OnPeer(1, "Teleporter_1/100_GR", "100_GR", false) == V_DUPLICATE,
          "follow/a heartbeat does not follow twice");

    // A genuinely new transition bumps seq.
    CHECK(b.OnPeer(2, "Teleporter_1/100_GR", "100_GR", false) == V_FOLLOW,
          "follow/a new seq is a new transition, even to the same actor");

    // Empty dest is "not travelling", not a travel.
    CHECK(b.OnPeer(3, "", "100_GR", false) == V_NONE, "follow/an empty dest is ignored");
}

// ===========================================================================
// Do not fight the player
// ===========================================================================
static void TestBusyIsSkippedButNotLost()
{
    Board b;
    CHECK(b.OnPeer(1, "Checkpoint_7/100_GR", "100_GR", true) == V_BUSY,
          "busy/a busy local player is not driven");
    CHECK(b.OnPeer(1, "Checkpoint_7/100_GR", "100_GR", true) == V_NONE,
          "busy/the skip is logged once per destination, not per heartbeat");
    CHECK(!b.HasActed(), "busy/a skip is not remembered as an attempt");

    // The player finishes what they were doing.  The same heartbeat (same seq, dest)
    // must now be able to follow once -- this is also the late-joiner case.
    CHECK(b.OnPeer(1, "Checkpoint_7/100_GR", "100_GR", false) == V_FOLLOW,
          "busy/freed later, the same destination follows");
    b.OnFollowResult(true, 0);
}

// ===========================================================================
// Wrong world: the hard map switch.  Attempt once, then never again.
// ===========================================================================
static void TestWrongWorldIsExplicitAndBounded()
{
    Board b;
    CHECK(b.OnPeer(1, "Teleporter_1/900_SipehaWorld", "100_GR", false) == V_WRONG_WORLD,
          "world/a destination in another map is refused");
    CHECK(b.OnPeer(1, "Teleporter_1/900_SipehaWorld", "100_GR", false) == V_DUPLICATE,
          "world/the same heartbeat is not a second attempt");
    CHECK(b.OnPeer(2, "Teleporter_1/900_SipehaWorld", "100_GR", false) == V_GAVE_UP,
          "world/a new seq for the same bad destination does not retry it");

    // A different destination is still allowed: one bad name must not disable travel.
    CHECK(b.OnPeer(3, "Teleporter_2/100_GR", "100_GR", false) == V_FOLLOW,
          "world/a different destination still follows");
}

// ===========================================================================
// An unknown local world is transient, not a mismatch
// ===========================================================================
static void TestUnknownWorldIsTransient()
{
    Board b;
    // During a load the local map name is briefly empty.  That must not be recorded as
    // a wrong world, or the destination would be refused for the rest of the session.
    CHECK(b.OnPeer(1, "Teleporter_1/100_GR", "", false) == V_NONE,
          "world/an unknown local world is not a refusal");
    CHECK(!b.HasActed(), "world/an unknown local world is not remembered");
    CHECK(b.OnPeer(1, "Teleporter_1/100_GR", "100_GR", false) == V_FOLLOW,
          "world/once the world is known the same announcement follows");
}

// ===========================================================================
// Failure is explicit: a destination that does not resolve is never retried
// ===========================================================================
static void TestResolutionFailureStopsRetries()
{
    Board b;
    CHECK(b.OnPeer(1, "Teleporter_9/100_GR", "100_GR", false) == V_FOLLOW,
          "fail/the actor is attempted");
    b.OnFollowResult(false, 0);                    // resolution failed
    CHECK(b.OnPeer(2, "Teleporter_9/100_GR", "100_GR", false) == V_GAVE_UP,
          "fail/the failure is remembered against the destination");
    CHECK(b.OnPeer(3, "Teleporter_9/100_GR", "100_GR", false) == V_GAVE_UP,
          "fail/and stays remembered");

    CHECK(b.OnPeer(4, "Teleporter_10/100_GR", "100_GR", false) == V_FOLLOW,
          "fail/a different destination is unaffected");
}

// ===========================================================================
// The echo guard: the follower's own travel is not announced
// ===========================================================================
static void TestEchoSuppression()
{
    Board b;
    CHECK(!b.NoteLocal("", false, 0), "echo/nothing to announce at rest");
    CHECK(SameText(b.Announce(), ""), "echo/at rest the dest is empty");

    // The partner travels; we follow at t=1000.  The grace lasts until t=3000.
    CHECK(b.OnPeer(7, "Teleporter_3/100_GR", "100_GR", false) == V_FOLLOW,
          "echo/the partner's travel is followed");
    b.OnFollowResult(true, 1000);

    // Our own pawn now reports the same destination.  Announcing it would invite the
    // partner to follow back, so NoteLocal must keep the wire empty and not bump seq.
    CHECK(!b.NoteLocal("Teleporter_3/100_GR", true, 1500),
          "echo/the travel a follow caused is not announced");
    CHECK(SameText(b.Announce(), ""), "echo/the wire stays empty for the echo");
    CHECK(b.Following(), "echo/the latch is still armed while the travel runs");

    // The travel ends.  The latch survives the grace so a travel that sets the game's
    // state a frame late is still suppressed.
    CHECK(!b.NoteLocal("", false, 1600), "echo/ending the travel changes nothing on the wire");
    CHECK(b.Following(), "echo/the latch outlives the travel by the grace");
    CHECK(!b.NoteLocal("", false, 4000), "echo/the grace expiry is not a wire change");
    CHECK(!b.Following(), "echo/the grace expiry clears the follow latch");

    // A later, deliberate trip to the same actor is announced normally.
    CHECK(b.NoteLocal("Teleporter_3/100_GR", true, 5000),
          "echo/a later deliberate trip is announced");
    CHECK(SameText(b.Announce(), "Teleporter_3/100_GR"), "echo/and the wire carries it");
}

// ===========================================================================
// The local announce side: change detection drives the seq bump
// ===========================================================================
static void TestAnnounceChangeDetection()
{
    Board b;
    CHECK(b.NoteLocal("Teleporter_1/100_GR", true, 0), "announce/a new destination is a change");
    CHECK(!b.NoteLocal("Teleporter_1/100_GR", true, 100), "announce/the same destination is not");
    CHECK(b.NoteLocal("Teleporter_2/100_GR", true, 200), "announce/a different actor is a change");
    CHECK(b.NoteLocal("Teleporter_2/500_AUT", true, 300), "announce/a different world is a change");
    CHECK(b.NoteLocal("", false, 400), "announce/ending the travel clears the dest");
    CHECK(SameText(b.Announce(), ""), "announce/the wire is empty when not travelling");
}

// ===========================================================================
// The refusal set is bounded -- sixteen bad names must not lock travel off
// ===========================================================================
static void TestGaveUpRing()
{
    Board b;
    char d[kDestCap];
    uint32_t seq = 1;

    for (int i = 0; i < kMaxGaveUp; ++i) {
        char world[16];
        world[0] = 'W';
        if (i >= 10) { world[1] = (char)('0' + (i / 10)); world[2] = (char)('0' + (i % 10)); world[3] = 0; }
        else { world[1] = (char)('0' + i); world[2] = 0; }
        MakeDest("Teleporter_X", world, d, sizeof(d));
        CHECK(b.OnPeer(seq++, d, "100_GR", false) == V_WRONG_WORLD,
              "ring/a wrong-world destination is refused");
    }

    // The seventeenth distinct failure evicts the first.
    CHECK(b.OnPeer(seq++, "Teleporter_X/W16", "100_GR", false) == V_WRONG_WORLD,
          "ring/the ring admits a seventeenth destination");

    // The evicted one is no longer remembered, so it is evaluated again rather than
    // reported as a permanent give-up.  Either outcome is bounded; the point is that
    // the answer is not "gave up" for ever.
    Verdict v = b.OnPeer(seq++, "Teleporter_X/W0", "100_GR", false);
    CHECK(v == V_WRONG_WORLD || v == V_FOLLOW,
          "ring/the oldest refusal is evicted rather than kept for ever");
}

// ===========================================================================
// Malformed destinations are refused once, not once per heartbeat
// ===========================================================================
static void TestBadDestination()
{
    Board b;
    CHECK(b.OnPeer(1, "not a destination", "100_GR", false) == V_BAD_DEST,
          "bad/a dest with no world is refused");
    CHECK(b.OnPeer(1, "not a destination", "100_GR", false) == V_DUPLICATE,
          "bad/the heartbeat does not re-warn");
    CHECK(b.OnPeer(2, "A/B/C", "100_GR", false) == V_BAD_DEST,
          "bad/a dest with two slashes is refused");
}

int main()
{
    printf("=== qd travel tests ===\n");
    TestDestinationForm();
    TestFollowOnce();
    TestBusyIsSkippedButNotLost();
    TestUnknownWorldIsTransient();
    TestWrongWorldIsExplicitAndBounded();
    TestResolutionFailureStopsRetries();
    TestEchoSuppression();
    TestAnnounceChangeDetection();
    TestGaveUpRing();
    TestBadDestination();
    printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
