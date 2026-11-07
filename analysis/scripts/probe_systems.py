#!/usr/bin/env python3
"""Keyword probes over package paths AND name tables, for system evidence."""
import json
import os
import re
import sys
from collections import Counter

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DATA = os.path.join(ROOT, "data")

files = json.load(open(os.path.join(ROOT, "pak-files.json"), encoding="utf-8"))["files"]
classes = json.load(open(os.path.join(DATA, "asset-classes.json"), encoding="utf-8"))
names = json.load(open(os.path.join(DATA, "name-index.json"), encoding="utf-8"))


def cls(p):
    return classes.get(p, {}).get("class") or "-"


def path_probe(title, pattern, cls_filter=None, limit=300):
    rx = re.compile(pattern, re.I)
    hits = sorted(p for p in classes if rx.search(p))
    if cls_filter:
        hits = [p for p in hits if cls(p) in cls_filter]
    print("\n### %s  -> %d packages" % (title, len(hits)))
    for p in hits[:limit]:
        print("   %-32s %8d  %s" % (cls(p), files.get(p, {}).get("size", 0), p))
    if len(hits) > limit:
        print("   ... %d more" % (len(hits) - len(hits[:limit])))


def name_probe(title, pattern, limit=25):
    rx = re.compile(pattern, re.I)
    hits = [(n, v) for n, v in names.items() if rx.search(n)]
    tot = sum(v["count"] for _, v in hits)
    print("\n### NAME PROBE %s -> %d distinct names, %d slots"
          % (title, len(hits), tot))
    for n, v in sorted(hits, key=lambda kv: -kv[1]["count"])[:limit]:
        print("   %-56s %5d  %s" % (n, v["count"],
                                    (v["paths"] or [""])[0]))


def main():
    print("=" * 78)
    print("CHECKPOINT / BONFIRE EQUIVALENT PROBES")
    print("=" * 78)
    name_probe("Bonfire", r"bonfire")
    name_probe("Checkpoint", r"checkpoint")
    name_probe("Respawn", r"respawn")
    name_probe("Shrine", r"shrine")
    name_probe("Altar", r"altar")
    name_probe("Fountain", r"fountain")
    name_probe("SavePoint/SaveStone", r"savepoint|savestone|save_stone")
    name_probe("Teleport", r"teleport")
    name_probe("Lantern/Idol", r"lantern|idol")
    name_probe("Sit/Meditate", r"Sit(?!e)|Meditat")

    print("\n" + "=" * 78)
    print("PATHS")
    print("=" * 78)
    path_probe("Quest/Objective/Mission/Task/Diary/Journal",
               r"Quest|Objective|Mission|Task|Diary|Journal", limit=80)
    path_probe("Save system blueprints",
               r"Save|Savable|Progress", limit=120)
    path_probe("Boss",
               r"Boss", limit=120)
    path_probe("Shrine/Altar/Fountain",
               r"Shrine|Altar|Fountain", limit=120)
    path_probe("Teleport",
               r"Teleport", limit=60)
    path_probe("Level transition / streaming / portal",
               r"LevelList|Stream|Portal|LevelStream|Transition", limit=80)


if __name__ == "__main__":
    main()
