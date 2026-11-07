import sys
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
from hunt2 import *
from capstone.x86 import X86_OP_MEM, X86_OP_REG, X86_REG_RIP

DL = sec_by_name(".data")[1]; DH = DL + sec_by_name(".data")[2]
GUO = {0x04A64250, 0x04A64258, 0x04A64260, 0x04A64274}

print("=== all memory-indirect calls inside 0x029AD950..+0x3A0 ===")
for ins in disasm(0x029AD950, end_rva=0x029AD950 + 0x3A0):
    if ins.mnemonic in ("call", "jmp") and ins.operands:
        op = ins.operands[0]
        if op.type == X86_OP_MEM:
            print("  %08X %-6s %s" % (ins.address, ins.mnemonic, ins.op_str))
        elif op.type == 2:
            tg = op.imm & 0xFFFFFFFF
            if 0x1000 <= tg < 0x373B000:
                pass

print()
print("=== GUObjectArray refs in 0x029AD950..+0x3A0 ===")
for ins in disasm(0x029AD950, end_rva=0x029AD950 + 0x3A0):
    for k, op in enumerate(ins.operands):
        if op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP:
            t = (ins.address + ins.size + op.mem.disp) & 0xFFFFFFFF
            if t in GUO:
                print("  %08X %-9s %s   ; GUO %08X" % (ins.address, ins.mnemonic, ins.op_str, t))
