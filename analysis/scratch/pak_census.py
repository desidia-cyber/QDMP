#!/usr/bin/env python3
"""Census of pak paths: top dirs, extensions, and topic searches."""
import json
import re
import collections

D = json.load(open(r"G:\Freespace\Queens_Domain\analysis\scratch\pak_paths.json", encoding="utf-8"))
P = D["entries"]
paths = sorted(P)
print("total entries:", len(paths))

print("\n=== top-level roots ===")
c = collections.Counter()
for p in paths:
    c[p.split("/")[0]] += 1
for k, v in c.most_common(40):
    print("  %-40s %d" % (k, v))

print("\n=== extensions ===")
e = collections.Counter()
for p in paths:
    n = p.rsplit("/", 1)[-1]
    e["." + n.rsplit(".", 1)[-1] if "." in n else "(noext)"] += 1
for k, v in e.most_common(30):
    print("  %-20s %d" % (k, v))

print("\n=== /Game top 2 levels ===")
c2 = collections.Counter()
for p in paths:
    if not p.startswith("Game/"):
        continue
    parts = p.split("/")
    c2["/".join(parts[:3])] += 1
for k, v in c2.most_common(80):
    print("  %-56s %d" % (k, v))

print("\n=== all directories containing 'Character'/'Char'/'Pawn'/'Player' ===")
for p in paths:
    if re.search(r"(?i)(character|/char|char_|pawn|player)", p) and p.endswith((".uasset", ".umap")):
        print("   ", p)


def sec(title, pat, exts=(".uasset", ".umap"), limit=200):
    rx = re.compile(pat, re.I)
    hits = [p for p in paths if rx.search(p) and (not exts or p.endswith(exts))]
    print("\n=== %s (%d) ===" % (title, len(hits)))
    for h in hits[:limit]:
        print("   ", h)
    if len(hits) > limit:
        print("    ... %d more" % (len(hits) - limit))


sec("Queen / QD / Domain / project prefix", r"queen|(?<![a-z])QD|domain")
sec("FirstPerson / FP_ / Arms / Hands / Viewmodel", r"firstperson|/fp_|_fp\b|arms|hand|viewmodel|viewport|vm_")
sec("Body / Mesh / Skeleton", r"body|mesh|skeleton|sk_|skm_")
sec("Quest / Dialogue / Item / Loot / Inventory / Save", r"quest|dialog|loot|inventor|savegame|/save")
sec("Enemy / AI / Boss / Monster", r"enemy|(?<![a-z])boss|monster|(?<![a-z])ai(?![a-z])|hostile")
sec("Mount / Ride / Vehicle", r"mount|ride|horse|vehicle")
