import sys
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
from qdlib import *

d = data()
fstarts = set(f[0] for f in functions())

def dump_vt(rva, count=40):
    print("=== vtable candidate %08X ===" % rva)
    for k in range(count):
        v = u64(rva + k*8)
        r = v - IMAGE_BASE if v and v >= IMAGE_BASE else None
        tag = ""
        if r is not None:
            tag = "funcstart" if r in fstarts else ("text" if 0x1000 <= r < 0x373B000 else "?")
            f = func_at(r)
            if f: tag += " inFunc %08X" % f[0]
        print("  [%02d] %016X -> %s %s" % (k, v, ("%08X" % r) if r is not None else "-", tag))

dump_vt(0x0373D900, 34)
print()
dump_vt(0x0374C4E8, 30)
