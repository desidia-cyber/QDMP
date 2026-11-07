import sys, marshal
sys.path.insert(0,r"G:\Freespace\Queens_Domain\analysis\re")
import qdlib as q
M=marshal.load(open(r"G:\Freespace\Queens_Domain\analysis\re\nativepairs.bin","rb"))
p=M["pairs"]
def impl_of(th):
    ins=q.disasm(th,count=80)
    cs=[i for i in ins if i.mnemonic=="call" and i.operands and i.operands[0].type==2]
    return cs[-1].operands[0].imm&0xFFFFFFFF if cs else None, ins
for name in ["SetSkeletalMesh"]:
    print(name, p.get(name))
    for b,c,nr in p.get(name,[]):
        im,ins=impl_of(c)
        print("  table %08X thunk %08X impl %s"%(b,c,("%08X"%im) if im else "-"))
        if im and im!=0x0119D280:
            for ins2 in q.disasm(im,count=30):
                print("     %08X %-9s %-44s"%(ins2.address,ins2.mnemonic,ins2.op_str))
