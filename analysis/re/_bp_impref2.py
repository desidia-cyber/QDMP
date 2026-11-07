"""Prove which blueprint function calls a native pause function, with opcode proof.

Method (validated on Photomode_Pawn and WeaponUpgradeMachine):
  * a native/static call in cooked BP bytecode is an int32 equal to -(import_index+1);
  * the int32 is the operand of a call opcode:
        EX_FinalFunction     0x1C
        EX_VirtualFunction   0x1B
        EX_LocalFinalFunction 0x1D
        EX_LocalVirtualFunction 0x1E
  * the byte immediately preceding the operand is that opcode.

The probe prints the opcode byte and the exact bytes, and resolves the byte offset
to the export whose [SerialOffset, SerialOffset+SerialSize) range contains it.
"""
import os
import sys
import struct
import binascii

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "analysis", "pak", "code"))
import pakread            # noqa: E402
import blueprints         # noqa: E402

OPCODES = {0x1B: "EX_VirtualFunction", 0x1C: "EX_FinalFunction",
           0x1D: "EX_LocalFinalFunction", 0x1E: "EX_LocalVirtualFunction"}

TARGETS = ["SetGamePaused", "IsGamePaused", "SetGlobalTimeDilation",
           "GetGlobalTimeDilation", "SetActorTickEnabled", "SetTickableWhenPaused",
           "SetActorTickInterval", "SetComponentTickInterval"]


def export_records(ua, info):
    f = info["tail_fields"]
    exp_count, exp_off = f[2], f[3]
    recs = []
    for i in range(exp_count):
        o = exp_off + i * 104
        name_idx, _ = struct.unpack("<2i", ua[o + 16:o + 24])
        ssize, soff = struct.unpack("<2q", ua[o + 28:o + 44])
        recs.append({"i": i, "name": info["names"][name_idx],
                     "size": ssize, "off": soff})
    return recs


def analyze(pk, asset, targets=TARGETS, verbose=True):
    ua = pk.payload(asset)
    uexp_path = asset[:-len(".uasset")] + ".uexp"
    uexp = pk.payload(uexp_path) if uexp_path in pk.entries else b""
    info = blueprints.parse(ua)
    imports, exports = blueprints.try_imports_exports(ua, info)
    recs = export_records(ua, info)
    base = len(ua)

    def owner(off):
        for r in recs:
            if r["off"] <= off < r["off"] + r["size"]:
                return r["name"]
        return None

    imp_names = [imp["name"] for imp in imports]
    results = {}
    for t in targets:
        if t not in imp_names:
            continue
        i = imp_names.index(t)
        pat = struct.pack("<i", -(i + 1))
        hits = []
        p = uexp.find(pat)
        while p >= 0:
            hits.append(p)
            p = uexp.find(pat, p + 1)
            if len(hits) > 200:
                break
        good = []
        for h in hits:
            op = uexp[h - 1] if h > 0 else None
            opn = OPCODES.get(op, None)
            if opn is None:
                continue
            good.append((h, opn, owner(h + base)))
        results[t] = {"import_index": i, "raw_hits": len(hits),
                      "opcode_hits": good}
        if verbose:
            print("  %-24s import#%-5d raw=%d  opcode-confirmed=%d"
                  % (t, i, len(hits), len(good)))
            for h, opn, own in good:
                ctx = binascii.hexlify(uexp[max(0, h - 4):h + 6]).decode()
                print("      uexp+%08X  %-20s in %-38s  bytes %s"
                      % (h, opn, own, ctx))
    return results


def main():
    assets = sys.argv[1:] or [
        "QueensDomain/Content/Player/FS_PlayerChar.uasset",
        "QueensDomain/Content/LD45/Blueprints/Base_LD45_GM.uasset",
    ]
    with pakread.Pak() as pk:
        for a in assets:
            print("\n==", a)
            try:
                analyze(pk, a)
            except Exception as e:
                import traceback
                traceback.print_exc()


if __name__ == "__main__":
    main()
