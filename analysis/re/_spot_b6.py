import sys; sys.path.insert(0,r"G:\Freespace\Queens_Domain\analysis\re")
import qdlib as q
print("--- 0x02664630 (SetReplicateMovement?) ---")
for ins in q.disasm(0x02664630, count=22):
    print("  %08X %-9s %-40s" % (ins.address,ins.mnemonic,ins.op_str))
print("--- 0x02663b30 (called just before) ---")
for ins in q.disasm(0x02663b30, count=12):
    print("  %08X %-9s %-40s" % (ins.address,ins.mnemonic,ins.op_str))
