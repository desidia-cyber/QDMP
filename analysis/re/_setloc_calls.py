import sys; sys.path.insert(0,r"G:\Freespace\Queens_Domain\analysis\re")
import qdlib as q
f=q.func_at(0x02D577E0); print("func", f)
end=f[1] if f else 0x02D577E0+0x400
print("size", end-0x02D577E0)
for ins in q.disasm(0x02D577E0, end_rva=end):
    if ins.mnemonic in ("call","jmp") and ins.operands and ins.operands[0].type==2:
        t=ins.operands[0].imm&0xFFFFFFFF
        if 0x1000<=t<0x373B000:
            print("  %08X -> %08X  %s"%(ins.address,t,ins.op_str))
