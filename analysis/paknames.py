#!/usr/bin/env python3
"""Extract the pak's directory-index name list.

The v10+ pak keeps filenames in a directory index near EOF as
        u32 hash, u32 len (includes NUL), char name[len]
so every NUL-terminated printable run in that region is exactly one name.
Directory entries end with '/' and give the folder taxonomy; file entries are
basenames only (the full path hash is not reversible without the game's CRC).
"""
import collections, os, re, sys

PAK = (r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Content\Paks"
       r"\QueensDomain-WindowsNoEditor.pak")
OUT = os.path.dirname(os.path.abspath(__file__))
TAIL = 3_000_000

sz = os.path.getsize(PAK)
with open(PAK, "rb") as f:
    f.seek(sz - TAIL)
    d = f.read()

runs = [m.group().decode("latin-1") for m in re.finditer(rb"[\x21-\x7e]{3,}", d)]
print("runs:", len(runs))

dirs = sorted({r for r in runs if r.endswith("/")})
maps = sorted({r for r in runs if r.endswith(".umap")})
assets = sorted({r for r in runs if r.endswith(".uasset")})
plugins = sorted({r for r in runs if r.endswith(".uplugin")})
print(f"dirs={len(dirs)} maps={len(maps)} uasset={len(assets)} uplugin={len(plugins)}")

with open(os.path.join(OUT, "pak_dirs.txt"), "w", encoding="utf-8") as g:
    g.write("\n".join(dirs))
with open(os.path.join(OUT, "pak_umaps.txt"), "w", encoding="utf-8") as g:
    g.write("\n".join(maps))
with open(os.path.join(OUT, "pak_assets.txt"), "w", encoding="utf-8") as g:
    g.write("\n".join(assets))
with open(os.path.join(OUT, "pak_plugins.txt"), "w", encoding="utf-8") as g:
    g.write("\n".join(plugins))

print("\n=== MAPS (%d) ===" % len(maps))
for m in maps:
    print("  " + m)
print("\n=== TOP DIRECTORY PREFIXES ===")
seen = collections.Counter()
for x in dirs:
    parts = x.split("/")
    seen["/".join(parts[:3])] += 1
for k, v in sorted(seen.items()):
    print(f"  {v:4d}  {k}")
