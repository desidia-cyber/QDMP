import sys, collections
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
from hunt import *

DL = sec_by_name(".data")[1]; DH = DL + sec_by_name(".data")[2]

def init8(g):
    b = read_rva(g, 8)
    if b is None or len(b) < 8:
        return 0
    return int.from_bytes(b, "little")

rank_load = collections.defaultdict(set)
rank_lea = collections.defaultdict(set)
for tgt, sites in xrefs().items():
    if not (DL <= tgt < DH):
        continue
    for site, func, mn in sites:
        if mn.startswith("mov") or mn.startswith("cmp") or mn.startswith("test"):
            rank_load[tgt].add(func)
        elif mn.startswith("lea"):
            rank_lea[tgt].add(func)

SKIP = {0x495C5F8}
print("=== top .data globals by #distinct dereferencing functions ===")
i = 0
for g, fs in sorted(rank_load.items(), key=lambda kv: -len(kv[1])):
    if g in SKIP:
        continue
    v = init8(g)
    note = ""
    if IMAGE_BASE <= v < IMAGE_BASE + 0x6000000:
        note = "-> ptr %08X" % (v - IMAGE_BASE)
    elif v == 0:
        note = "ZEROINIT"
    print("  %08X  funcs=%-5d init=%016X %s" % (g, len(fs), v, note))
    i += 1
    if i >= 45:
        break

print()
print("=== top .data globals by #distinct lea (address-taken) ===")
i = 0
for g, fs in sorted(rank_lea.items(), key=lambda kv: -len(kv[1])):
    print("  %08X  funcs=%-5d init=%016X" % (g, len(fs), init8(g)))
    i += 1
    if i >= 20:
        break
