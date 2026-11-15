#!/usr/bin/env python3
"""Across every umap, list placed actors whose CLASS looks like a door / breakable / mechanism."""
import re
import sys

sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\pak\code")
import pakread          # noqa: E402
import blueprints       # noqa: E402

PATTERN = re.compile(sys.argv[1] if len(sys.argv) > 1 else r"(door|gate|shutter|secretwall|lever|switch|pot|barrel|crate)", re.I)
SKIP = re.compile(r"^(StaticMeshComponent|SceneComponent|BoxComponent|TimelineComponent|"
                  r"Function|Level|World|Brush|Model|SkeletalMeshComponent|"
                  r"AudioComponent|ParticleSystemComponent|CapsuleComponent|"
                  r"SphereComponent|ArrowComponent|BillboardComponent)", re.I)


def main():
    with pakread.Pak() as pk:
        maps = sorted(p for p in pk.entries if p.lower().endswith(".umap"))
        for path in maps:
            try:
                d = pk.payload(path)
                info = blueprints.parse(d)
                imp, exp = blueprints.try_imports_exports(d, info)
            except Exception as exc:                      # noqa: BLE001
                print("!! %s: %s" % (path, exc))
                continue
            if not exp:
                continue
            rows = []
            for e in exp:
                cls = e["class"] or ""
                nm = e["name"] or ""
                if nm.startswith("Default__") or SKIP.match(cls):
                    continue
                if cls.endswith("_C") and PATTERN.search(cls):
                    rows.append((cls, nm))
            if not rows:
                continue
            print("== %s  (%d)" % (path.rsplit("/", 1)[-1][:-5], len(rows)))
            for cls, nm in sorted(rows)[:14]:
                print("     %-34s %s" % (cls, nm))
    return 0


if __name__ == "__main__":
    sys.exit(main())
