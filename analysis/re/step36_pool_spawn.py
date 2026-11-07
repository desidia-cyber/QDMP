import sys
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
from hunt2 import *
from capstone.x86 import X86_OP_MEM, X86_REG_RIP

DL = sec_by_name(".data")[1]; DH = DL + sec_by_name(".data")[2]

print("=== FNamePool region bytes ===")
for a in range(0x04A27EE0, 0x04A27F40, 16):
    b = read_rva(a, 16)
    print("  %08X %s" % (a, " ".join("%02X" % c for c in b) if b else "<bss>"))
print()
# find extent of the zero region after Blocks base
a = 0x04A27F10
while a < DH:
    b = read_rva(a, 16)
    if b is None: 
        print("  first non-file (bss) at %08X" % a); break
    if any(b): 
        print("  first nonzero after Blocks base at %08X : %s" % (a, " ".join("%02X" % c for c in b))); break
    a += 16
print("  Blocks base 0x04A27F10 ; aligned?", (0x04A27F10 % 16) == 0)

def show(fn, maxn=120, start=None):
    f = func_at(fn); end = f[1] if f else fn + 0x100
    print("=== FUN_%08X size 0x%X ===" % (fn, end - fn))
    i = 0
    for ins, t in func_scan(fn, end):
        if start is not None and ins.address < start: continue
        if i >= maxn: print("  ..."); break
        i += 1
        note = ""
        if t is not None and DL <= t < DH: note = "   ; DATA %08X" % t
        if ins.mnemonic in ("call", "jmp") and ins.operands and ins.operands[0].type == 2:
            tg = ins.operands[0].imm & 0xFFFFFFFF
            if 0x1000 <= tg < 0x373B000: note = "   ; FUN_%08X" % tg
        print("  %08X %-9s %-46s%s" % (ins.address, ins.mnemonic, ins.op_str, note))

print()
show(0x02651580, 60, start=0x02651626)
