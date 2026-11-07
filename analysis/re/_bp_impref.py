"""Attribute a native-function bind to the exact blueprint function that makes it.

Cooked BP bytecode stores a reference to a native UFunction as a *package index*
(negative = import table index), not as a name index -- which is why the raw name
index had zero occurrences. This probe:
  1. finds the target's import index i,
  2. searches the .uexp for int32 values -(i+1) and i,
  3. maps each hit to the export whose SerialOffset..SerialOffset+SerialSize covers it.

Validation: run on a small asset where the answer is guessable, and print the
export name for every hit. If hits land on sensible function names, the method is
sound; if they land randomly, it is rejected rather than used.
"""
import os
import sys
import struct
import json

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "analysis", "pak", "code"))
import pakread            # noqa: E402
import blueprints         # noqa: E402

TARGETS = ["SetGamePaused", "IsGamePaused", "SetGlobalTimeDilation",
           "GetGlobalTimeDilation", "SetActorTickEnabled", "SetTickableWhenPaused"]


def analyze(pk, asset):
    ua = pk.payload(asset)
    uexp_path = asset[:-len(".uasset")] + ".uexp"
    uexp = pk.payload(uexp_path) if uexp_path in pk.entries else b""
    info = blueprints.parse(ua)
    imports, exports = blueprints.try_imports_exports(ua, info)
    f = info["tail_fields"]
    exp_count, exp_off = f[2], f[3]

    # export records: ClassIndex,SuperIndex,TemplateIndex,OuterIndex @0,4,8,12
    #                 ObjectName(8)@16, ObjectFlags@24, SerialSize i64@28, SerialOffset i64@36
    recs = []
    for i in range(exp_count):
        o = exp_off + i * 104
        cls, sup, tmpl, outer = struct.unpack("<4i", ua[o:o + 16])
        name_idx, name_num = struct.unpack("<2i", ua[o + 16:o + 24])
        flags = struct.unpack("<I", ua[o + 24:o + 28])[0]
        ssize, soff = struct.unpack("<2q", ua[o + 28:o + 44])
        recs.append({"i": i, "cls": cls, "name": info["names"][name_idx],
                     "size": ssize, "off": soff})
    if not recs:
        return None
    base = len(ua)
    exp_lo = min(r["off"] for r in recs)
    exp_hi = max(r["off"] + r["size"] for r in recs)
    print("  uasset %d  uexp %d  exports %d  export-data range [%d,%d)  base==uasset? %s"
          % (len(ua), len(uexp), len(recs), exp_lo, exp_hi, exp_lo == base))

    def owner(blind_off):
        for r in recs:
            if r["off"] <= blind_off < r["off"] + r["size"]:
                return r["name"]
        return None

    imp_names = [imp["name"] for imp in imports]
    out = {}
    for t in TARGETS:
        if t not in imp_names:
            continue
        i = imp_names.index(t)
        cand = {}
        for val, tag in ((-(i + 1), "-(i+1)"), (i, "i")):
            pat = struct.pack("<i", val)
            hits = []
            p = uexp.find(pat)
            while p >= 0:
                hits.append(p)
                p = uexp.find(pat, p + 1)
                if len(hits) > 400:
                    break
            cand[tag] = [(h, owner(h + base)) for h in hits]
        out[t] = (i, cand)
        print("  %-24s import#%d" % (t, i))
        for tag, hl in cand.items():
            fam = {}
            for h, nm in hl:
                fam[nm] = fam.get(nm, 0) + 1
            print("      %-7s %3d hit(s)  owners: %s"
                  % (tag, len(hl), dict(sorted(fam.items(), key=lambda kv: -kv[1])[:8])))
    return out


def main():
    assets = sys.argv[1:] or [
        "QueensDomain/Content/UI/PhotoMode/Photomode_Pawn.uasset",
        "QueensDomain/Content/Blueprints/UpgradeMachine/WeaponUpgradeMachine.uasset",
    ]
    with pakread.Pak() as pk:
        for a in assets:
            print("\n==", a)
            try:
                analyze(pk, a)
            except Exception as e:
                print("   FAILED:", e)


if __name__ == "__main__":
    main()
