#!/usr/bin/env python3
"""List placed actors of interest in one cooked map (offline, reads the pak).

Usage: python list_map_actors.py <map short name> [substring]
"""
import os
import sys

ANALYSIS = r"G:\Freespace\Queens_Domain\analysis\pak\code"
sys.path.insert(0, ANALYSIS)
import pakread      # noqa: E402
import blueprints   # noqa: E402


def main():
    want_map = sys.argv[1] if len(sys.argv) > 1 else "100_GR"
    want = (sys.argv[2] if len(sys.argv) > 2 else "").lower()
    with pakread.Pak() as pk:
        target = None
        for p in pk.entries:
            if p.lower().endswith(".umap") and p.rsplit("/", 1)[-1][:-5].lower() == want_map.lower():
                target = p
                break
        if not target:
            print("map not found:", want_map)
            return 1
        print("map:", target)
        d = pk.payload(target)
        info = blueprints.parse(d)
        imp, exp = blueprints.try_imports_exports(d, info)
        if not exp:
            print("export table did not validate")
            return 1
        rows = []
        for e in exp:
            cls = e["class"] or ""
            nm = e["name"] or ""
            if nm.startswith("Default__"):
                continue
            blob = (cls + " " + nm).lower()
            if want and want not in blob:
                continue
            rows.append((cls, nm))
        rows.sort()
        print("exports:", len(exp), "matches:", len(rows))
        for cls, nm in rows:
            print("  %-30s %s" % (cls, nm))
    return 0


if __name__ == "__main__":
    sys.exit(main())
