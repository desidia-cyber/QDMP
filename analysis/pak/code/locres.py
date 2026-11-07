#!/usr/bin/env python3
"""Decode a QueensDomain .locres (modified UE4 locres) into its string catalogue.

Observed layout (both Game.locres and Engine.locres)
----------------------------------------------------
  +0x00  u32   magic 0x7574140E          (bytes 0E 14 74 75)
  +0x04  u8[13] IDENTICAL in every file: 67 4A 03 FC 4A 15 90 9D C3 37 7F 1B 03
                (0x67 is where stock UE would put the version byte; the other 12
                 bytes never change between files, so they are a fixed build stamp
                 rather than data. Stock locres has no such field.)
  +0x11  i64   string_table_offset       85689 in Game.locres / 862401 in Engine.locres
  +0x19  u32   string_count              2388 / 20149  (close to, but not equal to,
                                          the number of records actually parsed)
  +0x1D  u32   ?                         4 / 2173
  ...    then the namespace/key records, then the string table at string_table_offset

String table (this is the part that is definitely cracked)
----------------------------------------------------------
  repeat until EOF:  u32 refcount(1) ; FString text
  FString is i32 length + bytes; a NEGATIVE length means UTF-16LE, and the byte
  count is 2*|length|. The strings are stored in case-insensitive alphabetical order.

The namespace/key records before the string table use 32-hex-digit identifiers as
names; see localisation.md for what was and was not recovered from them.
"""
import os
import struct

PAK_READER = os.path.join(os.path.dirname(os.path.abspath(__file__)), "pakread.py")


def _fstr(data, o):
    (n,) = struct.unpack("<i", data[o:o + 4])
    o += 4
    if n == 0:
        return "", o
    if n < 0:
        nb = -n * 2
        return data[o:o + nb].decode("utf-16-le", "replace").split("\x00")[0], o + nb
    return data[o:o + n].split(b"\x00")[0].decode("latin-1"), o + n


def parse_table(data, start):
    """Parse (u32, FString) records from start until EOF. Returns list or None."""
    out = []
    o = start
    while o + 8 <= len(data):
        (ref,) = struct.unpack("<I", data[o:o + 4])
        (n,) = struct.unpack("<i", data[o + 4:o + 8])
        if n == 0:
            out.append((ref, ""))
            o += 8
            continue
        if n < 0:
            nb = -n * 2
            if o + 8 + nb > len(data):
                return None
            out.append((ref, data[o + 8:o + 8 + nb].decode("utf-16-le", "replace").split("\x00")[0]))
            o += 8 + nb
        else:
            if n > 65536 or o + 8 + n > len(data) or data[o + 8 + n - 1] != 0:
                return None
            out.append((ref, data[o + 8:o + 8 + n - 1].decode("latin-1")))
            o += 8 + n
    # allow up to 8 slack bytes at the end
    if len(data) - o > 8:
        return None
    return out


def decode(data):
    if data[:4].hex() != "0e147475":
        raise ValueError("not a locres (magic %s)" % data[:4].hex())
    (off,) = struct.unpack("<q", data[0x11:0x19])
    (count,) = struct.unpack("<I", data[0x19:0x1d])
    best = None
    for start in (off, off - 4, off - 8, off + 4):
        if 0 < start < len(data):
            recs = parse_table(data, start)
            if recs and (best is None or len(recs) > len(best[1])):
                best = (start, recs)
    if best is None:
        raise ValueError("could not locate string table")
    return {"header_offset": off, "header_count": count,
            "table_start": best[0], "records": best[1]}


def main():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("path")
    ap.add_argument("--tsv", default=None)
    a = ap.parse_args()
    data = open(a.path, "rb").read()
    r = decode(data)
    print("table_start %d  header said %d  header count %d  records %d"
          % (r["table_start"], r["header_offset"], r["header_count"], len(r["records"])))
    if a.tsv:
        with open(a.tsv, "w", encoding="utf-8") as g:
            for i, (ref, s) in enumerate(r["records"]):
                g.write("%d\t%d\t%s\n" % (i, ref, s.replace("\t", " ").replace("\r", "\\r").replace("\n", "\\n")))
        print("wrote", a.tsv)


if __name__ == "__main__":
    main()
