#!/usr/bin/env python3
"""Version resource + build-tree path census."""
import re
import collections
import pefile
import struct

PATH = r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64\QueensDomain-Win64-Shipping.exe"
raw = open(PATH, "rb").read()
pe = pefile.PE(PATH)


def version_resource():
    print("=" * 78)
    print("VS_VERSIONINFO (from .rsrc)")
    print("=" * 78)
    if not hasattr(pe, "FileInfo"):
        print("no FileInfo")
        return
    seen = set()

    def walk(e, depth=0):
        for entry in e:
            if entry.Key in (b"VS_VERSION_INFO", b"StringFileInfo", b"VarFileInfo"):
                pass
        if hasattr(e, "StringTable"):
            for st in e.StringTable:
                for k, v in st.entries.items():
                    key = (st.LangID, k.decode("latin1", "replace"))
                    if key in seen:
                        continue
                    seen.add(key)
                    try:
                        val = v.decode("utf-16-le", "replace").rstrip("\0")
                    except Exception:
                        val = repr(v)
                    print("  %-24s %s" % (k.decode("latin1", "replace"), val))
        if getattr(e, "Var", None):
            for var in e.Var:
                print("  VAR %-20s %s" % (var.Key, var.entry))
        if hasattr(e, "FileInfo"):
            for fi in e.FileInfo:
                walk(fi)

    for fi in pe.FileInfo:
        if hasattr(fi, "Key"):
            walk([fi])
        else:
            for f in fi:
                walk([f])
    # fixed file info
    for fi in pe.FileInfo:
        items = fi if not hasattr(fi, "Key") else [fi]
        for f in items:
            if getattr(f, "FileVersionMS", None) is not None:
                print("  FileVersion    : %d.%d.%d.%d" % (
                    f.FileVersionMS >> 16, f.FileVersionMS & 0xFFFF,
                    f.FileVersionLS >> 16, f.FileVersionLS & 0xFFFF))
                print("  ProductVersion : %d.%d.%d.%d" % (
                    f.ProductVersionMS >> 16, f.ProductVersionMS & 0xFFFF,
                    f.ProductVersionLS >> 16, f.ProductVersionLS & 0xFFFF))


def path_census():
    print()
    print("=" * 78)
    print("BUILD-TREE PATH CENSUS (source paths baked into the image)")
    print("=" * 78)
    paths = set()
    for m in re.finditer(rb"[/A-Za-z0-9_.\\:\-]{20,}?\.(?:cpp|h|inl|cs|usf|ush)", raw):
        try:
            paths.add(m.group().decode("latin1"))
        except Exception:
            pass
    print("total distinct source paths: %d" % len(paths))
    roots = collections.Counter()
    for p in paths:
        idx = p.find("/workspace/")
        if idx >= 0:
            rest = p[idx + len("/workspace/"):]
            parts = rest.split("/")
            roots["/workspace/" + "/".join(parts[:3])] += 1
        else:
            roots[p.split("/")[0] if p.startswith("/") else "(relative)"] += 1
    print("\n-- top roots")
    for r, c in roots.most_common(30):
        print("  %-60s %d" % (r, c))

    # group engine vs project
    eng = [p for p in paths if "/Engine/" in p]
    prj = [p for p in paths if "/Engine/" not in p]
    print("\nengine paths: %d   non-engine paths: %d" % (len(eng), len(prj)))

    print("\n-- NON-ENGINE (project / plugin) source paths, sorted")
    for p in sorted(prj):
        print("   ", p)

    print("\n-- Engine/Source/Runtime module dirs (count)")
    mods = collections.Counter()
    for p in eng:
        i = p.find("/Engine/Source/Runtime/")
        if i >= 0:
            rest = p[i + len("/Engine/Source/Runtime/"):]
            mods[rest.split("/")[0]] += 1
    for m, c in mods.most_common(200):
        print("   %-32s %d" % (m, c))

    print("\n-- Engine/Plugins dirs")
    pl = collections.Counter()
    for p in eng:
        i = p.find("/Engine/Plugins/")
        if i >= 0:
            rest = p[i + len("/Engine/Plugins/"):]
            parts = rest.split("/")
            pl["/".join(parts[:2])] += 1
    for m, c in pl.most_common(80):
        print("   %-48s %d" % (m, c))


version_resource()
path_census()
