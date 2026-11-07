#!/usr/bin/env python3
"""qdsave.py -- read/write Queen's Domain save files (FS_SaveSlot_C, UE4 GVAS).

WHY THIS EXISTS
---------------
The co-op mod mirrors one small integer map (`NamedSaveData`) between two
machines. This is the file-level half of that: inspect a save, extract the flag
bank, and apply a host-authoritative flag set back into it.

THE SAFETY RULE, AND WHY IT IS THE FIRST THING IN THIS FILE
-----------------------------------------------------------
The DollsMP predecessor (`dnsaveedit.py`) once ignored its `--root` argument and
edited the operator's real save. Everything here is therefore built around a
mandatory root:

  * `--root` is required for every command, including reads, and it is echoed.
  * Every path argument is resolved (symlinks/junctions included) and refused
    with exit code 2 unless it lands inside the root.
  * Writes are confined to the resolved path itself. Nothing else is created
    outside the root.

`selftest` is the one exception: it fabricates its own temporary root, because
its job is to prove the guard works rather than to operate on operator data.

CONTAINER (measured on the three sample saves in
`analysis/save-backup-before-live/`; the layout matches `analysis/gvas.py`):

    0x00  char[4]  "GVAS"
    0x04  int32    SaveGameFileVersion              = 2
    0x08  int32    PackageFileUE4Version            = 522
    0x0C  uint16   EngineVersion.Major/Minor/Patch  = 4 / 27 / 2
    0x12  int32    Changelist                       = 0
    ...   FString  branch ("++UE4+Release-4.27")
          int32    CustomVersionFormat              = 3 (Optimized)
          int32    CustomVersionCount               = 51
          count x (FGuid 16, int32 version)
          FString  SaveGameClassPath
          <tagged property stream, terminated by a tag named "None">
          uint32   0

Layout traps, both confirmed by parse-and-exact-fit against the samples:
  * Every FString length INCLUDES the terminating NUL, which differs from stock
    UE4 FString serialization. A positive length is single-byte (latin-1 here),
    a negative length is UTF-16 with `-n` code units.
  * There is one `hasGuid` byte between the type-specific tag fields and the
    value. For BoolProperty the boolean byte is a TAG field (like a struct's
    name), so it precedes `hasGuid` and the value region is empty; for every
    other numeric type the value follows `hasGuid`.
  * Array/Set/Map values begin with TWO int32s (a header int, then the element
    count). Set and Map also write a `numKeysToRemove` int first.
Getting any of those wrong desynchronises the stream silently, which is why
every decode below is a bounded exact fit: a property must consume exactly its
declared `size` bytes, and a container must consume exactly its value region.

WRITE STRATEGY
--------------
Only `NamedSaveData` is ever rewritten. Its value region is regenerated and
spliced back; every other byte in the file is copied through verbatim. Because
`NamedSaveData` is a top-level property, no enclosing size field depends on it,
so the only other byte that changes is that property's own `size` field. That is
what makes a no-op apply produce a byte-identical file (see `selftest`).

CO-OP RULE: MONOTONIC FLAGS
---------------------------
Story facts and counters only ever go up in this game. `apply-flags` therefore
refuses to lower an existing value, reports the refusal, and writes nothing for
that key. New keys are additions and are accepted; unchanged keys are no-ops.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import uuid

MAGIC = b"GVAS"
MAX_PROPERTIES = 100_000
MAX_CONTAINER = 1_000_000

# Sample saves used by `selftest`. They are the backup taken before the live
# session (`analysis/save-backup-before-live/`), never the live profile.
SAMPLE_NAMES = ("World0.sav", "System0.sav", "Challenges")


class SaveError(Exception):
    """The bytes do not match the format this module understands."""


# ---------------------------------------------------------------------------
# byte cursors
# ---------------------------------------------------------------------------

def _need(data: bytes, pos: int, n: int, end: int | None = None) -> None:
    limit = len(data) if end is None else end
    if n < 0 or pos < 0 or pos + n > limit:
        raise SaveError(
            "read past end at 0x%X: wanted %d bytes, %d available"
            % (pos, n, limit - pos)
        )


def _u8(data: bytes, pos: int, end: int | None = None) -> tuple[int, int]:
    _need(data, pos, 1, end)
    return data[pos], pos + 1


def _i32(data: bytes, pos: int, end: int | None = None) -> tuple[int, int]:
    _need(data, pos, 4, end)
    return struct.unpack_from("<i", data, pos)[0], pos + 4


def _u32(data: bytes, pos: int, end: int | None = None) -> tuple[int, int]:
    _need(data, pos, 4, end)
    return struct.unpack_from("<I", data, pos)[0], pos + 4


def _take(data: bytes, pos: int, n: int, end: int | None = None) -> tuple[bytes, int]:
    _need(data, pos, n, end)
    return data[pos:pos + n], pos + n


def _fstr(data: bytes, pos: int, end: int | None = None) -> tuple[str, int]:
    """Read one UE FString. The stored length includes the trailing NUL."""
    n, pos = _i32(data, pos, end)
    if n == 0:
        return "", pos
    if n > 0:
        if n > 65536:
            raise SaveError("implausible FString length %d at 0x%X" % (n, pos - 4))
        raw, pos = _take(data, pos, n, end)
        return raw.rstrip(b"\x00").decode("latin-1"), pos
    if n < 0:
        if -n > 65536:
            raise SaveError("implausible FString length %d at 0x%X" % (n, pos - 4))
        raw, pos = _take(data, pos, -n * 2, end)
        return raw.decode("utf-16-le", "replace").rstrip("\x00"), pos
    raise SaveError("unreachable")


def _enc_fstr(s: str) -> bytes:
    """Encode an FString the way this game writes it (length includes NUL)."""
    try:
        body = s.encode("latin-1")
    except UnicodeEncodeError:
        raise SaveError("flag name %r is not encodable as latin-1" % s) from None
    return struct.pack("<i", len(body) + 1) + body + b"\x00"


# ---------------------------------------------------------------------------
# header
# ---------------------------------------------------------------------------

def parse_header(data: bytes) -> dict:
    if len(data) < 32 or data[:4] != MAGIC:
        raise SaveError("not a GVAS save (bad magic %r)" % (data[:4],))
    pos = 4
    ver, pos = _i32(data, pos)
    pkg, pos = _i32(data, pos)
    major, pos = _u16(data, pos)
    minor, pos = _u16(data, pos)
    patch, pos = _u16(data, pos)
    changelist, pos = _i32(data, pos)
    branch, pos = _fstr(data, pos)
    cv_format, pos = _i32(data, pos)
    cv_count, pos = _i32(data, pos)
    if cv_count < 0 or cv_count > 4096:
        raise SaveError("implausible custom-version count %d" % cv_count)
    custom = []
    for _ in range(cv_count):
        g, pos = _take(data, pos, 16)
        v, pos = _i32(data, pos)
        custom.append([g.hex(), v])
    class_path, pos = _fstr(data, pos)
    return {
        "savegame_version": ver,
        "package_ue4_version": pkg,
        "engine_version": "%d.%d.%d" % (major, minor, patch),
        "changelist": changelist,
        "branch": branch,
        "custom_version_format": cv_format,
        "custom_version_count": cv_count,
        "custom_versions": custom,
        "class_path": class_path,
        "stream_offset": pos,
    }


def _u16(data: bytes, pos: int, end: int | None = None) -> tuple[int, int]:
    _need(data, pos, 2, end)
    return struct.unpack_from("<H", data, pos)[0], pos + 2


# ---------------------------------------------------------------------------
# tagged properties
# ---------------------------------------------------------------------------

class Prop:
    """One top-level tagged property, with the byte range of its tag and value."""

    __slots__ = (
        "name", "type", "size", "array_index",
        "struct_name", "struct_guid", "bool_val", "enum_name",
        "inner_type", "value_type", "has_guid", "guid",
        "size_offset", "value_offset", "value_len",
    )

    def __init__(self, name: str, type_: str):
        self.name = name
        self.type = type_
        self.size = 0
        self.array_index = 0
        self.struct_name = ""
        self.struct_guid = ""
        self.bool_val = 0
        self.enum_name = ""
        self.inner_type = ""
        self.value_type = ""
        self.has_guid = 0
        self.guid = ""
        self.size_offset = 0
        self.value_offset = 0
        self.value_len = 0

    @property
    def meta(self) -> dict:
        m = {"type": self.type}
        if self.struct_name:
            m["struct"] = self.struct_name
        if self.inner_type:
            m["inner"] = self.inner_type
        if self.value_type:
            m["value_type"] = self.value_type
        m["size"] = self.size
        return m

    def __repr__(self) -> str:
        return "<Prop %s %s size=%d>" % (self.name, self.type, self.size)


def _read_tag(data: bytes, pos: int, end: int) -> tuple[Prop | None, int]:
    """Read one tag. Returns (None, pos_after_terminator) at end of stream."""
    name, pos = _fstr(data, pos, end)
    if name == "None":
        return None, pos
    if name == "":
        raise SaveError("empty property name at 0x%X" % (pos - 4))
    typ, pos = _fstr(data, pos, end)
    if not typ.endswith("Property"):
        raise SaveError("property %r has no type at 0x%X (got %r)" % (name, pos, typ))
    prop = Prop(name, typ)
    prop.size_offset = pos
    prop.size, pos = _i32(data, pos, end)
    prop.array_index, pos = _i32(data, pos, end)
    if prop.size < 0:
        raise SaveError("negative size for %r" % name)

    if typ == "StructProperty":
        prop.struct_name, pos = _fstr(data, pos, end)
        raw, pos = _take(data, pos, 16, end)
        prop.struct_guid = raw.hex()
    elif typ == "BoolProperty":
        prop.bool_val, pos = _u8(data, pos, end)
    elif typ in ("ByteProperty", "EnumProperty"):
        prop.enum_name, pos = _fstr(data, pos, end)
    elif typ in ("ArrayProperty", "SetProperty"):
        prop.inner_type, pos = _fstr(data, pos, end)
    elif typ == "MapProperty":
        prop.inner_type, pos = _fstr(data, pos, end)
        prop.value_type, pos = _fstr(data, pos, end)

    prop.has_guid, pos = _u8(data, pos, end)
    if prop.has_guid:
        raw, pos = _take(data, pos, 16, end)
        prop.guid = raw.hex()

    prop.value_offset = pos
    prop.value_len = 0 if typ == "BoolProperty" else prop.size
    pos += prop.value_len
    _need(data, pos, 0, end)
    return prop, pos


def parse_properties(data: bytes, stream_offset: int) -> tuple[list[Prop], int]:
    """Parse the top-level property stream. Returns (props, offset after None)."""
    props: list[Prop] = []
    pos = stream_offset
    while True:
        if len(props) > MAX_PROPERTIES:
            raise SaveError("runaway property stream")
        prop, pos = _read_tag(data, pos, len(data))
        if prop is None:
            return props, pos
        props.append(prop)


# ---------------------------------------------------------------------------
# value decoding
# ---------------------------------------------------------------------------

def _decode_scalar(data: bytes, pos: int, end: int, typ: str) -> tuple[object, int]:
    """Decode one untagged element of `typ` (a tag field or a container item)."""
    if typ in ("IntProperty",):
        return _i32(data, pos, end)
    if typ in ("Int64Property", "UInt64Property"):
        _need(data, pos, 8, end)
        return struct.unpack_from("<q", data, pos)[0], pos + 8
    if typ == "UInt32Property":
        return _u32(data, pos, end)
    if typ in ("Int16Property", "UInt16Property"):
        _need(data, pos, 2, end)
        return struct.unpack_from("<H", data, pos)[0], pos + 2
    if typ in ("Int8Property", "UInt8Property"):
        return _u8(data, pos, end)
    if typ == "FloatProperty":
        _need(data, pos, 4, end)
        return round(struct.unpack_from("<f", data, pos)[0], 6), pos + 4
    if typ == "DoubleProperty":
        _need(data, pos, 8, end)
        return struct.unpack_from("<d", data, pos)[0], pos + 8
    if typ == "BoolProperty":
        v, p = _u8(data, pos, end)
        return bool(v), p
    if typ == "ByteProperty":
        return _u8(data, pos, end)
    if typ in ("StrProperty", "NameProperty", "ObjectProperty", "ClassProperty",
               "InterfaceProperty"):
        return _fstr(data, pos, end)
    if typ in ("SoftObjectProperty", "SoftClassProperty", "TextProperty"):
        # A soft reference is FName AssetPathName + FString SubPathString.
        first, pos = _fstr(data, pos, end)
        second, pos = _fstr(data, pos, end)
        return {"asset": first, "sub_path": second}, pos
    if typ == "EnumProperty":
        return _fstr(data, pos, end)
    raise SaveError("unsupported element type %r" % typ)


_BINARY_STRUCT_FIELDS = {
    "Vector": ("<3f", ("x", "y", "z")),
    "Vector3f": ("<3f", ("x", "y", "z")),
    "Vector2D": ("<2f", ("x", "y")),
    "Vector2f": ("<2f", ("x", "y")),
    "Vector4": ("<4f", ("x", "y", "z", "w")),
    "Rotator": ("<3f", ("pitch", "yaw", "roll")),
    "Quat": ("<4f", ("x", "y", "z", "w")),
    "LinearColor": ("<4f", ("r", "g", "b", "a")),
    "Color": ("<4B", ("b", "g", "r", "a")),
    "IntPoint": ("<2i", ("x", "y")),
    "IntVector": ("<3i", ("x", "y", "z")),
    "FloatRange": ("<2f", ("lower", "upper")),
}


def _decode_binary_struct(struct_name: str, raw: bytes) -> dict:
    """Decode an engine value struct that UE writes as raw bytes.

    A struct whose byte width does not match its known layout is left as hex
    rather than forced: a wrong unpack would produce plausible garbage, which
    is worse than an obviously opaque value.
    """
    spec = _BINARY_STRUCT_FIELDS.get(struct_name)
    if spec is not None:
        fmt, names = spec
        if struct.calcsize(fmt) == len(raw):
            out = {"struct": struct_name}
            out.update(zip(names, struct.unpack(fmt, raw)))
            return out
    if struct_name == "Guid" and len(raw) == 16:
        return {"struct": "Guid", "guid": raw.hex()}
    if struct_name in ("DateTime", "Timespan") and len(raw) == 8:
        return {"struct": struct_name,
                "ticks": struct.unpack("<q", raw)[0]}
    return {"struct": struct_name, "raw_hex": raw.hex()}


def _decode_struct_region(data: bytes, pos: int, size: int, end: int,
                          struct_name: str) -> tuple[dict, int]:
    """Decode a struct's value region of exactly `size` bytes."""
    region_end = pos + size
    _need(data, pos, size, end)
    got = _probe_tag_stream(data, pos, region_end)
    if got is not None and got[1] == region_end:
        return {"struct": struct_name, "fields": got[0]}, region_end
    raw = data[pos:region_end]
    return _decode_binary_struct(struct_name, raw), region_end


_TAG_FIELD_NAMES = {
    "StrProperty", "NameProperty", "ObjectProperty", "ClassProperty",
    "InterfaceProperty", "SoftObjectProperty", "SoftClassProperty",
    "IntProperty", "Int64Property", "UInt32Property", "UInt64Property",
    "Int16Property", "UInt16Property", "FloatProperty", "DoubleProperty",
    "BoolProperty", "ByteProperty", "EnumProperty", "StructProperty",
    "ArrayProperty", "SetProperty", "MapProperty", "TextProperty",
    "MulticastInlineDelegateProperty", "MulticastDelegateProperty",
    "DelegateProperty",
}


def _probe_tag_stream(data: bytes, pos: int, end: int) -> tuple[dict, int] | None:
    """Try to parse a bare tagged struct stream starting at `pos`.

    Every property name must look like a real identifier and every type must be
    a known UE property type. That is what stops a 16-byte FGuid -- which is
    indistinguishable from a stream at first glance -- from being accepted as
    one: its leading int32 becomes a string length, and the bytes that follow
    essentially never spell a valid name followed by a valid type.
    Returns None when the bytes are not a plausible stream.
    """
    fields: dict = {}
    p = pos
    try:
        for _ in range(256):
            name, p = _fstr(data, p, end)
            if name == "None":
                return fields, p
            if not name or not name.replace("_", "").isalnum():
                return None
            typ, p = _fstr(data, p, end)
            if typ not in _TAG_FIELD_NAMES:
                return None
            size, p = _i32(data, p, end)
            _idx, p = _i32(data, p, end)
            if size < 0:
                return None
            struct_name = ""
            if typ == "StructProperty":
                struct_name, p = _fstr(data, p, end)
                _g, p = _take(data, p, 16, end)
            elif typ == "BoolProperty":
                val, p = _u8(data, p, end)
                fields[name] = bool(val)
            elif typ in ("ByteProperty", "EnumProperty"):
                _en, p = _fstr(data, p, end)
            elif typ in ("ArrayProperty", "SetProperty"):
                _in, p = _fstr(data, p, end)
            elif typ == "MapProperty":
                _in, p = _fstr(data, p, end)
                _vt, p = _fstr(data, p, end)
            hg, p = _u8(data, p, end)
            if hg:
                _g, p = _take(data, p, 16, end)
            if typ == "StructProperty":
                fields[name], p = _decode_struct_region(
                    data, p, size, end, struct_name)
            elif typ != "BoolProperty":
                value, p = _decode_scalar(data, p, end, typ)
                fields[name] = value
    except SaveError:
        return None
    return None


def _decode_struct_body(data: bytes, pos: int, end: int) -> tuple[object, int] | None:
    """Decode one untagged struct element: a tagged stream, else raw hex."""
    got = _probe_tag_stream(data, pos, end)
    if got is not None:
        return got
    return None


def _decode_map(data: bytes, prop: Prop) -> dict:
    """Decode a TMap. Strategy is chosen by exact fit, never by assumption.

    Two encodings exist for a struct element inside a map: a fixed-width binary
    value (an FGuid key) or a bare tagged stream (an enemy save record). The
    container tag names only the inner *type*, so the choice is resolved by
    requiring the whole entry list to consume the value region exactly.
    """
    off = prop.value_offset
    end = off + prop.value_len
    _nrem, pos = _i32(data, off, end)
    n, pos = _i32(data, pos, end)
    if n < 0 or n > MAX_CONTAINER:
        raise SaveError("implausible map entry count %d in %r" % (n, prop.name))
    start = pos

    key_modes = ["scalar"] if prop.inner_type != "StructProperty" else \
        ["struct16", "struct_stream"]
    val_modes = ["scalar"] if prop.value_type != "StructProperty" else \
        ["struct_stream", "struct16"]

    for km in key_modes:
        for vm in val_modes:
            try:
                entries, p = _decode_map_entries(
                    data, start, end, n, prop.inner_type, prop.value_type, km, vm)
            except SaveError:
                continue
            if p == end:
                return {
                    "entries": entries,
                    "encoding": {"key": km, "value": vm},
                    "count": n,
                }
    raise SaveError(
        "could not decode map %r (%d entries, %d bytes); tried key=%s value=%s"
        % (prop.name, n, end - start, key_modes, val_modes)
    )


def _decode_map_entries(data: bytes, pos: int, end: int, n: int,
                        key_type: str, val_type: str,
                        key_mode: str, val_mode: str) -> tuple[list, int]:
    entries = []
    for _ in range(n):
        if key_mode == "struct16":
            raw, pos = _take(data, pos, 16, end)
            key: object = raw.hex()
        elif key_mode == "struct_stream":
            key, pos = _decode_struct_body(data, pos, end)
            key = json.dumps(key, sort_keys=True) if isinstance(key, dict) else key
        else:
            key, pos = _decode_scalar(data, pos, end, key_type)
        if val_mode == "struct16":
            raw, pos = _take(data, pos, 16, end)
            val: object = {"guid": raw.hex()}
        elif val_mode == "struct_stream":
            val, pos = _decode_struct_body(data, pos, end)
        else:
            val, pos = _decode_scalar(data, pos, end, val_type)
        entries.append([key, val])
    return entries, pos


class MapValue(dict):
    """A decoded TMap, keyed for JSON but carrying how it was decoded.

    The wire encoding of a struct element inside a map is resolved by exact
    fit, so recording which encoding won is the difference between "the tool
    understood this file" and "the tool guessed and got lucky".
    """

    def __init__(self, entries, encoding: dict, count: int):
        super().__init__(entries)
        self.encoding = encoding
        self.count = count


def decode_property(data: bytes, prop: Prop) -> tuple[object, str | None]:
    """Decode one property. Returns (value, reason_if_not_fully_decoded)."""
    typ = prop.type
    off = prop.value_offset
    end = off + prop.value_len

    if typ == "BoolProperty":
        return bool(prop.bool_val), None
    if typ == "StructProperty":
        value, pos = _decode_struct_region(
            data, off, prop.value_len, end, prop.struct_name)
        if pos != end:
            raise SaveError("%r struct overran its region" % prop.name)
        if "raw_hex" in value:
            return value, ("struct %r is opaque (no tagged stream, no known "
                           "binary layout)" % prop.struct_name)
        return value, None
    if typ in ("ArrayProperty", "SetProperty"):
        pos = off
        if typ == "SetProperty":
            _nrem, pos = _i32(data, pos, end)
        n, pos = _i32(data, pos, end)
        if n < 0 or n > MAX_CONTAINER:
            raise SaveError("implausible %s count %d in %r" % (typ, n, prop.name))
        if prop.inner_type == "StructProperty":
            return {"__opaque": data[off:end].hex()}, \
                "%s<%s> is not decoded" % (typ, prop.inner_type)
        items = []
        for _ in range(n):
            v, pos = _decode_scalar(data, pos, end, prop.inner_type)
            items.append(v)
        if pos != end:
            raise SaveError("%r consumed %d of %d value bytes"
                            % (prop.name, pos - off, prop.value_len))
        return items, None
    if typ == "MapProperty":
        m = _decode_map(data, prop)
        return MapValue(m["entries"], m["encoding"], m["count"]), None
    if typ in ("MulticastInlineDelegateProperty", "MulticastDelegateProperty",
               "DelegateProperty"):
        return {"__opaque": data[off:end].hex()}, \
            "%s is transient script state and has no documented wire layout" % typ
    value, pos = _decode_scalar(data, off, end, typ)
    if pos != end:
        raise SaveError("%r consumed %d of %d value bytes"
                        % (prop.name, pos - off, prop.value_len))
    return value, None


# ---------------------------------------------------------------------------
# save model
# ---------------------------------------------------------------------------

class Save:
    """A parsed save. Holds the raw bytes so writes can splice, not rebuild."""

    def __init__(self, path: str, data: bytes):
        self.path = path
        self.raw = data
        self.header = parse_header(data)
        self.props, self.stream_end = parse_properties(
            data, self.header["stream_offset"])
        if self.stream_end > len(data):
            raise SaveError("property stream runs past end of file")
        self.by_name = {p.name: p for p in self.props}

    def prop(self, name: str) -> Prop:
        try:
            return self.by_name[name]
        except KeyError:
            raise SaveError("save has no property %r" % name) from None

    def decoded(self) -> dict:
        out = {}
        for p in self.props:
            value, _why = decode_property(self.raw, p)
            out[p.name] = value
        return out

    def undecoded(self) -> list:
        out = []
        for p in self.props:
            _value, why = decode_property(self.raw, p)
            if why:
                out.append({"name": p.name, "type": p.type, "reason": why})
        return out

    def named_save_data(self) -> list[list]:
        """The flag bank as an ordered [[name, int], ...] list."""
        prop = self.prop("NamedSaveData")
        if prop.type != "MapProperty":
            raise SaveError("NamedSaveData is %s, expected MapProperty" % prop.type)
        m = _decode_map(self.raw, prop)
        return [[str(k), int(v)] for k, v in m["entries"]]

    def flags(self) -> dict:
        return {k: v for k, v in self.named_save_data()}


def load(path: str) -> Save:
    with open(path, "rb") as f:
        return Save(path, f.read())


# ---------------------------------------------------------------------------
# writing
# ---------------------------------------------------------------------------

def _encode_named_save_data(pairs: list[list]) -> bytes:
    out = bytearray()
    out += struct.pack("<ii", 0, len(pairs))
    for key, value in pairs:
        out += _enc_fstr(key)
        out += struct.pack("<i", int(value))
    return bytes(out)


def rewrite_named_save_data(raw: bytes, prop: Prop, pairs: list[list]) -> bytes:
    """Splice a new NamedSaveData map into the file, copying all else verbatim."""
    new_value = _encode_named_save_data(pairs)
    if not (prop.size_offset + 4 <= prop.value_offset):
        raise SaveError("internal: size field is not before the value region")
    out = bytearray()
    out += raw[:prop.size_offset]
    out += struct.pack("<i", len(new_value))
    out += raw[prop.size_offset + 4:prop.value_offset]
    out += new_value
    out += raw[prop.value_offset + prop.value_len:]
    return bytes(out)


def write_atomic(path: str, data: bytes) -> None:
    """Write atomically, keeping the pristine bytes in `<path>.bak`.

    The `.bak` is written only when it does not already exist, so the first
    write preserves the operator's original file for good instead of letting a
    later run overwrite that recovery copy.
    """
    directory = os.path.dirname(path) or "."
    bak = path + ".bak"
    if not os.path.exists(bak):
        with open(bak, "wb") as f:
            f.write(open(path, "rb").read())
    fd, tmp = tempfile.mkstemp(prefix=".qdsave-", suffix=".tmp", dir=directory)
    try:
        with os.fdopen(fd, "wb") as f:
            f.write(data)
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp, path)
    except BaseException:
        try:
            os.unlink(tmp)
        except OSError:
            pass
        raise


# ---------------------------------------------------------------------------
# root guard
# ---------------------------------------------------------------------------

def _real(p: str) -> str:
    """Absolute, symlink/junction-resolved path, with its original casing."""
    return os.path.realpath(os.path.abspath(p))


def _key(p: str) -> str:
    """Comparison form of a path: resolved and case-folded for Windows."""
    return os.path.normcase(_real(p))


def resolve_root(root: str) -> str:
    if root is None or root.strip() == "":
        raise SystemExit(2)
    real = _real(root)
    if not os.path.isdir(real):
        print("REFUSED: --root is not an existing directory: %s" % root,
              file=sys.stderr)
        raise SystemExit(2)
    return real


def resolve_inside(root_real: str, arg: str) -> str:
    """Resolve `arg` against the root and refuse to leave it (exit 2).

    Resolution happens before the containment test, so a junction or symlink
    inside the root that points outside it is caught rather than followed.
    """
    candidate = arg if os.path.isabs(arg) else os.path.join(root_real, arg)
    real = _real(candidate)
    root_key, real_key = _key(root_real), _key(real)
    try:
        inside = os.path.commonpath([root_key, real_key]) == root_key
    except ValueError:  # different drives on Windows
        inside = False
    if not inside:
        print("REFUSED: path escapes --root\n"
              "  root   : %s\n"
              "  arg    : %s\n"
              "  resolved: %s" % (root_real, arg, real), file=sys.stderr)
        raise SystemExit(2)
    return real


def require_file(path: str) -> str:
    if not os.path.isfile(path):
        print("REFUSED: not an existing file: %s" % path, file=sys.stderr)
        raise SystemExit(2)
    return path


def announce_root(root_real: str, root_arg: str, echo: bool = True) -> None:
    """Echo the root that was actually adopted, not just the one passed in.

    Printing the resolved root is the point: if `--root` is a link or a `..`
    path, the operator sees the directory the tool really confined itself to.
    This goes to stdout on purpose. `--no-echo` exists for the JSON commands,
    which would otherwise emit an unparseable first line.
    """
    if not echo:
        return
    print("root: %s" % root_real)
    if _key(root_arg) != _key(root_real):
        print("root (as given): %s" % os.path.abspath(root_arg))


# ---------------------------------------------------------------------------
# commands
# ---------------------------------------------------------------------------

def cmd_dump(args) -> int:
    root = resolve_root(args.root)
    announce_root(root, args.root, echo=not args.no_echo)
    path = require_file(resolve_inside(root, args.file))
    save = load(path)
    decoded = save.decoded()
    payload = {
        "file": os.path.basename(path),
        "path": path,
        "root": root,
        "size": len(save.raw),
        "class_path": save.header["class_path"],
        "header": save.header,
        "stream_end": save.stream_end,
        "trailing_bytes": len(save.raw) - save.stream_end,
        "property_count": len(save.props),
        "properties": decoded,
        "property_meta": {p.name: p.meta for p in save.props},
        "map_encodings": {name: value.encoding
                          for name, value in decoded.items()
                          if isinstance(value, MapValue)},
        "undecoded": save.undecoded(),
    }
    json.dump(payload, sys.stdout, indent=2, sort_keys=False)
    sys.stdout.write("\n")
    return 0


def cmd_flags(args) -> int:
    root = resolve_root(args.root)
    announce_root(root, args.root, echo=not args.no_echo)
    path = require_file(resolve_inside(root, args.file))
    save = load(path)
    flags = save.flags()
    for key in sorted(flags):
        print("%s=%d" % (key, flags[key]))
    return 0


def _load_flag_json(path: str) -> dict:
    try:
        with open(path, "r", encoding="utf-8") as f:
            doc = json.load(f)
    except OSError as exc:
        raise SystemExit("cannot read --json %s: %s" % (path, exc))
    except json.JSONDecodeError as exc:
        raise SystemExit("--json %s is not valid JSON: %s" % (path, exc))
    if not isinstance(doc, dict):
        raise SystemExit("--json must contain an object of name -> integer")
    # Accept the shapes this tool itself emits, so `dump` output can be fed
    # back in without hand-editing.
    for wrapper in ("NamedSaveData", "flags"):
        inner = doc.get(wrapper)
        if isinstance(inner, dict):
            doc = inner
            break
    out = {}
    for key, value in doc.items():
        if not isinstance(key, str) or not key:
            raise SystemExit("flag names must be non-empty strings (got %r)" % (key,))
        if isinstance(value, bool) or not isinstance(value, int):
            raise SystemExit("flag %r must be an integer (got %r)" % (key, value))
        out[key] = value
    if not out:
        raise SystemExit("--json contains no flags")
    return out


def cmd_apply_flags(args) -> int:
    root = resolve_root(args.root)
    announce_root(root, args.root, echo=not args.no_echo)
    path = require_file(resolve_inside(root, args.file))
    wanted = _load_flag_json(resolve_inside(root, args.json))

    save = load(path)
    prop = save.prop("NamedSaveData")
    current = save.named_save_data()
    index = {k: i for i, (k, _v) in enumerate(current)}

    applied, added, unchanged, refused = [], [], [], []
    for key in sorted(wanted):
        new = wanted[key]
        if key not in index:
            current.append([key, new])
            added.append((key, new))
            continue
        old = current[index[key]][1]
        if new < old:
            refused.append((key, old, new))
        elif new > old:
            current[index[key]][1] = new
            applied.append((key, old, new))
        else:
            unchanged.append((key, old))

    prefix = "would-" if args.dry_run else ""
    for key, old, new in applied:
        print("%sapply   %s: %d -> %d" % (prefix, key, old, new))
    for key, new in added:
        print("%sadd     %s = %d" % (prefix, key, new))
    for key, value in unchanged:
        print("same    %s = %d" % (key, value))
    for key, old, new in refused:
        print("REFUSED %s: monotonic guard would lower %d -> %d (not written)"
              % (key, old, new))

    changes = len(applied) + len(added)
    if changes == 0:
        print("no write: nothing to change (%d refused, %d unchanged)"
              % (len(refused), len(unchanged)))
        return 0

    if args.dry_run:
        print("dry-run: %d change(s) would be written, %d refused"
              % (changes, len(refused)))
        return 0

    new_bytes = rewrite_named_save_data(save.raw, prop, current)

    # Verify before replacing: the new file must parse and must report exactly
    # the values just computed. A failed verification leaves the file alone.
    check = Save(path, new_bytes)
    got = check.flags()
    expect = {k: v for k, v in current}
    if got != expect:
        print("ABORTED: post-write verification failed; file untouched",
              file=sys.stderr)
        print("  expected %d flags, got %d flags" % (len(expect), len(got)),
              file=sys.stderr)
        return 1

    write_atomic(path, new_bytes)
    print("wrote %s (%d -> %d bytes); backup: %s"
          % (path, len(save.raw), len(new_bytes), path + ".bak"))
    print("applied=%d added=%d unchanged=%d refused=%d"
          % (len(applied), len(added), len(unchanged), len(refused)))
    return 0


def cmd_diff(args) -> int:
    root = resolve_root(args.root)
    announce_root(root, args.root, echo=not args.no_echo)
    path_a = require_file(resolve_inside(root, args.a))
    path_b = require_file(resolve_inside(root, args.b))
    a = load(path_a)
    b = load(path_b)
    da = a.decoded()
    db = b.decoded()

    containers: dict = {}
    scalars: dict = {}
    for name in sorted(set(da) | set(db)):
        if name not in da:
            containers[name] = {"added": db[name], "removed": None,
                                "changed": [], "present_in": "b"}
            continue
        if name not in db:
            containers[name] = {"added": None, "removed": da[name],
                                "changed": [], "present_in": "a"}
            continue
        va, vb = da[name], db[name]
        if isinstance(va, dict) and isinstance(vb, dict):
            ea = {str(k): v for k, v in va.items()}
            eb = {str(k): v for k, v in vb.items()}
            added = {k: eb[k] for k in sorted(set(eb) - set(ea))}
            removed = {k: ea[k] for k in sorted(set(ea) - set(eb))}
            changed = [{"key": k, "a": ea[k], "b": eb[k]}
                       for k in sorted(set(ea) & set(eb)) if ea[k] != eb[k]]
            if added or removed or changed:
                containers[name] = {"added": added, "removed": removed,
                                    "changed": changed}
        elif isinstance(va, list) and isinstance(vb, list):
            sa = [json.dumps(x, sort_keys=True) for x in va]
            sb = [json.dumps(x, sort_keys=True) for x in vb]
            added = sorted(set(sb) - set(sa))
            removed = sorted(set(sa) - set(sb))
            changed = [{"a": va, "b": vb}] if sa != sb else []
            if added or removed or changed:
                containers[name] = {"added": added, "removed": removed,
                                    "changed": changed}
        elif va != vb:
            scalars[name] = {"a": va, "b": vb}

    payload = {
        "a": path_a,
        "b": path_b,
        "class_path_a": a.header["class_path"],
        "class_path_b": b.header["class_path"],
        "containers": containers,
        "scalars": scalars,
    }
    json.dump(payload, sys.stdout, indent=2, sort_keys=False)
    sys.stdout.write("\n")
    return 0


# ---------------------------------------------------------------------------
# selftest
# ---------------------------------------------------------------------------

class _Runner:
    """Assembles PASS/FAIL lines and remembers whether anything failed.

    Assertions are exact-value comparisons on purpose. A `len(x) >= 1` check
    passes on a parser that has silently stopped decoding, so the counts below
    are the measured counts of the sample saves.
    """

    def __init__(self) -> None:
        self.passed = 0
        self.failed = 0

    def check(self, label: str, ok: bool, detail: str = "") -> None:
        if ok:
            self.passed += 1
            print("PASS  %s" % label)
        else:
            self.failed += 1
            print("FAIL  %s%s" % (label, (": " + detail) if detail else ""))

    def eq(self, label: str, got, want) -> None:
        self.check(label, got == want, "expected %r, got %r" % (want, got))


def _run_cli(argv: list[str]) -> tuple[int, str, str]:
    """Run this script as a subprocess so exit codes and stdout are real."""
    proc = subprocess.run(
        [sys.executable, os.path.abspath(__file__)] + argv,
        capture_output=True, text=True)
    return proc.returncode, proc.stdout, proc.stderr


def _find_samples() -> str:
    here = os.path.dirname(os.path.abspath(__file__))
    candidates = []
    env = os.environ.get("QDSAVE_SAMPLES")
    if env:
        candidates.append(env)
    candidates.append(os.path.join(here, "..", "..", "analysis",
                                   "save-backup-before-live"))
    candidates.append(os.path.join(here, "..", "..", "..", "analysis",
                                   "save-backup-before-live"))
    for cand in candidates:
        real = os.path.abspath(cand)
        if all(os.path.isfile(os.path.join(real, n)) for n in SAMPLE_NAMES):
            return real
    raise SystemExit(
        "REFUSED: sample saves not found. Looked in:\n  "
        + "\n  ".join(os.path.abspath(c) for c in candidates)
        + "\nSet QDSAVE_SAMPLES to the directory holding "
        + ", ".join(SAMPLE_NAMES))


def cmd_selftest(args) -> int:
    samples = _find_samples()
    # The scratch root is created beside this script, not in the system temp
    # directory, so the run stays inside the mod tree this tool owns.
    # `os.makedirs` is used rather than `tempfile.mkdtemp` because some
    # workspace-write sandboxes permit writes into a plain directory but not
    # into one created through the platform temp API; this was measured on the
    # machine this tool was built on.
    scratch_parent = os.path.dirname(os.path.abspath(__file__))
    temp_root = os.path.join(
        scratch_parent, ".qdsave-selftest-" + uuid.uuid4().hex[:8])
    os.makedirs(temp_root)
    r = _Runner()
    print("root: %s  (temporary, fabricated by selftest)" % temp_root)
    print("samples: %s" % samples)
    print("")
    try:
        # Copy the *backup* samples into the temp root. The operator's live
        # profile is never read or written by this tool.
        for name in SAMPLE_NAMES:
            shutil.copyfile(os.path.join(samples, name),
                            os.path.join(temp_root, name))

        w0 = os.path.join(temp_root, "World0.sav")
        with open(w0, "rb") as f:
            original_w0 = f.read()
        # A pristine snapshot to diff against later. The sample set on disk also
        # has `World0_backup.sav`, but it is a separate observation, not a
        # before-image of our own writes.
        pristine_w0 = os.path.join(temp_root, "World0_pristine.sav")
        with open(pristine_w0, "wb") as f:
            f.write(original_w0)

        # ---- root echo is on stdout, as required -------------------------
        rc, echo_out, _err = _run_cli(
            ["dump", "--root", temp_root, "World0.sav"])
        r.eq("root is echoed on stdout by default",
             echo_out.splitlines()[0],
             "root: %s" % os.path.realpath(temp_root))

        # ---- dump: exact structure -------------------------------------
        print("-- dump World0.sav --")
        rc, out, err = _run_cli(["dump", "--root", temp_root, "--no-echo", "World0.sav"])
        r.eq("dump exits 0", rc, 0)
        try:
            doc = json.loads(out)
        except json.JSONDecodeError as exc:
            r.check("dump emits JSON", False, str(exc))
            doc = {}
        props = doc.get("properties", {})
        r.eq("class_path", doc.get("class_path"),
             "/Game/Blueprints/SaveSystem/FS_SaveSlot.FS_SaveSlot_C")
        r.eq("engine_version", doc.get("header", {}).get("engine_version"), "4.27.2")
        r.eq("package_ue4_version",
             doc.get("header", {}).get("package_ue4_version"), 522)
        r.eq("top-level property count", doc.get("property_count"), 30)
        r.eq("NamedSaveData count", len(props.get("NamedSaveData", {})), 11)
        r.eq("SavableActorStates count",
             len(props.get("SavableActorStates", {})), 7)
        enemy = props.get("EnemySaveStates", {})
        r.eq("EnemySaveStates count", len(enemy), 414)

        expected_flags = {
            "Arjaot_State": 1,
            "BrokeBarricade": 1,
            "CheckpointUnlockCount": 1,
            "CompletedChallenges": 0,
            "FullEnergyCellsCount": 0,
            "IntroMusicState": 0,
            "RustySwordKnightGaveSword": 1,
            "RustySwordKnightState": 1,
            "TiniScytheHammerAP": 0,
            "TotalAchievementTreasures": 0,
            "TotalAchievementWeapons": 1,
        }
        r.eq("NamedSaveData values", props.get("NamedSaveData"), expected_flags)
        r.eq("SavableActorStates values", props.get("SavableActorStates"), {
            "8d7e161721ba9c4aabe58f83d3257452": 1,
            "db324584ebf62e46882b9140bd2c803e": 1,
            "cc38ef1138930c4a9ec9cb8deedf02d0": 0,
            "4051b22259600b45ade8fb99bf657206": 0,
            "d31ac888f131c547a1af5461fe7757da": 0,
            "3021d4cf190d734a9c1a6651e3a9c82c": 0,
            "5c92d4cfe099ea4a964701dab3bc5b04": 1,
        })
        first_enemy = list(enemy.items())[0]
        r.eq("EnemySaveStates first key",
             first_enemy[0], "3b5db8ee49d50d4c8b52befb669a28cf")
        r.eq("EnemySaveStates first value", first_enemy[1],
             {"bValid": True, "RespawnTime": -1.0, "RemainingHP": -1.0})
        r.eq("all enemy records identical to the first",
             sum(1 for _k, v in enemy.items()
                 if v == {"bValid": True, "RespawnTime": -1.0,
                          "RemainingHP": -1.0}), 414)
        r.eq("EnemySaveStates key encoding",
             doc.get("map_encodings", {}).get("EnemySaveStates"),
             {"key": "struct16", "value": "struct_stream"})
        r.eq("NamedSaveData key encoding",
             doc.get("map_encodings", {}).get("NamedSaveData"),
             {"key": "scalar", "value": "scalar"})
        r.eq("SavableActorStates key encoding",
             doc.get("map_encodings", {}).get("SavableActorStates"),
             {"key": "struct16", "value": "scalar"})

        r.eq("SaveLevel", props.get("SaveLevel"), "Ruins_Level_00")
        r.eq("SavedArea", props.get("SavedArea"),
             "/Game/Blueprints/Areas/1GreenRuins/GR_Camp_Save.GR_Camp_Save")
        r.eq("CurrentPlayerHealth", props.get("CurrentPlayerHealth"), 90.0)
        r.eq("ActiveSuspendSlot", props.get("ActiveSuspendSlot"), 1)
        r.eq("SaveVersion", props.get("SaveVersion"), 6)
        r.eq("InventoryContents count", len(props.get("InventoryContents", {})), 1)
        r.eq("Equipment count", len(props.get("Equipment", {})), 1)
        r.eq("EquipmentSlots count", len(props.get("EquipmentSlots", {})), 1)
        r.eq("WeaponCharges count", len(props.get("WeaponCharges", {})), 106)
        r.eq("DaggerCharges count", len(props.get("DaggerCharges", {})), 26)
        r.eq("SavePointsVisited count", len(props.get("SavePointsVisited", [])), 1)
        r.eq("CollectedNotes count", len(props.get("CollectedNotes", [])), 5)
        r.eq("NPCsInJournal count", len(props.get("NPCsInJournal", [])), 2)
        r.eq("undecoded property count", len(doc.get("undecoded", [])), 1)
        r.eq("undecoded is ItemChangedEvent",
             (doc.get("undecoded") or [{}])[0].get("name"), "ItemChangedEvent")

        # ---- other two containers --------------------------------------
        print("")
        print("-- dump System0.sav / Challenges --")
        rc, out, _err = _run_cli(["dump", "--root", temp_root, "--no-echo", "System0.sav"])
        r.eq("System0 dump exits 0", rc, 0)
        sys_doc = json.loads(out)
        r.eq("System0 property count", sys_doc.get("property_count"), 1)
        r.eq("System0 LanguageCode",
             sys_doc.get("properties", {}).get("LanguageCode"), "en")
        rc, out, _err = _run_cli(["dump", "--root", temp_root, "--no-echo", "Challenges"])
        r.eq("Challenges dump exits 0", rc, 0)
        ch_doc = json.loads(out)
        r.eq("Challenges class_path", ch_doc.get("class_path"),
             "/Script/QueensDomain.FS_ChallengeSaveGame")
        ch = ch_doc.get("properties", {}).get("ChallengeStats", {})
        r.eq("Challenges entry count", len(ch), 2)
        r.eq("Challenges entries", ch, {
            "cha_Item_GotAllWeps": 1,
            "cha_Challenge_AllTeleporters": 1,
        })

        # ---- flags ------------------------------------------------------
        print("")
        print("-- flags World0.sav --")
        rc, out, _err = _run_cli(["flags", "--root", temp_root, "World0.sav"])
        r.eq("flags exits 0", rc, 0)
        want_lines = ["%s=%d" % (k, expected_flags[k])
                      for k in sorted(expected_flags)]
        rc, out_no_echo, _err = _run_cli(
            ["flags", "--root", temp_root, "--no-echo", "World0.sav"])
        r.eq("flags exits 0 with --no-echo", rc, 0)
        r.eq("flags output lines (echo suppressed)",
             out_no_echo.strip().splitlines(), want_lines)
        r.eq("flags prints root first", out.splitlines()[0],
             "root: %s" % os.path.realpath(temp_root))
        r.eq("flags body after the echo lines",
             [ln for ln in out.splitlines() if not ln.startswith("root")],
             want_lines)

        # ---- apply-flags: no-op round trip ------------------------------
        print("")
        print("-- apply-flags: round trip --")
        flags_json = os.path.join(temp_root, "flags.json")
        with open(flags_json, "w", encoding="utf-8") as f:
            json.dump(expected_flags, f)
        before = json.loads(_run_cli(
            ["dump", "--root", temp_root, "--no-echo", "World0.sav"])[1])
        rc, out, _err = _run_cli(
            ["apply-flags", "--root", temp_root, "World0.sav",
             "--json", "flags.json"])
        r.eq("no-op apply-flags exits 0", rc, 0)
        r.check("no-op apply reports no write", "no write:" in out, out.strip())
        with open(w0, "rb") as f:
            after_bytes = f.read()
        r.eq("no-op apply leaves bytes identical", after_bytes, original_w0)
        after = json.loads(_run_cli(
            ["dump", "--root", temp_root, "--no-echo", "World0.sav"])[1])
        r.eq("dump -> apply-flags -> dump is a no-op",
             after["properties"], before["properties"])
        r.eq("no-op apply creates no .bak",
             os.path.exists(w0 + ".bak"), False)

        # ---- monotonic guard: lowering is refused ----------------------
        print("")
        print("-- monotonic guard --")
        with open(flags_json, "w", encoding="utf-8") as f:
            json.dump({"Arjaot_State": 0, "CheckpointUnlockCount": 7}, f)
        rc, out, _err = _run_cli(
            ["apply-flags", "--root", temp_root, "World0.sav",
             "--json", "flags.json"])
        r.eq("mixed apply exits 0", rc, 0)
        r.check("lowered value is reported as REFUSED",
                "REFUSED Arjaot_State" in out, out.strip())
        r.check("raised value is applied",
                "apply   CheckpointUnlockCount: 1 -> 7" in out, out.strip())
        guarded = json.loads(_run_cli(
            ["dump", "--root", temp_root, "--no-echo", "World0.sav"])[1])["properties"]
        r.eq("refused key keeps its value", guarded["NamedSaveData"]["Arjaot_State"], 1)
        r.eq("applied key took the new value",
             guarded["NamedSaveData"]["CheckpointUnlockCount"], 7)
        r.eq("guard does not change the key count",
             len(guarded["NamedSaveData"]), 11)
        r.eq("write kept a .bak", os.path.exists(w0 + ".bak"), True)
        with open(w0 + ".bak", "rb") as f:
            r.eq(".bak holds the pristine bytes", f.read(), original_w0)

        # ---- additions are allowed -------------------------------------
        print("")
        print("-- new key --")
        with open(flags_json, "w", encoding="utf-8") as f:
            json.dump({"QD_CoopProbe": 3}, f)
        rc, out, _err = _run_cli(
            ["apply-flags", "--root", temp_root, "World0.sav",
             "--json", "flags.json"])
        r.eq("add exits 0", rc, 0)
        r.check("add is reported", "add     QD_CoopProbe = 3" in out, out.strip())
        added = json.loads(_run_cli(
            ["dump", "--root", temp_root, "--no-echo", "World0.sav"])[1])["properties"]
        r.eq("new key landed", added["NamedSaveData"].get("QD_CoopProbe"), 3)
        r.eq("key count after add", len(added["NamedSaveData"]), 12)
        r.eq("rest of the save is untouched by a write",
             {k: v for k, v in added.items() if k != "NamedSaveData"},
             {k: v for k, v in before["properties"].items()
              if k != "NamedSaveData"})

        # ---- dry run ----------------------------------------------------
        print("")
        print("-- dry run --")
        with open(flags_json, "w", encoding="utf-8") as f:
            json.dump({"QD_CoopProbe": 99}, f)
        pre_dry = open(w0, "rb").read()
        rc, out, _err = _run_cli(
            ["apply-flags", "--root", temp_root, "World0.sav",
             "--json", "flags.json", "--dry-run"])
        r.eq("dry-run exits 0", rc, 0)
        r.check("dry-run says what it would do",
                "would-apply   QD_CoopProbe: 3 -> 99" in out, out.strip())
        r.eq("dry-run writes nothing", open(w0, "rb").read(), pre_dry)

        # ---- diff -------------------------------------------------------
        print("")
        print("-- diff --")
        rc, out, _err = _run_cli(
            ["diff", "--root", temp_root, "--no-echo",
             "World0_pristine.sav", "World0.sav"])
        r.eq("diff exits 0", rc, 0)
        diff_doc = json.loads(out)
        nsd = diff_doc["containers"]["NamedSaveData"]
        r.eq("diff changed-key count", len(nsd["changed"]), 1)
        r.eq("diff changed key", nsd["changed"][0],
             {"key": "CheckpointUnlockCount", "a": 1, "b": 7})
        r.eq("diff added keys", nsd["added"], {"QD_CoopProbe": 3})
        r.eq("diff removed keys", nsd["removed"], {})
        r.eq("diff reports only the container that differs",
             sorted(diff_doc["containers"]), ["NamedSaveData"])
        r.eq("diff reports no scalar changes", diff_doc["scalars"], {})

        # ---- root guard -------------------------------------------------
        print("")
        print("-- root guard --")
        outside = os.path.abspath(os.path.join(temp_root, "..",
                                               "qdsave-outside.sav"))
        rc, _out, err = _run_cli(["dump", "--root", temp_root, outside])
        r.eq("absolute path outside root exits 2", rc, 2)
        r.check("absolute escape explains itself",
                "REFUSED: path escapes --root" in err, err.strip())
        rc, _out, err = _run_cli(
            ["dump", "--root", temp_root, "../" + os.path.basename(outside)])
        r.eq("relative ../ path exits 2", rc, 2)
        rc, _out, _err = _run_cli(["dump", "World0.sav"])
        r.eq("missing --root exits 2", rc, 2)
        rc, _out, _err = _run_cli(
            ["flags", "--root", os.path.join(temp_root, "nope"), "World0.sav"])
        r.eq("missing root directory exits 2", rc, 2)
        rc, _out, _err = _run_cli(
            ["apply-flags", "--root", temp_root, "World0.sav"])
        r.eq("apply-flags without --json exits 2", rc, 2)
        rc, _out, err = _run_cli(
            ["apply-flags", "--root", temp_root, "World0.sav",
             "--json", outside])
        r.eq("--json outside root exits 2", rc, 2)

        # ---- malformed input --------------------------------------------
        print("")
        print("-- malformed input --")
        bad = os.path.join(temp_root, "bad.json")
        with open(bad, "w", encoding="utf-8") as f:
            json.dump({"NotAnInt": "seven"}, f)
        rc, _out, err = _run_cli(
            ["apply-flags", "--root", temp_root, "World0.sav",
             "--json", "bad.json"])
        r.check("non-integer flag value is refused",
                rc != 0 and "must be an integer" in err, err.strip())
        junk = os.path.join(temp_root, "junk.sav")
        with open(junk, "wb") as f:
            f.write(b"NOTGVAS" + b"\x00" * 64)
        rc, _out, err = _run_cli(["dump", "--root", temp_root, "--no-echo", "junk.sav"])
        r.check("non-GVAS file fails without a traceback",
                rc != 0 and "Traceback" not in err, err.strip())
    finally:
        if args.keep:
            print("")
            print("kept temporary root: %s" % temp_root)
        else:
            shutil.rmtree(temp_root, ignore_errors=True)

    print("")
    print("selftest: %d passed, %d failed" % (r.passed, r.failed))
    return 0 if r.failed == 0 else 1


# ---------------------------------------------------------------------------
# entry point
# ---------------------------------------------------------------------------

def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="qdsave.py",
        description="Read/write Queen's Domain GVAS saves. --root is mandatory "
                    "for every command that touches a file.")
    sub = p.add_subparsers(dest="command", required=True)

    d = sub.add_parser("dump", help="print the whole save as JSON")
    d.add_argument("--root", required=True, help="directory every path must stay inside")
    d.add_argument("--no-echo", action="store_true",
                   help="suppress the root echo so stdout is pure JSON")
    d.add_argument("file", help="save file, absolute or relative to --root")
    d.set_defaults(func=cmd_dump)

    f = sub.add_parser("flags", help="print NamedSaveData as sorted key=value")
    f.add_argument("--root", required=True)
    f.add_argument("--no-echo", action="store_true")
    f.add_argument("file")
    f.set_defaults(func=cmd_flags)

    a = sub.add_parser("apply-flags",
                       help="apply a host-authoritative flag set (monotonic)")
    a.add_argument("--root", required=True)
    a.add_argument("--no-echo", action="store_true")
    a.add_argument("file")
    a.add_argument("--json", required=True, help="JSON object of name -> integer")
    a.add_argument("--dry-run", action="store_true")
    a.set_defaults(func=cmd_apply_flags)

    df = sub.add_parser("diff", help="per-container added/removed/changed entries")
    df.add_argument("--root", required=True)
    df.add_argument("--no-echo", action="store_true")
    df.add_argument("a")
    df.add_argument("b")
    df.set_defaults(func=cmd_diff)

    st = sub.add_parser("selftest",
                        help="run every command against the sample saves")
    st.add_argument("--keep", action="store_true",
                    help="keep the temporary root for inspection")
    st.set_defaults(func=cmd_selftest)
    return p


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        return args.func(args)
    except SaveError as exc:
        print("error: %s" % exc, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
