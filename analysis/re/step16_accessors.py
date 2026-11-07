import sys
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
from hunt2 import *
from capstone.x86 import X86_OP_MEM, X86_REG_RIP

DL = sec_by_name(".data")[1]; DH = DL + sec_by_name(".data")[2]
RL = sec_by_name(".rdata")[1]; RH = RL + sec_by_name(".rdata")[2]
tlo, thi = 0x1000, 0x373B000

def show(fn, maxn=60):
    f = func_at(fn); end = f[1] if f else fn + 0x100
    print("=== FUN_%08X size 0x%X ===" % (fn, end - fn))
    for i, (ins, t) in enumerate(func_scan(fn, end)):
        if i >= maxn: print("   ..."); break
        note = ""
        if t is not None:
            if t == 0x04BA86B0: note = "   <<< GWorld cand"
            elif t == 0x04BA4D88: note = "   <<< GEngine"
            elif DL <= t < DH: note = "   DATA %08X" % t
            else:
                s = read_rva(t, 50)
                if s:
                    k = s.find(b"\x00"); s2 = s[:k] if k >= 0 else s
                    if s2 and all(0x20 <= c < 0x7f for c in s2):
                        note = "   str %r" % s2.decode("latin-1")[:40]
        if ins.mnemonic == "call" and ins.operands and ins.operands[0].type == 2:
            tg = ins.operands[0].imm & 0xFFFFFFFF
            if tlo <= tg < thi: note = "   call FUN_%08X" % tg
        print("  %08X %-9s %-40s%s" % (ins.address, ins.mnemonic, ins.op_str, note))

for fn in [0x0077C620, 0x026397A0, 0x0272DB7C, 0x0272DAF0]:
    show(fn); print()
