#!/usr/bin/env python3
"""Measure the FPakInfo footer of a UE4 pak by dumping its tail.

Nothing about the footer is assumed: the UE magic 0x5A6F12E1 is located and every
candidate is printed with its distance from EOF, so the real footer size falls out
of the file instead of being guessed from an engine version.
"""
import struct
import sys

PAK = r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Content\Paks\QueensDomain-WindowsNoEditor.pak"
MAGIC = b"\xe1\x12\x6f\x5a"


def dump_tail(path, n=320):
    with open(path, "rb") as fh:
        fh.seek(0, 2)
        size = fh.tell()
        fh.seek(max(0, size - n))
        tail = fh.read(n)
    base = size - len(tail)
    print("file size   : %d (0x%X)" % (size, size))
    print("tail window : %d bytes from 0x%X" % (len(tail), base))
    print()
    print("off       hex                                              ascii")
    for i in range(0, len(tail), 16):
        chunk = tail[i:i + 16]
        hexs = " ".join("%02x" % b for b in chunk)
        asc = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        print("%8d  %-47s  %s" % (base + i, hexs, asc))
    print()
    print("=== magic candidates ===")
    pos = tail.find(MAGIC)
    while pos >= 0:
        abs_off = base + pos
        ver = struct.unpack_from("<i", tail, pos + 4)[0]
        idx_off, idx_size = struct.unpack_from("<qq", tail, pos + 8)
        idx_hash = tail[pos + 24:pos + 44]
        print("at 0x%X (%d from EOF): version=%d index_offset=%d index_size=%d"
              % (abs_off, size - abs_off, ver, idx_off, idx_size))
        print("   index hash : %s" % idx_hash.hex())
        # Print the region after the hash so the flag bytes are visible.
        rest = tail[pos + 44:]
        print("   after hash : %s" % " ".join("%02x" % b for b in rest[:24]))
        print("   as text    : %r" % rest[:180])
        pos = tail.find(MAGIC, pos + 1)


if __name__ == "__main__":
    dump_tail(sys.argv[1] if len(sys.argv) > 1 else PAK)
