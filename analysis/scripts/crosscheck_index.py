#!/usr/bin/env python3
"""Cross-check the FPackageIndex direction on the whole pak.

If a class index really resolves to a class, the object it resolves to must be an
import whose own ClassName is "Class" (that is how UE records a class import).
Same for super/template: a template resolves to a "Default__X" import. Counting
those two ratios over every package is a stronger check than eyeballing names.
"""
import os
import struct
import sys
from collections import Counter

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import queenspak  # noqa: E402
import classes    # noqa: E402

PAK = (r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Content\Paks"
       r"\QueensDomain-WindowsNoEditor.pak")
IMPORT_SIZE = 28
EXPORT_SIZE = 104


def read_imports(data, s, names):
    out = []
    p = s["import_offset"]
    for _ in range(s["import_count"]):
        if p + IMPORT_SIZE > len(data):
            break
        cp, _cpn, cn, _cnn, outer, on, _onn = struct.unpack_from("<7i", data, p)
        out.append(dict(class_name=names[cn] if 0 <= cn < len(names) else "?",
                        object_name=names[on] if 0 <= on < len(names) else "?",
                        outer=outer))
        p += IMPORT_SIZE
    return out


def read_exports(data, s, names):
    out = []
    p = s["export_offset"]
    for _ in range(s["export_count"]):
        if p + 44 > len(data):
            break
        ci, si, ti, oi = struct.unpack_from("<4i", data, p)
        on = struct.unpack_from("<i", data, p + 16)[0]
        out.append(dict(ci=ci, si=si, ti=ti, oi=oi,
                        name=names[on] if 0 <= on < len(names) else "?"))
        p += EXPORT_SIZE
    return out


def main():
    stats = {d: Counter() for d in ("A", "B")}
    with queenspak.Pak(PAK) as pk:
        for raw, e in pk.entries.items():
            gp = queenspak.game_path(pk.mount, raw)
            if not gp.endswith(".uasset"):
                continue
            try:
                data = pk.read_file(raw)
                r = classes.R(data)
                s = classes.read_summary(r)
                names = classes.read_names(r, s["name_count"], s["name_offset"])
                imp = read_imports(data, s, names)
                ex = read_exports(data, s, names)
            except Exception:                                  # noqa: BLE001
                continue
            if not ex:
                continue
            for d in ("A", "B"):
                st = stats[d]
                ci = ex[0]["ci"]
                if d == "A":
                    is_imp, is_exp = ci > 0, ci < 0
                else:
                    is_imp, is_exp = ci < 0, ci > 0
                st["packages"] += 1
                if is_imp:
                    j = abs(ci) - 1
                    st["class_index_import"] += 1
                    if j < len(imp) and imp[j]["class_name"] == "Class":
                        st["class_is_Class_import"] += 1
                    elif j >= len(imp):
                        st["class_index_out_of_range"] += 1
                elif is_exp:
                    j = abs(ci) - 1
                    st["class_index_export"] += 1
                    if j >= len(ex):
                        st["class_index_out_of_range"] += 1
                else:
                    st["class_index_null"] += 1

                ti = ex[0]["ti"]
                if ti:
                    if d == "A":
                        t_imp = ti > 0
                    else:
                        t_imp = ti < 0
                    if t_imp:
                        j = abs(ti) - 1
                        if j < len(imp):
                            st["template_import"] += 1
                            if imp[j]["object_name"].startswith("Default__"):
                                st["template_is_Default"] += 1

    for d in ("A", "B"):
        print("\ndirection %s" % d)
        for k, v in stats[d].most_common():
            print("   %-32s %d" % (k, v))


if __name__ == "__main__":
    main()
