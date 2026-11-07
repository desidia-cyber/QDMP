import sys
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
from hunt2 import *
from capstone.x86 import X86_OP_MEM, X86_REG_RIP
DL = sec_by_name(".data")[1]; DH = DL + sec_by_name(".data")[2]
TL, TH = 0x1000, 0x373B000
for a in [0x00F8B5E0, 0x00F8F240, 0x00FB24B0, 0x00FABF20, 0x011B0300, 0x026553E0]:
    f = func_at(a)
    size = (f[1]-f[0]) if f else -1
    guo = 0
    if f:
        for ins, t in func_scan(a, f[1]):
            if t in (0x04A64250, 0x04A64260, 0x04A64274, 0x04A64258):
                guo += 1
    print("%08X size 0x%-6X callers=%-4d GUObjectArrayRefs=%d" % (a, size, len(callers(a)), guo))

print()
print("=== 0x00F8B5E0 first 40 insns ===")
i = 0
for ins in disasm(0x00F8B5E0, end_rva=0x00F8B5E0 + 0x140):
    i += 1
    note = ""
    if ins.mnemonic == "mov" and len(ins.operands) == 2:
        s = ins.operands[1]
        if s.type == X86_OP_MEM and s.mem.base == X86_REG_RIP:
            t = (ins.address + ins.size + s.mem.disp) & 0xFFFFFFFF
            if DL <= t < DH: note = "   ; DATA %08X" % t
    if ins.mnemonic in ("call", "jmp") and ins.operands and ins.operands[0].type == 2:
        tg = ins.operands[0].imm & 0xFFFFFFFF
        if TL <= tg < TH: note = "   ; FUN_%08X" % tg
    print("  %08X %-9s %-44s%s" % (ins.address, ins.mnemonic, ins.op_str, note))
    if i > 40: break
