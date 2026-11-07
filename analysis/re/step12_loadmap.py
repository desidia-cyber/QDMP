import sys
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
from hunt2 import *
from capstone.x86 import X86_OP_MEM, X86_REG_RIP

DL = sec_by_name(".data")[1]; DH = DL + sec_by_name(".data")[2]
RL = sec_by_name(".rdata")[1]; RH = RL + sec_by_name(".rdata")[2]
tlo, thi = 0x1000, 0x373B000
fstarts = set(f[0] for f in functions())
READONLY = {"cmp","test","bt","comiss","comisd","ucomiss","ucomisd"}

def describe(fn, insn_head=0):
    f = func_at(fn); end = f[1] if f else fn+0x400
    print("=== FUN_%08X  size 0x%X ===" % (fn, end-fn))
    for i, (ins, t) in enumerate(func_scan(fn, end)):
        if insn_head and i >= insn_head: break
        note = ""
        if t is not None:
            if DL <= t < DH: note = "  DATA %08X" % t
            elif RL <= t < RH:
                s = read_rva(t, 70)
                if s:
                    k = s.find(b"\x00"); s2 = s[:k] if k >= 0 else s
                    if s2 and all(0x20 <= c < 0x7f for c in s2):
                        note = "  str %r" % s2.decode("latin-1")[:52]
        if ins.mnemonic.startswith("call"):
            for op in ins.operands:
                if op.type == 2:
                    tg = op.imm & 0xFFFFFFFF
                    if tlo <= tg < thi:
                        sz = 0; ff = func_at(tg)
                        if ff: sz = ff[1]-ff[0]
                        note = "  call FUN_%08X (0x%X)" % (tg, sz)
                    else:
                        note = "  call ext %X" % tg
        print("  %08X %-9s %-46s%s" % (ins.address, ins.mnemonic, ins.op_str, note))

describe(0x02CE85B0, 40)
