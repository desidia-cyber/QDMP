#!/usr/bin/env python3
"""Dump the full Queens Domain pak file listing to pak-files.json.

Every field comes from the pak's own directory index / EncodedPakEntries, so
there is no guessing here: path, stored size, uncompressed size, compression
method, per-entry encryption flag and byte offset.
"""
import json
import os
import sys
from collections import Counter

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import queenspak  # noqa: E402

PAK = (r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Content\Paks"
       r"\QueensDomain-WindowsNoEditor.pak")
OUTDIR = os.path.dirname(HERE)
OUT = os.path.join(OUTDIR, "pak-files.json")


def main():
    with queenspak.Pak(PAK) as pk:
        files = {}
        for raw, e in pk.entries.items():
            gp = queenspak.game_path(pk.mount, raw)
            files[gp] = {
                "size": e.get("uncompressed_size"),
                "stored": e.get("compressed_size"),
                "method": e.get("method"),
                "encrypted": e.get("encrypted"),
                "offset": e.get("offset"),
            }

        doc = {
            "pak": os.path.basename(PAK),
            "pak_size": pk.file_size,
            "pak_version": pk.info["version"],
            "mount_point": pk.mount,
            "index_offset": pk.info["index_offset"],
            "index_size": pk.info["index_size"],
            "index_hash": pk.info["index_hash"],
            "index_encrypted": False,
            "footer_size": pk.info["footer_size"],
            "compression_methods": [m for m in pk.methods if m],
            "num_entries": pk.num_entries,
            "num_dirs": pk.n_dirs,
            "entries_decoded": len(files),
            "sum_uncompressed": sum(v["size"] or 0 for v in files.values()),
            "files": dict(sorted(files.items())),
        }

    with open(OUT, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(doc, fh, indent=0, sort_keys=False)
        fh.write("\n")
    print("wrote %s (%.1f MB, %d entries)"
          % (OUT, os.path.getsize(OUT) / 1e6, len(doc["files"])))

    ext = Counter()
    for p, v in doc["files"].items():
        base = p.rsplit("/", 1)[-1]
        ext["." + base.rsplit(".", 1)[-1].lower() if "." in base else "<none>"] += 1
    print("\nextension histogram:")
    for k, v in ext.most_common():
        print("  %-12s %6d" % (k, v))

    roots = Counter()
    for p in doc["files"]:
        parts = p.split("/")
        if p.startswith("/Game/"):
            top = "/Game/" + (parts[2] if len(parts) > 2 else "")
        elif p.startswith("/Engine/"):
            top = "/Engine/" + (parts[2] if len(parts) > 2 else "")
        elif p.startswith("/Plugin/"):
            top = "/Plugin/" + "/".join(parts[2:4])
        else:
            top = parts[0]
        roots[top] += 1
    print("\ntop-level roots:")
    for k, v in sorted(roots.items(), key=lambda kv: -kv[1]):
        print("  %-55s %6d" % (k, v))


if __name__ == "__main__":
    main()
