"""C10 step 3: AddLevelListToStack / Flush / Update ordering."""
import sys, struct
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
import qdlib as q
from hunt2 import callers

TAU = 0x373B000


def note(ins):
    out = ""
    if ins.mnemonic in ("call", "jmp") and ins.operands and ins.operands[0].type == 2:
        t = ins.operands[0].imm & 0xFFFFFFFF
        if 0x1000 <= t < TAU:
            out = " -> %08X" % t
    return out


def show(rva, n=200, label="", exact_start=True):
    f = q.func_at(rva)
    start = f[0] if (f and exact_start) else rva
    print("\n=== %s @%08X func=%s ===" % (label, rva,
          ("%08X..%08X (%d B)" % (f[0], f[1], f[1] - f[0])) if f else "none"))
    shown = 0
    for ins in q.disasm(start, count=n * 2):
        if ins.address < start:
            continue
        print("  %08X %-9s %-46s%s" % (ins.address, ins.mnemonic, ins.op_str, note(ins)))
        shown += 1
        if shown >= n:
            break


show(0x00E215E0, 80, "AddLevelListToStack impl (UFUNCTION)")
show(0x00E37800, 80, "AddLevelListToStack worker 0xE37800")
show(0x00E378CD, 110, "0xE378CD")
show(0x00E2C2BE, 40, "flush tail worker")
show(0x00E42950, 90, "0xE42950 (called by update)")
show(0x00E265F0, 50, "0xE265F0 (done-loading check)")
show(0x029ACB20, 50, "SetShouldBeVisible")

print("\n=== callers of 0xE3CF9E (manager update) ===")
for s, f, mn in callers(0x00E3CF9E):
    print("  site %08X in %08X (%s)" % (s, f, mn))
print("\n=== callers of 0xE215E0 ===")
for s, f, mn in callers(0x00E215E0):
    print("  site %08X in %08X (%s)" % (s, f, mn))
print("\n=== callers of 0xE37800 ===")
for s, f, mn in callers(0x00E37800):
    print("  site %08X in %08X (%s)" % (s, f, mn))
print("\n=== callers of 0xE42950 ===")
for s, f, mn in callers(0x00E42950):
    print("  site %08X in %08X (%s)" % (s, f, mn))
