import sys, marshal, collections
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.path.insert(0, r'G:\Freespace\Queens_Domain\analysis\re')
import qdlib as q

CALLS = marshal.load(open(r'G:\Freespace\Queens_Domain\analysis\re\calls.bin', 'rb'))
# invert: function -> sorted set of callees
callees = collections.defaultdict(set)
for tgt, lst in CALLS.items():
    for site, fn, mn in lst:
        if tgt != fn:
            callees[fn].add(tgt)

NODES = {
    0x028D3C90: 'UGameplayStatics::BeginDeferredActorSpawnFromClass',
    0x028DCD50: 'UGameplayStatics::FinishSpawningActor',
    0x02651580: 'AActor::FinishSpawning',
    0x029AD950: 'UWorld::SpawnActor (entry)',
    0x011AF050: 'StaticConstructObject_Internal',
    0x028D3F90: 'helper 0x28D3F90',
    0x029AE4B0: 'UWorld spawn helper A',
    0x03100640: 'spawn helper B',
}
for f, label in NODES.items():
    cs = sorted(callees.get(f, ()))
    print('\n%s  FUN_%08X  callees=%d' % (label, f, len(cs)))
    for t in cs[:40]:
        lbl = NODES.get(t, '')
        print('    -> %08X %s' % (t, lbl))
