// qdplatforms.h -- moving platforms: which classes are movers, how a follower eases onto
// the owner's transform, and the PASSENGER lane (a rider's place ON the platform).
//
// WHAT WAS ALREADY THERE, AND THE THREE THINGS WRONG AROUND IT
// ------------------------------------------------------------
// `qdmover.h` owns the mover transform: one machine (the one whose player is nearer,
// host breaks a tie) publishes the absolute transform and the other writes it.  That half
// is right and is NOT rebuilt here.  Everything around it was wrong in a way that made the
// lane unreachable and the rider ugly:
//
//   1. THE CLASS FILTER DID NOT MATCH THIS GAME'S MOVERS, AND IT MATCHED A LIGHT.
//      `IsMoverClass` looked for "Elevator" / "Mover" / "Platform" / "Activatable".  Every
//      lift in Queen's Domain is named `*_Lift_*` (`Mausoleum_Lift_00_BP_C` x11,
//      `OW_Lift_00_BP_C` x5, `OW_TimedLift_00_BP_C` x6, `ROS_Lift_BP_C` x5,
//      `Castle_Lift_00_BP_C`, `Lab_Lift_BP_C`, ... -- census in `analysis/22-close-out.md`
//      §3 and `analysis/05-primary-evidence.md` §6), so the lane tracked NO lift at all;
//      and the fourth token matched a LIGHT, measured live in `997_FinalZoneCutscene`:
//      `movers: tracking FS_Light_Activatable372/... id=F9587D01`.  `IsMoverClass` below
//      is the census, and a light cannot match it.
//   2. THE FOLLOWER SNAPPED.  Rows arrive at 10 Hz and `MoverApply` called
//      `K2_SetActorLocationAndRotation` once per row, so on the machine that does not own
//      the platform it moved in ten steps a second.  `EaseAxis` is the missing filter.
//   3. THE RIDER WAS NOT ON THE PLATFORM AT ALL.  The partner's doll is drawn from that
//      player's own ABSOLUTE position samples, which are their own staircase while a lift
//      carries them, next to a platform moving on a different schedule.
//
// THE PASSENGER LANE, AND WHERE IT COMES FROM
// -------------------------------------------
// Ported from our other co-op mod: dollsMP `docs/sync/36-movers-elevators-platforms.md`.
// That document records three attempts and only the third worked, and its measurements are
// why this header has the shape it has:
//
//   * §7 sent the rider's place as "their sampled position minus the mover's route point"
//     and the receiver added the mover's velocity.  The measurement is NOT steady: measured
//     50-900 cm from one frame to the next, because the pawn and the mover are sampled at
//     different moments of the sender's frame.  No filter makes a standing passenger out
//     of it.
//   * §8 filtered it (250 ms lean, 150 cm snap) and attached the doll to the mover with
//     `K2_AttachToActor`.  Still twitched; the threshold was crossed almost every frame.
//   * §9 PASSENGER MODE is what worked: keep the rider's place **in the mover's own axes**,
//     do NOT use the measurements at all while the mover moves, and rebuild the drawn
//     position as `this machine's copy of the mover + axes * place`.  Simulated in their
//     `test_world`: 15 cm/frame of twitch from the samples, 17 cm/frame filtered, **0.02
//     cm/frame as a passenger**.
//
// So the wire carries `(platform stable id, place in the platform's axes)` and nothing
// else, and the receiver reconstructs from ITS OWN copy of the platform.  `qdmover.h`'s
// absolute rows and this lane are the two halves of one thing: MOVERS says where the
// platform is, PLATFORM_RIDE says where the person on it stands.
//
// Everything here is pure (no engine, no allocation) so it can be pinned off-target:
// `test_platforms.cpp` drives the classifier, the easing and the whole ride state machine
// with the numbers dollsMP measured.
#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>

namespace qd {
namespace platforms {

static const int kIdLen = 16;

// ---------------------------------------------------------------------------
// 1. Which classes are movers
// ---------------------------------------------------------------------------
// Case-insensitive substring, the same helper shape the other lanes use.
inline bool HasCI(const char* hay, const char* needle)
{
    if (!hay || !needle || !needle[0]) return false;
    for (int i = 0; hay[i]; ++i) {
        int k = 0;
        for (; needle[k]; ++k) {
            char a = hay[i + k];
            char b = needle[k];
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if (a != b) break;
        }
        if (!needle[k]) return true;
    }
    return false;
}

// The MOVER half: every class family the analysis corpus names as something that moves a
// player around.  `analysis/05-primary-evidence.md` §6 (`Elevator/Elevator_Base`,
// `MovingActor`, `Library_RotateBridge`, `Library_LowerBridge`) and the class tally in
// `analysis/22-close-out.md` §3 (`*_Lift_*` x16 families, `*_Elevator*` x2,
// `LabUG_Tram_BP_C`, `Library_RotateBridge_C`).
inline const char* const* MoverTokens(int* count)
{
    static const char* const kMoverTokens[] = {
        "Lift",          // Mausoleum_Lift_00_BP_C, OW_Lift_00_BP_C, OW_TimedLift_00_BP_C,
                         // Castle_Lift_00_BP_C, SZ_Lift_00_BP_C, PT_Lift_00_BP_C,
                         // Lab_Lift_BP_C, ROS_Lift_BP_C, OfficeLift_00_BP_C,
                         // IntroDungerLift_BP_C, OW_BrokenLift_00_BP_C, AQR_NoSoftLockLift_C,
                         // SZ_Lift_01_BP1_C
        "Elevator",      // Elevator_Base, AquaElevator_00_BP_C, Lib_ElevatorSmall_00BP_C
        "Tram",          // LabUG_Tram_BP_C
        "Mover",         // the generic family name
        "MovingPlatform",// DoMovingPlatformCollisionHack names the mechanism
        "MovingActor",   // Elevator/MovingActor
        "RotateBridge",  // Library_RotateBridge_C (it turns)
        "LowerBridge",   // Library_LowerBridge_C (it lowers)
        "Scaffold",      // the base family for a raising platform
    };
    if (count) *count = (int)(sizeof(kMoverTokens) / sizeof(kMoverTokens[0]));
    return kMoverTokens;
}

// The NOT half, checked FIRST.  Every entry is here because the corpus has a class that
// carries the mover word but is NOT a mover -- a control, a data asset, a static mesh --
// and tracking those is how the lane gets evicted by its own 64-record table.
inline const char* const* NotMoverTokens(int* count)
{
    static const char* const kNotMover[] = {
        // the CONTROLS.  `OW_Lift_Lever_00_BP`, `LabElevatorSwitch`, `LiftLever` -- these are
        // the input to a platform, not the platform (the mover lane follows the transform,
        // so the lever needs no separate lane; this is also what qdobjectstate.h SH_NONE
        // says about them).
        "Lever", "Switch", "Button",
        // other interactables that name a lift in their class
        "Door", "Gate", "Trigger",
        // not an actor that can be a mover
        "Group", "Manager", "Subsystem", "Component", "Volume", "Sphere", "Icon",
        "Light", "Camera", "Sound", "Effect", "Widget", "Text", "Sequence",
        // a data asset / spawner / a kill volume named after the lift
        "Data", "Spawner", "Kill",
    };
    if (count) *count = (int)(sizeof(kNotMover) / sizeof(kNotMover[0]));
    return kNotMover;
}

inline bool IsMoverClass(const char* cls)
{
    if (!cls || !cls[0]) return false;
    int n = 0;
    const char* const* no = NotMoverTokens(&n);
    for (int i = 0; i < n; ++i)
        if (HasCI(cls, no[i])) return false;
    const char* const* yes = MoverTokens(&n);
    for (int i = 0; i < n; ++i)
        if (HasCI(cls, yes[i])) return true;
    return false;
}

// ---------------------------------------------------------------------------
// 1b. WHICH ELEMENT OF THE ACTOR MOVES
// ---------------------------------------------------------------------------
// THE BUG THIS EXISTS FOR.  The mover lane read the ACTOR transform
// (`AActor::RootComponent`'s RelativeLocation).  But `Elevator_Base` -- the parent of every
// lever elevator in this game, read out of the pak: exports `ElevatorPlatformRoot`, and its
// ubergraph imports `MoveComponentTo`, `K2_GetComponentLocation`,
// `K2_SetRelativeLocationAndRotation`) does not move the actor at all: it moves its own
// `ElevatorPlatformRoot` SCENE COMPONENT.  `Mausoleum_Lift_00_BP` likewise ticks and calls
// `K2_SetRelativeLocation`.  So an actor-root read sees a lift that never moves, which is
// exactly what Session 31 measured (`their transform does not change`) about a lift nobody
// had made travel yet.
//
// THE RULE.  A mover is (actor, element).  Element 0 is the actor root, which is what every
// earlier session measured and what a mover that really does move its actor still uses.
// Element k+1 is the k-th child `USceneComponent`, ORDERED BY NAME -- the two machines run
// the same cooked package, so they enumerate the same components in the same order and the
// index means the same thing on both.  The index travels in the top six bits of the
// existing `MOVERS` row `flags` byte, so the wire shape (33 B) and the Rust side are
// untouched: `flags` bit0 moving, bit1 owner, bits 2-7 element.
static const int kMaxElems    = 8;
static const int kElemNameLen = 40;

static const uint8_t kElemShift = 2;
static const uint8_t kElemMask  = 0x3F;

inline uint8_t EncodeElem(int elem)
{
    if (elem < 0) elem = 0;
    if (elem > kMaxElems) elem = 0;
    return (uint8_t)(((uint32_t)elem << kElemShift) & kElemMask);
}

inline int DecodeElem(uint8_t flags) { return (int)((flags & kElemMask) >> kElemShift); }

// The element a frame's flags name, clamped to what this machine can address.  A row from a
// peer that has more components than this copy falls back to the root rather than writing
// the wrong component.
inline int ClampElem(int elem, int haveN)
{
    if (elem <= 0) return 0;
    return elem <= haveN ? elem : 0;
}

// ---------------------------------------------------------------------------
// 2. Geometry
// ---------------------------------------------------------------------------
static constexpr float kPi = 3.14159265358979323846f;

// UE yaw: forward is (cos, sin) in XY, and a positive yaw turns X toward Y.
inline void LocalToWorld2D(float lx, float ly, float yaw_deg, float* wx, float* wy)
{
    float a = yaw_deg * (kPi / 180.0f);
    float c = cosf(a), s = sinf(a);
    *wx = lx * c - ly * s;
    *wy = lx * s + ly * c;
}

inline void WorldToLocal2D(float wx, float wy, float yaw_deg, float* lx, float* ly)
{
    float a = -yaw_deg * (kPi / 180.0f);
    float c = cosf(a), s = sinf(a);
    *lx = wx * c - wy * s;
    *ly = wx * s + wy * c;
}

inline bool SanePos(const float p[3])
{
    const float kMax = 1.0e7f;
    for (int i = 0; i < 3; ++i) {
        float v = p[i];
        if (!(v == v)) return false;
        if (v > kMax || v < -kMax) return false;
    }
    return true;
}

// Did a spot move between two samples?  `kSpotCm` is the same 0.4 cm the mover lane already
// uses as its "this frame moved" threshold, so the element search and the moving test agree.
static constexpr float kSpotCm = 0.4f;

inline bool MovedBy(const float a[3], const float b[3])
{
    for (int i = 0; i < 3; ++i) {
        float d = a[i] - b[i];
        if (d > kSpotCm || d < -kSpotCm) return true;
    }
    return false;
}

// ONE FRAME OF THE ELEMENT SEARCH.  The mover lane cannot afford to poll every component of
// every mover every frame, so it commits to one element and probes one other per frame:
//
//   * the root moving always wins -- that is a mover that really does move its actor, and
//     it is the case every earlier session measured;
//   * otherwise the element it already follows is kept while it moves;
//   * otherwise the one probed element is adopted if IT moved.
//
// `probed` is an element index (1..n) or 0 for "nothing probed this frame".  Returns the
// element to follow from the next frame on.
inline int StepElem(int cur, bool rootMoved, bool curMoved, int probed, bool probedMoved)
{
    if (rootMoved) return 0;
    if (cur > 0 && curMoved) return cur;
    if (probed > 0 && probedMoved) return probed;
    return cur;
}

// A rider's place in the platform's axes, rebuilt on THIS machine's copy of the platform.
// This is the whole receiver-side rule of dollsMP §9 and it is one line of geometry.
inline bool Reconstruct(float mx, float my, float mz, float yaw_deg,
                        const float off[3], float out[3])
{
    float wx = 0.0f, wy = 0.0f;
    LocalToWorld2D(off[0], off[1], yaw_deg, &wx, &wy);
    out[0] = mx + wx;
    out[1] = my + wy;
    out[2] = mz + off[2];
    return SanePos(out);
}

// IS THE LOCAL PLAYER STANDING ON THIS PLATFORM?
// The mod cannot read `ACharacter::BasedMovement.MovementBase` here: the substrate resolves
// reflected property offsets from a class, and `MovementBase` lives inside the nested
// `FBasedMovementInfo` struct, so a by-name lookup on the movement component finds nothing.
// dollsMP read it at a hard-coded `+0x298`, which is that build's layout and not ours.
// So the capture is GEOMETRIC and the live log prints it (`platforms: capture ...`), which
// is what makes a wrong answer measurable instead of assumed.
//
// The three numbers are generous on purpose: the pawn root is a capsule centre, while a
// lift actor's root can be anywhere in its own geometry, so the vertical window has to hold
// "standing on top of it" as well as "standing in it".
inline bool CaptureTest(float px, float py, float pz,
                        float mx, float my, float mz,
                        float horiz_cm, float up_cm, float down_cm,
                        float* out_d2)
{
    float dx = px - mx, dy = py - my, dz = pz - mz;
    float h2 = dx * dx + dy * dy;
    if (out_d2) *out_d2 = h2;
    if (!(h2 == h2)) return false;
    if (h2 > horiz_cm * horiz_cm) return false;
    return dz <= up_cm && dz >= -down_cm;
}

// The capture window the engine half uses.  320 cm horizontally is a platform plus a step;
// 300 cm up and 150 cm down covers a capsule centre above an actor root that sits inside
// the platform's own mesh.
static constexpr float kCaptureHorizCm = 320.0f;
static constexpr float kCaptureUpCm    = 300.0f;
static constexpr float kCaptureDownCm  = 150.0f;

// ---------------------------------------------------------------------------
// 3. The follower's easing (fixes the 10 Hz staircase)
// ---------------------------------------------------------------------------
// Rows arrive at 10 Hz; the platform must move every frame.  A first-order filter with a
// 60 ms constant settles the staircase within about one row interval without visibly
// lagging the owner (the owner's own animation is the reference, and it is 10 Hz worth of
// error to begin with).  A gap larger than `snap_cm` is a discontinuity -- a new world, a
// reset, a mover that teleported -- and is taken at once, because easing through it draws
// a slide through geometry.
inline float EaseTimeAlpha(float dt_ms, float tau_ms)
{
    if (!(dt_ms > 0.0f)) return 0.0f;
    if (!(tau_ms > 0.0f)) return 1.0f;
    float a = 1.0f - expf(-dt_ms / tau_ms);
    if (!(a == a)) return 1.0f;
    return a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a);
}

inline float EaseAxis(float cur, float tgt, float alpha, float snap_cm)
{
    float d = tgt - cur;
    if (d > snap_cm || d < -snap_cm) return tgt;
    return cur + d * alpha;
}

static constexpr float kEaseTauMs   = 60.0f;
static constexpr float kEaseSnapCm  = 300.0f;

// ---------------------------------------------------------------------------
// 4. The ride state machine (the SENDER half)
// ---------------------------------------------------------------------------
struct Place {
    bool     have;
    uint8_t  id[kIdLen];
    float    ox, oy, oz;    // the rider's place in the platform's axes, cm
};

// "Their place on it, largest change in one frame" was 50-900 cm in dollsMP §9.  Mid-ride,
// only a disagreement this large is believed: it means the player really is elsewhere (a
// long fall off the platform, a teleport), not that the two samples were taken a frame
// apart.
static constexpr float kBelieveCm = 2500.0f;

// "On no mover" for this long ends the ride.  Measured on a fast platform in dollsMP §9:
// `attached 4, detached 3` in one ride -- the movement base flickers, the player does not.
static constexpr uint32_t kGraceMs = 400;

// At rest the measurement is exact, so lean onto it over this constant.
static constexpr float kRestLeanMs = 200.0f;

// A place that changed by less than this is not a change worth a datagram.
static constexpr float kPublishEpsCm = 0.5f;

struct RideInput {
    bool           on_mover;      // the capture test says the local player is on `id`
    const uint8_t* id;            // that platform's stable id (16 bytes)
    float          px, py, pz;    // the pawn root, world cm
    float          mx, my, mz;    // the platform root, world cm (this machine's copy)
    float          mover_yaw_deg;
    float          rider_yaw_deg;
    bool           moving;        // this machine's copy moved this frame
};

class Ride
{
public:
    Ride() { Reset(); }

    void Reset()
    {
        memset(&place_, 0, sizeof(place_));
        last_on_ms_ = 0;
        moving_ = false;
        rel_yaw_deg_ = 0.0f;
        dirty_ = true;
    }

    bool               Riding() const { return place_.have; }
    bool               Moving() const { return moving_; }
    const Place&       Current() const { return place_; }
    float              RelYawDeg() const { return rel_yaw_deg_; }

    // One frame.  Returns true when the published place changed enough to send.
    //
    // THE RULE, IN ORDER (each line is a dollsMP §9 finding):
    //   * boarding believes the measurement -- at rest it is exact, and a jump onto a
    //     moving platform has to be believed once;
    //   * while the platform moves the measurement is IGNORED, because it is 50-900 cm of
    //     noise per frame; a standing passenger's place in the platform's axes does not
    //     change at all;
    //   * the one thing believed mid-ride is a disagreement over 25 m (they really left);
    //   * at rest the place leans onto the exact measurement, so whatever a ride
    //     accumulated is taken out at the station;
    //   * losing the platform for under 400 ms while it moves is a bump, not a step off.
    bool Step(const RideInput& in, uint32_t now_ms, float dt_ms)
    {
        const bool  was_have  = place_.have;
        const bool  was_moving = moving_;
        uint8_t     prev_id[kIdLen];
        memcpy(prev_id, place_.id, kIdLen);
        const float p0 = place_.ox, p1 = place_.oy, p2 = place_.oz;

        if (!in.on_mover || !in.id) {
            if (place_.have && in.moving && (now_ms - last_on_ms_) < kGraceMs) {
                // a bump: keep the passenger
            } else {
                place_.have = false;
            }
        } else {
            last_on_ms_ = now_ms;
            float dx = in.px - in.mx, dy = in.py - in.my, dz = in.pz - in.mz;
            float lx = 0.0f, ly = 0.0f;
            WorldToLocal2D(dx, dy, in.mover_yaw_deg, &lx, &ly);

            if (!place_.have || !SameId(place_.id, in.id)) {
                place_.have = true;
                memcpy(place_.id, in.id, kIdLen);
                place_.ox = lx; place_.oy = ly; place_.oz = dz;
            } else if (!in.moving) {
                float a = EaseTimeAlpha(dt_ms > 0.0f ? dt_ms : 16.0f, kRestLeanMs);
                place_.ox = EaseAxis(place_.ox, lx, a, kBelieveCm);
                place_.oy = EaseAxis(place_.oy, ly, a, kBelieveCm);
                place_.oz = EaseAxis(place_.oz, dz, a, kBelieveCm);
            } else {
                float ex = lx - place_.ox, ey = ly - place_.oy, ez = dz - place_.oz;
                if (ex * ex + ey * ey + ez * ez > kBelieveCm * kBelieveCm) {
                    place_.ox = lx; place_.oy = ly; place_.oz = dz;
                }
            }
        }

        moving_ = in.moving;

        // The rider's heading RELATIVE to the platform, so a turning platform carries the
        // facing with it.  Only meaningful while riding.
        if (in.on_mover && in.id) {
            float d = in.rider_yaw_deg - in.mover_yaw_deg;
            while (d > 180.0f) d -= 360.0f;
            while (d < -180.0f) d += 360.0f;
            rel_yaw_deg_ = d;
        }

        bool changed = (was_have != place_.have) || (was_moving != moving_) ||
                       !SameId(prev_id, place_.id) ||
                       fabsf(p0 - place_.ox) > kPublishEpsCm ||
                       fabsf(p1 - place_.oy) > kPublishEpsCm ||
                       fabsf(p2 - place_.oz) > kPublishEpsCm;
        if (changed) dirty_ = true;
        return changed;
    }

    // The rider's heading RELATIVE to the platform, so a turning platform carries the
    // facing too.  Wrapped to one turn of the quantiser's range.
    static int16_t RelativeYaw(float rider_deg, float mover_deg)
    {
        float d = rider_deg - mover_deg;
        while (d > 180.0f) d -= 360.0f;
        while (d < -180.0f) d += 360.0f;
        return (int16_t)(int32_t)(d * (65536.0f / 360.0f));
    }

    bool TakeDirty()
    {
        bool d = dirty_;
        dirty_ = false;
        return d;
    }

    static bool SameId(const uint8_t* a, const uint8_t* b)
    {
        for (int i = 0; i < kIdLen; ++i)
            if (a[i] != b[i]) return false;
        return true;
    }

private:
    Place    place_;
    uint32_t last_on_ms_;
    bool     moving_;
    float    rel_yaw_deg_;
    bool     dirty_;
};

} // namespace platforms
} // namespace qd
