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
#include <math.h>

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

// ===========================================================================
// Hard map switches: both arrive, then resume (HardBoard)
// ===========================================================================
static void TestHardLocalBegin()
{
    HardBoard h;
    // The first observation is a baseline, not a transition.
    CHECK(!h.OnLocalMap("100_GR", 1000), "hard/the first map is a baseline");
    // A hard switch starts a transition and asks for HARD_BEGIN.
    CHECK(h.OnLocalMap("900_SipehaWorld", 2000), "hard/a map change is a transition");
    CHECK(h.Active(), "hard/it is active");
    CHECK(h.LocalArrived(), "hard/we are already in the new world");
    CHECK(!h.PeerArrived(), "hard/the peer has not arrived");
    CHECK(h.TakeAnnounce() == 1, "hard/the announce is HARD_BEGIN");
    CHECK(h.TakeAnnounce() == 0, "hard/and it is consumed once");
    CHECK(!h.ReadyToResume(), "hard/not resumable until the peer arrives");
    CHECK(h.OnPeerArrived(1, "900_SipehaWorld", "900_SipehaWorld") == HV_NONE,
          "hard/the peer's arrival is accepted");
    CHECK(h.ReadyToResume(), "hard/both arrived: the host may resume");
    CHECK(h.OnPeerResume(2, "900_SipehaWorld") == HV_RESUME, "hard/resume releases the pair");
    CHECK(!h.Active(), "hard/the transition is over");

    // The host must stop queuing RESUME after it has sent one, or it would repeat for
    // the rest of the session.
    HardBoard host;
    host.OnLocalMap("100_GR", 1000);
    host.OnLocalMap("900_SipehaWorld", 2000);
    host.TakeAnnounce();
    host.OnPeerArrived(1, "900_SipehaWorld", "900_SipehaWorld");
    CHECK(host.ReadyToResume(), "hard/the host is ready to release");
    host.MarkResumed();
    CHECK(!host.ReadyToResume(), "hard/after sending RESUME it does not queue another");
    CHECK(!host.Active(), "hard/and the transition is over");
}

static void TestHardFollowSuppressesEcho()
{
    HardBoard h;
    h.OnLocalMap("100_GR", 1000);
    // The peer begins a hard switch to another map: this machine loads it.
    CHECK(h.OnPeerBegin(1, "900_SipehaWorld", "100_GR", false) == HV_LOAD,
          "hard/a peer's begin asks for a load");
    h.Arm(2000);
    CHECK(h.Active() && h.PeerArrived() && !h.LocalArrived(),
          "hard/the peer is there, we are not yet");
    // The load finishes: the local map changes to the target.  That is an arrival, not
    // a new transition, so it must NOT announce a begin back.
    CHECK(!h.OnLocalMap("900_SipehaWorld", 3000), "hard/the followed load is not a new transition");
    CHECK(h.LocalArrived(), "hard/and it is an arrival");
    CHECK(h.ReadyToResume(), "hard/both arrived");
    // The arrival must be announced or the sender never learns the follower is there.
    CHECK(h.TakeAnnounce() == 2, "hard/the followed load announces ARRIVED");
}

static void TestHardAlreadyThere()
{
    HardBoard h;
    h.OnLocalMap("900_SipehaWorld", 1000);
    CHECK(h.OnPeerBegin(1, "900_SipehaWorld", "900_SipehaWorld", false) == HV_ANNOUNCE_ARRIVE,
          "hard/a begin for the map we are in reports arrival");
    CHECK(!h.Active(), "hard/and does not start a load");
    CHECK(h.TakeAnnounce() == 2, "hard/the announce is ARRIVED");
}

static void TestHardRefusalsAndDeadline()
{
    HardBoard h;
    h.OnLocalMap("100_GR", 1000);
    CHECK(h.OnPeerBegin(1, "", "100_GR", false) == HV_REFUSED, "hard/an empty map is refused");
    CHECK(h.OnPeerBegin(2, "/Game/Maps/X", "100_GR", false) == HV_REFUSED,
          "hard/a path is not a map name");
    CHECK(h.OnPeerBegin(3, "900_SipehaWorld", "100_GR", true) == HV_NONE,
          "hard/a busy player is skipped, not refused");
    CHECK(!h.GaveUp("900_SipehaWorld"), "hard/a busy skip is not remembered");

    CHECK(h.OnPeerBegin(4, "900_SipehaWorld", "100_GR", false) == HV_LOAD,
          "hard/when free the same map loads");
    h.Arm(2000);
    h.OnLoadResult(false, 3000);
    CHECK(h.GaveUp("900_SipehaWorld"), "hard/a failed load is remembered");
    CHECK(h.OnPeerBegin(5, "900_SipehaWorld", "100_GR", false) == HV_REFUSED,
          "hard/and is not retried");

    // A different map is still allowed.
    CHECK(h.OnPeerBegin(6, "990_B33sment", "100_GR", false) == HV_LOAD,
          "hard/a different map is unaffected");
    h.Arm(4000);
    CHECK(!h.Tick(4000 + kHardDeadlineMs - 1), "deadline/not yet");
    CHECK(h.Tick(4000 + kHardDeadlineMs), "deadline/an abandoned load is reported");
    CHECK(h.GaveUp("990_B33sment"), "deadline/and remembered");
}

static void TestHardResumeGuard()
{
    HardBoard h;
    h.OnLocalMap("100_GR", 1000);
    CHECK(h.OnPeerResume(1, "900_SipehaWorld") == HV_IGNORED,
          "resume/a resume for a map we are not loading is ignored");
    h.OnPeerBegin(2, "900_SipehaWorld", "100_GR", false);
    h.Arm(2000);
    CHECK(h.OnPeerResume(3, "900_SipehaWorld") == HV_REFUSED,
          "resume/a resume before we arrived is refused");
}

static void TestHardReturnHome()
{
    // A map this machine is IN is reachable, so entering it clears a refusal.  Without
    // this the boot race (both machines announcing the start transition) puts the home
    // map in the gave-up ring and the deliberate journey home is refused for the rest of
    // the session (measured live, Session 7).
    HardBoard h;
    h.OnLocalMap("990_B33sment", 1000);
    CHECK(h.OnPeerBegin(1, "Ruins_Level_00", "990_B33sment", false) == HV_LOAD,
          "return/the home map is loadable");
    h.Arm(2000);
    h.OnLoadResult(false, 3000);
    CHECK(h.GaveUp("Ruins_Level_00"), "return/a failed home load is remembered");
    CHECK(h.OnPeerBegin(2, "Ruins_Level_00", "990_B33sment", false) == HV_REFUSED,
          "return/and is not retried");
    // The game itself puts this machine in the map (the boot path).
    h.OnLocalMap("Ruins_Level_00", 4000);
    CHECK(!h.GaveUp("Ruins_Level_00"), "return/being in the map clears its gave-up entry");
    CHECK(h.OnPeerBegin(3, "Ruins_Level_00", "Ruins_Level_00", false) == HV_ANNOUNCE_ARRIVE,
          "return/the next begin is an arrival, not a refusal");

    // The boot race arms a transition to the map we are already in; its deadline must not
    // poison that map.
    HardBoard b;
    b.OnLocalMap("title_screen", 1000);
    CHECK(b.OnLocalMap("Ruins_Level_00", 2000), "deadline/the boot transition is observed");
    CHECK(b.Active(), "deadline/it is armed");
    CHECK(b.Tick(2000 + kHardDeadlineMs), "deadline/and reports its deadline");
    CHECK(!b.GaveUp("Ruins_Level_00"), "deadline/a transition to the map we are IN is not a failed load");
}

// ===========================================================================
// Session 12 -- the world you are in, and where the player stands after a load
// ===========================================================================
static void TestRefuseCurrentWorld()
{
    // Session 11 R92: `LoadMapWithLoadingScreen` of the current world faults the GAME
    // (no WINMM frame in the stack).  The refusal is the guard every caller goes through.
    CHECK(RefuseCurrentWorld("900_SipehaWorld", "900_SipehaWorld"),
          "reload/same map is refused");
    CHECK(!RefuseCurrentWorld("900_SipehaWorld", "Ruins_Level_00"),
          "reload/a different map is allowed");
    CHECK(!RefuseCurrentWorld("", "900_SipehaWorld"), "reload/an empty target is not a load");
    CHECK(!RefuseCurrentWorld("900_SipehaWorld", ""),
          "reload/an unknown local world cannot be compared");
    CHECK(!RefuseCurrentWorld(nullptr, "900_SipehaWorld"), "reload/null target is not a load");
    // The comparison is exact: a prefix is a DIFFERENT world.
    CHECK(!RefuseCurrentWorld("900_Sipeha", "900_SipehaWorld"), "reload/no prefix match");
}

static ArrivalInput ArrivalAt(float x, float y)
{
    ArrivalInput in;
    memset(&in, 0, sizeof(in));
    in.havePawn = true;
    in.px = x; in.py = y;
    in.localMap = "900_SipehaWorld";
    return in;
}

static void TestArrivalNotArmed()
{
    ArrivalBoard b;
    ArrivalInput in = ArrivalAt(0.0f, 0.0f);
    CHECK(b.Decide(1000, in) == AR_NONE, "arrive/not armed does nothing");
    CHECK(!b.Armed(), "arrive/and stays off");
}

static void TestArrivalWaitsForAPawn()
{
    ArrivalBoard b;
    b.Arm("900_SipehaWorld", 1000);
    ArrivalInput in = ArrivalAt(0.0f, 0.0f);
    in.havePawn = false;
    CHECK(b.Decide(1500, in) == AR_WAIT, "arrive/no pawn yet waits");

    // A position that did not read must not be read as "the engine placed the player".
    ArrivalInput nan = ArrivalAt(0.0f, 0.0f);
    nan.px = nan.py = nanf("");
    CHECK(b.Decide(2000, nan) == AR_WAIT, "arrive/a failed read waits");
    CHECK(b.Armed(), "arrive/and does not disarm");
}

static void TestArrivalLeavesARealSpawnAlone()
{
    // A world WITH a PlayerStart (or a restored saved position) puts the pawn far from the
    // origin and it STANDS there; moving it would be the mod pretending to be the spawn
    // path.  One poll is not evidence: a world swap leaves the previous world's player
    // readable for a frame or two (measured Session 12), so the position has to hold.
    ArrivalBoard b;
    b.Arm("Ruins_Level_00", 1000);
    ArrivalInput in = ArrivalAt(4106.0f, -1744.0f);
    in.localMap = "Ruins_Level_00";
    in.pz = 140.0f;
    CHECK(b.Decide(2000, in) == AR_WAIT, "arrive/a far pawn on the first poll is not evidence");
    CHECK(b.Armed(), "arrive/and the board keeps waiting");
    in.pz = 139.0f;                                   // standing: no fall
    CHECK(b.Decide(2000 + kPlacedSettleMs - 1, in) == AR_WAIT, "arrive/not yet settled");
    CHECK(b.Decide(2000 + kPlacedSettleMs, in) == AR_DONE, "arrive/a standing player ends it");
    CHECK(!b.Armed(), "arrive/and disarms");
    CHECK(b.Decide(9000, in) == AR_NONE, "arrive/no follow-up placement");
}

static void TestArrivalPlacesAPawnThatIsFalling()
{
    // The same far coordinates in a world with no PlayerStart are the VOID: the player keeps
    // the transform it had and drops out of it.  A drop between polls is a fall, and a
    // falling player has not been placed however far from the origin it is.
    ArrivalBoard b;
    b.Arm("900_SipehaWorld", 1000);
    ArrivalInput in = ArrivalAt(4106.0f, -1744.0f);
    in.pz = 140.0f;
    in.haveAnchor = true; in.anchorX = -3431.0f; in.anchorY = -5705.0f; in.anchorWhy = "marker";
    CHECK(b.Decide(2000, in) == AR_WAIT, "arrive/the first far poll only records the height");
    in.pz = -400.0f;                                  // fell 540 cm in one poll
    CHECK(b.Decide(3000, in) == AR_PLACE_ANCHOR, "arrive/a falling pawn IS placed");
    CHECK(b.TargetX() == -3431.0f, "arrive/on the world's own marker");

    // The identity spawn is placed the same way, and the settle clock restarts after a
    // placement so a respawn gets corrected.
    b.Applied(true, 3000);
    ArrivalInput origin = ArrivalAt(0.0f, 0.0f);
    origin.pz = 90.0f;
    origin.haveAnchor = true; origin.anchorX = -3431.0f; origin.anchorY = -5705.0f;
    origin.anchorWhy = "marker";
    CHECK(b.Decide(4000, origin) == AR_PLACE_ANCHOR, "arrive/a respawn to the origin is placed again");
}

static void TestArrivalAtOriginWaitsForAnAnchor()
{
    ArrivalBoard b;
    b.Arm("900_SipehaWorld", 1000);
    ArrivalInput in = ArrivalAt(0.0f, 0.0f);
    CHECK(b.Decide(2000, in) == AR_WAIT, "arrive/origin with nothing to aim at waits");
}

static void TestArrivalUsesThePartnerWhenItIsReal()
{
    ArrivalBoard b;
    b.Arm("900_SipehaWorld", 1000);
    ArrivalInput in = ArrivalAt(0.0f, 0.0f);
    in.havePeer = true;
    in.peerMap = "900_SipehaWorld";
    in.peerAgeMs = 200;
    in.peerRowMs = 1000 + kPeerSettleMs + 500;   // settled: well after this machine's arrival
    in.peerX = -7139.0f; in.peerY = -658.0f;
    in.haveAnchor = true; in.anchorX = 100.0f; in.anchorY = 100.0f; in.anchorWhy = "marker";
    CHECK(b.Decide(6000, in) == AR_PLACE_PEER, "arrive/a standing partner wins over a marker");
    CHECK(b.TargetX() == -7139.0f && b.TargetY() == -658.0f, "arrive/target is the partner");
    CHECK(SameText(b.Why(), "partner"), "arrive/and the log says why");
    b.Applied(true, 6000);
    CHECK(b.Tries() == 1, "arrive/the attempt is counted");
}

static void TestArrivalIgnoresAPartnerThatSaysNothing()
{
    // Four ways the partner's row is not an answer: it arrived before the load had
    // settled (the previous world -- measured Session 12, it put the follower in the
    // void), it is old, it is in another world, or it is itself the identity spawn.
    ArrivalBoard early;
    early.Arm("900_SipehaWorld", 1000);
    ArrivalInput in = ArrivalAt(0.0f, 0.0f);
    in.havePeer = true; in.peerMap = "900_SipehaWorld"; in.peerAgeMs = 100;
    in.peerRowMs = 1000 + kPeerSettleMs - 1;   // inside the settle window
    in.peerX = 500.0f; in.peerY = 500.0f;
    in.haveAnchor = true; in.anchorX = 10.0f; in.anchorY = 20.0f; in.anchorWhy = "marker";
    CHECK(early.Decide(6000, in) == AR_PLACE_ANCHOR,
          "arrive/a row from inside the settle window is not used");
    CHECK(early.TargetX() == 10.0f && early.TargetY() == 20.0f, "arrive/the marker is");

    ArrivalBoard stale;
    stale.Arm("900_SipehaWorld", 1000);
    in.peerRowMs = 1000 + kPeerSettleMs + 500;
    in.peerAgeMs = kPeerFreshMs + 1;
    CHECK(stale.Decide(6000, in) == AR_PLACE_ANCHOR, "arrive/a stale row is not used");

    ArrivalBoard other = stale;
    in.peerAgeMs = 100; in.peerMap = "Ruins_Level_00";
    CHECK(other.Decide(6000, in) == AR_PLACE_ANCHOR, "arrive/another world's row is not used");

    ArrivalBoard zero;
    zero.Arm("900_SipehaWorld", 1000);
    in.peerMap = "900_SipehaWorld"; in.peerX = 3.0f; in.peerY = -4.0f;
    CHECK(zero.Decide(6000, in) == AR_PLACE_ANCHOR,
          "arrive/a partner that is also at the origin is not an anchor");
}

static void TestArrivalUsesTheWorldsOwnMarker()
{
    // Both machines stranded: the destination world's marker is the same answer on both,
    // which is what puts them next to each other.
    ArrivalBoard b;
    b.Arm("990_B33sment", 1000);
    ArrivalInput in = ArrivalAt(0.0f, 0.0f);
    in.localMap = "990_B33sment";
    in.haveAnchor = true; in.anchorX = 12.0f; in.anchorY = -34.0f;
    in.anchorWhy = "B33smentTeleportPoint_C";
    CHECK(b.Decide(2000, in) == AR_PLACE_ANCHOR, "arrive/the world's marker is used");
    CHECK(SameText(b.Why(), "B33smentTeleportPoint_C"), "arrive/and the class is named");
}

static void TestArrivalIsBounded()
{
    // The game respawns the stranded pawn every few seconds, so a placement has to be
    // repeatable -- but not unbounded: a wrong anchor must cost seconds, not the session.
    ArrivalBoard b;
    b.Arm("900_SipehaWorld", 1000);
    ArrivalInput in = ArrivalAt(0.0f, 0.0f);
    in.haveAnchor = true; in.anchorX = 10.0f; in.anchorY = 10.0f; in.anchorWhy = "marker";
    for (int i = 0; i < kArrivalMaxTries; ++i) {
        CHECK(b.Decide(2000 + i * 1000, in) == AR_PLACE_ANCHOR, "arrive/each attempt decides");
        b.Applied(true, 2000 + i * 1000);
    }
    CHECK(b.Decide(9000, in) == AR_DONE, "arrive/then it stops");
    CHECK(!b.Armed(), "arrive/and disarms");

    // The window ends it too (a load that never produced a pawn, a world we left).
    ArrivalBoard w;
    w.Arm("900_SipehaWorld", 1000);
    ArrivalInput none = ArrivalAt(0.0f, 0.0f);
    none.havePawn = false;
    CHECK(w.Decide(1000 + kArrivalWindowMs - 1, none) == AR_WAIT, "arrive/not yet at the deadline");
    CHECK(w.Decide(1000 + kArrivalWindowMs, none) == AR_DONE, "arrive/the deadline ends it");
    CHECK(!w.Armed(), "arrive/and disarms");
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
    TestHardLocalBegin();
    TestHardFollowSuppressesEcho();
    TestHardAlreadyThere();
    TestHardRefusalsAndDeadline();
    TestHardResumeGuard();
    TestHardReturnHome();
    TestRefuseCurrentWorld();
    TestArrivalNotArmed();
    TestArrivalWaitsForAPawn();
    TestArrivalLeavesARealSpawnAlone();
    TestArrivalPlacesAPawnThatIsFalling();
    TestArrivalAtOriginWaitsForAnAnchor();
    TestArrivalUsesThePartnerWhenItIsReal();
    TestArrivalIgnoresAPartnerThatSaysNothing();
    TestArrivalUsesTheWorldsOwnMarker();
    TestArrivalIsBounded();
    printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
