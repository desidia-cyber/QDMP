#!/usr/bin/env python3
"""Dump the name table (and, where it parses, the import/export table) of a
QueensDomain .uasset.

The packages in this build are cooked with all version fields zeroed:
    LegacyFileVersion = -7, LegacyUE3Version = 0, FileVersionUE4 = 0,
    FileVersionLicenseeUE4 = 0, CustomVersionCount = 0
so the summary is short and fixed:

    +0x00 u32  tag 0x9E2A83C1
    +0x04 i32  LegacyFileVersion (-7)
    +0x08 i32  LegacyUE3Version
    +0x0C i32  FileVersionUE4
    +0x10 i32  FileVersionLicenseeUE4
    +0x14 i32  CustomVersionCount
    +0x18 i32  TotalHeaderSize          == the .uasset entry size
    +0x1C FString FolderName            "None"
    +??   u32  PackageFlags             0x80000000 (PKG_FilterEditorOnly)
    +??   i32  NameCount
    +??   i32  NameOffset
    +??   i32  ImportCount, i32 ImportOffset, i32 ExportCount, i32 ExportOffset,
              i32 DependsOffset
    ...   then small fixed tables, then imports, exports and the name table

Name table entry = FString + u16 non-case-preserving hash + u16 case-preserving hash.
This file only *needs* the name table, so the import/export parse is best-effort and
each call reports whether it validated.
"""
import struct
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pakread  # noqa: E402

PKG_MAGIC = b"\xc1\x83\x2a\x9e"


class Reader:
    def __init__(self, d):
        self.d = d

    def i32(self, o):
        return struct.unpack("<i", self.d[o:o + 4])[0]

    def u32(self, o):
        return struct.unpack("<I", self.d[o:o + 4])[0]

    def fname(self, o):
        """Return (name_index, number, next_offset) for an un-hashed FName."""
        idx, num = struct.unpack("<ii", self.d[o:o + 8])
        return idx, num, o + 8

    def fstring(self, o):
        n = self.i32(o)
        o += 4
        if n == 0:
            return "", o
        if n < 0:
            nb = -n * 2
            return self.d[o:o + nb].decode("utf-16-le", "replace").split("\x00")[0], o + nb
        return self.d[o:o + n].split(b"\x00")[0].decode("latin-1"), o + n


def parse(data):
    r = Reader(data)
    if data[:4] != PKG_MAGIC:
        raise ValueError("not a package")
    o = 4
    legacy = r.i32(o); o += 4
    if legacy != -4:
        o += 4
    o += 4
    o += 4
    custom = 0
    if legacy <= -2:
        custom = r.i32(o); o += 4
        o += custom * 20
    if legacy < 0:
        total_header = r.i32(o); o += 4
    else:
        total_header = 0
    folder, o = r.fstring(o)
    pkg_flags = r.u32(o); o += 4
    name_count = r.i32(o); o += 4
    name_offset = r.i32(o); o += 4
    # SoftObjectPathsCount, SoftObjectPathsOffset,
    # ExportCount, ExportOffset, ImportCount, ImportOffset, DependsOffset
    tail_fields = struct.unpack("<7i", data[o:o + 28])
    o += 28

    names = []
    p = name_offset
    for _ in range(name_count):
        s, p = r.fstring(p)
        names.append(s)
        p += 4  # two u16 hashes

    return {
        "legacy_version": legacy,
        "total_header_size": total_header,
        "folder": folder,
        "package_flags": pkg_flags,
        "name_count": name_count,
        "name_offset": name_offset,
        "tail_fields": tail_fields,
        "names": names,
        "size": len(data),
    }


def _nameref(names, idx):
    if 0 <= idx < len(names):
        return names[idx]
    return "<bad %d>" % idx


def try_imports_exports(data, info):
    """Import/export tables. Returns (imports, exports), both [] if the geometry
    does not validate.

    Field order observed (and confirmed by the two tables tiling the header exactly):
        tail = (SoftObjectPathsCount, SoftObjectPathsOffset,
                ExportCount, ExportOffset, ImportCount, ImportOffset, DependsOffset)
    with  ImportOffset + ImportCount*28 == ExportOffset
          ExportOffset + ExportCount*104 == DependsOffset
    """
    r = Reader(data)
    f = info["tail_fields"]
    exp_count, exp_off, imp_count, imp_off = f[2], f[3], f[4], f[5]
    names = info["names"]
    n = len(data)
    if not (0 <= imp_count < 200000 and 0 <= exp_count < 200000):
        return [], []
    if not (0 <= imp_off <= n and 0 <= exp_off <= n):
        return [], []
    if imp_off + imp_count * 28 > n or exp_off + exp_count * 104 > n:
        return [], []
    imports = []
    for i in range(imp_count):
        o = imp_off + i * 28
        cp, _, o = r.fname(o)
        cn, _, o = r.fname(o)
        outer = r.i32(o); o += 4
        on, _, o = r.fname(o)
        if not all(0 <= x < len(names) for x in (cp, cn, on)):
            return [], []
        imports.append({"class_package": names[cp], "class": names[cn],
                        "outer": outer, "name": names[on]})
    exports = []
    for i in range(exp_count):
        o = exp_off + i * 104
        class_idx = r.i32(o)
        super_idx = r.i32(o + 4)
        outer_idx = r.i32(o + 12)
        on, onnum, _ = r.fname(o + 16)
        if not (0 <= on < len(names)):
            return [], []
        def resolve(idx):
            if idx == 0:
                return None
            if idx < 0:
                k = -idx - 1
                return imports[k]["name"] if 0 <= k < len(imports) else "<imp %d>" % idx
            k = idx - 1
            return "<exp %d>" % k
        exports.append({"name": names[on], "class": resolve(class_idx),
                        "super": resolve(super_idx), "outer": resolve(outer_idx)})
    return imports, exports


# names that are compiler/Blueprint-VM noise rather than the mod's hook surface
NOISE_PREFIXES = ("CallFunc_", "K2Node_", "EdGraph", "Execute_", "Array_", "Map_", "Set_")
NOISE_SUFFIXES = ("_ReturnValue", "_GEN_VARIABLE", "_Value")
NOISE_EXACT = set("""
Actor ActorComponent AnimInstance Array ArrayProperty BoolProperty ByteProperty Class ClassProperty
DateTime Default__BlueprintGeneratedClass Default__Object Default__Function Default__Class
DelegateProperty DoubleProperty EnumProperty Field FloatProperty Function Guid IntProperty
Interface InterfaceProperty LazyObjectProperty MapProperty MulticlassDelegate MulticlassDelegateProperty
Name None NumericProperty Object ObjectProperty Package SceneComponent SetProperty SoftClassProperty
SoftObjectProperty StrProperty StructProperty TextProperty UInt16Property UInt32Property UInt64Property
Widget BlueprintGeneratedClass ActorTickFunction ActorUROManager ScsNode SimpleConstructionScript
Timeline Struct Array Int Bool Float Byte Name StrText ClassProperty MulticlassDelegateProperty
BlueprintGeneratedClass_0 EdGraphPinType EdGraphSchema EdGraphSchema_K2
""".split())


def classify(names):
    """Split the name table into 'looks user-authored' vs engine/VM noise."""
    props, funcs, other = [], [], []
    for n in names:
        if not n or n.startswith("/"):
            continue
        if n in NOISE_EXACT:
            continue
        if any(n.startswith(p) for p in NOISE_PREFIXES):
            continue
        if any(n.endswith(s) for s in NOISE_SUFFIXES):
            # *_Value / *_GEN_VARIABLE: keep the base but drop the synthesised suffix
            continue
        if n.endswith("_C") or n.startswith("Default__"):
            other.append(n)
            continue
        # heuristic: verb-first names are functions, otherwise properties
        if n[:1].isupper() and len(n) > 1 and n[1:2].islower() is False and "_" not in n:
            funcs.append(n)
        else:
            props.append(n)
    return props, funcs, other


def main():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("pakpath")
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args()
    with pakread.Pak() as pk:
        data = pk.payload(a.pakpath)
    info = parse(data)
    imports, exports = try_imports_exports(data, info)
    print("path            ", a.pakpath)
    print("uasset size     ", info["size"])
    print("total header    ", info["total_header_size"])
    print("names           ", info["name_count"])
    print("imports/exports ", len(imports), "/", len(exports))
    if exports:
        print("exports         ", [e["class"] for e in exports][:12])
    if imports:
        print("import classes  ", sorted({i["class"] for i in imports}))
    print("\n--- names ---")
    for n in info["names"]:
        print("  " + n)


if __name__ == "__main__":
    main()
