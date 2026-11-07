import sys, collections
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
from qdlib import *

d = data()
tlo = sec_by_name(".text")[1]; thi = tlo + sec_by_name(".text")[2]
fstarts = set(f[0] for f in functions())
name, va, vsz, raw, rawsz, ch = sec_by_name(".rdata")
blob = d[raw:raw + min(vsz, rawsz)]
n = len(blob)

def cls(i):
    v = int.from_bytes(blob[i:i+8], "little")
    r = v - IMAGE_BASE
    if v == 0: return "zero"
    if v < IMAGE_BASE: return "low"
    if tlo <= r < thi:
        return "funcstart" if r in fstarts else "text-notstart"
    for nm, sva, svz, sraw, srsz, _c in sections():
        if sva <= r < sva + max(svz, srsz):
            return "ptr->" + nm
    return "other"

# find long runs without the funcstart requirement on the tail, report what breaks
runs = []
i = 0
while i + 8 <= n:
    if cls(i) == "funcstart":
        j = i; k = 0
        while j + 8 <= n and cls(j) == "funcstart":
            k += 1; j += 8
        if k >= 20:
            runs.append((va + i, k, cls(j) if j + 8 <= n else "eof"))
        i = j if k >= 4 else i + 8
    else:
        i += 8

print("runs with >=20 slots:", len(runs))
c = collections.Counter(r[2] for r in runs)
print("terminator kinds:", c.most_common(10))
for r in runs[:10]:
    print("  %08X slots=%d term=%s" % r)
print()
print("longest runs:")
for r in sorted(runs, key=lambda x: -x[1])[:10]:
    print("  %08X slots=%d term=%s" % r)
