import sys, collections
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
from hunt2 import *
from capstone.x86 import X86_OP_MEM, X86_OP_REG, X86_REG_RIP

DL = sec_by_name(".data")[1]; DH = DL + sec_by_name(".data")[2]
CORE = (0x00E00000, 0x01300000)   # Core / CoreUObject neighbourhood

def init8(g):
    b = read_rva(g, 8)
    return int.from_bytes(b, "little") if b and len(b) == 8 else 0

cands = []
for g, sites in xrefs().items():
    if not (DL <= g < DH): continue
    funcs = set(s[1] for s in sites)
    if not (2 <= len(funcs) <= 12): continue
    if init8(g) != 0: continue
    if not all(CORE[0] <= f < CORE[1] for f in funcs): continue
    cands.append((g, len(funcs), sorted(funcs)))

print("globals in .bss, 2..12 referring Core functions:", len(cands))
for g, n, fs in cands[:60]:
    print("  %08X refs=%d  funcs=%s" % (g, n, ["%08X" % f for f in fs[:12]]))
