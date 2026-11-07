import sys, marshal
sys.path.insert(0,r"G:\Freespace\Queens_Domain\analysis\re")
import qdlib as q
M=marshal.load(open(r"G:\Freespace\Queens_Domain\analysis\re\nativepairs.bin","rb"))
p=M["pairs"]
def impl_of(th):
    ins=q.disasm(th,count=100)
    cs=[i for i in ins if i.mnemonic=="call" and i.operands and i.operands[0].type==2]
    return (cs[-1].operands[0].imm&0xFFFFFFFF if cs else None)
for name in ["GetLevelManager","AddLevelListToStack","RemoveLevelListFromStack","LoadStreamLevel","SetShouldBeLoaded","SetShouldBeVisible"]:
    print("==",name)
    for b,c,nr in p.get(name,[]):
        im=impl_of(c)
        print("   thunk %08X impl %s"%(c,("%08X"%im) if im else "-"))
