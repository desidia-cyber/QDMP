import sys
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
from hunt2 import *
for a in [0x02651320, 0x02660B80, 0x029AD950, 0x0256EC10, 0x028E0340, 0x02B620C0,
          0x02CDEDF0, 0x02D391C0, 0x011A7AA0, 0x00FB8DE0, 0x028D3C90, 0x028DCD50,
          0x02651580, 0x007753A0, 0x00774A70, 0x00776970, 0x02CE9A60]:
    f = func_at(a)
    print("%08X size 0x%-6X callers=%d" % (a, (f[1]-f[0]) if f else -1, len(callers(a))))

print()
print("=== 0x02651320 calls ===")
TL, TH = 0x1000, 0x373B000
for ins in disasm(0x02651320, end_rva=0x02651320 + 0x200):
    if ins.mnemonic in ("call",) and ins.operands and ins.operands[0].type == 2:
        tg = ins.operands[0].imm & 0xFFFFFFFF
        if TL <= tg < TH:
            ff = func_at(tg)
            print("  %08X call FUN_%08X (sz 0x%X)" % (ins.address, tg, (ff[1]-ff[0]) if ff else 0))
