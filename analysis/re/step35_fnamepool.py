import sys
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
from hunt2 import *
from capstone.x86 import X86_OP_MEM, X86_REG_RIP

DL = sec_by_name(".data")[1]; DH = DL + sec_by_name(".data")[2]
RL = sec_by_name(".rdata")[1]; RH = RL + sec_by_name(".rdata")[2]
TL, TH = 0x1000, 0x373B000

f = func_at(0x00FB8DE0)
print("=== FUN_00FB8DE0 [%08X..%08X) size 0x%X ===" % (0x00FB8DE0, f[1], f[1]-0x00FB8DE0))
for ins, t in func_scan(0x00FB8DE0, f[1]):
    note = ""
    if t is not None:
        if DL <= t < DH:
            note = "   ; DATA %08X" % t
        elif RL <= t < RH:
            s = read_rva(t, 60)
            if s:
                k = s.find(b"\x00"); s2 = s[:k] if k >= 0 else s
                if s2 and all(0x20 <= c < 0x7f for c in s2):
                    note = "   ; str %r" % s2.decode("latin-1")[:40]
    if ins.mnemonic in ("call", "jmp") and ins.operands and ins.operands[0].type == 2:
        tg = ins.operands[0].imm & 0xFFFFFFFF
        if TL <= tg < TH: note = "   ; FUN_%08X" % tg
    print("  %08X %-9s %-44s%s" % (ins.address, ins.mnemonic, ins.op_str, note))
