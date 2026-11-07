"""Probe: can a UFunction bind inside a cooked blueprint be attributed to the
exporting blueprint function (i.e. the exact call site)?

If yes, the state mapping becomes proven rather than name-inferred. The test is
empirical: locate the packge name-table index of 'SetGamePaused', search the
serialized body for it, and inspect the bytes that precede each occurrence for a
Blueprint opcode (EX_FinalFunction 0x1B / EX_VirtualFunction 0x1C / local variants).

Prints the export table geometry so the offset->export mapping can be validated.
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

ASSET = "QueensDomain/Content/Player/FS_PlayerChar.uasset"


def fname_reader(data, names):
    pass


def main():
    with pakread.Pak() as pk:
        ua = pk.payload(ASSET)
        uexp_path = ASSET[:-len(".uasset")] + ".uexp"
        uexp = pk.payload(uexp_path) if uexp_path in pk.entries else b""
    print("uasset %d bytes, uexp %d bytes" % (len(ua), len(uexp)))

    info = blueprints.parse(ua)
    imports, exports = blueprints.try_imports_exports(ua, info)
    names = info["names"]
    print("names %d  imports %d  exports %d" % (len(names), len(imports), len(exports)))
    print("tail_fields", info["tail_fields"])

    f = info["tail_fields"]
    exp_count, exp_off, imp_count, imp_off = f[2], f[3], f[4], f[5]
    print("export table at %d, count %d, stride 104" % (exp_off, exp_count))

    # export record geometry: verify by dumping the first few i32/i64 words
    for i in range(min(3, exp_count)):
        o = exp_off + i * 104
        words = struct.unpack("<26i", ua[o:o + 104])
        print("  exp[%d] @" % i, o, words)

    # name index of the target functions
    for target in ["SetGamePaused", "IsGamePaused", "OpenPauseMenu", "TryAutoPause",
                   "CheckForOverlayPause", "Pause"]:
        try:
            idx = names.index(target)
        except ValueError:
            print("  %-22s NOT in name table" % target)
            continue
        print("  %-22s name index %d" % (target, idx))
        pat = struct.pack("<i", idx)
        hits = []
        p = uexp.find(pat)
        while p >= 0 and len(hits) < 40:
            hits.append(p)
            p = uexp.find(pat, p + 1)
        print("      raw u32 occurrences in .uexp: %d" % len(hits))
        for h in hits[:12]:
            ctx = uexp[max(0, h - 6):h + 10]
            print("        @%08X  %s" % (h, binascii.hexlify(ctx).decode()))


if __name__ == "__main__":
    main()
