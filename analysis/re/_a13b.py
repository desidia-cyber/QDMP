import sys; sys.path.insert(0,r"G:\Freespace\Queens_Domain\analysis\re")
import qdlib as q
for s in ["BeginDeferredActorSpawnFromClass","FinishSpawningActor","ReceiveTick","SpawnActor","GetPlayerPawn","GetPlayerCharacter","GetPlayerController","PlayerTick","TickActor"]:
    hits=q.find_string(s,exact=True,limit=8)
    print("%-38s %d  %s"%(s,len(hits),["%08X"%r for r,t in hits]))
print()
print("--- 0x2B60C30 ---")
for ins in q.disasm(0x2B60C30,count=25):
    print("  %08X %-9s %-44s"%(ins.address,ins.mnemonic,ins.op_str))
print("--- 0x028E04B0 tail ---")
for ins in q.disasm(0x028E04B0,count=60):
    if ins.address<0x028E0504: continue
    print("  %08X %-9s %-44s"%(ins.address,ins.mnemonic,ins.op_str))
