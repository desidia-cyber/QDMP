import sys
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
import qdlib as q

print("=== function starts with prologue 48 83 EC 28 E8 (sub rsp,0x28; call) ===")
d = q.data()
for b, e, u in q.functions():
    if not (0x028D0000 <= b < 0x02900000):
        continue
    o = q.rva_to_off(b)
    if o is None:
        continue
    head = d[o:o + 5]
    if head == b"\x48\x83\xEC\x28\xE8":
        ins = q.disasm(b, count=8)
        txt = " | ".join("%s %s" % (i.mnemonic, i.op_str) for i in ins)
        # check test rax,rax appears within first 5 insns
        if any(i.mnemonic == "test" and "rax, rax" in i.op_str for i in ins[:5]):
            print("  %08X  %s" % (b, txt))

print()
print("=== GameplayStatics cluster 0x028E0000-0x028E2100, first 5 insns ===")
for b, e, u in q.functions():
    if not (0x028E0000 <= b < 0x028E2100):
        continue
    ins = q.disasm(b, count=5)
    txt = " | ".join("%s %s" % (i.mnemonic, i.op_str) for i in ins)
    print("  %08X [%5d] %s" % (b, e - b, txt))
