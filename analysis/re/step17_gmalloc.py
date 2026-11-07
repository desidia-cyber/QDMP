import sys, collections
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
from hunt2 import *
from capstone.x86 import X86_OP_MEM, X86_OP_REG, X86_REG_RIP

DL = sec_by_name(".data")[1]; DH = DL + sec_by_name(".data")[2]

CAND = [0x049CBE20, 0x04C06368, 0x049DFB18, 0x049DFB08, 0x049D5758,
        0x04B43768, 0x04B536A4, 0x04BA4D88, 0x04BA86B0]

def vtable_calls(g):
    """count sites: mov reg,[rip+G] ... call qword ptr [reg+disp] within next 6 insns"""
    hits = []
    for site, func, mn in refs_to(g):
        ins = insn_at(site)
        if ins is None: continue
        if not ins.operands or ins.operands[0].type != X86_OP_MEM: continue
        if ins.operands[0].mem.base != X86_REG_RIP: continue
        if (ins.address + ins.size + ins.operands[0].mem.disp) & 0xFFFFFFFF != g: continue
        # find dest register
        try:
            dst = ins.operands[1].reg
        except Exception:
            continue
        # look ahead
        f = func_at(site); end = f[1] if f else site + 0x40
        seen = False
        for ins2 in disasm(site, end_rva=min(end, site + 96)):
            if ins2.address == site: seen = True; continue
            if not seen: continue
            if ins2.mnemonic == "call":
                for op in ins2.operands:
                    if op.type == X86_OP_MEM and op.mem.base == dst:
                        hits.append((site, func, ins2.op_str))
                        break
                break
            if ins2.mnemonic in ("mov", "test", "cmp", "je", "jne", "ret", "jmp", "xor"):
                if ins2.mnemonic in ("ret", "jmp"): break
    return hits

for g in CAND:
    h = vtable_calls(g)
    print("%08X  vtable-dispatch-after-load sites: %-5d  distinct funcs: %d"
          % (g, len(h), len(set(x[1] for x in h))))
    for x in h[:5]:
        print("      %08X FUN_%08X  call %s" % x)
