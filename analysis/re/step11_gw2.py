import sys
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
from hunt2 import *
from capstone.x86 import X86_OP_MEM, X86_REG_RIP

DL = sec_by_name(".data")[1]; DH = DL + sec_by_name(".data")[2]
tlo, thi = 0x1000, 0x373B000
READONLY = {"cmp", "test", "bt", "comiss", "comisd", "ucomiss", "ucomisd"}

def writes_in(fn):
    f = func_at(fn); end = f[1] if f else fn + 0x400
    out = []
    for ins, t in func_scan(fn, end):
        if t is None or not (DL <= t < DH):
            continue
        if not ins.operands or ins.operands[0].type != X86_OP_MEM:
            continue
        if ins.mnemonic in READONLY:
            continue
        if ins.operands[0].mem.base != X86_REG_RIP:
            continue
        out.append((ins.address, t, ins.mnemonic, ins.op_str))
    return out

for fn in [0x00BB4790, 0x02B4A0E0, 0x02CE85B0, 0x02D47B08]:
    f = func_at(fn)
    print("### FUN_%08X size 0x%X : .data writes" % (fn, (f[1]-f[0]) if f else 0))
    for a, t, m, o in writes_in(fn):
        print("    %08X -> DATA %08X   %s %s" % (a, t, m, o))
    print()
