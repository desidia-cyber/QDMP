import sys, struct
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
from hunt import *

D = data()
tlo = 0x1000; thi = 0x373B000
RL = sec_by_name(".rdata")[1]; RH = RL + sec_by_name(".rdata")[2]
fstarts = set(f[0] for f in functions())

def name_at(rva):
    if not (RL <= rva < RH): return None
    s = read_rva(rva, 96)
    if not s: return None
    i = s.find(b"\x00")
    s = s[:i] if i >= 0 else s
    if not s or not all(0x20 <= c < 0x7f for c in s): return None
    return s.decode("latin-1")

def describe(rva, span=0x60):
    print("--- raw rows around %08X ---" % rva)
    a = rva - 0x30
    for k in range(0, span, 8):
        r = a + k
        v = u64(r)
        rel = (v - IMAGE_BASE) if v >= IMAGE_BASE else None
        tag = ""
        if rel is not None:
            if tlo <= rel < thi: tag = "<TEXT %08X>" % rel
            else:
                n = name_at(rel)
                tag = ("<str %r>" % n) if n else ("<rva %08X>" % rel)
        print("  %08X  %016X %s" % (r, v, tag))

describe(0x03F9B028)
print()
describe(0x03F9AC90)
print()
print("=== is FUN_02DBFE10 an exec thunk? first 12 insns ===")
for i, (ins, t) in enumerate(func_scan(0x02DBFE10)):
    if i > 11: break
    print("   %08X %-9s %s" % (ins.address, ins.mnemonic, ins.op_str))
