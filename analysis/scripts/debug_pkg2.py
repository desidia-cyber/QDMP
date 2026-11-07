#!/usr/bin/env python3
"""Read the import/export tables with package FNames (8 bytes: index + number)."""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import queenspak  # noqa: E402
import classes    # noqa: E402

PAK = (r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Content\Paks"
       r"\QueensDomain-WindowsNoEditor.pak")

IMPORT_SIZE = 28   # FName(8) + FName(8) + i32 + FName(8)
EXPORT_SIZE = 104  # see below


def parse_imports(data, off, n, names):
    out = []
    p = off
    for _ in range(n):
        cp, cpn, cn, cnn, outer, on, onn = struct.unpack_from("<7i", data, p)
        p += IMPORT_SIZE
        out.append(dict(
            class_package=names[cp] if 0 <= cp < len(names) else "?",
            class_name=names[cn] if 0 <= cn < len(names) else "?",
            outer=outer,
            object_name=names[on] if 0 <= on < len(names) else "?",
        ))
    return out


def parse_exports(data, off, n, names):
    out = []
    p = off
    for _ in range(n):
        ci, si, ti, oi = struct.unpack_from("<4i", data, p)
        on, onn = struct.unpack_from("<2i", data, p + 16)
        oflags = struct.unpack_from("<I", data, p + 24)[0]
        ssize, soff = struct.unpack_from("<2q", data, p + 28)
        p += EXPORT_SIZE
        out.append(dict(class_index=ci, super_index=si, template_index=ti,
                        outer_index=oi,
                        object_name=names[on] if 0 <= on < len(names) else "?",
                        flags=oflags, serial_size=ssize, serial_offset=soff))
    return out


def main(gp):
    with queenspak.Pak(PAK) as pk:
        raw = next(r for r, e in pk.entries.items()
                   if queenspak.game_path(pk.mount, r) == gp)
        data = pk.read_file(raw)
        others = [queenspak.game_path(pk.mount, r) for r in pk.entries
                  if queenspak.game_path(pk.mount, r).startswith(gp.rsplit(".", 1)[0])]

    print("package : %s  (%d bytes)" % (gp, len(data)))
    print("siblings: %r" % (others,))
    r = classes.R(data)
    s = classes.read_summary(r)
    print("summary : names=%d@%d imports=%d@%d exports=%d@%d header=%s"
          % (s["name_count"], s["name_offset"], s["import_count"],
             s["import_offset"], s["export_count"], s["export_offset"],
             s.get("total_header_size")))
    print("          import_span=%d  export_span=%d  (file %d)"
          % (s["import_count"] * IMPORT_SIZE,
             s["export_count"] * EXPORT_SIZE, len(data)))
    names = classes.read_names(r, s["name_count"], s["name_offset"])

    imp = parse_imports(data, s["import_offset"], s["import_count"], names)
    print("\nimports:")
    for i, x in enumerate(imp):
        print("  %2d %-26r class=%-24r outer=%-4d %r"
              % (i, x["object_name"], x["class_name"], x["outer"], x["class_package"]))

    exp = parse_exports(data, s["export_offset"], s["export_count"], names)
    print("\nexports:")
    for i, x in enumerate(exp):
        print("  %2d %-30r classIndex=%d superIndex=%d template=%d outer=%d size=%d off=%d"
              % (i, x["object_name"], x["class_index"], x["super_index"],
                 x["template_index"], x["outer_index"], x["serial_size"],
                 x["serial_offset"]))

    print("\nraw export ints:")
    for k in range(0, max(0, len(data) - s["export_offset"]), 4):
        v = struct.unpack_from("<i", data, s["export_offset"] + k)[0]
        print("  +%3d  %d" % (k, v))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1
         else "/Game/Player/Assets/Anims/Attacks/FlyingSword/Player_FPArms_FS_Grounded_Back.uasset")
