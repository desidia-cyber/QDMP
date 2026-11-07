import sys, struct, collections, os, marshal
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
from hunt import *

D = data()
tlo = 0x1000; thi = 0x373B000
RL = sec_by_name(".rdata")[1]; RH = RL + sec_by_name(".rdata")[2]

def rdstr(rva):
    if not (RL <= rva < RH): return None
    s = read_rva(rva, 220)
    if not s: return None
    i = s.find(b"\x00")
    s = s[:i] if i >= 0 else s
    if not s or not all(0x20 <= c < 0x7f for c in s): return None
    return s.decode("latin-1")

def pair_at(i):
    """[name_ptr][func_ptr] at i ; returns (name, code, name_rva) or None"""
    v1 = int.from_bytes(D[i:i+8], "little")
    v2 = int.from_bytes(D[i+8:i+16], "little")
    if not (IMAGE_BASE + RL <= v1 < IMAGE_BASE + RH): return None
    if not (IMAGE_BASE + tlo <= v2 < IMAGE_BASE + thi): return None
    nm = rdstr(v1 - IMAGE_BASE)
    if nm is None: return None
    return (nm, v2 - IMAGE_BASE, v1 - IMAGE_BASE)

pairs = collections.defaultdict(list)
runs = []
i = 0
n = len(D)
while i + 16 <= n:
    p = pair_at(i)
    if p:
        start = i
        base = None
        ents = []
        while i + 16 <= n:
            q = pair_at(i)
            if not q: break
            ents.append(q)
            i += 16
        if len(ents) >= 3:
            base = off_to_rva(start)
            runs.append((base, len(ents)))
            for nm, code, nrva in ents:
                pairs[nm].append((base, code, nrva))
        else:
            i = start + 8
    else:
        i += 8

print("runs:", len(runs), "unique names:", len(pairs))
for b, c in sorted(runs, key=lambda x: -x[1])[:10]:
    print("   table %08X entries=%d" % (b, c))
print()
for probe in ["GetPlayerPawn", "GetWorldDeltaSeconds", "BeginDeferredActorSpawnFromClass",
              "FinishSpawningActor", "GetPlayerController", "AreSubtitlesEnabled",
              "ApplyDamage", "GetViewProjectionMatrix"]:
    print("%-34s %s" % (probe, ["%08X@%08X" % (c, b) for b, c, _ in pairs.get(probe, [])][:6]))

with open(r"G:\Freespace\Queens_Domain\analysis\re\nativepairs.bin", "wb") as f:
    marshal.dump({"pairs": dict(pairs), "runs": runs}, f)
print("saved.")
