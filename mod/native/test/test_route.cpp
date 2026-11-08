// test_route.cpp -- the map-divergence board and the level-name gate.
//
// G6 is "hard transitions are not coordinated".  The deliverable is not travel; it is
// DETECTION plus REFUSAL.  These tests pin the two rules that make that safe:
//
//   * a difference between the two machines' map names is not a divergence until it
//     has persisted (a load looks exactly like a split for a frame or two), and a
//     match clears it at once;
//   * once diverged, the partner's union names are never installed into the local
//     union.  A wrong name there makes the partner protect a level that does not
//     exist, which is indistinguishable from "the level did not stream".
//
// The name gate is tested separately because the WorldAsset soft path is unverified:
// an empty name, "None" or the ULevelStreaming wrapper's own object name must be
// refused rather than published.
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

using namespace qd;
using namespace qd::stream;

// ===========================================================================
// level-name gate
// ===========================================================================
static void TestPlausibleNames()
{
    CHECK(PlausibleLevelName("100_GR"), "name/accepts a short package name");
    CHECK(PlausibleLevelName("000_Shared_Base_Lighting"), "name/accepts underscores");
    CHECK(PlausibleLevelName("997_FinalZoneCutscene"), "name/accepts a hard-transition map");
    CHECK(!PlausibleLevelName(nullptr), "name/refuses null");
    CHECK(!PlausibleLevelName(""), "name/refuses empty");
    CHECK(!PlausibleLevelName("/Game/Maps/Ruins/100_GR"),
          "name/refuses a whole path that escaped ShortLevelName");
    CHECK(!PlausibleLevelName("100_GR.100_GR"), "name/refuses a dotted object path");
    CHECK(!PlausibleLevelName("None"), "name/refuses the FName None");
    CHECK(!PlausibleLevelName("Default__LevelStreamingDynamic"),
          "name/refuses a class default object");
    CHECK(!PlausibleLevelName("LevelStreamingDynamic_0"),
          "name/refuses the streaming wrapper's own object name");
    CHECK(!PlausibleLevelName("has space"), "name/refuses whitespace");
}

// ===========================================================================
// MapBoard -- unknown / matching states
// ===========================================================================
static void TestMapBoardBasics()
{
    MapBoard m;
    CHECK(!m.BothKnown(), "map/starts with no names");
    CHECK(!m.Diverged(0), "map/unknown names are not a divergence");
    CHECK(m.PeerStateApplies(0), "map/unknown peer state is not refused");

    CHECK(m.OnLocal("Ruins_Level_00", 100), "map/first local name is announced");
    CHECK(!m.OnLocal("Ruins_Level_00", 200), "map/an unchanged local name is not re-announced");
    CHECK(!m.BothKnown(), "map/local alone is still unknown");
    CHECK(!m.Diverged(100000), "map/no peer map is not a divergence");
    CHECK(m.PeerStateApplies(100000), "map/no peer map does not refuse state");

    CHECK(m.OnPeer("Ruins_Level_00", 100), "map/first peer name is announced");
    CHECK(m.BothKnown(), "map/both names known");
    CHECK(!m.NamesDiffer(), "map/equal names do not differ");
    CHECK(!m.Diverged(100), "map/equal names are not divergent");
    CHECK(!m.Diverged(60000), "map/equal names stay agreed");
    CHECK(m.PeerStateApplies(60000), "map/equal names apply peer state");

    // An empty peer announcement means "no map" (a menu or a dead world), not a
    // different map.  It must not start a divergence clock.
    MapBoard e;
    e.OnLocal("100_GR", 0);
    e.OnPeer("", 0);
    CHECK(!e.Diverged(10000), "map/an empty peer map is unknown, not divergent");
}

// ===========================================================================
// MapBoard -- the debounce
// ===========================================================================
static void TestMapBoardDebounce()
{
    MapBoard m;
    m.OnLocal("100_GR", 1000);
    m.OnPeer("500_AUT", 1000);
    CHECK(m.NamesDiffer(), "map/two different names differ");
    CHECK(!m.Diverged(1000), "map/a difference is not divergence at once");
    CHECK(!m.Diverged(3999), "map/still inside the debounce at 2999 ms");
    CHECK(m.Diverged(4000), "map/diverges after 3000 ms");
    CHECK(!m.PeerStateApplies(4000), "map/a diverged peer's state is refused");
}

// ===========================================================================
// MapBoard -- clear-on-match and restart-on-a-new-mismatch
// ===========================================================================
static void TestMapBoardClearAndRestart()
{
    MapBoard m;
    m.OnLocal("100_GR", 0);
    m.OnPeer("500_AUT", 0);
    CHECK(m.Diverged(3000), "map/diverged before the match");
    CHECK(m.OnPeer("100_GR", 3000), "map/the matching name is a change");
    CHECK(!m.NamesDiffer(), "map/the names now match");
    CHECK(!m.Diverged(3000), "map/a match clears divergence at once");
    CHECK(m.PeerStateApplies(3000), "map/a match applies peer state again");
    CHECK(!m.Diverged(9000), "map/the match stays clear");

    // A load changes one name and then the other.  Every change that leaves a
    // mismatch restarts the clock, so the load never declares divergence.
    MapBoard r;
    r.OnLocal("A", 0);
    r.OnPeer("B", 0);
    CHECK(!r.Diverged(2000), "map/first difference inside the window");
    r.OnLocal("C", 2000);                     // still != B: a new mismatch
    CHECK(!r.Diverged(4999), "map/a new mismatch restarts the debounce");
    CHECK(r.Diverged(5000), "map/diverges 3 s after the last name change");

    MapBoard f;
    f.OnLocal("A", 0);
    f.OnPeer("B", 0);
    CHECK(!f.Diverged(2000), "map/flap: difference inside the window");
    f.OnLocal("B", 2000);                     // match
    CHECK(!f.Diverged(2000), "map/flap: a match clears the clock");
    f.OnLocal("A", 2500);                     // differ again
    CHECK(!f.Diverged(5499), "map/flap: the second difference starts over");
    CHECK(f.Diverged(5500), "map/flap: divergence only after a full 3 s");
}

// ===========================================================================
// The actual rule: a diverged peer's union names are never applied.
// ===========================================================================
static void TestPeerUnionGate()
{
    MapBoard m;
    m.OnLocal("100_GR", 0);
    m.OnPeer("500_AUT", 0);

    const char* local[] = { "100_GR" };
    const char* peerNames[] = { "500_AUT", "510_AUT_droptunnel" };

    // Inside the grace window this is still an ordinary load.
    UnionGuard grace;
    grace.OnLocalList(local, 1, 2999);
    if (m.PeerStateApplies(2999)) grace.OnPeerList(0, peerNames, 2, 2999);
    CHECK(grace.ShouldShow("500_AUT", 2999),
          "map/inside the debounce the peer list is still applied");

    // After the debounce the gate must refuse it.  A fresh guard makes the point
    // unambiguous: the names never entered the union at all.
    UnionGuard g;
    g.OnLocalList(local, 1, 3000);
    if (m.PeerStateApplies(3000)) g.OnPeerList(0, peerNames, 2, 3000);
    CHECK(!g.ShouldShow("500_AUT", 3000),
          "map/a diverged peer's union names are never applied");
    CHECK(!g.ShouldShow("510_AUT_droptunnel", 3000),
          "map/none of a diverged peer's union names are applied");
    CHECK(g.ShouldShow("100_GR", 3000),
          "map/the local world's own names still apply");

    // When the names agree again, the next list is applied normally.
    m.OnPeer("100_GR", 3100);
    UnionGuard g2;
    g2.OnLocalList(local, 1, 3100);
    if (m.PeerStateApplies(3100)) g2.OnPeerList(0, peerNames, 2, 3100);
    CHECK(g2.ShouldShow("500_AUT", 3100),
          "map/a matching peer's union is applied again");

    // And clearing the slot on divergence (what StreamPump does) drops the old list
    // immediately rather than waiting for its 30 s staleness window.
    UnionGuard stale;
    stale.OnLocalList(local, 1, 0);
    stale.OnPeerList(0, peerNames, 2, 0);
    CHECK(stale.ShouldShow("500_AUT", 0), "map/the old list held the level before the split");
    stale.OnPeerGone(0);
    CHECK(!stale.ShouldShow("500_AUT", 0), "map/clearing on divergence releases the old list");
}

int main()
{
    printf("=== qd route/travel tests ===\n");
    TestPlausibleNames();
    TestMapBoardBasics();
    TestMapBoardDebounce();
    TestMapBoardClearAndRestart();
    TestPeerUnionGate();
    printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
