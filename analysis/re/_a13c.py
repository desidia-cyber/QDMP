import sys; sys.path.insert(0,r"G:\Freespace\Queens_Domain\analysis\re")
from hunt import *
import qdlib as q
for nm,rva in [("BeginDeferredActorSpawnFromClass",0x03F94D98),("FinishSpawningActor",0x03F95050),("ReceiveTick",0x03F0D470),("SpawnActor",0x040E6210),("GetPlayerPawn",0x03F955F0),("GetPlayerCharacter",0x03F954A8),("GetPlayerController",0x03F954D8),("GetPlayerCameraManager",0x03F95)];
    print(nm)
    try:
        rs=refs_to(rva)
        print("  refs:",len(rs))
        for s in rs[:12]: print("    %08X in FUN_%08X %s"%(s[0],s[1],s[2]))
    except Exception as e: print("  err",e)
print()
print("xref index entries:",len(xrefs()))
print("sample keys:",list(xrefs().keys())[:5])
