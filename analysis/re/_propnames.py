import qdlib as q
for s in ["DeltaTimeSeconds","UnpausedTimeSeconds","RealTimeSeconds","TimeSeconds","DeltaSeconds"]:
    h=q.find_string(s,exact=True,limit=3)
    print("%-22s %d %s"%(s,len(h),["%08X"%r for r,t in h]))
