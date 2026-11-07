import sys; sys.path.insert(0,r"G:\Freespace\Queens_Domain\analysis\re")
import qdlib as q
def show(rva,n=30,label=""):
    print("=== %s %08X ==="%(label,rva))
    for ins in q.disasm(rva,count=n):
        note=""
        if ins.mnemonic in ("call","jmp") and ins.operands and ins.operands[0].type==2:
            t=ins.operands[0].imm&0xFFFFFFFF
            if 0x1000<=t<0x373B000: note=" -> %08X"%t
        print("  %08X %-9s %-44s%s"%(ins.address,ins.mnemonic,ins.op_str,note))
show(0x02D575A0,25,"exec K2_GetActorLocation (native copy)")
show(0x02D577E0,30,"exec K2_SetActorLocation (native copy)")
