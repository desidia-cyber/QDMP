import sys, struct
sys.path.insert(0, r"G:\Freespace\Queens_Domain\.worktrees\sync-fix\analysis\pak\code")
import pakread, importlib.util
spec = importlib.util.spec_from_file_location(
    "bpr", r"G:\Freespace\Queens_Domain\.worktrees\sync-fix\analysis\pak\code\probes\bp_bytecode_refs.py")
bpr = importlib.util.module_from_spec(spec); spec.loader.exec_module(bpr)
pk = pakread.Pak()
data, _ = bpr.combined(pk, r"QueensDomain/Content/Player/FS_PlayerChar.uasset")
names, imports, exports = bpr.tables(data)
for fn in ["Kill", "ShowGameOverScreen", "Respawn", "UseFallbackCheckpoint"]:
    e = next((x for x in exports if x["name"] == fn and bpr.cls_of(exports, imports, x["cls"]) == "Function"), None)
    if not e: continue
    lo, hi = e["serial_off"], e["serial_off"] + e["serial_size"]
    b = data[lo:hi]
    print("\n---", fn, e["serial_size"], "B ---")
    for off in range(0, min(len(b), 96), 16):
        print("  +%04X  %s  %s" % (off, " ".join("%02X" % x for x in b[off:off+16]),
              "".join(chr(x) if 32 <= x < 127 else "." for x in b[off:off+16])))
    # tail too
    print("  ... tail:")
    for off in range(max(0, len(b)-48), len(b), 16):
        print("  +%04X  %s  %s" % (off, " ".join("%02X" % x for x in b[off:off+16]),
              "".join(chr(x) if 32 <= x < 127 else "." for x in b[off:off+16])))
pk.close()
