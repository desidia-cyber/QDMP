"""UI / system state taxonomy: which menu packages exist, what they derive from,
and which clock mechanism each one references.

For every package under the UI, Cutscenes, Player and gameplay-manager trees it
prints:
  * the generated class and its resolved parent class (from the export table),
  * mechanism names the package imports (proof of a native static call),
  * mechanism names present only in the name table (a virtual BP call, a declared
    function, or a property -- weaker, and labelled as such).
"""
import os
import sys
import re
import json
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "analysis", "pak", "code"))
import pakread            # noqa: E402
import blueprints         # noqa: E402

PKG_MAGIC = b"\xc1\x83\x2a\x9e"

MECH = [
    "SetGamePaused", "IsGamePaused", "SetGlobalTimeDilation", "GetGlobalTimeDilation",
    "SetTickableWhenPaused", "SetActorTickEnabled", "CustomTimeDilation",
    "bIgnoreTimeDilation", "SaveMenu", "OpenPauseMenu", "TryAutoPause",
    "CheckForOverlayPause", "ForceNoOverlayPause", "IsInGameplayModal",
    "EnterCutsceneMode", "ExitCutsceneMode", "InCutscene", "InPhotoMode",
    "EndPhotoMode", "CurrentGameMenu", "ExitMenu", "IsOpen",
    "SetPauseTimeDilation", "IsTimeDilationPaused", "SetPausedEnemyTimeDilation",
    "DisableAllPlayerInput", "EnableAllPlayerInput", "bShowMouseCursor",
    "SetIgnoreLookInput", "SetIgnoreMoveInput",
]
SET_MECH = set(MECH)

PREFIXES = ("QueensDomain/Content/UI/", "QueensDomain/Content/Cutscenes/",
            "QueensDomain/Content/Player/", "QueensDomain/Content/Blueprints/")


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
    out = []
    with pakread.Pak() as pk:
        paths = [p for p in pk.entries
                 if p.endswith((".uasset",)) and p.startswith(PREFIXES)]
        print("candidate packages:", len(paths))
        for p in paths:
            try:
                data = header_bytes(pk, p)
                if not data:
                    continue
                info = blueprints.parse(data)
                imports, exports = blueprints.try_imports_exports(data, info)
            except Exception:
                continue
            names = set(info["names"])
            ment = sorted(n for n in SET_MECH if n in names)
            if not ment:
                continue
            imp = sorted({i["name"] for i in imports
                          if i["class"] == "Function" and i["name"] in SET_MECH})
            # generated class + parent
            gen = None
            for e in exports:
                if e["class"] == "BlueprintGeneratedClass" or e["class"] == "WidgetBlueprintGeneratedClass":
                    gen = e
                    break
            rec = {"path": p, "imports": imp,
                   "mentions_only": [m for m in ment if m not in imp],
                   "class": (gen["name"] if gen else None),
                   "parent": (gen["super"] if gen else None)}
            out.append(rec)

    with open(os.path.join(HERE, "_ui_taxonomy.json"), "w") as fh:
        json.dump(out, fh, indent=1)

    print("\n%d packages reference a clock/modal mechanism\n" % len(out))
    for r in sorted(out, key=lambda x: x["path"]):
        short = r["path"].split("/Content/")[-1]
        print("%-70s parent=%-28s I:%s  M:%s"
              % (short, r["parent"] or "-",
                 ",".join(r["imports"]) or "-",
                 ",".join(r["mentions_only"]) or "-"))


if __name__ == "__main__":
    main()
