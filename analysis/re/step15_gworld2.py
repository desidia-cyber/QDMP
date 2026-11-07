import sys
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
from hunt2 import *
from capstone.x86 import X86_OP_MEM, X86_REG_RIP

DL = sec_by_name(".data")[1]; DH = DL + sec_by_name(".data")[2]
G = 0x04BA86B0
GE = 0x04BA4D88

def analyze(g, label):
    rs = refs_to(g)
    funcs = sorted(set(s[1] for s in rs))
    print("### %s  %08X : %d sites, %d distinct funcs" % (label, g, len(rs), len(funcs)))
    selfcmp = []
    writes = []
    for site, func, mn in rs:
        ins = insn_at(site)
        if ins is None: continue
        txt = ins.mnemonic + " " + ins.op_str
        if ins.mnemonic in ("cmp", "test"):
            selfcmp.append((site, func, txt))
        elif ins.operands and ins.operands[0].type == X86_OP_MEM and \
                ins.operands[0].mem.base == X86_REG_RIP and \
                (ins.address + ins.size + ins.operands[0].mem.disp) & 0xFFFFFFFF == g:
            writes.append((site, func, txt))
    print("   writes: %d" % len(writes))
    for w in writes[:10]:
        print("      %08X FUN_%08X  %s" % w)
    print("   cmp/test: %d" % len(selfcmp))
    for w in selfcmp[:10]:
        print("      %08X FUN_%08X  %s" % w)
    print("   sample funcs: %s" % ", ".join("%08X" % f for f in funcs[:20]))
    print()

analyze(G, "GWorld candidate")
analyze(GE, "GEngine (known)")
