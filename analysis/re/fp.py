import sys, collections
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
from hunt2 import *
from capstone.x86 import X86_OP_MEM, X86_REG_RIP

DL = sec_by_name(".data")[1]; DH = DL + sec_by_name(".data")[2]
RL = sec_by_name(".rdata")[1]; RH = RL + sec_by_name(".rdata")[2]
tlo, thi = 0x1000, 0x373B000

def fingerprint(fn, maxstr=6, maxcall=10):
    f = func_at(fn)
    if not f:
        print("  FUN_%08X ???" % fn); return
    end = f[1]
    strs, calls, dw = [], [], []
    for ins, t in func_scan(fn, end):
        if t is not None:
            if RL <= t < RH:
                s = read_rva(t, 60)
                if s:
                    k = s.find(b"\x00"); s2 = s[:k] if k >= 0 else s
                    if s2 and len(s2) >= 4 and all(0x20 <= c < 0x7f for c in s2):
                        strs.append(s2.decode("latin-1")[:44])
            elif DL <= t < DH:
                dw.append(t)
        if ins.mnemonic == "call" and ins.operands and ins.operands[0].type == 2:
            tg = ins.operands[0].imm & 0xFFFFFFFF
            if tlo <= tg < thi:
                calls.append(tg)
    calls_named = []
    for c in dict.fromkeys(calls):
        ff = func_at(c)
        calls_named.append("%08X(0x%X)" % (c, (ff[1]-ff[0]) if ff else 0))
    print("  FUN_%08X size=0x%-5X" % (fn, end - fn))
    if strs:
        print("      strs: %s" % "; ".join(dict.fromkeys(strs)[:maxstr] if isinstance(dict.fromkeys(strs), list) else list(dict.fromkeys(strs))[:maxstr]))
    if calls_named:
        print("      calls: %s" % ", ".join(calls_named[:maxcall]))
    if dw:
        print("      data: %s" % ", ".join("%08X" % x for x in sorted(set(dw))[:12]))

funcs = sorted(set(s[1] for s in refs_to(0x04BA86B0)))
for fn in funcs:
    fingerprint(fn)
