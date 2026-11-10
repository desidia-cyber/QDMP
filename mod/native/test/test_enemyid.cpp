// test_enemyid.cpp -- a stable identity for enemies that have no spawner.
//
// WHY THESE TESTS AND NOT A LIVE RUN
// ----------------------------------
// The rule exists to make two machines compute the SAME key without negotiating, and
// every way of getting it wrong looks like sync rather than like a bug:
//
//   * hashing something per-machine (a pointer, a runtime object name) gives two
//     different keys for one enemy -- the copy never follows and both AIs run;
//   * letting a second enemy share a spawner's GUID gives two enemies one record --
//     the health bar, the row and the kill all apply to the wrong one;
//   * keying a summon on anything discovered in scan order gives the same summon
//     different keys on the two machines, because GObjects order is per machine.
//
// None of those is visible in a code review and each costs a game run to find.  Here
// they are a two-second test with the property stated as a number.
//
// The central assertion is DETERMINISM: for any input, two independent evaluations
// (which is what two machines are) must produce identical bytes.  That is checked
// directly rather than by comparing against a hard-coded constant, because a constant
// only proves the current build agrees with itself -- the property is that the function
// has no hidden input at all.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "qdenemyid.h"
#include "qdenemy.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, name)                                                          \
    do {                                                                           \
        if (cond) { ++g_pass; }                                                    \
        else { ++g_fail; printf("FAIL %s  (%s:%d)\n", name, __FILE__, __LINE__); } \
    } while (0)

using namespace qd;

static bool Eq(const uint32_t a[4], const uint32_t b[4])
{
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

// ===========================================================================
// Determinism: the same input gives the same key, always
// ===========================================================================
static void TestHashIsDeterministic()
{
    uint32_t a[4], b[4];
    CHECK(enemyid::PlacedKey("PersistentLevel/Level_01/Grunt_3", a), "placed key resolves");
    CHECK(enemyid::PlacedKey("PersistentLevel/Level_01/Grunt_3", b), "placed key resolves twice");
    CHECK(Eq(a, b), "the same placed path hashes to the same key on a second evaluation");

    // An empty or null path is a caller bug, not a key: reporting it is what stops a
    // whole level of enemies collapsing onto the hash of "".
    uint32_t c[4];
    CHECK(!enemyid::PlacedKey("", c), "an empty placed path is refused");
    CHECK(!enemyid::PlacedKey(nullptr, c), "a null placed path is refused");
}

// ===========================================================================
// Distinctness: different enemies do not share a key
// ===========================================================================
static void TestPlacedKeysDiffer()
{
    uint32_t a[4], b[4], c[4];
    enemyid::PlacedKey("PersistentLevel/Level_01/Grunt_3", a);
    enemyid::PlacedKey("PersistentLevel/Level_01/Grunt_4", b);
    enemyid::PlacedKey("PersistentLevel/Level_02/Grunt_3", c);
    CHECK(!Eq(a, b), "two placed enemies in one level have different keys");
    CHECK(!Eq(a, c), "the same actor name in two levels has different keys");

    // The prefix trap: a byte string that is a prefix of another must not collide.
    uint32_t p1[4], p2[4];
    enemyid::PlacedKey("Level/Grunt", p1);
    enemyid::PlacedKey("Level/Grunt2", p2);
    CHECK(!Eq(p1, p2), "a path that is a prefix of another does not collide");

    CHECK(!enemyid::ZeroKey(a) && !enemyid::ZeroKey(b), "a placed key is never the zero key");
}

// ===========================================================================
// Summons: a function of the summoner and the SPAWN POINT, not of scan order
// ===========================================================================
static void TestSummonKeys()
{
    const uint32_t summoner[4] = {0x5ae3b37bu, 0x4f721b25u, 0xbd55a3b5u, 0x958ab1c2u};
    const uint32_t other[4]    = {0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u};
    const int32_t p1[3] = {100, 200, 300};
    const int32_t p2[3] = {100, 200, 310};
    const char* archer = "Enemies/Boner_Gray_Archer.Boner_Gray_Archer_BP_C";
    const char* skull  = "Enemies/FlamingSkull.FlamingSkull_BP_C";

    uint32_t a1[4], a2[4], b1[4], again[4];
    enemyid::SummonKey(summoner, p1, 0, archer, a1);
    enemyid::SummonKey(summoner, p2, 0, archer, a2);
    enemyid::SummonKey(other, p1, 0, archer, b1);
    enemyid::SummonKey(summoner, p1, 0, archer, again);

    CHECK(!Eq(a1, a2), "two summons of one summoner at different points have different keys");
    CHECK(!Eq(a1, b1), "the same point under two summoners has different keys");
    CHECK(Eq(a1, again), "a summon key is reproducible");
    CHECK(!enemyid::ZeroKey(a1), "a summon key is never the zero key");

    // R137: the same summoner at the same point with a DIFFERENT class is a different
    // enemy.  Without this the two keys collide and one machine's HP/row/kill applies to
    // the other's enemy -- the measured `hp 85` vs `hp 30` on one key.
    uint32_t s1[4], s2[4];
    enemyid::SummonKey(summoner, p1, 0, archer, s1);
    enemyid::SummonKey(summoner, p1, 0, skull, s2);
    CHECK(!Eq(s1, s2), "two classes summoned at one point have different keys");
    CHECK(Eq(s1, a1), "the class discriminator does not disturb the plain case");
    // An empty class is still a valid, distinct discriminator (a caller that could not
    // read the path must not collide with a class whose path is empty).
    uint32_t e1[4];
    enemyid::SummonKey(summoner, p1, 0, "", e1);
    CHECK(!enemyid::ZeroKey(e1), "an empty class still yields a key");

    // The coincidence index separates summons at ONE point, and only there.
    uint32_t d0[4], d1[4], d2[4];
    enemyid::SummonKey(summoner, p1, 0, archer, d0);
    enemyid::SummonKey(summoner, p1, 1, archer, d1);
    enemyid::SummonKey(summoner, p1, 2, archer, d2);
    CHECK(!Eq(d0, d1) && !Eq(d1, d2) && !Eq(d0, d2),
          "coincident summons are separated by the index");

    // The coordinate spelling must not conflate: "1,23" and "12,3" are different sums.
    const int32_t c1[3] = {1, 23, 0};
    const int32_t c2[3] = {12, 3, 0};
    uint32_t k1[4], k2[4];
    enemyid::SummonKey(summoner, c1, 0, archer, k1);
    enemyid::SummonKey(summoner, c2, 0, archer, k2);
    CHECK(!Eq(k1, k2), "coordinates are separated so 1,23 and 12,3 differ");

    // Negative coordinates are distinguishable from positive ones.
    const int32_t n1[3] = {-5, 0, 0};
    const int32_t n2[3] = {5, 0, 0};
    uint32_t kn1[4], kn2[4];
    enemyid::SummonKey(summoner, n1, 0, archer, kn1);
    enemyid::SummonKey(summoner, n2, 0, archer, kn2);
    CHECK(!Eq(kn1, kn2), "a negative coordinate differs from its positive twin");

    // A chain: a summon whose summoner is itself a summon stays reproducible.
    uint32_t chain[4], chainAgain[4];
    enemyid::SummonKey(a1, p1, 0, skull, chain);
    enemyid::SummonKey(a1, p1, 0, skull, chainAgain);
    CHECK(Eq(chain, chainAgain), "a chained summon key is reproducible");
    CHECK(!Eq(chain, a1), "a chained summon key differs from its summoner's");
}

// ===========================================================================
// Order independence: the property that makes a summon key safe
// ===========================================================================
// This is the assertion the whole design turns on.  Two machines discover the same
// summons in different orders; the key must not depend on that order at all.
static void TestOrderIndependence()
{
    const uint32_t summoner[4] = {0xAAu, 0xBBu, 0xCCu, 0xDDu};
    const int32_t pts[3][3] = {{10, 20, 30}, {40, 50, 60}, {-70, 80, -90}};
    const char* cls = "Enemies/RattyBugger.RattyBugger_BP_C";

    // Forward and reverse presentation.
    uint32_t fwd[3][4], rev[3][4];
    for (int i = 0; i < 3; ++i) enemyid::SummonKey(summoner, pts[i], 0, cls, fwd[i]);
    for (int i = 2; i >= 0; --i) enemyid::SummonKey(summoner, pts[i], 0, cls, rev[i]);

    bool same = true;
    for (int i = 0; i < 3; ++i) if (!Eq(fwd[i], rev[i])) same = false;
    CHECK(same, "a summon's key does not depend on discovery order");

    // And the three are still distinct, so the property is not "everything collides".
    CHECK(!Eq(fwd[0], fwd[1]) && !Eq(fwd[1], fwd[2]) && !Eq(fwd[0], fwd[2]),
          "order independence did not cost distinctness");
}

// ===========================================================================
// Which source applies
// ===========================================================================
static void TestDecide()
{
    CHECK(enemyid::Decide(true, false, true) == enemyid::SRC_SPAWNER,
          "a free spawner GUID wins, even when the actor is also placed");
    CHECK(enemyid::Decide(true, true, true) == enemyid::SRC_PLACED,
          "a GUID another live enemy holds falls through to the placed path");
    CHECK(enemyid::Decide(false, false, true) == enemyid::SRC_PLACED,
          "no spawner GUID falls through to the placed path");
    CHECK(enemyid::Decide(false, false, false) == enemyid::SRC_SUMMON,
          "a runtime spawn with no place in a level is a summon");
    CHECK(enemyid::Decide(true, true, false) == enemyid::SRC_SUMMON,
          "a shared GUID and no placed path is a summon");

    // Only a hash-derived key may be adopted by the receiver: a spawner GUID is exact, so
    // a mismatched one is a genuinely different enemy.
    CHECK(!enemyid::HashDerived(enemyid::SRC_SPAWNER),
          "a spawner GUID is never adopted");
    CHECK(enemyid::HashDerived(enemyid::SRC_PLACED), "a placed key may be adopted");
    CHECK(enemyid::HashDerived(enemyid::SRC_SUMMON), "a summon key may be adopted");
}

// ===========================================================================
// The flag bits: source and state coexist, and an unknown pattern is safe
// ===========================================================================
static void TestFlags()
{
    using namespace enemyid;
    uint8_t f = TagFlags(0x07, SRC_SPAWNER);
    CHECK(SourceOf(f) == SRC_SPAWNER && StateBits(f) == 0x07, "spawner tags to bits 4-5 = 0");
    f = TagFlags(0x07, SRC_PLACED);
    CHECK(SourceOf(f) == SRC_PLACED && StateBits(f) == 0x07, "placed tags to bits 4-5 = 1");
    CHECK((f & kSrcMask) == 0x10, "placed is bit 4");
    f = TagFlags(0x07, SRC_SUMMON);
    CHECK(SourceOf(f) == SRC_SUMMON && StateBits(f) == 0x07, "summon tags to bits 4-5 = 2");
    CHECK((f & kSrcMask) == 0x20, "summon is bit 5");

    // Re-tagging replaces the source without touching the state bits, which is what a
    // row that changes ownership needs.
    f = TagFlags(0x05, SRC_PLACED);
    f = TagFlags(f, SRC_SPAWNER);
    CHECK(SourceOf(f) == SRC_SPAWNER && StateBits(f) == 0x05, "re-tagging keeps the state bits");

    // The existing four state bits must not be disturbed by the new ones.
    CHECK(TagFlags(0x0F, SRC_SUMMON) == 0x2F, "state bits 0-3 plus a summon source");
}

// ===========================================================================
// Coincident spawns: first is 0, then 1, 2; bounded; recycled
// ===========================================================================
static void TestCoincident()
{
    enemyid::CoincidentSpawns c;
    const uint32_t a[4] = {1, 2, 3, 4};
    const uint32_t b[4] = {5, 6, 7, 8};
    const int32_t p[3]  = {100, 200, 300};
    const int32_t q[3]  = {100, 200, 400};

    CHECK(c.Claim(a, p, 0) == 0, "the first summon at a point is index 0");
    CHECK(c.Claim(a, p, 10) == 1, "the second at the same point is index 1");
    CHECK(c.Claim(a, p, 20) == 2, "the third is index 2");
    CHECK(c.Claim(a, q, 30) == 0, "a different point starts at 0 again");
    CHECK(c.Claim(b, p, 40) == 0, "a different summoner starts at 0 again");
    CHECK(c.Claim(a, p, 50) == 3, "the first point's counter is unaffected");
    CHECK(c.Count() == 3, "three (summoner, point) pairs are tracked");

    // The zero summoner is not a summoner.
    const uint32_t z[4] = {0, 0, 0, 0};
    CHECK(c.Claim(z, p, 0) == 0, "a zero summoner claims nothing");
    CHECK(c.Overflows() == 1, "and the refusal is counted");

    // Recycling: fill the table, then a fresh pair is refused until a record is stale.
    enemyid::CoincidentSpawns r;
    uint32_t k[4];
    for (int i = 0; i < enemyid::CoincidentSpawns::kMax; ++i) {
        k[0] = (uint32_t)(i + 1); k[1] = 0; k[2] = 0; k[3] = 0;
        CHECK(r.Claim(k, p, 0) == 0, "each new pair's first index is 0");
    }
    CHECK(r.Count() == enemyid::CoincidentSpawns::kMax, "the table is full");
    uint32_t fresh[4] = {0xDEADBEEFu, 0, 0, 0};
    CHECK(r.Claim(fresh, p, 1000) == 0 && r.Overflows() == 1,
          "a full table refuses a fresh pair");
    uint32_t late = enemyid::CoincidentSpawns::kRecycleMs + 1;
    CHECK(r.Claim(fresh, p, late) == 0 && r.Overflows() == 1,
          "a recycled slot is reused for the fresh pair");
    CHECK(r.Count() == enemyid::CoincidentSpawns::kMax, "the table stays at its bound");
}

// ===========================================================================
// Adoption: a summon seen late is not a second enemy
// ===========================================================================
static void TestAdoption()
{
    CHECK(enemyid::CouldAdopt(0.0f), "a coincident summon can be adopted");
    CHECK(enemyid::CouldAdopt(299.0f), "a summon inside 3 m can be adopted");
    CHECK(!enemyid::CouldAdopt(301.0f), "a summon beyond 3 m is a different enemy");
    CHECK(!enemyid::CouldAdopt(-1.0f), "an unknown distance is not adopted");

    // The rule exists so that a row for an unknown summon key can claim a local record;
    // the ambiguity guard is the caller's.  Pinned through a volatile read so the check
    // is a runtime assertion rather than a constant expression the compiler folds.
    const volatile float kRadius = enemyid::kAdoptCm;
    CHECK(kRadius == 300.0f, "the adoption radius is 3 m");
}

// ===========================================================================
// Reporting a key's HP: unknown is not zero (Session 19, R137)
// ===========================================================================
static void TestHpReporting()
{
    using namespace enemy;
    CHECK(!HpKnown(false, true, true, 100.0f), "a key with no live actor has no known HP");
    CHECK(!HpKnown(true, false, true, 0.0f), "an unreadable HP is unknown");
    CHECK(!HpKnown(true, true, false, 0.0f),
          "a copy never seen alive has unknown HP (HP 0 is pre-activation)");
    CHECK(HpKnown(true, true, true, 100.0f), "a live copy seen alive reports its HP");
    // Session 21: a POSITIVE reading is evidence on its own.  `seenAlive` is set by the
    // death sweep, which is suppressed across a load and early-returns on a mass vanish,
    // so requiring it withheld a number that was there -- the reproducible shape of
    // Session 20's "one shared live key, hp=?".
    CHECK(HpKnown(true, true, false, 100.0f),
          "a positive reading is reported even when seenAlive has not caught up");
    CHECK(!HpKnown(true, true, false, 0.0f),
          "HP 0 that was never seen alive is still unknown (pre-activation)");
    CHECK(HpKnown(true, true, true, 0.0f),
          "HP 0 after being seen alive IS a value: it is the corpse the sweep names");

    CHECK(EffectivelyDead(false, false, true, 100.0f, true),
          "no actor after being seen alive is dead (the R146 broadcast case)");
    CHECK(!EffectivelyDead(false, false, false, 0.0f, false),
          "a vanished copy never seen alive is NOT a corpse (the FA2FC279 false claim)");
    CHECK(EffectivelyDead(true, true, false, 0.0f, false), "the game's own dead flag is dead");
    CHECK(EffectivelyDead(true, false, true, 0.0f, true), "HP 0 after being seen alive is dead");
    CHECK(!EffectivelyDead(true, false, true, 0.0f, false),
          "HP 0 before activation is NOT dead");
    CHECK(!EffectivelyDead(true, false, true, 50.0f, true), "positive HP is alive");
    // Session 23, R151: ONE KEY, ONE BODY.  A key that two records answer for cannot be
    // compared.  The measured case: one host dump held a constructed copy at HP 140 and the
    // real enemy at 30 under `530F7FD0`, both `src=0` (a spawner key), both the same class.
    // They sit at the same FRACTION, so fraction-based convergence has nothing to move and
    // the absolute difference never moves -- reporting it as a desync is a false claim.
    CHECK(HpComparable(true, 0, 0), "one body per key on both machines is comparable");
    CHECK(!HpComparable(true, 1, 0), "a key with a second body here is NOT comparable");
    CHECK(!HpComparable(true, 0, 2), "a key with a second body there is NOT comparable");
    CHECK(!HpComparable(true, 3, 3), "two duplicated sides are not comparable either");
    CHECK(!HpComparable(false, 0, 0), "a key not alive on both is not comparable");
}

// ===========================================================================
// Why a `?` was printed (Session 21)
// ===========================================================================
// Three different facts print the same marker and each needs a different answer, so the
// mapping is pinned here rather than left to the dump line's formatting.
static void TestHpUnknownReason()
{
    using namespace enemy;
    // Not HP at all.
    CHECK(HpUnknownWhy(true, true, true, 100.0f, true, 0) == HPZ_GONE,
          "a dead key reports no HP reason: nothing about it is compared");
    CHECK(HpUnknownWhy(false, false, false, 0.0f, false, 0) == HPZ_GONE,
          "a key with no live actor reports no HP reason either");
    // The three read failures are distinguished, because they mean different things.
    CHECK(HpUnknownWhy(true, false, true, 0.0f, false, 1) == HPZ_NOFN,
          "the getter not resolving is 'nofn' (the attribute component is not there)");
    CHECK(HpUnknownWhy(true, false, true, 0.0f, false, 2) == HPZ_CALLFAIL,
          "a getter whose call did not run is 'callfail'");
    CHECK(HpUnknownWhy(true, false, true, 0.0f, false, 3) == HPZ_NOOUT,
          "a getter with no 4-byte result is 'noout'");
    // The read succeeded.  A reading of 0 on a copy never seen alive is the DORMANT
    // spawner enemy, which is the "the encounter is not running" answer.
    CHECK(HpUnknownWhy(true, true, false, 0.0f, false, 0) == HPZ_ZERO,
          "HP 0 never seen alive is a dormant spawner enemy, not a missing value");
    CHECK(HpUnknownWhy(true, true, false, 120.0f, false, 0) == HPZ_UNSEEN,
          "a positive reading before seenAlive is 'unseen', and HpKnown now reports it");
    CHECK(HpKnown(true, true, false, 120.0f), "the 'unseen' positive reading is reported");
    CHECK(strcmp(HpUnknownText(HPZ_ZERO), "zero") == 0 &&
          strcmp(HpUnknownText(HPZ_NOFN), "nofn") == 0 &&
          strcmp(HpUnknownText(HPZ_GONE), "dead") == 0,
          "the reason text is stable (a run greps it)");
}

int main()
{
    TestHashIsDeterministic();
    TestPlacedKeysDiffer();
    TestSummonKeys();
    TestOrderIndependence();
    TestDecide();
    TestFlags();
    TestCoincident();
    TestAdoption();
    TestHpReporting();
    TestHpUnknownReason();

    printf("\n=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
