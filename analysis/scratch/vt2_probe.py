import sys, struct
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.path.insert(0, r'G:\Freespace\Queens_Domain\analysis\re')
import qdlib as q

IMAGE = 0x140000000
PE = q.pe()
d = q.data()
s = q.sec_by_name('.rdata')
name, rva, vsz, raw, rawsz, ch = s
blob = d[raw:raw + min(vsz, rawsz)]
tlo = q.sec_by_name('.text')[1]
thi = tlo + q.sec_by_name('.text')[2]
PROC = IMAGE + 0x11A7AA0

def qw(i):
    return struct.unpack_from('<Q', blob, i)[0]

def incode(v):
    return IMAGE <= v < IMAGE + 0x40000000 and tlo <= v - IMAGE < thi

# all offsets where qword == ProcessEvent
hits = []
i = 0
n = len(blob)
while i + 8 <= n:
    if qw(i) == PROC:
        hits.append(i)
    i += 8
print('ProcessEvent qword occurrences in .rdata:', len(hits))

tables = []
for p in hits:
    start = p - 68 * 8
    if start < 0:
        continue
    # walk back to the leading edge of the pointer run
    k = 0
    while start - (k + 1) * 8 >= 0 and incode(qw(start - (k + 1) * 8)):
        k += 1
    real_start = start - k * 8
    # walk forward
    j = p
    cnt = 68
    while j + 8 <= n and incode(qw(j)):
        j += 8
        cnt += 1
    ntables = (j - real_start) // 8
    if ntables > 100:
        tables.append((rva + real_start, ntables, (p - real_start) // 8))

tables.sort()
print('candidate vtable runs (len>100):', len(tables))
seen = set()
for va, ln, slot68 in tables:
    if va in seen:
        continue
    seen.add(va)
    vals = [q.u64(va + k * 8) - IMAGE for k in (0, 1, 83, 84, 85) if k < ln]
    s83 = q.u64(va + 83 * 8)
    s83 = (s83 - IMAGE) if s83 else 0
    print('vtable %08X len %-4d slot68_at=%d slot0=%08X slot1=%08X slot83=%08X'
          % (va, ln, slot68, vals[0], vals[1] if len(vals) > 1 else 0, s83))

# For every candidate with slot83 in the AActor-ish cluster, disassemble slot83
print('\n== slot83 targets in 0x2600000-0x2700000 ==')
for va, ln, slot68 in tables:
    if ln <= 83:
        continue
    t = q.u64(va + 83 * 8)
    if not t:
        continue
    t -= IMAGE
    if 0x2600000 <= t < 0x2700000:
        print('vtable %08X  slot0=%08X  slot83=%08X' % (va, q.u64(va) - IMAGE, t))
