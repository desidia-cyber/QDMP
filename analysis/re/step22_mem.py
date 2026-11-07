import sys
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
from hunt2 import *
from capstone.x86 import X86_OP_MEM, X86_REG_RIP

print("=== strings: memory anchors ===")
for s in ["FMemory::", "GMalloc", "Malloc", "Memory allocation", "allocator",
          "BaseAllocator", "GCreateMalloc", "InitializeMemory", "Out of memory",
          "OOM", "FMemory"]:
    hits = find_string(s, limit=6)
    print("%-22s %s" % (s, ["%08X:%r" % (r, t[:44]) for r, t in hits[:5]]))

print()
print("=== 0x04BDCB58 context ===")
for site, func, mn in refs_to(0x04BDCB58):
    print("  site %08X FUN_%08X %s" % (site, func, mn))
    f = func_at(func)
    for ins, t in func_scan(func, f[1] if f else func + 0x100):
        note = ""
        if t == 0x04BDCB58: note = "  <<<"
        if ins.mnemonic == "call" and ins.operands and ins.operands[0].type == 2:
            tg = ins.operands[0].imm & 0xFFFFFFFF
            if 0x1000 <= tg < 0x373B000: note = "  call FUN_%08X" % tg
        print("     %08X %-9s %-38s%s" % (ins.address, ins.mnemonic, ins.op_str, note))
