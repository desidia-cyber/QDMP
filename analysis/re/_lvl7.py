import sys
sys.path.insert(0,r"G:\Freespace\Queens_Domain\analysis\re")
import qdlib as q
def show_range(start, lo, n=140, label=""):
    print("=== %s %08X (from %08X) ==="%(label,start,lo))
    shown=0
    for ins in q.disasm(start, count=500):
        if ins.address < lo: continue
        note=""
        if ins.mnemonic in ("call","jmp") and ins.operands and ins.operands[0].type==2:
            t=ins.operands[0].imm&0xFFFFFFFF
            if 0x1000<=t<0x373B000: note=" -> %08X"%t
        print("  %08X %-9s %-44s%s"%(ins.address,ins.mnemonic,ins.op_str,note))
        shown+=1
        if shown>=n: break
show_range(0x00E37800,0x00E378CD,90,"AddLevelListToStack real")
show_range(0x00E2C270,0x00E2C270,60,"FlushLevelManagerActions")
