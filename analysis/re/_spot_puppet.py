import sys; sys.path.insert(0,r"G:\Freespace\Queens_Domain\analysis\re")
import qdlib as q
print("--- APawn::APawn around 0x02AD912E ---")
for ins in q.disasm(0x02AD90F0, count=40):
    if 0x02AD9110 <= ins.address <= 0x02AD9160:
        print("  %08X %-9s %-40s" % (ins.address,ins.mnemonic,ins.op_str))
print("--- ACharacter ctor clear-bit5 site 0x02747C6D ---")
for ins in q.disasm(0x02747C40, count=24):
    print("  %08X %-9s %-40s" % (ins.address,ins.mnemonic,ins.op_str))
print("--- AddToPlayerScreen 0x023F6C40 ---")
for ins in q.disasm(0x023F6C40, count=10):
    print("  %08X %-9s %-40s" % (ins.address,ins.mnemonic,ins.op_str))
