#!/usr/bin/env python3
"""Answer the player-model / multiplayer / anticheat questions from pak content."""
import json
import re
import collections

D = json.load(open(r"G:\Freespace\Queens_Domain\analysis\scratch\pak_paths.json",
                   encoding="utf-8"))
P = D["entries"]
paths = sorted(P)
QD = [p for p in paths if p.startswith("QueensDomain/")]
print("project entries:", len(QD), " total:", len(paths))


def show(title, pat, limit=400, roots=("QueensDomain/",)):
    rx = re.compile(pat, re.I)
    hits = [p for p in paths if rx.search(p) and
            (not roots or any(p.startswith(r) for r in roots))]
    print("\n" + "=" * 78)
    print("%s   (%d)" % (title, len(hits)))
    print("=" * 78)
    for h in hits[:limit]:
        print("  ", h)
    if len(hits) > limit:
        print("   ... %d more" % (len(hits) - limit))


print("\n" + "=" * 78)
print("PLAYER CONTENT TREE (first 3 levels under Content/Player)")
print("=" * 78)
c = collections.Counter()
for p in QD:
    if not p.startswith("QueensDomain/Content/Player/"):
        continue
    parts = p.split("/")
    c["/".join(parts[:5])] += 1
for k, v in sorted(c.items()):
    print("  %-72s %d" % (k, v))

show("PLAYER skeletal meshes (SK_/SKM_/SkeletalMesh/SM_Body)", r"/Player/.*(sk_|skm_|skeletalmesh|body|fullchar)")
show("Anything named 'FullChar'", r"fullchar")
show("Anything with 'FPArms' (count only sample)", r"fparms", limit=20)
show("First-person aliases: FP_, _FP, FirstPerson, ViewModel, Arms, Hands", r"(/fp_|_fp[._/]|firstperson|viewmodel|/arms|_arms|hands)")
show("Character customization / appearance / skin / outfit / wardrobe", r"customi[sz]|appearance|outfit|wardrobe|skin_?tone|body_?type|face_?|hair")
show("Third-person / cinematic cameras", r"thirdperson|cutscene|cinecamera|spectator")
show("Player blueprints under Blueprints", r"QueensDomain/Content/Blueprints/.*player", limit=120)
