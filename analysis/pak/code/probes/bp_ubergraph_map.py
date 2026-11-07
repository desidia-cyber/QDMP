"""Inspect FS_PlayerChar stubs + ubergraph to map the SetGamePaused call to an event.

Blueprint compiles events into a shared ExecuteUbergraph_* function; each event stub
pushes an int32 entry point and calls it. We dump the stubs (they are small) and the
head of the ubergraph so the entry offsets and bytecode base can be read directly.
"""
import sys, struct
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\pak\code")
import pakread
import importlib.util
spec = importlib.util.spec_from_file_location("bpr", r"G:\Freespace\Queens_Domain\analysis\pak\code\probes\bp_bytecode_refs.py")
bpr = importlib.util.module_from_spec(spec); spec.loader.exec_module(bpr)

pk = pakread.Pak()
path = "QueensDomain/Content/Player/FS_PlayerChar.uasset"
data, split = bpr.combined(pk, path)
names, imports, exports = bpr.tables(data)

want = ("Auto", "UI", "Menu", "menu", "Dialogue", "Status", "Pause", "Open", "Close", "Enter", "Exit", "Inventory")
print("--- small function exports ---")
for e in exports:
    if bpr.cls_of(exports, imports, e["cls"]) != "Function":
        continue
    if e["serial_size"] <= 400 and any(w in e["name"] for w in want):
        lo = e["serial_off"]; hi = lo + e["serial_size"]
        seg = data[lo:hi]
        print("\n%s  off=%d size=%d" % (e["name"], e["serial_off"], e["serial_size"]))
        print("   " + seg.hex(" "))

ub = next(e for e in exports if e["name"].startswith("ExecuteUbergraph"))
print("\n--- ubergraph head ---")
lo = ub["serial_off"]
print("name", ub["name"], "off", lo, "size", ub["serial_size"])
print(data[lo:lo + 128].hex(" "))

idx = next((i for i, imp in enumerate(imports) if imp[2] == "SetGamePaused"), None)
print("\nSetGamePaused import #%d needle %s" % (idx, struct.pack("<i", -(idx + 1)).hex()))
seg = data[lo:lo + ub["serial_size"]]
pos = seg.find(struct.pack("<i", -(idx + 1)))
print("needle at ubergraph serial offset +0x%X" % pos)
print("context:", seg[max(0, pos - 32):pos + 16].hex(" "))
pk.close()
