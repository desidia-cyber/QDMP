#!/usr/bin/env python3
"""List every entry in the QueensDomain pak (UE4 pak v11) -> paths JSON."""
import json
import struct
import sys

PAK = r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Content\Paks\QueensDomain-WindowsNoEditor.pak"
OUT = r"G:\Freespace\Queens_Domain\analysis\scratch\pak_paths.json"


class C:
    def __init__(self, b, o=0):
        self.b = b
        self.o = o

    def i32(self):
        v, = struct.unpack_from("<i", self.b, self.o)
        self.o += 4
        return v

    def u32(self):
        v, = struct.unpack_from("<I", self.b, self.o)
        self.o += 4
        return v

    def i64(self):
        v, = struct.unpack_from("<q", self.b, self.o)
        self.o += 8
        return v

    def u64(self):
        v, = struct.unpack_from("<Q", self.b, self.o)
        self.o += 8
        return v

    def fstr(self):
        n = self.i32()
        if n == 0:
            return ""
        if n < 0:
            raw = self.b[self.o:self.o + (-n) * 2]
            self.o += (-n) * 2
            return raw.decode("utf-16-le").rstrip("\0")
        raw = self.b[self.o:self.o + n]
        self.o += n
        return raw.decode("latin1").rstrip("\0")

    def bytes_(self, n):
        v = self.b[self.o:self.o + n]
        self.o += n
        return v


def decode_entry(idx_bytes, off):
    c = C(idx_bytes, off)
    v = c.u32()
    blocksize = (v & 0x3F) << 11
    blockcount = (v >> 6) & 0xFFFF
    encrypted = bool((v >> 22) & 1)
    cmethod = (v >> 23) & 0x3F
    if blocksize == 0x3F << 11:
        blocksize = c.u32()
    offset = c.u32() if (v >> 31) & 1 else c.u64()
    usize = c.u32() if (v >> 30) & 1 else c.u64()
    csize = 0
    if cmethod != 0:
        csize = c.u32() if (v >> 29) & 1 else c.u64()
    if blockcount:
        for _ in range(blockcount):
            c.u32()
    return {"off": offset, "usize": usize, "compressed": bool(cmethod),
            "cmethod": cmethod, "enc": encrypted, "blocks": blockcount}


def main():
    f = open(PAK, "rb")
    f.seek(0, 2)
    size = f.tell()
    f.seek(size - 204)
    foot = f.read(204)
    assert foot[:4] == b"\xe1\x12\x6f\x5a"
    ver, = struct.unpack_from("<I", foot, 4)
    idx_off, idx_size = struct.unpack_from("<qq", foot, 8)
    print("pak version %d, index 0x%X + %d" % (ver, idx_off, idx_size))
    f.seek(idx_off)
    idx = f.read(idx_size)

    c = C(idx)
    mp = c.fstr()
    n = c.i32()
    seed = c.u64()
    print("mount=%r entries=%d seed=0x%X" % (mp, n, seed))

    pfi = c.i32()
    if pfi:
        poff, psize = c.i64(), c.i64()
        c.bytes_(20)
        print("path hash index: off=0x%X size=%d" % (poff, psize))
    fdi = c.i32()
    foff = fsize = None
    if fdi:
        foff, fsize = c.i64(), c.i64()
        c.bytes_(20)
        print("full dir index : off=0x%X size=%d" % (foff, fsize))
    enc_size = c.i32()
    enc_off = c.o
    enc = idx[enc_off:enc_off + enc_size]
    print("encoded entries: %d bytes" % enc_size)

    paths = {}
    if foff is not None:
        f.seek(foff)
        fd = f.read(fsize)
        d = C(fd)
        ndirs = d.i32()
        print("num dirs: %d" % ndirs)
        for _ in range(ndirs):
            dirname = d.fstr()
            nfiles = d.i32()
            for _ in range(nfiles):
                name = d.fstr()
                eoff = d.i32()
                full = (dirname.rstrip("/") + "/" + name) if dirname else name
                if full.startswith("/"):
                    full = full[1:]
                try:
                    paths[full] = decode_entry(enc, eoff)
                except Exception as exc:
                    paths[full] = {"error": str(exc)}
    print("parsed paths: %d" % len(paths))
    json.dump({"mount": mp, "entries": paths}, open(OUT, "w", encoding="utf-8"), indent=0)
    print("wrote", OUT)

    # quick sanity: first 10
    for p in list(sorted(paths))[:10]:
        print("  ", p, paths[p])
    # sanity on offsets
    bad = [p for p, e in paths.items() if e.get("off", 0) <= 0 or e.get("off", 0) >= size]
    print("entries with implausible offset: %d" % len(bad))
    comp = sum(1 for e in paths.values() if e.get("compressed"))
    encd = sum(1 for e in paths.values() if e.get("enc"))
    print("compressed entries: %d   encrypted entries: %d" % (comp, encd))


main()
