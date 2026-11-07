import sys, marshal
sys.path.insert(0,r"G:\Freespace\Queens_Domain\analysis\re")
import qdlib as q
M=marshal.load(open(r"G:\Freespace\Queens_Domain\analysis\re\nativepairs.bin","rb"))
p=M["pairs"]
def impl_of(th):
    ins=q.disasm(th,count=100)
    cs=[i for i in ins if i.mnemonic=="call" and i.operands and i.operands[0].type==2]
    return (cs[-1].operands[0].imm&0xFFFFFFFF if cs else None)
for name in ["FlushLevelManagerActions","SetFreezeLevelListUpdates","ClearTriggerLevelLists"]:
    print("==",name)
    for b,c,nr in p.get(name,[]):
        print("   thunk %08X impl %s"%(c,("%08X"%impl_of(c)) if impl_of(c) else "-"))
print()
print("=== AddLevelListToStack real 0xE378CD ===")
for ins in q.disasm(0x00E378CD,count=110):
    note=""
    if ins.mnemonic in ("call","jmp") and ins.operands and ins.operands[0].type==2:
        t=ins.operands[0].imm&0xFFFFFFFF
        if 0x1000<=t<0x373B000: note=" -> %08X"%t
    print("  %08X %-9s %-44s%s"%(ins.address,ins.mnemonic,ins.op_str,note))
