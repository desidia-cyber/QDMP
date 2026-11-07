import sys, struct
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\pak\code")
import pakread, importlib.util
spec = importlib.util.spec_from_file_location("bpr", r"G:\Freespace\Queens_Domain\analysis\pak\code\probes\bp_bytecode_refs.py")
bpr = importlib.util.module_from_spec(spec); spec.loader.exec_module(bpr)
pk = pakread.Pak()
path = "QueensDomain/Content/Player/FS_PlayerChar.uasset"
data,_ = bpr.combined(pk, path)
names, imports, exports = bpr.tables(data)
ub = next(e for e in exports if e["name"].startswith("ExecuteUbergraph"))
ubi = next(i for i,e in enumerate(exports) if e is ub) + 1
print("ubergraph export index+1 =", ubi, hex(ubi))
stub = next(e for e in exports if e["name"]=="TryAutoPause")
s = data[stub["serial_off"]:stub["serial_off"]+stub["serial_size"]]
print("TryAutoPause hex:", s.hex(" "))
print("pattern 46 + ubi:", (b"\x46"+struct.pack("<i",ubi)).hex(" "), "found:", s.find(b"\x46"+struct.pack("<i",ubi)))
# search ubergraph for local-call opcode 0x46 + EnterUIMode index+1 (80)
ent = next(i for i,e in enumerate(exports) if e["name"]=="EnterUIMode") + 1
seg = data[ub["serial_off"]:ub["serial_off"]+ub["serial_size"]]
pat = b"\x46"+struct.pack("<i",ent)
print("EnterUIMode idx+1", ent, "ubergraph occurrences of 0x46+idx:", seg.count(pat))
# where
p=seg.find(pat); locs=[]
while p>=0:
    locs.append(p); p=seg.find(pat,p+1)
print("at:", ["0x%X"%x for x in locs[:10]])
pk.close()
