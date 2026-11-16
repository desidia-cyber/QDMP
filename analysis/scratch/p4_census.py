#!/usr/bin/env python3
"""p4 census: placed actors per map/class for the R35-R46 part.

Usage:
  p4_census.py maps
  p4_census.py map <MapName> [regex]
  p4_census.py grep <MapName> <regex>     (class OR name)
"""
import re
import sys

sys.path.insert(0, r"G:\Freespace\Queens_Domain\.worktrees\sync-fix\analysis\pak\code")
import pakread          # noqa: E402
import blueprints       # noqa: E402

SKIP = re.compile(
    r"^(StaticMeshComponent|SceneComponent|BoxComponent|TimelineComponent|"
    r"Function|Level|World|Brush|Model|SkeletalMeshComponent|"
    r"AudioComponent|ParticleSystemComponent|CapsuleComponent|"
    r"SphereComponent|ArrowComponent|BillboardComponent|"
    r"Texture2D|Material|MaterialInstance|Font|SoundCue|"
    r"CurveFloat|CurveVector|DataTable|UserDefinedEnum|"
    r"UserDefinedStruct|StringTable|PrimaryDataAsset)", re.I)


def find(pk, mapname):
    for p in pk.entries:
        if p.lower().endswith(".umap") and \
           p.rsplit("/", 1)[-1][:-5].lower() == mapname.lower():
            return p
    return None


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "maps"
    with pakread.Pak() as pk:
        if cmd == "maps":
            ps = sorted(p for p in pk.entries if p.lower().endswith(".umap"))
            print(len(ps), "umaps")
            for p in ps:
                print(" ", p.rsplit("/", 1)[-1][:-5])
            return 0
        mapname = sys.argv[2]
        pat = re.compile(sys.argv[3] if len(sys.argv) > 3 else ".", re.I)
        target = find(pk, mapname)
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
            if nm.startswith("Default__") or SKIP.match(cls):
                continue
            if not (cls.endswith("_C") or cls.endswith("_BP_C") or cls in ("Actor",)):
                continue
            blob = (cls + " " + nm)
            if not pat.search(blob):
                continue
            rows.append((cls, nm))
        rows.sort()
        print("map:", target)
        print("exports:", len(exp), "matches:", len(rows))
        for cls, nm in rows:
            print("  %-36s %s" % (cls, nm))
    return 0


if __name__ == "__main__":
    sys.exit(main())
