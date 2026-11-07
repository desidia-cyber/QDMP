"""Which FS_PlayerChar functions call EnterUIMode / TryAutoPause?

Local blueprint calls use the callee's export index. We resolve each export's
index+1, then search every Function export's serial bytes for that value.
"""
import sys, struct
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\pak\code")
import pakread
import importlib.util
spec = importlib.util.spec_from_file_location("bpr", r"G:\Freespace\Queens_Domain\analysis\pak\code\probes\bp_bytecode_refs.py")
bpr = importlib.util.module_from_spec(spec); spec.loader.exec_module(bpr)

pk = pakread.Pak()
for path in ["QueensDomain/Content/Player/FS_PlayerChar.uasset",
             "QueensDomain/Content/Player/FS_PlayerController_BP.uasset"]:
    data, _ = bpr.combined(pk, path)
    names, imports, exports = bpr.tables(data)
    idx_of = {e["name"]: i + 1 for i, e in enumerate(exports)}   # positive FPackageIndex
    print("\n=== %s (%d exports) ===" % (path, len(exports)))
    targets = ["EnterUIMode", "ExitUIMode", "TryAutoPause", "OpenPauseMenu", "SetGamePaused"]
    for t in targets:
        if t in idx_of:
            print("  %-18s export index+1 = %d" % (t, idx_of[t]))
    print()
    for t in ["EnterUIMode", "TryAutoPause"]:
        if t not in idx_of:
            print("  %s: not exported here" % t); continue
        # import-based alternative: also try negative import index
        imp_idx = next((i for i, imp in enumerate(imports) if imp[2] == t), None)
        needles = [(idx_of[t], "export+1")]
        if imp_idx is not None:
            needles.append((-(imp_idx + 1), "import"))
        print("  callers of %s:" % t)
        for e in exports:
            if bpr.cls_of(exports, imports, e["cls"]) != "Function":
                continue
            lo, hi = e["serial_off"], e["serial_off"] + e["serial_size"]
            seg = data[lo:hi]
            found = []
            for val, kind in needles:
                b = struct.pack("<i", val)
                p = seg.find(b)
                if p >= 0:
                    found.append("%s@+0x%X" % (kind, p))
            if found:
                print("      %-46s %s" % (e["name"], ", ".join(found)))
pk.close()
