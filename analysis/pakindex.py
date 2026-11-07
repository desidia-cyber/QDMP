#!/usr/bin/env python3
"""Read a UE4 (version 11) .pak index and print every stored asset path.

Header/footer as observed on QueensDomain-WindowsNoEditor.pak:
    footer (at EOF-...):  u32 magic 0x5A6F12E1, i32 version=11,
                          i64 indexOffset, i64 indexSize,
                          u8[20] hash, then v11 extras
The index itself:
    FString MountPoint, i32 NumEntries, then per entry:
      FString Filename, FPakEntry
"""
import os, struct, sys

PAK = (r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Content\Paks"
       r"\QueensDomain-WindowsNoEditor.pak")
MAGIC = 0x5A6F12E1


class Bad(Exception):
    pass


class R:
    def __init__(self, b, o=0):
        self.b, self.o = b, o

    def take(self, n):
        if self.o + n > len(self.b):
            raise Bad(f"want {n} at {self.o}/{len(self.b)}")
        v = self.b[self.o:self.o + n]
        self.o += n
        return v

    def u8(self):
        return self.take(1)[0]

    def i32(self):
        return struct.unpack("<i", self.take(4))[0]

    def u32(self):
        return struct.unpack("<I", self.take(4))[0]

    def i64(self):
        return struct.unpack("<q", self.take(8))[0]

    def u64(self):
        return struct.unpack("<Q", self.take(8))[0]

    def fstr(self):
        n = self.i32()
        if n == 0:
            return ""
        if n < 0:
            s = self.take(-n).decode("latin-1")
            return s.split("\x00")[0]
        return self.take(n).split(b"\x00")[0].decode("latin-1")


def read_footer(f, size):
    f.seek(max(0, size - 4096))
    tail = f.read()
    pos = tail.rfind(struct.pack("<I", MAGIC))
    if pos < 0:
        raise Bad("no pak magic in last 4 KiB")
    r = R(tail, pos + 4)
    ver = r.i32()
    idx_off = r.i64()
    idx_size = r.i64()
    return ver, idx_off, idx_size


def parse_entry(r, ver):
    off = r.i64()
    size_ = r.i64()
    usize = r.i64()
    cmeth = r.u32()
    e = dict(offset=off, size=size_, usize=usize, compression=cmeth)
    e["hash"] = r.take(20).hex()
    if cmeth != 0:
        nb = r.i32()
        if nb < 0 or nb > 100000:
            raise Bad(f"bad block count {nb}")
        blocks = [(r.u64(), r.u64()) for _ in range(nb)]
        e["blocks"] = blocks
    e["flags"] = r.u8() if ver >= 8 else 0
    return e


def main():
    size = os.path.getsize(PAK)
    with open(PAK, "rb") as f:
        ver, idx_off, idx_size = read_footer(f, size)
        print(f"pak {size:,} bytes  version={ver}  index@{idx_off:,} size={idx_size:,}")
        f.seek(idx_off)
        blob = f.read(idx_size)
    r = R(blob)
    mount = r.fstr()
    n = r.i32()
    print(f"mount point {mount!r}  entries {n:,}")
    entries = []
    for i in range(n):
        fn = r.fstr()
        try:
            e = parse_entry(r, ver)
        except Bad as ex:
            print(f"entry {i} fail at {r.o}: {ex}; last name {fn!r}")
            break
        entries.append((fn, e))
    print(f"parsed {len(entries):,} entries, consumed {r.o:,}/{len(blob):,}")
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "pak_files.txt")
    with open(out, "w", encoding="utf-8") as g:
        for fn, e in entries:
            g.write(f"{fn}\t{e['size']}\t{e['usize']}\t{1 if e['flags'] & 1 else 0}\n")
    print("wrote", out)


if __name__ == "__main__":
    main()
