// qdview.h -- the dialogue spectator CAMERA and the "may I cut yet" policy.
//
// THE TWO DEFECTS THIS IS WRITTEN AGAINST, BOTH MEASURED ON THE RIG
// ----------------------------------------------------------------
// A. THE CAMERA IS INSIDE THE NPC.  `DlgSpectatorShow` used to call
//    `SetViewTargetWithBlend(npcActor)` (proxy.cpp, qddialogue2.h).  With an actor as
//    the view target UE uses the actor's own camera component, and an NPC has none, so
//    the POV ends up at the actor's ORIGIN -- inside its mesh, at its feet, with the
//    player's control rotation.  The user's screenshot
//    (`_test/sdlg-spectator/shots/sdlg-spectator-during-inst1.png`) shows exactly that:
//    the dialogue box ("Bored Knight") is readable, the NPC is invisible (a camera
//    inside a mesh sees back faces, which are culled) and the ground/foliage clip
//    through the near plane.
//
//    What the user asked for is "the conversation from the point of view of the one
//    talking", i.e. the TALKER's eye.  The talker is not on this machine: the only
//    facts here are the partner's PLAYER row (capsule CENTRE + view yaw/pitch, WIRE.md
//    3.5) and, when PuppetSync is on, the partner's rendered body.  So the camera is
//    built from the row: eye = centre + kEyeAboveCentre, pulled slightly BACK and
//    SIDEWAYS (over the shoulder) so the talker's own body is in frame and the near
//    plane is outside both meshes, then aimed at the NPC's head.
//
// B. THE CUT HAPPENS BEFORE THE TALKER'S AREA IS ON SCREEN.  A dialogue can start the
//    instant the talking player enters an area whose levels the watching machine has
//    not streamed (level streaming here is the game's own level-list stack, qdstream.h;
//    the union forces the partner's levels at two per pump, so it lags).  Blending the
//    camera to a place with no geometry is the "parts of the map and the NPC are
//    missing" half of the report.  Hence a WAIT: the widget, the input lock and the
//    cursor happen at once (the line is readable immediately), but the CUT is held
//    until the caller reports the NPC's level on screen -- with a bound, because a
//    camera that never cuts is a worse defect than one that cuts into a hole, and the
//    log has to say which happened.
//
// WHY THE MATH IS HERE AND NOT AT THE CALL SITE
// ---------------------------------------------
// The camera is a place and two angles, and the mistakes are numeric: a yaw in the
// wrong quadrant, a pitch with the wrong sign (UE pitch is positive UP), an eye height
// taken from the FEET instead of the capsule centre.  Each one looks like "the camera
// faced the wrong way", which costs a two-instance run to diagnose.  So the placement
// and the cut policy are pure and `test_view.cpp` pins them to exact numbers.
#pragma once
#include <math.h>
#include <stdint.h>

namespace qd {
namespace view {

// ---------------------------------------------------------------------------
// Placement
// ---------------------------------------------------------------------------
// The PLAYER row reports a capsule CENTRE (WIRE.md 3.5).  This game's capsule half
// height is 90 cm (measured, `capsule half height 90.0` in every run), so the eyes sit
// ~80 % of a half height above that centre -- 72 cm, i.e. 162 cm above the feet for a
// 170 cm body.  Using the FEET value (pz - 90) would put the camera at the hips.
static const float kCapsuleHalfHeightCm = 90.0f;
static const float kEyeAboveCentreFactor = 0.8f;

// Over the shoulder, not inside the head: back from the eye, to the talker's right, and
// slightly up.  Measured framing at a 150-200 cm NPC distance: the talker's head sits ~19
// deg left of centre (inside a 90 deg horizontal FOV, on the left third of the frame) and
// the NPC's face is centred, so "the one talking" is unmistakably the viewpoint's owner.
// These are deliberately smaller than a gameplay boom -- the requirement is the talker's
// point of view, and a 2 m third-person camera would make it the camera's.
static const float kBackCm = 80.0f;
static const float kSideCm = 45.0f;
static const float kUpCm = 15.0f;

// When the talker's position is unknown (no PLAYER row yet) the camera is put in front
// of the NPC at conversation distance instead.  Refusing to place it at all would leave
// the spectator with B's own view, which is the old, visibly wrong behaviour.
static const float kFallbackDistanceCm = 150.0f;

// A pose the caller writes to the temporary CameraActor.  UE angles, degrees:
// pitch positive looks UP, yaw positive turns toward +Y, roll unused.
struct CamPose {
    bool  valid;
    float x, y, z;
    float pitch, yaw;
};

// Where the talker is, as this machine can know it.
struct TalkerPose {
    bool  valid;
    float x, y, z;      // capsule CENTRE, world cm
    float yaw, pitch;   // the talker's view, degrees (informational only)
};

// What to look at: the NPC's head.  `measured` says whether that HEIGHT came from the
// engine (bounds, capsule) or is a default: a default is not trusted for the angle.
struct AimPoint {
    bool  valid;
    bool  measured;
    float x, y, z;
};

inline float RadToDeg(float r) { return r * (180.0f / 3.14159265358979323846f); }

// A row is a NETWORK fact, and a network fact can be stale, out of order, or name the
// moment before a teleport.  MEASURED (session view-fix, run 2): 300 ms after the rig
// teleported the talking player, the movement interpolator still answered the position from
// BEFORE the teleport, and the camera cut 3.4 km from the conversation -- pitch 83 deg, the
// shot was of the canopy.  A dialogue needs the two people within reach, so a row that puts
// the talker 12 m or more from the NPC is not this conversation's anchor: the NPC's own
// place is used instead.  That is a bound, not a preference -- the real positions differ by
// the interaction distance (2-3 m).
static const float kAnchorMaxCm = 1200.0f;

inline bool AnchorPlausible(const TalkerPose& t, const AimPoint& aim)
{
    if (!t.valid || !aim.valid) return false;
    float dx = t.x - aim.x, dy = t.y - aim.y, dz = t.z - aim.z;
    return dx * dx + dy * dy + dz * dz <= kAnchorMaxCm * kAnchorMaxCm;
}

// Yaw that points along (dx, dy).  UE: +X is 0 deg, +Y is +90 deg.
inline float YawDegTo(float dx, float dy)
{
    if (dx == 0.0f && dy == 0.0f) return 0.0f;
    return RadToDeg(atan2f(dy, dx));
}

// Pitch that points along a rise of `dz` over a horizontal run of `horiz`.  UE pitch is
// positive UP, which is the OPPOSITE sign of the screen-space convention people reach
// for; the caller gets one answer and the test pins it.
inline float PitchDegTo(float dz, float horiz)
{
    if (horiz <= 0.0f) return dz >= 0.0f ? 89.0f : -89.0f;
    return RadToDeg(atan2f(dz, horiz));
}

// The camera for a conversation, from the talker's eye over their shoulder, aimed at
// the NPC's head.  `valid` is false only when there is nothing to aim at.
//
// The direction used for the shoulder offset is TALKER -> NPC (horizontal), not the
// talker's view yaw: the player may be looking away from the NPC while reading, and
// anchoring the frame to the conversation is what makes the shot the same on both
// machines.
//
// EYE TO EYE WHEN THE HEIGHT IS A GUESS.  MEASURED (session view-fix, run 3): the NPC's
// measured-height sources all missed on this build's ghosts, the default said "the origin is
// at the feet" (+160), and the NPC's origin is really its capsule CENTRE -- so the camera
// aimed 84 cm over its head and the shot was of the sky above the trees.  A default is not a
// measurement and must not be aimed at: when `aim.measured` is false the camera looks at the
// TALKER'S OWN EYE HEIGHT instead, which is where a standing conversation partner's head is
// whenever the two share the ground (they are 2-3 m apart; a slope costs a few cm).
inline CamPose Compose(const TalkerPose& t, const AimPoint& aim, float halfHeightCm)
{
    CamPose c = {};
    if (!aim.valid) return c;
    if (!(halfHeightCm > 20.0f && halfHeightCm < 300.0f)) halfHeightCm = kCapsuleHalfHeightCm;
    float eyeAbove = halfHeightCm * kEyeAboveCentreFactor;
    float ex = t.x, ey = t.y, ez = t.z + eyeAbove;
    bool haveTalker = t.valid;
    if (haveTalker) {
        float dx = aim.x - ex, dy = aim.y - ey;
        float len = sqrtf(dx * dx + dy * dy);
        if (len > 1.0f) {
            float ux = dx / len, uy = dy / len;
            float rx = -uy, ry = ux;              // the talker's RIGHT for heading (ux,uy)
            c.x = ex - ux * kBackCm + rx * kSideCm;
            c.y = ey - uy * kBackCm + ry * kSideCm;
            c.z = ez + kUpCm;
            c.valid = true;
        } else {
            // Aim point on top of the talker (the NPC is behind them, or the row is the
            // NPC's own place): no usable direction, fall through to the NPC anchor.
            haveTalker = false;
        }
    }
    if (!haveTalker) {
        // The talker's place is unknown: stand in front of the NPC, facing it.  The NPC's
        // own facing is not known in this entry point, so the fixed world -X side is the
        // convention; `ComposeFacingNpc` is the one a caller with the NPC's yaw uses.
        c.x = aim.x - kFallbackDistanceCm;
        c.y = aim.y;
        c.z = aim.z + kUpCm;
        c.valid = true;
    }
    float aimZ = aim.z;
    if (!aim.measured && haveTalker) aimZ = ez;
    float vx = aim.x - c.x, vy = aim.y - c.y, vz = aimZ - c.z;
    float horiz = sqrtf(vx * vx + vy * vy);
    c.yaw = YawDegTo(vx, vy);
    c.pitch = PitchDegTo(vz, horiz);
    return c;
}

// The default capsule half height, for a caller that has not measured this body.
inline CamPose Compose(const TalkerPose& t, const AimPoint& aim)
{
    return Compose(t, aim, kCapsuleHalfHeightCm);
}

// The same placement when the NPC's own facing is known and the talker is not: put the
// camera in FRONT of the NPC (the side a conversation happens on) rather than at a
// fixed world axis.  `npcYawDeg` is the NPC actor's yaw.
inline CamPose ComposeFacingNpc(const AimPoint& aim, float npcYawDeg)
{
    CamPose c = {};
    if (!aim.valid) return c;
    float r = npcYawDeg * (3.14159265358979323846f / 180.0f);
    float fx = cosf(r), fy = sinf(r);
    c.x = aim.x + fx * kFallbackDistanceCm + (-fy) * kSideCm;
    c.y = aim.y + fy * kFallbackDistanceCm + (fx) * kSideCm;
    c.z = aim.z + kUpCm;
    float vx = aim.x - c.x, vy = aim.y - c.y, vz = aim.z - c.z;
    c.yaw = YawDegTo(vx, vy);
    c.pitch = PitchDegTo(vz, sqrtf(vx * vx + vy * vy));
    c.valid = true;
    return c;
}

// Did the pose move enough to be worth re-writing?  The talker stands still during a
// conversation, so a re-place is only for a genuine move (a teleport, a knockback) and
// for the eye height becoming readable late.  Rewriting every frame would fight the
// camera blend for its whole duration.
static const float kMoveTolCm = 25.0f;
static const float kMoveTolDeg = 3.0f;

inline float AngleDeltaDeg(float a, float b)
{
    float d = a - b;
    while (d > 180.0f) d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    return d < 0.0f ? -d : d;
}

inline bool Moved(const CamPose& a, const CamPose& b)
{
    if (!b.valid) return false;
    if (!a.valid) return true;
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    if (dx * dx + dy * dy + dz * dz > kMoveTolCm * kMoveTolCm) return true;
    return AngleDeltaDeg(a.yaw, b.yaw) > kMoveTolDeg ||
           AngleDeltaDeg(a.pitch, b.pitch) > kMoveTolDeg;
}

// ---------------------------------------------------------------------------
// The cut policy
// ---------------------------------------------------------------------------
// The widget/input/cursor are shown at once -- the line is what the spectator came for,
// and it costs nothing to read a box over a streamed-out area.  The CAMERA is held until
// the caller says the talker's level is on screen.  Four seconds is the bound: the
// measured stream of an already-resident sublevel is under a second, and a level the
// game has to load from disk is well inside it; past that the camera cuts anyway and
// says so, because "no camera at all" is a worse defect than "a camera in a hole".
static const uint32_t kMaxWaitMs = 4000;

// After the blend back to the pawn, the temporary CameraActor is kept alive for this
// long.  Destroying the actor that is still the OUTGOING view target would leave the
// player camera manager reading a destroyed actor for the blend.  700 ms covers the
// 0.4 s blend with margin.
static const uint32_t kRestoreHoldMs = 700;

enum Act {
    ACT_NONE = 0,
    ACT_CUT = 1,          // the level is on screen: blend now
    ACT_CUT_ANYWAY = 2,   // the wait expired: blend now and log that it did
    ACT_DESTROY = 3
};

class CamState {
public:
    CamState() { Reset(); }

    void Reset()
    {
        s_ = S_IDLE;
        at_ = 0;
        cutAt_ = 0;
        waited_ = 0;
    }

    // A conversation started and a temporary camera exists.
    void Begin(uint32_t now_ms)
    {
        s_ = S_WAIT;
        at_ = now_ms;
        cutAt_ = 0;
        waited_ = 0;
    }

    // Called every pump while WAITING.  Returns ACT_CUT exactly once (when `ready`), or
    // ACT_CUT_ANYWAY when the bound expired.  It does NOT change state: the caller calls
    // `Cut()` only after the blend actually ran, so a failed `SetViewTargetWithBlend`
    // stays visible as a state the next pump retries.
    Act Poll(bool ready, uint32_t now_ms)
    {
        if (s_ != S_WAIT) return ACT_NONE;
        waited_ = now_ms - at_;
        if (ready) return ACT_CUT;
        if (waited_ >= kMaxWaitMs) return ACT_CUT_ANYWAY;
        return ACT_NONE;
    }

    void Cut(uint32_t now_ms)
    {
        s_ = S_LIVE;
        cutAt_ = now_ms;
    }

    // The conversation ended (or the session did): give the pawn its camera back.  Only a
    // camera that was taken owes a restore.
    bool BeginRestore(uint32_t now_ms)
    {
        if (s_ != S_LIVE) { s_ = S_IDLE; return false; }
        s_ = S_RESTORE;
        at_ = now_ms;
        return true;
    }

    Act PollRestore(uint32_t now_ms)
    {
        if (s_ != S_RESTORE) return ACT_NONE;
        if ((uint32_t)(now_ms - at_) >= kRestoreHoldMs) return ACT_DESTROY;
        return ACT_NONE;
    }

    void Destroyed() { s_ = S_IDLE; }

    bool Waiting() const { return s_ == S_WAIT; }
    bool CutDone() const { return s_ == S_LIVE; }
    bool Restoring() const { return s_ == S_RESTORE; }
    bool Engaged() const { return s_ == S_WAIT || s_ == S_LIVE; }

    // How long the cut has been BLOCKED, for the log.  Zero before the first poll.
    uint32_t WaitedMs() const { return waited_; }

private:
    enum S { S_IDLE, S_WAIT, S_LIVE, S_RESTORE } s_;
    uint32_t at_;
    uint32_t cutAt_;
    uint32_t waited_;
};

} // namespace view
} // namespace qd
