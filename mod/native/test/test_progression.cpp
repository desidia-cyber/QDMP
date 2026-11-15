// test_progression.cpp -- the three progression gaps (mod/doc/features/progression.md).
//
//   1. which items cross: quest/key only (`Items/KeyItems/`, or the item definition's own
//      `ItemCategory::Keys`) and nothing else;
//   2. a kill pays both players once each, decided by what each machine MEASURED rather
//      than by an assumed award;
//   3. a save on one machine is a save on the other, without a ping-pong and without a save
//      during a load, a menu, a death or a cutscene.
//
// The interesting failures are all state-machine ones and all invisible in a live run: a
// report that arrives before the death it belongs to, a reliable retransmit that pays
// twice, an echo that re-saves for ever, and a key that the host consumed being handed
// straight back by the next absolute body.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "qdprogression.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, name)                                                          \
    do {                                                                           \
        if (cond) { ++g_pass; }                                                    \
        else { ++g_fail; printf("FAIL %s  (%s:%d)\n", name, __FILE__, __LINE__); } \
    } while (0)

using namespace qd;
using namespace qd::progression;

// A real consumable path: the item test below needs one that is NOT a key item, to prove
// that the category fallback decides rather than the folder.
static const char* const pathNoCategory =
    "/Game/Items/Consumables/Herb/Herb_Item_BP.Herb_Item_BP_C";

static SaveGate MakeBlocked(bool loading, bool travelling, bool menu, bool dead,
                            bool cutscene, bool noSave, bool suspendHold)
{
    SaveGate g = {};
    g.loading = loading; g.travelling = travelling; g.menu = menu; g.dead = dead;
    g.cutscene = cutscene; g.noSave = noSave; g.suspendHold = suspendHold;
    return g;
}

// ---------------------------------------------------------------------------
// 1. The item test
// ---------------------------------------------------------------------------
static void TestItemPath()
{
    // The measured forms.  `analysis/23` §4: the inventory stores the soft object path;
    // the drop lane publishes the pickup CLASS path.  Both name the family folder.
    CHECK(IsQuestItemPath("/Game/Items/KeyItems/AquaDoorKey/AquaDoorKey_2_Data.AquaDoorKey_2_Data"),
          "path/a key item's data path is a quest item");
    CHECK(IsQuestItemPath("/Game/Items/KeyItems/AquaDoorKey/AquaDoorKey_BP.AquaDoorKey_BP_C"),
          "path/a key item's pickup class path is a quest item");
    CHECK(IsQuestItemPath("Items/KeyItems/BookKeys/Dragons/BookKey_00_Dragons_BP"),
          "path/the bare pak form (no /Game) is recognised");
    CHECK(IsQuestItemPath("/Game/Items/KeyItems/SecretRelicKey/SkullKeyRelic_Data.SkullKeyRelic_Data"),
          "path/the quest-reward skull key is a quest item");

    // Everything the task says must stay LOCAL.  Each of these is a real asset path from
    // the cooked pak: the herb and the potion are consumables, the sword is equipment, the
    // amulet is an accessory, the bottle is treasure.
    CHECK(!IsQuestItemPath("/Game/Items/Consumables/Herb/Herb_Item_BP.Herb_Item_BP_C"),
          "path/a consumable is local");
    CHECK(!IsQuestItemPath("/Game/Items/Consumables/HealingPotion/HealingPotion_1_Item.HealingPotion_1_Item"),
          "path/a potion is local");
    CHECK(!IsQuestItemPath("/Game/Items/Weapons/RustedSword/RustySword_Item.RustySword_Item"),
          "path/a weapon is local");
    CHECK(!IsQuestItemPath("/Game/Items/Accessories/AkumuAmulet/AkumuAmulet_Item.AkumuAmulet_Item"),
          "path/an accessory is local");
    CHECK(!IsQuestItemPath("/Game/Items/Treasure/Bottle1/Bottle1_Item.Bottle1_Item"),
          "path/treasure is local");
    CHECK(!IsQuestItemPath("/Game/Items/Misc/Coin.Coin"),
          "path/money is local");
    CHECK(!IsQuestItemPath(""), "path/the empty path is not a quest item");
    CHECK(!IsQuestItemPath(nullptr), "path/a null path is not a quest item");

    // Case: the pak and the save disagree on capitalisation in some tooling, and a case
    // difference must not change the answer.
    CHECK(IsQuestItemPath("/game/items/keyitems/aquadookey/x"), "path/the folder test is case-insensitive");

    // The category fallback: an item definition that resolves but lives outside the folder.
    CHECK(IsQuestItem(pathNoCategory, true, kItemCategoryKeys),
          "cat/ItemCategory::Keys is a quest item wherever it lives");
    CHECK(!IsQuestItem(pathNoCategory, true, 0),
          "cat/Consumables is not");
    CHECK(!IsQuestItem(pathNoCategory, true, 2),
          "cat/Weapons is not");
    CHECK(!IsQuestItem(pathNoCategory, true, 8),
          "cat/All is not a key");
    // An unresolvable category is not a "yes": the path decides, and it said no.
    CHECK(!IsQuestItem(pathNoCategory, false, 0),
          "cat/an unreadable category does not make an item shared");
    // ... and an unresolvable category with a key path is still shared.
    CHECK(IsQuestItem("/Game/Items/KeyItems/Ros_Orb/Ros_Orb_Data.Ros_Orb_Data", false, 0),
          "cat/the path alone is enough");
}

// ---------------------------------------------------------------------------
// 2. The host's fall must reach the peer
// ---------------------------------------------------------------------------
static void TestRemoval()
{
    // The ordinary case: the host used a key it had granted this peer.
    CHECK(RemovalNeeded(1, 0, 0, 0) == 1, "rm/the peer removes what the host no longer holds");
    CHECK(RemovalNeeded(2, 1, 0, 0) == 1, "rm/the removal is the difference");
    CHECK(RemovalNeeded(1, 1, 0, 0) == 0, "rm/equal counts remove nothing");
    CHECK(RemovalNeeded(0, 0, 0, 0) == 0, "rm/an empty container removes nothing");
    CHECK(RemovalNeeded(1, 3, 0, 0) == 0, "rm/the peer below the host is a grant, not a removal");

    // The two cases where a difference is NOT the host's use, and the item must be kept.
    CHECK(RemovalNeeded(1, 0, 1, 0) == 0,
          "rm/a difference the peer's own pickup explains is not removed");
    CHECK(RemovalNeeded(1, 0, 0, 1) == 0,
          "rm/a use of the peer's own still in flight is not removed");

    // A negative host count cannot happen, but a clamp is cheaper than a subtraction that
    // removes more than the peer holds.
    CHECK(RemovalNeeded(1, -5, 0, 0) == 1, "rm/a negative host count clamps to zero");
}

static void TestHostView()
{
    HostView v;
    CHECK(v.Count() == 0, "view/starts empty");
    CHECK(v.Set("/Game/Items/KeyItems/A/A_Data", 2), "view/an entry is accepted");
    CHECK(v.Set("/Game/Items/KeyItems/B/B_Data", 1), "view/a second entry is accepted");
    CHECK(v.Count() == 2, "view/two entries");
    CHECK(v.Get("/Game/Items/KeyItems/A/A_Data") == 2, "view/the count is readable");
    CHECK(v.Set("/Game/Items/KeyItems/A/A_Data", 5), "view/a repeat updates in place");
    CHECK(v.Count() == 2, "view/and does not grow the table");
    CHECK(v.Get("/Game/Items/KeyItems/A/A_Data") == 5, "view/the count is the newer one");
    CHECK(v.Get("/Game/Items/KeyItems/nope") == 0, "view/an absent path is zero");
    CHECK(!v.Set("", 1), "view/an empty path is refused");
    CHECK(!v.Set(nullptr, 1), "view/a null path is refused");

    // The bound is the publisher's own, so nothing the host can send is lost here.
    HostView full;
    char p[128];
    int ok = 0;
    for (int i = 0; i < kMaxPaths; ++i) {
        snprintf(p, sizeof(p), "/Game/Items/KeyItems/F%d/F%d_Data", i, i);
        if (full.Set(p, 1)) ++ok;
    }
    CHECK(ok == kMaxPaths, "view/512 entries fit");
    CHECK(full.Set("/Game/Items/KeyItems/Extra/Extra_Data", 1) == false, "view/the 513th is refused");
    CHECK(full.Overflowed(), "view/and the overflow is reported");
}

// ---------------------------------------------------------------------------
// 3. Kill XP
// ---------------------------------------------------------------------------
static const uint32_t kEnemyA[4] = {0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u};
static const uint32_t kEnemyB[4] = {0x55555555u, 0x66666666u, 0x77777777u, 0x88888888u};

static void TestXpBothPaid()
{
    // The normal case measured in the design: each machine's own copy of the death runs the
    // game's award, so both players already have it and NOTHING must cross.
    KillXp k;
    CHECK(k.LocalDeath(kEnemyA, 1000, 0), "xp/a death starts a measurement");
    XpOut out[4];
    CHECK(k.Tick(100, true, 1000, out, 4) == 0, "xp/nothing is decided inside the settle window");
    // The game pays 40 at t=200.
    int n = k.Tick(400, true, 1040, out, 4);
    CHECK(n == 1 && out[0].action == XP_SEND_AWARD && out[0].xp == 40,
          "xp/the machine that was paid reports the amount it measured");
    CHECK(k.Tick(800, true, 1040, out, 4) == 0, "xp/and reports it once");

    // The partner reports the same death.  This machine's own game already paid, so the
    // report must NOT double it.
    CHECK(k.PeerAward(kEnemyA, 1, 40, 900), "xp/the partner's report is recorded");
    CHECK(k.Tick(1000, true, 1040, out, 4) == 0,
          "xp/a report that arrives after a local award pays nothing");
}

static void TestXpOnlyOwnerPaid()
{
    // The failure the task names ("the partner's copy may or may not award"): only the
    // machine that ran the death was paid, so the other must pay the measured amount.
    //
    // A machine whose own game paid nothing has nothing to report: silence is the honest
    // message, so no award crosses from it.
    KillXp z;
    z.LocalDeath(kEnemyA, 500, 0);
    XpOut out[4];
    CHECK(z.Tick(400, true, 500, out, 4) == 0,
          "xp/a machine its game did not pay reports nothing");

    // The receiving side: no local award, and the partner says 40.
    KillXp peer;
    peer.LocalDeath(kEnemyA, 500, 0);
    peer.PeerAward(kEnemyA, 1, 40, 10);
    int n = peer.Tick(400, true, 500, out, 4);
    CHECK(n == 1 && out[0].action == XP_PAY_LOCAL && out[0].xp == 40,
          "xp/the machine the game did not pay pays the partner's measured amount");
    CHECK(peer.Tick(800, true, 540, out, 4) == 0, "xp/and pays it once");
}

static void TestXpReportBeforeDeath()
{
    // Order reversal: the datagram arrives before the local death (the enemy's copy on
    // this machine has not died yet).  It must wait for the measurement, not pay at once.
    KillXp k;
    CHECK(k.PeerAward(kEnemyA, 1, 40, 0), "order/the report creates a record");
    XpOut out[4];
    CHECK(k.Tick(100, true, 0, out, 4) == 0, "order/it does not pay inside the grace window");
    // The local death arrives and its game pays nothing.
    CHECK(k.LocalDeath(kEnemyA, 0, 200), "order/the local death attaches to the record");
    int n = k.Tick(700, true, 0, out, 4);
    CHECK(n == 1 && out[0].action == XP_PAY_LOCAL && out[0].xp == 40,
          "order/the award is paid once the local measurement settles");
    CHECK(k.Tick(900, true, 0, out, 4) == 0, "order/and only once");
}

static void TestXpNoLocalCopy()
{
    // The enemy has no copy on this machine at all, so no death is ever observed.  The
    // award is paid after the bounded grace window, and the caller is given the action so
    // it can log that it was not backed by a measurement.
    KillXp k;
    k.PeerAward(kEnemyB, 1, 25, 0);
    XpOut out[4];
    CHECK(k.Tick(kRemoteGraceMs - 1, true, 0, out, 4) == 0, "noremote/nothing inside the grace window");
    int n = k.Tick(kRemoteGraceMs + 1, true, 0, out, 4);
    CHECK(n == 1 && out[0].action == XP_PAY_LOCAL && out[0].xp == 25,
          "noremote/paid after the grace window");
}

static void TestXpDedup()
{
    // The game writes its persistence record several times per death (measured: five times
    // in 2.5 s).  Those are ONE death.
    KillXp k;
    XpOut out[4];
    CHECK(k.LocalDeath(kEnemyA, 10, 0), "dup/the first record starts the measurement");
    CHECK(!k.LocalDeath(kEnemyA, 10, 100), "dup/the second is the same death");
    CHECK(!k.LocalDeath(kEnemyA, 10, 3000), "dup/the fifth is still the same death");
    CHECK(k.Tick(3400, true, 50, out, 4) == 1, "dup/one measurement, one decision");
    CHECK(out[0].action == XP_SEND_AWARD && out[0].xp == 40,
          "dup/the delta is measured from the death, not from the first record write");

    // A reliable retransmit of the partner's report: the same ordinal must not pay twice.
    KillXp p;
    p.PeerAward(kEnemyA, 3, 40, 0);
    CHECK(p.PeerAward(kEnemyA, 3, 40, 50) == false, "dup/a replayed ordinal is refused");
    CHECK(p.Tick(kRemoteGraceMs + 60, true, 0, out, 4) == 1, "dup/and pays once");

    // A respawned kill of the same enemy is a NEW award.
    KillXp r;
    r.LocalDeath(kEnemyA, 100, 0);
    r.Tick(400, true, 100, out, 4);
    CHECK(r.LocalDeath(kEnemyA, 100, kSameDeathMs + 1), "dup/a later death is a new record");
    int n = r.Tick(kSameDeathMs + 500, true, 140, out, 4);
    CHECK(n == 1 && out[0].action == XP_SEND_AWARD && out[0].xp == 40,
          "dup/and reports its own award");
}

static void TestXpNoCounter()
{
    // No readable XP counter: the lane must do nothing rather than invent an award.
    KillXp k;
    k.LocalDeath(kEnemyA, 0, 0);
    XpOut out[4];
    CHECK(k.Tick(400, false, 0, out, 4) == 0, "nocounter/a local death cannot be measured");
    CHECK(k.Count() == 0, "nocounter/the unverifiable record is dropped");
    // A report that cannot be verified is not paid either.
    KillXp p;
    p.PeerAward(kEnemyA, 1, 40, 0);
    CHECK(p.Tick(kRemoteGraceMs + 1, false, 0, out, 4) == 0,
          "nocounter/a report cannot be settled against an unreadable counter");
}

static void TestXpReload()
{
    // A reload lowers the counter.  The delta is clamped so a load is never an "award",
    // and there is nothing to report because nothing was paid.
    KillXp k;
    k.LocalDeath(kEnemyA, 5000, 0);
    XpOut out[4];
    CHECK(k.Tick(400, true, 100, out, 4) == 0, "reload/a falling counter is not an award");
    CHECK(k.PaidFor(kEnemyA) == 0, "reload/and the measurement is recorded as zero");
}

static void TestXpOverflow()
{
    KillXp k;
    uint32_t g[4];
    for (int i = 0; i < kMaxDeathXp; ++i) {
        g[0] = (uint32_t)(i + 1); g[1] = 0; g[2] = 0; g[3] = 0;
        k.LocalDeath(g, 0, 0);
    }
    g[0] = 999; g[1] = 0; g[2] = 0; g[3] = 0;
    CHECK(k.LocalDeath(g, 0, 0) == false, "overflow/a full table refuses the record");
    CHECK(k.Overflowed(), "overflow/and says so");
    // The table recycles: an old record's slot is reused rather than the lane dying.
    for (int i = 0; i < kMaxDeathXp; ++i) {
        g[0] = (uint32_t)(i + 1); g[1] = 0; g[2] = 0; g[3] = 0;
        XpOut out[4];
        k.Tick(kRecordLifeMs + 1, true, 0, out, 4);
    }
    g[0] = 999; g[1] = 0; g[2] = 0; g[3] = 0;
    CHECK(k.LocalDeath(g, 0, kRecordLifeMs + 2), "overflow/an expired slot is reused");
}

// ---------------------------------------------------------------------------
// 4. The mirrored save
// ---------------------------------------------------------------------------
static void TestSaveVerbs()
{
    CHECK(IsSaveVerb("SaveGameToDisk"), "verb/SaveFunctions' writer is a save");
    CHECK(IsSaveVerb("GI_SaveGameToDisk"), "verb/the game instance's writer is a save");
    CHECK(IsSaveVerb("Save Game to Disk"), "verb/the pinned spelling is a save");
    CHECK(!IsSaveVerb("SaveNamedData"), "verb/a named-data write is not a slot save");
    CHECK(!IsSaveVerb("AddItemToInventory"), "verb/an inventory write is not a save");
    CHECK(!IsSaveVerb(nullptr), "verb/null is not a save");

    // qdsuspend.h owns these; mirroring one is the rollback that lane exists to prevent.
    CHECK(!IsSaveVerb("WriteSuspendSave"), "verb/a suspend save is refused");
    CHECK(!IsSaveVerb("GI_WriteSuspendSaveImmediately"), "verb/the immediate suspend write is refused");
    CHECK(!IsSaveVerb("SuspendSaveLoop"), "verb/the suspend loop is refused");
    CHECK(IsSuspendSaveVerb("WriteSuspendSave"), "verb/named as a suspend verb");

    // Per-machine settings, not progression.
    CHECK(!IsSaveVerb("GI_SaveSystemSaveToDisk"), "verb/a system save is refused");
    CHECK(!IsSaveVerb("QueueSystemSave"), "verb/the queued system save is refused");
    CHECK(IsSystemSaveVerb("GI_QueueSystemSave"), "verb/named as a system save");

    // [live-ip2] THE TRIGGER IS A SUPERSET OF THE MIRRORABLE NAMES.  MEASURED round 3: a
    // checkpoint rest -- the save a player actually makes -- writes
    // `World0_Suspend_N.sav`, so its dispatch names are suspend names, which `IsSaveVerb`
    // refuses by design.  The two questions are different and were answered by one list,
    // which is why the detector never saw the player's own save.
    CHECK(IsProgressSaveTrigger("SaveGameToDisk"), "trigger/a slot save triggers a mirror");
    CHECK(IsProgressSaveTrigger("WriteSuspendSave"),
          "trigger/a rest's suspend write triggers a mirror");
    CHECK(IsProgressSaveTrigger("GI_WriteSuspendSaveImmediately"),
          "trigger/the immediate suspend write triggers a mirror");
    CHECK(!IsProgressSaveTrigger("CancelSuspendSaveLoop"),
          "trigger/cancelling a save does not trigger one");
    CHECK(!IsProgressSaveTrigger("UnsafeDeleteSuspendSavesOnSlot"),
          "trigger/deleting a save does not trigger one");
    CHECK(!IsProgressSaveTrigger("SaveNamedData"),
          "trigger/a named-data write is still not a save");
    CHECK(!IsProgressSaveTrigger("AddItemToInventory"),
          "trigger/an inventory write is still not a save");
    CHECK(!IsProgressSaveTrigger(nullptr), "trigger/null does not trigger");
    CHECK(IsSuspendTrigger("WriteSuspendSave"), "trigger/a suspend write is the rest route");
    CHECK(!IsSuspendTrigger("SaveGameToDisk"), "trigger/a slot save is the main-slot route");
}

static void TestSaveGate()
{
    SaveGate g = {};
    CHECK(!g.Blocked(), "gate/clear is not blocked");
    g.loading = true;
    CHECK(g.Blocked() && !strcmp(g.Why(), "a world load is in progress"), "gate/a load blocks");
    g.loading = false; g.travelling = true;
    CHECK(g.Blocked() && g.Why()[0], "gate/travel blocks and names itself");
    g.travelling = false; g.dead = true;
    CHECK(g.Blocked(), "gate/death blocks");
    g.dead = false; g.cutscene = true;
    CHECK(g.Blocked(), "gate/a cutscene blocks");
    g.cutscene = false; g.menu = true;
    CHECK(g.Blocked(), "gate/a menu blocks");
    g.menu = false; g.noSave = true;
    CHECK(g.Blocked() && !strcmp(g.Why(), "no active save"), "gate/no save object blocks");
    g.noSave = false; g.suspendHold = true;
    CHECK(g.Blocked(), "gate/a suspend write in progress blocks");
}

static void TestSaveMirror()
{
    SaveBoard b;
    SaveGate clear = {};
    const char* why = "";

    // A local save goes out.
    CHECK(b.OnLocalSave(0), "save/a local save is announced");
    CHECK(b.LastLocalMs() == 0, "save/and recorded");

    // The partner saved: this machine owes a save.
    CHECK(b.OnPeerSave(1, 1, 100), "save/the partner's save owes one here");
    CHECK(b.WantedRoute() == ROUTE_SLOT, "save/the request names the main-slot route");
    CHECK(b.Want(), "save/the want is visible");
    CHECK(!b.TakeSave(200, MakeBlocked(/*loading*/ true, false, false, false, false, false, false), &why),
          "save/a load defers it");
    CHECK(why[0] != 0, "save/and names the reason");
    CHECK(b.Want(), "save/the want survives the deferral");
    CHECK(b.TakeSave(300, clear, &why), "save/it runs when the gate is clear");
    CHECK(!b.Want(), "save/and the want is consumed");
    CHECK(b.OwnSaveActive() == false, "save/the bracket is the caller's to open");

    // THE ECHO.  Running that save dispatches the local writer -- in this build MORE THAN
    // ONE of them, nested (`GI_SaveGameToDisk` runs `Save Game to Disk`, which runs
    // `SaveGameToDisk`), which is why the guard is a BRACKET and not a count.
    b.BeginOwnSave(300);
    CHECK(b.OwnSaveActive(), "echo/the bracket is open");
    CHECK(!b.OnLocalSave(305), "echo/the outer dispatch is swallowed");
    CHECK(!b.OnLocalSave(306), "echo/the nested one is swallowed too");
    CHECK(!b.OnLocalSave(307), "echo/and a third");
    b.EndOwnSave(307);
    CHECK(!b.OwnSaveActive(), "echo/the bracket is closed");
    CHECK(b.Echoes() == 3, "echo/all three were counted as echoes");

    // A writer that defers its dispatch by a frame is still covered by the short window...
    CHECK(!b.OnLocalSave(400), "echo/a deferred echo inside the window is swallowed");

    // ...and the player's own save after it is still announced.  This is the property a
    // time-only guard could not have: a real save must never be lost.
    CHECK(b.OnLocalSave(307 + kSaveEchoMs + 1), "echo/the player's own later save still goes out");

    // The partner repeating the same request (a reliable retransmit) does not queue two saves.
    SaveBoard c;
    CHECK(c.OnPeerSave(7, 1, 0), "dedup/the first request is honoured");
    CHECK(c.TakeSave(10, clear, &why), "dedup/and served");
    CHECK(!c.OnPeerSave(7, 1, 10), "dedup/the retransmit is refused by ordinal");
    CHECK(!c.Want(), "dedup/so a second save is not owed");

    // A stale ordinal is refused too.
    CHECK(!c.OnPeerSave(6, 1, 5000), "dedup/an older ordinal is refused");
}

static void TestSaveGiveUp()
{
    // A save that can never run must be REPORTED, not remembered for ever.
    SaveBoard b;
    SaveGate blocked = {};
    blocked.menu = true;
    const char* why = "";
    b.OnPeerSave(1, 1, 0);
    CHECK(!b.TakeSave(1000, blocked, &why), "giveup/a menu keeps it waiting");
    CHECK(!b.TakeSave(kSaveDeferMs + 2000, blocked, &why), "giveup/the deadline expires");
    CHECK(b.TakeGaveUp(), "giveup/the give-up is reported once");
    CHECK(!b.TakeGaveUp(), "giveup/and only once");
    CHECK(!b.Want(), "giveup/the want is dropped");
}

static void TestSaveManyLocal()
{
    // Three local saves in a row are three announcements; nothing suppresses them.
    SaveBoard b;
    CHECK(b.OnLocalSave(0), "many/one");
    CHECK(b.OnLocalSave(10), "many/two");
    CHECK(b.OnLocalSave(20), "many/three");
}

static void TestSaveRoute()
{
    // [live-ip2] MEASURED round 3: on this build the player's real save is the checkpoint
    // REST, which writes `World0_Suspend_N.sav`, so a partner answering a rest with a
    // main-slot write would have saved a file its own game never resumes from.  The route
    // travels with the request and the receiver runs its OWN matching writer.
    SaveBoard b;
    SaveGate clear = {};
    const char* why = "";
    CHECK(b.WantedRoute() == ROUTE_SLOT, "route/nothing owed means the main slot");
    CHECK(b.OnPeerSave(3, ROUTE_SUSPEND, 100), "route/a rest request is honoured");
    CHECK(b.WantedRoute() == ROUTE_SUSPEND, "route/and the rest route is remembered");
    CHECK(b.TakeSave(200, clear, &why), "route/it runs");
    CHECK(b.WantedRoute() == ROUTE_SUSPEND,
          "route/the route survives until the writer is chosen");

    // A zero route is "unspecified" and means the main slot, so a body from a sender that
    // predates the field still mirrors.
    SaveBoard c;
    CHECK(c.OnPeerSave(1, 0, 0), "route/an unspecified route is honoured");
    CHECK(c.WantedRoute() == ROUTE_SLOT, "route/and means the main slot");
}

// ---------------------------------------------------------------------------
int main()
{
    TestItemPath();
    TestRemoval();
    TestHostView();
    TestXpBothPaid();
    TestXpOnlyOwnerPaid();
    TestXpReportBeforeDeath();
    TestXpNoLocalCopy();
    TestXpDedup();
    TestXpNoCounter();
    TestXpReload();
    TestXpOverflow();
    TestSaveVerbs();
    TestSaveGate();
    TestSaveMirror();
    TestSaveGiveUp();
    TestSaveManyLocal();
    TestSaveRoute();

    printf("progression: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
