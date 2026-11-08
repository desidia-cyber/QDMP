#!/usr/bin/env python3
"""Locate the game's own hard-map-load entry (SYNC-GAPS G6).

`LoadMapWithLoadingScreen` exists in the exe's name pool and in NO cooked package,
so it is a native UFunction, not Blueprint.  It sits in the same name cluster as
`CompletedMapPackageLoad`, `GetActiveBaseSave`, `InitSave`, `MapPackage`,
`CurrentlyLoadingMap`, `bLoadingMap` -- i.e. the GameInstance, not a menu.

This probe finds the literal, the code that references it, and the function that
contains that reference, so the mod can call the game's own hard-travel path rather
than `UGameplayStatics::OpenLevel` (which only menus, death and cheats import).

Run from the repo root:

    python analysis/re/travel_entry_probe.py
"""
import struct
import sys

import qdlib

TARGETS = [b"LoadMapWithLoadingScreen", b"CompletedMapPackageLoad",
           b"CurrentlyLoadingMapPackage", b"CurrentlyLoadingMap", b"bLoadingMap",
           b"OpenLevelBySoftObjectPtr"]


def find_literals(needle):
    d = qdlib.data()
    out = []
    start = 0
    while True:
        i = d.find(needle, start)
        if i < 0:
            break
        rva = qdlib.off_to_rva(i)
        if rva is not None:
            out.append(rva)
        start = i + 1
    return out


def main():
    qdlib._load()
    idx = qdlib.build_rip_xref_index()
    print("xref index entries:", len(idx))
    for needle in TARGETS:
        lits = find_literals(needle)
        print("\n%s: %d literal(s)" % (needle.decode(), len(lits)))
        for rva in lits:
            refs = qdlib.find_xrefs(rva, idx)
            print("  literal at RVA %08X, %d xref(s)" % (rva, len(refs)))
            for site, mn in refs[:12]:
                f = qdlib.func_at(site)
                fr = "FUN_%08X" % f[0] if f else "?"
                print("    %08X  %-5s  in %s" % (site, mn, fr))
            # Print the function prologue and nearby strings for the first xref.
            if refs:
                f = qdlib.func_at(refs[0][0])
                if f:
                    print("    --- %s prologue ---" % ("FUN_%08X" % f[0]))
                    for ins in qdlib.disasm(f[0], count=24):
                        print("      %08X  %s %s" % (ins.address, ins.mnemonic, ins.op_str))
                    # Which other named literals does this function reference?
                    seen = []
                    for ins in qdlib.disasm(f[0], end_rva=f[1]):
                        t = qdlib.insn_target(ins)
                        if t is None:
                            continue
                        b = qdlib.read_rva(t, 64)
                        if not b:
                            continue
                        run = b.split(b"\x00")[0]
                        if 4 <= len(run) and all(0x20 <= c < 0x7f for c in run):
                            s = run.decode("latin-1")
                            if s not in seen:
                                seen.append(s)
                    print("    --- string refs ---")
                    for s in seen[:60]:
                        print("      ", s)


if __name__ == "__main__":
    main()
