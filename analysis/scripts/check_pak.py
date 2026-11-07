#!/usr/bin/env python3
"""Sanity check: open the pak, print format facts, and spot-check decoding."""
import os
import sys
from collections import Counter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import queenspak  # noqa: E402

PAK = r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Content\Paks\QueensDomain-WindowsNoEditor.pak"

with queenspak.Pak(PAK) as pk:
    print("pak file        : %s" % os.path.basename(pk.path))
    print("file size       : {:,}".format(pk.file_size))
    print("footer size     : %d bytes (distance from EOF)" % pk.info["footer_size"])
    print("footer offset   : {:,}".format(pk.info["file_offset"]))
    print("pak version     : %d" % pk.info["version"])
    print("mount point     : %r" % pk.mount)
    print("index offset    : {:,}".format(pk.info["index_offset"]))
    print("index size      : {:,}".format(pk.info["index_size"]))
    print("index hash      : %s" % pk.info["index_hash"])
    print("index encrypted : %s" % pk.info.get("index_encrypted"))
    print("index decode    : %s" % pk.index_encryption)
    print("index sha1 ok   : %s" % pk.index_sha1_matches)
    print("compression tbl : %r" % pk.methods)
    print("NumEntries      : %d" % pk.num_entries)
    print("dirs in FDI     : %d" % pk.n_dirs)
    print("FDI sha1 ok     : %s" % pk.fdi_sha1_matches)
    print("FilesNum        : %d" % pk.files_num)
    print("path hash seed  : 0x%X" % pk.path_hash_seed)
    print("entries decoded : %d" % len(pk.entries))
    print("skipped         : deleted=%d non-encoded=%d"
          % (pk.skipped_deleted, pk.skipped_non_encoded))
    print()
    print("methods   : %r" % Counter(e.get("method") for e in pk.entries.values()))
    print("encrypted : %r" % Counter(e.get("encrypted") for e in pk.entries.values()))
    print("errors    : %d" % sum(1 for e in pk.entries.values() if "error" in e))
    us = sum(e.get("uncompressed_size", 0) for e in pk.entries.values())
    cs = sum(e.get("compressed_size", 0) for e in pk.entries.values())
    print("sum uncompressed : {:,}".format(us))
    print("sum stored       : {:,}".format(cs))
    print("data region end  : {:,} (index starts {:,})"
          % (max((e.get("data_offset", 0) + e.get("compressed_size", 0))
                 for e in pk.entries.values()), pk.info["index_offset"]))
    print()
    print("--- 25 sample paths ---")
    for p in list(pk.entries)[:25]:
        e = pk.entries[p]
        print("  %10d  %s" % (e.get("uncompressed_size", 0), p))
