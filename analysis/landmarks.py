#!/usr/bin/env python3
"""Locate structural landmarks in the Queens Domain saves."""
import os, struct, re, collections

SAVES = r"C:\Users\ALVIBO\AppData\Local\QueensDomain\Saved\SaveGames"
FILES = ["System0.sav", "World0.sav", "Challenges"]

for f in FILES:
    data = open(os.path.join(SAVES, f), "rb").read()
    print("=" * 78)
    print(f"{f} size={len(data)}")
    # every printable run >=6 with its offset
    for m in re.finditer(rb"[ -~]{6,}", data):
        if m.start() < 0 or True:
            s = m.group().decode()
            if "/" in s or s in ("None",) or s.endswith("Property"):
                print(f"  @{m.start():6d} ({m.start():#06x}) len={len(s):4d} {s[:100]}")
    if f == "System0.sav":
        print("  --- first 0x140 bytes ---")
        for i in range(0, 0x120, 16):
            chunk = data[i:i + 16]
            print(f"   {i:04x}  {chunk.hex(' ')}  {''.join(chr(c) if 32<=c<127 else '.' for c in chunk)}")
