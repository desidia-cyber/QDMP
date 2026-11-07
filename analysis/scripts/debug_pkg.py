#!/usr/bin/env python3
"""Dump summary + import/export tables for one package, with raw ints, so the
record layout can be read off the data instead of assumed."""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import queenspak  # noqa: E402
import classes    # noqa: E402

PAK = (r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Content\Paks"
       r"\QueensDomain-WindowsNoEditor.pak")


def main(gp):
    with queenspak.Pak(PAK) as pk:
        raw = None
        for r, e in pk.entries.items():
            if queenspak.game_path(pk.mount, r) == gp:
                raw = r
                break
        if raw is None:
            print("not found:", gp)
            return
        data = pk.read_file(raw)

    print("package : %s  (%d bytes)" % (gp, len(data)))
    r = classes.R(data)
    s = classes.read_summary(r)
    for k, v in s.items():
        print("  %-24s %r" % (k, v))
    names = classes.read_names(r, s["name_count"], s["name_offset"])
    print("\nnames (%d)" % len(names))
    for i, n in enumerate(names[:40]):
        print("  %3d %r" % (i, n))
    print("  ...")
    for i, n in list(enumerate(names))[-10:]:
        print("  %3d %r" % (i, n))

    print("\nimports (%d) @%d  raw=(classPackage, className, outerIndex, objectName)"
          % (s["import_count"], s["import_offset"]))
    r.p = s["import_offset"]
    for i in range(min(s["import_count"], 25)):
        cp, cn, outer, on = struct.unpack_from("<4i", data, r.p)
        r.p += 16
        def nm(x):
            return names[x] if 0 <= x < len(names) else "<%d>" % x
        print("  %3d  cp=%-6d %-28r cn=%-6d %-26r outer=%-6d on=%-6d %r"
              % (i, cp, nm(cp), cn, nm(cn), outer, on, nm(on)))

    print("\nexports (%d) @%d" % (s["export_count"], s["export_offset"]))
    r.p = s["export_offset"]
    for i in range(min(s["export_count"], 12)):
        vals = struct.unpack_from("<3i", data, r.p)
        print("  %3d  first3 ints: %r   next4: %r"
              % (i, vals, struct.unpack_from("<i", data, r.p + 12)))
        print("        next8 ints from +16: %r"
              % (struct.unpack_from("<8i", data, r.p + 16),))
        r.p += 56          # 3*4 + 4 + 4 + 4 + 8 + 8 + 12 + 16 + 4 + 8 + 20
    print("\n  layout guess length 56 for shown rows")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1
         else "/Game/Player/Assets/Anims/Attacks/FlyingSword/Player_FPArms_FS_Grounded_Back.uasset")
