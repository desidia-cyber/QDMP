// test_platforms.cpp -- moving platforms and the person standing on one (qdplatforms.h).
//
// The properties that matter, each one measured as a failure mode elsewhere:
//   * the CLASS FILTER matches this game's lifts and refuses the things that are not movers
//     (measured: the old filter tracked a LIGHT and missed every `*_Lift_*`);
//   * the FOLLOWER'S EASING turns a 10 Hz row stream into a smooth transform and still
//     takes a real jump at once (the old code snapped, which is a staircase);
//   * the RECONSTRUCTION puts the rider on the platform from THIS machine's copy of it;
//   * the RIDE machine ignores the mid-ride measurement noise (measured by dollsMP as
//     50-900 cm per frame), believes one measurement on boarding, leans onto the exact one
//     at rest, survives a flickering capture for 300 ms, and releases when the player
//     really leaves.
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "qdplatforms.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, name)                                                          \
    do {                                                                           \
        if (cond) { ++g_pass; }                                                    \
        else { ++g_fail; printf("FAIL %s  (%s:%d)\n", name, __FILE__, __LINE__); } \
    } while (0)

#define CHECK_NEAR(a, b, eps, name)                                                       \
    do {                                                                                  \
        float _a = (float)(a), _b = (float)(b);                                            \
        if (fabsf(_a - _b) <= (float)(eps)) { ++g_pass; }                                  \
        else { ++g_fail; printf("FAIL %s: %g vs %g  (%s:%d)\n", name, _a, _b, __FILE__, __LINE__); } \
    } while (0)

using namespace qd;
using namespace qd::platforms;

// ---------------------------------------------------------------------------
// 1. the classifier
// ---------------------------------------------------------------------------
static void TestClassifier()
{
    // Every lift family the analysis census names must be a mover.  These are the exact
    // strings that appeared in `analysis/22-close-out.md` §3 with a placement count.
    static const char* const kMustMatch[] = {
        "Mausoleum_Lift_00_BP_C",
        "OW_Lift_00_BP_C",
        "OW_TimedLift_00_BP_C",
        "OW_BrokenLift_00_BP_C",
        "ROS_Lift_BP_C",
        "IntroDungerLift_BP_C",
        "OfficeLift_00_BP_C",
        "Castle_Lift_00_BP_C",
        "SZ_Lift_00_BP_C",
        "SZ_Lift_01_BP1_C",
        "PT_Lift_00_BP_C",
        "Lab_Lift_BP_C",
        "AQR_NoSoftLockLift_C",
        "AquaElevator_00_BP_C",
        "Lib_ElevatorSmall_00BP_C",
        "LabUG_Tram_BP_C",
        "Library_RotateBridge_C",
        "Library_LowerBridge_C",
        "Elevator_Base",
        "MovingActor",
    };
    for (int i = 0; i < (int)(sizeof(kMustMatch) / sizeof(kMustMatch[0])); ++i)
        CHECK(IsMoverClass(kMustMatch[i]), kMustMatch[i]);

    // And the false positives and the controls must be refused.  The first entry is the
    // measured one: the old filter matched "Activatable" and tracked a light.
    static const char* const kMustNotMatch[] = {
        "FS_Light_Activatable372",
        "ActivatableGroup_C",
        "OW_Lift_Lever_00_BP",
        "LabElevatorSwitch",
        "LiftLever",
        "WoodElevator_Lever",
        "AQ_SCStairs_BP_C",           // a stair family: not established as a mover
        "SZ_Lift_00_Kill_SM",
        "Teleporter_PlatformAreaSphere",
        "ElevatorDataAsset",
        "ElevatorManager_C",
        "LiftSpawner_C",
        "",
    };
    for (int i = 0; i < (int)(sizeof(kMustNotMatch) / sizeof(kMustNotMatch[0])); ++i)
        CHECK(!IsMoverClass(kMustNotMatch[i]), kMustNotMatch[i]);

    CHECK(!IsMoverClass(nullptr), "classifier/null");
    // The lists are non-trivial (a filter that matched nothing would pass every refusal).
    int ny = 0, nn = 0;
    MoverTokens(&ny);
    NotMoverTokens(&nn);
    CHECK(ny >= 5 && nn >= 5, "classifier/the token lists are real");
}

// ---------------------------------------------------------------------------
// 2. the follower's easing
// ---------------------------------------------------------------------------
static void TestEasing()
{
    // A 10 Hz row stream is a staircase: the target jumps every 100 ms.  Eased at 60 fps it
    // must not: the largest single-frame step has to shrink to a fraction of the row step.
    const float row_step_cm = 30.0f;      // 3 m/s, 10 Hz
    float cur = 0.0f, tgt = 0.0f;
    float max_step = 0.0f;
    float max_lag = 0.0f;
    float alpha = EaseTimeAlpha(16.0f, kEaseTauMs);
    for (int frame = 0; frame < 120; ++frame) {
        if (frame % 6 == 0) tgt += row_step_cm;      // a new row every ~100 ms
        float prev = cur;
        cur = EaseAxis(cur, tgt, alpha, kEaseSnapCm);
        float step = fabsf(cur - prev);
        if (step > max_step) max_step = step;
        float lag = fabsf(cur - tgt);
        if (lag > max_lag) max_lag = lag;
    }
    CHECK(max_step < row_step_cm * 0.5f, "ease/the staircase is smoothed out");
    // A first-order filter tracking a stepped ramp lags by at most about one row step plus
    // its residual: 30 cm + 8 cm here.  That is the price of not snapping, and it is less
    // than the 100 ms of interpolation delay the doll is drawn behind anyway.
    CHECK(max_lag < 45.0f, "ease/the ramp lag is bounded by about one row step");
    for (int frame = 0; frame < 60; ++frame) cur = EaseAxis(cur, tgt, alpha, kEaseSnapCm);
    CHECK_NEAR(cur, tgt, 0.5f, "ease/it converges on the owner's transform");

    // A real discontinuity is taken at once: easing through a 5000 cm gap draws a slide
    // through geometry.
    CHECK_NEAR(EaseAxis(0.0f, 5000.0f, 0.1f, kEaseSnapCm), 5000.0f, 0.01f, "ease/snap");
    // And a zero-length frame does not move anything.
    CHECK_NEAR(EaseAxis(10.0f, 100.0f, EaseTimeAlpha(0.0f, kEaseTauMs), kEaseSnapCm),
               10.0f, 0.001f, "ease/dt=0");
    // A long stall must not overshoot past the target.
    CHECK_NEAR(EaseAxis(10.0f, 20.0f, EaseTimeAlpha(1.0e6f, kEaseTauMs), kEaseSnapCm),
               20.0f, 0.01f, "ease/long frame converges");
}

// ---------------------------------------------------------------------------
// 3. the reconstruction (the receiver half)
// ---------------------------------------------------------------------------
static void TestReconstruct()
{
    // On a platform facing +X, a place of (100, 0, 50) is 100 cm ahead and 50 cm up.
    float out[3] = {0, 0, 0};
    float off[3] = {100.0f, 0.0f, 50.0f};
    CHECK(Reconstruct(1000.0f, 2000.0f, 300.0f, 0.0f, off, out), "reconstruct/ok");
    CHECK_NEAR(out[0], 1100.0f, 0.01f, "reconstruct/x");
    CHECK_NEAR(out[1], 2000.0f, 0.01f, "reconstruct/y");
    CHECK_NEAR(out[2], 350.0f, 0.01f, "reconstruct/z");

    // Turn the platform 90 deg: the same place must now be 100 cm along +Y.
    CHECK(Reconstruct(1000.0f, 2000.0f, 300.0f, 90.0f, off, out), "reconstruct/rotated");
    CHECK_NEAR(out[0], 1000.0f, 0.01f, "reconstruct/turned x");
    CHECK_NEAR(out[1], 2100.0f, 0.01f, "reconstruct/turned y");

    // THE POINT OF THE LANE: the two machines' copies are metres apart, and the rider still
    // lands on the RECEIVER's copy, not on the sender's.
    float sender[3], receiver[3];
    Reconstruct(0.0f, 0.0f, 0.0f, 0.0f, off, sender);
    Reconstruct(600.0f, 0.0f, 0.0f, 0.0f, off, receiver);
    CHECK_NEAR(receiver[0] - sender[0], 600.0f, 0.01f,
               "reconstruct/the rider follows the local copy, not the sender's");

    // A NaN place is refused (the session-24 rule: never hand the engine a non-place).
    float bad[3] = {NAN, 0.0f, 0.0f};
    CHECK(!Reconstruct(0.0f, 0.0f, 0.0f, 0.0f, bad, out), "reconstruct/NaN is refused");
}

// ---------------------------------------------------------------------------
// 4. the ride state machine
// ---------------------------------------------------------------------------
// One frame of a simulated rider.  `noise_cm` is the measured place on the platform; dollsMP
// measured the real one moving 50-900 cm between frames, so the tests feed it that.
static RideInput Frame(bool on, const uint8_t* id, float px, float py, float pz,
                       float mx, float my, float mz, float yaw, bool moving)
{
    RideInput in;
    memset(&in, 0, sizeof(in));
    in.on_mover = on;
    in.id = id;
    in.px = px; in.py = py; in.pz = pz;
    in.mx = mx; in.my = my; in.mz = mz;
    in.mover_yaw_deg = yaw;
    in.rider_yaw_deg = yaw;
    in.moving = moving;
    return in;
}

static void TestRideBoardingAndRest()
{
    Ride r;
    uint8_t id[kIdLen];
    for (int i = 0; i < kIdLen; ++i) id[i] = (uint8_t)(i + 1);

    // At rest, boarding is exact: the place is (offx, offy, offz) on the platform.
    RideInput in = Frame(true, id, 1200.0f, 2000.0f, 350.0f, 1000.0f, 2000.0f, 300.0f, 0.0f, false);
    CHECK(r.Step(in, 1000, 16.0f), "ride/boarding is a change worth sending");
    CHECK(r.Riding(), "ride/riding after boarding");
    CHECK_NEAR(r.Current().ox, 200.0f, 0.01f, "ride/place x");
    CHECK_NEAR(r.Current().oy, 0.0f, 0.01f, "ride/place y");
    CHECK_NEAR(r.Current().oz, 50.0f, 0.01f, "ride/place z");

    // A stationary rider at rest is NOT a change: no datagram.
    CHECK(!r.Step(in, 1016, 16.0f), "ride/a standing rider at rest sends nothing");

    // Walking on the platform at rest is followed (the measurement is exact at rest).  It
    // lags by tau x walk speed while it walks -- 200 ms x 125 cm/s = 25 cm -- and is exactly
    // right the moment the walking stops, which is the case that matters for a standing
    // passenger.
    float walk_ms = 1032.0f;
    for (int i = 0; i < 40; ++i) {
        in.px += 2.0f;             // 2 cm a frame on the platform
        walk_ms += 16.0f;
        r.Step(in, (uint32_t)walk_ms, 16.0f);
    }
    CHECK_NEAR(r.Current().ox, 280.0f, 30.0f, "ride/walking on a resting platform is followed");
    for (int i = 0; i < 40; ++i) {           // stop walking
        walk_ms += 16.0f;
        r.Step(in, (uint32_t)walk_ms, 16.0f);
    }
    CHECK_NEAR(r.Current().ox, 280.0f, 1.0f, "ride/and it is exact once they stand still");
}

static void TestRideMidRideNoise()
{
    Ride r;
    uint8_t id[kIdLen];
    for (int i = 0; i < kIdLen; ++i) id[i] = (uint8_t)(i + 7);

    // Board at rest.
    RideInput in = Frame(true, id, 1000.0f, 1000.0f, 200.0f, 900.0f, 1000.0f, 100.0f, 0.0f, false);
    r.Step(in, 0, 16.0f);
    float place0[3] = { r.Current().ox, r.Current().oy, r.Current().oz };

    // Now the platform moves and the MEASUREMENT is noisy in both directions by up to 400 cm
    // -- which is what dollsMP measured.  The place must not move one bit.
    float max_drift = 0.0f;
    uint32_t ms = 16;
    for (int i = 0; i < 200; ++i) {
        ms += 16;
        // the platform climbs 5 cm a frame
        in.mz += 5.0f;
        in.moving = true;
        // the measured place swarms: +-400 cm in x and y
        float nx = ((i % 7) - 3) * 120.0f;
        float ny = ((i % 5) - 2) * 190.0f;
        in.px = in.mx + place0[0] + nx;
        in.py = in.my + place0[1] + ny;
        in.pz = in.mz + place0[2];
        r.Step(in, ms, 16.0f);
        float d = fabsf(r.Current().ox - place0[0]);
        if (d > max_drift) max_drift = d;
        d = fabsf(r.Current().oy - place0[1]);
        if (d > max_drift) max_drift = d;
    }
    CHECK_NEAR(max_drift, 0.0f, 0.001f, "ride/mid-ride noise does not move the place");
    CHECK(r.Moving(), "ride/the moving flag is reported");

    // The rider is drawn on the receiver's copy wherever IT is: 900 cm of difference between
    // the two copies moves the doll with the local platform, not with the sender's.
    float off[3] = { r.Current().ox, r.Current().oy, r.Current().oz };
    float out[3];
    CHECK(Reconstruct(0.0f, 0.0f, 0.0f, 0.0f, off, out), "ride/reconstruct mid-ride");

    // Getting back to rest, the place leans onto the exact measurement and anything a ride
    // accumulated is taken out.
    in.moving = false;
    for (int i = 0; i < 120; ++i) {
        ms += 16;
        in.mz += 5.0f;
        in.px = in.mx + 233.0f;
        in.py = in.my + 17.0f;
        in.pz = in.mz + 50.0f;
        r.Step(in, ms, 16.0f);
    }
    CHECK_NEAR(r.Current().ox, 233.0f, 1.0f, "ride/at rest the place is exact again (x)");
    CHECK_NEAR(r.Current().oy, 17.0f, 1.0f, "ride/at rest the place is exact again (y)");
}

static void TestRideBumpAndStepOff()
{
    Ride r;
    uint8_t id[kIdLen];
    for (int i = 0; i < kIdLen; ++i) id[i] = (uint8_t)(i + 3);

    RideInput in = Frame(true, id, 1000.0f, 1000.0f, 200.0f, 900.0f, 1000.0f, 100.0f, 0.0f, false);
    r.Step(in, 0, 16.0f);
    CHECK(r.Riding(), "bump/riding");

    // The capture test flickers off for one frame while the platform moves: still riding
    // (measured on a fast platform in dollsMP: "attached 4, detached 3" in one ride).
    in.on_mover = false;
    in.moving = true;
    r.Step(in, 100, 16.0f);
    CHECK(r.Riding(), "bump/a one-frame capture loss does not end the ride");
    r.Step(in, 200, 16.0f);
    CHECK(r.Riding(), "bump/200 ms of silence is still riding");

    // Past the 400 ms window it is a step off.
    r.Step(in, 500, 16.0f);
    r.Step(in, 700, 16.0f);
    CHECK(!r.Riding(), "bump/past the grace window the ride ends");

    // At rest, losing the platform ends the ride at once: no bump is possible off a
    // stationary platform.
    Ride r2;
    RideInput rest = Frame(true, id, 1000.0f, 1000.0f, 200.0f, 900.0f, 1000.0f, 100.0f, 0.0f, false);
    r2.Step(rest, 0, 16.0f);
    rest.on_mover = false;
    rest.moving = false;
    r2.Step(rest, 16, 16.0f);
    CHECK(!r2.Riding(), "bump/a step off at rest is immediate");
}

static void TestRideBelieveFarMeasurement()
{
    Ride r;
    uint8_t id[kIdLen];
    for (int i = 0; i < kIdLen; ++i) id[i] = (uint8_t)(i + 5);
    RideInput in = Frame(true, id, 1000.0f, 1000.0f, 200.0f, 900.0f, 1000.0f, 100.0f, 0.0f, false);
    r.Step(in, 0, 16.0f);          // board at rest: place x = 100

    // Mid-ride, a measurement 3 m away from the place is believed: the player really moved
    // (fell off the far side, was pushed, teleported) while the platform kept moving.
    in.moving = true;
    in.mz += 100.0f;
    in.px = 900.0f + 3000.0f;
    r.Step(in, 16, 16.0f);
    CHECK_NEAR(r.Current().ox, 3000.0f, 1.0f,
               "believe/a 3 m disagreement mid-ride IS taken");

    // ... and one at 2 m is not: that is inside the noise band dollsMP measured.
    Ride r2;
    RideInput rest = Frame(true, id, 1000.0f, 1000.0f, 200.0f, 900.0f, 1000.0f, 100.0f,
                           0.0f, false);
    r2.Step(rest, 0, 16.0f);       // board at rest: place x = 100
    rest.moving = true;
    rest.mz += 100.0f;
    rest.px = 900.0f + 2000.0f;
    r2.Step(rest, 16, 16.0f);
    CHECK_NEAR(r2.Current().ox, 100.0f, 0.01f, "believe/2 m of noise is not believed");
}

static void TestRideSwitchPlatformAndIdentity()
{
    Ride r;
    uint8_t a[kIdLen], b[kIdLen];
    for (int i = 0; i < kIdLen; ++i) { a[i] = (uint8_t)i; b[i] = (uint8_t)(i + 100); }

    RideInput in = Frame(true, a, 1000.0f, 1000.0f, 200.0f, 900.0f, 1000.0f, 100.0f, 0.0f, false);
    r.Step(in, 0, 16.0f);
    CHECK(Ride::SameId(r.Current().id, a), "identity/the place carries the platform id");

    // Stepping onto a DIFFERENT platform is a boarding, not a silent re-key.
    in.id = b;
    CHECK(r.Step(in, 16, 16.0f), "identity/switching platform is a change");
    CHECK(Ride::SameId(r.Current().id, b), "identity/the place now names the new platform");

    // Relative heading is wrapped to +-180: a rider facing 350 on a platform facing 10 is
    // 20 deg to the LEFT, not 340 to the right.
    int16_t rel = Ride::RelativeYaw(350.0f, 10.0f);
    CHECK(rel < 0, "yaw/a left turn of the rider is negative");
    CHECK_NEAR((float)rel * 360.0f / 65536.0f, -20.0f, 0.1f, "yaw/the relative heading is -20");
}

static void TestCaptureTest()
{
    float d2 = 0.0f;
    // Standing on a platform whose root is at its own centre.
    CHECK(CaptureTest(0.0f, 0.0f, 100.0f, 0.0f, 0.0f, 0.0f, kCaptureHorizCm, kCaptureUpCm,
                      kCaptureDownCm, &d2), "capture/standing on it");
    // 400 cm to one side: not on it.
    CHECK(!CaptureTest(400.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, kCaptureHorizCm, kCaptureUpCm,
                       kCaptureDownCm, &d2), "capture/far to the side");
    // 600 cm above: not on it.
    CHECK(!CaptureTest(0.0f, 0.0f, 600.0f, 0.0f, 0.0f, 0.0f, kCaptureHorizCm, kCaptureUpCm,
                       kCaptureDownCm, &d2), "capture/far above");
    // 400 cm below: not on it.
    CHECK(!CaptureTest(0.0f, 0.0f, -400.0f, 0.0f, 0.0f, 0.0f, kCaptureHorizCm, kCaptureUpCm,
                       kCaptureDownCm, &d2), "capture/far below");
}

int main()
{
    printf("=== qd platforms tests ===\n");
    TestClassifier();
    TestEasing();
    TestReconstruct();
    TestRideBoardingAndRest();
    TestRideMidRideNoise();
    TestRideBumpAndStepOff();
    TestRideBelieveFarMeasurement();
    TestRideSwitchPlatformAndIdentity();
    TestCaptureTest();
    printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
