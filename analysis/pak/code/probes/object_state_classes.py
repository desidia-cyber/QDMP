#!/usr/bin/env python3
"""D5 -- the cooked classes behind the placed-object lane (`OBJECTS`, WIRE.md 3.20).

Run:  python object_state_classes.py [class-name ...]
Needs: ../pakread.py, ../blueprints.py and the pak.

Why this probe exists
---------------------
LIVE-TEST-LOG Session 8 R67 measured `SG_Chest_BP2 [SG_Chest_BP_C] savable=no guid=-
index=-1 ... opened=0`: the chest has no GUID, so the GUID-keyed ACTOR_STATES lane cannot
carry it.  What the mod needs before it can put the partner's copy into the opened state
without paying out is, per class: the property that IS the state, the Blueprint entry that
opens/activates it visually, and the verbs that pay out (which the lane must never call).

This prints exactly that, from the shipped packages, so the table in `qdobjectstate.h` can
be re-derived rather than believed.  The class `super` chain matters: `SG_Chest_BP_C` is a
mesh/sound subclass whose state and functions live on `Chest_C`, which is why the probe
follows the `Chest_C` package rather than the level instance's class name.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(HERE, "..", "..", ".."))
import pakread     # noqa: E402
import blueprints  # noqa: E402

# The classes the survey asked about, plus the package that actually holds each one's
# state and functions (a subclass's name table carries only its overrides).
PACKAGES = [
    ("chest",      "QueensDomain/Content/Props/Chest_00/Chest.uasset"),
    ("chest-mesh", "QueensDomain/Content/Props/Chest_00/SG_Chest_BP.uasset"),
    ("door",       "QueensDomain/Content/Blueprints/Door_Base.uasset"),
    ("barricade",  "QueensDomain/Content/Blueprints/BreakableBarricade/BreakableBarricade.uasset"),
    ("pickup",     "QueensDomain/Content/Blueprints/ItemPickup_Base.uasset"),
    ("lever",      "QueensDomain/Content/LD45/Blueprints/Objects/Shared/Activatable.uasset"),
]

# The names that decide the lane's behaviour, per the header comment in qdobjectstate.h.
INTEREST = {
    "state":  ["Opened", "AlwaysOpened", "PreviewOpen", "Animating", "DoorClosed",
               "CurrentActivationCount", "PickedUp", "Acquired", "GrantedItems",
               "Activated", "bActivated"],
    "visual": ["SetToFinalState", "SetToOpenState", "OpenLid", "OpenLid__FinishedFunc",
               "Restore State From Save Data", "RestoreFromSave", "SetActorHiddenInGame",
               "SetActorEnableCollision", "DeleteDropData", "Activate"],
    "payout": ["Interact", "Grant", "GrantItemsAndActivate", "InsertKey",
               "CheckRequiredItems", "OpenDoor", "CloseDoor", "Open", "TryUseKey",
               "AddItemToInventory", "TotalChestsOpened", "ActorsToActivateOnOpen",
               "SaveNamedData", "SaveNamedData_Advanced"],
    "identity": ["GUIDs", "SavableActorState", "Save Data Index to World Save", "MySaveData"],
}


def report(pk, label, path):
    data = pk.payload(path)
    info = blueprints.parse(data)
    imports, exports = blueprints.try_imports_exports(data, info)
    names = set(info["names"])
    supers = [e["super"] for e in exports[:1] if e["super"]]

    print("=" * 78)
    print("%-11s %s" % (label, path))
    print("  super       : %s" % (supers[0] if supers else "?"))
    for group, wanted in INTEREST.items():
        hit = [w for w in wanted if w in names]
        miss = [w for w in wanted if w not in names]
        print("  %-9s : %s" % (group, ", ".join(hit) if hit else "-"))
        if miss and group in ("state", "visual"):
            print("  %-9s   absent: %s" % ("", ", ".join(miss)))


def main():
    labels = sys.argv[1:]
    with pakread.Pak() as pk:
        for label, path in PACKAGES:
            if labels and label not in labels:
                continue
            if path not in pk.entries:
                print("MISSING %s  (%s is not in this pak)" % (path, label))
                continue
            report(pk, label, path)


if __name__ == "__main__":
    main()
