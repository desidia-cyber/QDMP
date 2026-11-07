import sys, marshal, os
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
import qdlib as q

X = marshal.load(open(os.path.join(os.path.dirname(__file__), "xrefs_all.bin"), "rb"))

names = {
    "BeginDeferredActorSpawnFromClass": 0x03F94D98,
    "FinishSpawningActor": 0x03F95050,
    "ReceiveTick": 0x03F0D470,
    "SpawnActor": 0x040E6210,
    "GetPlayerPawn": 0x03F955F0,
    "GetPlayerCharacter": 0x03F954A8,
    "GetPlayerController": 0x03F954D8,
}
for nm, rva in names.items():
    hits = X.get(rva, [])
    print("%-34s %08X  xrefs=%d" % (nm, rva, len(hits)))
    for site, mn in hits[:20]:
        f = q.func_at(site)
        print("    site %08X %-6s in FUN_%08X" % (site, mn, f[0] if f else 0))
