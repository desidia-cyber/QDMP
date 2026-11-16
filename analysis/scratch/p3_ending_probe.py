"""play-p3 -- the ending's own wake-up: WHO calls PostEndingWakeOnBeach, and what the
Queen/Sipeha ending entries are called.

Method (same as probes/bp_local_callers.py): a local Blueprint call serialises the
callee's export index + 1.  So for each Function export in a package we search its
serial bytes for that value, which names the caller.
"""
import sys, struct, importlib.util
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\pak\code")
import pakread
spec = importlib.util.spec_from_file_location(
    "bpr", r"G:\Freespace\Queens_Domain\analysis\pak\code\probes\bp_bytecode_refs.py")
bpr = importlib.util.module_from_spec(spec); spec.loader.exec_module(bpr)

pk = pakread.Pak()

TARGETS = [
    ("QueensDomain/Content/Player/FS_PlayerChar.uasset",
     ["EndingWakeOnBeach", "PostEndingWakeOnBeach", "WakeOnBeachAnim",
      "CancelLockInPlaceForLoad", "LockInPlaceForLoad", "GrantFinalSword"]),
    ("QueensDomain/Content/Enemies/Queen/Queen_BP.uasset",
     ["Ending1Complete", "Ending2CutsceneEnded", "Ending1CutsceneManager_C",
      "Ending2CutsceneManager_C", "SipehaTransitionEnded", "DoStateTransition",
      "OnPhaseChanged", "DoOrbAnimation", "SetUpPlayerForOrbCutscene"]),
    ("QueensDomain/Content/Enemies/Sipeha/Sipeha_NPC.uasset",
     ["Ending3Complete", "Ending4Complete", "Sipeha_BanishDialogueAction",
      "Sipeha_EatDialogueAction", "EndingSeen", "RunActionWithinContext"]),
]

for path, targets in TARGETS:
    if path not in pk.entries:
        print("MISSING", path); continue
    data, split = bpr.combined(pk, path)
    names, imports, exports = bpr.tables(data)
    idx_of = {e["name"]: i + 1 for i, e in enumerate(exports)}
    print("\n=== %s  exports=%d split=%s ===" % (path.split("/")[-1], len(exports), split))
    funcs = [e for e in exports if bpr.cls_of(exports, imports, e["cls"]) == "Function"]
    print("  Function exports (%d): %s" % (len(funcs), ", ".join(f["name"] for f in funcs)))
    for t in targets:
        if t not in idx_of:
            imp = next((i for i, x in enumerate(imports) if x[2] == t), None)
            print("  %-30s NOT an export here%s" % (t, "" if imp is None else " (import #%d)" % imp))
            continue
        needles = [(idx_of[t], "export+1")]
        imp = next((i for i, x in enumerate(imports) if x[2] == t), None)
        if imp is not None:
            needles.append((-(imp + 1), "import"))
        hits = []
        for e in funcs:
            if e["name"] == t:
                continue
            seg = data[e["serial_off"]:e["serial_off"] + e["serial_size"]]
            for val, kind in needles:
                p = seg.find(struct.pack("<i", val))
                if p >= 0:
                    hits.append("%s [%s@+0x%X]" % (e["name"], kind, p))
        print("  %-30s idx=%-4d called by: %s" % (t, idx_of[t], ", ".join(hits) if hits else "NOBODY (in this package)"))
pk.close()
