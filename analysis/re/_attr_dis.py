import sys; sys.path.insert(0,r"G:\Freespace\Queens_Domain\analysis\re")
import qdlib as q
for rva,n,label in [(0x00CF8020,40,"GetAttributeValue"),(0x00CFB8A0,45,"SetAttributeBaseValue"),(0x00CF68C0,40,"ApplyAttributeModifier")]:
    print("=== %s %08X ==="%(label,rva))
    for ins in q.disasm(rva,count=n):
        note=""
        if ins.mnemonic in ("call","jmp") and ins.operands and ins.operands[0].type==2:
            t=ins.operands[0].imm&0xFFFFFFFF
            if 0x1000<=t<0x373B000: note=" -> %08X"%t
        print("  %08X %-9s %-44s%s"%(ins.address,ins.mnemonic,ins.op_str,note))
    print()
