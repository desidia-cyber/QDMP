// test_hazard.cpp -- what a spawned hazard is, who it hurts, and how often.
//
// WHY THESE TESTS AND NOT A LIVE RUN
// ----------------------------------
// Every rule in qdhazard.h fails in a way that reads as something else in a game:
//   * a wrong containment test makes the partner INVULNERABLE to boss lava (too narrow)
//     or burns them across the room and through a ceiling (too wide);
//   * a wrong de-duplication burns them once per frame, which a player reports as
//     "enemies hit too hard" and nobody traces back to a hazard lane;
//   * a wrong units rule (seconds read as milliseconds) makes a puddle eight times too
//     fast, and a wrong identity hash makes the receiver apply the hazard to a hazard it
//     already applied;
//   * a wrong class rule turns a boss's decorative FX actor into a source of damage.
// Each assertion below pins one of those to a number.  That is the only evidence a single
// machine can produce for arithmetic like this.
#include <stdint.h>
#include <stdio.h>

#include "qdhazard.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, name)                                                          \
    do {                                                                           \
        if (cond) { ++g_pass; }                                                    \
        else { ++g_fail; printf("FAIL %s  (%s:%d)\n", name, __FILE__, __LINE__); } \
    } while (0)

using namespace qd;

// ---------------------------------------------------------------------------
// 1. What counts as a hazard
// ---------------------------------------------------------------------------
static void TestKindOf()
{
    // The names the shipped content actually uses for enemy/boss-spawned damaging fields.
    CHECK(hazard::KindOf("DemonSpider_MagmaPuddle_C", false, true) == hazard::HZ_FIELD,
          "kind/magma puddle is a field");
    CHECK(hazard::KindOf("ROS_Dragon_PoisonPuddle_C", false, true) == hazard::HZ_FIELD,
          "kind/poison puddle is a field");
    CHECK(hazard::KindOf("Manticore_Disease_Puddle_C", false, true) == hazard::HZ_FIELD,
          "kind/disease puddle is a field");
    CHECK(hazard::KindOf("Manticore_Weaken_Puddle_C", false, true) == hazard::HZ_FIELD,
          "kind/weaken puddle is a field");
    CHECK(hazard::KindOf("FirePuddle_C", false, true) == hazard::HZ_FIELD,
          "kind/fire puddle is a field");
    CHECK(hazard::KindOf("Queen_P2_AOE_C", false, true) == hazard::HZ_FIELD,
          "kind/queen p2 aoe is a field");
    CHECK(hazard::KindOf("Boner_Witch_AOE_C", false, true) == hazard::HZ_FIELD,
          "kind/witch aoe is a field");
    CHECK(hazard::KindOf("Balooner_Aoe_C", false, true) == hazard::HZ_FIELD,
          "kind/ballooner aoe is a field (mixed case)");
    CHECK(hazard::KindOf("Voxord_AOE_C", false, true) == hazard::HZ_FIELD,
          "kind/voxord aoe is a field");

    // Enemy projectiles: the engine's own movement component is the primary signal,
    // because eleven of these classes derive from `Actor` and share no ancestor.
    CHECK(hazard::KindOf("Fireball_Projectile_BP_C", true, true) == hazard::HZ_PROJ,
          "kind/a projectile movement component is a projectile");
    CHECK(hazard::KindOf("Enemy_Projectile_Base_C", false, true) == hazard::HZ_PROJ,
          "kind/an enemy projectile BY NAME is a projectile");
    CHECK(hazard::KindOf("Queen_GroundWave_C", false, true) == hazard::HZ_PROJ,
          "kind/a ground wave is a projectile");
    CHECK(hazard::KindOf("LuudeCrossbow_Projectile_C", false, true) == hazard::HZ_PROJ,
          "kind/a crossbow bolt is a projectile");

    // NOT a hazard: nothing spawns damage from an actor nobody enemy-owns.
    CHECK(hazard::KindOf("DemonSpider_MagmaPuddle_C", false, false) == hazard::HZ_NONE,
          "kind/an unowned field is not a hazard");
    CHECK(hazard::KindOf("Fireball_Projectile_BP_C", true, false) == hazard::HZ_NONE,
          "kind/an unowned projectile is not a hazard");
    // NOT a hazard: a decorative actor or an enemy standing still.
    CHECK(hazard::KindOf("Ribber_Base_BP_C", false, true) == hazard::HZ_NONE,
          "kind/an enemy is not a hazard");
    CHECK(hazard::KindOf("Queen_P1_AOE_Montage_C", false, true) == hazard::HZ_NONE,
          "kind/a montage is not a hazard");
    CHECK(hazard::KindOf("Queen_AOE_Mat_C", false, true) == hazard::HZ_NONE,
          "kind/a material is not a hazard");
    CHECK(hazard::KindOf("Balooner_Aoe_Mat_C", false, true) == hazard::HZ_NONE,
          "kind/a mixed-case material is not a hazard");
    CHECK(hazard::KindOf("Boss_Deco_00_BP_C", false, true) == hazard::HZ_NONE,
          "kind/a decoration is not a hazard");
    CHECK(hazard::KindOf("Queen_GroundWave_Loop_Cue_C", false, true) == hazard::HZ_NONE,
          "kind/a sound cue is not a hazard");
    // A real damaging volume from the corpus: the big robot's AOE edge actor.
    CHECK(hazard::KindOf("LargeRobotAOEEdges_C", false, true) == hazard::HZ_FIELD,
          "kind/the robot's AOE edges are a field");
    // An `*_FX` actor carrying a field word IS a field: refusing it would leave the partner
    // standing in lava unhurt.  The coincident rule below is what stops a decorative copy
    // from being a second burn.
    CHECK(hazard::KindOf("Dragon_FirePuddle_FX_C", false, true) == hazard::HZ_FIELD,
          "kind/an FX actor named like a puddle is a field (deduped by position)");
    CHECK(hazard::KindOf(nullptr, false, true) == hazard::HZ_NONE,
          "kind/a null name is not a hazard");
    CHECK(hazard::KindOf("", false, true) == hazard::HZ_NONE,
          "kind/an empty name is not a hazard");
}

// ---------------------------------------------------------------------------
// 2. Identity
// ---------------------------------------------------------------------------
static void TestKey()
{
    uint32_t a[4], b[4], c[4];
    hazard::MakeKey(a, "DemonSpider_MagmaPuddle_C", 800, -1600, 240, 0);
    hazard::MakeKey(b, "DemonSpider_MagmaPuddle_C", 800, -1600, 240, 0);
    CHECK(a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3],
          "key/the same class at the same quantised point is the same key");
    CHECK(!hazard::ZeroKey(a), "key/a real key is not zero");
    CHECK(a[3] == 0x48415A31u, "key/the key carries its own domain word");

    // 1/8 cm: one centimetre apart must be a different key (the caller quantises, but a
    // hash that ignored the low bits would merge two puddles dropped a metre apart only
    // if the SHIFT was wrong -- this pins the shift).
    hazard::MakeKey(c, "DemonSpider_MagmaPuddle_C", 808, -1600, 240, 0);
    CHECK(c[1] != a[1] || c[2] != a[2], "key/a point one centimetre away is a different key");

    // Two puddles co-located in the same frame are separated by the ordinal.
    hazard::MakeKey(c, "DemonSpider_MagmaPuddle_C", 800, -1600, 240, 1);
    CHECK(c[2] != a[2], "key/a second spawn at the same point differs by its ordinal");

    // The class participates: a fire puddle and a poison puddle on the same tile differ.
    hazard::MakeKey(c, "FirePuddle_C", 800, -1600, 240, 0);
    CHECK(c[0] != a[0], "key/a different class at the same point is a different key");

    uint32_t z[4] = {0, 0, 0, 0};
    CHECK(hazard::ZeroKey(z), "key/an all-zero key is recognised");
}

// ---------------------------------------------------------------------------
// 3. Containment
// ---------------------------------------------------------------------------
static void TestInside()
{
    const float centre[3] = {1000.0f, 2000.0f, 300.0f};
    const float centreIn[3] = {1000.0f, 2000.0f, 300.0f};
    CHECK(hazard::Inside(centre, 300.0f, centreIn), "inside/the centre is inside");

    // The pad is the victim capsule's radius: a player whose CENTRE is one pad outside
    // the edge is still touching it.
    const float edgePad[3] = {1000.0f + 300.0f + hazard::kRangePadCm - 1.0f, 2000.0f, 300.0f};
    CHECK(hazard::Inside(centre, 300.0f, edgePad), "inside/inside the pad at the edge counts");

    const float outside[3] = {1000.0f + 300.0f + hazard::kRangePadCm + 2.0f, 2000.0f, 300.0f};
    CHECK(!hazard::Inside(centre, 300.0f, outside), "inside/outside the pad does not count");

    // A puddle is a flat disc: a player on a ledge more than one radius above it is not
    // standing in it.  (A sphere test with a 3 m radius would burn them.)
    const float above[3] = {1000.0f, 2000.0f, 300.0f + 300.0f + 1.0f};
    CHECK(!hazard::Inside(centre, 300.0f, above), "inside/above the hazard by more than its radius");

    const float justAbove[3] = {1000.0f, 2000.0f, 300.0f + 299.0f};
    CHECK(hazard::Inside(centre, 300.0f, justAbove), "inside/just above, within one radius, is inside");

    // A zero or negative radius is "unknown", never "everything".
    CHECK(!hazard::Inside(centre, 0.0f, centreIn), "inside/a zero radius contains nothing");
    CHECK(!hazard::Inside(centre, -50.0f, centreIn), "inside/a negative radius contains nothing");
}

// ---------------------------------------------------------------------------
// 4. Clamps, and the units trap
// ---------------------------------------------------------------------------
static void TestClamps()
{
    CHECK(hazard::ClampRadius(0.0f) == hazard::kFallbackRadiusCm, "clamp/an unread radius is the fallback");
    CHECK(hazard::ClampRadius(-1.0f) == hazard::kFallbackRadiusCm, "clamp/a negative radius is the fallback");
    CHECK(hazard::ClampRadius(450.0f) == 450.0f, "clamp/a real radius is kept");
    CHECK(hazard::ClampRadius(1.0e6f) == 3000.0f, "clamp/an absurd radius is capped at 30 m");

    // SECONDS vs MILLISECONDS.  The content declares `DamageTickRate` in seconds; reading
    // 0.5 as half a millisecond would burn once per frame.
    CHECK(hazard::ClampTick(0.5f) == 500, "tick/half a second is 500 ms");
    CHECK(hazard::ClampTick(1.0f) == 1000, "tick/one second is 1000 ms");
    CHECK(hazard::ClampTick(500.0f) == 500, "tick/500 is already milliseconds");
    CHECK(hazard::ClampTick(0.0f) == hazard::kDefaultTickMs, "tick/zero is the default");
    CHECK(hazard::ClampTick(-1.0f) == hazard::kDefaultTickMs, "tick/negative is the default");
    CHECK(hazard::ClampTick(0.001f) == hazard::kMinTickMs, "tick/below the floor is the floor");
    CHECK(hazard::ClampTick(9000.0f) == hazard::kMaxTickMs, "tick/above the ceiling is the ceiling");
    CHECK(hazard::ClampTick(2.5f) == 2500, "tick/two and a half seconds is 2500 ms");

    CHECK(hazard::ClampAmount(0.0f) == hazard::kFallbackAmountCenti, "amount/an unread amount is the fallback");
    CHECK(hazard::ClampAmount(20.0f) == 2000, "amount/20 damage is 2000 centi");
    CHECK(hazard::ClampAmount(0.5f) == 50, "amount/half a point is 50 centi");
}

// ---------------------------------------------------------------------------
// 5. One application per tick, and never two for one projectile
// ---------------------------------------------------------------------------
static void TestHitGate()
{
    uint32_t key[4];
    hazard::MakeKey(key, "DemonSpider_MagmaPuddle_C", 0, 0, 0, 0);

    {   // a FIELD burns at its own cadence, not at the row rate
        hazard::HitGate g;
        CHECK(g.Admit(key, 0, 500, false, 1000), "gate/the first application is admitted");
        CHECK(!g.Admit(key, 0, 500, false, 1200), "gate/a row 200 ms later is a repeat");
        CHECK(g.Repeats() == 1, "gate/the repeat is counted");
        CHECK(!g.Admit(key, 0, 500, false, 1499), "gate/still inside the interval");
        CHECK(g.Admit(key, 0, 500, false, 1500), "gate/the interval boundary is admitted");
        CHECK(!g.Admit(key, 0, 500, false, 1700), "gate/the next row after an application is a repeat");
    }

    {   // a PROJECTILE applies once, ever
        uint32_t pk[4];
        hazard::MakeKey(pk, "Fireball_Projectile_BP_C", 0, 0, 0, 0);
        hazard::HitGate g;
        CHECK(g.Admit(pk, 0, 100, true, 1000), "gate/a projectile applies once");
        CHECK(!g.Admit(pk, 0, 100, true, 5000), "gate/a projectile cannot apply twice");
        CHECK(g.Spent() == 1, "gate/the spent projectile is counted");
    }

    {   // two different hazards are independent
        uint32_t a[4], b[4];
        hazard::MakeKey(a, "FirePuddle_C", 0, 0, 0, 0);
        hazard::MakeKey(b, "ROS_Dragon_PoisonPuddle_C", 0, 0, 0, 0);
        hazard::HitGate g;
        CHECK(g.Admit(a, 0, 500, false, 1000), "gate/hazard A applies");
        CHECK(g.Admit(b, 0, 500, false, 1000), "gate/hazard B applies in the same millisecond");
        CHECK(!g.Admit(a, 0, 500, false, 1100), "gate/A remains on its own interval");
    }

    {   // an empty key is never a hazard
        uint32_t z[4] = {0, 0, 0, 0};
        hazard::HitGate g;
        CHECK(!g.Admit(z, 0, 500, false, 1000), "gate/an empty key cannot damage");
    }
}

// ---------------------------------------------------------------------------
// 6. The rolling-second bound
// ---------------------------------------------------------------------------
static void TestRateBound()
{
    hazard::HitGate g;
    uint32_t k[4];
    int admitted = 0;
    // Fifty DIFFERENT fields, all due at the same instant: a boss fight that drops a whole
    // ring of puddles must not send fifty applications in one frame.
    for (int i = 0; i < 50; ++i) {
        char name[32];
        name[0] = 'X'; name[1] = (char)('a' + (i % 26));
        name[2] = (char)('a' + (i / 26)); name[3] = 0;
        hazard::MakeKey(k, name, i * 8, 0, 0, 0);
        if (g.Admit(k, 0, 100, false, 1000)) ++admitted;
    }
    CHECK(admitted == hazard::kMaxPerSec, "rate/the rolling second admits exactly its cap");
    CHECK(g.RateDrops() == (uint32_t)(50 - hazard::kMaxPerSec), "rate/every refusal is counted");

    // A second later the window has moved and the cap is available again.
    CHECK(g.Admit(k, 0, 100, false, 2001), "rate/the window reopens after a second");
}

// ---------------------------------------------------------------------------
// 7. The projectile class dictionary
// ---------------------------------------------------------------------------
static void TestClassDamage()
{
    hazard::ClassDamage d;
    int32_t amt = 0;
    uint16_t tick = 0;
    uint8_t flags = 0;
    CHECK(!d.Get("Fireball_Projectile_BP_C", &amt, &tick, &flags), "classdmg/a miss reports a miss");

    d.Put("Fireball_Projectile_BP_C", 3500, 1000, 0x02);
    CHECK(d.Get("Fireball_Projectile_BP_C", &amt, &tick, &flags), "classdmg/a put is readable");
    CHECK(amt == 3500 && tick == 1000 && flags == 0x02, "classdmg/the values round-trip");

    d.Put("Fireball_Projectile_BP_C", 4200, 500, 0x02);
    CHECK(d.Get("Fireball_Projectile_BP_C", &amt, &tick, &flags) && amt == 4200,
          "classdmg/a second put replaces rather than duplicates");
    CHECK(d.Count() == 1, "classdmg/replacing does not grow the table");

    // A full table refuses rather than evicts: an evicted class would silently lose its
    // damage, and a class that is wrong is worse than absent.
    for (int i = 0; i < hazard::kMaxClassDmg + 8; ++i) {
        char n[40];
        n[0] = 'C'; n[1] = (char)('0' + (i % 10)); n[2] = (char)('a' + (i / 10)); n[3] = 0;
        d.Put(n, 100, 500, 0);
    }
    CHECK(d.Count() == hazard::kMaxClassDmg, "classdmg/the table stops at its cap");
    CHECK(d.Get("Fireball_Projectile_BP_C", &amt, nullptr, nullptr) && amt == 4200,
          "classdmg/the original entry survives a full table");
}

// ---------------------------------------------------------------------------
// 7b. Two hazards at the same place are one hazard
// ---------------------------------------------------------------------------
static void TestCoincident()
{
    const float a[3] = {1000.0f, 2000.0f, 300.0f};
    const float same[3] = {1000.0f, 2000.0f, 300.0f};
    const float nearE[3] = {1000.0f + hazard::kDedupCm - 1.0f, 2000.0f, 300.0f};
    const float away[3] = {1000.0f + hazard::kDedupCm + 1.0f, 2000.0f, 300.0f};
    const float above[3] = {1000.0f, 2000.0f, 300.0f + 900.0f};

    CHECK(hazard::Coincident(a, same), "dedup/the same point coincides");
    CHECK(hazard::Coincident(a, nearE), "dedup/inside the dedup radius coincides");
    CHECK(!hazard::Coincident(a, away), "dedup/beyond the radius is a different hazard");
    // Positional in the HORIZONTAL plane on purpose: a decorative actor prints at a
    // slightly different height and must still be treated as the same hazard.
    CHECK(hazard::Coincident(a, above), "dedup/a height difference alone still coincides");
}

// ---------------------------------------------------------------------------
// 8. The row policy
// ---------------------------------------------------------------------------
static void TestRowPolicy()
{
    hazard::FieldState a;
    a.px = 0; a.py = 0; a.pz = 0; a.radiusCm = 300; a.tickMs = 500; a.amountCenti = 500; a.flags = 0;
    hazard::FieldState b = a;

    CHECK(hazard::RowDue(b, a, false, 1000, 0), "row/never published is due");
    CHECK(!hazard::RowDue(b, a, true, 1100, 1000), "row/unchanged 100 ms later is not due");
    CHECK(hazard::RowDue(b, a, true, 1200, 1000), "row/unchanged after kRowMs is due as a keepalive");

    b.px = 64;    // 8 cm at 1/8 cm units
    CHECK(hazard::RowDue(b, a, true, 1050, 1000), "row/an 8 cm move is due");
    b.px = 8;     // 1 cm
    CHECK(!hazard::RowDue(b, a, true, 1050, 1000), "row/a 1 cm jitter is not due");

    b = a; b.radiusCm = 320;
    CHECK(hazard::RowDue(b, a, true, 1050, 1000), "row/a radius change is due");
    b = a; b.tickMs = 250;
    CHECK(hazard::RowDue(b, a, true, 1050, 1000), "row/a cadence change is due");
    b = a; b.amountCenti = 900;
    CHECK(hazard::RowDue(b, a, true, 1050, 1000), "row/an amount change is due");
    b = a; b.flags = 0x01;
    CHECK(hazard::RowDue(b, a, true, 1050, 1000), "row/a flag change is due");
}

int main()
{
    printf("=== qd hazard tests ===\n");
    TestKindOf();
    TestKey();
    TestInside();
    TestClamps();
    TestHitGate();
    TestRateBound();
    TestClassDamage();
    TestCoincident();
    TestRowPolicy();
    printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
