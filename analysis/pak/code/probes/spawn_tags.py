#!/usr/bin/env python3
"""The signature side of the game's own hard travel (Session 12).

`FS_PlayerChar` exports `QueueTeleportLevelLoad`, and no cooked package names
`LoadMapWithLoadingScreen` at all.  So the question is what the player's own
half-load carries, and what state survives the world teardown (only the
`GameInstance` does).  This walks each function's tagged property list and each
class's variable list the way `SYNC-RESEARCH` §A.2 does.

    PYTHONPATH=analysis/pak/code python analysis/pak/code/probes/spawn_tags.py
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))

import pakread      # noqa: E402
import blueprints   # noqa: E402

TYPES = {"BoolProperty", "ByteProperty", "IntProperty", "FloatProperty", "ObjectProperty",
         "StructProperty", "ArrayProperty", "MapProperty", "SetProperty", "NameProperty",
         "StrProperty", "TextProperty", "InterfaceProperty", "SoftObjectProperty",
         "ClassProperty", "EnumProperty", "DelegateProperty", "MulticastInlineDelegateProperty",
         "MulticastDelegateProperty", "DoubleProperty", "Int64Property", "UInt16Property",
         "UInt32Property", "UInt64Property", "Int8Property"}

FUNCS = {
    "QueensDomain/Content/Player/FS_PlayerChar.uasset":
        ["QueueTeleportLevelLoad", "BeginTeleport", "FinishTeleport", "TeleportToLocation",
         "TeleportToTeleporterData", "TeleportToSave", "MovePlayerToTempTeleporter",
         "FindTeleporterActorFromData", "SuperFastTeleport", "SetTeleportedRecently"],
    "QueensDomain/Content/Blueprints/GameInstanceUtils.uasset":
        ["LoadSaveSynch", "OpenLevel"],
    "QueensDomain/Content/Blueprints/Teleporter/Teleporter.uasset":
        ["ReceiveTeleport", "UpdateTeleportTransform", "Interact"],
    "QueensDomain/Content/Blueprints/B33smentTeleportPoint.uasset": ["*"],
    "QueensDomain/Content/Blueprints/PlayerTeleportTargetPos.uasset": ["*"],
}
VARS = {
    "QueensDomain/Content/Player/FS_PlayerChar.uasset":
        ("LevelLoadingSuspendState", "Teleport", "Level", "Save", "Suspend", "Pending",
         "Queued", "Next", "Target", "Location", "Map"),
    "QueensDomain/Content/Blueprints/FS_GameInstance.uasset":
        ("LevelLoad", "Teleport", "Level", "Suspend", "Pending", "Queued", "Next",
         "TargetPos", "Area", "Location", "Map"),
    "QueensDomain/Content/Blueprints/B33smentTeleportPoint.uasset": ("",),
    "QueensDomain/Content/Blueprints/PlayerTeleportTargetPos.uasset": ("",),
}


def tags_of(body, names):
    out = []
    seen = set()
    for k in range(max(0, len(body) - 24)):
        t, n2 = struct.unpack("<ii", body[k:k + 8])
        if not (0 <= t < len(names) and names[t] in TYPES) or n2:
            continue
        nm, num = struct.unpack("<ii", body[k + 8:k + 16])
        if num or not (0 <= nm < len(names)):
            continue
        fl, dim, sz = struct.unpack("<iii", body[k + 16:k + 28])
        if not (0 <= dim < 64 and 0 <= sz < 100000):
            continue
        if (names[t], names[nm]) in seen:
            continue
        seen.add((names[t], names[nm]))
        out.append((names[t], names[nm], fl, dim, sz))
    return out


def main():
    with pakread.Pak() as pk:
        for asset, wanted in list(FUNCS.items()) + [(a, None) for a in VARS if a not in FUNCS]:
            try:
                ua = pk.payload(asset)
                ux = pk.payload(asset[:-7] + ".uexp")
            except Exception as e:
                print("\n%s: cannot read (%s)" % (asset, e))
                continue
            info = blueprints.parse(ua)
            imps, exps = blueprints.try_imports_exports(ua, info)
            names = info["names"]
            base = len(ua)
            print("\n=== %s (exports %d) ===" % (asset, len(exps)))
            for i, e in enumerate(exps):
                o = info["tail_fields"][3] + i * 104
                ss, so = struct.unpack("<2q", ua[o + 28:o + 44])
                body = ux[so - base: so - base + ss]
                want = FUNCS.get(asset, [])
                if want:
                    if e["class"] not in ("Function", "BlueprintGeneratedClass"):
                        continue
                    if e["name"] not in want and "*" not in want:
                        continue
                elif e["class"] != "BlueprintGeneratedClass":
                    continue
                t = tags_of(body, names)
                if not t:
                    continue
                if e["class"] == "Function":
                    print("  FUNCTION %s(%d bytes): %s" % (
                        e["name"], ss, ", ".join("%s:%s" % (a.split("Property")[0], b)
                                                 for a, b, *_ in t)))
                else:
                    keys = VARS.get(asset, ("",))
                    sel = [x for x in t if any(k.lower() in x[1].lower() for k in keys if k)]
                    if not sel:
                        continue
                    print("  CLASS VARS (%d of %d):" % (len(sel), len(t)))
                    for a, b, fl, dim, sz in sel:
                        print("    %-28s %-44s flags=0x%08X size=%d" % (a, b, fl, sz))


if __name__ == "__main__":
    main()
