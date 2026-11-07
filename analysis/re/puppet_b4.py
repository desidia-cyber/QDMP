import sys, marshal, re
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
import qdlib as q

M = marshal.load(open(r"G:\Freespace\Queens_Domain\analysis\re\nativepairs.bin", "rb"))
pairs = M["pairs"]

def lookup(name):
    return [(b, c, n) for (b, c, n) in pairs.get(name, [])]

def impl_of(exec_rva):
    """exec thunk -> last call target (the real C++ implementation)."""
    ins = q.disasm(exec_rva, count=60)
    calls = [i for i in ins if i.mnemonic == "call" and i.operands and i.operands[0].type == 2]
    if not calls:
        return None, ins
    tgt = calls[-1].operands[0].imm & 0xFFFFFFFF
    return tgt, ins

def show(rva, n=40, label=""):
    print("--- %s FUN_%08X ---" % (label, rva))
    for ins in q.disasm(rva, count=n):
        note = ""
        if ins.mnemonic in ("call", "jmp") and ins.operands and ins.operands[0].type == 2:
            t = ins.operands[0].imm & 0xFFFFFFFF
            if 0x1000 <= t < 0x373B000:
                note = " -> FUN_%08X" % t
        print("  %08X %-9s %-46s%s" % (ins.address, ins.mnemonic, ins.op_str, note))

for name in ["SetOwnerNoSee", "SetOnlyOwnerSee", "SetHiddenInGame",
             "SetVisibility", "SetRenderInMainPass", "SetCastShadow"]:
    v = lookup(name)
    print("== %s : %s" % (name, ["exec %08X @table %08X" % (c, b) for b, c, _ in v]))
    for b, c, _ in v:
        impl, ins = impl_of(c)
        print("   exec %08X -> impl %s" % (c, ("%08X" % impl) if impl else "NONE"))
        if impl:
            show(impl, 45, name + " impl")
    print()
