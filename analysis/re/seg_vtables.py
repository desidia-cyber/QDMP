import sys, os, marshal, collections
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
from hunt import *

IMAGE = IMAGE_BASE
d = data()
tlo = sec_by_name(".text")[1]; thi = tlo + sec_by_name(".text")[2]
name, va, vsz, raw, rawsz, ch = sec_by_name(".rdata")
blob = d[raw:raw + min(vsz, rawsz)]
n = len(blob)

lea_targets = set()
for tgt, sites in xrefs().items():
    if va <= tgt < va + len(blob):
        for site, func, mn in sites:
            if mn.startswith("lea") or mn.startswith("mov"):
                lea_targets.add(tgt)
                break

def is_textptr(i):
    if i + 8 > n: return False
    v = int.from_bytes(blob[i:i+8], "little")
    r = v - IMAGE
    return IMAGE <= v and tlo <= r < thi

# maximal runs
runs = []
i = 0
while i + 8 <= n:
    if is_textptr(i) and not is_textptr(i-8 if i >= 8 else -8):
        j = i
        while j + 8 <= n and is_textptr(j):
            j += 8
        if (j - i)//8 >= 4:
            runs.append((va + i, (j - i)//8))
        i = j
    else:
        i += 8

print("maximal pointer runs into .text (>=4):", len(runs))

# split runs at lea targets
vtables = []
for start, cnt in runs:
    cuts = [start]
    for k in range(1, cnt):
        a = start + k*8
        if a in lea_targets:
            cuts.append(a)
    cuts.append(start + cnt*8)
    for a, b in zip(cuts, cuts[1:]):
        if (b - a)//8 >= 4:
            vtables.append((a, (b - a)//8))

print("segmented vtables:", len(vtables))
lens = collections.Counter(l for _, l in vtables)
print("length hist (top):", lens.most_common(15))
print("vtables with >60 slots:", sum(1 for _, l in vtables if l > 60))

with open(r"G:\Freespace\Queens_Domain\analysis\re\vtables2.bin", "wb") as f:
    marshal.dump(vtables, f)

# slot histogram
print()
print("=== slot histogram (slot -> top function, count) ===")
slots = collections.defaultdict(collections.Counter)
for a, l in vtables:
    for k in range(l):
        v = u64(a + k*8)
        if v:
            slots[k][v - IMAGE] += 1
for k in range(0, 90):
    if k in slots and slots[k]:
        top, cnt = slots[k].most_common(1)[0]
        tot = sum(slots[k].values())
        print("  slot %2d: %08X x%-5d (of %d)  2nd=%s" % (
            k, top, cnt, tot,
            ("%08X x%d" % slots[k].most_common(2)[1]) if len(slots[k]) > 1 else "-"))
