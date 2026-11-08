#!/usr/bin/env python3
"""Find the UFunction metadata for `LoadMapWithLoadingScreen` (G6).

The native registration table gives the exec thunk but not the parameter list.  The
UFunction's parameter properties reference their names from the pool; `MapPackage` is
the only name in that cluster that looks like a parameter.  Scanning data for
pointers to it finds the property metadata, and the tags around it name the type.

    python analysis/re/travel_ufunction_probe.py
"""
import struct

import qdlib

NAMES = {
    "MapPackage": 0x0394C890,
    "LoadMapWithLoadingScreen": 0x0394C418,
    "bLoadingMap": 0x0394D060,
    "CurrentlyLoadingMapPackage": 0x0394D070,
    "CurrentlyLoadingMap": 0x0394D090,
}


def ptr_hits(rva, secs=(".rdata", ".data")):
    d = qdlib.data()
    want = struct.pack("<Q", qdlib.IMAGE_BASE + rva)
    out = []
    for name, va, vsz, raw, rawsz, ch in qdlib.sections():
        if name not in secs:
            continue
        blob = d[raw:raw + min(vsz, rawsz)]
        s = 0
        while True:
            i = blob.find(want, s)
            if i < 0:
                break
            if i % 8 == 0:
                out.append(va + i)
            s = i + 1
    return out


def main():
    qdlib._load()
    for nm, rva in NAMES.items():
        hits = ptr_hits(rva)
        print("\n%s (%08X): %d pointer(s)" % (nm, rva, len(hits)))
        for h in hits[:10]:
            print("  at %08X" % h)
            for k in range(-3, 6):
                a = h + 8 * k
                v = qdlib.u64(a)
                if v is None:
                    continue
                if qdlib.IMAGE_BASE <= v < qdlib.IMAGE_BASE + 0x05043000:
                    t = v - qdlib.IMAGE_BASE
                    b = qdlib.read_rva(t, 48)
                    s = ""
                    if b:
                        run = b.split(b"\x00")[0]
                        if 2 <= len(run) and all(0x20 <= c < 0x7f for c in run):
                            s = run.decode("latin-1")
                    print("     %+d %08X -> %08X %r" % (k, a, t, s))
                else:
                    print("     %+d %08X = %016X" % (k, a, v))


if __name__ == "__main__":
    main()
