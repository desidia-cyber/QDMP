import sys, marshal
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
from hunt2 import *

M = marshal.load(open(r"G:\Freespace\Queens_Domain\analysis\re\nativepairs.bin", "rb"))
pairs = M["pairs"]; runs = M["runs"]
runbase = set(b for b, _ in runs)

def lookup(name):
    return pairs.get(name, [])

print("tables:", sorted("%08X" % b for b, _ in runs)[:40])
print("total names:", len(pairs))
print()
for probe in ["Conv_NameToString", "Conv_StringToName", "ConstructObjectFromClass",
              "SpawnActorFromClass", "FinishSpawningActor", "BeginDeferredActorSpawnFromClass",
              "GetPlayerPawn", "GetWorldDeltaSeconds", "GetPlayerController",
              "GetAllActorsOfClass", "GetGameMode", "GetGameInstance",
              "GetWorldOriginLocation", "MakeLiteralName", "GetDisplayName",
              "IsValid", "GetObjectName", "LoadObjectFromClass", "GetClass",
              "GetActorNameOrLabel", "GetPlayerCharacter", "SetActorLocation"]:
    v = lookup(probe)
    print("%-34s %s" % (probe, ["%08X@%08X" % (c, b) for b, c, _ in v][:6]))
