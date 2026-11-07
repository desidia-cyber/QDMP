#!/usr/bin/env python3
"""QueensDomain-WindowsNoEditor.pak reader (UE4 pak version 11, plaintext, uncompressed).

Solved layout
-------------
footer (last 4 KiB, magic 0x5A6F12E1)
    u32 magic | i32 version | i64 index_offset | i64 index_size
    u8[20] index_hash | u8 bEncryptedIndex | u8[16] EncryptionKeyGuid
    char[4][32] compression method names   (all zero -> nothing compressed)
    -> index_offset = 1_973_612_650, index_size = 529_218

primary index at index_offset (index_size bytes, exclusive)
    +0      FString  mount point                 "../../../"
    +14     i32      NumEntries                  44_092
    +18     u64      PathHashSeed
    +26     u8       bEncodedPakEntries          1
    +27     u8[87]   reserved (zero except two 40-byte FPakEntry-looking records)
    +114    blob     NumEntries x 12-byte encoded entries
                    -> 114 + 44092*12 = 529_218 == index_size

encoded entry (12 bytes at blob[loc])
    u32 offset      absolute pak offset of the entry HEADER
    u32 size        payload size in bytes
    u32 flags       0xE0000000 for every live entry, 0 for the one tombstone

At `offset` sits a 53-byte inline FPakEntry header:
    i64 0 | i64 size | i64 size | u32 compression_method(0) | u8[20] sha1 | u32 0 | u8 0
The payload is at offset+53, length `size`. sha1(payload) == the inline hash.

secondary indexes, laid out back to back after index_offset+index_size
    PathHashIndex        i32 count=44092, then count x (u64 hash, i32 blob_offset)
                         44092 entries, offsets 0,12,24,...,529092  (12-byte slots)
    PrunedDirectoryIndex i32 numDirs=287, then dirs                 (3625 files)
    FullDirectoryIndex   i32 numDirs=1401, then dirs                (44092 files)
                         dir = FString path; i32 n; n x (FString name; i32 blob_offset)
    then 17 bytes padding, then the footer.
"""
import os
import struct

PAK_PATH = (r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Content\Paks"
            r"\QueensDomain-WindowsNoEditor.pak")

FOOTER_MAGIC = 0x5A6F12E1
ENTRY_HEADER_SIZE = 53
PKG_MAGIC = b"\xc1\x83\x2a\x9e"


class PakError(Exception):
    pass


class Stream:
    """Byte reader over a file handle."""

    def __init__(self, f, base=0):
        self.f = f
        self.base = base

    def read(self, off, n):
        self.f.seek(self.base + off)
        return self.f.read(n)

    def i32(self, off):
        return struct.unpack("<i", self.read(off, 4))[0]

    def fstring(self, off):
        n = self.i32(off)
        off += 4
        if n == 0:
            return "", off
        if n < 0:
            n = -n
            raw = self.read(off, n)
            return raw.decode("utf-16-le", "replace").split("\x00")[0], off + n
        raw = self.read(off, n)
        return raw.split(b"\x00")[0].decode("latin-1"), off + n


class Pak:
    def __init__(self, path=PAK_PATH):
        self.path = path
        self.size = os.path.getsize(path)
        self.f = open(path, "rb")
        self.stream = Stream(self.f)
        self._read_footer()
        self._read_primary_index()
        self._read_path_hash_index()
        self._read_directory_indexes()
        self._build_entries()

    # ---- footer -------------------------------------------------------
    def _read_footer(self):
        tail_off = max(0, self.size - 4096)
        tail = self.stream.read(tail_off, self.size - tail_off)
        pos = tail.rfind(struct.pack("<I", FOOTER_MAGIC))
        if pos < 0:
            raise PakError("footer magic not found in last 4 KiB")
        self.footer_offset = tail_off + pos
        b = self.stream.read(self.footer_offset, 64)
        (self.version,) = struct.unpack("<i", b[4:8])
        (self.index_offset, self.index_size) = struct.unpack("<qq", b[8:24])
        self.index_hash = b[24:44]
        self.b_encrypted_index = b[44]
        self.encryption_key_guid = b[45:61]
        self.compression_methods = [b[61 + 32 * i: 61 + 32 * (i + 1)].split(b"\x00")[0].decode("latin-1")
                                    for i in range(4)]

    # ---- primary index ------------------------------------------------
    def _read_primary_index(self):
        s = self.stream
        o = self.index_offset
        self.mount_point, o = s.fstring(o)
        self.num_entries = s.i32(o); o += 4
        self.path_hash_seed = struct.unpack("<Q", s.read(o, 8))[0]; o += 8
        self.header_b_encoded = s.read(o, 1)[0]; o += 1
        self.header_reserved = s.read(o, 87); o += 87          # 114-byte header total
        self.blob_offset = self.index_offset + 114
        self.blob_len = self.num_entries * 12
        assert self.blob_offset + self.blob_len == self.index_offset + self.index_size, \
            "encoded-entry blob does not end at index_offset+index_size"
        self.blob = s.read(self.blob_offset, self.blob_len)

    # ---- path hash index ----------------------------------------------
    def _read_path_hash_index(self):
        base = self.index_offset + self.index_size
        s = self.stream
        count = s.i32(base)
        raw = s.read(base + 4, count * 12)
        self.path_hash_index = [struct.unpack("<Qi", raw[i * 12:i * 12 + 12]) for i in range(count)]
        self.path_hash_index_base = base
        self.path_hash_index_end = base + 4 + count * 12

    # ---- directory indexes ---------------------------------------------
    def _parse_directory_index(self, start):
        s = self.stream
        num_dirs = s.i32(start)
        if num_dirs < 0 or num_dirs > 1_000_000:
            raise PakError("bad numDirs %d at %d" % (num_dirs, start))
        o = start + 4
        dirs = []
        for _ in range(num_dirs):
            dname, o = s.fstring(o)
            n = s.i32(o); o += 4
            if n < 0 or n > 1_000_000:
                raise PakError("bad file count %d in %r" % (n, dname))
            files = []
            for _ in range(n):
                name, o = s.fstring(o)
                loc = s.i32(o); o += 4
                files.append((name, loc))
            dirs.append((dname, files))
        return dirs, o

    def _read_directory_indexes(self):
        self.pruned_dirs, self.pruned_end = self._parse_directory_index(self.path_hash_index_end)
        self.full_dirs, self.full_dir_end = self._parse_directory_index(self.pruned_end)
        self.footer_gap = self.footer_offset - self.full_dir_end

    # ---- entries ------------------------------------------------------
    def _build_entries(self):
        self.path_to_loc = {}
        for dname, files in self.full_dirs:
            for name, loc in files:
                self.path_to_loc[dname + name] = loc
        self.entries = {}
        for path, loc in self.path_to_loc.items():
            if loc < 0 or loc + 12 > self.blob_len:
                raise PakError("entry loc out of range for %r: %d" % (path, loc))
            off, size, flags = struct.unpack("<III", self.blob[loc:loc + 12])
            self.entries[path] = (off, size, flags)

    # ---- payloads -----------------------------------------------------
    def header(self, path):
        """Return the decoded 53-byte inline FPakEntry header as a dict."""
        off = self.entries[path][0]
        b = self.stream.read(off, ENTRY_HEADER_SIZE)
        if len(b) != ENTRY_HEADER_SIZE:
            raise PakError("short entry header for %r" % path)
        reserved, size1, size2 = struct.unpack("<qqq", b[0:24])
        (cmethod,) = struct.unpack("<I", b[24:28])
        return {
            "reserved": reserved,
            "size": size1,
            "uncompressed_size": size2,
            "compression_method": cmethod,
            "sha1": b[28:48],
            "tail": b[48:53],
        }

    def payload(self, path):
        off, size, _flags = self.entries[path]
        return self.stream.read(off + ENTRY_HEADER_SIZE, size)

    def size_of(self, path):
        return self.entries[path][1]

    def listdir(self, prefix):
        return sorted(p for p in self.entries if p.startswith(prefix))

    def close(self):
        self.f.close()

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.close()


# ---------------------------------------------------------------------------
# small helpers shared by the mining scripts
# ---------------------------------------------------------------------------

def is_package(path):
    return path.lower().endswith((".uasset", ".umap"))


def uasset_names(data):
    """Return the package name table of a .uasset/.umap payload.

    The name table is located from the fixed FPackageFileSummary prefix:
        u32 tag | i32 legacy_file_version | i32 legacy_ue3 | i32 ver_ue4
        i32 ver_licensee | i32 custom_version_count | i32 total_header_size
        FString folder | u32 package_flags | i32 name_count | i32 name_offset
    Each name is FString followed by u16 non-case-preserving + u16 case-preserving hash.
    """
    if data[:4] != PKG_MAGIC:
        raise PakError("not a package (bad magic)")
    o = 4
    (legacy,) = struct.unpack("<i", data[o:o + 4]); o += 4
    if legacy != -4:
        o += 4                                     # LegacyUE3Version
    o += 4                                         # FileVersionUE4
    o += 4                                         # FileVersionLicenseeUE4
    if legacy <= -2:
        (n_custom,) = struct.unpack("<i", data[o:o + 4]); o += 4
        if n_custom < 0 or o + n_custom * 20 > len(data):
            raise PakError("bad custom version count %d" % n_custom)
        o += n_custom * 20                         # FGuid(16) + i32
    if legacy < 0:
        (total_header_size,) = struct.unpack("<i", data[o:o + 4]); o += 4
    else:
        total_header_size = 0
    # FString folder
    (flen,) = struct.unpack("<i", data[o:o + 4]); o += 4
    o += abs(flen) * (2 if flen < 0 else 1)
    o += 4                                          # PackageFlags
    name_count, name_offset = struct.unpack("<ii", data[o:o + 8]); o += 8
    if name_offset <= 0 or name_offset >= len(data) or name_count < 0:
        raise PakError("bad name table loc count=%d off=%d len=%d"
                       % (name_count, name_offset, len(data)))
    p = name_offset
    names = []
    for _ in range(name_count):
        (n,) = struct.unpack("<i", data[p:p + 4]); p += 4
        if n == 0:
            names.append("")
            continue
        if n < 0:
            n = -n
            raw = data[p:p + n]; p += n
            names.append(raw.decode("utf-16-le", "replace").split("\x00")[0])
        else:
            raw = data[p:p + n]; p += n
            names.append(raw.split(b"\x00")[0].decode("latin-1"))
        p += 4                                     # two u16 hashes
    return {"names": names, "name_count": name_count, "name_offset": name_offset,
            "total_header_size": total_header_size, "legacy_version": legacy}


def main():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("--pak", default=PAK_PATH)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p_list = sub.add_parser("list")
    p_list.add_argument("prefix", nargs="?", default="")
    p_info = sub.add_parser("info")
    p_info.add_argument("path")
    p_get = sub.add_parser("get")
    p_get.add_argument("path")
    p_get.add_argument("out")
    p_verify = sub.add_parser("verify")
    p_verify.add_argument("--count", type=int, default=250)
    a = ap.parse_args()

    with Pak(a.pak) as pk:
        if a.cmd == "list":
            for p in pk.listdir(a.prefix):
                off, size, flags = pk.entries[p]
                print("%10d %10d %s" % (off, size, p))
        elif a.cmd == "info":
            off, size, flags = pk.entries[a.path]
            h = pk.header(a.path)
            print("path      ", a.path)
            print("offset    ", off, hex(off))
            print("payload at", off + ENTRY_HEADER_SIZE)
            print("size      ", size)
            print("flags     ", hex(flags))
            print("hdr.size  ", h["size"], "hdr.usize", h["uncompressed_size"],
                  "cmethod", h["compression_method"], "sha1", h["sha1"].hex())
        elif a.cmd == "get":
            with open(a.out, "wb") as g:
                g.write(pk.payload(a.path))
            print("wrote", a.out, pk.size_of(a.path), "bytes")
        elif a.cmd == "verify":
            import hashlib, random
            paths = sorted(pk.entries)
            random.seed(1234)
            sample = random.sample(paths, min(a.count, len(paths)))
            ok = 0
            magic_ok = magic_tot = 0
            for p in sample:
                d = pk.payload(p)
                if hashlib.sha1(d).digest() == pk.header(p)["sha1"]:
                    ok += 1
                if is_package(p):
                    magic_tot += 1
                    if d[:4] == PKG_MAGIC:
                        magic_ok += 1
            print("sampled %d: sha1 ok %d (%.2f%%)" % (len(sample), ok, 100.0 * ok / len(sample)))
            print("packages %d: magic ok %d" % (magic_tot, magic_ok))


if __name__ == "__main__":
    main()
