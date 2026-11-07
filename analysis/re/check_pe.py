import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64, CS_OP_MEM
EXE = r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64\QueensDomain-Win64-Shipping.exe"
IB = 0x140000000
pe = pefile.PE(EXE, fast_load=True)
secs=[(s.Name.rstrip(b"\0").decode(), s.VirtualAddress, s.Misc_VirtualSize, s.PointerToRawData, s.SizeOfRawData) for s in pe.sections]
data=open(EXE,"rb").read()
def r2o(rva):
    for n,va,vs,ra,rs in secs:
        if va<=rva<va+max(vs,rs): return ra+(rva-va)
    return None
def dis(rva,n,label):
    off=r2o(rva); md=Cs(CS_ARCH_X86,CS_MODE_64); md.detail=True
    print(f"\n=== {label} RVA 0x{rva:X} ===")
    for k,ins in enumerate(md.disasm(data[off:off+n*16], IB+rva)):
        print(f"  0x{ins.address-IB:06X}: {ins.mnemonic:<8} {ins.op_str}")
        if ins.mnemonic=='ret' and k>4: break
        if k>40: break
dis(0x11A7AA0, 45, "vtable slot 68")
