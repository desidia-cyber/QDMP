"""Independent pause-surface scan (cross-check for the delegated D1 audit).

Lists every reflected name matching pause / time-dilation / tick-disable, resolves
each to its exec thunk and implementation, and prints the implementation's first
instructions so a pause flag write is visible.
"""
import sys, marshal, re
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
import qdlib as q

M = marshal.load(open(r"G:\Freespace\Queens_Domain\analysis\re\nativepairs.bin", "rb"))
pairs = M["pairs"]

PAT = re.compile(r"(Pause|TimeDilation|TickEnabled|TickableWhenPaused|GlobalTime|SetTick)", re.I)

def impl_of(exec_rva):
    ins = q.disasm(exec_rva, count=80)
    calls = [i for i in ins if i.mnemonic == "call" and i.operands and i.operands[0].type == 2]
    if not calls:
        return None
    return calls[-1].operands[0].imm & 0xFFFFFFFF

names = sorted(n for n in pairs if PAT.search(n))
print("matching reflected names:", len(names))
for n in names:
    print("\n== %s" % n)
    for base, thunk, nrva in pairs[n]:
        impl = impl_of(thunk)
        print("   table %08X  exec %08X  impl %s" % (base, thunk, ("%08X" % impl) if impl else "-"))
        if impl and impl != 0x0119D280:
            for ins in q.disasm(impl, count=12):
                print("       %08X %-9s %-42s" % (ins.address, ins.mnemonic, ins.op_str))
