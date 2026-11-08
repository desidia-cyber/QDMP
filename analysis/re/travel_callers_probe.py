#!/usr/bin/env python3
"""Callers of the native hard-load body (SYNC-GAPS G6).

`LoadMapWithLoadingScreen`'s exec thunk ends in a call to 0x00E34F30, which sets
`bLoadingMap = 1`, copies the package string and starts an async world load.  If a
native function calls that body directly, that caller is the game's own hard-travel
path; if nothing does, the UFunction is the only entry and the mod drives it through
ProcessEvent.

    python analysis/re/travel_callers_probe.py
"""
import struct
import sys

import qdlib

TARGETS = {
    "LoadMap body": 0x00E34F30,
    "LoadMap thunk": 0x00E58F70,
    "LoadMap thunk (first table)": 0x00E55690,
    "OpenLevel dispatcher": 0x02DB8360,
}


def calls_to(tgt):
    d = qdlib.data()
    name, va, vsz, raw, rawsz, ch = qdlib.sec_by_name(".text")
    blob = d[raw:raw + rawsz]
    hits = []
    n = len(blob)
    for i in range(n - 5):
        if blob[i] != 0xE8:
            continue
        rel = struct.unpack("<i", blob[i + 1:i + 5])[0]
        site = va + i
        dest = (site + 5 + rel) & 0xFFFFFFFF
        if dest == tgt:
            hits.append(site)
    return hits


def main():
    qdlib._load()
    for label, tgt in TARGETS.items():
        hits = calls_to(tgt)
        print("\n%s (%08X): %d call(s)" % (label, tgt, len(hits)))
        for site in hits[:24]:
            f = qdlib.func_at(site)
            print("   site %08X in %s" % (site, ("FUN_%08X" % f[0]) if f else "?"))


if __name__ == "__main__":
    main()
