#!/usr/bin/env python3
"""Enumerate placed Teleporter and Save-point actors in every .umap.

Cooked maps keep one export per placed actor, so the export table gives the
authoritative count and the actor's object name. Object path is
    <map package>.<map short name>:PersistentLevel.<export name>
e.g. /Game/RuinsLevel/Maps/100_GR.100_GR:PersistentLevel.Teleporter_GR
"""
import collections
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pakread      # noqa: E402
import blueprints   # noqa: E402


def main():
    with pakread.Pak() as pk:
        maps = sorted(p for p in pk.entries if p.lower().endswith(".umap"))
        print("maps:", len(maps))
        tele = []
        saves = []
        per_map = []
        for m in maps:
            try:
                d = pk.payload(m)
                info = blueprints.parse(d)
                imp, exp = blueprints.try_imports_exports(d, info)
            except Exception as e:
                per_map.append((m, "PARSE FAIL: %s" % e, 0, 0))
                continue
            if not exp:
                per_map.append((m, "export table did not validate", 0, 0))
                continue
            short = m.rsplit("/", 1)[-1][:-len(".umap")]
            pkg = "/Game/" + m.split("Content/", 1)[1][:-len(".umap")]
            t = s = 0
            for e in exp:
                cls = e["class"] or ""
                nm = e["name"]
                if nm.startswith("Default__"):
                    continue
                if "Teleporter" in cls:
                    tele.append((m, "%s.%s:PersistentLevel.%s" % (pkg, short, nm), nm, cls))
                    t += 1
                elif cls.endswith("_Save_C") or cls.startswith("Checkpoint"):
                    saves.append((m, "%s.%s:PersistentLevel.%s" % (pkg, short, nm), nm, cls))
                    s += 1
            per_map.append((m, "", t, s))
        print("\n=== per map ===")
        for m, note, t, s in per_map:
            print("  %-62s teleporters=%-3d savepoints=%-3d %s" % (m.rsplit("/", 1)[-1], t, s, note))
        print("\n=== TELEPORTERS (%d) ===" % len(tele))
        for m, path, nm, cls in tele:
            print("  %-24s %-26s %s" % (cls, nm, path))
        print("\n=== SAVE POINTS (%d) ===" % len(saves))
        for m, path, nm, cls in saves:
            print("  %-24s %-26s %s" % (cls, nm, path))
        print("\nmap coverage: %d/%d" % (sum(1 for m, n, t, s in per_map if not n), len(maps)))


if __name__ == "__main__":
    main()
