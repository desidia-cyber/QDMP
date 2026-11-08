#!/usr/bin/env python3
"""Who references the hard maps, and what does FS_GameInstance import? (G6)

`LoadMapWithLoadingScreen` has no Blueprint import anywhere, and its only caller is
its own exec thunk.  So the question becomes: which package names the hard maps
(`900_SipehaWorld`, `990_B33sment`, the cutscene maps) and through which call?

The map names are FString literals in the caller's bytecode, not name-table entries,
so this scans every package PAYLOAD for the bytes.

    python analysis/pak/code/probes/travel_hardmaps.py
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))

import pakread      # noqa: E402
import blueprints   # noqa: E402

MAPS = ["900_SipehaWorld", "990_B33sment", "997_FinalZoneCutscene",
        "998_QueenCutscenes", "999_SipehaCutscenes", "Intro_Level_00"]
CALLS = ["LoadMapWithLoadingScreen", "OpenLevel", "QueueTeleportLevelLoad",
         "BeginCutsceneWithManager"]


def main():
    with pakread.Pak() as pk:
        paths = sorted(p for p in pk.entries if pakread.is_package(p))
        hits = {}
        for n, path in enumerate(paths):
            d = pk.payload(path)
            found = [m for m in MAPS if m.encode() in d or m.encode("utf-16-le") in d]
            if found:
                hits[path] = found
            if (n + 1) % 4000 == 0:
                print("  %d/%d hits %d" % (n + 1, len(paths), len(hits)))
        print("\n--- packages naming a hard map ---")
        for path, maps in sorted(hits.items()):
            print("%-95s %s" % (path, ",".join(maps)))

        print("\n--- imports/exports of the caller candidates ---")
        for path in sorted(hits):
            if not paths:
                continue
            try:
                data = pk.payload(path)
                info = blueprints.parse(data)
                imports, exports = blueprints.try_imports_exports(data, info)
            except Exception as e:
                print("  cannot parse %s: %s" % (path, e))
                continue
            words = [w.lower() for w in CALLS]
            print("\n== %s ==" % path)
            for i in imports:
                if any(w in str(i).lower() for w in words):
                    print("  IMPORT", i)
            for e in exports:
                if any(w in str(e).lower() for w in words):
                    print("  EXPORT", e)


if __name__ == "__main__":
    main()
