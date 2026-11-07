import sys; sys.path.insert(0,r"G:\Freespace\Queens_Domain\analysis\re")
import qdlib as q
print("--- AddToPlayerScreen 0x023F6C40 full ---")
for ins in q.disasm(0x023F6C40, count=40):
    print("  %08X %-9s %-44s" % (ins.address,ins.mnemonic,ins.op_str))
print("--- find literal ReceiveDrawHUD ---")
for r,t in q.find_string("ReceiveDrawHUD", exact=True, limit=5):
    print("  literal at %08X" % r)
