"""C10 step 2: the functions that build/apply the manager's resolved level set."""
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


def show(rva, n=160, label="", start=None):
    f = q.func_at(rva)
    print("\n=== %s @%08X func=%s ===" % (label, rva,
          ("%08X..%08X (%d B)" % (f[0], f[1], f[1] - f[0])) if f else "none"))
    base = start if start is not None else rva
    shown = 0
    for ins in q.disasm(base, count=n * 2):
        if ins.address < base:
            continue
        print("  %08X %-9s %-46s%s" % (ins.address, ins.mnemonic, ins.op_str, note(ins)))
        shown += 1
        if shown >= n:
            break


def funcs_in(lo, hi):
    out = []
    for b, e, u in q.functions():
        if lo <= b < hi:
            out.append((b, e))
    return out


print("=== functions 0xE2C000..0xE2D000 ===")
for b, e in funcs_in(0x00E2C000, 0x00E2D000):
    print("  %08X..%08X  %d B" % (b, e, e - b))

print("\n=== functions 0xE3CF00..0xE3D200 ===")
for b, e in funcs_in(0x00E3CF00, 0x00E3D200):
    print("  %08X..%08X  %d B" % (b, e, e - b))

for rva, label in [(0x00E2C306, "caller of apply (flush tail)"),
                   (0x00E3CF9E, "second applier"),
                   (0x02995FB0, "engine SetShouldBeVisible caller"),
                   (0x02CD457D, "engine SetShouldBeVisible caller 2"),
                   (0x02D4D630, "engine SetShouldBeVisible caller 3")]:
    f = q.func_at(rva)
    show(f[0] if f else rva, 200, label)

print("\n=== callers of 0xE2C306's function ===")
f = q.func_at(0x00E2C306)
if f:
    for s, ff, mn in callers(f[0]):
        print("  site %08X in %08X" % (s, ff))

# FName scan for LevelManagerUpdate / LevelManagerDoneLoading
print("\n=== string xref scan (LevelManagerUpdate) ===")
for rva, text in q.iter_strings(minlen=4):
    if "LevelManager" in text or "LevelList" in text:
        print("  %08X %r" % (rva, text))
