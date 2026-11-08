#!/usr/bin/env python3
"""Second pass on the native hard-map-load entry (SYNC-GAPS G6).

The first pass (travel_entry_probe.py) found `LoadMapWithLoadingScreen` once, in the
FName pool, with no rip-relative code reference: so it is registered through a table,
not called by a direct `lea`.  UE4's native-function registration is an array of
`{const char* NameUTF8; FNativeFuncPtr Pointer}`; a shipping build may instead hold
pointers to the pooled name.  This pass scans .rdata/.data for 8-byte pointers at the
literal and walks the surrounding table, which both identifies the owning class's
native function set and gives the exec thunk addresses.

    python analysis/re/travel_entry_probe2.py
"""
import struct
import sys

import qdlib


def u64_at_off(off):
    return struct.unpack("<Q", qdlib.data()[off:off + 8])[0]


def scan_pointer(base_rva, secs=(".rdata", ".data")):
    d = qdlib.data()
    want = qdlib.IMAGE_BASE + base_rva
    hits = []
    for name, va, vsz, raw, rawsz, ch in qdlib.sections():
        if name not in secs:
            continue
        blob = d[raw:raw + min(vsz, rawsz)]
        start = 0
        needle = struct.pack("<Q", want)
        while True:
            i = blob.find(needle, start)
            if i < 0:
                break
            # 8-byte aligned, mapped
            if i % 8 == 0:
                hits.append(va + i)
            start = i + 1
    return hits


def nearby_string(rva):
    b = qdlib.read_rva(rva, 96)
    if not b:
        return ""
    run = b.split(b"\x00")[0]
    if len(run) >= 3 and all(0x20 <= c < 0x7f for c in run):
        return run.decode("latin-1")
    return ""


def main():
    qdlib._load()
    lit = 0x0394C418
    print("literal RVA %08X = %r" % (lit, nearby_string(lit)))
    hits = scan_pointer(lit)
    print("pointer hits: %d" % len(hits))
    for h in hits:
        print("  pointer at RVA %08X" % h)
        # Show +-8 qwords as (string, rva) pairs.
        for k in range(-4, 5):
            a = h + 8 * k
            v = qdlib.u64(a)
            if v is None:
                continue
            if qdlib.IMAGE_BASE <= v < qdlib.IMAGE_BASE + 0x05043000:
                tr = v - qdlib.IMAGE_BASE
                print("    %+d %08X -> %08X  %r" % (k, a, tr, nearby_string(tr)))
            else:
                f = qdlib.func_at(v - qdlib.IMAGE_BASE) if qdlib.IMAGE_BASE <= v < qdlib.IMAGE_BASE + 0x05043000 else None
                print("    %+d %08X = %016X" % (k, a, v))

    # Also: does a raw C string form exist anywhere?
    print("\nraw C-string occurrences:")
    d = qdlib.data()
    needle = b"LoadMapWithLoadingScreen\x00"
    s = 0
    while True:
        i = d.find(needle, s)
        if i < 0:
            break
        print("  off %08X rva %s" % (i, qdlib.off_to_rva(i)))
        s = i + 1


if __name__ == "__main__":
    main()
