// qdavatar.h -- the remote player's visible body.
//
// WHY NOT A COPY OF THE PLAYER CHARACTER
// --------------------------------------
// The game is first-person.  FS_Full_PlayerChar_C draws only first-person arms (its
// CharacterMesh0 carries no body), so a spawned copy rendered as "two small blue
// fragments" (LIVE-TEST-LOG).  It is also a full gameplay Blueprint -- input, HUD
// hooks, weapon systems -- which is the wrong thing to instantiate for a puppet.
//
// The game does ship a full-body model of the player: Player_FullChar_2026 (skeleton
// Player_FullChar_2026_Skeleton, an Auto-Rig Pro export with Weapon_L / Weapon_R bones),
// used by the intro and ending cutscenes.  It is not referenced by gameplay, so it is
// loaded on demand by path through UKismetSystemLibrary::LoadAsset_Blocking.  The body
// is a plain engine SkeletalMeshActor carrying that mesh: no gameplay logic at all.
//
// Everything here goes through reflected UFunctions (qdreflect.h); no engine struct
// layout is assumed beyond FTransform and FSoftObjectPtr, both checked against the
// reflected parameter size before use.
#pragma once

#include "qdreflect.h"
#include "qdcombatvfx.h"
#include <math.h>

namespace qd {
namespace avatar {

static const char* kBodyMeshPath =
    "/Game/Player/Assets/FPModel/Player2026/Player_FullChar_2026.Player_FullChar_2026";

// "<package>.<object>" for a top-level asset (Outer is its UPackage).  0 if the object
// is not a top-level asset.
inline int AssetPath(const void* obj, char* out, int cap)
{
    out[0] = 0;
    if (!obj) return 0;
    const void* pkg = ObjOuter(obj);
    if (!pkg || ObjOuter(pkg) != nullptr) return 0;
    char a[200], b[100];
    if (FNameToString(ObjNameRaw(pkg), a, sizeof(a)) <= 0) return 0;
    if (FNameToString(ObjNameRaw(obj), b, sizeof(b)) <= 0) return 0;
    int w = 0;
    for (int i = 0; a[i] && w < cap - 1; ++i) out[w++] = a[i];
    if (w < cap - 1) out[w++] = '.';
    for (int i = 0; b[i] && w < cap - 1; ++i) out[w++] = b[i];
    out[w] = 0;
    return w;
}

// Load (or find) an asset by "<package>.<object>" path.  Game thread only.
inline void* LoadAsset(const char* path)
{
    static void* ksl = nullptr;
    static Fn fLoad = {};
    if (!ksl || !ObjectIsLive(ksl)) ksl = FindObjectByNameAndClass("Default__KismetSystemLibrary", "KismetSystemLibrary");
    if (!ksl) { QDLOG(LOG_ERROR, "avatar: KismetSystemLibrary CDO not found"); return nullptr; }
    if (!fLoad.Resolve(ksl, "LoadAsset_Blocking")) return nullptr;
    const PropDesc* d = FindPropOfType(fLoad.sig, "SoftObjectProperty");
    if (!d || d->size != 40) {
        QDLOG(LOG_ERROR, "avatar: LoadAsset_Blocking parameter is not a 40-byte soft pointer");
        return nullptr;
    }
    QdName n;
    if (!FNameFromAnsi(path, &n)) return nullptr;
    Call c(fLoad);
    // TPersistentObjectPtr<FSoftObjectPath>: FWeakObjectPtr(8) TagAtLastTest(4)+pad(4)
    // then FSoftObjectPath { FName AssetPathName; FString SubPathString } -- 40 bytes.
    uint8_t* p = c.At(d->name, 40);
    if (!p) return nullptr;
    memcpy(p + 16, &n, 8);
    if (!c.Run(ksl)) { QDLOG(LOG_ERROR, "avatar: LoadAsset_Blocking faulted for ", path); return nullptr; }
    void* r = c.Ret() ? *(void**)c.Ret() : nullptr;
    if (!r) QDLOG(LOG_WARN, "avatar: could not load ", path);
    return r;
}

// ---------------------------------------------------------------------------
// Engine functions used on components
// ---------------------------------------------------------------------------
struct Api {
    bool tried, ok;
    void* clsSkinned;     // SkinnedMeshComponent
    void* clsSkeletal;    // SkeletalMeshComponent
    void* clsPoseable;    // PoseableMeshComponent
    void* clsStatic;      // StaticMeshComponent
    void* clsScene;       // SceneComponent
    void* clsActor;
    Fn setSkeletalMesh, playAnimation, getNumBones, getBoneName, getParentBone;
    Fn getSocketTransform, setActorCollision, addComponentByClass, attachToComponent;
    Fn setStaticMesh, setBoneTransform, getBoneTransform, copyPose, setRelXform;
    Fn setPlayRate, setVisibility;
    int skelMeshComp;     // ASkeletalMeshActor::SkeletalMeshComponent
    int attachParent, attachSocket;
};

inline Api& A() { static Api a = {}; return a; }

inline bool EnsureApi()
{
    Api& a = A();
    if (a.tried) return a.ok;
    a.tried = true;
    a.clsSkinned  = FindClassByName("SkinnedMeshComponent");
    a.clsSkeletal = FindClassByName("SkeletalMeshComponent");
    a.clsPoseable = FindClassByName("PoseableMeshComponent");
    a.clsStatic   = FindClassByName("StaticMeshComponent");
    a.clsScene    = FindClassByName("SceneComponent");
    a.clsActor    = FindClassByName("Actor");
    void* smActor = FindClassByName("SkeletalMeshActor");
    if (!a.clsSkinned || !a.clsSkeletal || !a.clsStatic || !a.clsScene || !a.clsActor || !smActor) {
        QDLOG(LOG_ERROR, "avatar: engine component classes not found");
        return false;
    }
    a.setSkeletalMesh.Resolve(nullptr, "SetSkeletalMesh", a.clsSkinned);
    a.playAnimation.Resolve(nullptr, "PlayAnimation", a.clsSkeletal);
    a.setPlayRate.Resolve(nullptr, "SetPlayRate", a.clsSkeletal);
    a.getNumBones.Resolve(nullptr, "GetNumBones", a.clsSkinned);
    a.getBoneName.Resolve(nullptr, "GetBoneName", a.clsSkinned);
    a.getParentBone.Resolve(nullptr, "GetParentBone", a.clsSkinned);
    a.getSocketTransform.Resolve(nullptr, "GetSocketTransform", a.clsScene);
    a.attachToComponent.Resolve(nullptr, "K2_AttachToComponent", a.clsScene);
    a.setRelXform.Resolve(nullptr, "K2_SetRelativeTransform", a.clsScene);
    a.setVisibility.Resolve(nullptr, "SetVisibility", a.clsScene);
    a.setActorCollision.Resolve(nullptr, "SetActorEnableCollision", a.clsActor);
    a.addComponentByClass.Resolve(nullptr, "AddComponentByClass", a.clsActor);
    a.setStaticMesh.Resolve(nullptr, "SetStaticMesh", a.clsStatic);
    if (a.clsPoseable) {
        a.setBoneTransform.Resolve(nullptr, "SetBoneTransformByName", a.clsPoseable);
        a.getBoneTransform.Resolve(nullptr, "GetBoneTransformByName", a.clsPoseable);
        a.copyPose.Resolve(nullptr, "CopyPoseFromSkeletalComponent", a.clsPoseable);
    }
    a.skelMeshComp  = FindPropertyOffsetIn(smActor, "SkeletalMeshComponent");
    a.attachParent  = FindPropertyOffsetIn(a.clsScene, "AttachParent");
    a.attachSocket  = FindPropertyOffsetIn(a.clsScene, "AttachSocketName");
    a.ok = a.setSkeletalMesh.fn && a.skelMeshComp >= 0 && a.getSocketTransform.fn;
    QDLOG(LOG_INFO, "avatar api: ok=", dec(a.ok ? 1 : 0), " poseable=", dec(a.clsPoseable ? 1 : 0),
          " addComponent=", dec(a.addComponentByClass.fn ? 1 : 0),
          " attach=", dec(a.attachToComponent.fn ? 1 : 0),
          " SkeletalMeshComponent@", dec(a.skelMeshComp),
          " AttachParent@", dec(a.attachParent), " AttachSocketName@", dec(a.attachSocket));
    return a.ok;
}

// FTransform (4.27, float): FQuat Rotation, FVector Translation (+pad), FVector Scale3D (+pad).
struct Xf {
    float qx, qy, qz, qw;
    float tx, ty, tz, _p0;
    float sx, sy, sz, _p1;
};

inline Xf XfIdentity()
{
    Xf x = {0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 1, 0};
    return x;
}

inline bool SetSkeletalMesh(void* comp, void* mesh)
{
    Call c(A().setSkeletalMesh);
    c.Obj("NewMesh", mesh);
    c.U8("bReinitPose", 1);
    return c.Run(comp);
}

inline bool PlayAnimation(void* comp, void* anim, bool loop)
{
    Call c(A().playAnimation);
    c.Obj("NewAnimToPlay", anim);
    c.U8("bLooping", loop ? 1 : 0);
    return c.Run(comp);
}

inline bool SetPlayRate(void* comp, float rate)
{
    Call c(A().setPlayRate);
    c.F32("Rate", rate);
    return c.Run(comp);
}

inline bool SetActorCollision(void* actor, bool on)
{
    Call c(A().setActorCollision);
    c.U8("bNewActorEnableCollision", on ? 1 : 0);
    return c.Run(actor);
}

inline bool SetVisibility(void* comp, bool vis)
{
    Call c(A().setVisibility);
    c.U8("bNewVisibility", vis ? 1 : 0);
    c.U8("bPropagateToChildren", 1);
    return c.Run(comp);
}

inline int NumBones(void* comp)
{
    Call c(A().getNumBones);
    if (!c.Run(comp) || !c.Ret()) return 0;
    return *(int32_t*)c.Ret();
}

inline QdName BoneName(void* comp, int i)
{
    Call c(A().getBoneName);
    c.I32("BoneIndex", i);
    QdName z = {0, 0};
    if (!c.Run(comp) || !c.Ret()) return z;
    return *(QdName*)c.Ret();
}

inline QdName ParentBone(void* comp, const QdName& bone)
{
    Call c(A().getParentBone);
    c.Name("BoneName", bone);
    QdName z = {0, 0};
    if (!c.Run(comp) || !c.Ret()) return z;
    return *(QdName*)c.Ret();
}

// ERelativeTransformSpace: 0 World, 1 Actor, 2 Component, 3 ParentBoneSpace.
inline bool SocketTransform(void* comp, const QdName& socket, int space, Xf* out)
{
    Call c(A().getSocketTransform);
    c.Name("InSocketName", socket);
    c.U8("TransformSpace", (uint8_t)space);
    if (!c.Run(comp) || !c.Ret()) return false;
    memcpy(out, c.Ret(), sizeof(Xf));
    return true;
}

// AActor::AddComponentByClass(Class, bManualAttachment, RelativeTransform, bDeferredFinish).
inline void* AddComponent(void* actor, void* cls, const Xf& rel)
{
    if (!cls) return nullptr;
    Call c(A().addComponentByClass);
    c.Obj("Class", cls);
    c.U8("bManualAttachment", 0);
    c.Set("RelativeTransform", &rel, sizeof(Xf));
    c.U8("bDeferredFinish", 0);
    if (!c.Run(actor) || !c.Ret()) return nullptr;
    return *(void**)c.Ret();
}

// USceneComponent::K2_AttachToComponent with SnapToTarget for all three rules.
inline bool AttachTo(void* comp, void* parent, const QdName& socket)
{
    Call c(A().attachToComponent);
    c.Obj("Parent", parent);
    c.Name("SocketName", socket);
    c.U8("LocationRule", 2);   // EAttachmentRule::SnapToTarget
    c.U8("RotationRule", 2);
    c.U8("ScaleRule", 0);      // KeepRelative: the attached mesh keeps its own scale
    c.U8("bWeldSimulatedBodies", 0);
    return c.Run(comp);
}

inline bool SetRelativeTransform(void* comp, const Xf& x)
{
    Call c(A().setRelXform);
    c.Set("NewTransform", &x, sizeof(Xf));
    c.U8("bSweep", 0);
    c.U8("bTeleport", 1);
    return c.Run(comp);
}

inline bool SetStaticMesh(void* comp, void* mesh)
{
    Call c(A().setStaticMesh);
    c.Obj("NewMesh", mesh);
    return c.Run(comp);
}

// EBoneSpaces: 0 WorldSpace, 1 ComponentSpace.
inline bool GetBoneXf(void* poseable, const QdName& bone, Xf* out)
{
    Call c(A().getBoneTransform);
    c.Name("BoneName", bone);
    c.U8("BoneSpace", 1);
    if (!c.Run(poseable) || !c.Ret()) return false;
    memcpy(out, c.Ret(), sizeof(Xf));
    return true;
}

inline bool SetBoneXf(void* poseable, const QdName& bone, const Xf& x)
{
    Call c(A().setBoneTransform);
    c.Name("BoneName", bone);
    c.Set("InTransform", &x, sizeof(Xf));
    c.U8("BoneSpace", 1);
    return c.Run(poseable);
}

inline bool CopyPose(void* poseable, void* src)
{
    Call c(A().copyPose);
    c.Obj("InComponentToCopy", src);
    return c.Run(poseable);
}

// ---------------------------------------------------------------------------
// Quaternion helpers (UE convention: FQuat(x,y,z,w), q * v rotates v by q)
// ---------------------------------------------------------------------------
struct V3 { float x, y, z; };
struct Q4 { float x, y, z, w; };

inline Q4 QMul(const Q4& a, const Q4& b)    // a * b : apply b, then a
{
    Q4 r;
    r.w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z;
    r.x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y;
    r.y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x;
    r.z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w;
    return r;
}

inline Q4 QAxisAngle(const V3& axis, float rad)
{
    float s = sinf(rad * 0.5f), c = cosf(rad * 0.5f);
    Q4 q = {axis.x * s, axis.y * s, axis.z * s, c};
    return q;
}

inline V3 QRotate(const Q4& q, const V3& v)
{
    // v' = v + 2w(u x v) + 2 u x (u x v)
    V3 u = {q.x, q.y, q.z};
    V3 t = {2 * (u.y * v.z - u.z * v.y), 2 * (u.z * v.x - u.x * v.z), 2 * (u.x * v.y - u.y * v.x)};
    V3 r = {v.x + q.w * t.x + (u.y * t.z - u.z * t.y),
            v.y + q.w * t.y + (u.z * t.x - u.x * t.z),
            v.z + q.w * t.z + (u.x * t.y - u.y * t.x)};
    return r;
}

inline V3 Sub(const V3& a, const V3& b) { V3 r = {a.x - b.x, a.y - b.y, a.z - b.z}; return r; }
inline V3 Add(const V3& a, const V3& b) { V3 r = {a.x + b.x, a.y + b.y, a.z + b.z}; return r; }
inline float Len(const V3& a) { return sqrtf(a.x * a.x + a.y * a.y + a.z * a.z); }
inline V3 Norm(const V3& a) { float l = Len(a); V3 r = {0, 0, 0}; if (l > 1e-6f) { r.x = a.x / l; r.y = a.y / l; r.z = a.z / l; } return r; }

// FRotator (degrees) -> FQuat, UE4's own formula (FRotator::Quaternion).
inline Q4 QFromRotator(float pitch, float yaw, float roll)
{
    const float d2r = 3.14159265f / 360.0f;
    float sp = sinf(pitch * d2r), cp = cosf(pitch * d2r);
    float sy = sinf(yaw * d2r), cy = cosf(yaw * d2r);
    float sr = sinf(roll * d2r), cr = cosf(roll * d2r);
    Q4 q;
    q.x =  cr * sp * sy - sr * cp * cy;
    q.y = -cr * sp * cy - sr * cp * sy;
    q.z =  cr * cp * sy - sr * sp * cy;
    q.w =  cr * cp * cy + sr * sp * sy;
    return q;
}

inline Q4 QConj(const Q4& q) { Q4 r = {-q.x, -q.y, -q.z, q.w}; return r; }

// FQuat -> FRotator (degrees), UE4's own formula (FQuat::Rotator): out = pitch, yaw, roll.
inline void QToRotator(const Q4& q, float out[3])
{
    const float r2d = 180.0f / 3.14159265f;
    float st = q.z * q.x - q.w * q.y;
    float yy = 2.0f * (q.w * q.z + q.x * q.y);
    float yx = 1.0f - 2.0f * (q.y * q.y + q.z * q.z);
    float yaw = atan2f(yy, yx) * r2d;
    float pitch, roll;
    if (st < -0.4999995f) {
        pitch = -90.0f;
        roll = -yaw - 2.0f * atan2f(q.x, q.w) * r2d;
    } else if (st > 0.4999995f) {
        pitch = 90.0f;
        roll = yaw - 2.0f * atan2f(q.x, q.w) * r2d;
    } else {
        pitch = asinf(2.0f * st) * r2d;
        roll = atan2f(-2.0f * (q.w * q.x + q.y * q.z), 1.0f - 2.0f * (q.x * q.x + q.y * q.y)) * r2d;
    }
    while (roll > 180.0f) roll -= 360.0f;
    while (roll < -180.0f) roll += 360.0f;
    out[0] = pitch; out[1] = yaw; out[2] = roll;
}

inline Q4 XfRot(const Xf& x) { Q4 q = {x.qx, x.qy, x.qz, x.qw}; return q; }
inline V3 XfPos(const Xf& x) { V3 v = {x.tx, x.ty, x.tz}; return v; }

// parent * child (UE FTransform composition: child expressed in parent's space).
inline Xf XfCompose(const Xf& parent, const Xf& child)
{
    Xf r = XfIdentity();
    Q4 q = QMul(XfRot(parent), XfRot(child));
    r.qx = q.x; r.qy = q.y; r.qz = q.z; r.qw = q.w;
    V3 t = {child.tx * parent.sx, child.ty * parent.sy, child.tz * parent.sz};
    t = Add(QRotate(XfRot(parent), t), XfPos(parent));
    r.tx = t.x; r.ty = t.y; r.tz = t.z;
    r.sx = parent.sx * child.sx; r.sy = parent.sy * child.sy; r.sz = parent.sz * child.sz;
    return r;
}

// `world` expressed in `parent`'s space -- the exact inverse of XfCompose(parent, .),
// so XfCompose(parent, XfRelativeTo(parent, world)) == world.  Needed to hang a
// component off a moving socket: the engine stores a transform relative to the socket,
// while a placement is known in the body's component space (the swing arc, R45).
inline Xf XfRelativeTo(const Xf& parent, const Xf& world)
{
    Xf r = XfIdentity();
    Q4 qi = QConj(XfRot(parent));
    Q4 q = QMul(qi, XfRot(world));
    r.qx = q.x; r.qy = q.y; r.qz = q.z; r.qw = q.w;
    V3 t = QRotate(qi, Sub(XfPos(world), XfPos(parent)));
    r.tx = parent.sx != 0.0f ? t.x / parent.sx : t.x;
    r.ty = parent.sy != 0.0f ? t.y / parent.sy : t.y;
    r.tz = parent.sz != 0.0f ? t.z / parent.sz : t.z;
    r.sx = parent.sx != 0.0f ? world.sx / parent.sx : world.sx;
    r.sy = parent.sy != 0.0f ? world.sy / parent.sy : world.sy;
    r.sz = parent.sz != 0.0f ? world.sz / parent.sz : world.sz;
    return r;
}

// ---------------------------------------------------------------------------
// The body
// ---------------------------------------------------------------------------
// A SkeletalMeshActor whose root component carries NO mesh (it is only a movable
// root); a PoseableMeshComponent child carries Player_FullChar_2026 and is posed by
// hand every frame.  The game has no locomotion animation for this skeleton (its only
// animations are cutscene shots), so the walk is procedural: hips, knees, feet, arms,
// spine, driven from the measured ground speed.
//
// Posing is forward kinematics in component space over the bones below.  Each bone
// gets a delta rotation expressed in the REST pose's axes; a child's delta is
// composed onto its parent's, and its pivot is carried by the parent's motion.
// SetBoneTransformByName re-expresses each target relative to the bone's real parent,
// so undriven children (twist bones, fingers, Weapon_L) follow on their own.
//
// The mesh's rest pose (measured): Y forward, X to the model's LEFT, Z up, feet at
// z=0, 170 cm tall.  So "swing forward" is a rotation about +X, and the poseable is
// yawed -90 deg relative to the actor so the actor's heading is the body's heading.
enum BoneId {
    B_PELVIS, B_SPINE1, B_SPINE2, B_NECK, B_HEAD,
    B_THIGH_L, B_KNEE_L, B_FOOT_L, B_THIGH_R, B_KNEE_R, B_FOOT_R,
    B_SHOULDER_L, B_ARM_L, B_FORE_L, B_HAND_L,
    B_SHOULDER_R, B_ARM_R, B_FORE_R, B_HAND_R,
    B_WEAPON_R,
    B_COUNT
};

static const char* const kBoneNames[B_COUNT] = {
    "root_x", "spine_01_x", "spine_02_x", "neck_x", "head_x",
    "thigh_stretch_l", "leg_stretch_l", "Foot_L", "thigh_stretch_r", "leg_stretch_r", "Foot_R",
    "shoulder_l", "arm_stretch_l", "forearm_stretch_l", "Hand_L",
    "shoulder_r", "arm_stretch_r", "forearm_stretch_r", "Hand_R",
    "Weapon_R",
};

// The FK parent used here (not always the skeleton's: Weapon_R hangs off Root in the
// rig, which would leave it behind when the arm moves, so it is carried by Hand_R).
static const int kBoneParent[B_COUNT] = {
    -1, B_PELVIS, B_SPINE1, B_SPINE2, B_NECK,
    B_PELVIS, B_THIGH_L, B_KNEE_L, B_PELVIS, B_THIGH_R, B_KNEE_R,
    B_SPINE2, B_SHOULDER_L, B_ARM_L, B_FORE_L,
    B_SPINE2, B_SHOULDER_R, B_ARM_R, B_FORE_R,
    B_HAND_R,
};

struct HeldMesh {
    void*   comp;            // StaticMeshComponent on the body
    void*   skel;            // SkeletalMeshComponent on the body (skeletal weapons)
    char    path[200];       // what it currently shows ("" = nothing)
    int     kind;            // 1 static, 2 skeletal
};

struct Body {
    void*  actor;
    void*  root;             // the movable root the poseable hangs off
    void*  pose;             // PoseableMeshComponent with the player mesh
    bool   rigged;           // rest pose captured
    float  zoff;             // actor Z = the reported capsule centre + zoff (proxy side)
    QdName bone[B_COUNT];
    Xf     rest[B_COUNT];    // component-space rest transforms
    float  phase;            // walk cycle, radians
    float  speed;            // smoothed ground speed, cm/s
    float  air;              // 0..1 airborne blend
    float  lastX, lastY, lastZ;
    float  vz;
    uint32_t lastMs;
    float  time;
    float  forceSpeed;       // dev: > 0 walks in place at this speed
    V3     dbgHandL;         // last FK target for Hand_L (read-back check)
    HeldMesh held[2];        // [0] right hand, [1] left hand
    // Action playback (PLAYER.action, WIRE.md 3.5): kind + 3-bit restart counter.
    uint8_t actByte;         // last byte applied
    int     actKind;         // kind being played (0 = none)
    float   actT, actDur;    // seconds into it, its length
    bool    actHold;         // the sender is still in this kind (charge wind-up)
    float   from[5];         // arm params when it started (smooth entry)
    float   cur[5];          // arm params last frame
    int     airIn;           // -1 unknown, else the sender's "airborne" flag
    float   actSlow;         // dev: > 1 plays actions this many times slower
    float   hipYaw;          // deg: legs turned toward the travel direction (strafe)
    float   gait;            // +1 stepping forward, -1 stepping backward (smoothed)
    float   wrist;           // deg about X on Hand_R: a stab points the blade forward
    float   dead;            // 0..1 collapse blend (A_DYING / A_DEAD)
    bool    deadIn;          // the sender is dying or dead
    uint32_t statusMask;     // STATUS bits shown
    void*   statusFx[16][5]; // Niagara components per bit, one per anchor bone
    // [combat-grip] The FP arms and the full-body mesh are different rigs: the weapon
    // socket sits at the same place on both hands but with a different ORIENTATION, so a
    // socket-relative transform from the first-person side lands rotated on the body.
    // `grip` is that frame difference measured once from the two rest poses
    // (inv(L_body) * L_fp); `ApplyEquip` composes it onto every held item.
    bool    gripCal;         // the FP-socket -> body-bone grip correction is measured
    uint8_t gripTries;       // bounded retries, so a hopeless calibration is not per-frame
    Xf      grip[2];         // [0] right, [1] left
    // The two weapon-bone REST transforms, captured with the rest pose.  Weapon_L is not
    // in the FK table on this branch (an undriven bone keeps its stored offset from
    // Hand_L and follows the arm), so its rest transform is captured separately: both are
    // needed to measure the grip frame difference.
    Xf      weaponRest[2];   // [0] Weapon_R, [1] Weapon_L (component space, at rest)
    bool    weaponRestOk[2];
    // [combat-grip] The body rig has no grip pose, so a held item used to sit in an open
    // palm.  These are the four-bone finger chains and their rest local transforms.
    bool    fingersReady;    // the finger chains below were resolved
    QdName  fbone[2][5][4];  // [hand][finger][segment]
    Xf      flocal[2][5][4]; // rest local transform of each segment (relative to its parent)
    uint8_t fcount[2][5];    // segments in each chain
};

// PLAYER.action kinds (WIRE.md 3.5).  0..4 are the original values; the rest name the
// move so the body can play something that reads as it.  The last three are the moves
// the weapon-specific montage sets needed (qdcombatvfx.h): a gun firing, a scythe
// sweeping, a claw/katar flurry.  The kind travels in 5 bits of the action byte, so
// they need no wire change.
enum ActKind {
    A_NONE = 0, A_ATTACK = 1, A_HURT = 2, A_DYING = 3, A_DEAD = 4,
    A_SLASH_R = 5, A_SLASH_L = 6, A_SLASH_DOWN = 7, A_THRUST = 8, A_CHARGE = 9,
    A_THROW = 10, A_USE = 11, A_SWAP = 12, A_CAST = 13,
    A_CHARGE_THRUST = 14,    // held before a stab (A_CHARGE is held before a swing)
    A_SHOOT = 15,            // a gun / crossbow / launcher fires
    A_SPIN = 16,             // scythe, flail, morning star
    A_FLURRY = 17,           // claw, katar, cestus
};

inline bool IsChargeKind(int k) { return k == A_CHARGE || k == A_CHARGE_THRUST; }

inline bool HasCI(const char* s, const char* k)
{
    for (int i = 0; s[i]; ++i) {
        int j = 0;
        for (; k[j]; ++j) {
            char a = s[i + j], b = k[j];
            if (!a) return false;
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (a != b) break;
        }
        if (!k[j]) return true;
    }
    return false;
}

// The montage that plays on the PRESS of an attack and is held for as long as the
// button is: the strike comes on release, when the next montage starts (measured:
// Std_Charge_Attack / Stab_Charge then the left hand's MeleeResponse 85 ms after a
// tap, 1.5 s after a 1.5 s hold; the rapier's AltAttack_Charge then AltAttack_Startup).
// Returns the move the release plays (A_ATTACK = a swing, A_THRUST = a stab), or 0.
inline int ChargeOpenerMove(const char* n)
{
    // cxf::ReleaseMove is this same rule with the weapon read out of the montage name.
    // The list below matched only weapons whose word appeared in it, so a charge on
    // anything else fell through to "a swing" without the sender ever noticing.
    int m = cxf::ReleaseMove(n, cxf::ShapeOfName(n));
    if (m == cxf::K_NONE) return 0;
    return m == cxf::K_THRUST ? A_THRUST : A_ATTACK;
}

// The first-person arms' montage -> kind.  -1: not an action of its own (the left
// hand's reaction during a wind-up, jump/land) -- keep whatever is playing.
// Measured names: Player_FPArms_Std_Charge_Attack_Montage (wind-up before every
// swing), Player_Slash_Right_Montage, Player_Slash_LeftDown_Montage,
// Player_FPArms_LeftHand_MeleeResponse_Montage, Player_SwapRightWeapon,
// Player_FPArms_Base_Jump_Action_Left_Montage, ..._Jump_Landing_Left_Montage.
inline int ClassifyMontage(const char* n, const char* held = nullptr)
{
    // The rule lives in qdcombatvfx.h: it reads the WEAPON out of the montage name
    // (the game names every weapon's montages after that weapon) and picks the move
    // that weapon makes.  The English-keyword list this replaced turned every
    // weapon-specific alt attack it did not recognise -- hammer, claw, lance,
    // greatsword, every gun -- into a generic right-to-left sword swing, or into
    // nothing at all.
    return cxf::ClassifyMontage(n, held);
}

inline uint8_t ActionByte(int kind, int seq) { return (uint8_t)(((seq & 7) << 5) | (kind & 0x1F)); }

// Feed the sender's action byte.  A new kind or a new counter restarts playback; "none"
// lets a running one-shot finish (a 30 Hz sample can land after the montage ended).
inline void SetAction(Body* b, uint8_t byte)
{
    int kind = byte & 0x1F;
    if (byte == b->actByte) return;
    b->actByte = byte;
    b->deadIn = (kind == A_DYING || kind == A_DEAD);
    if (kind == A_NONE || kind == A_DYING || kind == A_DEAD) { b->actHold = false; return; }
    bool fromCharge = IsChargeKind(b->actKind) && !IsChargeKind(kind);
    b->actKind = kind;
    b->actT = 0.0f;
    b->actHold = IsChargeKind(kind);
    for (int i = 0; i < 5; ++i) b->from[i] = b->cur[i];
    switch (kind) {
    case A_SLASH_DOWN: b->actDur = 0.62f; break;
    case A_THRUST:     b->actDur = 0.50f; break;
    case A_CHARGE:
    case A_CHARGE_THRUST: b->actDur = 0.25f; break;   // time to reach the held pose
    case A_THROW:      b->actDur = 0.60f; break;
    case A_USE:        b->actDur = 1.20f; break;
    case A_SWAP:       b->actDur = 0.45f; break;
    case A_CAST:       b->actDur = 0.70f; break;
    case A_HURT:       b->actDur = 0.35f; break;
    case A_ATTACK:     b->actDur = 0.90f; break;   // strike 0.2-0.52 s, as measured
    case A_SHOOT:      b->actDur = 0.36f; break;   // a shot is a snap, not a swing
    case A_SPIN:       b->actDur = 0.78f; break;   // a scythe sweep travels a long arc
    case A_FLURRY:     b->actDur = 0.30f; break;   // one strike of a multi-hit set
    default:           b->actDur = 0.52f; break;
    }
    // Released out of a held charge: the arm is already up, so only 80 ms are left
    // to reach the move's start pose and the strike follows the release at once.
    if (fromCharge) {
        float w = 0.22f * b->actDur - 0.08f;
        b->actT = w > 0.0f ? w : 0.0f;
    }
}

// One more stab of a flurry (the rapier's alt attack sends a thrust arc every
// ~150 ms): restart the strike from the start pose without a wind-up.
inline void Jab(Body* b)
{
    if (b->actKind != A_THRUST || b->actT < 0.12f) return;
    for (int i = 0; i < 5; ++i) b->from[i] = b->cur[i];
    b->actDur = 0.36f;
    b->actT = 0.22f * b->actDur - 0.04f;
}

// WHICH SWING, FROM THE SENDER'S ARC.  Ported from the wip line's R47.
//
// The body used to pick its swing row from the action counter's PARITY
// (`row = (actByte >> 5) & 1 ? 0 : 2`), which is a coin toss: the game chooses between
// `Player_Slash_Right_Montage` and `Player_Slash_LeftDown_Montage` from the direction the
// player actually swung in, and the counter says nothing about it, so half the partner's
// swings came out in the wrong direction.
//
// The sender already measures the arc's roll and already puts it on the wire: the melee-arc
// EFFECT row carries (pitch, yaw, roll) in `id`/`yaw`/`pitch`, and `SpawnMeleeArc` reads it
// to orient the Niagara system.  This is the same number, spent on the POSE: a swing that
// travels near-horizontal reads as the right-to-left variant and one that turns past a
// quarter turn reads as the high-right-to-low-left one -- the two moves the game itself
// has.  Only the generic `A_ATTACK` is refined; an explicit `A_SLASH_R/L/DOWN` is the
// sender's answer already and is left alone.
inline void SetSwingVariant(Body* b, float rollDeg)
{
    if (!b || b->actKind != A_ATTACK) return;
    if (!(rollDeg == rollDeg)) return;                 // NaN from a bad row
    float r = rollDeg;
    while (r > 180.0f) r -= 360.0f;
    while (r < -180.0f) r += 360.0f;
    int row = (fabsf(r) > 90.0f) ? A_SLASH_DOWN : A_SLASH_R;
    if (row == b->actKind) return;
    b->actKind = row;
    QDLOG(LOG_INFO, "body: swing variant from the sender's arc roll ", flt(r, 1),
          " deg -> ", (row == A_SLASH_DOWN ? "high-right to low-left" : "right to left"));
}

// `z` shifts the component along the parent's Z.  It is 0 when the parent's origin is the
// body's feet (the SkeletalMeshActor's root), and the negative capsule half-height when
// the parent is a Character's capsule, whose origin is its CENTRE.
inline bool SetRelYaw(void* comp, float yawDeg, float z = 0.0f)
{
    Xf x = XfIdentity();
    Q4 q = QFromRotator(0, yawDeg, 0);
    x.qx = q.x; x.qy = q.y; x.qz = q.z; x.qw = q.w;
    x.tz = z;
    return SetRelativeTransform(comp, x);
}

// Build the visual parts on a freshly spawned actor.  Returns false (and logs) if any
// engine call is missing; the caller destroys the actor.
//
// `root` and `poseZ` exist for the player-class proxy body (proxy.cpp, qdplayerbody.h):
// a Character's root is its capsule, whose origin is the capsule's CENTRE, so the body
// must hang half a capsule below it to stand on the ground.  With `root` null the actor
// is read as an ASkeletalMeshActor and its own SkeletalMeshComponent is the root, which
// is the proven puppet path.
inline bool BuildBody(Body* b, void* actor, void* root = nullptr, float poseZ = 0.0f)
{
    memset(b, 0, sizeof(*b));
    b->actor = actor;
    b->airIn = -1;
    Api& a = A();
    if (!EnsureApi() || !a.clsPoseable || !a.addComponentByClass.fn || !a.setBoneTransform.fn) {
        QDLOG(LOG_ERROR, "body: engine API incomplete");
        return false;
    }
    if (root) {
        b->root = root;
    } else {
        if (a.skelMeshComp < 0) { QDLOG(LOG_ERROR, "body: no SkeletalMeshComponent offset"); return false; }
        b->root = *(void**)((uint8_t*)actor + a.skelMeshComp);
    }
    if (!b->root) return false;
    SetActorCollision(actor, false);
    void* mesh = LoadAsset(kBodyMeshPath);
    if (!mesh) return false;
    b->pose = AddComponent(actor, a.clsPoseable, XfIdentity());
    if (!b->pose) { QDLOG(LOG_ERROR, "body: AddComponentByClass(PoseableMeshComponent) failed"); return false; }
    if (!SetSkeletalMesh(b->pose, mesh)) { QDLOG(LOG_ERROR, "body: SetSkeletalMesh on poseable failed"); return false; }
    SetRelYaw(b->pose, -90.0f, poseZ);
    for (int i = 0; i < B_COUNT; ++i) {
        if (!FNameFromAnsi(kBoneNames[i], &b->bone[i])) return false;
    }
    for (int h = 0; h < 2; ++h) {
        QdName sock;
        FNameFromAnsi(h == 0 ? "Weapon_R" : "Weapon_L", &sock);
        b->held[h].comp = AddComponent(actor, a.clsStatic, XfIdentity());
        if (b->held[h].comp) AttachTo(b->held[h].comp, b->pose, sock);
        b->held[h].skel = AddComponent(actor, a.clsSkeletal, XfIdentity());
        if (b->held[h].skel) AttachTo(b->held[h].skel, b->pose, sock);
    }
    QDLOG(LOG_INFO, "body: built (poseable + ", dec(b->held[0].comp ? 1 : 0), "+",
          dec(b->held[1].comp ? 1 : 0), " hand slots)");
    return true;
}

// [combat-grip] `CalibrateFingers` is defined below CaptureRest (it only needs the
// captured rest pose); forward-declare it so CaptureRest can call it.
inline bool CalibrateFingers(Body* b);

inline bool CaptureRest(Body* b)
{
    for (int i = 0; i < B_COUNT; ++i) {
        if (!GetBoneXf(b->pose, b->bone[i], &b->rest[i])) return false;
    }
    // A sanity check against the measured rig: head well above feet.
    if (b->rest[B_HEAD].tz < b->rest[B_FOOT_L].tz + 100.0f) {
        QDLOG(LOG_WARN, "body: rest pose implausible (head z=", flt(b->rest[B_HEAD].tz, 1),
              " foot z=", flt(b->rest[B_FOOT_L].tz, 1), ")");
        return false;
    }
    b->rigged = true;
    // The weapon bones' rest transforms (see Body::weaponRest).  A missing Weapon_L is
    // not fatal: the left grip then stays an identity and the off-hand item keeps the
    // transform the sender measured.
    for (int h = 0; h < 2; ++h) {
        QdName wn;
        b->weaponRestOk[h] = FNameFromAnsi(h == 0 ? "Weapon_R" : "Weapon_L", &wn) &&
                             GetBoneXf(b->pose, wn, &b->weaponRest[h]);
    }
    CalibrateFingers(b);
    QDLOG(LOG_INFO, "body: rest pose captured, hips at ", flt(b->rest[B_PELVIS].tz, 1), " cm");
    return true;
}

// [combat-grip] Measure the FP-socket -> body-weapon-bone frame difference once.
// MEASURED (wip R48): the two rigs put the weapon socket at nearly the same place on the
// hand (local offset ~ (0,-7,-2) on both) but with a different orientation, so the FP
// socket-relative transform landed rotated by ~100 deg on the body -- an axe held at the
// wrong angle while the mesh itself resolved correctly.
//   body_weapon_local = inv(L_body) * L_fp * fp_socket_local
// Both L terms are rig constants (a socket/bone relative to its own hand), so the
// calibration does not depend on the animation pose.  Needs the body's rest pose and the
// LOCAL player's first-person arms (the only place the FP rig exists).

inline bool CalibrateGrip(Body* b, void* fpArms)
{
    if (!b || !b->rigged || !b->pose || !fpArms) return false;
    static const char* const kHandName[2] = {"Hand_R", "Hand_L"};
    static const char* const kWeapName[2] = {"Weapon_R", "Weapon_L"};
    bool any = false;
    for (int h = 0; h < 2; ++h) {
        if (!b->weaponRestOk[h]) { b->grip[h] = XfIdentity(); continue; }
        QdName hn, wn;
        Xf fpHand, fpWeap;
        if (!FNameFromAnsi(kHandName[h], &hn) || !FNameFromAnsi(kWeapName[h], &wn)) return false;
        if (!SocketTransform(fpArms, hn, 2, &fpHand)) return false;
        if (!SocketTransform(fpArms, wn, 2, &fpWeap)) return false;
        // lfp: the FP socket inside the FP hand.  lb: the body weapon bone inside the
        // body hand.  Both are rig constants, so this does not depend on the pose.
        Xf lfp = XfRelativeTo(fpHand, fpWeap);
        Xf lb = XfRelativeTo(b->rest[h == 0 ? B_HAND_R : B_HAND_L], b->weaponRest[h]);
        b->grip[h] = XfRelativeTo(lb, lfp);                 // inv(L_body) * L_fp
        any = true;
    }
    b->gripCal = any;
    if (any) QDLOG(LOG_INFO, "body: grip calibrated (FP socket -> body weapon bone)");
    return any;
}

// [combat-grip] Resolve the body's four-bone finger chains and their rest local
// transforms.  Called once with the hand at rest; Animate() then curls them when that
// hand holds an item.  MEASURED (wip R48): 38 bones across both hands.
inline bool CalibrateFingers(Body* b)
{
    if (!b || !b->rigged || !b->pose) return false;
    static const char* const kChain[5][4] = {
        {"thumb1", "thumb2", "thumb3", ""},
        {"index1_base", "index1", "index2", "index3"},
        {"middle1_base", "middle1", "middle2", "middle3"},
        {"ring1_base", "ring1", "ring2", "ring3"},
        {"pinky1_base", "pinky1", "pinky2", "pinky3"},
    };
    static const int kHand[2] = {B_HAND_R, B_HAND_L};
    int total = 0;
    for (int h = 0; h < 2; ++h) {
        const char* suffix = h == 0 ? "_r" : "_l";
        for (int c = 0; c < 5; ++c) {
            Xf prev = b->rest[kHand[h]];
            int n = 0;
            for (int k = 0; k < 4 && kChain[c][k][0]; ++k) {
                char name[64];
                wsprintfA(name, "%s%s", kChain[c][k], suffix);
                QdName fn;
                Xf cur;
                if (!FNameFromAnsi(name, &fn) || !GetBoneXf(b->pose, fn, &cur)) { n = 0; break; }
                b->fbone[h][c][k] = fn;
                b->flocal[h][c][k] = XfRelativeTo(prev, cur);
                prev = cur;
                ++n;
            }
            b->fcount[h][c] = (uint8_t)n;
            total += n;
        }
    }
    b->fingersReady = total > 0;
    QDLOG(LOG_INFO, "body: finger chains calibrated (", dec(total), " bones)");
    return b->fingersReady;
}

// Show `path` in hand h with `rel` relative to the hand's weapon bone: a static mesh
// (kind 1) or a skeletal mesh (kind 2, shown in its reference pose).  "" clears the
// hand.  Loads on demand; a failed load leaves the hand empty.
inline void SetHeld(Body* b, int h, const char* path, const Xf& rel, int kind = 1)
{
    HeldMesh& m = b->held[h];
    if (!m.comp) return;
    if (kind != 2) kind = 1;
    void* use = kind == 2 ? m.skel : m.comp;
    bool same = m.kind == kind || !path[0];
    for (int i = 0; same && i < (int)sizeof(m.path); ++i) {
        if (m.path[i] != path[i]) { same = false; break; }
        if (!path[i]) break;
    }
    if (same && path[0]) { if (use) SetRelativeTransform(use, rel); return; }
    if (same) return;
    int i = 0;
    for (; path[i] && i < (int)sizeof(m.path) - 1; ++i) m.path[i] = path[i];
    m.path[i] = 0;
    m.kind = kind;
    void* mesh = path[0] ? LoadAsset(path) : nullptr;
    // Clear the other kind's slot so a static <-> skeletal swap never shows both.
    SetStaticMesh(m.comp, kind == 1 ? mesh : nullptr);
    if (m.skel) SetSkeletalMesh(m.skel, kind == 2 ? mesh : nullptr);
    if (use) SetRelativeTransform(use, rel);
    QDLOG(LOG_INFO, "body: hand ", (h == 0 ? "R" : "L"), " now holds ", (path[0] ? path : "nothing"),
          kind == 2 ? " (skeletal)" : "", (path[0] && !mesh ? " (LOAD FAILED)" : ""),
          (kind == 2 && !m.skel ? " (NO SKELETAL SLOT)" : ""));
}

// Advance the walk cycle from the body's own movement and pose the bones.
inline void Animate(Body* b, float x, float y, float z, float yawDeg, uint32_t nowMs)
{
    if (!b->pose) return;
    if (!b->rigged && !CaptureRest(b)) return;
    float dt = b->lastMs ? (float)(nowMs - b->lastMs) * 0.001f : 0.0f;
    b->lastMs = nowMs;
    if (dt <= 0.0f || dt > 0.5f) { b->lastX = x; b->lastY = y; b->lastZ = z; return; }
    float dx = x - b->lastX, dy = y - b->lastY, dz = z - b->lastZ;
    b->lastX = x; b->lastY = y; b->lastZ = z;
    float inst = sqrtf(dx * dx + dy * dy) / dt;
    if (inst > 2000.0f) inst = 0.0f;                      // a teleport, not a stride
    float k = dt * 8.0f; if (k > 1.0f) k = 1.0f;
    if (b->forceSpeed > 0.0f) inst = b->forceSpeed;
    b->speed += (inst - b->speed) * k;
    b->vz += (dz / dt - b->vz) * k;
    // The sender's own "not on the ground" flag when it has one: inferring it from
    // vertical speed also fired on stairs and slopes.
    float airT = b->airIn >= 0 ? (float)b->airIn : ((fabsf(b->vz) > 120.0f) ? 1.0f : 0.0f);
    b->air += (airT - b->air) * (dt * 10.0f > 1.0f ? 1.0f : dt * 10.0f);
    // Death: fall onto the back over ~0.6 s and stay down until the sender respawns.
    {
        float kd = dt * (b->deadIn ? 2.5f : 4.0f); if (kd > 1.0f) kd = 1.0f;
        b->dead += ((b->deadIn ? 1.0f : 0.0f) - b->dead) * kd;
        if (b->dead < 0.001f) b->dead = 0.0f;
    }
    b->time += dt;
    if (b->gait == 0.0f) b->gait = 1.0f;
    // Travel direction relative to the heading (UE: X forward, Y right, yaw turns
    // toward +Y).  Strafing turns the legs toward the travel direction (up to 60 deg,
    // the chest keeps facing the view); moving backward steps backward instead of
    // turning the hips around.
    {
        float hs = sqrtf(dx * dx + dy * dy);
        if (hs / dt > 40.0f && b->forceSpeed <= 0.0f) {
            float yr = yawDeg * 3.14159265f / 180.0f;
            float fwd = dx * cosf(yr) + dy * sinf(yr);
            float right = -dx * sinf(yr) + dy * cosf(yr);
            float ang = atan2f(right, fwd) * 57.2957795f;           // -180..180, + = right
            float wantGait = 1.0f, wantHip = ang;
            if (ang > 100.0f)       { wantGait = -1.0f; wantHip = ang - 180.0f; }
            else if (ang < -100.0f) { wantGait = -1.0f; wantHip = ang + 180.0f; }
            if (wantHip > 60.0f) wantHip = 60.0f;
            if (wantHip < -60.0f) wantHip = -60.0f;
            float kh = dt * 8.0f; if (kh > 1.0f) kh = 1.0f;
            b->hipYaw += (wantHip - b->hipYaw) * kh;
            b->gait += (wantGait - b->gait) * kh;
        } else {
            float kh = dt * 4.0f; if (kh > 1.0f) kh = 1.0f;
            b->hipYaw += (0.0f - b->hipYaw) * kh;
            b->gait += (1.0f - b->gait) * kh;
        }
    }

    const float PI = 3.14159265f, D = PI / 180.0f;
    float s = b->speed / 450.0f; if (s > 1.0f) s = 1.0f;  // 0 idle .. 1 full run
    if (b->speed < 8.0f) s = 0.0f;
    // One full cycle covers two steps; ~160 cm per cycle at a walk, longer at a run.
    float stride = 160.0f + 120.0f * s;
    b->phase += 2.0f * PI * (b->speed / stride) * dt * (b->gait < 0.0f ? -1.0f : 1.0f);
    if (b->phase > 2.0f * PI) b->phase -= 2.0f * PI;
    if (b->phase < 0.0f) b->phase += 2.0f * PI;
    float ph = b->phase;
    float g = 1.0f - b->air;                              // ground weight

    V3 X = {1, 0, 0}, Y = {0, 1, 0}, Z = {0, 0, 1};
    Q4 d[B_COUNT];
    for (int i = 0; i < B_COUNT; ++i) { d[i].x = d[i].y = d[i].z = 0; d[i].w = 1; }

    float hip = (32.0f * s) * D * g, knee = (55.0f * s + 5.0f) * D;
    float sl = sinf(ph), cl = cosf(ph), sr = -sl, cr = -cl;
    float kl = -(knee * (cl > 0 ? cl : 0) + 6.0f * D) * g - 72.0f * D * b->air;
    float kr = -(knee * (cr > 0 ? cr : 0) + 6.0f * D) * g - 40.0f * D * b->air;
    float tl = hip * sl + 42.0f * D * b->air, tr = hip * sr + 12.0f * D * b->air;
    d[B_THIGH_L] = QAxisAngle(X, tl);
    d[B_KNEE_L]  = QAxisAngle(X, kl);
    d[B_FOOT_L]  = QAxisAngle(X, -(tl + kl) * 0.7f);
    d[B_THIGH_R] = QAxisAngle(X, tr);
    d[B_KNEE_R]  = QAxisAngle(X, kr);
    d[B_FOOT_R]  = QAxisAngle(X, -(tr + kr) * 0.7f);

    // Arms.  Pose params per arm: [0] pitch (about X: + raises the arm forward),
    // [1] yaw (about Z: + swings it toward the body's RIGHT, since +Z carries +Y toward
    // -X and +X is the model's left), [2] abduction (about Y: brings the rest A-pose in
    // to the sides), [3] forearm bend, [4] chest twist the arm asks for.
    bool armed = b->held[0].path[0] != 0;
    float armDeg = 28.0f * s * g;
    float breathe = sinf(b->time * 1.7f) * 1.2f * D;
    float airArm = 22.0f * b->air;                        // arms come up off the ground
    float R[5], Lp[5];
    if (armed) { R[0] = 18.0f + 0.3f * armDeg * sl; R[2] = -26.0f; R[3] = 62.0f; }
    else       { R[0] = 4.0f + armDeg * sl;         R[2] = -32.0f; R[3] = 12.0f + 18.0f * s; }
    R[0] += airArm; R[1] = 0.0f; R[4] = 0.0f;
    Lp[0] = 4.0f - armDeg * sl + airArm; Lp[1] = 0.0f; Lp[2] = 32.0f; Lp[3] = 12.0f + 18.0f * s; Lp[4] = 0.0f;
    // A two-handed weapon is held by BOTH hands, so the free left hand comes across
    // to the shaft instead of hanging at the side (a partner carrying a greatsword
    // otherwise looks like it is held by one hand and floating).  Only when the left
    // hand holds nothing: an off-hand dagger or shield keeps its own pose, and it is
    // the item, not the weapon's shape, that decides that.
    if (armed && !b->held[1].path[0] &&
        cxf::ShapeTwoHanded(cxf::ShapeOfName(b->held[0].path), b->held[0].path)) {
        Lp[0] = 52.0f + airArm; Lp[1] = 18.0f; Lp[2] = 6.0f; Lp[3] = 72.0f; Lp[4] = -8.0f;
    }

    // Action overlay: wind from where the arm was to the move's start pose, swing to
    // its end pose, recover to the normal pose.
    float leanFwd = 0.0f;
    if (b->actKind != A_NONE) {
        b->actT += b->actSlow > 1.0f ? dt / b->actSlow : dt;
        static const float K[11][2][5] = {
            //            start {pitch, yaw, abd, fore, twist}      end
            /*SLASH_R*/ {{ 95,  75, -10,  45,  28}, { 80, -55, -10,  5, -22}},
            /*SLASH_L*/ {{ 95, -45, -10,  75, -22}, { 85,  70, -10,  5,  22}},
            /*DOWN   */ {{165,  25, -10,  55,  15}, { 35, -30, -10,  5, -15}},
            /*THRUST */ {{ 70,  12, -10, 100,  18}, { 92,   0, -10,  0, -12}},
            /*CHARGE */ {{120,  70, -10,  65,  32}, {120,  70, -10, 65,  32}},
            /*SWAP   */ {{-10,  15, -20,  25,   0}, { 18,   0, -26, 62,   0}},
            /*HURT   */ {{ 30,  20, -35,  70, -10}, { 18,   0, -26, 62,   0}},
            /*CHG_THR*/ {{ 62,  14, -12, 118,  24}, { 62,  14, -12, 118, 24}},
            /*SHOOT  */ {{ 86,   6,  -8,  22,   4}, { 78,   2,  -8,  26,  -4}},
            /*SPIN   */ {{ 78, -72, -14,  34, -32}, { 74,  78, -14,  10,  30}},
            /*FLURRY */ {{102,  58, -12,  64,  26}, { 72, -42, -12,  16, -20}},
        };
        int row = -1;
        switch (b->actKind) {
        // The game alternates right-to-left and high-right-to-low-left per attack
        // (Slash_Right / Slash_LeftDown); the counter's parity does the same.
        case A_ATTACK: row = ((b->actByte >> 5) & 1) ? 0 : 2; break;
        case A_SLASH_R: row = 0; break;
        case A_SLASH_L: row = 1; break;
        case A_SLASH_DOWN: row = 2; break;
        case A_THRUST: row = 3; break;
        case A_CHARGE: row = 4; break;
        case A_SWAP: row = 5; break;
        case A_HURT: row = 6; break;
        case A_CHARGE_THRUST: row = 7; break;
        case A_SHOOT: row = 8; break;
        case A_SPIN: row = 9; break;
        case A_FLURRY: row = 10; break;
        default: break;                  // throw / use / cast lead with the left hand
        }
        float u = b->actT / b->actDur;
        bool done = false;
        if (row >= 0) {
            const float* a = K[row][0];
            const float* e = K[row][1];
            float P[5];
            if (IsChargeKind(b->actKind)) {
                float k2 = u > 1.0f ? 1.0f : u;
                k2 = k2 * k2 * (3 - 2 * k2);
                for (int i = 0; i < 5; ++i) P[i] = b->from[i] + (a[i] - b->from[i]) * k2;
                if (!b->actHold && u > 1.0f) {   // released without a swing: settle back
                    float r = (b->actT - b->actDur) / 0.4f;
                    if (r >= 1.0f) { r = 1.0f; done = true; }
                    for (int i = 0; i < 5; ++i) P[i] = a[i] + (R[i] - a[i]) * r;
                }
            } else if (u < 0.22f) {
                float k2 = u / 0.22f; k2 = k2 * k2 * (3 - 2 * k2);
                for (int i = 0; i < 5; ++i) P[i] = b->from[i] + (a[i] - b->from[i]) * k2;
            } else if (u < 0.58f) {
                float k2 = (u - 0.22f) / 0.36f; k2 = k2 * k2 * (3 - 2 * k2);
                for (int i = 0; i < 5; ++i) P[i] = a[i] + (e[i] - a[i]) * k2;
                leanFwd = 8.0f * sinf(k2 * PI);
            } else {
                float k2 = (u - 0.58f) / 0.42f;
                if (k2 >= 1.0f) { k2 = 1.0f; done = true; }
                k2 = k2 * k2 * (3 - 2 * k2);
                for (int i = 0; i < 5; ++i) P[i] = e[i] + (R[i] - e[i]) * k2;
            }
            for (int i = 0; i < 5; ++i) R[i] = P[i];
        } else {
            // Left hand: throw = back-and-up then forward; use = to the mouth and hold;
            // cast = both hands pushed forward.
            float w = u < 0.25f ? u / 0.25f : (u > 0.75f ? (1.0f - u) / 0.25f : 1.0f);
            if (w <= 0.0f) { w = 0.0f; done = true; }
            float tl[5];
            if (b->actKind == A_THROW) {
                float k2 = u < 0.35f ? 0.0f : (u - 0.35f) / 0.25f;
                if (k2 > 1.0f) k2 = 1.0f;
                tl[0] = 150.0f - 70.0f * k2; tl[1] = -30.0f + 40.0f * k2; tl[2] = 10.0f;
                tl[3] = 85.0f * (1.0f - k2); tl[4] = -20.0f + 35.0f * k2;
            } else if (b->actKind == A_USE) {
                tl[0] = 70.0f; tl[1] = 35.0f; tl[2] = 10.0f; tl[3] = 130.0f; tl[4] = 0.0f;
            } else {
                tl[0] = 88.0f; tl[1] = 10.0f; tl[2] = 5.0f; tl[3] = 5.0f; tl[4] = 0.0f;
                const float tr[5] = {88.0f, -10.0f, -5.0f, 5.0f, 0.0f};
                for (int i = 0; i < 5; ++i) R[i] += (tr[i] - R[i]) * w;
            }
            for (int i = 0; i < 5; ++i) Lp[i] += (tl[i] - Lp[i]) * w;
        }
        if (done && !b->actHold) b->actKind = A_NONE;
    }
    for (int i = 0; i < 5; ++i) b->cur[i] = R[i];
    // A stab (and its held charge) points the blade at the target; the held grip
    // otherwise keeps it up along the forearm (measured on screen: an upright rapier
    // during the thrust).  +X carries the blade's up toward the back, so forward is -X.
    {
        bool stab = (b->actKind == A_THRUST || b->actKind == A_CHARGE_THRUST);
        float target = stab ? -78.0f : 0.0f;
        float k = dt * 14.0f; if (k > 1.0f) k = 1.0f;
        b->wrist += (target - b->wrist) * k;
        d[B_HAND_R] = QAxisAngle(X, b->wrist * D);
    }

    d[B_ARM_L] = QMul(QAxisAngle(Z, Lp[1] * D),
                      QMul(QAxisAngle(X, Lp[0] * D), QAxisAngle(Y, Lp[2] * D + breathe)));
    d[B_FORE_L] = QAxisAngle(X, Lp[3] * D);
    d[B_ARM_R] = QMul(QAxisAngle(Z, R[1] * D),
                      QMul(QAxisAngle(X, R[0] * D), QAxisAngle(Y, R[2] * D - breathe)));
    d[B_FORE_R] = QAxisAngle(X, R[3] * D);
    // Torso: lean into the run, counter-rotate against the hips, breathe, and turn
    // the chest with a swing.
    // Sign: a +X rotation carries the spine's +Z (up) toward -Y, i.e. BACKWARD (the
    // first version leaned the runner back -- "awkward and unnatural" in review), so
    // a forward lean is a NEGATIVE angle.  The legs use +X for "forward" because they
    // point down (-Z), which +X carries toward +Y.
    float twist = (R[4] + Lp[4]) * D;
    // The pelvis carries the legs toward the travel direction; spine_01 takes the
    // same angle back so the chest still faces the view.  Leaning back a little when
    // stepping backward reads better than leaning into it.
    float hipT = b->hipYaw * D * (s * 4.0f > 1.0f ? 1.0f : s * 4.0f);
    float lean = b->gait < 0.0f ? (2.0f - 5.0f * s) : (2.0f + 7.0f * s);
    d[B_PELVIS] = QAxisAngle(Z, 6.0f * s * sl * D * g + hipT);
    d[B_SPINE1] = QMul(QAxisAngle(Z, -hipT),
                       QMul(QAxisAngle(X, -(lean + leanFwd) * D),
                            QAxisAngle(Z, -10.0f * s * sl * D * g + 0.5f * twist)));
    d[B_SPINE2] = QMul(QAxisAngle(Z, 0.5f * twist), QAxisAngle(X, breathe));

    // Forward kinematics in component space.
    Q4 acc[B_COUNT];
    V3 pos[B_COUNT];
    float bob = (-2.5f * s * fabsf(cl)) * g;              // hips dip at each footfall
    if (b->dead > 0.0f) {
        // Ease-in so it reads as a fall, not a hinge.  The root tips backward (+X) about
        // the hips and the hips come down to lying height; legs end up along the ground.
        float k = b->dead * b->dead;
        d[B_PELVIS] = QMul(QAxisAngle(X, 82.0f * D * k), d[B_PELVIS]);
        bob += -(b->rest[B_PELVIS].tz - 16.0f) * k;
        d[B_KNEE_L] = QMul(d[B_KNEE_L], QAxisAngle(X, -25.0f * D * k));
        d[B_KNEE_R] = QMul(d[B_KNEE_R], QAxisAngle(X, -10.0f * D * k));
        d[B_ARM_L] = QMul(QAxisAngle(Y, 35.0f * D * k), d[B_ARM_L]);
        d[B_ARM_R] = QMul(QAxisAngle(Y, -35.0f * D * k), d[B_ARM_R]);
    }
    for (int i = 0; i < B_COUNT; ++i) {
        int p = kBoneParent[i];
        V3 rp = XfPos(b->rest[i]);
        if (p < 0) {
            acc[i] = d[i];
            pos[i] = rp;
            pos[i].z += bob;
        } else {
            acc[i] = QMul(acc[p], d[i]);
            pos[i] = Add(pos[p], QRotate(acc[p], Sub(rp, XfPos(b->rest[p]))));
        }
        Xf t = b->rest[i];
        Q4 q = QMul(acc[i], XfRot(b->rest[i]));
        t.qx = q.x; t.qy = q.y; t.qz = q.z; t.qw = q.w;
        t.tx = pos[i].x; t.ty = pos[i].y; t.tz = pos[i].z;
        // Neck and head carry no delta of their own; they only need re-placing when
        // the spine moves, which SetBoneTransform's parent-relative storage does.
        if (i == B_NECK || i == B_HEAD || i == B_SHOULDER_L || i == B_SHOULDER_R ||
            i == B_HAND_L || (i == B_HAND_R && fabsf(b->wrist) < 0.5f))
            continue;
        SetBoneXf(b->pose, b->bone[i], t);
    }
    // [combat-grip] A hand that holds something closes its fingers around it.  The body
    // rig has no grip pose, so without this a weapon sits in an open palm.  Curl each
    // four-bone chain about the bone's local X; an empty hand is left in the rest pose.
    if (b->fingersReady) {
        static const float kCurl[5][4] = {
            {30.0f, 25.0f, 20.0f, 0.0f},     // thumb
            {20.0f, 45.0f, 50.0f, 35.0f},    // index
            {20.0f, 45.0f, 50.0f, 35.0f},    // middle
            {20.0f, 45.0f, 50.0f, 35.0f},    // ring
            {20.0f, 45.0f, 50.0f, 35.0f},    // pinky
        };
        static const int kHandBone[2] = {B_HAND_R, B_HAND_L};
        for (int h = 0; h < 2; ++h) {
            if (!b->held[h].path[0]) continue;
            Xf hand = b->rest[kHandBone[h]];
            Q4 hq = QMul(acc[kHandBone[h]], XfRot(b->rest[kHandBone[h]]));
            hand.qx = hq.x; hand.qy = hq.y; hand.qz = hq.z; hand.qw = hq.w;
            hand.tx = pos[kHandBone[h]].x; hand.ty = pos[kHandBone[h]].y; hand.tz = pos[kHandBone[h]].z;
            for (int c = 0; c < 5; ++c) {
                Xf cur = hand;
                for (int k = 0; k < b->fcount[h][c]; ++k) {
                    Xf curl = XfIdentity();
                    Q4 q = QAxisAngle(X, kCurl[c][k] * D);
                    curl.qx = q.x; curl.qy = q.y; curl.qz = q.z; curl.qw = q.w;
                    cur = XfCompose(cur, XfCompose(b->flocal[h][c][k], curl));
                    SetBoneXf(b->pose, b->fbone[h][c][k], cur);
                }
            }
        }
    }
    b->dbgHandL = pos[B_HAND_L];
}

} // namespace avatar
} // namespace qd
