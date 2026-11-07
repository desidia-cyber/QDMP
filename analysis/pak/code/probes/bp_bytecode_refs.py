"""Which Blueprint function calls an imported UFunction — uasset+uexp aware.

Cooked packages are split: the .uasset holds the summary/name/import/export
tables and the .uexp holds the serialized export data. FObjectExport.SerialOffset
is an offset into the combined original package, so we reconstruct
combined = uasset_payload + uexp_payload and index directly.

Self-checks: every export range must fit in `combined`; we print the first/last.
"""
import sys, struct
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\pak\code")
import pakread, blueprints

EXPORT_SIZE = 104


def combined(pk, path):
    data = pk.payload(path)
    uexp = path[:-len(".uasset")] + ".uexp"
    if uexp in pk.entries:
        data += pk.payload(uexp)
        return data, True
    return data, False


def tables(data):
    r = blueprints.Reader(data)
    o = 4
    legacy = r.i32(o); o += 4
    if legacy != -4:
        o += 4
    o += 4; o += 4
    if legacy <= -2:
        custom = r.i32(o); o += 4; o += custom * 20
    if legacy < 0:
        o += 4
    folder, o = r.fstring(o)
    o += 4
    name_count = r.i32(o); o += 4
    name_offset = r.i32(o); o += 4
    soft_n, soft_off, exp_count, exp_off, imp_count, imp_off, dep_off = \
        struct.unpack("<7i", data[o:o + 28])
    names = []
    p = name_offset
    for _ in range(name_count):
        s, p = r.fstring(p); names.append(s); p += 4
    imports = []
    for i in range(imp_count):
        q = imp_off + i * 28
        # FImport: class_package FName(8) | class FName(8) | outer i32 | name FName(8)
        cp = r.i32(q)
        cn = r.i32(q + 8)
        outer = r.i32(q + 16)
        on = r.i32(q + 20)
        imports.append((names[cp] if 0 <= cp < len(names) else "?",
                        names[cn] if 0 <= cn < len(names) else "?",
                        names[on] if 0 <= on < len(names) else "?"))
    exports = []
    for i in range(exp_count):
        q = exp_off + i * EXPORT_SIZE
        cls, sup, tpl, outer = struct.unpack("<4i", data[q:q + 16])
        nm, num = struct.unpack("<ii", data[q + 16:q + 24])
        serial_size, serial_off = struct.unpack("<qq", data[q + 28:q + 44])
        exports.append(dict(name=names[nm] if 0 <= nm < len(names) else "?",
                            cls=cls, serial_off=serial_off, serial_size=serial_size))
    return names, imports, exports


def cls_of(exports, imports, idx):
    if idx == 0:
        return None
    if idx < 0:
        k = -idx - 1
        return imports[k][2] if 0 <= k < len(imports) else "?"
    return exports[idx - 1]["name"] if 0 <= idx - 1 < len(exports) else "?"


def analyse(pk, path, functions):
    data, split = combined(pk, path)
    names, imports, exports = tables(data)
    bad = [e for e in exports if e["serial_off"] < 0 or e["serial_size"] < 0
           or e["serial_off"] + e["serial_size"] > len(data)]
    print("\n%s  (split=%s, combined=%d B, exports=%d, bad_ranges=%d)" %
          (path, split, len(data), len(exports), len(bad)))
    if bad:
        print("   first bad:", bad[0])
    for fn in functions:
        k = next((i for i, imp in enumerate(imports) if imp[2] == fn), None)
        if k is None:
            print("   %-22s : not imported" % fn)
            continue
        needle = struct.pack("<i", -(k + 1))
        hits = []
        for e in exports:
            if cls_of(exports, imports, e["cls"]) != "Function":
                continue
            lo, hi = e["serial_off"], e["serial_off"] + e["serial_size"]
            if hi > len(data):
                continue
            pos = data[lo:hi].find(needle)
            if pos >= 0:
                hits.append((e["name"], pos))
        print("   %-22s : import #%d -> %d function export(s)" % (fn, k, len(hits)))
        for nm, pos in hits:
            print("        %s   (+0x%X in export)" % (nm, pos))
    return imports, exports, data


if __name__ == "__main__":
    pk = pakread.Pak()
    TARGETS = [
        "QueensDomain/Content/Player/FS_PlayerChar.uasset",
        "QueensDomain/Content/UI/PhotoMode/Photomode_Pawn.uasset",
        "QueensDomain/Content/Blueprints/UpgradeMachine/WeaponUpgradeMachine.uasset",
        "QueensDomain/Content/LD45/Blueprints/Base_LD45_GM.uasset",
        "QueensDomain/Content/Blueprints/Utils/ShaderCompileManager.uasset",
        "QueensDomain/Content/LD45/Blueprints/FS_CommonGM.uasset",
    ]
    for t in TARGETS:
        analyse(pk, t, ["SetGamePaused", "SetGlobalTimeDilation"])
    pk.close()
