#!/usr/bin/env python3
"""Scan the shipping exe for pak/modding-relevant strings (no disassembly)."""
import re
import sys

EXE = (r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64"
       r"\QueensDomain-Win64-Shipping.exe")

PATTERNS = [
    rb"bEnablePakSigning",
    rb"PakSigning",
    rb"UnsignedPak",
    rb"AllowUnsigned",
    rb"bSigned",
    rb"~mods",
    rb"\.pak",
    rb"Paks",
    rb"4\.2[0-9]\.[0-9]+",
    rb"UE4SS",
    rb"bUseIoStore",
    rb"IoStore",
    rb"bUsePakFile",
    rb"PakFile",
    rb"DefaultPakFile",
    rb"CompressionMethod",
    rb"bEnableMemoryMappedFiles",
    rb"LoADER",
    rb"SIGNATURE",
]


def main():
    found = {}
    with open(EXE, "rb") as fh:
        prev = b""
        while True:
            chunk = fh.read(1 << 22)
            if not chunk:
                break
            buf = prev + chunk
            for pat in PATTERNS:
                for m in re.finditer(pat, buf):
                    s = max(0, m.start() - 60)
                    ctx = buf[s:m.end() + 90]
                    # keep a printable rendering of the surrounding bytes
                    txt = "".join(chr(b) if 32 <= b < 127 else "." for b in ctx)
                    found.setdefault(pat, set()).add(txt)
            prev = buf[-256:]

    for pat in PATTERNS:
        hits = found.get(pat, ())
        print("\n### %s  (%d distinct contexts)" % (pat.decode(), len(hits)))
        for h in sorted(hits)[:6]:
            print("   ", h)


if __name__ == "__main__":
    main()
