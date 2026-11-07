"""Decode the imports referenced around the SetGamePaused call in FS_PlayerChar's
ubergraph. Each VM call operand is an FPackageIndex = -(import+1); scanning the
window for such values names the neighbouring calls and identifies the event body."""
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
pos = seg.find(needle)
print("ubergraph %s  needle at +0x%X  (import #%d)" % (ub["name"], pos, k))

W = 600
lo = max(0, pos - W)
hi = min(len(seg), pos + W)
print("window +0x%X .. +0x%X" % (lo, hi))
# every 4-byte value that is a valid negative import index
for off in range(lo, hi - 3):
    if off % 1:  # scan every byte so unaligned operands are found
        pass
    v = struct.unpack_from("<i", seg, off)[0]
    if v < 0:
        kk = -v - 1
        if 0 <= kk < len(imports):
            cls, cname, oname = imports[kk]
            mark = "  <== SetGamePaused" if oname == "SetGamePaused" and off == pos else ""
            print("  +0x%05X  idx=%-5d %-28s %-30s%s" % (off, kk, oname, cls, mark))

print("\nimports whose object name matches menu/ui/pause/photo/dialogue:")
for i, imp in enumerate(imports):
    if any(w.lower() in imp[2].lower() for w in
           ["Pause", "Menu", "UI", "Photo", "Dialogue", "Inventory", "Status", "InputMode"]):
        print("  #%d %-34s class=%s" % (i, imp[2], imp[1]))
pk.close()
