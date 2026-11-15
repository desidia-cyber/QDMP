// qdinteract.h -- the WORLD-INTERACT coverage layer: every door, breakable and secret
// mechanism, classified generically and driven through the game's OWN functions.
//
// WHY THIS EXISTS
// ---------------
// `qdobjectstate.h` proved the shape of the lane (a placed actor's cooked path hash, its
// own state bool, a visual/restore entry that is never the interaction verb) but its
// class survey was deliberately narrow: chest, door, pickup, barricade, activatable.  A
// pass over the shipped pak finds the rest of the vocabulary -- barrels, crates, pots,
// urns, destructible barricades, six families of secret wall, trapdoors, portcullis-style
// gates, shutters, one-sided shop doors, double doors, jail doors, boss-arena gates and
// the lever/switch/rune/button mechanisms that open them.  Those are the objects a co-op
// session is *made of*: a door that opens on one screen and stays shut on the other ends
// the run more often than any combat bug.
//
// WHAT THE SHIPPED PACKAGES SAY
// -----------------------------
// Derived by `analysis/pak/code/probes/interact_classes.py` (re-run it; it prints the
// table and the rejects).  117 BlueprintGeneratedClass actors classify, in three
// families:
//
//   DOORS (52)          Door_Base_C and its subclasses: Shop_Door_00_BP_C (one-sided,
//                       keeps only the `ShopDoorOpened` flag), SecretZoneDoor_*_BP_C,
//                       SZ_Door_02_BP_C, SZ_BoringDoor_00_BP_C, SZ_JailDoor_BP_C,
//                       Lib_FrontDoor/IntDoor/MidDoor/TrapDoors, LibGate_00_BP_C,
//                       Lib_BossDoor_BP_C, Lib_TreasureGate_BP_C, KitchenDoor_BP_C,
//                       M_DoorSmall_00_BP_C, M_WodasGate_00_BP_C, EggDoor_BP_C,
//                       Queen_Door_Bp_C, CastleFrontDoor_BP_C, Castle_Door_Int_BP_C,
//                       OW_BridgeDoor_C, OW_TP_Interior_Door_C, OW_TP_Shutter_BP_C,
//                       OW_Tower_RobotShutter_BP_C, AquaDoor_00_C, AquaGate_00/01_BP_C,
//                       AquaBossDoor_BP_C, AQR_BossExitDoor_C, AqrStatuePuzzleDoor_C,
//                       GraveyardBoss_Gate_C, DesBoss_Gate_C, Necro_Boss_Door_C,
//                       JosamiPrisonDoor_C, PT_Door_C, Maus_QG_Door_C, SG_Door_Beepee_C,
//                       SwordShrineGate_BP_C, ForbiddenDoor_BaseClass_C and its
//                       Forbidden/Office/Tower/BloodRitual subclasses, JigekiDoor_BP_C,
//                       ROS_Door_BP_C, Ros_Cage_Gate_BP_C, DAquaDoor_01_C.
//   BREAKABLES (33)     Permabreakable_base_C and Barrel/Crate/ScifiCrate (+ _Hard/_Mossy/
//                       _Rusty), BreakableBarricade_C, Wellcover_Breakable_C, Pot_Base_C
//                       and the Pot_Fat/Small/Tall/Sci families, M_Kitchen_Pot_*_BP_C,
//                       OW_BigCrate_BP_C, OW_MachineBarrel_BP_C, or_pot_tall_bp_C.
//   SECRET/MECH (32)    SecretWall_C and ScienceSecretWall_C (both are DOOR-shaped: they
//                       animate out of the wall), and the mechanisms: Lever_C,
//                       OW_Lever_00_BP_C, MausoLever_00_C, AquaLever_00_C,
//                       Lib_Switchbox_BP_C, RuneButton_C, DaggerButton_*, gate_switch_00,
//                       Gargoyle_Switch_C, Wodaso_Rune_BP_C.
//
// The dangerous rejects are named by the probe and enforced here: a `DoorLamp`, a
// `SwordShrine_Gate_Key`, a `DS_Plate_BP` (dining plate), a `SecretSkullDoor_NPC_BP`,
// a `UIButtonBindingDA`, a `DoorOpenShake` and -- the one that cost a crash -- a
// lift/elevator lever (`OW_Lever_00_lift_BP8`, `LabElevatorSwitch`, `OW_Lift_Lever_00_BP`).
//
// THE STATE PROPERTY IS PER FAMILY
// --------------------------------
// Measured from the class name tables:
//   Door_Base_C            Opened / DoorClosed / CurrentActivationCount
//   SecretWall_C           Opened / PreviewOpen
//   Lever_C                Activated / ToggledOn / ActivatedFromLoad
//   BreakableBarricade_C   BrokeBarricade
//   Permabreakable_base_C  no bool at all -- its state lives in the SavableActorStates
//                          container, which is why ACTOR_STATES owns it and this lane
//                          only logs the class.
//
// THE APPLY IS THE CLASS' OWN ANIMATION, BOTH WAYS
// ------------------------------------------------
// `OpenDoor` and `CloseDoor` on Door_Base both call the SAME no-container visual
// `SetToOpenState` (bytecode measured: both exports reference it), and `ShapeOfClass`'s
// old "never apply a 0" rule therefore threw away half of a door's life.  The plan below
// calls the visual with the WANTED bool, which is exactly what the game's own open/close
// verbs do, so the partner sees the same opening/closing timeline rather than a snap.
// A class that only implements `Activate`/`Deactivate` (Lever_C, OW_Tower_RobotShutter)
// gets that pair.  A breakable gets `Break` -- the game's own break effect -- and is
// never un-broken; `Restore State From Save Data` is the only path that can do that, and
// it belongs to the savable lane.
//
// NOTHING HERE IS AN INTERACTION VERB OR A PAYOUT
// ----------------------------------------------
// `Forbidden` refuses `Interact`, `OpenDoor`, `CloseDoor`, `InsertKey`, `TryUseKey`,
// `CheckRequiredItems`, `Grant*`, `AddItemToInventory` and `TryApplyDamage` by name
// before a call can be built.  The receiver plays the door; it never presses it, never
// consumes a key, never loots and never rolls a drop.  Breakables are the one deliberate
// exception in spirit: `Break` is the class' visual/state path (the loaded save calls it
// too), and the loot lane -- not this lane -- owns the drops, which stay per-player.
//
// PURE, SO IT CAN BE TESTED WITHOUT THE GAME
// ------------------------------------------
// No socket, no UObject, no allocation.  The classification and the call plan are pure
// functions of a class-name string and a direction; test/test_interact.cpp exercises
// them.  proxy.cpp performs the calls, because only it has the reflection layer.
#pragma once
#include <stdint.h>
#include <string.h>

#include "qdobjectstate.h"   // objects::Shape (the wire shape), objects::HasCI

namespace qd {
namespace interact {

// Categories, derived from the wire shape so a row needs no new byte.
enum Cat : uint8_t {
    CAT_NONE       = 0,
    CAT_DOOR       = 1,
    CAT_BREAKABLE  = 2,
    CAT_ACTIVATABLE = 3,
};

// ---------------------------------------------------------------------------
// Rejection: bookkeeping, helper geometry, UI, effects, keys, lifts
// ---------------------------------------------------------------------------
// Every entry below was a false positive the coverage probe produced.  A class name is
// matched case-insensitively as a SUBSTRING unless noted.  `Key` rejects `AquaDoorKey`
// and `SwordShrine_Gate_Key`; `Lamp` rejects `OW_DoorLamp_00_BP`; `Dining` rejects the
// `DS_Plate_*` dining set; `NPC`/`UI`/`Widget` reject the dialogue skull and the widget
// buttons; `Shake` rejects `DoorOpenShake`; `NoDoor` rejects `SZFrame_NoDoor_BP`.
inline bool Reject(const char* cls)
{
    if (!cls || !cls[0]) return true;
    static const char* const kNo[] = {
        "Manager", "Subsystem", "Component", "Data", "State", "Save", "Interface",
        "Helper", "Getter", "Utils", "Function", "__Delegate", "Default__",
        "_GEN_VARIABLE", "Settings", "Config", "Library", "Audio", "Sound", "Cue",
        "Mesh", "Material", "Texture", "Anim", "Montage", "Skeleton", "Physics",
        "Particle", "Icon", "HINT", "Lamp", "Key", "Dining", "Spotlight",
        "Spawner", "Trigger", "Volume", "Projectile", "Weapon",
        "UI", "Widget", "ButtonStyle", "DefaultButton", "HighlightButton",
        "AspectRatio", "Wishlist", "Binding", "DialogueExit", "NPC",
        "DamageTarget", "Shake", "NoDoor", "CheckForReward", "TurnIn", "Reward",
        "Remnant", "Broken", "Blocker",
    };
    for (unsigned i = 0; i < sizeof(kNo) / sizeof(kNo[0]); ++i) {
        if (objects::HasCI(cls, kNo[i])) return true;
    }
    return false;
}

// Word-boundary test for the short words that are substrings of unrelated names:
// "Return" contains "urn", "Spot" contains "pot", "Jargon" contains "jar".
inline bool HasWordCI(const char* hay, const char* word)
{
    if (!hay || !word || !word[0]) return false;
    for (int i = 0; hay[i]; ++i) {
        int k = 0;
        for (; word[k]; ++k) {
            char a = hay[i + k];
            char b = word[k];
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if (a != b) break;
        }
        if (!word[k]) {
            char before = (i > 0) ? hay[i - 1] : 0;
            char after = hay[i + k];
            bool bOk = !(before >= 'a' && before <= 'z') && !(before >= 'A' && before <= 'Z') &&
                       !(before >= '0' && before <= '9');
            bool aOk = !(after >= 'a' && after <= 'z') && !(after >= 'A' && after <= 'Z') &&
                       !(after >= '0' && after <= '9');
            if (bOk && aOk) return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Classification -- one function, the wire shape out
// ---------------------------------------------------------------------------
// Order matters and is the probe's order: a `SecretZoneDoor` is a door before it is
// anything else, a `MimicChest` stays a chest, and the lift/elevator rejection comes
// before the lever test (Session 26: writing `Activated` on a lift lever faulted the
// Blueprint on the next frame, both roles, entering 500_AUT).
inline objects::Shape ShapeOfClass(const char* cls)
{
    if (!cls || !cls[0]) return objects::SH_NONE;
    if (Reject(cls)) return objects::SH_NONE;

    // NOT world-interact state: the input to the mover lane, and a measured crash.
    if (objects::HasCI(cls, "Lift") || objects::HasCI(cls, "Elevator") ||
        objects::HasCI(cls, "ElevSwitch")) return objects::SH_NONE;

    if (objects::HasCI(cls, "Chest")) return objects::SH_CHEST;

    // A secret wall/door is DOOR-shaped: it animates out of the wall with the same
    // `Opened` bool and a final-state visual.
    if (objects::HasCI(cls, "SecretWall") || objects::HasCI(cls, "SecretDoor") ||
        objects::HasCI(cls, "ScienceSecret") || objects::HasCI(cls, "Illusory") ||
        objects::HasCI(cls, "Illusion") || objects::HasCI(cls, "HiddenWall") ||
        objects::HasCI(cls, "FakeWall") || objects::HasCI(cls, "SecretPassage"))
        return objects::SH_DOOR;

    if (objects::HasCI(cls, "Door") || objects::HasCI(cls, "Gate") ||
        objects::HasCI(cls, "Shutter") || objects::HasCI(cls, "Portcullis") ||
        objects::HasCI(cls, "Trapdoor")) return objects::SH_DOOR;

    if (objects::HasCI(cls, "Pickup") || objects::HasCI(cls, "Drop"))
        return objects::SH_PICKUP;

    if (objects::HasCI(cls, "Breakable") || objects::HasCI(cls, "Barricade") ||
        objects::HasCI(cls, "Barrel") || objects::HasCI(cls, "Crate") ||
        objects::HasCI(cls, "Permabreakable") ||
        HasWordCI(cls, "Pot") || HasWordCI(cls, "Vase") ||
        HasWordCI(cls, "Urn") || HasWordCI(cls, "Jar"))
        return objects::SH_BARRICADE;

    if (objects::HasCI(cls, "Lever") || objects::HasCI(cls, "Switch") ||
        objects::HasCI(cls, "Button") || objects::HasCI(cls, "Valve") ||
        objects::HasCI(cls, "Crank") || objects::HasCI(cls, "Rune") ||
        objects::HasCI(cls, "PressurePlate") || objects::HasCI(cls, "Activatable"))
        return objects::SH_ACTIVATABLE;

    return objects::SH_NONE;
}

inline Cat CatOfClass(const char* cls)
{
    objects::Shape s = ShapeOfClass(cls);
    switch (s) {
    case objects::SH_DOOR:        return CAT_DOOR;
    case objects::SH_BARRICADE:   return CAT_BREAKABLE;
    case objects::SH_ACTIVATABLE: return CAT_ACTIVATABLE;
    default:                      return CAT_NONE;
    }
}

inline const char* CatName(Cat c)
{
    switch (c) {
    case CAT_DOOR:        return "door";
    case CAT_BREAKABLE:   return "breakable";
    case CAT_ACTIVATABLE: return "activatable";
    default:              return "none";
    }
}

// ---------------------------------------------------------------------------
// The state property, per family, with its polarity
// ---------------------------------------------------------------------------
// The caller reads the primary first and the alternate only when the primary is absent
// (a subclass may implement one and not the other).  `inverted` means the property is
// true when the object is CLOSED (`DoorClosed`), so the wire value is `!prop`.
struct Prop {
    const char* name;
    bool        inverted;
};

inline Prop StateProp(objects::Shape s)
{
    Prop p = { nullptr, false };
    switch (s) {
    case objects::SH_CHEST:      p.name = "Opened";          break;
    case objects::SH_DOOR:       p.name = "Opened";          break;
    case objects::SH_PICKUP:     p.name = "PickedUp";        break;
    case objects::SH_BARRICADE:  p.name = "BrokeBarricade";  break;
    case objects::SH_ACTIVATABLE:p.name = "Activated";       break;
    default: break;
    }
    return p;
}

inline Prop StateAlt(objects::Shape s)
{
    Prop p = { nullptr, false };
    switch (s) {
    case objects::SH_DOOR:       p.name = "DoorClosed";  p.inverted = true;  break;
    case objects::SH_PICKUP:     p.name = "Acquired";     break;
    // `BreakableBarricade_C` says `BrokeBarricade`; the whole Pot family
    // (`Pot_Base_C`, a plain Actor -- NOT a SavableActor) says `Broken`.  Both are the
    // runtime latch, and a pot is the one breakable that is not carried by the GUID lane.
    case objects::SH_BARRICADE:  p.name = "Broken";       break;
    case objects::SH_ACTIVATABLE:p.name = "bActivated";   break;
    default: break;
    }
    return p;
}

// The third probe, only when both above are absent.  A breakable's save value is the
// truth and lives in ACTOR_STATES; `PreviewBroken` is the class' own runtime mirror of
// it where one exists, so it is a last resort and never a guess.
inline Prop StateAlt2(objects::Shape s)
{
    Prop p = { nullptr, false };
    switch (s) {
    case objects::SH_DOOR:       p.name = "PreviewOpen";   break;
    case objects::SH_BARRICADE:  p.name = "PreviewBroken"; break;
    case objects::SH_ACTIVATABLE:p.name = "ToggledOn";     break;
    default: break;
    }
    return p;
}

// ---------------------------------------------------------------------------
// Direction policy
// ---------------------------------------------------------------------------
// A door can be shut again and a lever can be switched off; a chest, a pickup and a
// broken barrel cannot come back.  This is the rule `objects::ApplyAllowed` had to
// refuse for every shape because it had no close path -- the door's close path is
// `SetToOpenState(false)` / `Deactivate`, and it exists.
inline bool Bidirectional(objects::Shape s)
{
    return s == objects::SH_DOOR || s == objects::SH_ACTIVATABLE;
}

inline bool ApplyAllowed(uint8_t want, uint8_t local, bool haveLocal, objects::Shape s)
{
    if (!Bidirectional(s) && want == 0) return false;   // never undo an open/break
    if (haveLocal && local == want) return false;       // already there
    return true;
}

// ---------------------------------------------------------------------------
// The call plan: the class' own animation, in the order that preserves both
// directions and never names a payout
// ---------------------------------------------------------------------------
// `mode` tells the caller how to build the call:
//   M_OPEN   a no-parameter verb (Activate, ForceOpen, Break)
//   M_CLOSE  a no-parameter verb (Deactivate)
//   M_BOOL   a one-bool visual (SetToOpenState, SetToFinalState, SetTOEndState); the
//            caller passes `want`, and if the function has no parameter it is called
//            bare only for want != 0 (a bare "open" call cannot close)
// The first member that resolves AND whose call succeeds wins; the caller then writes
// the state bool, so a class whose visual did nothing still converges.
enum Mode : uint8_t {
    M_NONE  = 0,
    M_OPEN  = 1,
    M_CLOSE = 2,
    M_BOOL  = 3,
};

struct Step {
    const char* fn;
    uint8_t     mode;
};

inline int Plan(objects::Shape s, uint8_t want, Step* out, int cap)
{
    if (!out || cap <= 0) return 0;
    int n = 0;
    // `SetToOpenState` takes the direction as a bool (both OpenDoor and CloseDoor call
    // it), so it is the first step for either direction.
    if (s == objects::SH_DOOR) {
        if (want) {
            if (n < cap) out[n++] = Step{ "SetToOpenState", M_BOOL };
            if (n < cap) out[n++] = Step{ "SetTOEndState",  M_BOOL };
            if (n < cap) out[n++] = Step{ "SetToFinalState", M_BOOL };
            if (n < cap) out[n++] = Step{ "Activate",       M_OPEN };
            if (n < cap) out[n++] = Step{ "ForceOpen",      M_OPEN };
        } else {
            if (n < cap) out[n++] = Step{ "SetToOpenState", M_BOOL };
            if (n < cap) out[n++] = Step{ "SetToFinalState", M_BOOL };
            if (n < cap) out[n++] = Step{ "Deactivate",     M_CLOSE };
        }
        return n;
    }
    if (s == objects::SH_ACTIVATABLE) {
        if (want) {
            if (n < cap) out[n++] = Step{ "Activate",       M_OPEN };
            if (n < cap) out[n++] = Step{ "ForceOpen",      M_OPEN };
            if (n < cap) out[n++] = Step{ "SetToFinalState", M_BOOL };
        } else {
            if (n < cap) out[n++] = Step{ "Deactivate",     M_CLOSE };
            if (n < cap) out[n++] = Step{ "SetToFinalState", M_BOOL };
        }
        return n;
    }
    if (s == objects::SH_BARRICADE) {
        // The break is one-way.  `Restore State From Save Data` is the only un-break
        // path and it belongs to the savable lane, which calls it itself.
        if (want && n < cap) out[n++] = Step{ "Break", M_OPEN };
        return n;
    }
    if (s == objects::SH_CHEST) {
        if (want && n < cap) out[n++] = Step{ "SetToFinalState", M_BOOL };
        return n;
    }
    return n;   // a pickup hides itself; the caller has a dedicated path
}

// ---------------------------------------------------------------------------
// Names the plan must never build
// ---------------------------------------------------------------------------
inline bool Forbidden(const char* fn)
{
    if (!fn || !fn[0]) return false;
    static const char* const kNo[] = {
        "Interact", "Grant", "GrantItemsAndActivate", "InsertKey",
        "CheckRequiredItems", "OpenDoor", "CloseDoor", "Open", "TryUseKey",
        "AddItemToInventory", "TryApplyDamage", "GrantNewbestiary",
        "OpenKeymenu", "TryConsumingItemFromInventory",
    };
    for (unsigned i = 0; i < sizeof(kNo) / sizeof(kNo[0]); ++i) {
        if (strcmp(fn, kNo[i]) == 0) return true;
    }
    return false;
}

// A plan entry is legal only if it is a real visual/state path.  Exposed so a test can
// assert the whole plan for every shape and direction in one loop.
inline bool PlanLegal(const Step* steps, int n)
{
    for (int i = 0; i < n; ++i) {
        if (!steps[i].fn || !steps[i].fn[0]) return false;
        if (Forbidden(steps[i].fn)) return false;
        if (steps[i].mode == M_NONE) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Does a SAVABLE actor of this shape need an explicit visual after the container restore?
// ---------------------------------------------------------------------------
// `RestoreFromSave` calls the class' own `Restore State From Save Data`.  For a breakable
// that override calls `Break` and for a secret wall it drives `SetToFinalState`, so those
// need nothing more.  A plain `Door_Base_C` door's restore only loads `Opened`; the
// timeline is driven by `SetToOpenState`, and `OpenDoor`/`CloseDoor` both call it.  So a
// door whose state actually changed gets the same call the game would have made.
inline bool NeedsExplicitAnim(objects::Shape s, uint8_t had, uint8_t want)
{
    return s == objects::SH_DOOR && had != want;
}

} // namespace interact
} // namespace qd
