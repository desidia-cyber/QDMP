import sys
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
from hunt2 import *

DL = sec_by_name(".data")[1]; DH = DL + sec_by_name(".data")[2]
def init8(g):
    b = read_rva(g, 8)
    return int.from_bytes(b, "little") if b and len(b) == 8 else 0

print("### head of FUN_02D391C0 (claimed UWorld::InitializeActorsForPlay)")
for i, (ins, t) in enumerate(func_scan(0x02D391C0)):
    if i > 16: break
    extra = ""
    if t is not None:
        s = read_rva(t, 60)
        if s:
            k = s.find(b"\x00"); s2 = s[:k] if k >= 0 else s
            if s2 and all(0x20 <= c < 0x7f for c in s2):
                extra = "  ; %r" % s2.decode("latin-1")[:50]
    print("   %08X %-9s %-40s%s" % (ins.address, ins.mnemonic, ins.op_str, extra))

print()
print("### callers of FUN_02D391C0")
c = callers(0x02D391C0)
print("count:", len(c))
for site, func, mn in c[:20]:
    print("   %08X in FUN_%08X (%s)" % (site, func, mn))

print()
print("### writers of GUObjectArray fields (sanity) and GMalloc candidate")
for g in [0x049CBE20, 0x04C06368, 0x049DFB18, 0x049DFB08]:
    w = writers_of(g)
    print("  %08X init=%016X writers=%d" % (g, init8(g), len(w)))
    for site, func, txt in w[:6]:
        print("        %08X in FUN_%08X   %s" % (site, func, txt))
