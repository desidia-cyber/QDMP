import sys, marshal
sys.path.insert(0,r"G:\Freespace\Queens_Domain\analysis\re")
import qdlib as q
M=marshal.load(open(r"G:\Freespace\Queens_Domain\analysis\re\nativepairs.bin","rb"))
p=M["pairs"]
def impl_of(th):
    ins=q.disasm(th,count=120)
    cs=[i for i in ins if i.mnemonic=="call" and i.operands and i.operands[0].type==2]
    return (cs[-1].operands[0].imm&0xFFFFFFFF if cs else None)
for name in ["GetLevelManager","AddLevelListToStack","RemoveLevelListFromStack","LoadStreamLevel","SetShouldBeLoaded","SetShouldBeVisible"]:
    print("\n==",name)
    for b,c,nr in p.get(name,[]):
        im=impl_of(c)
        print("  table %08X thunk %08X impl %s"%(b,c,("%08X"%im) if im else "-"))
        if im and im!=0x0119D280:
            for ins2 in q.disasm(im,count=14):
                note=""
                if ins2.mnemonic in ("call","jmp") and ins2.operands and ins2.operands[0].type==2:
                    t=ins2.operands[0].imm&0xFFFFFFFF
                    if 0x1000<=t<0x373B000: note=" -> %08X"%t
                print("       %08X %-9s %-40s%s"%(ins2.address,ins2.mnemonic,ins2.op_str,note))
