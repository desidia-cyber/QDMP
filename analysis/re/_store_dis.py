import sys; sys.path.insert(0,r"G:\Freespace\Queens_Domain\analysis\re")
import qdlib as q
for ins in q.disasm(0x00E3BF10, count=45):
    t=q.insn_target(ins); rt=q.rel_target(ins)
    note=""
    if rt and 0x1000<=rt<0x373B000: note=" ; -> %08X"%rt
    if t and 0x46F1000<=t<0x4C08000: note=" ; DATA %08X"%t
    print("  %08X %-9s %-44s%s"%(ins.address,ins.mnemonic,ins.op_str,note))
