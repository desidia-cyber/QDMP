#!/usr/bin/env python3
"""Extract the FName/reflection string surface from a UE4 shipping binary.

UE4 stores its name table as length-prefixed entries. ASCII-only FNames are
stored as plain narrow bytes, so a run-extractor finds them; anything non-ASCII
is stored UTF-16LE. Both are collected here.

Usage: stringsx.py <exe> <outdir>
"""
import os, re, sys, collections

exe = sys.argv[1]
outdir = sys.argv[2] if len(sys.argv) > 2 else "."

data = open(exe, "rb").read()
print(f"{exe}: {len(data):,} bytes")

ascii_re = re.compile(rb"[\x20-\x7e]{4,}")
utf16_re = re.compile(rb"(?:[\x20-\x7e]\x00){4,}")

a = [m.group().decode("ascii") for m in ascii_re.finditer(data)]
u = [m.group().decode("utf-16-le") for m in utf16_re.finditer(data)]
print(f"ascii runs: {len(a):,}   utf16 runs: {len(u):,}")

uniq = list(dict.fromkeys(a + u))
print(f"unique runs: {len(uniq):,}")

with open(os.path.join(outdir, "exe_strings_all.txt"), "w", encoding="utf-8") as f:
    for s in uniq:
        f.write(s + "\n")

# UE reflected names: identifiers with an optional class prefix.
ident = re.compile(r"^[A-Za-z_][A-Za-z0-9_]{2,80}$")
names = [s for s in uniq if ident.match(s)]
print(f"identifier-shaped names: {len(names):,}")
with open(os.path.join(outdir, "exe_names.txt"), "w", encoding="utf-8") as f:
    for s in names:
        f.write(s + "\n")

# /Game and /Script asset paths
paths = sorted({s for s in uniq if re.match(r"^/(Game|Script|Engine)/", s)})
print(f"asset/script paths: {len(paths):,}")
with open(os.path.join(outdir, "exe_paths.txt"), "w", encoding="utf-8") as f:
    for s in paths:
        f.write(s + "\n")
