#!/usr/bin/env python3
"""List umaps, and for one map the sublevels it streams / the actors a filter matches."""
import sys

sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\pak\code")
import pakread          # noqa: E402
import blueprints       # noqa: E402


def umaps():
    with pakread.Pak() as pk:
        ps = sorted(p for p in pk.entries if p.lower().endswith(".umap"))
        print(len(ps), "umaps")
        for p in ps:
            print(" ", p)


def actors(mapname, want):
    with pakread.Pak() as pk:
        target = None
        for p in pk.entries:
            if p.lower().endswith(".umap") and \
               p.rsplit("/", 1)[-1][:-5].lower() == mapname.lower():
                target = p
                break
        if not target:
            print("map not found:", mapname)
            return 1
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
            if want and want.lower() not in blob:
                continue
            rows.append((cls, nm))
        rows.sort()
        print("map:", target)
        print("exports:", len(exp), "matches:", len(rows))
        for cls, nm in rows:
            print("  %-32s %s" % (cls, nm))
    return 0


if __name__ == "__main__":
    if len(sys.argv) < 2 or sys.argv[1] == "umaps":
        umaps()
    else:
        sys.exit(actors(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else ""))
