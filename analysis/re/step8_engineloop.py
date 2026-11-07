import sys
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
from hunt import *

D = data()
DL = sec_by_name(".data")[1]; DH = DL + sec_by_name(".data")[2]
tlo = 0x1000; thi = 0x373B000
RL = sec_by_name(".rdata")[1]; RH = RL + sec_by_name(".rdata")[2]

def dis(func, show_data_only=False):
    f = func_at(func)
    end = f[1] if f else func + 0x200
    print("--- FUN_%08X [%08X..%08X] size 0x%X ---" % (func, func, end, end-func))
    for ins, t in func_scan(func, end):
        mark = ""
        if t is not None:
            if DL <= t < DH:
                mark = "   ; DATA %08X" % t
                if ins.mnemonic == "mov" and ins.op_str.startswith("qword ptr ["):
                    mark += "  <<WRITE?"
                elif ins.mnemonic == "mov":
                    mark += "  (load)"
            elif RL <= t < RH:
                s = read_rva(t, 80)
                if s:
                    i = s.find(b"\x00"); s2 = s[:i] if i >= 0 else s
                    if s2 and all(0x20 <= c < 0x7f for c in s2):
                        mark = "   ; str %r" % s2.decode("latin-1")[:60]
        if ins.mnemonic.startswith("call") and ins.operands and ins.operands[0].type == 2:
            tg = ins.operands[0].imm & 0xFFFFFFFF
            if tlo <= tg < thi:
                mark = "   ; call FUN_%08X" % tg
        if show_data_only and "DATA" not in mark:
            continue
        print("  %08X  %-9s %-42s%s" % (ins.address, ins.mnemonic, ins.op_str, mark))

dis(0x00774A70)
