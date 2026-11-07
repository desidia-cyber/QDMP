import sys, struct
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
from hunt import *

D = data()

def abs_ptr_hits(rva, width=8):
    pat = struct.pack("<Q", IMAGE_BASE + rva)
    out = []; off = 0
    while True:
        i = D.find(pat, off)
        if i < 0: break
        out.append((i, off_to_rva(i))); off = i + 1
    return out

for s in ["GetPlayerPawn", "GetWorldDeltaSeconds", "BeginDeferredActorSpawnFromClass",
          "FinishSpawningActor", "GetPlayerController", "MaxClientUpdateInterval"]:
    for rva, t in find_string(s, exact=True, limit=2):
        h = abs_ptr_hits(rva)
        print("%-32s %08X -> %d abs ptrs: %s" % (s, rva, len(h),
              [("%08X" % r) if r else "off%X" % o for o, r in h[:8]]))

print()
print("=== what is at 0x43F94FB0 (target of the pointer run at 03F95580) ===")
for a in range(0x43F94F80, 0x43F95060, 16):
    b = read_rva(a, 16)
    txt = "".join(chr(c) if 0x20 <= c < 0x7f else "." for c in b)
    print("  %08X  %-47s %s" % (a, " ".join("%02X" % c for c in b), txt))

print()
print("=== region before 0x03F90000 that contains CODE pointers (exec thunks?) ===")
# hunt for 8-byte values pointing into .text within .rdata 0x3F00000..0x4000000
tlo, thi = 0x1000, 0x373B000
cnt = 0
for off in range(0, len(D) - 8, 8):
    v = struct.unpack_from("<Q", D, off)[0] if False else int.from_bytes(D[off:off+8], "little")
    if IMAGE_BASE + tlo <= v < IMAGE_BASE + thi:
        r = off_to_rva(off)
        if r and 0x373B000 <= r < 0x4700000:
            print("   data %08X -> code %08X" % (r, v - IMAGE_BASE)); cnt += 1
            if cnt > 20: break
