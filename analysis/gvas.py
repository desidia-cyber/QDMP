#!/usr/bin/env python3
"""Queens Domain save-file (UE4 GVAS SaveGame) reader.

Header, measured on the three real files in
%LOCALAPPDATA%\\QueensDomain\\Saved\\SaveGames:

    0x00  char[4]  "GVAS"
    0x04  int32    SaveGameFileVersion              = 2
    0x08  int32    PackageFileUE4Version            = 522
    0x0C  uint16   EngineVersion.Major/Minor/Patch  = 4 / 27 / 2
    0x12  int32    Changelist                       = 0
    0x16  FString-like (len includes the NUL): branch "++UE4+Release-4.27"
          int32    CustomVersionFormat              = 3 (Optimized)
          int32    CustomVersionCount               = 51
          count x (FGuid 16, int32 version)
          FString  SaveGameClassPath
          <tagged property stream, terminated by a "None" tag>
          uint32   0

Everything strings-len-wise in this format is len+1 (the terminator is
included in the count), which differs from stock UE4 FString serialization.
"""
import json, os, struct, sys

SAVES = r"C:\Users\ALVIBO\AppData\Local\QueensDomain\Saved\SaveGames"

NATIVE_FIXED = {  # native struct name -> fixed byte width, when known
    "Vector": 12, "Rotator": 12, "Vector2D": 8, "Vector4": 16, "Quat": 16,
    "IntPoint": 8, "IntVector": 12, "Guid": 16, "DateTime": 8, "Timespan": 8,
    "LinearColor": 16, "Color": 4, "Box": 25, "Box2D": 41, "Matrix": 64,
    "FrameNumber": 4, "FloatRange": 8, "Int32Range": 8,
}
SCALAR_FIXED = {"IntProperty": 4, "UInt32Property": 4, "Int64Property": 8,
                "UInt64Property": 8, "FloatProperty": 4, "DoubleProperty": 8,
                "BoolProperty": 1, "ByteProperty": 1}


class Bad(Exception):
    pass


class R:
    def __init__(self, b, o=0, end=None):
        self.b, self.o = b, o
        self.end = len(b) if end is None else end

    def need(self, n):
        if self.o + n > self.end:
            raise Bad(f"want {n} at {self.o}, end {self.end}")

    def u8(self):
        self.need(1); v = self.b[self.o]; self.o += 1; return v

    def i16(self):
        self.need(2); v = struct.unpack_from("<h", self.b, self.o)[0]; self.o += 2; return v

    def u16(self):
        self.need(2); v = struct.unpack_from("<H", self.b, self.o)[0]; self.o += 2; return v

    def i32(self):
        self.need(4); v = struct.unpack_from("<i", self.b, self.o)[0]; self.o += 4; return v

    def u32(self):
        self.need(4); v = struct.unpack_from("<I", self.b, self.o)[0]; self.o += 4; return v

    def i64(self):
        self.need(8); v = struct.unpack_from("<q", self.b, self.o)[0]; self.o += 8; return v

    def f32(self):
        self.need(4); v = struct.unpack_from("<f", self.b, self.o)[0]; self.o += 4; return v

    def f64(self):
        self.need(8); v = struct.unpack_from("<d", self.b, self.o)[0]; self.o += 8; return v

    def raw(self, n):
        self.need(n); v = self.b[self.o:self.o + n]; self.o += n; return v

    def fstr(self):
        n = self.i32()
        if n == 0:
            return ""
        if n > 0:
            if n > 4096:
                raise Bad(f"fstr len {n} at {self.o}")
            return self.raw(n).decode("latin-1").rstrip("\x00")
        return self.raw(-n * 2).decode("utf-16-le", "replace").rstrip("\x00")


def parse_header(data):
    r = R(data, 4)
    ver = r.i32()
    pkg = r.i32()
    major, minor, patch = r.u16(), r.u16(), r.u16()
    cl = r.i32()
    blen = r.i32()
    branch = r.raw(blen).decode("latin-1").rstrip("\x00")
    fmt = r.i32()
    ncv = r.i32()
    cvs = [(r.raw(16).hex(), r.i32()) for _ in range(ncv)]
    cls = r.fstr()
    return dict(savegame_version=ver, package_ue4_version=pkg,
                engine_version=f"{major}.{minor}.{patch}", changelist=cl,
                branch=branch, custom_version_format=fmt,
                custom_version_count=ncv, class_path=cls,
                stream_offset=r.o, custom_versions=cvs)


def read_tag(r):
    name = r.fstr()
    if name == "None":
        return None
    typ = r.fstr()
    size = r.i32()
    idx = r.i32()
    t = dict(name=name, type=typ, size=size, index=idx)
    if typ == "StructProperty":
        t["struct"] = r.fstr()
        t["guid"] = r.raw(16).hex()
    elif typ == "BoolProperty":
        t["bool"] = r.u8()
    elif typ in ("ByteProperty", "EnumProperty"):
        t["enum"] = r.fstr()
    elif typ in ("ArrayProperty", "SetProperty"):
        t["inner"] = r.fstr()
    elif typ == "MapProperty":
        t["inner"] = r.fstr()
        t["value_type"] = r.fstr()
    if r.u8():
        t["propguid"] = r.raw(16).hex()
    return t


def parse_native(r, name, size):
    end = r.o + size
    if name == "Vector":
        return dict(x=round(r.f32(), 3), y=round(r.f32(), 3), z=round(r.f32(), 3))
    if name == "Rotator":
        return dict(pitch=round(r.f32(), 3), yaw=round(r.f32(), 3), roll=round(r.f32(), 3))
    if name == "Transform":
        rot = r.raw(16).hex(); tr = r.raw(12); sc = r.raw(12)
        return dict(rotation=rot, translation=tr.hex(), scale=sc.hex())
    if name == "Guid":
        return {"guid": r.raw(size).hex()}
    if name in ("DateTime", "Timespan"):
        return {"ticks": r.i64()}
    if name == "IntPoint":
        return dict(x=r.i32(), y=r.i32())
    if name == "IntVector":
        return dict(x=r.i32(), y=r.i32(), z=r.i32())
    if name == "LinearColor":
        return dict(r=round(r.f32(), 3), g=round(r.f32(), 3), b=round(r.f32(), 3), a=round(r.f32(), 3))
    if name == "Color":
        return dict(b=r.u8(), g=r.u8(), r=r.u8(), a=r.u8())
    if name == "Vector2D":
        return dict(x=round(r.f32(), 3), y=round(r.f32(), 3))
    r.o = end
    return {"__raw_size": size}


def parse_struct(r, size, sname):
    """Blueprint (user-defined) structs are a nested tagged stream; native ones
    are fixed-width binary. This game declares its own USTRUCT named
    `Transform`, so tagged-parse with an exact-fit requirement wins over the
    name, and the native table is only the fallback."""
    end = r.o + size
    save = r.o
    try:
        props = parse_tagged(r, end)
        if r.o == end:
            return {"__struct": sname, "props": props}
    except (Bad, struct.error):
        pass
    r.o = save
    if sname in NATIVE_FIXED and NATIVE_FIXED[sname] == size:
        return parse_native(r, sname, size)
    r.o = end
    return {"__opaque_struct": sname, "__size": size}


def fixed_width(typ, tag=None):
    if typ in SCALAR_FIXED:
        return SCALAR_FIXED[typ]
    if typ == "StructProperty" and tag and tag.get("struct") in NATIVE_FIXED:
        return NATIVE_FIXED[tag["struct"]]
    if typ in ("IntProperty", "FloatProperty"):
        return 4
    return None


def parse_elem(r, typ, tag, end=None):
    if typ == "IntProperty":
        return r.i32()
    if typ == "Int64Property":
        return r.i64()
    if typ == "UInt32Property":
        return r.u32()
    if typ == "FloatProperty":
        return round(r.f32(), 6)
    if typ == "DoubleProperty":
        return r.f64()
    if typ == "BoolProperty":
        return bool(r.u8())
    if typ == "ByteProperty":
        return r.u8()
    if typ in ("NameProperty", "StrProperty", "ObjectProperty", "ClassProperty",
               "InterfaceProperty", "SoftObjectProperty", "SoftClassProperty"):
        return r.fstr()
    if typ == "StructProperty":
        t = read_tag(r)
        if t is None:
            raise Bad("no tag in struct elem")
        return parse_value(r, t)
    raise Bad(f"elem type {typ}")


def parse_container(r, inner, tag, count):
    """count elements of `inner`; `tag` describes the inner struct when used."""
    out = []
    for i in range(count):
        try:
            out.append(parse_elem(r, inner, tag))
        except Bad:
            out.append(None)
            raise
    return out


def parse_value(r, t):
    typ, size = t["type"], t["size"]
    end = r.o + size
    if typ == "BoolProperty":
        return bool(t.get("bool"))
    if typ == "IntProperty":
        return r.i32()
    if typ == "Int64Property":
        return r.i64()
    if typ == "UInt32Property":
        return r.u32()
    if typ == "UInt64Property":
        return r.i64()
    if typ == "FloatProperty":
        return round(r.f32(), 6)
    if typ == "DoubleProperty":
        return r.f64()
    if typ in ("StrProperty", "NameProperty", "ObjectProperty", "ClassProperty",
               "InterfaceProperty", "SoftObjectProperty", "SoftClassProperty"):
        return r.fstr()
    if typ in ("ByteProperty", "EnumProperty"):
        if size == 1:
            return r.u8()
        return r.fstr()
    if typ == "StructProperty":
        return parse_struct(r, size, t.get("struct", ""))
    if typ in ("ArrayProperty", "SetProperty"):
        nrem = r.i32() if typ == "SetProperty" else 0
        n = r.i32()
        if n < 0 or n > 5_000_000:
            raise Bad(f"container count {n}")
        inner = t["inner"]
        if inner == "StructProperty":
            it = read_tag(r)
            if it is None:
                return []
            body_end = it["size"] + r.o
            elems = []
            for _ in range(n):
                save = r.o
                try:
                    elems.append(parse_tagged(r, body_end))
                except Bad:
                    r.o = save
                    break
                if r.o > body_end:
                    r.o = save
                    break
            if r.o == body_end and len(elems) == n:
                return {"__struct": it.get("struct"), "items": elems}
            # fall back to fixed-width elements
            r.o = body_end - it["size"]
            w = it["size"] // n if n else 0
            if n and it["size"] % n == 0 and w in (4, 8, 12, 16):
                r.o = body_end
                return {"__struct": it.get("struct"), "__width": w, "__count": n}
            r.o = body_end
            return {"__struct": it.get("struct"), "__count": n, "__bytes": it["size"]}
        return parse_container(r, inner, None, n)
    if typ == "MapProperty":
        nrem = r.i32()
        n = r.i32()
        if n < 0 or n > 1_000_000:
            raise Bad(f"map count {n}")
        inner, vtype = t["inner"], t["value_type"]
        out = {}
        save = r.o
        try:
            for _ in range(n):
                k = parse_elem(r, inner, None)
                v = parse_elem(r, vtype, None)
                out[jkey(k)] = v
            if r.o == end:
                return out
        except Bad:
            pass
        r.o = save
        # binary-struct key fallback: derive width from the declared size
        vw = fixed_width(vtype)
        if vw is not None and n:
            kw = (size - 8 - vw * n) // n
            if kw > 0:
                out = {}
                for _ in range(n):
                    k = r.raw(kw).hex()
                    out[k] = parse_elem(r, vtype, None)
                return out
        r.o = end
        return {"__unparsed_map": n, "__inner": inner, "__value": vtype}
    r.o = end
    return {"__unparsed": typ, "__size": size}


def jkey(k):
    if isinstance(k, (str, int, float, bool)):
        return k
    return json.dumps(k, sort_keys=True)


def parse_tagged(r, end=None):
    if end is None:
        end = r.end
    props = {}
    while r.o < end:
        start = r.o
        t = read_tag(r)
        if t is None:
            break
        try:
            props[t["name"]] = parse_value(r, t)
        except (Bad, struct.error) as e:
            r.o = start + (r.o - start)
            raise Bad(f"{t['name']}({t['type']}): {e}") from e
        if r.o > end:
            raise Bad(f"overran end at {t['name']}")
    return props


def load(path, fn=None):
    data = open(path, "rb").read()
    h = parse_header(data)
    r = R(data, h["stream_offset"], len(data))
    h["props"] = parse_tagged(r)
    h["stream_end"] = r.o
    h["file_size"] = len(data)
    h["trailing"] = len(data) - r.o
    if fn:
        json.dump(h, open(fn, "w"), indent=1, default=str)
    return h


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    targets = args or ["System0.sav", "Challenges", "World0.sav"]
    for t in targets:
        p = t if os.path.isabs(t) else os.path.join(SAVES, t)
        print("=" * 78)
        print(os.path.basename(p), os.path.getsize(p))
        try:
            h = load(p)
            print("class:", h["class_path"], "| stream_end", h["stream_end"],
                  "| trailing", h["trailing"])
            print(json.dumps(h["props"], indent=1)[:60000])
        except Exception:
            import traceback; traceback.print_exc()
