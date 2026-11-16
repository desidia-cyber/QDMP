// test_join.cpp -- the join-reconcile rules (qdjoin.h).
//
// These are the parts that go wrong SILENTLY in a game and cannot be debugged from a log:
// which keys are compared, in which direction, and what is excluded.  A wrong direction is
// a door that closes on the player who opened it; a missing exclusion is a shop shelf that
// empties when the partner buys the last relic; a delta counter adopted as an absolute is a
// chest the player opened that stops being counted.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "qdjoin.h"
#include "qdobjectstate.h"   // objects::SH_* -- the shape bound must agree

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, name)                                                          \
    do {                                                                           \
        if (cond) { ++g_pass; }                                                    \
        else { ++g_fail; printf("FAIL %s  (%s:%d)\n", name, __FILE__, __LINE__); } \
    } while (0)

using namespace qd;
using namespace qd::join;

// The tables are large (a 1024-key enemy set, a 512-row plan); static so a test process
// does not put half a megabyte of diff on its stack.  The GAME side does the same.
static State g_host;
static State g_local;
static Plan  g_plan;

static void Reset3()
{
    g_host.Reset();
    g_local.Reset();
    g_plan.Reset();
}

static void SetGuid(uint32_t out[4], uint32_t a)
{
    out[0] = a; out[1] = a + 1; out[2] = a + 2; out[3] = a + 3;
}

// ---------------------------------------------------------------------------
// Flags
// ---------------------------------------------------------------------------
static void TestFlags()
{
    Reset3();

    g_host.AddFlag("Arjaot_State", 1);
    g_host.AddFlag("BrokeBarricade", 1);
    g_host.AddFlag("CheckpointUnlockCount", 7);
    g_host.AddFlag("TotalChestsOpened", 5);
    g_host.AddFlag("TotalAchievementWeapons", 2);
    g_host.AddFlag("ShopUnlockFinished", 1);

    g_local.AddFlag("Arjaot_State", 1);            // same: nothing
    g_local.AddFlag("BrokeBarricade", 0);          // host 1, peer 0: adopt
    g_local.AddFlag("CheckpointUnlockCount", 8);   // host 7, peer 8: revert (the peer is ahead)
    g_local.AddFlag("TotalChestsOpened", 8);       // delta counter, peer ahead: donate
    g_local.AddFlag("TotalAchievementWeapons", 5);// per-player, peer ahead: NEVER lower
    g_local.AddFlag("CompletedChallenges", 0);     // per-player, host absent: nothing
    g_local.AddFlag("QuestOnlyOnPeer", 1);         // shared, host absent: revert to 0
    g_local.AddFlag("Shop_00_ShopPurchasedKey", 1);// per-player stock: never touched

    BuildPlan(g_host, g_local, &g_plan);

    int sawArjaot = 0, sawBroke = 0, sawCp = 0, sawChest = 0, sawAch = 0, sawLocal = 0;
    int sawShop = 0, sawCompleted = 0;
    for (int i = 0; i < g_plan.nFlag; ++i) {
        const FlagOp& o = g_plan.flag[i];
        if (!strcmp(o.name, "Arjaot_State")) { sawArjaot = 1; }
        if (!strcmp(o.name, "BrokeBarricade")) {
            sawBroke = 1;
            CHECK(o.act == A_ADOPT, "flags/same-shape rise is an adopt");
            CHECK(o.from == 0 && o.to == 1, "flags/rise carries both values");
        }
        if (!strcmp(o.name, "CheckpointUnlockCount")) {
            sawCp = 1;
            CHECK(o.act == A_REVERT, "flags/a peer-ahead unlock count is a REVERT");
            CHECK(o.from == 8 && o.to == 7, "flags/revert carries both values");
        }
        if (!strcmp(o.name, "TotalChestsOpened")) {
            sawChest = 1;
            CHECK(o.act == A_DONATE, "flags/a union counter the peer is ahead on is a DONATE");
            CHECK(o.to == 3, "flags/the donated amount is the excess");
        }
        if (!strcmp(o.name, "TotalAchievementWeapons")) sawAch = 1;
        if (!strcmp(o.name, "QuestOnlyOnPeer")) {
            sawLocal = 1;
            CHECK(o.act == A_REVERT && o.to == 0, "flags/a peer-only shared flag is reverted");
        }
        if (!strcmp(o.name, "Shop_00_ShopPurchasedKey")) sawShop = 1;
        if (!strcmp(o.name, "CompletedChallenges")) sawCompleted = 1;
    }
    CHECK(!sawArjaot, "flags/equal values are not a change");
    CHECK(sawBroke, "flags/a peer-behind flag is adopted");
    CHECK(sawCp, "flags/the derived unlock counter IS reconciled (it follows the set)");
    CHECK(sawChest, "flags/the union counter is planned");
    CHECK(!sawAch, "flags/a per-player achievement counter is NEVER lowered");
    CHECK(!sawCompleted, "flags/a host-absent per-player counter is not reverted");
    CHECK(sawLocal, "flags/a host-absent shared flag is planned");
    CHECK(!sawShop, "flags/shop stock never crosses");
    CHECK(g_plan.Any(), "flags/a real difference makes Any() true");

    // A peer ahead on an ACHIEVEMENT adopts upward, and a host-ahead one is not lowered.
    Reset3();
    g_host.AddFlag("TotalAchievementWeapons", 9);
    g_local.AddFlag("TotalAchievementWeapons", 4);
    BuildPlan(g_host, g_local, &g_plan);
    CHECK(g_plan.nFlag == 1 && g_plan.flag[0].act == A_ADOPT && g_plan.flag[0].to == 9,
          "flags/a per-player counter IS raised to a host that is ahead");
}

// ---------------------------------------------------------------------------
// Savable actor states
// ---------------------------------------------------------------------------
static void TestActors()
{
    Reset3();
    uint32_t g1[4], g2[4], g3[4];
    SetGuid(g1, 0x11110000u);
    SetGuid(g2, 0x22220000u);
    SetGuid(g3, 0x33330000u);

    g_host.AddActor(g1, 1);          // peer holds 0 -> adopt 1
    g_host.AddActor(g2, 0);          // peer holds 1 -> revert 0
    g_host.AddActor(g3, 0);          // absent/0 on both -> nothing
    g_local.AddActor(g1, 0);
    g_local.AddActor(g2, 1);
    uint32_t g4[4]; SetGuid(g4, 0x44440000u);
    g_local.AddActor(g4, 1);         // peer-only, non-zero -> revert 0

    BuildPlan(g_host, g_local, &g_plan);
    CHECK(g_plan.nActor == 3, "actors/three real changes");
    int adopt = 0, revert = 0;
    for (int i = 0; i < g_plan.nActor; ++i) {
        if (g_plan.actor[i].act == A_ADOPT) ++adopt;
        if (g_plan.actor[i].act == A_REVERT) ++revert;
    }
    CHECK(adopt == 1, "actors/the host's open is adopted");
    CHECK(revert == 2, "actors/a peer open the host does not hold IS reverted");

    // A key the host does not hold and the peer holds at 0 is not a change.
    Reset3();
    SetGuid(g4, 0x44440000u);
    g_local.AddActor(g4, 0);
    BuildPlan(g_host, g_local, &g_plan);
    CHECK(g_plan.nActor == 0, "actors/absent == 0 on both sides");
}

// ---------------------------------------------------------------------------
// Placed world objects
// ---------------------------------------------------------------------------
static void TestObjects()
{
    Reset3();
    uint32_t k1[4], k2[4], k3[4];
    SetGuid(k1, 0xAAA10000u);
    SetGuid(k2, 0xAAA20000u);
    SetGuid(k3, 0xAAA30000u);

    g_host.AddObject(k1, objects::SH_DOOR, 1);      // peer closed -> adopt open
    g_host.AddObject(k3, objects::SH_DOOR, 0);      // peer open -> revert (a door can close)
    g_local.AddObject(k1, objects::SH_DOOR, 0);
    g_local.AddObject(k3, objects::SH_DOOR, 1);
    g_local.AddObject(k2, objects::SH_CHEST, 1);    // host does not hold it -> NOT a divergence

    BuildPlan(g_host, g_local, &g_plan);
    CHECK(g_plan.nObject == 2, "objects/only keys present on both are compared");
    int seenAdopt = 0, seenRevert = 0, seenK2 = 0;
    for (int i = 0; i < g_plan.nObject; ++i) {
        const ObjectOp& o = g_plan.object[i];
        if (o.act == A_ADOPT) ++seenAdopt;
        if (o.act == A_REVERT) {
            ++seenRevert;
            CHECK(o.to == 0, "objects/the revert target is closed");
        }
        if (State::GuidEq(o.key, k2)) seenK2 = 1;
    }
    CHECK(seenAdopt == 1, "objects/an open door on the host is adopted");
    CHECK(seenRevert == 1, "objects/a door open only on the peer IS closed");
    CHECK(!seenK2, "objects/a host-absent object is not reverted (its level may not be loaded)");
}

// ---------------------------------------------------------------------------
// Enemy deaths
// ---------------------------------------------------------------------------
static void TestEnemies()
{
    Reset3();
    uint32_t e1[4], e2[4], e3[4];
    SetGuid(e1, 0xE1110000u);
    SetGuid(e2, 0xE2220000u);
    SetGuid(e3, 0xE3330000u);

    g_host.AddEnemy(e1);     // dead on the host only -> kill here
    g_host.AddEnemy(e3);     // dead on both -> nothing
    g_local.AddEnemy(e2);    // dead here only -> revive
    g_local.AddEnemy(e3);

    BuildPlan(g_host, g_local, &g_plan);
    CHECK(g_plan.nEnemy == 2, "enemy/one kill and one revive");
    CHECK(g_plan.EnemyKilled() == 1, "enemy/a host-only death is a kill");
    CHECK(g_plan.EnemyRevived() == 1, "enemy/a peer-only death is a revive");

    Reset3();
    g_host.AddEnemy(e1);
    g_local.AddEnemy(e1);
    BuildPlan(g_host, g_local, &g_plan);
    CHECK(g_plan.nEnemy == 0, "enemy/the same dead set is nothing to do");
}

// ---------------------------------------------------------------------------
// Quest / key items
// ---------------------------------------------------------------------------
static void TestQuest()
{
    Reset3();
    g_host.AddQuest("/Game/Items/KeyItems/SwordShrineKey", 1);
    g_host.AddQuest("/Game/Items/KeyItems/AquaDoorKey", 2);
    g_host.AddQuest("/Game/Items/Consumables/Herb", 9);   // not a key item: ignored

    g_local.AddQuest("/Game/Items/KeyItems/SwordShrineKey", 0);  // adopt
    g_local.AddQuest("/Game/Items/KeyItems/SwordShrineKey_dup", 1); // absent on host: revert
    g_local.AddQuest("/Game/Items/KeyItems/AquaDoorKey", 5);     // host 2, peer 5: revert
    g_local.AddQuest("/Game/Items/Consumables/Herb", 3);         // per-player: ignored

    BuildPlan(g_host, g_local, &g_plan);
    CHECK(g_plan.ItemsAdded() == 1, "quest/a missing key is added");
    CHECK(g_plan.ItemsRemoved() == 2, "quest/an extra key is removed and an excess is lowered");
    int sawHerb = 0;
    for (int i = 0; i < g_plan.nQuest; ++i)
        if (!strcmp(g_plan.quest[i].path, "/Game/Items/Consumables/Herb")) sawHerb = 1;
    CHECK(!sawHerb, "quest/a normal item never crosses");

    // Exact adoption: the host's count is the target in BOTH directions.
    Reset3();
    g_host.AddQuest("/Game/Items/KeyItems/AquaDoorKey", 2);
    g_local.AddQuest("/Game/Items/KeyItems/AquaDoorKey", 5);
    BuildPlan(g_host, g_local, &g_plan);
    CHECK(g_plan.nQuest == 1 && g_plan.quest[0].to == 2 && g_plan.quest[0].act == A_REVERT,
          "quest/an excess key count is lowered to the host");
}

// ---------------------------------------------------------------------------
// World sets
// ---------------------------------------------------------------------------
static void TestSets()
{
    Reset3();
    g_host.AddSet(worldset::SET_TELEPORTERS, "Teleporter_400_AUT");
    g_host.AddSet(worldset::SET_SAVEPOINTS, "GR_Camp_Save");
    g_local.AddSet(worldset::SET_TELEPORTERS, "Teleporter_500_AUT");   // peer-only: remove
    g_local.AddSet(worldset::SET_SAVEPOINTS, "GR_Camp_Save");

    BuildPlan(g_host, g_local, &g_plan);
    CHECK(g_plan.nSet == 2, "sets/one add and one remove");
    int addT = 0, removeT = 0;
    for (int i = 0; i < g_plan.nSet; ++i) {
        const SetOp& o = g_plan.set[i];
        if (o.act == A_ADOPT && o.set == worldset::SET_TELEPORTERS) addT = 1;
        if (o.act == A_REVERT && o.set == worldset::SET_TELEPORTERS) removeT = 1;
    }
    CHECK(addT, "sets/the host's teleporter is added");
    CHECK(removeT, "sets/a teleporter only the peer unlocked IS removed");
}

// ---------------------------------------------------------------------------
// Puzzle poses
// ---------------------------------------------------------------------------
static void TestPuzzle()
{
    Reset3();
    PuzzleRow r;
    memset(&r, 0, sizeof(r));
    SetGuid(r.key, 0xB0000000u);
    r.yaw = 100; r.pitch = 0; r.state = 0; r.seq = 3;
    g_host.AddPuzzle(r);

    PuzzleRow l = r;
    l.yaw = 200;
    l.seq = 1;
    g_local.AddPuzzle(l);

    BuildPlan(g_host, g_local, &g_plan);
    CHECK(g_plan.nPuzzle == 1, "puzzle/a differing pose is adopted");
    CHECK(g_plan.puzzle[0].row.yaw == 100, "puzzle/the host's pose is the target");

    // Same pose: nothing.  A prop the peer does not hold: not invented.
    Reset3();
    g_host.AddPuzzle(r);
    g_local.AddPuzzle(r);
    BuildPlan(g_host, g_local, &g_plan);
    CHECK(g_plan.nPuzzle == 0, "puzzle/the same pose is not news");

    Reset3();
    g_host.AddPuzzle(r);
    BuildPlan(g_host, g_local, &g_plan);
    CHECK(g_plan.nPuzzle == 0, "puzzle/a prop the peer does not hold is not invented");
}

// ---------------------------------------------------------------------------
// Map / area travel
// ---------------------------------------------------------------------------
static void TestMap()
{
    Reset3();
    g_host.SetMap("900_SipehaWorld", "", 0, 0, 0);
    g_local.SetMap("Ruins_Level_00", "", 0, 0, 0);
    BuildPlan(g_host, g_local, &g_plan);
    CHECK(g_plan.travel == TV_WORLD, "map/a different UWorld is a hard travel");
    CHECK(!strcmp(g_plan.travelTo, "900_SipehaWorld"), "map/the world name is the target");

    Reset3();
    g_host.SetMap("Ruins_Level_00", "780_SZ_Depths", 0, 0, 0);
    g_local.SetMap("Ruins_Level_00", "", 0, 0, 0);
    BuildPlan(g_host, g_local, &g_plan);
    CHECK(g_plan.travel == TV_AREA, "map/a different area is an area travel");
    CHECK(!strcmp(g_plan.travelTo, "780_SZ_Depths"), "map/the area name is the target");

    Reset3();
    g_host.SetMap("Ruins_Level_00", "", 0, 0, 0);
    g_local.SetMap("Ruins_Level_00", "780_SZ_Depths", 0, 0, 0);
    BuildPlan(g_host, g_local, &g_plan);
    CHECK(g_plan.travel == TV_LEAVE, "map/the host left its area: the peer must leave too");

    Reset3();
    g_host.SetMap("Ruins_Level_00", "780_SZ_Depths", 0, 0, 0);
    g_local.SetMap("Ruins_Level_00", "780_SZ_Depths", 0, 0, 0);
    BuildPlan(g_host, g_local, &g_plan);
    CHECK(g_plan.travel == TV_NONE, "map/the same place is no travel");
    CHECK(!g_plan.Any(), "map/an identical world/area is 'already in sync'");
}

// ---------------------------------------------------------------------------
// Wire
// ---------------------------------------------------------------------------
static void TestWire()
{
    Reset3();
    g_host.AddFlag("Arjaot_State", 1);
    g_host.AddFlag("BrokeBarricade", 0);
    uint32_t g1[4]; SetGuid(g1, 0x11110000u);
    g_host.AddActor(g1, 1);
    uint32_t k1[4]; SetGuid(k1, 0xAAA10000u);
    g_host.AddObject(k1, objects::SH_DOOR, 1);
    uint32_t e1[4]; SetGuid(e1, 0xE1110000u);
    g_host.AddEnemy(e1);
    g_host.AddQuest("/Game/Items/KeyItems/SwordShrineKey", 1);
    g_host.AddSet(worldset::SET_TELEPORTERS, "Teleporter_400_AUT");
    g_host.AddSet(worldset::SET_SAVEPOINTS, "GR_Camp_Save");
    PuzzleRow pr; memset(&pr, 0, sizeof(pr));
    SetGuid(pr.key, 0xB0000000u); pr.yaw = 42; pr.seq = 1;
    g_host.AddPuzzle(pr);
    g_host.SetMap("Ruins_Level_00", "780_SZ_Depths", 10, 20, 30);

    // Req / End / Ack round-trip.
    {
        uint8_t buf[64];
        wire::Writer w(buf, sizeof(buf));
        WriteJoinReq(w, 7, JC_FLAGS);
        wire::Reader r(buf, w.len);
        uint32_t seq = 0; uint8_t cat = 0;
        CHECK(ReadJoinReq(r, &seq, &cat) && seq == 7 && cat == JC_FLAGS, "wire/req round-trip");
        wire::Writer w2(buf, sizeof(buf));
        WriteJoinReq(w2, 7, 0);
        wire::Reader r2(buf, w2.len);
        CHECK(ReadJoinReq(r2, &seq, &cat) && cat == 0, "wire/req allows 0 = all");
    }
    {
        uint8_t buf[64];
        wire::Writer w(buf, sizeof(buf));
        WriteJoinReq(w, 1, JC_COUNT);   // out of range
        wire::Reader r(buf, w.len);
        uint32_t seq = 0; uint8_t cat = 0;
        CHECK(!ReadJoinReq(r, &seq, &cat), "wire/req refuses an out-of-range category");
    }
    {
        uint8_t buf[64];
        wire::Writer w(buf, sizeof(buf));
        WriteJoinEnd(w, 3, 0xFF);
        wire::Reader r(buf, w.len);
        uint32_t seq = 0; uint8_t mask = 0;
        CHECK(ReadJoinEnd(r, &seq, &mask) && seq == 3 && mask == 0xFF, "wire/end round-trip");
        wire::Writer wa(buf, sizeof(buf));
        WriteJoinAck(wa, 3, AK_RECONCILED);
        wire::Reader ra(buf, wa.len);
        uint8_t res = 0;
        CHECK(ReadJoinAck(ra, &seq, &res) && res == AK_RECONCILED, "wire/ack round-trip");
    }

    // Every category round-trips through the chunk writer/reader, at a SMALL cap so the
    // cursor path (several chunks) is exercised rather than one big body.
    for (uint8_t cat = JC_FLAGS; cat <= JC_MAP; ++cat) {
        uint8_t sub = (cat == JC_WORLDSET) ? worldset::SET_TELEPORTERS : 0;
        if (cat == JC_WORLDSET) sub = worldset::SET_TELEPORTERS;
        int total = TotalRows(g_host, cat, sub);
        if (cat == JC_WORLDSET) total = g_host.nSet[sub];
        CHECK(total >= 1, "wire/a category the host carried has rows");

        State got;
        got.Reset();
        int first = 0;
        int guard = 0;
        while (first < total && guard++ < 32) {
            uint8_t body[120];                    // deliberately small: forces several chunks
            wire::Writer w(body, sizeof(body));
            StateHeader h;
            h.seq = 9; h.cat = cat; h.sub = sub;
            h.total = (uint16_t)total; h.first = (uint16_t)first;
            WriteStateHeader(w, h);
            int next = WriteRows(w, g_host, cat, sub, first);
            CHECK(next > first, "wire/a chunk makes progress");
            wire::Reader r(body, w.len);
            StateHeader rh;
            if (!ReadStateHeader(r, &rh)) { CHECK(false, "wire/header reads back"); break; }
            CHECK(rh.first == first && rh.total == total, "wire/header carries the cursor");
            int n = ReadRows(r, cat, sub, &got);
            CHECK(n == next - first, "wire/the chunk's row count matches");
            first = next;
        }
        CHECK(first == total, "wire/the cursor completes the category");
        CHECK(TotalRows(got, cat, sub) == total, "wire/the receiver rebuilds the same set");
    }

    // Worldset gets its own sub index: the second set is a separate body.
    {
        State got;
        got.Reset();
        uint8_t body[120];
        wire::Writer w(body, sizeof(body));
        StateHeader h;
        h.seq = 9; h.cat = JC_WORLDSET; h.sub = worldset::SET_SAVEPOINTS;
        h.total = (uint16_t)g_host.nSet[worldset::SET_SAVEPOINTS]; h.first = 0;
        WriteStateHeader(w, h);
        WriteRows(w, g_host, JC_WORLDSET, worldset::SET_SAVEPOINTS, 0);
        wire::Reader r(body, w.len);
        StateHeader rh;
        CHECK(ReadStateHeader(r, &rh) && rh.sub == worldset::SET_SAVEPOINTS,
              "wire/worldset sub is the set id");
        CHECK(ReadRows(r, JC_WORLDSET, rh.sub, &got) == 1 && got.HasSet(worldset::SET_SAVEPOINTS, "GR_Camp_Save"),
              "wire/worldset rows land in the right set");
    }

    // A malformed body is refused, never applied as a prefix.
    {
        State got;
        got.Reset();
        uint8_t body[8] = { 0, 1 };            // a 1-byte "string" with no bytes behind it
        wire::Reader r(body, 4);
        CHECK(ReadRows(r, JC_FLAGS, 0, &got) < 0, "wire/a truncated flag row is refused");
    }
    {
        uint8_t body[32];
        wire::Writer w(body, sizeof(body));
        StateHeader h;
        h.seq = 1; h.cat = JC_OBJECTS; h.sub = 0; h.total = 1; h.first = 0;
        WriteStateHeader(w, h);
        w.Guid(k1);
        w.U8(9);                                // an invalid shape
        w.U8(0);
        wire::Reader r(body, w.len);
        StateHeader rh;
        ReadStateHeader(r, &rh);
        State got;
        got.Reset();
        CHECK(ReadRows(r, JC_OBJECTS, 0, &got) < 0, "wire/an unknown object shape is refused");
    }
    {
        uint8_t body[32];
        wire::Writer w(body, sizeof(body));
        StateHeader h;
        h.seq = 1; h.cat = JC_WORLDSET; h.sub = 0; h.total = 1; h.first = 0;
        WriteStateHeader(w, h);
        wire::Reader r(body, w.len);
        StateHeader rh;
        CHECK(!ReadStateHeader(r, &rh), "wire/a worldset body without a set id is refused");
    }

    // The shape bound must be the object lane's own.
    CHECK(objects_shape_valid((uint8_t)objects::SH_ACTIVATABLE),
          "wire/the shape bound covers every objects::Shape");
    CHECK(!objects_shape_valid((uint8_t)objects::SH_ACTIVATABLE + 1),
          "wire/the shape bound refuses one past the last shape");
}

// ---------------------------------------------------------------------------
// Overflow is reported, not silently truncated
// ---------------------------------------------------------------------------
static void TestOverflow()
{
    Reset3();
    State small;
    small.Reset();
    for (int i = 0; i < kMaxFlags; ++i) {
        char n[32];
        snprintf(n, sizeof(n), "F%04d", i);
        small.AddFlag(n, 1);
    }
    CHECK(small.flagsOverflow == false, "overflow/a full table is not an overflow yet");
    CHECK(!small.AddFlag("OneTooMany", 1), "overflow/the next flag is refused");
    CHECK(small.flagsOverflow, "overflow/the refusal is REPORTED");
}

int main()
{
    TestFlags();
    TestActors();
    TestObjects();
    TestEnemies();
    TestQuest();
    TestSets();
    TestPuzzle();
    TestMap();
    TestWire();
    TestOverflow();

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
