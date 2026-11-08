#!/usr/bin/env python3
"""Dump the two native function tables that contain `LoadMapWithLoadingScreen`.

Each entry is `{ const char* Name; FNativeFuncPtr Pointer; }` (16 bytes).  The
neighbourhood names identify the class; the pointers are the exec thunks.  Printing
both tables and their callers is what pins which one belongs to the GameInstance.

    python analysis/re/travel_table_probe.py
"""
import struct

import qdlib

TABLES = [0x0394CDA8, 0x03950090]


def string_at(rva):
    b = qdlib.read_rva(rva, 80)
    if not b:
        return None
    run = b.split(b"\x00")[0]
    if 2 <= len(run) and all(0x20 <= c < 0x7f for c in run):
        return run.decode("latin-1")
    return None


def dump_table(center, before=24, after=40):
    print("\n== table around %08X ==" % center)
    for k in range(-before, after + 1):
        a = center + 8 * k
        v = qdlib.u64(a)
        if v is None:
            continue
        if not (qdlib.IMAGE_BASE <= v < qdlib.IMAGE_BASE + 0x05043000):
            print("  %+4d %08X  %016X" % (k, a, v))
            continue
        rva = v - qdlib.IMAGE_BASE
        s = string_at(rva)
        if s:
            # name entry: the next qword is the function
            fv = qdlib.u64(a + 8)
            fr = (fv - qdlib.IMAGE_BASE) if fv and qdlib.IMAGE_BASE <= fv < qdlib.IMAGE_BASE + 0x05043000 else None
            print("  %+4d %08X  name %-40r fn %s" % (k, a, s, ("%08X" % fr) if fr else "?"))
        else:
            f = qdlib.func_at(rva)
            print("  %+4d %08X  code %08X%s" % (k, a, rva, (" (FUN_%08X)" % f[0]) if f else ""))


def find_table_xrefs(table_rva):
    d = qdlib.data()
    want = struct.pack("<Q", qdlib.IMAGE_BASE + table_rva)
    hits = []
    for name, va, vsz, raw, rawsz, ch in qdlib.sections():
        blob = d[raw:raw + min(vsz, rawsz)]
        s = 0
        while True:
            i = blob.find(want, s)
            if i < 0:
                break
            if i % 8 == 0:
                hits.append(va + i)
            s = i + 1
    return hits


def main():
    qdlib._load()
    for t in TABLES:
        dump_table(t)
    print("\n== table pointer xrefs ==")
    for t in TABLES:
        for h in find_table_xrefs(t):
            print("  table %08X referenced at %08X" % (t, h))
            for ins in qdlib.disasm(h - 0x20, count=16):
                print("    %08X  %s %s" % (ins.address, ins.mnemonic, ins.op_str))
            f = qdlib.func_at(h)
            if f:
                print("    -> in FUN_%08X (%08X..%08X)" % (f[0], f[0], f[1]))
                print("    --- first 40 insns ---")
                for ins in qdlib.disasm(f[0], count=40):
                    print("      %08X  %s %s" % (ins.address, ins.mnemonic, ins.op_str))


if __name__ == "__main__":
    main()
