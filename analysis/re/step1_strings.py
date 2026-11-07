import sys, re
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
from qdlib import *

ANCHORS = ["GUObjectArray","ProcessEvent","StaticFindObject","FNamePool","FName",
           "GameNetDriver","FS_PlayerCharacter","AFS_PlayerController","UFS_SaveGame",
           "EnemyDiedEvent","NamedSaveData","Can't find function","Blueprint",
           "Function engine","Attempted to","GMalloc","GWorld","GEngine","ToString",
           "SpawnActor","FinishSpawning","GetPlayerPawn","GetWorldDeltaSeconds",
           "StaticConstructObject","StaticAllocateObject","BeginDeferredActorSpawn",
           "UGameplayStatics","UWorld","UEngine","UObject"]

print("=== anchor strings ===")
for a in ANCHORS:
    try:
        hits = find_string(a, limit=8)
    except Exception as e:
        hits = []
        print("  ERR", a, e)
    if hits:
        print("%-28s %d+ hits; first:" % (a, len(hits)))
        for rva, t in hits[:4]:
            print("       %08X  %r" % (rva, t[:90]))
    else:
        print("%-28s ABSENT" % a)

print()
print("=== RTTI type-descriptor scan (.?AV / .?AU) ===")
cnt = 0
samples = []
for rva, t in iter_strings(minlen=8):
    if t.startswith(".?AV") or t.startswith(".?AU"):
        cnt += 1
        if len(samples) < 20:
            samples.append((rva, t))
print("total RTTI name strings:", cnt)
for rva, t in samples:
    print("   %08X %r" % (rva, t))

print()
print("=== look for specific UE RTTI names ===")
for want in [".?AVUObject@@", ".?AVUClass@@", ".?AVUWorld@@", ".?AVUEngine@@",
             ".?AVUGameplayStatics@@", ".?AVAActor@@", ".?AVUFunction@@",
             ".?AVFNameEntry@@", ".?AVULevel@@"]:
    hits = [x for x in find_string(want, exact=True, limit=3)]
    print("%-28s %s" % (want, hits if hits else "ABSENT"))
