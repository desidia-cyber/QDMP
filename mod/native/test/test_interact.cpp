// test_interact.cpp -- the world-interact classification and its call plan.
//
// WHY THESE ASSERTIONS
// --------------------
// The lane's dangerous mistakes are all decisions made from a CLASS NAME and a
// direction, and each one is invisible until a live run wastes a session:
//
//   classification  a `DoorLamp`, a door KEY, a dining plate, a UI button, a dialogue
//                   skull NPC and a lift lever all contain a category word.  The lift is
//                   the one that already cost a crash (Session 26), so it is tested.
//   state property  a door's `DoorClosed` is inverted; a breakable has no bool on the
//                   base class and falls back to `BrokeBarricade`; a lever is `Activated`.
//   direction       a door must be closable (the old lane refused every 0), a broken
//                   barrel must not come back.
//   plan            `SetToOpenState` is the SAME visual for open and close (both the
//                   game's OpenDoor and CloseDoor call it), and no plan may ever name the
//                   interaction verb or a payout.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "qdinteract.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, name)                                                          \
    do {                                                                           \
        if (cond) { ++g_pass; }                                                    \
        else { ++g_fail; printf("FAIL %s  (%s:%d)\n", name, __FILE__, __LINE__); } \
    } while (0)

using namespace qd;

static void ShapeIs(const char* cls, objects::Shape want)
{
    objects::Shape got = interact::ShapeOfClass(cls);
    if (got != want) {
        printf("  shape %-34s got=%d want=%d\n", cls, (int)got, (int)want);
    }
    CHECK(got == want, cls);
}

static void PropIs(objects::Shape s, const char* name, bool inverted, int which)
{
    interact::Prop p = (which == 0) ? interact::StateProp(s)
                      : (which == 1) ? interact::StateAlt(s)
                                     : interact::StateAlt2(s);
    bool ok = (name == nullptr) ? (p.name == nullptr) : (p.name && strcmp(p.name, name) == 0);
    if (ok && name) ok = (p.inverted == inverted);
    if (!ok) {
        printf("  prop sh=%d which=%d got=%s inv=%d want=%s inv=%d\n", (int)s, which,
               p.name ? p.name : "(null)", (int)p.inverted, name ? name : "(null)",
               (int)inverted);
    }
    CHECK(ok, "state property");
}

static bool StepNamed(const interact::Step* st, int n, const char* fn, uint8_t mode)
{
    for (int i = 0; i < n; ++i) {
        if (strcmp(st[i].fn, fn) == 0) return st[i].mode == mode;
    }
    return false;
}

int main()
{
    // ---- classification: every door family ----------------------------------
    const char* doors[] = {
        "Door_Base_C", "Shop_Door_00_BP_C", "SecretZoneDoor_BP_C",
        "SecretZoneDoor_Blue_BP_C", "SecretZoneDoor_Green_BP_C",
        "SecretZoneDoor_Red_BP_C", "SZ_Door_02_BP_C", "SZ_BoringDoor_00_BP_C",
        "SZ_JailDoor_BP_C", "Lib_FrontDoor_BP_C", "Lib_IntDoor_00_BP_C",
        "Lib_MidDoor_BP1_C", "Lib_TrapDoors_00_BP_C", "Lib_Door_01_BP_C",
        "LibGate_00_BP_C", "LibGate_00_BP_Circle_C", "Lib_BossDoor_BP_C",
        "Lib_TreasureGate_BP_C", "KitchenDoor_BP_C", "M_DoorSmall_00_BP_C",
        "M_WodasGate_00_BP_C", "EggDoor_BP_C", "Queen_Door_Bp_C",
        "CastleFrontDoor_BP_C", "Castle_Door_Int_BP_C", "OW_BridgeDoor_C",
        "OW_TP_Interior_Door_C", "OW_TP_Shutter_BP_C", "OW_Tower_RobotShutter_BP_C",
        "AquaDoor_00_C", "AquaGate_00_BP_C", "AquaGate_01_BP_C", "AquaBossDoor_BP_C",
        "AQR_BossExitDoor_C", "AqrStatuePuzzleDoor_C", "GraveyardBoss_Gate_C",
        "DesBoss_Gate_C", "Necro_Boss_Door_C", "JosamiPrisonDoor_C", "PT_Door_C",
        "Maus_QG_Door_C", "SG_Door_Beepee_C", "SwordShrineGate_BP_C",
        "ForbiddenDoor_BaseClass_C", "Forbidden_Door_00_BP_C", "OfficeDoor_00_BP_C",
        "TowerDoor_00_BP_C", "BloodRitualDoor_BP_C", "JigekiDoor_BP_C",
        "ROS_Door_BP_C", "Ros_Cage_Gate_BP_C", "DAquaDoor_01_C",
        // A secret wall is DOOR-shaped: it animates out of the wall.
        "SecretWall_C", "ScienceSecretWall_C",
    };
    for (unsigned i = 0; i < sizeof(doors) / sizeof(doors[0]); ++i)
        ShapeIs(doors[i], objects::SH_DOOR);

    // ---- classification: every breakable ------------------------------------
    const char* breakables[] = {
        "Permabreakable_base_C", "Barrel_C", "Barrel_Hard_C", "Barrel_Mossy_C",
        "Crate_C", "Crate_Hard_C", "Crate_Mossy_C", "ScifiCrate_BP_C",
        "ScifiCrate_Hard_BP_C", "ScifiCrate_Rusty_BP_C", "BreakableBarricade_C",
        "Wellcover_Breakable_C", "Pot_Base_C", "Pot_Fat_C", "Pot_Fat_2_C",
        "Pot_Fat_Sci_C", "Pot_Small_Base_C", "Pot_Tall_Base_C",
        "M_Kitchen_Pot_00_BP_C", "M_Kitchen_Pot_01a_BP1_C",
        "OW_BigCrate_BP_C", "OW_MachineBarrel_BP_C", "or_pot_tall_bp_C",
    };
    for (unsigned i = 0; i < sizeof(breakables) / sizeof(breakables[0]); ++i)
        ShapeIs(breakables[i], objects::SH_BARRICADE);

    // ---- classification: secret mechanisms ----------------------------------
    const char* mechs[] = {
        "Lever_C", "OW_Lever_00_BP_C", "MausoLever_00_C", "AquaLever_00_C",
        "Lib_Switchbox_BP_C", "RuneButton_C", "Gargoyle_Switch_C",
        "Wodaso_Rune_BP_C",
    };
    for (unsigned i = 0; i < sizeof(mechs) / sizeof(mechs[0]); ++i)
        ShapeIs(mechs[i], objects::SH_ACTIVATABLE);

    // ---- classification: the other two shapes the lane still carries --------
    ShapeIs("Chest_C", objects::SH_CHEST);
    ShapeIs("SG_Chest_BP_C", objects::SH_CHEST);
    ShapeIs("MimicChest_C", objects::SH_CHEST);
    ShapeIs("ItemPickup_Base_C", objects::SH_PICKUP);
    ShapeIs("ItemDrop_BP_C", objects::SH_PICKUP);

    // ---- classification: the rejects that contain a category word -----------
    const char* rejects[] = {
        "DoorLamp_00_BP_C",            // a lamp
        "SwordShrine_Gate_Key_C",      // a key
        "AquaDoorKey_BP_C",            // a key
        "SecretSkullDoor_NPC_BP_C",    // a dialogue NPC
        "SecretSkullDoor_CheckForReward_C",
        "UIButtonBindingDA_C",         // a widget
        "DefaultButton_Error_C",
        "WishlistButton_C",
        "DS_Plate_BP_C",               // a dining plate (not a pressure plate)
        "DoorOpenShake_C",             // a camera shake
        "SZFrame_NoDoor_BP_C",         // a frame with no door
        "QueenDoortempBlocker_C",      // a collision blocker
        "Barrel_Broken_Mesh_C",        // a broken mesh asset, not an actor
        "Barrel_Remnant_C",
        "Lib_LiftSwitch_BP_C",         // Session 26 crash families
        "OW_Lift_Lever_00_BP_C",
        "LabElevatorSwitch_C",
        "OW_Lever_00_lift_BP8_C",
        "SkullDoorQuestStateManager_C",
        "DoorQuestStateSaveData_C",
        "ReturnThing_C",               // "urn" is a substring, not a word
        "Spotlight_C",                 // "pot" is a substring, not a word
        "",
    };
    for (unsigned i = 0; i < sizeof(rejects) / sizeof(rejects[0]); ++i)
        ShapeIs(rejects[i], objects::SH_NONE);

    // ---- state property per family ------------------------------------------
    PropIs(objects::SH_CHEST, "Opened", false, 0);
    PropIs(objects::SH_DOOR, "Opened", false, 0);
    PropIs(objects::SH_DOOR, "DoorClosed", true, 1);      // inverted
    PropIs(objects::SH_DOOR, "PreviewOpen", false, 2);
    PropIs(objects::SH_BARRICADE, "BrokeBarricade", false, 0);
    PropIs(objects::SH_BARRICADE, "Broken", false, 1);
    PropIs(objects::SH_BARRICADE, "PreviewBroken", false, 2);
    PropIs(objects::SH_ACTIVATABLE, "Activated", false, 0);
    PropIs(objects::SH_ACTIVATABLE, "bActivated", false, 1);
    PropIs(objects::SH_ACTIVATABLE, "ToggledOn", false, 2);
    PropIs(objects::SH_PICKUP, "PickedUp", false, 0);
    PropIs(objects::SH_PICKUP, "Acquired", false, 1);

    // ---- direction policy ---------------------------------------------------
    CHECK(interact::Bidirectional(objects::SH_DOOR), "door is bidirectional");
    CHECK(interact::Bidirectional(objects::SH_ACTIVATABLE), "lever is bidirectional");
    CHECK(!interact::Bidirectional(objects::SH_BARRICADE), "break is one-way");
    CHECK(!interact::Bidirectional(objects::SH_CHEST), "chest is one-way");
    CHECK(!interact::Bidirectional(objects::SH_PICKUP), "pickup is one-way");

    // a door may close; a broken barrel may not come back; an identical value is skipped
    CHECK(interact::ApplyAllowed(0, 1, true, objects::SH_DOOR), "door closes");
    CHECK(!interact::ApplyAllowed(0, 0, true, objects::SH_DOOR), "door already shut");
    CHECK(!interact::ApplyAllowed(0, 1, true, objects::SH_BARRICADE), "barrel stays broken");
    CHECK(interact::ApplyAllowed(1, 0, true, objects::SH_BARRICADE), "barrel breaks");
    CHECK(!interact::ApplyAllowed(1, 1, true, objects::SH_DOOR), "door already open");
    CHECK(interact::ApplyAllowed(1, 0, false, objects::SH_DOOR), "door no local view");

    // ---- the call plan ------------------------------------------------------
    interact::Step st[8];
    for (int s = 0; s <= (int)objects::SH_ACTIVATABLE; ++s) {
        for (int want = 0; want < 2; ++want) {
            int n = interact::Plan((objects::Shape)s, (uint8_t)want, st, 8);
            if (!interact::PlanLegal(st, n)) {
                printf("  illegal plan shape=%d want=%d\n", s, want);
            }
            CHECK(interact::PlanLegal(st, n), "plan is legal");
            int m = interact::Plan((objects::Shape)s, (uint8_t)want, st, 2);
            CHECK(m <= 2, "plan honours cap");
        }
    }

    int n = interact::Plan(objects::SH_DOOR, 1, st, 8);
    CHECK(StepNamed(st, n, "SetToOpenState", interact::M_BOOL), "door open visual");
    CHECK(StepNamed(st, n, "Activate", interact::M_OPEN), "door open fallback");
    CHECK(!StepNamed(st, n, "OpenDoor", interact::M_OPEN), "door never uses OpenDoor");

    n = interact::Plan(objects::SH_DOOR, 0, st, 8);
    CHECK(n > 0 && strcmp(st[0].fn, "SetToOpenState") == 0, "door close is the same visual");
    CHECK(StepNamed(st, n, "Deactivate", interact::M_CLOSE), "door close fallback");
    CHECK(!StepNamed(st, n, "CloseDoor", interact::M_CLOSE), "door never uses CloseDoor");

    n = interact::Plan(objects::SH_BARRICADE, 1, st, 8);
    CHECK(n == 1 && strcmp(st[0].fn, "Break") == 0, "breakable uses Break");
    n = interact::Plan(objects::SH_BARRICADE, 0, st, 8);
    CHECK(n == 0, "no un-break plan");

    n = interact::Plan(objects::SH_ACTIVATABLE, 1, st, 8);
    CHECK(n > 0 && strcmp(st[0].fn, "Activate") == 0, "lever activates");
    n = interact::Plan(objects::SH_ACTIVATABLE, 0, st, 8);
    CHECK(n > 0 && strcmp(st[0].fn, "Deactivate") == 0, "lever deactivates");

    n = interact::Plan(objects::SH_CHEST, 1, st, 8);
    CHECK(n > 0 && strcmp(st[0].fn, "SetToFinalState") == 0, "chest final state");
    n = interact::Plan(objects::SH_CHEST, 0, st, 8);
    CHECK(n == 0, "no chest un-open");
    CHECK(interact::Plan(objects::SH_PICKUP, 1, st, 8) == 0, "pickup has no plan");
    CHECK(interact::Plan(objects::SH_NONE, 1, st, 8) == 0, "none has no plan");

    // ---- forbidden names ----------------------------------------------------
    const char* bad[] = {
        "Interact", "OpenDoor", "CloseDoor", "InsertKey", "TryUseKey",
        "CheckRequiredItems", "Grant", "GrantItemsAndActivate", "AddItemToInventory",
        "TryApplyDamage", "Open", "OpenKeymenu", "TryConsumingItemFromInventory",
    };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
        CHECK(interact::Forbidden(bad[i]), bad[i]);
    const char* ok[] = {
        "SetToOpenState", "SetToFinalState", "SetTOEndState", "Activate",
        "Deactivate", "Break", "ForceOpen", "SetActorEnableCollision",
    };
    for (unsigned i = 0; i < sizeof(ok) / sizeof(ok[0]); ++i)
        CHECK(!interact::Forbidden(ok[i]), ok[i]);

    // ---- the savable lane's explicit-animation decision ---------------------
    CHECK(interact::NeedsExplicitAnim(objects::SH_DOOR, 0, 1), "door open animates");
    CHECK(interact::NeedsExplicitAnim(objects::SH_DOOR, 1, 0), "door close animates");
    CHECK(!interact::NeedsExplicitAnim(objects::SH_DOOR, 1, 1), "already-open door stays");
    CHECK(!interact::NeedsExplicitAnim(objects::SH_DOOR, 0, 0), "already-shut door stays");
    CHECK(!interact::NeedsExplicitAnim(objects::SH_BARRICADE, 0, 1),
          "breakable restore already calls Break");
    CHECK(!interact::NeedsExplicitAnim(objects::SH_CHEST, 0, 1), "chest restore is the visual");

    // ---- the words that must not be matched as substrings -------------------
    CHECK(interact::HasWordCI("Pot_Fat_C", "Pot"), "pot is a word here");
    CHECK(!interact::HasWordCI("ReturnThing", "Urn"), "urn is not a word in Return");
    CHECK(!interact::HasWordCI("Spotlight", "Pot"), "pot is not a word in Spotlight");
    CHECK(interact::HasWordCI("or_pot_tall_bp_C", "pot"), "pot between underscores");

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
