import sys
sys.path.insert(0,r"G:\Freespace\Queens_Domain\analysis\re")
import qdlib as q
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
md=Cs(CS_ARCH_X86,CS_MODE_64); md.detail=True
o=q.rva_to_off(0x00E378CD); data=q.data()[o:o+0x300]
for i,ins in enumerate(md.disasm(data,0x00E378CD)):
    if i>=95: break
    note=""
    if ins.mnemonic in ("call","jmp") and ins.operands and ins.operands[0].type==2:
        t=ins.operands[0].imm&0xFFFFFFFF
        if 0x1000<=t<0x373B000: note=" -> %08X"%t
    print("  %08X %-9s %-44s%s"%(ins.address,ins.mnemonic,ins.op_str,note))
