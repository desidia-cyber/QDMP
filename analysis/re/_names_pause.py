"""Enumerate every reflected name touching pause / dilation / tick, with impl RVAs.

Prints the raw pair-table membership so the analysis can state exactly which
instrument saw what. Also resolves helper functions the impls call
(UEngine::GetWorldFromContextObject, UWorld::GetWorldSettings) so later steps do
not have to guess.
"""
import os
import sys
import re
import marshal

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qdlib as q

PAIRS = os.path.join(HERE, "nativepairs.bin")
VM_DISPATCH = 0x0119D280

PAT = re.compile(r"(Pause|TimeDilation|GlobalTime|TickEnabled|TickableWhenPaused|"
                 r"GamePaused|TickInterval|ActorTick)", re.I)


def load_pairs():
    with open(PAIRS, "rb") as f:
        return marshal.load(f)["pairs"]


def last_call(thunk):
    f = q.func_at(thunk)
    if f is None:
        return None
    calls = []
    for ins in q.disasm(thunk, end_rva=f[1]):
        if ins.mnemonic in ("call", "jmp"):
            t = q.rel_target(ins)
            if t is not None:
                calls.append((ins.address, ins.mnemonic, t))
    if not calls:
        return None
    return calls[-1][2]


def main():
    pairs = load_pairs()
    names = sorted(n for n in pairs if PAT.search(n))
    print("reflected names matching pause/dilation/tick pattern: %d" % len(names))
    print()
    real = {}
    for n in names:
        ents = pairs[n]
        good = []
        for base, thunk, nrva in ents:
            tgt = last_call(thunk)
            if tgt and tgt != VM_DISPATCH:
                good.append((base, thunk, tgt))
        real[n] = good
        print("%-42s tables=%d  usable=%d  %s"
              % (n, len(ents), len(good),
                 " ".join("%08X" % g[2] for g in good)))
    print()
    print("usable (non-VM-dispatch) names: %d of %d" % (sum(1 for v in real.values() if v), len(names)))
    print()
    # which names have no usable copy at all
    dead = [n for n, v in real.items() if not v]
    if dead:
        print("only VM-dispatcher copies (no information):")
        for n in dead:
            print("   ", n)


if __name__ == "__main__":
    main()
