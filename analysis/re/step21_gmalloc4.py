import sys, collections
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
from hunt2 import *
from capstone.x86 import X86_OP_MEM, X86_OP_REG, X86_REG_RIP

DL = sec_by_name(".data")[1]; DH = DL + sec_by_name(".data")[2]
tlo, thi = 0x1000, 0x373B000

def init8(g):
    b = read_rva(g, 8)
    return int.from_bytes(b, "little") if b and len(b) == 8 else 0

def is_write_site(ins, g):
    if ins is None or len(ins.operands) != 2: return False
    d, s = ins.operands
    if d.type != X86_OP_MEM or d.mem.base != X86_REG_RIP: return False
    if (ins.address + ins.size + d.mem.disp) & 0xFFFFFFFF != g: return False
    return ins.mnemonic not in ("cmp", "test")

cands = []
for g, sites in xrefs().items():
    if not (DL <= g < DH): continue
    w = []
    for site, func, mn in sites:
        ins = insn_at(site)
        if is_write_site(ins, g):
            w.append((site, func, ins.mnemonic + " " + ins.op_str))
    if 1 <= len(w) <= 2 and len(sites) <= 60:
        cands.append((g, init8(g), len(sites), len(w), w))

print("candidates: %d" % len(cands))
print()
print("== zero-init, <=40 refs, 1-2 writes ==")
n = 0
for g, iv, nr, nw, w in cands:
    if iv != 0: continue
    if nr > 40: continue
    print("  %08X refs=%-4d writes=%d init=%016X" % (g, nr, nw, iv))
    for x in w:
        print("        write @%08X in FUN_%08X  %s" % x)
    n += 1
print("count:", n)
