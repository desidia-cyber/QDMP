import sys
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
from hunt2 import *
from capstone.x86 import X86_OP_MEM, X86_REG_RIP

tlo, thi = 0x1000, 0x373B000
fstarts = set(f[0] for f in functions())
print("0x007746E0 in pdata starts:", 0x007746E0 in fstarts, " func_at:", func_at(0x007746E0))
print("0x007764F0 in pdata starts:", 0x007764F0 in fstarts)

def cls(a):
    v = u64(a)
    if v is None: return "?"
    if not (IMAGE_BASE <= v < IMAGE_BASE + 0x6000000): return "const %016X" % v
    r = v - IMAGE_BASE
    if tlo <= r < thi: return "TEXT %08X" % r
    return "rva %08X" % r

print()
print("=== find vtable start for the block ending at 0x0376D2A0 ===")
for a in range(0x0376CFE0, 0x0376D060, 8):
    print("  %08X  %s" % (a, cls(a)))
print()
print("predicted slot-68 base: 0x0376D258 - 0x220 = %08X" % (0x0376D258 - 0x220))
print()
print("=== entries 0x0376D038 .. 0x0376D0A0 ===")
for a in range(0x0376D038, 0x0376D0A8, 8):
    print("  %08X  %s" % (a, cls(a)))
