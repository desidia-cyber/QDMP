import sys
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
from hunt2 import *
from capstone.x86 import X86_OP_MEM, X86_REG_RIP

DL = sec_by_name(".data")[1]; DH = DL + sec_by_name(".data")[2]

def show(fn, maxn=200):
    f = func_at(fn); end = f[1] if f else fn + 0x100
    print("=== FUN_%08X size 0x%X ===" % (fn, end - fn))
    for i, (ins, t) in enumerate(func_scan(fn, end)):
        if i >= maxn: print("  ..."); break
        note = ""
        if t is not None and DL <= t < DH:
            note = "   ; DATA %08X" % t
            if t == 0x04BA4D88: note += " <GENGINE>"
            if t == 0x04BA86B0: note += " <GWORLD>"
        if ins.mnemonic in ("call", "jmp") and ins.operands and ins.operands[0].type == 2:
            tg = ins.operands[0].imm & 0xFFFFFFFF
            if 0x1000 <= tg < 0x373B000: note = "   ; FUN_%08X" % tg
        print("  %08X %-9s %-46s%s" % (ins.address, ins.mnemonic, ins.op_str, note))

show(0x028D3C90, 70)
print()
show(0x02DBDF80, 100)
