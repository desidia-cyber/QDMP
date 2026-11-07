#!/usr/bin/env python3
"""Produce the evidence tables the report needs, as text files under data/."""
import json
import os
import re
import sys
from collections import Counter, defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DATA = os.path.join(ROOT, "data")

files = json.load(open(os.path.join(ROOT, "pak-files.json"), encoding="utf-8"))["files"]
classes = json.load(open(os.path.join(DATA, "asset-classes.json"), encoding="utf-8"))
names = json.load(open(os.path.join(DATA, "name-index.json"), encoding="utf-8"))

out = open(os.path.join(DATA, "evidence.txt"), "w", encoding="utf-8")


def w(s=""):
    out.write(s + "\n")


def cls(p):
    return classes.get(p, {}).get("class") or "-"


def size(p):
    return files.get(p, {}).get("size") or 0


def section(t):
    w("\n" + "=" * 78)
    w("== " + t)
    w("=" * 78)


# ---------------------------------------------------------------- classes
section("ASSET CLASS COUNTS (from export[0].ClassIndex, all 19,213 packages)")
per = Counter()
for p, c in classes.items():
    per[c["class"] or "<null>"] += 1
for k, v in per.most_common():
    w("  %-42s %5d" % (k, v))
w("  TOTAL packages: %d" % len(classes))

# ---------------------------------------------------------------- player
section("EVERYTHING UNDER /Game/Player (class + size)")
player = sorted(p for p in classes if p.startswith("/Game/Player/"))
for p in player:
    w("  %-34s %8d  %s" % (cls(p), size(p), p))
w("  total: %d" % len(player))

# ---------------------------------------------------------------- meshes
section("ALL SkeletalMesh ASSETS (%d)" % len([p for p in classes if cls(p) == "SkeletalMesh"]))
for p in sorted(p for p in classes if cls(p) == "SkeletalMesh"):
    w("  %8d  %s" % (size(p), p))

section("ALL Skeleton ASSETS")
for p in sorted(p for p in classes if cls(p) == "Skeleton"):
    w("  %8d  %s" % (size(p), p))

section("ALL StaticMesh ASSETS (%d)" % len([p for p in classes if cls(p) == "StaticMesh"]))
sm = sorted(p for p in classes if cls(p) == "StaticMesh")
for p in sm:
    w("  %8d  %s" % (size(p), p))

# ---------------------------------------------------------------- name probes
section("NAME-TABLE PROBES (name -> #packages containing it)")
probes = [
    "FirstPerson", "FPArms", "FP_", "Viewmodel", "ViewMesh", "PlayerMesh",
    "Hands", "Arms", "SkeletalMeshComponent", "Socket", "Weapon", "Queen",
    "Hero", "Protagonist", "PlayerCharacter", "Body", "FullBody", "ThirdPerson",
    "Camera", "AnimMontage", "AnimSequence", "BlendSpace", "AimOffset",
    "MasterPose", "Retarget", "Outfit", "Skin", "Customiz", "Equip",
]
for probe in probes:
    hits = [(n, v) for n, v in names.items() if probe.lower() in n.lower()]
    tot = sum(v["count"] for _, v in hits)
    w("  %-24s %5d distinct names, %7d package-slots" % (probe, len(hits), tot))
    for n, v in sorted(hits, key=lambda kv: -kv[1]["count"])[:8]:
        w("      %-58s %5d  e.g. %s" % (n, v["count"], v["paths"][0] if v["paths"] else ""))

# ---------------------------------------------------------------- maps
section("MAPS (51 .umap)")
for p in sorted(p for p in files if p.endswith(".umap")):
    w("  %10d  %s" % (size(p), p))

# ---------------------------------------------------------------- systems
section("SYSTEM KEYWORD SWEEP over package paths")
groups = {
    "Quest/Objective": r"Quest|Objective|Mission|Task|Diary|Journal",
    "Dialogue": r"Dialogue|Dlg|Conversation|Lines|_VO|VO_",
    "Items/Loot": r"Item|Loot|Pickup|Inventory|Equip|Weapon|Consumable",
    "Enemies/AI": r"Enemy|/AI|Boss|Pawn|Spawn|Behaviour|Behavior",
    "Save/Checkpoint": r"Save|Checkpoint|Progress|Savable",
    "UI": r"/UI/|HUD|Menu|Widget",
    "Audio": r"/Audio/|/Music/|Sound",
    "Cutscene": r"Cutscene|Sequencer|LevelSequence",
}
seen = {}
for label, pat in groups.items():
    rx = re.compile(pat, re.I)
    hits = sorted(p for p in classes if rx.search(p))
    byclass = Counter(cls(p) for p in hits)
    w("\n-- %s : %d packages" % (label, len(hits)))
    for k, v in byclass.most_common(12):
        w("     %-38s %5d" % (k, v))
    seen[label] = hits

section("/Game/UI package paths (first 200)")
for p in sorted(p for p in classes if p.startswith("/Game/UI/"))[:200]:
    w("  %-34s %8d  %s" % (cls(p), size(p), p))

section("BlueprintGeneratedClass packages under /Game/Blueprints (all)")
for p in sorted(p for p in classes
                if p.startswith("/Game/Blueprints/") and cls(p) == "BlueprintGeneratedClass"):
    w("  %8d  %s" % (size(p), p))

section("Files whose path contains Quest/Objective/Mission/Task/Diary/Journal")
for p in seen["Quest/Objective"]:
    w("  %-34s %8d  %s" % (cls(p), size(p), p))

section("Files whose path contains Dialogue/Dlg/Conversation/Lines/VO")
for p in seen["Dialogue"]:
    w("  %-34s %8d  %s" % (cls(p), size(p), p))

section("Files whose path contains Save/Checkpoint/Progress")
for p in seen["Save/Checkpoint"]:
    w("  %-34s %8d  %s" % (cls(p), size(p), p))

section("DataTable / CurveTable / UserDefinedStruct / UserDefinedEnum packages")
for p in sorted(p for p in classes
                if cls(p) in ("DataTable", "CurveTable", "UserDefinedStruct",
                              "UserDefinedEnum", "CurveFloat", "CurveVector",
                              "CurveLinearColor")):
    w("  %-34s %8d  %s" % (cls(p), size(p), p))

out.close()
print("wrote", os.path.join(DATA, "evidence.txt"))
print("bytes:", os.path.getsize(os.path.join(DATA, "evidence.txt")))
