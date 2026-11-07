import sys, marshal
sys.path.insert(0,r"G:\Freespace\Queens_Domain\analysis\re")
M=marshal.load(open(r"G:\Freespace\Queens_Domain\analysis\re\nativepairs.bin","rb"))
p=M["pairs"]
for pat in ["LevelList","LevelManager","AddLevel","RemoveLevel","LoadLevel","StreamLevel","SetShouldBe","LevelStreaming"]:
    hits=sorted(k for k in p if pat.lower() in k.lower())
    print(pat, hits[:25])
