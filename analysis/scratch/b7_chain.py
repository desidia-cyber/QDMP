import sys, re, marshal, collections, json
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.path.insert(0, r'G:\Freespace\Queens_Domain\analysis\re')
import qdlib as q
import hunt2

NP = marshal.load(open(r'G:\Freespace\Queens_Domain\analysis\re\nativepairs.bin', 'rb'))
P = NP['pairs']
T2C = json.load(open(r'G:\Freespace\Queens_Domain\analysis\scratch\table2class.json'))
T2C = {int(k): v for k, v in T2C.items()}

PAT = re.compile(r'Spawn|Construct|Create|Possess|Merge', re.I)
print('=== game-module UFUNCTION names matching Spawn/Construct/Create/Possess/Merge ===')
found = 0
for nm, lst in sorted(P.items()):
    if not PAT.search(nm):
        continue
    for b, c, nr in lst:
        if b in T2C:
            print('  %-40s class=%-32s exec=%08X' % (nm, T2C[b][0], c))
            found += 1
print('total:', found)

DISPATCH = 0x0119D280

def impl_of(exec_rva):
    if exec_rva == DISPATCH:
        return None
    calls = []
    for i in q.disasm(exec_rva, count=80):
        if i.mnemonic == 'call' and i.operands and i.operands[0].type == 2:
            t = i.operands[0].imm & 0xFFFFFFFF
            if 0x1000 <= t < 0x373B000:
                calls.append(t)
        if i.mnemonic == 'ret':
            break
    return calls[-1] if calls else None

print('\n=== engine spawn/possess chain: exec -> impl -> callers ===')
ENG = ['RestartPlayer', 'RestartPlayerAtPlayerStart', 'RestartPlayerAtTransform',
       'SpawnDefaultPawnFor', 'SpawnDefaultPawnAtTransform', 'SpawnActor',
       'BeginDeferredActorSpawnFromClass', 'FinishSpawningActor', 'Possess',
       'ReceivePossess', 'UnPossess', 'InitPlayerState', 'GiveDefaultInventory',
       'SpawnDefaultController', 'BeginSpawningActorFromClass', 'BeginSpawningActorFromBlueprint',
       'ConstructObject', 'NewObject', 'CreateDefaultSubobject']
for nm in ENG:
    lst = P.get(nm)
    if not lst:
        print('  %-34s (not in pair table)' % nm)
        continue
    for b, c, nr in lst:
        im = impl_of(c)
        callers = hunt2.callers(im) if im else []
        print('  %-34s exec=%08X impl=%s callers=%d %s'
              % (nm, c, ('%08X' % im) if im else '-', len(callers),
                 ['FUN_%08X' % f for s, f, m in callers[:5]]))
