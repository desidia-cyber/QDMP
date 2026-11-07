import sys
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
import qdlib as q
import struct

def dump(rva, n, label=""):
    print("--- %s %08X ---" % (label, rva))
    for off in range(0, n, 16):
        b = q.read_rva(rva + off, 16)
        if not b:
            break
        asc = "".join(chr(c) if 32 <= c < 127 else "." for c in b)
        print("  %08X  %s  |%s|" % (rva + off, b.hex(" "), asc))

for r, lbl in [(0x2B620C0, "tail target of GetPlayerPawn"),
               (0x2B60C30, "AController::GetPawn?"),
               (0x028E0610, "candidate 0610"),
               (0x3F0D440, "reflection blob A"),
               (0x3F08D68, "pointer target from ReceiveTick rec"),
               (0x3F94D80, "spawn names"),
               (0x3F95040, "FinishSpawning names")]:
    print()
    for ins in q.disasm(r, count=14):
        print("  %08X %-9s %-46s" % (ins.address, ins.mnemonic, ins.op_str))
    print()

dump(0x3F0D400, 0x140, "ReceiveTick region")
dump(0x3F94D60, 0x100, "spawn region")
