import sys
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
from hunt2 import *
from capstone.x86 import X86_OP_MEM, X86_REG_RIP
DL = sec_by_name(".data")[1]; DH = DL + sec_by_name(".data")[2]
TL, TH = 0x1000, 0x373B000
def show_range(a, b, only=None):
    print("=== raw %08X..%08X ===" % (a, b))
    for ins in disasm(a, end_rva=b):
        note = ""
        if ins.mnemonic == "mov" and len(ins.operands) == 2:
            s = ins.operands[1]
            if s.type == X86_OP_MEM and s.mem.base == X86_REG_RIP:
                t = (ins.address + ins.size + s.mem.disp) & 0xFFFFFFFF
                if DL <= t < DH: note = "   ; DATA %08X" % t
        if ins.mnemonic in ("call", "jmp") and ins.operands and ins.operands[0].type == 2:
            tg = ins.operands[0].imm & 0xFFFFFFFF
            if TL <= tg < TH: note = "   ; FUN_%08X" % tg
        if only and not note: continue
        print("  %08X %-9s %-46s%s" % (ins.address, ins.mnemonic, ins.op_str, note))
show_range(0x029AD950, 0x029ADB40, only=True)
