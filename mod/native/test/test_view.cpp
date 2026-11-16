// test_view.cpp -- the dialogue spectator camera and its cut policy (qdview.h).
//
// Why these tests exist, in the order the live failures show up:
//   * a camera placed at the NPC's ORIGIN (inside the mesh, at the feet) is the defect
//     this module was written for -- the placement must be the TALKER's eye, over the
//     shoulder, and the eye comes from the capsule CENTRE the PLAYER row carries;
//   * UE pitch is positive UP.  A sign slip aims at the floor and still "looks like a
//     camera", so every angle here is pinned to a number;
//   * the shoulder offset must be anchored to the TALKER -> NPC direction, not to the
//     talker's view yaw (the talker may be reading, not looking);
//   * the cut is held until the caller reports the level on screen, and the hold is
//     BOUNDED -- a camera that never cuts is a worse defect than one that cuts into a
//     hole, and the two must be distinguishable in the log;
//   * the temporary camera is destroyed only after the blend back, never while it is
//     still the outgoing view target.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "qdview.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, name)                                                          \
    do {                                                                           \
        if (cond) { ++g_pass; }                                                    \
        else { ++g_fail; printf("FAIL %s  (%s:%d)\n", name, __FILE__, __LINE__); } \
    } while (0)

#define NEAR(a, b, tol) (((a) > (b) ? (a) - (b) : (b) - (a)) <= (tol))

using namespace qd;
using namespace qd::view;

// A talker at the origin (capsule centre), facing +X, and an NPC head 150 cm ahead at
// 160 cm above the ground.  The capsule centre is 90 cm above the feet, so the eye is at
// 90 + 72 = 162 cm.
static void TestEyeAndShoulder()
{
    TalkerPose t = {};
    t.valid = true; t.x = 0; t.y = 0; t.z = 90.0f; t.yaw = 0; t.pitch = 0;
    AimPoint a = {};
    a.valid = true; a.measured = true; a.x = 150.0f; a.y = 0.0f; a.z = 160.0f;

    CamPose c = Compose(t, a, 90.0f);
    CHECK(c.valid, "eye/a valid aim produces a camera");
    // eye = (0,0,162); back 45 along +X; right 40 along +Y; up 12.
    CHECK(NEAR(c.x, -80.0f, 0.01f), "eye/the camera sits BACK from the talker's eye");
    CHECK(NEAR(c.y, 45.0f, 0.01f), "eye/the camera sits to the talker's RIGHT");
    CHECK(NEAR(c.z, 177.0f, 0.01f), "eye/the camera sits slightly ABOVE the eye (90+72+15)");
    // It looks at the head from the right shoulder: yaw a little left, and slightly DOWN
    // because the shoulder is 12 cm above the eye and the head is 2 cm below it.
    // atan2(0-45, 150+80) and atan2(160-177, sqrt(230^2+45^2)).
    CHECK(NEAR(c.yaw, -11.0686f, 0.05f), "eye/yaw points from the shoulder at the NPC");
    CHECK(NEAR(c.pitch, -4.1503f, 0.05f), "eye/pitch points at the head (UE: + is up)");
}

// The old defect, stated as a number: the camera must NOT be the NPC's own place.
static void TestNotAtTheNpcOrigin()
{
    TalkerPose t = {};
    t.valid = true; t.x = 0; t.y = 0; t.z = 90.0f;
    AimPoint a = {};
    a.valid = true; a.x = 150.0f; a.y = 0.0f; a.z = 160.0f;
    CamPose c = Compose(t, a, 90.0f);
    float dx = c.x - a.x, dy = c.y - a.y, dz = c.z - a.z;
    float d = dx * dx + dy * dy + dz * dz;
    CHECK(d > 100.0f * 100.0f, "defect/the camera is not within a metre of the NPC's origin");
    // And it is behind the talker's eye, i.e. the eye lies between the camera and the NPC.
    float ex = 0.0f, ey = 0.0f;
    float te_x = a.x - ex, te_y = a.y - ey;
    float tc_x = a.x - c.x, tc_y = a.y - c.y;
    CHECK(te_x * tc_x + te_y * tc_y > 0.0f,
          "defect/the NPC is in front of the camera along the talker's line of sight");
}

// The shoulder depends on where the NPC is, not on where the talker happens to look.
static void TestAnchoredToTheConversation()
{
    TalkerPose t = {};
    t.valid = true; t.x = 0; t.y = 0; t.z = 90.0f;
    t.yaw = 135.0f;                // the talker is looking away from the NPC
    AimPoint a = {};
    a.valid = true; a.x = 0.0f; a.y = 150.0f; a.z = 160.0f;
    CamPose c = Compose(t, a, 90.0f);
    // Direction is +Y: back 80 along +Y, right 45 along -X.
    CHECK(NEAR(c.x, -45.0f, 0.01f), "anchor/the shoulder follows the NPC, not the view yaw");
    CHECK(NEAR(c.y, -80.0f, 0.01f), "anchor/the camera stands back along the conversation");
    // atan2(150+80, 0+45).
    CHECK(NEAR(c.yaw, 78.9314f, 0.05f), "anchor/yaw turns toward an NPC at +Y");
}

// An NPC exactly on the talker, or a talker row that is not a place: fall back instead of
// dividing by zero / aiming at nothing.
static void TestDegenerate()
{
    TalkerPose t = {};
    t.valid = false; t.x = 0; t.y = 0; t.z = 90.0f;
    AimPoint a = {};
    a.valid = true; a.x = 500.0f; a.y = 100.0f; a.z = 160.0f;
    CamPose c = Compose(t, a, 90.0f);
    CHECK(c.valid, "fallback/no talker still produces a camera");
    CHECK(NEAR(c.x, 350.0f, 0.01f), "fallback/it stands in front of the NPC");
    CHECK(NEAR(c.y, 100.0f, 0.01f), "fallback/on the NPC's own line");
    CHECK(NEAR(c.yaw, 0.0f, 0.01f), "fallback/and faces it");

    // Aim point on top of the talker: no direction, so the NPC anchor is used.
    TalkerPose t2 = {};
    t2.valid = true; t2.x = 100.0f; t2.y = 100.0f; t2.z = 90.0f;
    AimPoint a2 = {};
    a2.valid = true; a2.x = 100.0f; a2.y = 100.0f; a2.z = 160.0f;
    CamPose c2 = Compose(t2, a2, 90.0f);
    CHECK(c2.valid && NEAR(c2.x, 100.0f - kFallbackDistanceCm, 0.01f),
          "degenerate/an aim on the talker falls back to the NPC anchor");

    AimPoint none = {};
    CHECK(!Compose(t2, none, 90.0f).valid, "degenerate/no aim point is not a camera");
    CHECK(!ComposeFacingNpc(none, 0.0f).valid, "degenerate/no aim point is not a camera (facing)");
}

// The NPC-facing fallback (the caller knows the NPC's yaw, not the talker's place).
static void TestFacingNpc()
{
    AimPoint a = {};
    a.valid = true; a.x = 0.0f; a.y = 0.0f; a.z = 160.0f;
    // NPC facing +Y: the camera stands in front of it (also +Y), right shoulder -X.
    CamPose c = ComposeFacingNpc(a, 90.0f);
    CHECK(c.valid, "facing/a camera in front of the NPC");
    CHECK(NEAR(c.x, -45.0f, 0.01f), "facing/the side offset is the NPC's right");
    CHECK(NEAR(c.y, 150.0f, 0.01f), "facing/the camera is in front along the NPC's yaw");
    CHECK(NEAR(c.yaw, -73.3008f, 0.05f), "facing/and it looks back at the head");
}

// A re-place is only for a genuine move: the camera must not chase a 1 cm jitter every
// frame and fight the blend.
static void TestMoved()
{
    CamPose a = {};
    a.valid = true; a.x = 0; a.y = 0; a.z = 0; a.pitch = 0; a.yaw = 10.0f;
    CamPose b = a;
    CHECK(!Moved(a, b), "moved/an identical pose is not a move");
    b.x = 10.0f;
    CHECK(!Moved(a, b), "moved/10 cm is inside the tolerance");
    b.x = 40.0f;
    CHECK(Moved(a, b), "moved/40 cm is a move");
    b = a; b.yaw = 14.0f;
    CHECK(Moved(a, b), "moved/a 4 deg turn is a move");
    b = a; b.yaw = 12.0f;
    CHECK(!Moved(a, b), "moved/a 2 deg turn is noise");
    // Yaw wraps: 179 -> -179 is 2 degrees apart, not 358.
    b = a; b.yaw = -179.0f; a.yaw = 179.0f;
    CHECK(!Moved(a, b), "moved/yaw wrap is measured the short way");
    CamPose invalid = {};
    CHECK(!Moved(a, invalid), "moved/an unplaced pose never asks for a move");
    CHECK(Moved(invalid, a), "moved/the first placement always happens");
}

// The anchor bound: a PLAYER row is a network fact and can name the moment before a
// teleport.  Measured live: 300 ms after a rig teleport the camera was anchored 3.4 km from
// the conversation and the shot was of the canopy, so a row that far from the NPC is refused.
static void TestAnchorPlausible()
{
    TalkerPose t = {};
    t.valid = true; t.x = 200.0f; t.y = 30.0f; t.z = 90.0f;   // 2 m from the NPC
    AimPoint a = {};
    a.valid = true; a.x = 0.0f; a.y = 0.0f; a.z = 160.0f;
    CHECK(AnchorPlausible(t, a), "anchor/2 m away IS the conversation");
    t.x = 1000.0f;
    CHECK(AnchorPlausible(t, a), "anchor/10 m is still inside the bound");
    t.x = 1500.0f;
    CHECK(!AnchorPlausible(t, a), "anchor/15 m is a stale row, not this conversation");
    t.x = 200.0f; t.valid = false;
    CHECK(!AnchorPlausible(t, a), "anchor/no talker row is not plausible");
    t.valid = true;
    AimPoint none = {};
    CHECK(!AnchorPlausible(t, none), "anchor/no aim point is not plausible");
}

// The cut policy.  The numbers matter: 4 s is the bound, 700 ms is the blend hold.
static void TestCutPolicy()
{
    CamState s;
    CHECK(!s.Engaged(), "cut/idle is not engaged");
    s.Begin(1000);
    CHECK(s.Waiting() && s.Engaged(), "cut/a begun camera is waiting");
    CHECK(s.Poll(false, 1000) == ACT_NONE, "cut/nothing while the level is missing");
    CHECK(s.Poll(false, 1200) == ACT_NONE, "cut/still nothing early");
    CHECK(s.WaitedMs() == 200, "cut/the wait is measured for the log");
    // The state does not advance on a poll it did not act on.
    CHECK(s.Waiting(), "cut/a refused poll leaves it waiting");
    CHECK(s.Poll(true, 1500) == ACT_CUT, "cut/ready cuts at once");
    CHECK(s.Waiting(), "cut/but the state only changes when the blend ran");
    s.Cut(1500);
    CHECK(s.CutDone() && !s.Waiting(), "cut/the cut is recorded");
    CHECK(s.Poll(true, 1600) == ACT_NONE, "cut/a live camera is not cut twice");

    // The bound: 4 s after Begin, a missing level still cuts (and says so).
    CamState b;
    b.Begin(1000);
    CHECK(b.Poll(false, 4999) == ACT_NONE, "cut/one ms before the bound still waits");
    CHECK(b.Poll(false, 5000) == ACT_CUT_ANYWAY, "cut/the bound expires into a cut anyway");
}

// The restore: the camera outlives the blend back by kRestoreHoldMs, and a camera that was
// never the view target is destroyed immediately (BeginRestore says it owes nothing).
static void TestRestore()
{
    CamState s;
    s.Begin(1000);
    CHECK(!s.BeginRestore(1100), "restore/a camera that never cut owes no restore");
    CHECK(!s.Engaged(), "restore/and it is no longer engaged");

    s.Begin(2000);
    CHECK(s.Poll(true, 2000) == ACT_CUT, "restore/cut");
    s.Cut(2000);
    CHECK(s.BeginRestore(2500), "restore/a cut camera owes the pawn its view back");
    CHECK(s.Restoring(), "restore/it is restoring");
    CHECK(s.PollRestore(2500 + kRestoreHoldMs - 1) == ACT_NONE,
          "restore/the camera is kept through the blend");
    CHECK(s.PollRestore(2500 + kRestoreHoldMs) == ACT_DESTROY,
          "restore/then it is destroyed");
    s.Destroyed();
    CHECK(!s.Restoring() && !s.Engaged(), "restore/the state is idle again");
    CHECK(s.PollRestore(9999) == ACT_NONE, "restore/nothing more to do");
}

// THE HEIGHT IS A GUESS UNLESS IT WAS MEASURED.  Measured live (run 3): the NPC's origin
// was its capsule CENTRE, the default said "feet" (+160), and the camera aimed 84 cm over
// its head.  A guessed height must not steer the angle: the camera looks at the TALKER'S own
// eye height instead, which is where a standing partner's head is.
static void TestEyeToEye()
{
    TalkerPose t = {};
    t.valid = true; t.x = 0; t.y = 0; t.z = 90.0f;
    AimPoint a = {};
    a.valid = true; a.measured = false; a.x = 150.0f; a.y = 0.0f; a.z = 500.0f;  // absurd guess
    CamPose c = Compose(t, a, 90.0f);
    // camera z = 90 + 72 + 15 = 177; eye = 162; atan2(162-177, sqrt(230^2+45^2)).
    CHECK(NEAR(c.pitch, -3.6645f, 0.05f), "guess/an unmeasured height is ignored (eye to eye)");
    AimPoint b = a;
    b.z = 0.0f;                       // a different absurd guess
    CamPose c2 = Compose(t, b, 90.0f);
    CHECK(NEAR(c.pitch, c.pitch, 0.001f), "guess/the guessed height makes no difference");
    CHECK(NEAR(c2.y, c.y, 0.001f), "guess/and the shoulder is unchanged");
    // A MEASURED height is honoured, high or low.
    AimPoint m = a;
    m.measured = true;
    CamPose c3 = Compose(t, m, 90.0f);
    CHECK(c3.pitch > 40.0f, "guess/a measured height IS aimed at");
}

// Angles: one quadrant test each, because a swapped atan2 argument is silent.
static void TestAngles()
{
    CHECK(NEAR(YawDegTo(1.0f, 0.0f), 0.0f, 0.001f), "angles/+X is yaw 0");
    CHECK(NEAR(YawDegTo(0.0f, 1.0f), 90.0f, 0.001f), "angles/+Y is yaw +90");
    CHECK(NEAR(YawDegTo(-1.0f, 0.0f), 180.0f, 0.001f), "angles/-X is yaw 180");
    CHECK(NEAR(YawDegTo(0.0f, -1.0f), -90.0f, 0.001f), "angles/-Y is yaw -90");
    CHECK(NEAR(PitchDegTo(1.0f, 1.0f), 45.0f, 0.001f), "angles/up is a positive pitch");
    CHECK(NEAR(PitchDegTo(-1.0f, 1.0f), -45.0f, 0.001f), "angles/down is a negative pitch");
    CHECK(NEAR(PitchDegTo(1.0f, 0.0f), 89.0f, 0.001f), "angles/straight up is clamped");
    CHECK(NEAR(AngleDeltaDeg(179.0f, -179.0f), 2.0f, 0.001f), "angles/the short way round");
}

int main()
{
    TestEyeAndShoulder();
    TestNotAtTheNpcOrigin();
    TestAnchoredToTheConversation();
    TestDegenerate();
    TestFacingNpc();
    TestMoved();
    TestAnchorPlausible();
    TestCutPolicy();
    TestRestore();
    TestEyeToEye();
    TestAngles();

    printf("\ntest_view: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
