import sys
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
from hunt2 import *
from capstone.x86 import X86_OP_MEM, X86_REG_RIP
DL = sec_by_name(".data")[1]; DH = DL + sec_by_name(".data")[2]
TL, TH = 0x1000, 0x373B000

f = func_at(0x026450F0)
print("func_at(0x026450F0):", f, "callers:", len(callers(0x026450F0)))
if f: print("size 0x%X" % (f[1]-f[0]))

print()
print("=== head of 0x026450F0 ===")
i = 0
for ins in disasm(0x026450F0, end_rva=0x026450F0 + 0x120):
    i += 1
    note = ""
    if ins.mnemonic == "mov" and len(ins.operands) == 2:
        s = ins.operands[1]
        if s.type == X86_OP_MEM and s.mem.base == X86_REG_RIP:
            t = (ins.address + ins.size + s.mem.disp) & 0xFFFFFFFF
            if DL <= t < DH: note = "   ; DATA %08X" % t
    if ins.mnemonic in ("call", "jmp") and ins.operands and ins.operands[0].type == 2:
        tg = ins.operands[0].imm & 0xFFFFFFFF
        if TL <= tg < TH:
            ff = func_at(tg)
            note = "   ; FUN_%08X (sz 0x%X)" % (tg, (ff[1]-ff[0]) if ff else 0)
    print("  %08X %-9s %-44s%s" % (ins.address, ins.mnemonic, ins.op_str, note))
    if i > 45: break
