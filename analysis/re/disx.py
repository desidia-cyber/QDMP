import sys
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
from hunt import *

RL = sec_by_name(".rdata")[1]; RH = RL + sec_by_name(".rdata")[2]
DL = sec_by_name(".data")[1]; DH = DL + sec_by_name(".data")[2]
tlo = 0x1000; thi = 0x373B000
fstarts = set(f[0] for f in functions())

def desc(r):
    if tlo <= r < thi:
        return "FUN_%08X%s" % (r, "" if r in fstarts else " (+%08X)" % r)
    if RL <= r < RH:
        s = read_rva(r, 96)
        if s:
            i = s.find(b"\x00")
            s = s[:i] if i >= 0 else s
            if s and all(0x20 <= c < 0x7f for c in s):
                return "str %r" % s.decode("latin-1")[:56]
        return "rdata %08X" % r
    if DL <= r < DH:
        return "DATA %08X" % r
    return "rva %08X" % r

def show(rva, limit=100000):
    f = func_at(rva)
    end = f[1] if f else rva + 0x200
    print("--- FUN_%08X  [%08X..%08X] size 0x%X ---" % (rva, rva, end, end - rva))
    for ins, t in func_scan(rva, end):
        line = "  %08X  %-9s %s" % (ins.address, ins.mnemonic, ins.op_str)
        if t is not None:
            d = desc(t)
            if not d.startswith("rdata") and not d.startswith("DATA"):
                line += "   ; " + d
            elif d.startswith("DATA"):
                line += "   ; " + d
        if ins.mnemonic.startswith("call") or ins.mnemonic.startswith("jmp"):
            for op in ins.operands:
                if op.type == 2:  # IMM
                    tgt = op.imm & 0xFFFFFFFF
                    if tlo <= tgt < thi:
                        line += "   ; -> FUN_%08X" % tgt
        print(line)

for a in [0x02DB7D20, 0x02DBF950]:
    show(a); print()
print("############ GetWorldDeltaSeconds ############")
for a in [0x02DB8100, 0x02DBFE10]:
    show(a); print()
