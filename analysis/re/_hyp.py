import sys
sys.path.insert(0,r"G:\Freespace\Queens_Domain\analysis\re")
import qdlib as q
for rva,label in [(0x029A66B0,"hyp ProcessLevelStreamingVolumes"),(0x029D3A30,"hyp decision")]:
    f=q.func_at(rva)
    print("=== %s %08X  func=%s ==="%(label,rva,("%08X..%08X"%(f[0],f[1])) if f else "none"))
    for ins in q.disasm(rva,count=18):
        note=""
        if ins.mnemonic in ("call","jmp") and ins.operands and ins.operands[0].type==2:
            t=ins.operands[0].imm&0xFFFFFFFF
            if 0x1000<=t<0x373B000: note=" -> %08X"%t
        print("  %08X %-9s %-44s%s"%(ins.address,ins.mnemonic,ins.op_str,note))
    print()
