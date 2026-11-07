"""Neighbourhood decode around a bytecode call site.

A blueprint ubergraph is one merged code blob, so imports referenced within a few
hundred bytes of a call site belong to the same event implementation. This decodes
every int32 in a window that matches a valid import reference -(j+1) and prints the
import name, with the distance to the target call. That is the evidence used to
say *which* event a pause call belongs to when the exact event entry cannot be
recovered statically.
"""
import os
import sys
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "analysis", "pak", "code"))
import pakread            # noqa: E402
import blueprints         # noqa: E402

WINDOW = 700


def load(pk, asset):
    ua = pk.payload(asset)
    uexp = pk.payload(asset[:-len(".uasset")] + ".uexp")
    info = blueprints.parse(ua)
    imports, exports = blueprints.try_imports_exports(ua, info)
    f = info["tail_fields"]
    recs = []
    for i in range(f[2]):
        o = f[3] + i * 104
        ni, _ = struct.unpack("<2i", ua[o + 16:o + 24])
        ss, so = struct.unpack("<2q", ua[o + 28:o + 44])
        recs.append((info["names"][ni], so, ss))
    return ua, uexp, imports, recs


def owner(recs, base, off):
    for n, so, ss in recs:
        if so <= off < so + ss:
            return n
    return None


def refs_in(uexp, imports, lo, hi):
    """Decode -(j+1) import references in [lo,hi)."""
    names = [i["name"] for i in imports]
    idx = {}
    for j, nm in enumerate(names):
        idx.setdefault(-(j + 1), nm)
    out = []
    for p in range(max(0, lo), min(len(uexp) - 3, hi)):
        v = struct.unpack("<i", uexp[p:p + 4])[0]
        nm = idx.get(v)
        if nm is not None:
            out.append((p, nm))
    return out


def main():
    jobs = sys.argv[1:]
    if not jobs:
        jobs = [
            "QueensDomain/Content/Player/FS_PlayerChar.uasset:SetGamePaused",
            "QueensDomain/Content/Player/FS_PlayerChar.uasset:IsGamePaused",
            "QueensDomain/Content/LD45/Blueprints/Base_LD45_GM.uasset:SetGlobalTimeDilation",
        ]
    with pakread.Pak() as pk:
        for job in jobs:
            asset, target = job.split(":")
            ua, uexp, imports, recs = load(pk, asset)
            base = len(ua)
            names = [i["name"] for i in imports]
            if target not in names:
                print("!! %s not imported by %s" % (target, asset))
                continue
            j = names.index(target)
            pat = struct.pack("<i", -(j + 1))
            print("\n==== %s :: %s (import #%d)" % (asset.split("/Content/")[-1], target, j))
            p = uexp.find(pat)
            while p >= 0:
                own = owner(recs, base, p + base)
                print("  call @uexp%08X  export=%s" % (p, own))
                near = refs_in(uexp, imports, p - WINDOW, p + WINDOW)
                # dedupe, keep nearest occurrence per name
                best = {}
                for q, nm in near:
                    if nm == target:
                        continue
                    d = q - p
                    if nm not in best or abs(d) < abs(best[nm]):
                        best[nm] = d
                for nm, d in sorted(best.items(), key=lambda kv: abs(kv[1]))[:24]:
                    print("      %+6d  %s" % (d, nm))
                print()
                p = uexp.find(pat, p + 1)


if __name__ == "__main__":
    main()
