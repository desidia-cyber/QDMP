#!/usr/bin/env python3
"""Probe the Queens Domain GVAS save files.

Structural questions this answers, in order:
  1. Does the container match UE4's UGameplayStatics::SaveGameToSlot layout?
  2. Where does the opaque region begin (i.e. is the payload encrypted/compressed)?
  3. If compressed, can we inflate it and read the tagged-property stream?
"""
import os, struct, sys, zlib, collections

SAVES = r"C:\Users\ALVIBO\AppData\Local\QueensDomain\Saved\SaveGames"
FILES = ["System0.sav", "World0.sav", "Challenges"]


class R:
    def __init__(self, b, o=0):
        self.b, self.o = b, o

    def u8(self):
        v = self.b[self.o]; self.o += 1; return v

    def u32(self):
        v = struct.unpack_from("<I", self.b, self.o)[0]; self.o += 4; return v

    def i32(self):
        v = struct.unpack_from("<i", self.b, self.o)[0]; self.o += 4; return v

    def u64(self):
        v = struct.unpack_from("<Q", self.b, self.o)[0]; self.o += 8; return v

    def guid(self):
        v = self.b[self.o:self.o + 16]; self.o += 16; return v.hex()

    def fstr(self):
        n = self.i32()
        if n == 0:
            return ""
        if n < 0:  # utf16
            raw = self.b[self.o:self.o + (-n) * 2]; self.o += (-n) * 2
            s = raw.decode("utf-16-le", "replace").rstrip("\x00")
            return s
        raw = self.b[self.o:self.o + n]; self.o += n
        return raw.decode("latin-1").rstrip("\x00")

    def left(self):
        return len(self.b) - self.o


def parse_header(data):
    r = R(data)
    out = {}
    out["magic"] = r.b[0:4]
    r.o = 4
    out["savegame_file_version"] = r.u32()
    out["package_ue4_version"] = r.u32()
    # 4.27 writes license version too in some builds; record raw
    save = r.o
    out["custom_version_format"] = r.u32()
    fmt = out["custom_version_format"]
    try:
        if fmt in (0, 1, 2):
            n = r.u32()
            out["custom_version_count"] = n
            out["custom_versions"] = [(r.guid(), r.i32()) for _ in range(n)]
        elif fmt == 3:  # optimized
            n = r.u32()
            out["custom_version_count"] = n
            out["custom_versions_raw"] = data[r.o:r.o + n * 20]
            r.o += n * 20
    except Exception as e:
        out["cv_error"] = repr(e)
        r.o = save
    out["offset_after_customversions"] = r.o
    try:
        out["class_name"] = r.fstr()
        out["offset_after_classname"] = r.o
    except Exception as e:
        out["class_error"] = repr(e)
    return out, r


def entropy(b):
    if not b:
        return 0.0
    c = collections.Counter(b)
    import math
    n = len(b)
    return -sum(v / n * math.log2(v / n) for v in c.values())


def try_inflate(data):
    hits = []
    for off in range(0, min(len(data), 512)):
        for wbits, name in ((15, "zlib"), (-15, "raw"), (31, "gzip")):
            try:
                d = zlib.decompressobj(wbits)
                out = d.decompress(data[off:])
                if len(out) > 32 and d.eof:
                    hits.append((off, name, len(out)))
            except Exception:
                pass
    return hits


for f in FILES:
    p = os.path.join(SAVES, f)
    data = open(p, "rb").read()
    print("=" * 78)
    print(f"{f}  size={len(data)}")
    h, r = parse_header(data)
    for k, v in h.items():
        if k == "custom_versions":
            print(f"  {k}: {len(v)} entries")
            for g, ver in v:
                print(f"      {g} v{ver}")
        elif isinstance(v, bytes):
            print(f"  {k}: {v!r}")
        else:
            print(f"  {k}: {v}")
    print(f"  payload_offset={r.o} payload_len={len(data)-r.o}")
    pay = data[r.o:]
    print(f"  payload entropy={entropy(pay):.3f} bits/byte  first16={pay[:16].hex()}")
    asc = bytes(c if 32 <= c < 127 else 0 for c in pay)
    runs = [s for s in asc.split(b"\x00") if len(s) >= 4]
    print(f"  ascii runs in payload: {len(runs)}; sample={[s.decode() for s in runs[:15]]}")
    hits = try_inflate(data)
    print(f"  zlib hits: {hits[:6]}")
    # guess encryption: repeated 16-byte blocks?
    blocks = [pay[i:i + 16] for i in range(0, len(pay) - 15, 16)]
    print(f"  distinct 16B blocks {len(set(blocks))}/{len(blocks)}")
