import sys, struct, collections
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\pak\code")
import pakread, importlib.util
spec = importlib.util.spec_from_file_location(
    "bpr", r"G:\Freespace\Queens_Domain\analysis\pak\code\probes\bp_bytecode_refs.py")
bpr = importlib.util.module_from_spec(spec); spec.loader.exec_module(bpr)

pk = pakread.Pak()
path = "QueensDomain/Content/Player/FS_PlayerChar.uasset"
data, _ = bpr.combined(pk, path)
names, imports, exports = bpr.tables(data)
ub = next(e for e in exports if e["name"].startswith("ExecuteUbergraph"))
seg = data[ub["serial_off"]:ub["serial_off"] + ub["serial_size"]]
idx = {e["name"]: i + 1 for i, e in enumerate(exports)}

for tgt in ["EnterUIMode", "ExitUIMode", "TryAutoPause", "OpenPauseMenu",
            "StartDialogue", "PrepareForDialogue", "OpenStatus",
            "OpenSaveMenu", "OpenKeymenu", "OpenShopmenu"]:
    v = idx.get(tgt)
    if v is None:
        print(tgt, "not exported")
        continue
    b = struct.pack("<i", v)
    ops = collections.Counter()
    p = seg.find(b)
    n = 0
    while p >= 0:
        n += 1
        if p > 0:
            ops[seg[p - 1]] += 1
        p = seg.find(b, p + 1)
    print("%-20s idx=%-5d occurrences=%d preceding=%s" %
          (tgt, v, n, {hex(k): c for k, c in ops.most_common(6)}))
pk.close()
