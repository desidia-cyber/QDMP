#!/usr/bin/env python3
"""Targeted queries over the extracted inventory + name index."""
import json
import os
import sys
from collections import Counter, defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(os.path.dirname(HERE), "data")

files = json.load(open(os.path.join(os.path.dirname(HERE), "pak-files.json"),
                       encoding="utf-8"))["files"]
classes = json.load(open(os.path.join(DATA, "asset-classes.json"), encoding="utf-8"))
names = json.load(open(os.path.join(DATA, "name-index.json"), encoding="utf-8"))


def by_class(*kinds):
    return sorted(p for p, c in classes.items() if c["class"] in kinds)


def show(title, items, limit=400):
    print("\n### %s  (%d)" % (title, len(items)))
    for p in items[:limit]:
        c = classes.get(p, {})
        print("  %-9s %8s  %s" % (c.get("class"), files.get(p, {}).get("size"), p))
    if len(items) > limit:
        print("  ... %d more" % (len(items) - limit))


def main():
    print("=== MAPS (.umap) ===")
    maps = sorted(p for p in files if p.endswith(".umap"))
    for p in maps:
        print("  %10d  %s" % (files[p]["size"], p))

    show("SkeletalMesh", by_class("SkeletalMesh"))
    show("Skeleton", by_class("Skeleton"))
    show("PhysicsAsset", by_class("PhysicsAsset"))
    show("StaticMesh", by_class("StaticMesh"), limit=20)
    show("WidgetBlueprint", by_class("WidgetBlueprint"),
         limit=20)
    show("DataTable", by_class("DataTable"))
    show("CurveTable/CurveFloat", by_class("CurveTable", "CurveFloat", "CurveVector"))
    show("Font/FontFace", by_class("Font", "FontFace"))
    show("LevelSequence", by_class("LevelSequence"))
    show("Blueprint", by_class("Blueprint"))


if __name__ == "__main__":
    main()
