import sys, marshal
sys.path.insert(0,r"G:\Freespace\Queens_Domain\analysis\re")
M=marshal.load(open(r"G:\Freespace\Queens_Domain\analysis\re\nativepairs.bin","rb"))
p=M["pairs"]
for pat in ["SetSkeletalMesh","GetSkeletalMesh","SaveNamedData","SetNamed","NamedSaveData","SaveData","StoreNamed"]:
    hits=[k for k in p if pat.lower() in k.lower()]
    print(pat, hits[:15])
