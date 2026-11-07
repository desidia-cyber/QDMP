"""Detect MSVC vtables in .rdata: maximal runs of 8-byte pointers into .text
where every pointer lands exactly on a .pdata function start.

Then histogram 'which function occupies slot N' across all detected vtables.
UObject::ProcessEvent should dominate slot 68 (rarely/never overridden).
"""
import sys, marshal, os, collections
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
from qdlib import *

OUT = r"G:\Freespace\Queens_Domain\analysis\re\vtables.bin"
MIN_SLOTS = 8


def text_range():
    s = sec_by_name(".text")
    return s[1], s[1] + s[2]


def detect():
    d = data()
    tlo, thi = text_range()
    fstarts = set(f[0] for f in functions())
    s = sec_by_name(".rdata")
    name, va, vsz, raw, rawsz, ch = s
    blob = d[raw:raw + min(vsz, rawsz)]
    n = len(blob)

    def ptr_at(i):
        v = int.from_bytes(blob[i:i + 8], "little")
        if v < IMAGE_BASE:
            return None
        r = v - IMAGE_BASE
        return r

    vt = []
    i = 0
    while i + 8 <= n:
        r = ptr_at(i)
        if r is not None and tlo <= r < thi and r in fstarts:
            # attempt a run
            slots = []
            j = i
            while j + 8 <= n:
                r2 = ptr_at(j)
                if r2 is None or not (tlo <= r2 < thi) or r2 not in fstarts:
                    break
                slots.append(r2)
                j += 8
            if len(slots) >= MIN_SLOTS:
                vt.append((va + i, slots))
                i = j
                continue
        i += 8
    return vt


if __name__ == "__main__":
    vt = detect()
    print("candidate vtables:", len(vt))
    lens = collections.Counter(len(s) for s in (v[1] for v in vt))
    print("most common slot counts:", lens.most_common(12))
    with open(OUT, "wb") as f:
        marshal.dump(vt, f)
    print("wrote", OUT, os.path.getsize(OUT))
