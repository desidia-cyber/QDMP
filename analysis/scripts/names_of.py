#!/usr/bin/env python3
"""Print the name table of a package, which for a Skeleton is its bone list."""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import queenspak  # noqa: E402
import classes    # noqa: E402

PAK = (r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Content\Paks"
       r"\QueensDomain-WindowsNoEditor.pak")


def main(paths):
    with queenspak.Pak(PAK) as pk:
        for target in paths:
            raw = next((r for r in pk.entries
                        if queenspak.game_path(pk.mount, r) == target), None)
            if raw is None:
                print("not found:", target)
                continue
            data = pk.read_file(raw)
            r = classes.R(data)
            s = classes.read_summary(r)
            names = classes.read_names(r, s["name_count"], s["name_offset"])
            print("\n=== %s (%d names) ===" % (target, len(names)))
            for n in names:
                print("   ", n)


if __name__ == "__main__":
    main(sys.argv[1:])
