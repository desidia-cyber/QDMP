import struct, sys

p = r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64\QueensDomain-Win64-Shipping.exe"
d = open(p, "rb").read()

e_lfanew = struct.unpack_from("<I", d, 0x3C)[0]
nsec = struct.unpack_from("<H", d, e_lfanew + 6)[0]
optsz = struct.unpack_from("<H", d, e_lfanew + 20)[0]
opt = e_lfanew + 24
magic = struct.unpack_from("<H", d, opt)[0]
assert magic == 0x20B
ddoff = opt + 112
dirs = [struct.unpack_from("<II", d, ddoff + i * 8) for i in range(16)]

soff = opt + optsz
secs = []
for i in range(nsec):
    nm = d[soff + i*40: soff + i*40 + 8].rstrip(b"\0").decode("ascii", "replace")
    vs, va, rs, ra = struct.unpack_from("<IIII", d, soff + i*40 + 8)
    secs.append((nm, va, vs, ra, rs))

def r2o(rva):
    for nm, va, vs, ra, rs in secs:
        if va <= rva < va + max(vs, rs):
            return ra + (rva - va)
    return None

def cstr(off):
    e = d.index(b"\0", off)
    return d[off:e].decode("ascii", "replace")

print("=== SECTIONS ===")
for nm, va, vs, ra, rs in secs:
    print(f"  {nm:<10} va=0x{va:08x} vsize=0x{vs:08x} raw=0x{ra:08x} rsize=0x{rs:08x}")

# ---- normal imports ----
print("\n=== STATIC IMPORTS (import directory) ===")
irva, isz = dirs[1]
o = r2o(irva)
k = 0
while True:
    ilt, ts, fwd, nameRva, iat = struct.unpack_from("<IIIII", d, o + k*20)
    if nameRva == 0:
        break
    print(f"  {cstr(r2o(nameRva))}")
    k += 1

# ---- delay imports ----
print("\n=== DELAY-LOAD IMPORTS (delay import directory) ===")
drva, dsz = dirs[13]
if dsz == 0:
    print("  (none)")
else:
    o = r2o(drva)
    k = 0
    found = 0
    while True:
        attrs, nameRva, hmod, iat, int_, bound, unload, ts = struct.unpack_from("<IIIIIIII", d, o + k*32)
        if nameRva == 0 and attrs == 0 and hmod == 0:
            break
        nm = cstr(r2o(nameRva)) if nameRva else "?"
        print(f"  {nm}   attrs=0x{attrs:x} {'(RVA-based)' if attrs & 1 else '(VA-based)'}")
        found += 1
        k += 1
        if found > 32:
            break
