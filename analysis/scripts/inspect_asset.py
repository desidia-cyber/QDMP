#!/usr/bin/env python3
"""Print every export of one package: class, object name, outer, serial size.

export[0] is not guaranteed to be the package's main asset, so a package that
classifies as MorphTarget can still hold the SkeletalMesh. This tool exists to
stop the report from making that mistake.
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import queenspak  # noqa: E402
import classes    # noqa: E402

PAK = (r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Content\Paks"
       r"\QueensDomain-WindowsNoEditor.pak")


def inspect(data, label):
    r = classes.R(data)
    s = classes.read_summary(r)
    names = classes.read_names(r, s["name_count"], s["name_offset"])
    imp = classes.read_imports(r, s["import_count"], s["import_offset"], names)
    ex = classes.read_exports(r, s["export_count"], s["export_offset"])
    print("\n--- %s  (%d bytes, %d exports, %d imports)"
          % (label, len(data), len(ex), len(imp)))
    for i, e in enumerate(ex):
        c = classes.resolve(e["class_index"], imp, ex, names)
        sup = classes.resolve(e["super_index"], imp, ex, names)
        nm = names[e["object_name"]] if e["object_name"] < len(names) else "?"
        print("  [%2d] %-34s %-28r outer=%-4d super=%-22r size=%-8d"
              % (i, c, nm, e["outer_index"], sup, e["serial_size"]))
    return s, names, imp, ex


def main(prefixes):
    with queenspak.Pak(PAK) as pk:
        for pre in prefixes:
            for raw, e in pk.entries.items():
                gp = queenspak.game_path(pk.mount, raw)
                if (gp == pre or gp.startswith(pre)) and gp.endswith((".uasset", ".umap")):
                    inspect(pk.read_file(raw), gp)


if __name__ == "__main__":
    args = sys.argv[1:] or [
        "/Game/Player/Assets/FPModel/Player2026/Player_FullChar_2026.uasset",
        "/Game/Player/Assets/FPModel/Player_FullChar_2026.uasset",
        "/Game/Player/Assets/FPModel/Player_FPArms_Arms.uasset",
    ]
    main(args)
