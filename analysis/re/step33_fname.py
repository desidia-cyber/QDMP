import sys
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
from hunt2 import *
from capstone.x86 import X86_OP_MEM, X86_REG_RIP

DL = sec_by_name(".data")[1]; DH = DL + sec_by_name(".data")[2]

def show(fn, maxn=90, start=None):
    f = func_at(fn); end = f[1] if f else fn + 0x100
    print("=== FUN_%08X size 0x%X ===" % (fn, end - fn))
    i = 0
    for ins, t in func_scan(fn, end):
        if start is not None and ins.address < start: continue
        if i >= maxn: print("  ..."); break
        i += 1
        note = ""
        if t is not None and DL <= t < DH: note = "   ; DATA %08X" % t
        if ins.mnemonic in ("call", "jmp") and ins.operands and ins.operands[0].type == 2:
            tg = ins.operands[0].imm & 0xFFFFFFFF
            if 0x1000 <= tg < 0x373B000: note = "   ; FUN_%08X" % tg
        print("  %08X %-9s %-46s%s" % (ins.address, ins.mnemonic, ins.op_str, note))

show(0x02E18320, 40)
print()
show(0x02E29640, 40)
