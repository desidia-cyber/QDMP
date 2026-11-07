import sys
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64, CS_OP_MEM

EXE = r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64\QueensDomain-Win64-Shipping.exe"
IMAGE_BASE = 0x140000000
pe = pefile.PE(EXE, fast_load=True)
secs = [(s.Name.rstrip(b"\0").decode(), s.VirtualAddress, s.Misc_VirtualSize, s.PointerToRawData, s.SizeOfRawData) for s in pe.sections]
data = open(EXE, "rb").read()

def r2o(rva):
    for n, va, vs, ra, rs in secs:
        if va <= rva < va + max(vs, rs):
            return ra + (rva - va)
    return None

def dis(rva, n=60, label=""):
    off = r2o(rva)
    code = data[off:off + n * 20]
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True
    print(f"\n=== {label} RVA 0x{rva:X} ===")
    for k, ins in enumerate(md.disasm(code, IMAGE_BASE + rva)):
        note = ""
        try:
            for op in ins.operands:
                if op.type == CS_OP_MEM and op.mem.base == 41:
                    t = ins.address + ins.size + op.mem.disp - IMAGE_BASE
                    sec = next((s[0] for s in secs if s[1] <= t < s[1] + max(s[2], s[4])), "?")
                    note += f"   -> 0x{t:X}({sec})"
        except Exception:
            pass
        print(f"  0x{ins.address - IMAGE_BASE:06X}: {ins.mnemonic:<8} {ins.op_str}{note}")
        if ins.mnemonic == "ret" and k > 3:
            break

dis(0xFB25B0, 70, "helper called by FName::ToString")
dis(0xFAC1C0, 40, "pool-init helper")
