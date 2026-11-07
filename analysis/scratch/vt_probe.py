import sys, marshal, collections
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.path.insert(0, r'G:\Freespace\Queens_Domain\analysis\re')
import qdlib as q

VT = marshal.load(open(r'G:\Freespace\Queens_Domain\analysis\re\vtables.bin', 'rb'))
print('vtables.bin entries:', len(VT), 'first entry type:', type(VT[0]), VT[0][0], len(VT[0][1]))

TARGETS = {0x026599F0, 0x0265DC5E, 0x0265DF00, 0x0265A290, 0x0265A580,
           0x0265DB50, 0x02652560, 0x0265258D, 0x02664640, 0x0264D030,
           0x0264D99A, 0x02662560, 0x026698F0, 0x026526D1, 0x026527A0}

# 1) vtables whose slot 83 is one of our +0x5b touchers
print('\n== vtables with slot83 in target set ==')
for va, slots in VT:
    if len(slots) > 83 and slots[83] in TARGETS:
        print('vtable %08X len %d  slot83=%08X slot68=%08X slot0=%08X'
              % (va, len(slots), slots[83], slots[68] if len(slots) > 68 else 0, slots[0]))

# 2) vtables with ProcessEvent at slot 68 and >200 slots (candidate class vtables)
print('\n== big vtables with ProcessEvent at slot68, slot83 listing ==')
cnt = 0
for va, slots in VT:
    if len(slots) > 200 and len(slots) > 68 and slots[68] == 0x11A7AA0:
        cnt += 1
        print('vtable %08X len %d slot83=%08X slot0=%08X slot1=%08X'
              % (va, len(slots), slots[83] if len(slots) > 83 else 0, slots[0], slots[1]))
print('count', cnt)

# 3) where do the target functions appear at ANY slot
print('\n== target functions anywhere in vtables ==')
idx = collections.defaultdict(list)
for va, slots in VT:
    for k, s in enumerate(slots):
        if s in TARGETS:
            idx[s].append((va, k))
for t in sorted(TARGETS):
    print('%08X -> %s' % (t, ['%08X:slot%d' % (va, k) for va, k in idx[t][:8]]))
