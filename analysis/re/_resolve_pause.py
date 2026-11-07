"""Resolve and validate the pause / time-dilation API surface in the shipping exe.

For each reflected UFunction name of interest:
  * every {name -> exec-thunk} table entry from nativepairs.bin
  * the thunk's shape (UE4 native exec thunks are a fixed prologue; a tail jmp or
    a single direct call reaches the real implementation)
  * the derived implementation RVA and whether .pdata knows it as a function

The 0x0119D280 table copy is the generic Blueprint-VM dispatcher; every name in
that copy resolves there and carries no information. Detected and reported.
"""
import os
import sys
import marshal

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qdlib as q

PAIRS = os.path.join(HERE, "nativepairs.bin")
VM_DISPATCH = 0x0119D280

NAMES = [
    "SetGamePaused", "IsGamePaused", "SetPause", "CanPause",
    "SetGlobalTimeDilation", "GetGlobalTimeDilation",
    "SetTimeDilation", "GetActorTimeDilation",
    "SetActorTickEnabled", "IsActorTickEnabled",
    "SetComponentTickEnabled", "IsComponentTickEnabled",
    "SetTickableWhenPaused", "GetTickableWhenPaused",
    "SetActorEnableCollision", "SetGlobalTimeDilation",
    "GetUnpausedTimeSeconds", "GetRealTimeSeconds", "GetWorldDeltaSeconds",
    "SetWorldTimeSeconds",
]


def load_pairs():
    with open(PAIRS, "rb") as f:
        return marshal.load(f)["pairs"]


def direct_calls(rva, limit=400):
    """Direct call/jmp rel32 targets inside a function body."""
    f = q.func_at(rva)
    if f is None:
        return [], None
    out = []
    for ins in q.disasm(f[0], end_rva=f[1]):
        if ins.mnemonic in ("call", "jmp"):
            t = q.rel_target(ins)
            if t is not None:
                out.append((ins.address, ins.mnemonic, t))
        if len(out) >= limit:
            break
    return out, f


def classify_thunk(thunk):
    """Return (kind, target) where kind is 'call' (last direct call) / 'jmp' (tail)."""
    calls, f = direct_calls(thunk, limit=200)
    if f is None:
        return ("not-a-function", None)
    jmps = [c for c in calls if c[1] == "jmp"]
    if jmps and jmps[-1][0] >= (f[1] - 6):
        return ("tailjmp", jmps[-1][2])
    if calls:
        return ("lastcall", calls[-1][2])
    return ("no-transfer", None)


def main():
    pairs = load_pairs()
    print("pair table: %d names" % len(pairs))
    print()
    for name in NAMES:
        ents = pairs.get(name)
        if not ents:
            print("== %s : NOT IN PAIR TABLE" % name)
            continue
        print("== %s : %d table entr%s" % (name, len(ents), "y" if len(ents) == 1 else "ies"))
        for base, thunk, nrva in ents:
            kind, tgt = classify_thunk(thunk)
            flag = ""
            if tgt == VM_DISPATCH or (kind == "lastcall" and tgt == VM_DISPATCH):
                flag = "   <-- VM dispatcher copy, no info"
            print("   table %08X  exec %08X  %-14s -> %s%s"
                  % (base, thunk, kind, ("%08X" % tgt) if tgt else "-", flag))
            if tgt and tgt != VM_DISPATCH:
                tf = q.func_at(tgt)
                print("       impl %08X  .pdata=%s  size=%s  start_of_func=%s"
                      % (tgt,
                         "yes" if tf else "NO",
                         ("%X" % (tf[1] - tf[0])) if tf else "-",
                         "yes" if q.is_func_start(tgt) else "no"))
        print()


if __name__ == "__main__":
    main()
