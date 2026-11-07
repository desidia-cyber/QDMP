"""C10 residual: QD level-manager apply/ordering + the recorded-visibility function.

Prints:
  - every UFUNCTION name containing LevelManager/LevelList and its thunk/impl
  - callers of ULevelStreaming::SetShouldBeVisible 0x029ACB20 (engine side)
  - the manager apply function 0x00E21E40, its callers, and its callees
  - FlushLevelManagerActions 0x00E2C270 and AddLevelListToStack 0x00E37800
  - GetLevelManager candidates 0x00E2EDC0 / 0x00E2EE50
  - UWorld::ProcessLevelStreamingVolumes 0x029A66B0 and neighbours
"""
import sys, marshal
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


def show(rva, n=120, label="", lo=None):
    f = q.func_at(rva)
    print("\n=== %s %08X func=%s ===" % (label, rva,
          ("%08X..%08X (%d B)" % (f[0], f[1], f[1] - f[0])) if f else "none"))
    shown = 0
    for ins in q.disasm(rva, count=n * 3 if lo else n):
        if lo is not None and ins.address < lo:
            continue
        print("  %08X %-9s %-46s%s" % (ins.address, ins.mnemonic, ins.op_str, note(ins)))
        shown += 1
        if shown >= n:
            break


def main():
    M = marshal.load(open(r"G:\Freespace\Queens_Domain\analysis\re\nativepairs.bin", "rb"))
    pairs = M["pairs"]
    print("=== UFUNCTIONs matching LevelManager/LevelList/StreamLevel ===")
    for name in sorted(pairs):
        low = name.lower()
        if "levelmanager" in low or "levellist" in low or "streamlevel" in low or name in (
                "SetShouldBeLoaded", "SetShouldBeVisible", "ShouldBeVisible", "ShouldBeLoaded"):
            for b, c, nr in pairs[name]:
                # resolve thunk impl = last direct call in the first 100 insns
                ins = q.disasm(c, count=100)
                calls = [i.operands[0].imm & 0xFFFFFFFF for i in ins
                         if i.mnemonic == "call" and i.operands and i.operands[0].type == 2]
                impl = calls[-1] if calls else None
                print("  %-34s table=%08X thunk=%08X impl=%s" %
                      (name, b, c, ("%08X" % impl) if impl else "-"))

    print("\n=== callers of SetShouldBeVisible 0x029ACB20 ===")
    for s, f, mn in callers(0x029ACB20):
        print("  call site %08X in %08X (%s)" % (s, f, mn))

    show(0x00E21E40, 140, "manager apply loop")
    print("\n--- callers of 0x00E21E40 ---")
    for s, f, mn in callers(0x00E21E40):
        print("  call site %08X in %08X (%s)" % (s, f, mn))

    show(0x00E2C270, 90, "FlushLevelManagerActions")
    print("\n--- callers of 0x00E2C270 ---")
    for s, f, mn in callers(0x00E2C270):
        print("  call site %08X in %08X (%s)" % (s, f, mn))

    show(0x00E37800, 40, "AddLevelListToStack entry", lo=0x00E378CD)
    show(0x00E378CD, 110, "AddLevelListToStack real")
    show(0x00E2EDC0, 40, "GetLevelManager A")
    show(0x00E2EE50, 60, "GetLevelManager B")

    show(0x029A66B0, 60, "UWorld::ProcessLevelStreamingVolumes?")
    for s, f, mn in callers(0x029A66B0):
        print("  call site %08X in %08X (%s)" % (s, f, mn))


if __name__ == "__main__":
    main()
