#!/usr/bin/env python3
"""How the GAME places the player after its own hard cross-world travel (Session 12).

The co-op follower lands at the origin, falling, after
`GameInstance.LoadMapWithLoadingScreen`.  That is not a mod bug: it is what the
engine does when a world has no `PlayerStart`.  The question this probe answers is
what the GAME's own teleport path does instead, so the follower can do the same:

  1. which packages name `QueueTeleportLevelLoad` / `LoadMapWithLoadingScreen`
     (the player's own hard-travel entry), and what their import/export tables say;
  2. whether the hard maps place a `PlayerStart` at all;
  3. every name in `FS_PlayerChar` / `FS_GameInstance` that could carry a pending
     spawn (Pending / Queued / Next / Location / Teleport / Level).

Run with the pak tools on sys.path (they are caches, not sources):

    PYTHONPATH=analysis/pak/code python analysis/pak/code/probes/spawn_path.py
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))

import pakread      # noqa: E402
import blueprints   # noqa: E402

INTEREST = ["QueueTeleportLevelLoad", "LoadMapWithLoadingScreen", "BeginTeleport",
            "FinishTeleport", "TeleportToLocation", "SetTeleportedRecently"]
WORDS = ("pending", "queued", "queue", "next", "spawn", "playerstart", "teleport",
         "levelload", "locationdata", "destination", "arrive")

ASSETS = [
    "QueensDomain/Content/Player/FS_PlayerChar.uasset",
    "QueensDomain/Content/Blueprints/FS_GameInstance.uasset",
    "QueensDomain/Content/Blueprints/Teleporter/Teleporter.uasset",
    "QueensDomain/Content/Blueprints/Teleporter/MiniTeleporter.uasset",
]
MAPS = ["Ruins_Level_00", "900_SipehaWorld", "990_B33sment", "780_SZ_Depths",
        "997_FinalZoneCutscene", "760_SZ_AUT", "100_GR"]


def interesting(names):
    out = []
    for n in names:
        low = n.lower()
        if any(w in low for w in WORDS):
            out.append(n)
    return out


def main():
    with pakread.Pak() as pk:
        paths = sorted(p for p in pk.entries if pakread.is_package(p))
        print("packages: %d" % len(paths))

        print("\n=== packages whose name table names a travel entry ===")
        for path in paths:
            d = pk.payload(path)
            info = blueprints.parse(d)
            hits = [n for n in info["names"] if n in INTEREST]
            if hits:
                print("%-88s %s" % (path, ",".join(sorted(set(hits)))))

        for path in ASSETS:
            try:
                d = pk.payload(path)
            except Exception as e:
                print("\n%s: cannot read (%s)" % (path, e))
                continue
            info = blueprints.parse(d)
            imports, exports = blueprints.try_imports_exports(d, info)
            print("\n=== %s (names %d, imports %d, exports %d) ===" %
                  (path, info["name_count"], len(imports), len(exports)))
            print("  -- exports naming a travel entry --")
            for e in exports:
                if e["name"] in INTEREST or any(w in e["name"].lower() for w in
                                                ("teleport", "levelload")):
                    print("   EXPORT", e)
            print("  -- imports naming a travel entry --")
            for i in imports:
                if i["name"] in INTEREST or any(w in i["name"].lower() for w in
                                                ("teleport", "levelload")):
                    print("   IMPORT", i)
            print("  -- names that could carry a pending spawn --")
            for n in interesting(info["names"]):
                print("   ", n)

        print("\n=== hard maps: is there a PlayerStart? ===")
        for m in MAPS:
            path = None
            for p in paths:
                if p.endswith("/%s.umap" % m):
                    path = p
                    break
            if not path:
                print("%-26s (no package)" % m)
                continue
            d = pk.payload(path)
            info = blueprints.parse(d)
            imports, exports = blueprints.try_imports_exports(d, info)
            ps = [e for e in exports if e["class"] == "PlayerStart"]
            imp = [i for i in imports if i["class"] == "PlayerStart"
                   or i["name"] == "PlayerStart"]
            print("%-26s exports=%d PlayerStart exports=%d imports=%d %s" %
                  (m, len(exports), len(ps),
                   len(imp), ",".join(e["name"] for e in ps)))


if __name__ == "__main__":
    main()
