"""Name-table scan for the mechanism vocabulary.

Why this instrument is required in addition to the import scan:
  * a *final* (native/static) call such as SetGamePaused compiles to an import;
  * a *virtual* call on a blueprint object, such as OpenPauseMenu or
    EnterCutsceneMode, is resolved by name at runtime and creates **no import** --
    the name only has to exist in the package name table.

So "imports X" proves a native call, while "name table contains X" proves the
package references X either by calling it, declaring it, or holding a property of
that name. Both are recorded, and the report never conflates them.
"""
import os
import sys
import re
import json
import struct
import collections

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "analysis", "pak", "code"))
import pakread            # noqa: E402
import blueprints         # noqa: E402

PKG_MAGIC = b"\xc1\x83\x2a\x9e"

# mechanism names that decide whether the world clock advances, or that gate a
# modal UI state. Matched exactly against name-table entries.
MECH = [
    "SetGamePaused", "IsGamePaused",
    "SetGlobalTimeDilation", "GetGlobalTimeDilation", "SetTimeDilation",
    "Set BaseTimeDilation", "SetPauseTimeDilation", "Set Pause Time Dilation",
    "IsTimeDilationPaused", "IsPausedTimeDilationEnabled", "IsEnemyPauseTDLEnabled",
    "SetPausedEnemyTimeDilation", "Bind To Enemy Pause TDL",
    "SetTickableWhenPaused", "SetActorTickEnabled", "CustomTimeDilation",
    "bIgnoreTimeDilation", "GetActorTimeDilation", "SetActorTickInterval",
    "OpenPauseMenu", "TryAutoPause", "CheckForOverlayPause", "ForceNoOverlayPause",
    "IsInGameplayModal", "InGameplayModal",
    "EnterCutsceneMode", "ExitCutsceneMode", "InCutscene", "CutsceneSequencePlayer",
    "InPhotoMode", "EndPhotoMode", "PhotoMode", "PhotoModePawn",
    "CurrentGameMenu", "ExitMenu", "OpenSaveMenu", "CurrentSaveMenu", "IsOpen",
    "CurrentShopMenu", "InspectMode", "QuickJournal", "Journal",
    "EnableAllPlayerInput", "DisableAllPlayerInput", "SetInputLockType",
    "SetIgnoreLookInput", "SetIgnoreMoveInput", "bShowMouseCursor",
]

SET_MECH = set(MECH)


def header_bytes(pk, path):
    off, size, _ = pk.entries[path]
    head = pk.stream.read(off + pakread.ENTRY_HEADER_SIZE, min(size, 4096))
    if head[:4] != PKG_MAGIC:
        return None
    o = 4
    (legacy,) = struct.unpack("<i", head[o:o + 4]); o += 4
    if legacy != -4:
        o += 4
    o += 4 + 4
    if legacy <= -2:
        (n_custom,) = struct.unpack("<i", head[o:o + 4]); o += 4
        o += n_custom * 20
    if legacy < 0:
        (total_header,) = struct.unpack("<i", head[o:o + 4]); o += 4
    else:
        total_header = size
    if total_header <= 0 or total_header > size:
        return None
    return pk.stream.read(off + pakread.ENTRY_HEADER_SIZE, total_header)


def main():
    rows = []
    with pakread.Pak() as pk:
        paths = [p for p in pk.entries
                 if p.endswith((".uasset", ".umap")) and p.startswith("QueensDomain/")]
        for i, p in enumerate(paths):
            try:
                data = header_bytes(pk, p)
                if not data:
                    continue
                info = blueprints.parse(data)
                imports, exports = blueprints.try_imports_exports(data, info)
            except Exception:
                continue
            names = set(info["names"])
            hit = sorted(n for n in SET_MECH if n in names)
            if not hit:
                continue
            imported = sorted({imp["name"] for imp in imports
                               if imp["class"] == "Function" and imp["name"] in SET_MECH})
            declared = sorted({e["name"] for e in exports
                               if e["class"] == "Function" and e["name"] in SET_MECH})
            rows.append({"path": p, "mentions": hit, "imports": imported,
                         "exports": declared})
            if i % 4000 == 0:
                print("  %d/%d  hits=%d" % (i, len(paths), len(rows)), flush=True)

    print("\npackages mentioning a mechanism name: %d\n" % len(rows))
    rows.sort(key=lambda r: r["path"])
    with open(os.path.join(HERE, "_mech_mentions.json"), "w") as fh:
        json.dump(rows, fh, indent=1)

    # compact table: path + I/D/M markers
    print("%-78s %s" % ("package", "imports | declares | mentions-only"))
    for r in rows:
        imp = ",".join(r["imports"]) or "-"
        dec = ",".join(r["exports"]) or "-"
        only = ",".join(n for n in r["mentions"]
                        if n not in r["imports"] and n not in r["exports"]) or "-"
        print("%-78s I:%s  D:%s  M:%s"
              % (r["path"].split("/Content/")[-1], imp, dec, only))
    print("\nwrote _mech_mentions.json")


if __name__ == "__main__":
    main()
