import sys, struct
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
from hunt import *

D = data()
tlo = 0x1000; thi = 0x373B000
fstarts = set(f[0] for f in functions())

def what(v):
    if v == 0: return "0"
    if not (IMAGE_BASE <= v < IMAGE_BASE + 0x6000000): return "const %016X" % v
    rel = v - IMAGE_BASE
    if tlo <= rel < thi:
        return "TEXT %08X%s" % (rel, "" if rel in fstarts else " (mid)")
    s = read_rva(rel, 72)
    if s and len(s) > 2 and all(0x20 <= c < 0x7f or c == 0 for c in s[:6]):
        return "str %08X %r" % (rel, s.split(b"\x00")[0].decode("latin-1")[:48])
    return "rva %08X" % rel

def dumpq(a, n, stride=8):
    for k in range(n):
        r = a + k*stride
        v = u64(r)
        if v is None: return
        print("  %08X  %016X  %s" % (r, v, what(v)))

print("=== .rdata 0x03F98D60 (BeginDeferredActorSpawnFromClass abs-ptr site) ===")
dumpq(0x03F98D60, 24)
print()
print("=== .rdata 0x03F9AC48 ===")
dumpq(0x03F9AC48, 24)
print()
print("=== 0x02DB8420 (the shared code pointer) ===")
for ins, t in func_scan(0x02DB8420):
    print("   %08X %-8s %-30s" % (ins.address, ins.mnemonic, ins.op_str) +
          ("" if t is None else "   ; -> %s" % what(IMAGE_BASE + t)))
    if ins.address > 0x02DB8420 + 0xC0: break
