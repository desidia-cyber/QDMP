import sys
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
from hunt import *

TARGETS = ["GetPlayerPawn", "GetWorldDeltaSeconds", "BeginDeferredActorSpawnFromClass",
           "FinishSpawningActor", "GetPlayerController", "GetGameInstance",
           "GetPlayerCharacter"]

for s in TARGETS:
    hits = find_string(s, exact=True, limit=5)
    print("### %-32s %s" % (s, ["%08X" % h[0] for h in hits]))
    for rva, txt in hits:
        rs = refs_to(rva)
        print("   string %08X: %d xrefs" % (rva, len(rs)))
        for site, func, mn in rs:
            print("      site %08X in FUN_%08X (%s)" % (site, func, mn))
    print()
