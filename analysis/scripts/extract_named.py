#!/usr/bin/env python3
"""Extract files from the pak to a local output directory (read-only on the pak)."""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import queenspak  # noqa: E402

PAK = (r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Content\Paks"
       r"\QueensDomain-WindowsNoEditor.pak")
OUT = os.path.join(os.path.dirname(HERE), "data", "extracted")


def main(wanted):
    os.makedirs(OUT, exist_ok=True)
    with queenspak.Pak(PAK) as pk:
        index = {}
        for raw in pk.entries:
            index[queenspak.game_path(pk.mount, raw)] = raw
        for gp in wanted:
            raw = index.get(gp)
            if raw is None:
                print("MISSING", gp)
                continue
            data = pk.read_file(raw)
            dest = os.path.join(OUT, gp.strip("/").replace("/", "__"))
            with open(dest, "wb") as fh:
                fh.write(data)
            print("%10d  %s" % (len(data), dest))


if __name__ == "__main__":
    main(sys.argv[1:])
