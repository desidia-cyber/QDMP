"""Test whether the ubergraph bytecode is duplicated and attribute SetGamePaused to
an event entry point.

Each event stub pushes an int32 entry (pattern `1d <int32>`). If the ubergraph
serial contains two copies of the bytecode, then needle_pos - entry is the same
base for the copy that contains the needle, and we can read off the event.
"""
import sys, struct
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\pak\code")
import pakread
import importlib.util
spec = importlib.util.spec_from_file_location("bpr", r"G:\Freespace\Queens_Domain\analysis\pak\code\probes\bp_bytecode_refs.py")
bpr = importlib.util.module_from_spec(spec); spec.loader.exec_module(bpr)

pk = pakread.Pak()
path = "QueensDomain/Content/Player/FS_PlayerChar.uasset"
data, _ = bpr.combined(pk, path)
names, imports, exports = bpr.tables(data)
ub = next(e for e in exports if e["name"].startswith("ExecuteUbergraph"))
seg = data[ub["serial_off"]:ub["serial_off"] + ub["serial_size"]]
k = next(i for i, imp in enumerate(imports) if imp[2] == "SetGamePaused")
needle = struct.pack("<i", -(k + 1))
occ = []
p = seg.find(needle)
while p >= 0:
    occ.append(p); p = seg.find(needle, p + 1)
print("needle occurrences:", ["0x%X" % x for x in occ])

# extract entry point from each small stub: pattern 1d <int32>, take the last one
entries = {}
for e in exports:
    if bpr.cls_of(exports, imports, e["cls"]) != "Function":
        continue
    lo, hi = e["serial_off"], e["serial_off"] + e["serial_size"]
    s = data[lo:hi]
    q = s.rfind(b"\x1d")
    if q >= 0 and q + 5 <= len(s):
        v = struct.unpack("<i", s[q + 1:q + 5])[0]
        if 0 <= v < len(seg):
            entries[e["name"]] = v
print("stub entries found:", len(entries))

for pos in occ:
    print("\nneedle @0x%X" % pos)
    for nm, ev in sorted(entries.items(), key=lambda kv: kv[1]):
        base = pos - ev
        if 0 <= base < len(seg):
            print("   base 0x%05X  entry 0x%05X  %s" % (base, ev, nm))

# duplicate check: compare a window at the two most plausible bases
bases = sorted({occ[0] - ev for ev in entries.values() if 0 <= occ[0] - ev < len(seg)})
print("\ncandidate bases for first occurrence:", ["0x%X" % b for b in bases[:20]])
pk.close()
