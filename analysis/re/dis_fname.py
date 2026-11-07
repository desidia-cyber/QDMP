import struct, sys
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64, CS_OP_MEM, CS_OP_IMM

EXE = r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64\QueensDomain-Win64-Shipping.exe"
IMAGE_BASE = 0x140000000

pe = pefile.PE(EXE, fast_load=True)
sections = []
for s in pe.sections:
    name = s.Name.rstrip(b"\x00").decode("ascii", "replace")
    sections.append((name, s.VirtualAddress, s.Misc_VirtualSize, s.PointerToRawData, s.SizeOfRawData))
data = open(EXE, "rb").read()

def r2o(rva):
    for name, va, vs, ra, rs in sections:
        if va <= rva < va + max(vs, rs):
            return ra + (rva - va)
    return None

def sec_of(rva):
    for name, va, vs, ra, rs in sections:
        if va <= rva < va + max(vs, rs):
            return name, va, vs
    return None, 0, 0

print("=== sections ===")
for s in sections:
    print(f"  {s[0]:<10} va=0x{s[1]:08X} vsize=0x{s[2]:08X}")

TARGET = 0xFB8DE0        # FName::ToString, from UE4SS's pattern scan
print(f"\n=== disassembling RVA 0x{TARGET:X} (FName::ToString) ===")
off = r2o(TARGET)
code = data[off:off + 400]

md = Cs(CS_ARCH_X86, CS_MODE_64)
md.detail = True

globals_found = []
for ins in md.disasm(code, IMAGE_BASE + TARGET):
    note = ""
    if ins.mnemonic in ("ret", "int3") and ins.address != IMAGE_BASE + TARGET:
        print(f"  0x{ins.address - IMAGE_BASE:06X}: {ins.mnemonic}")
        if ins.mnemonic == "ret":
            break
    try:
        for op in ins.operands:
            # rip-relative memory operand -> absolute target
            if op.type == CS_OP_MEM and op.mem.base == 41:  # X86_REG_RIP
                target_rva = ins.address + ins.size + op.mem.disp - IMAGE_BASE
                sec, sva, svsz = sec_of(target_rva)
                if sec in (".data", "_RDATA", ".rdata") and not (sva <= target_rva < sva + svsz and sec == ".rdata" and target_rva < 0x100000):
                    note += f"  -> [0x{target_rva:X}] ({sec})"
                    globals_found.append((ins.address - IMAGE_BASE, target_rva, sec))
    except Exception:
        pass
    print(f"  0x{ins.address - IMAGE_BASE:06X}: {ins.mnemonic:<8} {ins.op_str}{note}")

print("\n=== globals referenced (candidates for FNamePool) ===")
seen = set()
for here, tgt, sec in globals_found:
    if tgt in seen:
        continue
    seen.add(tgt)
    o = r2o(tgt)
    val = data[o:o + 8].hex() if o is not None else "??"
    print(f"  from 0x{here:06X} -> .data/0x{tgt:X} ({sec})  first8={val}")
