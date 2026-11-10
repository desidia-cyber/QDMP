// test_sanitize.cpp -- is this a position the engine can use?  (Session 24, R155.)
//
// WHY THIS TEST EXISTS
// --------------------
// The defect it guards was measured, not imagined: the rig read a "transform" out of an
// object that was no longer the actor an enemy record named (the object-table slot had been
// reused by a `WidgetTree`), got `(0, -9.223372e18, 0)`, and wrote it into the local player
// pawn with `K2_SetActorLocationAndRotation`.  Six seconds later the game faulted inside its
// own Blueprint dispatch -- a movement/steering function reading `Target->RootComponent`
// with a null target -- and the session was over.
//
// The rule is deliberately two-sided and deliberately crude:
//   * NaN is refused, because it propagates silently: every distance becomes NaN, every
//     comparison false, and the engine's own vector math carries it into navigation;
//   * a magnitude past 100 km is refused, because a cooked level stores centimetres and
//     this game's worlds span ~50 km.  A 9e18 is a pointer read as a float.
//
// A real position -- including a legitimately distant one, a level origin, and a negative
// coordinate -- must still pass: a guard that refuses real places is worse than none,
// because it turns "cannot move" into a mystery.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "qdmotion.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, name)                                                          \
    do {                                                                           \
        if (cond) { ++g_pass; }                                                    \
        else { ++g_fail; printf("FAIL %s  (%s:%d)\n", name, __FILE__, __LINE__); } \
    } while (0)

using namespace qd::motion;

// Bit patterns, not `0.0f / 0.0f`: MSVC folds the latter into C2124 at compile time.  The
// values are also read through a volatile so the comparisons below are not constant
// expressions (C4127 at /W4).
static float FromBits(uint32_t bits)
{
    float f = 0.0f;
    memcpy(&f, &bits, sizeof(f));
    volatile float v = f;
    return v;
}

static float NotANumber()   { return FromBits(0x7FC00000u); }
static float Infinity()     { return FromBits(0x7F800000u); }

static void TestRealPlaces()
{
    CHECK(SanePos(0.0f, 0.0f, 0.0f), "the world origin is a place");
    CHECK(SanePos(-28287.0f, -11149.0f, -15228.0f), "a measured 780_SZ_Depths drop position");
    CHECK(SanePos(-7139.0f, -675.0f, 2920.0f), "a measured hub position");
    CHECK(SanePos(41053.0f, -13231.0f, 15552.0f), "a placed NPC 41 km out (a real level)");
    CHECK(SanePos(1.0e7f, -1.0e7f, 1.0e7f), "the edge of the range is inside it");
    CHECK(SanePos(-0.0f, 0.0f, -0.0f), "negative zero is not suspicious");
}

static void TestStaleReads()
{
    // The two values a reused object slot actually produced.
    const float kGarbage = -9.223372e18f;
    CHECK(!SanePos(0.0f, kGarbage, 0.0f), "R155: (0, -9.223372e18, 0) is refused");
    CHECK(!SanePos(kGarbage, 0.0f, 0.0f), "the same in X");
    CHECK(!SanePos(0.0f, 0.0f, kGarbage), "the same in Z");
    CHECK(!SanePos(1.0e7f + 1.0f, 0.0f, 0.0f), "one centimetre past the range is refused");
}

static void TestNotANumber()
{
    const float nan = NotANumber();
    CHECK(!SanePos(nan, 0.0f, 0.0f), "NaN in X");
    CHECK(!SanePos(0.0f, nan, 0.0f), "NaN in Y");
    CHECK(!SanePos(0.0f, 0.0f, nan), "NaN in Z");
    // A NaN must be caught by the finite test and not by the range test: NaN compares false
    // against every bound, so a naive `if (x > kMax || x < -kMax)` would let it through.
    CHECK(!(nan > 1.0e7f) && !(nan < -1.0e7f), "NaN passes a range-only test (why the NaN test exists)");
}

static void TestInfinities()
{
    const float inf = Infinity();
    CHECK(!SanePos(inf, 0.0f, 0.0f), "+inf in X");
    CHECK(!SanePos(0.0f, -inf, 0.0f), "-inf in Y");
    CHECK(!SanePos(0.0f, 0.0f, inf), "+inf in Z");
    // An overflowed distance is how a NaN/garbage pair becomes an infinite one: the same
    // sum of squares the rig does before it divides.
    volatile float huge = 9.2e18f;
    float d2 = huge * huge;
    CHECK(!SanePos(d2, 0.0f, 0.0f), "an overflowed square is refused too");
}

int main()
{
    TestRealPlaces();
    TestStaleReads();
    TestNotANumber();
    TestInfinities();
    printf("test_sanitize: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
