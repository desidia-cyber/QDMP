// qdhazard.h -- what a spawned HAZARD is, and which machine hurts which player with it.
//
// THE GAP THIS CLOSES (COVERAGE-MATRIX C11, GAP 5)
// -----------------------------------------------
// An enemy or a boss SPAWNS its damaging fields at runtime: `DemonBoss_FightController_C`
// spawns despawnable magma puddles along a spline and applies lava damage, the Queen and
// the Witch drop `*_AOE` volumes, the dragons leave fire / poison / sticky-binding puddles,
// and the enemy roster fires ~120 projectile classes.  Every one of them exists on the
// machine that SPAWNED it and on no other, so the partner stands in visible-or-invisible
// lava and is either burned or untouched depending on which machine happened to run the
// boss.  `DamagingCollision_C` and the `*_TrapDecoy_C` families are PLACED, so both
// machines already hold them from the level and they are not this lane's business.
//
// THE RULE, AND WHY IT CANNOT DOUBLE-DAMAGE
// -----------------------------------------
// A player's HP is owned by exactly one machine: the one that machine's player is.
// Therefore every hazard's damage to a player must be applied by THAT PLAYER'S machine,
// and never by the machine that runs the hazard:
//
//   * the machine that runs the hazard applies its own game's damage to its own player --
//     that is the game doing its job, and this module does not touch it;
//   * the OTHER machine receives the hazard (a visual copy plus its geometry) and applies
//     the damage to ITS OWN player when that player stands inside it;
//   * nothing else ever applies it, and the mirrored copy has collision and actor tick
//     turned off, so the game on the receiving side cannot apply it a second time.
//
// An enemy PROJECTILE is the same thing with a smaller radius and a single hit: the
// receiving machine's visual copy of the projectile (the EFFECT lane's, already proven)
// is tested against its own player and applies once.  Its damage amount is a property of
// its CLASS, so it travels once per class instead of once per shot.
//
// WHY THE RULES ARE PURE
// ----------------------
// Every decision here is arithmetic over names, positions and clocks, and every one of
// them fails in a way that reads as something else in a game run: a wrong radius makes the
// partner invulnerable or burns them across the room, a wrong de-duplication burns them
// once per frame, and a wrong class rule turns a boss's own projectile visual into a source
// of damage.  `test_hazard.cpp` pins each to a number, which is the only evidence available
// on one machine.
//
// THE CONSTANTS, AND WHY THEY ARE THESE
// -------------------------------------
//   kFallbackRadiusCm   300   A hazard whose volume cannot be read (a class with no shape
//                             component and no radius property) is treated as 3 m: large
//                             enough that the magma a player is visibly standing in is
//                             still felt, small enough that it cannot cover a room.  The
//                             row carries kHazardGuessed so the log says so.
//   kFallbackAmountCenti 500  = 5 damage points per tick, deliberately small: an
//                             unresolvable attacker must not be able to one-shot anyone.
//   kDefaultTickMs      500   The typical damage-over-tick cadence of this content.  A
//                             guessed interval is reported, never silent.
//   kMinTickMs          100   A hazard re-applied faster than this is a per-frame burn.
//   kMaxTickMs         5000   Slower than this is a hazard that never hurts.
//   kRangePadCm          60   The victim is a capsule ~48 cm across; a hazard edge is
//                             judged against its centre, so the pad is the capsule's
//                             radius plus the ~10 cm a 100 ms rendered position can lag.
//   kProjRadiusCm        70   A projectile's own body.  Its collision capsule is smaller,
//                             but the copy is drawn 100 ms behind a 3000 cm/s bolt (300 cm),
//                             so the test is against the owner's reported position and the
//                             radius stays honest rather than compensating for the lag.
//   kMaxPerSec           8   Damage applications sent per rolling second, all hazards and
//                             projectiles together.  A burning floor ticks a few times a
//                             second; this is the ceiling a pathological fight cannot pass.
//   kStaleMs           2000   A field with no row for this long is gone (the owner sends at
//                             5 Hz, so this is 10 missed rows).
//   kRowMs              200   5 Hz.  Hazards do not move fast; `DemonBoss_FightController_C`
//                             walks its spline with the player.
//   kMaxFields          64   Live hazards on one machine.  A boss fight drops a few dozen.
//   kMaxClassDmg        32   Projectile classes with a resolved damage amount.
#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>

namespace qd {
namespace hazard {

// ---------------------------------------------------------------------------
// What a spawned actor is
// ---------------------------------------------------------------------------
enum Kind : uint8_t {
    HZ_NONE   = 0,   // not a hazard: an enemy, a pickup, a decoration
    HZ_FIELD  = 1,   // a damaging volume that stays where it is (puddle, AoE, trap)
    HZ_PROJ   = 2,   // a moving projectile; the EFFECT lane already draws it
};

inline bool HasCI(const char* s, const char* k)
{
    if (!s) return false;
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

// A hazard's volume word.  Every one of these is a word the SHIPPED assets use
// (`DemonSpider_MagmaPuddle`, `ROS_Dragon_PoisonPuddle`, `Manticore_Disease_Puddle`,
// `FirePuddle`, `Queen_AOE`, `Queen_P1_AOE`, `Witch_AOE`, `Boner_Witch_AOE`,
// `Voxord_AOE`, `SuperCube_AOE`, `Zweihander_AOE`, `Orbiter_AOE`, `Balooner_Aoe`,
// `LargeRobotAOEEdges`, `SpectralSword_FirePuddle_BP`).
static const char* const kFieldWords[] = {
    "puddle", "magma", "lava", "aoe", "hazard", "damaging",
    "firefield", "firezone", "flamewall", "spikefield", "acidpool",
    "poisonpool", "poisoncloud", "gascloud", "damagezone", "damagefield",
};

// A word that means the actor is NOT a damaging volume even when it carries one of the
// field words: a montage, a material, a Niagara cue, a decal, a decoration.  Actors only
// reach this test (the spawn ring is fed by ReceiveBeginPlay), but this content does place
// actors whose names carry `_Mat`, `_Deco` and `_Cue`, and a decorative actor must not
// become a source of damage.
//
// An `*_FX` actor carrying a field word is deliberately NOT rejected here: a dragon's fire
// puddle is as likely to be named `..._FirePuddle_FX` as the volume itself, and refusing it
// would make the partner stand in lava unhurt.  The coincident-application rule below
// (kDedupCm) is what stops a decorative copy of the same hazard from being a second burn.
static const char* const kNotHazardWords[] = {
    "_montage", "montage", "_mat", "material", "texture", "_cue", "soundwave",
    "decal", "deco", "billboard", "lightbeam",
};

inline bool NotHazardName(const char* n)
{
    if (!n || !n[0]) return true;
    for (int i = 0; i < (int)(sizeof(kNotHazardWords) / sizeof(kNotHazardWords[0])); ++i)
        if (HasCI(n, kNotHazardWords[i])) return true;
    return false;
}

inline bool FieldName(const char* n)
{
    if (NotHazardName(n)) return false;
    for (int i = 0; i < (int)(sizeof(kFieldWords) / sizeof(kFieldWords[0])); ++i)
        if (HasCI(n, kFieldWords[i])) return true;
    return false;
}

// The projectile rule is the combat-vfx one (`cxf::LooksLikeProjectile`) with the engine's
// own movement component as the primary signal, because eleven of this content's projectile
// classes derive from `Actor` and share no other ancestor.
inline bool ProjName(const char* n)
{
    if (NotHazardName(n)) return false;
    return HasCI(n, "projectile") || HasCI(n, "travellingwave") ||
           HasCI(n, "travelwave") || HasCI(n, "arcprojectile") ||
           HasCI(n, "groundwave") || HasCI(n, "ground_wave") ||
           HasCI(n, "bolt") || HasCI(n, "arrow");
}

// `movement` = the actor carries a ProjectileMovementComponent (the engine's own answer).
// `from_enemy` = an enemy (or the enemy's controller) owns or instigated the spawn.  A
// field spawned by nobody is somebody's decorative actor, and the conservative answer is
// "not a hazard": a wrong yes puts damage on a wall.
inline uint8_t KindOf(const char* className, bool movement, bool from_enemy)
{
    if (!from_enemy) return HZ_NONE;
    if (movement) return HZ_PROJ;
    if (ProjName(className)) return HZ_PROJ;
    if (FieldName(className)) return HZ_FIELD;
    return HZ_NONE;
}

// ---------------------------------------------------------------------------
// Identity
// ---------------------------------------------------------------------------
// A spawned hazard has no GUID and no cooked path, and the two machines see it with the
// same transform because they are looking at the same simulation.  The key is therefore a
// hash of (class name, quantised spawn position, ordinal among coincident spawns), which is
// the same shape the summon identity already uses (`qdenemyid.h`): positional, so the two
// machines derive it independently, with an ordinal only to separate two puddles dropped on
// exactly the same spot in the same second.
inline uint32_t Fnv1a(const char* s)
{
    uint32_t h = 2166136261u;
    if (!s) return h;
    for (int i = 0; s[i]; ++i) {
        h ^= (uint8_t)s[i];
        h *= 16777619u;
    }
    return h;
}

inline void MakeKey(uint32_t out[4], const char* className, int32_t px, int32_t py,
                    int32_t pz, uint8_t ord)
{
    // 1/8 cm already, so the position is used as-is: two machines that compute the same
    // spawn point quantise it identically.
    out[0] = Fnv1a(className);
    out[1] = (uint32_t)px ^ ((uint32_t)py << 13) ^ ((uint32_t)py >> 19);
    out[2] = (uint32_t)pz ^ ((uint32_t)ord << 28);
    out[3] = 0x48415A31u;   // 'HAZ1': the key's own domain, so it cannot collide with a GUID
}

inline bool ZeroKey(const uint32_t g[4]) { return (g[0] | g[1] | g[2] | g[3]) == 0; }

// ---------------------------------------------------------------------------
// Containment and amounts
// ---------------------------------------------------------------------------
static const float    kFallbackRadiusCm   = 300.0f;
static const int32_t  kFallbackAmountCenti = 500;
static const uint16_t kDefaultTickMs      = 500;
static const uint16_t kMinTickMs          = 100;
static const uint16_t kMaxTickMs          = 5000;
static const float    kRangePadCm         = 60.0f;
static const float    kProjRadiusCm       = 70.0f;
static const float    kDedupCm            = 100.0f;
static const int      kMaxPerSec          = 8;
static const uint32_t kRateWindowMs       = 1000;
static const uint32_t kStaleMs            = 2000;
static const uint32_t kRowMs              = 200;
static const int      kMaxFields          = 64;
static const int      kMaxClassDmg        = 32;

// Row flags.
static const uint8_t kHazardGuessed = 0x01;   // the amount and/or the radius did not resolve
static const uint8_t kHazardOnce    = 0x02;   // a projectile: one application, then spent

// TWO HAZARDS AT THE SAME PLACE ARE ONE HAZARD.
//
// A spawner commonly produces a volume AND a coincident decorative actor for the same
// effect (an `*_AOE` actor and its `*_AOE_FX` sibling), and both are owned by the enemy, so
// both classify.  Two records for one puddle would be two burns per tick.  The rule is
// purely positional and it is the same on both machines, so it cannot make the two
// disagree: two applications whose centres are within kDedupCm are the same application.
inline bool Coincident(const float a[3], const float b[3])
{
    float dx = a[0] - b[0], dy = a[1] - b[1];
    return (dx * dx + dy * dy) <= (kDedupCm * kDedupCm);
}

inline float Dist3(const float a[3], const float b[3])
{
    float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

// Is `player` inside the hazard?  A HORIZONTAL cylinder, not a sphere: a puddle is a flat
// disc on the floor and its collider is a box a few centimetres tall, so a sphere test with
// a 3 m radius would burn a player standing on a ledge above it.  The vertical tolerance is
// the hazard's own radius, which is the honest bound for "the same floor".
inline bool Inside(const float centre[3], float radiusCm, const float player[3])
{
    if (!(radiusCm > 0.0f)) return false;
    float dx = player[0] - centre[0], dy = player[1] - centre[1];
    float dz = fabsf(player[2] - centre[2]);
    if (dz > radiusCm) return false;
    float d2 = dx * dx + dy * dy;
    float r = radiusCm + kRangePadCm;
    return d2 <= r * r;
}

inline float ClampRadius(float v)
{
    if (!(v > 0.0f)) return kFallbackRadiusCm;
    if (v > 3000.0f) return 3000.0f;
    return v;
}

inline uint16_t ClampTick(float secondsOrMs)
{
    // The content declares its cadence in SECONDS (`DamageTickRate`, `TickInterval`); a
    // value up to 20 is seconds, above it is milliseconds.  Both spellings exist in this
    // game's data and getting the units wrong makes a hazard eight times too fast.
    //
    // A POSITIVE value outside the band is clamped into it rather than replaced by the
    // default: a class that declares 2 ms is a units mistake, and the floor (100 ms) is a
    // safer reading of "very fast" than a default that may not suit that content.  Only a
    // non-positive value -- the "not declared at all" state -- takes the default.
    if (!(secondsOrMs > 0.0f)) return kDefaultTickMs;
    float ms = (secondsOrMs <= 20.0f) ? secondsOrMs * 1000.0f : secondsOrMs;
    if (ms < (float)kMinTickMs) return kMinTickMs;
    if (ms > (float)kMaxTickMs) return kMaxTickMs;
    return (uint16_t)ms;
}

inline int32_t ClampAmount(float v)
{
    if (!(v > 0.0f)) return kFallbackAmountCenti;
    float c = v * 100.0f;
    if (c > 1000000.0f) c = 1000000.0f;
    return (int32_t)(c + 0.5f);
}

// ---------------------------------------------------------------------------
// De-duplication and the sustained-rate bound
// ---------------------------------------------------------------------------
// One application per (hazard, victim) per tick interval.  A FIELD burns at its own
// declared cadence; a PROJECTILE applies once and is then spent, because a bolt that
// passed a player cannot hit them again.
//
// Both are keyed on the hazard's key plus `victim` (a slot, 0 = the local player here; the
// receiver only ever has one), so a hazard that is re-sent every 200 ms still burns at its
// own rate rather than at the row rate.  That distinction is the whole difference between a
// fair fight and a shredder.
class HitGate {
public:
    HitGate() { Reset(); }

    void Reset()
    {
        for (int i = 0; i < kMaxFields; ++i) { r_[i].used = false; r_[i].spent = false; }
        for (int i = 0; i < kMaxPerSec; ++i) sent_[i] = 0;
        sentN_ = 0;
        sentHead_ = 0;
        spent_ = 0;
        repeats_ = 0;
        rateDrops_ = 0;
        overflow_ = 0;
    }

    // True when the receiver may apply damage for this hazard now.  `tickMs` is the
    // hazard's cadence; `once` marks a projectile.  `now` is a monotonic millisecond clock.
    bool Admit(const uint32_t key[4], uint8_t victim, uint16_t tickMs, bool once, uint32_t now_ms)
    {
        if (ZeroKey(key)) return false;
        Rec* r = Touch(key, victim, now_ms);
        if (!r) { ++overflow_; return false; }

        if (r->used2) {
            if (once) { ++spent_; return false; }
            uint32_t gap = (uint32_t)(now_ms - r->lastMs);
            if (gap < (uint32_t)(tickMs ? tickMs : kDefaultTickMs)) { ++repeats_; return false; }
        }
        if (InWindow(now_ms) >= kMaxPerSec) { ++rateDrops_; return false; }

        r->used2 = true;
        r->lastMs = now_ms;
        r->spent = once;
        sent_[sentHead_] = now_ms;
        sentHead_ = (sentHead_ + 1) % kMaxPerSec;
        if (sentN_ < kMaxPerSec) ++sentN_;
        return true;
    }

    uint32_t Spent()      const { return spent_; }
    uint32_t Repeats()    const { return repeats_; }
    uint32_t RateDrops()  const { return rateDrops_; }
    uint32_t Overflows()  const { return overflow_; }
    int      Count()      const { int n = 0; for (int i = 0; i < kMaxFields; ++i) n += r_[i].used ? 1 : 0; return n; }

private:
    struct Rec {
        bool     used;      // the slot holds a key
        bool     used2;     // at least one application was admitted for it
        bool     spent;     // a one-shot hazard already applied
        uint32_t key[4];
        uint8_t  victim;
        uint32_t lastMs;
        uint32_t touchMs;
    };

    int InWindow(uint32_t now_ms) const
    {
        int n = 0;
        for (int i = 0; i < sentN_; ++i)
            if ((uint32_t)(now_ms - sent_[i]) < kRateWindowMs) ++n;
        return n;
    }

    Rec* Find(const uint32_t g[4], uint8_t victim)
    {
        for (int i = 0; i < kMaxFields; ++i)
            if (r_[i].used && r_[i].victim == victim &&
                r_[i].key[0] == g[0] && r_[i].key[1] == g[1] &&
                r_[i].key[2] == g[2] && r_[i].key[3] == g[3]) return &r_[i];
        return nullptr;
    }

    Rec* Touch(const uint32_t g[4], uint8_t victim, uint32_t now_ms)
    {
        if (Rec* r = Find(g, victim)) { r->touchMs = now_ms; return r; }
        int victimSlot = -1;
        uint32_t oldest = 0;
        for (int i = 0; i < kMaxFields; ++i) {
            if (!r_[i].used) { victimSlot = i; break; }
            uint32_t age = (uint32_t)(now_ms - r_[i].touchMs);
            if (age > kStaleMs && age > oldest) { oldest = age; victimSlot = i; }
        }
        if (victimSlot < 0) return nullptr;
        Rec* r = &r_[victimSlot];
        r->used = true;
        r->used2 = false;
        r->spent = false;
        r->victim = victim;
        r->lastMs = 0;
        for (int k = 0; k < 4; ++k) r->key[k] = g[k];
        r->touchMs = now_ms;
        return r;
    }

    Rec      r_[kMaxFields];
    uint32_t sent_[kMaxPerSec];
    int      sentN_;
    int      sentHead_;
    uint32_t spent_;
    uint32_t repeats_;
    uint32_t rateDrops_;
    uint32_t overflow_;
};

// ---------------------------------------------------------------------------
// The projectile class dictionary
// ---------------------------------------------------------------------------
// A projectile's damage is a property of its CLASS (`Fireball_Projectile_BP_C` is the same
// number every time), so the sender publishes one record per class and the receiver uses it
// for every copy of that class the EFFECT lane draws.  A lookup miss is not an error: a
// class whose amount could not be resolved is simply not in the table, and the copy then
// deals nothing rather than an invented number.
class ClassDamage {
public:
    ClassDamage() { n_ = 0; }
    void Reset() { n_ = 0; }

    void Put(const char* cls, int32_t amountCenti, uint16_t tickMs, uint8_t flags)
    {
        if (!cls || !cls[0]) return;
        for (int i = 0; i < n_; ++i) {
            if (strcmp(e_[i].cls, cls) != 0) continue;
            e_[i].amount = amountCenti;
            e_[i].tick = tickMs;
            e_[i].flags = flags;
            return;
        }
        if (n_ >= kMaxClassDmg) return;   // full: the caller logs, nothing is guessed
        strncpy(e_[n_].cls, cls, sizeof(e_[n_].cls) - 1);
        e_[n_].cls[sizeof(e_[n_].cls) - 1] = 0;
        e_[n_].amount = amountCenti;
        e_[n_].tick = tickMs;
        e_[n_].flags = flags;
        ++n_;
    }

    bool Get(const char* cls, int32_t* amountCenti, uint16_t* tickMs, uint8_t* flags) const
    {
        if (!cls || !cls[0]) return false;
        for (int i = 0; i < n_; ++i) {
            if (strcmp(e_[i].cls, cls) != 0) continue;
            if (amountCenti) *amountCenti = e_[i].amount;
            if (tickMs) *tickMs = e_[i].tick;
            if (flags) *flags = e_[i].flags;
            return true;
        }
        return false;
    }

    int Count() const { return n_; }

private:
    struct Ent { char cls[200]; int32_t amount; uint16_t tick; uint8_t flags; };
    Ent e_[kMaxClassDmg];
    int n_;
};

// ---------------------------------------------------------------------------
// The row policy
// ---------------------------------------------------------------------------
// The receiver needs a field's position, radius and cadence, and it needs them often
// enough that a puddle that follows the player does not trail behind.  It does not need
// them per frame: a row is due when anything CHANGED, or every kRowMs as a keepalive (the
// staleness window is ten times that, so a lost row costs nothing).
struct FieldState {
    int32_t  px, py, pz;
    uint16_t radiusCm;
    uint16_t tickMs;
    int32_t  amountCenti;
    uint8_t  flags;
};

inline bool Changed(const FieldState& a, const FieldState& b)
{
    // 8 cm of movement, one radius step, or any amount/tick change.  A puddle that jitters
    // by a centimetre must not produce a row every tick.
    int32_t dx = a.px - b.px, dy = a.py - b.py, dz = a.pz - b.pz;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    if (dz < 0) dz = -dz;
    if (dx + dy + dz >= 64) return true;        // 64 * 1/8 cm = 8 cm
    if (a.radiusCm != b.radiusCm) return true;
    if (a.tickMs != b.tickMs) return true;
    if (a.amountCenti != b.amountCenti) return true;
    if (a.flags != b.flags) return true;
    return false;
}

inline bool RowDue(const FieldState& now, const FieldState& pub, bool havePub, uint32_t nowMs,
                   uint32_t pubMs)
{
    if (!havePub) return true;
    if (Changed(now, pub)) return true;
    return (uint32_t)(nowMs - pubMs) >= kRowMs;
}

} // namespace hazard
} // namespace qd
