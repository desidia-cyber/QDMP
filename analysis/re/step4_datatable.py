import sys, struct
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
from hunt import *

tlo = 0x1000; thi = 0x373B000
fstarts = set(f[0] for f in functions())

def dump8(a, n=24, base=0x047AC000):
    for k in range(n):
        r = a + k*8
        v = u64(r)
        if v is None: break
        rel = v - IMAGE_BASE
        tag = ""
        if IMAGE_BASE <= v < IMAGE_BASE + 0x6000000:
            if tlo <= rel < thi:
                tag = "<TEXT %08X%s>" % (rel, "" if rel in fstarts else " (not fn start)")
            else:
                try:
                    s = read_rva(rel, 64)
                    if s and all(0x20 <= c < 0x7f or c == 0 for c in s[:8]):
                        txt = s.split(b"\x00")[0].decode("latin-1")
                        tag = "<str %08X %r>" % (rel, txt[:50])
                    else:
                        tag = "<rva %08X>" % rel
                except Exception:
                    tag = "<rva %08X>" % rel
        else:
            tag = "<const %016X>" % v
        print("  %08X  %016X  %s" % (r, v, tag))

print("=== .data around GetPlayerPawn's abs-ptr site 0x47AC460 ===")
dump8(0x047AC400, 40)
