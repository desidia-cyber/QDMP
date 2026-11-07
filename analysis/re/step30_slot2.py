import sys
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
from hunt2 import *
from capstone.x86 import X86_OP_MEM, X86_REG_RIP

tlo, thi = 0x1000, 0x373B000
fstarts = set(f[0] for f in functions())

def describe_ptr(rva):
    v = u64(rva)
    if v is None: return "?"
    if not (IMAGE_BASE <= v < IMAGE_BASE + 0x6000000): return "const %X" % v
    r = v - IMAGE_BASE
    if tlo <= r < thi:
        return "TEXT %08X%s" % (r, "" if r in fstarts else " !NOTSTART")
    return "rva %08X" % r

for base in [0x0376D180, 0x0376D258]:
    print("=== around %08X ===" % base)
    for k in range(0, 0x70, 8):
        a = base + k
        print("  %08X  %s" % (a, describe_ptr(a)))
    print()

# how many of the 1818 occurrences sit at index 68 of a vtable built from contiguous .text ptrs?
import struct
D = data()
occs = []
pat = struct.pack("<Q", IMAGE_BASE + 0x011A7AA0)
o = rva_to_off(0x373B000); e = rva_to_off(0x46F0000 - 8)
i = D.find(pat, o, e)
while i >= 0:
    occs.append(off_to_rva(i)); i = D.find(pat, i+1, e)

def is_tp(a):
    v = u64(a)
    if v is None: return False
    r = v - IMAGE_BASE
    return IMAGE_BASE <= v and tlo <= r < thi and r in fstarts

from collections import Counter
idx = Counter()
for loc in occs:
    s = loc
    while s - 8 >= 0x373B000 and is_tp(s - 8): s -= 8
    idx[(loc - s)//8] += 1
print("index histogram within contiguous runs:", idx.most_common(12))
