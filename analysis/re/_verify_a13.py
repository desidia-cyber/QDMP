import sys; sys.path.insert(0,r"G:\Freespace\Queens_Domain\analysis\re")
import qdlib as q
def show(rva,n=40,label=""):
    print("=== %s  FUN_%08X ==="%(label,rva))
    for i,ins in enumerate(q.disasm(rva,count=n)):
        note=""
        t=q.insn_target(ins); rt=q.rel_target(ins)
        if t is not None and 0x4500000<=t<0x4C08000: note=" ; DATA %08X"%t
        if rt is not None and 0x1000<=rt<0x373B000: note=" ; FUN_%08X"%rt
        print("  %08X %-9s %-46s%s"%(ins.address,ins.mnemonic,ins.op_str,note))
show(0x028E02C0,30,"GetPlayerCameraManager?")
show(0x028E0340,30,"GetPlayerController? (cand A)")
show(0x028E04B0,30,"GetPlayerController? (cand B)")
show(0x028E0650,30,"GetPlayerPawn? (delta +0x310)")
