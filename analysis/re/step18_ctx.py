import sys
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
from hunt2 import *
from capstone.x86 import X86_OP_MEM, X86_REG_RIP

DL = sec_by_name(".data")[1]; DH = DL + sec_by_name(".data")[2]
tlo, thi = 0x1000, 0x373B000

def ctx(g, n=4, span=10):
    print("##### global %08X" % g)
    for site, func, mn in refs_to(g)[:n]:
        f = func_at(func); end = f[1] if f else site + 0x80
        print("  --- site %08X in FUN_%08X (size 0x%X) ---" % (site, func, (end - func)))
        shown = 0
        for ins, t in func_scan(func, end):
            if ins.address < site: continue
            if shown >= span: break
            note = ""
            if t is not None:
                note = ("   <<<G" if t == g else ("   DATA %08X" % t if DL <= t < DH else ""))
            if ins.mnemonic == "call" and ins.operands and ins.operands[0].type == 2:
                tg = ins.operands[0].imm & 0xFFFFFFFF
                if tlo <= tg < thi: note = "   call FUN_%08X" % tg
            print("      %08X %-9s %-38s%s" % (ins.address, ins.mnemonic, ins.op_str, note))
            shown += 1
        print()

for g in [0x049CBE20, 0x04C06368, 0x04B43768]:
    ctx(g, 3, 9)
