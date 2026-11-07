import sys
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
from hunt2 import *
from capstone.x86 import X86_OP_MEM, X86_REG_RIP

DL = sec_by_name(".data")[1]; DH = DL + sec_by_name(".data")[2]
TL, TH = 0x1000, 0x373B000

print("=== references to FNamePool base 0x04A27F00 ===")
for site, func, mn in refs_to(0x04A27F00):
    ins = insn_at(site)
    print("  %08X FUN_%08X  %s %s" % (site, func, ins.mnemonic if ins else "?", ins.op_str if ins else ""))
print("  total:", len(refs_to(0x04A27F00)))
print()
print("=== references to 0x04A27EF8 (pool init flag) ===")
print("  total:", len(refs_to(0x04A27EF8)))
for site, func, mn in refs_to(0x04A27EF8)[:8]:
    print("  %08X FUN_%08X %s" % (site, func, mn))

print()
print("=== writers of GUObjectArray NumElements (0x04A64274) ===")
for site, func, mn in refs_to(0x04A64274):
    ins = insn_at(site)
    if ins and ins.operands and ins.operands[0].type == X86_OP_MEM and \
       ins.operands[0].mem.base == X86_REG_RIP and ins.mnemonic not in ("cmp","test"):
        print("  WRITE %08X FUN_%08X size 0x%X  %s %s" % (
            site, func, (func_at(func)[1]-func_at(func)[0]) if func_at(func) else 0,
            ins.mnemonic, ins.op_str))
