// test_stream.cpp -- which level list carries a streamed area.
//
// WHY THIS TEST EXISTS
// --------------------
// Session 14 concluded that 780_SZ_Depths is in `GreenRuins_SZ` and asked for that list to
// be pushed onto the level manager's stack.  Read out of the shipped pak, `GreenRuins_SZ`
// does not contain 780 at all; `SZ_Depths` does (and carries 760 too).  Pushing a list
// that does not contain the area is a call that succeeds and changes nothing -- the exact
// shape of failure this project keeps paying for.
//
// So the mapping is DERIVED from the pak-read membership, and these tests hold the two
// facts that matter:
//   * the list returned for an area CONTAINS that area (no wrong-list call), and
//   * the preference is the trigger's own list, so 760 and 780 both land on `SZ_Depths`.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "qdstream.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, name)                                                          \
    do {                                                                           \
        if (cond) { ++g_pass; }                                                    \
        else { ++g_fail; printf("FAIL %s  (%s:%d)\n", name, __FILE__, __LINE__); } \
    } while (0)

using namespace qd::stream;

// ===========================================================================
// The measured fact: 780 and 760 are carried by SZ_Depths
// ===========================================================================
static void TestAreaToTriggerList()
{
    const char* l780 = LevelListForArea("780_SZ_Depths");
    const char* l760 = LevelListForArea("760_SZ_AUT");
    CHECK(l780 != nullptr, "area/780 has a list");
    CHECK(l760 != nullptr, "area/760 has a list");
    CHECK(l780 && LevelNameEq(l780, "SZ_Depths"), "area/780 -> SZ_Depths (the trigger's list)");
    CHECK(l760 && LevelNameEq(l760, "SZ_Depths"), "area/760 -> SZ_Depths (preferred over AutumnIsland)");

    // The Session 14 name.  It is not one of the lists this module pushes at all.
    CHECK(!LevelListContains("Greenruins_SZ", "780_SZ_Depths"),
          "area/Greenruins_SZ does not contain 780 (the corrected fact)");
    CHECK(!(l780 && LevelNameEq(l780, "Greenruins_SZ")),
          "area/780 is never mapped to Greenruins_SZ");
}

// ===========================================================================
// The invariant: a list is only returned when it CONTAINS the area
// ===========================================================================
static void TestReturnedListContainsTheArea()
{
    const char* probes[] = {
        "780_SZ_Depths", "760_SZ_AUT", "770_SZ_Swamp", "775_JigekiTemple", "790_SG",
        "710_SZ_AQ", "740_SZ_Prison", "750_SZ_MAUS2",
        "100_GR", "110_GR_dungeon", "120_GR_goldenroom", "125_GR_CubeHall",
        "130_GR_outerisles", "140_GR_ShopIsleOnly", "200_AQ", "310_LIB_exterior",
        "400_MAUS_entrance", "700_SZ_GR",
        "500_AUT", "510_AUT_droptunnel", "600_Castle", "610_Castle_Ext",
    };
    int wrong = 0;
    for (unsigned i = 0; i < sizeof(probes) / sizeof(probes[0]); ++i) {
        const char* list = LevelListForArea(probes[i]);
        if (!list) { ++wrong; continue; }
        if (!LevelListContains(list, probes[i])) ++wrong;
    }
    CHECK(wrong == 0, "invariant/every returned list contains the area it was returned for");

    // The complement: an area nobody carries is refused rather than guessed.  NB
    // `VideoConfigMap` and `000_Shared_Base_Lighting` ARE members of these lists -- they
    // are the engine's shared lighting/preview sublevels -- so they are not in this set.
    const char* none[] = { "900_SipehaWorld", "990_B33sment", "997_FinalZoneCutscene",
                           "NotALevel", "GreenRuins_OuterIsles_ExtraPri" };
    int guessed = 0;
    for (unsigned i = 0; i < sizeof(none) / sizeof(none[0]); ++i)
        if (LevelListForArea(none[i])) ++guessed;
    CHECK(guessed == 0, "invariant/areas no list carries are refused");

    CHECK(LevelListForArea("") == nullptr, "invariant/the empty name is refused");
    CHECK(LevelListForArea(nullptr) == nullptr, "invariant/the null name is refused");
}

// ===========================================================================
// Every name that can reach the manager is a name the game can spell
// ===========================================================================
static void TestNamesArePlausible()
{
    int bad = 0;
    for (int i = 0; i < kLevelListCount; ++i) {
        if (!PlausibleLevelName(kLevelLists[i].name)) ++bad;
        for (int k = 0; k < kLevelLists[i].n; ++k)
            if (!PlausibleLevelName(kLevelLists[i].levels[k])) ++bad;
    }
    CHECK(bad == 0, "names/every list name and member is a plausible level name");

    // The membership arrays are the pak's, so the size the doc states must match.
    int sz = 0;
    for (int i = 0; i < kLevelListCount; ++i)
        if (LevelNameEq(kLevelLists[i].name, "SZ_Depths")) sz = kLevelLists[i].n;
    CHECK(sz == 11, "names/SZ_Depths has the 11 levels the pak lists");
}

// ===========================================================================
// Membership is a set question, not a prefix or a substring one
// ===========================================================================
static void TestMembershipIsExact()
{
    CHECK(LevelListContains("SZ_Depths", "780_SZ_Depths"), "exact/780 is a member");
    CHECK(!LevelListContains("SZ_Depths", "780_SZ_Depth"), "exact/a prefix is not a member");
    CHECK(!LevelListContains("SZ_Depths", "780_SZ_Depths_2"), "exact/a longer name is not a member");
    CHECK(!LevelListContains("GreenRuins", "760_SZ_AUT"), "exact/760 is not in GreenRuins");
    CHECK(LevelListContains("AutumnIsland", "760_SZ_AUT"), "exact/760 is in AutumnIsland");
    CHECK(!LevelListContains("NoSuchList", "780_SZ_Depths"), "exact/an unknown list contains nothing");
    CHECK(LevelNameEq("SZ_Depths", "SZ_Depths"), "exact/identical names are equal");
    CHECK(!LevelNameEq("SZ_Depths", "sz_depths"), "exact/names are case-sensitive like the engine's");
}

int main()
{
    printf("=== qd stream level-list tests ===\n");
    TestAreaToTriggerList();
    TestReturnedListContainsTheArea();
    TestNamesArePlausible();
    TestMembershipIsExact();
    printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
