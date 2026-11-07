#!/usr/bin/env python3
"""Read every cooked package header in the pak and record its real asset class.

Why headers and not folder names: folder names lie ("Enemies/Foo/Mesh_A" could be
anything). A cooked package carries its own summary, name table, import table and
export table, so the *class of the exported object* is a fact. The layout
implemented here follows the measured header in the sibling project
(`uasset_meta.py`) and CUE4Parse's FPackageFileSummary.

Cooked packages in this build are UNVERSIONED (FileVersionUE3/UE4/Licensee == 0),
so every version branch is evaluated against UE 4.27 = 522, never against 0.

Outputs (both machine-readable, under analysis/data/):
    asset-classes.json  {game_path: {"class": str, "exports": n, "imports": n, ...}}
    name-index.json     {name: {"count": n, "paths": [..up to 3..]}}
"""
import json
import os
import struct
import sys
from collections import Counter, defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import queenspak  # noqa: E402

PAK = (r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Content\Paks"
       r"\QueensDomain-WindowsNoEditor.pak")
OUTDIR = os.path.join(os.path.dirname(HERE), "data")

TAG = 0x9E2A83C1
PKG_FilterEditorOnly = 0x80000000
UE4_27 = 522

MOVED_EXPORTIMPORTMAPS_ADDED_TOTALHEADERSIZE = 158
FOLDER_ADDED = 224
SERIALIZE_TEXT_IN_PACKAGES = 214
ADDED_LINKER_DEPENDENCIES = 215
ADD_STRING_ASSET_REFERENCES_MAP = 218
ADDED_SEARCHABLE_NAMES = 219
ASSET_THUMBNAILS_IN_PACKAGES = 220
ADDED_PACKAGE_OWNER = 300
ENGINE_VERSION_OBJECT = 336
PACKAGE_SUMMARY_HAS_COMPATIBLE_ENGINE_VERSION = 337
ADDED_PACKAGE_SUMMARY_LOCALIZATION_ID = 516


class Err(Exception):
    pass


class R:
    def __init__(self, b):
        self.b = b
        self.p = 0

    def i32(self):
        v = struct.unpack_from("<i", self.b, self.p)[0]
        self.p += 4
        return v

    def u32(self):
        v = struct.unpack_from("<I", self.b, self.p)[0]
        self.p += 4
        return v

    def u16(self):
        v = struct.unpack_from("<H", self.b, self.p)[0]
        self.p += 2
        return v

    def i64(self):
        v = struct.unpack_from("<q", self.b, self.p)[0]
        self.p += 8
        return v

    def raw(self, n):
        v = self.b[self.p:self.p + n]
        if len(v) != n:
            raise Err("short read at 0x%X" % self.p)
        self.p += n
        return v

    def fstring(self):
        n = self.i32()
        if n == 0:
            return ""
        if n > 0:
            if n > 1 << 20:
                raise Err("implausible FString len %d" % n)
            return self.raw(n).rstrip(b"\x00").decode("latin-1")
        n = -n
        if n > 1 << 20:
            raise Err("implausible wide FString len %d" % n)
        return self.raw(n * 2).decode("utf-16-le", "replace").rstrip("\x00")


def read_summary(r):
    s = {}
    if r.u32() != TAG:
        raise Err("bad magic")
    legacy = r.i32()
    ue3 = r.i32() if legacy != -4 else 0
    ue4 = r.i32()
    if legacy <= -8:
        r.i32()
    lic = r.i32()
    unversioned = (ue3 == 0 and ue4 == 0 and lic == 0)
    ue4 = UE4_27 if unversioned else ue4
    s["unversioned"] = unversioned
    s["ue4"] = ue4

    n_custom = r.i32()
    if not 0 <= n_custom <= 4096:
        raise Err("custom version count %d" % n_custom)
    if n_custom:
        r.raw(n_custom * 16)
        r.raw(n_custom * 4)

    if ue4 >= MOVED_EXPORTIMPORTMAPS_ADDED_TOTALHEADERSIZE:
        s["total_header_size"] = r.i32()
    if ue4 >= FOLDER_ADDED:
        s["package_name"] = r.fstring()
    flags = r.u32()
    s["package_flags"] = flags
    filtered = bool(flags & PKG_FilterEditorOnly)

    s["name_count"] = r.i32()
    s["name_offset"] = r.i32()
    if not filtered and ue4 >= ADDED_PACKAGE_SUMMARY_LOCALIZATION_ID:
        r.fstring()
    if ue4 >= SERIALIZE_TEXT_IN_PACKAGES:
        r.i32(); r.i32()
    s["export_count"] = r.i32()
    s["export_offset"] = r.i32()
    s["import_count"] = r.i32()
    s["import_offset"] = r.i32()
    if ue4 >= ADDED_LINKER_DEPENDENCIES:
        r.i32()
    if ue4 >= ADD_STRING_ASSET_REFERENCES_MAP:
        r.i32(); r.i32()
    if ue4 >= ADDED_SEARCHABLE_NAMES:
        r.i32()
    if ue4 >= ASSET_THUMBNAILS_IN_PACKAGES:
        r.i32()
    r.raw(16)
    if not filtered and ue4 >= ADDED_PACKAGE_OWNER:
        r.raw(16)
    ngen = r.i32()
    r.raw(max(ngen, 0) * 8)
    if ue4 >= ENGINE_VERSION_OBJECT:
        r.u16(); r.u16(); r.u16(); r.u32(); r.fstring()
    if ue4 >= PACKAGE_SUMMARY_HAS_COMPATIBLE_ENGINE_VERSION:
        r.u16(); r.u16(); r.u16(); r.u32(); r.fstring()
    s["summary_end"] = r.p
    return s


def read_names(r, count, offset):
    r.p = offset
    out = []
    for _ in range(count):
        out.append(r.fstring())
        r.raw(4)
    return out


def read_imports(r, count, offset, names):
    """FObjectImport: ClassPackage(FName) ClassName(FName) OuterIndex(i32) ObjectName(FName).

    A package FName is 8 bytes -- an int32 name index plus an int32 number -- not
    the 4 bytes a bare index would be. At 16 bytes per record the table drifts out
    of alignment after the first entry (measured: the 9 imports above spanned 252
    bytes, which is 9 x 28 and lands exactly on ExportOffset).
    """
    r.p = offset
    out = []
    for _ in range(count):
        cp, _cpn, cn, _cnn, outer, on, _onn = struct.unpack_from("<7i", r.b, r.p)
        r.p += 28
        out.append((cp, cn, outer, on))
    return out


def read_exports(r, count, offset):
    """FObjectExport, stride 104 -- measured, not assumed.

    For multi-export packages only 104 makes every export's SerialOffset land at
    or after the .uasset size and stay monotonic (1159/1159 packages; 100 gives
    118/1159, 108 gives 0/1159).
    """
    r.p = offset
    out = []
    for _ in range(count):
        ei, si, ti = struct.unpack_from("<3i", r.b, r.p)
        oi = struct.unpack_from("<i", r.b, r.p + 12)[0]
        on = struct.unpack_from("<i", r.b, r.p + 16)[0]
        oflags = struct.unpack_from("<I", r.b, r.p + 24)[0]
        ssize, soff = struct.unpack_from("<2q", r.b, r.p + 28)
        r.p += 104
        out.append(dict(class_index=ei, super_index=si, template_index=ti,
                        outer_index=oi, object_name=on, flags=oflags,
                        serial_size=ssize, serial_offset=soff))
    return out


def classes_of(export, imports, exports, names):
    return resolve(export["class_index"], imports, exports, names) or "None"


def analyse(data):
    r = R(data)
    s = read_summary(r)
    names = read_names(r, s["name_count"], s["name_offset"])
    imports = []
    if 0 < s["import_count"] < 2_000_000 and 0 < s["import_offset"] < len(data):
        try:
            imports = read_imports(r, s["import_count"], s["import_offset"], names)
        except Exception:                                    # noqa: BLE001
            imports = []
    exports = []
    if 0 < s["export_count"] < 2_000_000 and 0 < s["export_offset"] < len(data):
        try:
            exports = read_exports(r, s["export_count"], s["export_offset"])
        except Exception:                                    # noqa: BLE001
            exports = []
    return s, names, imports, exports


def resolve(index, imports, exports, names):
    """FPackageIndex -> object name.

    MEASURED DIRECTION for this build: a NEGATIVE index addresses the import map
    and a POSITIVE index addresses the export map, both `abs(index) - 1`. Across
    all 19,162 packages every export[0] ClassIndex is negative and 18,390 of them
    (96.0%) land on an import whose ClassName is literally "Class"; all 19,162
    TemplateIndex values land on a "Default__*" import. The opposite direction
    puts 3,252 class indices out of range and resolves essentially no class
    names. So this follows the data, not the usual documentation.
    """
    if index == 0:
        return None
    j = abs(index) - 1
    if index < 0:
        if j < len(imports):
            on = imports[j][3]
            return names[on] if 0 <= on < len(names) else "?"
        return "?"
    if j < len(exports):
        on = exports[j]["object_name"]
        return names[on] if 0 <= on < len(names) else "?"
    return "?"


def main():
    os.makedirs(OUTDIR, exist_ok=True)
    classes = {}
    name_counts = Counter()
    name_examples = defaultdict(list)
    failures = []
    scan = 0

    with queenspak.Pak(PAK) as pk:
        targets = []
        for raw, e in pk.entries.items():
            gp = queenspak.game_path(pk.mount, raw)
            if gp.endswith(".uasset") or gp.endswith(".umap"):
                targets.append((gp, raw, e))

        for gp, raw, e in targets:
            scan += 1
            if scan % 4000 == 0:
                print("  ... %d/%d" % (scan, len(targets)), flush=True)
            try:
                data = pk.read_file(raw)
            except Exception as exc:                          # noqa: BLE001
                failures.append((gp, "read: %s" % exc))
                continue
            try:
                s, names, imports, exports = analyse(data)
            except Exception as exc:                          # noqa: BLE001
                failures.append((gp, "parse: %s" % exc))
                continue

            cls = None
            supercls = None
            all_classes = []
            sane = None
            if exports:
                sane = all(x["serial_size"] >= 0 and x["serial_offset"] >= 0
                           for x in exports)
                all_classes = sorted({classes_of(x, imports, exports, names)
                                      for x in exports} - {"None"})
                # export[0] is NOT always the package's asset: a mesh package can
                # list its morph targets first (measured on Player_FullChar_2026,
                # where export[0] is a MorphTarget and export[4] is the 485 KB
                # SkeletalMesh). The asset is the root export (OuterIndex == 0)
                # with the largest serial size.
                roots = [x for x in exports if x["outer_index"] == 0]
                pick = max(roots or exports, key=lambda x: x["serial_size"])
                cls = classes_of(pick, imports, exports, names)
                supercls = resolve(pick["super_index"], imports, exports, names)
            classes[gp] = {
                "class": cls,
                "super": supercls,
                "all_classes": all_classes,
                "exports": len(exports),
                "imports": len(imports),
                "names": len(names),
                "header_size": s.get("total_header_size"),
                "sane": sane,
            }
            for n in names:
                name_counts[n] += 1
                lst = name_examples[n]
                if len(lst) < 3 and gp not in lst:
                    lst.append(gp)

    with open(os.path.join(OUTDIR, "asset-classes.json"), "w",
              encoding="utf-8", newline="\n") as fh:
        json.dump(classes, fh, indent=0)

    index = {n: {"count": c, "paths": name_examples[n]}
             for n, c in name_counts.items()}
    with open(os.path.join(OUTDIR, "name-index.json"), "w",
              encoding="utf-8", newline="\n") as fh:
        json.dump(index, fh, indent=0)

    print("\nparsed %d packages, %d failures" % (len(classes), len(failures)))
    print("export tables validating (all serial sizes/offsets >= 0): %d / %d"
          % (sum(1 for c in classes.values() if c["sane"]), len(classes)))
    for f in failures[:20]:
        print("  FAIL", f)
    print("\nclass histogram:")
    for k, v in Counter(c["class"] for c in classes.values()).most_common(40):
        print("  %-34s %5d" % (k, v))
    print("\nunique names indexed: %d" % len(index))


if __name__ == "__main__":
    main()
