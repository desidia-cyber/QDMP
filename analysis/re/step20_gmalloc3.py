import sys, collections, math
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
from hunt2 import *
from capstone.x86 import X86_OP_MEM, X86_OP_REG, X86_REG_RIP

DL = sec_by_name(".data")[1]; DH = DL + sec_by_name(".data")[2]
tlo, thi = 0x1000, 0x373B000

# collect, per .data global, the set of vtable offsets dispatched after loading it
disp = collections.defaultdict(collections.Counter)
nref = collections.Counter()

for tgt, sites in xrefs().items():
    if not (DL <= tgt < DH):
        continue
    nref[tgt] = len(sites)
    for site, func, mn in sites:
        if mn not in ("mov", "jmp") and not mn.startswith("call"):
            pass
        ins = insn_at(site)
        if ins is None or ins.mnemonic != "mov" or len(ins.operands) != 2:
            continue
        d, s = ins.operands
        if d.type != X86_OP_REG or s.type != X86_OP_MEM or s.mem.base != X86_REG_RIP:
            continue
        if (ins.address + ins.size + s.mem.disp) & 0xFFFFFFFF != tgt:
            continue
        reg = d.reg
        f = func_at(site); end = f[1] if f else site + 0x100
        cnt = 0
        for ins2 in disasm(site, end_rva=min(end, site + 0x180)):
            if ins2.address == site: continue
            cnt += 1
            if cnt > 12: break
            if ins2.mnemonic in ("call", "jmp") and ins2.operands:
                op = ins2.operands[0]
                if op.type == X86_OP_MEM and op.mem.base == reg and op.mem.index == 0:
                    disp[tgt][op.mem.disp] += 1
                break
            if ins2.mnemonic == "mov" and len(ins2.operands) == 2:
                d2, s2 = ins2.operands
                if d2.type == X86_OP_REG and s2.type == X86_OP_REG and s2.reg == reg:
                    reg = d2.reg
                    continue
                if d2.type == X86_OP_REG and s2.type == X86_OP_MEM and s2.mem.base == X86_REG_RIP:
                    break
            if ins2.mnemonic in ("ret", "jmp"):
                break

WANT = {0x10, 0x18, 0x20, 0x30}
print("=== globals matching FMalloc-ish dispatch offsets ===")
scored = []
for g, c in disp.items():
    s = set(c.keys())
    sc = len(s & WANT)
    if sc >= 2:
        scored.append((sc, g, sorted(s), nref[g]))
for sc, g, s, n in sorted(scored, key=lambda x: (-x[0], x[3]))[:25]:
    print("  %08X  match=%d/4  nref=%-5d offs=%s" % (g, sc, n, [hex(x) for x in s[:14]]))
