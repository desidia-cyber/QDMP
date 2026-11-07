import sys, struct
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
from hunt2 import *
from capstone.x86 import X86_OP_MEM, X86_REG_RIP

D = data()
RL = sec_by_name(".rdata")[1]; RH = RL + sec_by_name(".rdata")[2]
tlo, thi = 0x1000, 0x373B000
fstarts = set(f[0] for f in functions())

def occurrences_of_ptr(rva):
    pat = struct.pack("<Q", IMAGE_BASE + rva)
    out = []
    o = rva_to_off(RL)
    end = rva_to_off(RH - 8)
    i = D.find(pat, o, end)
    while i >= 0:
        out.append(off_to_rva(i))
        i = D.find(pat, i + 1, end)
    return out

PE = 0x011A7AA0
print("occurrences of ProcessEvent ptr in .rdata: ")
locs = occurrences_of_ptr(PE)
print(len(locs))

def is_textptr_rva(rva):
    v = u64(rva)
    if v is None: return False
    r = v - IMAGE_BASE
    return IMAGE_BASE <= v and tlo <= r < thi and r in fstarts

for loc in locs[:6]:
    # walk back to vtable start
    s = loc
    while s - 8 >= RL and is_textptr_rva(s - 8):
        s -= 8
    # walk forward
    e = loc
    while e + 8 < RH and is_textptr_rva(e + 8):
        e += 8
    slots = (e - s) // 8 + 1
    print("  at %08X -> vtable %08X .. %08X  slots=%d  index=%d"
          % (loc, s, e, slots, (loc - s) // 8))
    print("     first 4 entries: %s" % [("%08X" % (u64(s + k*8) - IMAGE_BASE)) for k in range(min(4, slots))])
    print("     slot68=%s slot69=%s" % (
        ("%08X" % (u64(s+68*8) - IMAGE_BASE)) if (s+68*8) <= e else "-",
        ("%08X" % (u64(s+69*8) - IMAGE_BASE)) if (s+69*8) <= e else "-"))
