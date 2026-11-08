#!/usr/bin/env python3
"""Disassemble the two `LoadMapWithLoadingScreen` exec thunks (SYNC-GAPS G6).

Registration tables found by travel_entry_probe2.py:

    0x0394CDA8  { GetActiveBaseSave 0x00E55620, InitSave 0x00E55650,
                  LoadMapWithLoadingScreen 0x00E55690, LoadSaveSynch 0x00E556C0, ... }
    0x03950090  { GetActiveBaseSave 0x00E58AA0, InitSave 0x00E58E20,
                  LoadMapWithLoadingScreen 0x00E58F70, LoadSaveSynch 0x00E590A0, ... }

A UE4 exec thunk is `(UObject* Context, FFrame& Stack, void* Result)`; it reads the
declared parameters out of the FFrame by property, so the property count and order are
visible in the disassembly.  The body after the parameter reads is the native call.

    python analysis/re/travel_thunk_probe.py
"""
import sys

import qdlib

THUNKS = [0x00E55690, 0x00E58F70]


def dump(rva, limit=200):
    f = qdlib.func_at(rva)
    print("\n== %08X  (%s .. %s) ==" %
          (rva, ("%08X" % f[0]) if f else "?", ("%08X" % f[1]) if f else "?"))
    for ins in qdlib.disasm(rva, count=limit):
        t = qdlib.insn_target(ins)
        tag = ""
        if t is not None:
            b = qdlib.read_rva(t, 64)
            if b:
                run = b.split(b"\x00")[0]
                if 3 <= len(run) and all(0x20 <= c < 0x7f for c in run):
                    tag = "   ; %r" % run.decode("latin-1")
                else:
                    tag = "   ; %08X" % t
        print("  %08X  %-24s %s%s" % (ins.address, ins.mnemonic + " " + ins.op_str, "", tag))


def main():
    qdlib._load()
    for t in THUNKS:
        dump(t)


if __name__ == "__main__":
    main()
