import sys, marshal
sys.path.insert(0,r"G:\Freespace\Queens_Domain\analysis\re")
M=marshal.load(open(r"G:\Freespace\Queens_Domain\analysis\re\nativepairs.bin","rb"))
pairs=M["pairs"]
for n in ["K2_GetActorLocation","GetActorLocation","K2_SetActorLocation","SetActorLocation","GetActorForwardVector","GetDistanceTo","ApplyDamage","ApplyPointDamage","GetWorld"]:
    v=pairs.get(n,[])
    print("%-26s %s"%(n, ["exec %08X impl? table %08X"%(c,b) for b,c,_ in v][:4]))
