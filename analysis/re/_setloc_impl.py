import sys; sys.path.insert(0,r"G:\Freespace\Queens_Domain\analysis\re")
import qdlib as q
print("=== 0x0265B400 ===")
for ins in q.disasm(0x0265B400, count=45):
    note=""
    if ins.mnemonic in ("call","jmp") and ins.operands and ins.operands[0].type==2:
        t=ins.operands[0].imm&0xFFFFFFFF
        if 0x1000<=t<0x373B000: note=" -> %08X"%t
    print("  %08X %-9s %-44s%s"%(ins.address,ins.mnemonic,ins.op_str,note))
