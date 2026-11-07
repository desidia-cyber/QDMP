import sys, collections
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
from hunt2 import *
from capstone.x86 import X86_OP_MEM, X86_OP_REG, X86_OP_IMM, X86_REG_RIP

DL = sec_by_name(".data")[1]; DH = DL + sec_by_name(".data")[2]
tlo, thi = 0x1000, 0x373B000

hits = collections.defaultdict(list)
for (b, e, u) in functions():
    if b < tlo or b >= thi: continue
    sz = e - b
    if sz > 0x80 or sz < 6: continue
    insns = disasm(b, end_rva=e)
    if not insns: continue
    g = None; reg = None
    for ins in insns:
        if ins.mnemonic == "mov" and len(ins.operands) == 2:
            d, s = ins.operands
            if d.type == X86_OP_REG and s.type == X86_OP_MEM and s.mem.base == X86_REG_RIP:
                t = (ins.address + ins.size + s.mem.disp) & 0xFFFFFFFF
                if DL <= t < DH:
                    g = t; reg = d.reg
                    continue
            if d.type == X86_OP_REG and s.type == X86_OP_REG and reg is not None and s.reg == reg:
                reg = d.reg
                continue
        if ins.mnemonic in ("call", "jmp") and g is not None and ins.operands:
            op = ins.operands[0]
            if op.type == X86_OP_MEM and op.mem.base == reg and op.mem.index == 0:
                hits[g].append((b, sz, op.mem.disp, ins.mnemonic, ins.address))

print("=== tiny wrappers that vtable-dispatch through a .data pointer ===")
for g, hs in sorted(hits.items(), key=lambda kv: -len(kv[1]))[:25]:
    print("  %08X  n=%d" % (g, len(hs)))
    for x in hs[:5]:
        print("        FUN_%08X sz 0x%-4X %s [%s%s+0x%X] @%08X" % (x[0], x[1], x[3], "vt", "", x[2], x[4]))
