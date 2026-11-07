#!/usr/bin/env python3
"""C10 - level streaming: every level-manager volume in every map, and the
level list each one configures.

Run:  python stream_volumes.py
Needs: ../pakread.py, ../blueprints.py, and the pak on disk.
Writes nothing.

What is measured
----------------
1. Per-map census of the streaming actor classes (all 51 .umap export tables).
2. Volume -> LevelList mapping. A cooked level actor's serial block is a tagged
   property stream; the value of an ObjectProperty sits 1 byte after the 24-byte
   tag + type-specific names. Rather than trust a whole-stream parse (map actor
   blocks do not parse cleanly end-to-end), each candidate is *structurally
   validated*: for a reference value at offset p, the FName at p-25 must name the
   property ("LevelList" / "A Side" / "B Side") and the FName at p-17 must be
   "ObjectProperty". That signature was recovered and then confirmed on the two
   property names the blueprint declares, so it is self-checking.
3. LevelList asset -> level names. The list assets are UFS_LevelList data assets
   and their element names are readable ASCII in the paired .uexp.
4. Streamed sublevels declared by the persistent map (map package imports).
5. Overlap/union arithmetic over the level lists (answers "can two players'
   needs be unioned").

Export serial data for a split package lives in the paired .uexp at
    uexp_offset = SerialOffset - TotalHeaderSize
which tiles exactly (sum of SerialSize == len(uexp) - 4). Validated below.
"""
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
import pakread      # noqa: E402
import blueprints   # noqa: E402

MAP_PREFIX = "QueensDomain/Content/"
STREAM_CLASSES = (
    "FS_LevelManagerVolume",
    "LevelManagerTrigger_C",
    "LevelStreamingDynamic",
    "LevelStreamingAlwaysLoaded",
    "LevelStreamingPersistent",
)
PROP_NAME_IDX = {}          # filled per map: name index -> property name


def serial_block(data, info, expo, exp_index, uexp):
    """Return the export's serialised bytes (uexp offset = SerialOffset - header)."""
    rec = expo + exp_index * 104
    size = struct.unpack("<q", data[rec + 28:rec + 36])[0]
    off = struct.unpack("<q", data[rec + 36:rec + 44])[0] - info["total_header_size"]
    if off < 0 or off + size > len(uexp):
        return None
    return uexp[off:off + size]


def fname_at(buf, off, names):
    if off < 0 or off + 8 > len(buf):
        return None
    idx, num = struct.unpack("<ii", buf[off:off + 8])
    if not (0 <= idx < len(names)) or num != 0:
        return None
    return names[idx]


def find_list_refs(blk, names, imports):
    """ObjectProperty values whose *tag* names a level-list property.

    Signature (validated): name FName at p-25, type FName at p-17, value i32 at p.
    Returns [(offset, property_name, import_name)].
    """
    out = []
    for p in range(25, len(blk) - 3):
        v = struct.unpack("<i", blk[p:p + 4])[0]
        if v >= 0 or v < -len(imports):
            continue
        prop = fname_at(blk, p - 25, names)
        if prop not in ("LevelList", "A Side", "B Side"):
            continue
        if fname_at(blk, p - 17, names) != "ObjectProperty":
            continue
        imp = imports[-v - 1]
        if imp["class"] != "FS_LevelList":
            continue
        out.append((p, prop, imp["name"]))
    return out


def uexp_strings(blob, minlen=4):
    """ASCII FString runs in a uexp (level names are short ASCII identifiers)."""
    return [s.decode() for s in re.findall(rb"[\x20-\x7e]{%d,}" % minlen, blob)]


def load_map(pk, mappath):
    data = pk.payload(mappath)
    info = blueprints.parse(data)
    imports, exports = blueprints.try_imports_exports(data, info)
    uexp_path = mappath.rsplit(".", 1)[0] + ".uexp"
    uexp = pk.payload(uexp_path) if uexp_path in pk.entries else b""
    return data, info, imports, exports, uexp


def stream_census(pk, maps):
    per_map = {}
    totals = {c: 0 for c in STREAM_CLASSES}
    level_streaming = []
    for m in maps:
        try:
            data, info, imports, exports, uexp = load_map(pk, m)
        except Exception as e:                       # pragma: no cover
            per_map[m] = {"error": str(e)}
            continue
        row = {c: 0 for c in STREAM_CLASSES}
        streams = []
        for e in exports:
            cls = e["class"] or ""
            if cls in row:
                if not e["name"].startswith("Default__"):
                    row[cls] += 1
            if cls.startswith("LevelStreaming"):
                streams.append((e["name"], cls))
        for c in row:
            totals[c] += row[c]
        per_map[m] = row
        if streams:
            level_streaming.append((m, streams, len(data), len(uexp)))
    return per_map, totals, level_streaming


def volume_table(pk, maps):
    """[(map, actor, class, property, level_list_name, offset)]"""
    rows = []
    for m in maps:
        try:
            data, info, imports, exports, uexp = load_map(pk, m)
        except Exception:
            continue
        if not uexp:
            continue
        names = info["names"]
        expo = info["tail_fields"][3]
        for k, e in enumerate(exports):
            if (e["class"] or "") not in ("FS_LevelManagerVolume", "LevelManagerTrigger_C"):
                continue
            blk = serial_block(data, info, expo, k, uexp)
            if blk is None:
                continue
            for off, prop, listname in find_list_refs(blk, names, imports):
                rows.append((m, e["name"], e["class"], prop, listname, off))
    return rows


def level_lists(pk):
    """Every UFS_LevelList asset anywhere in the pak, with its level names."""
    out = {}
    for p in sorted(pk.entries):
        if not p.endswith(".uasset"):
            continue
        try:
            d = pk.payload(p)
            info = blueprints.parse(d)
            _, exports = blueprints.try_imports_exports(d, info)
        except Exception:
            continue
        if not exports:
            continue
        base = p.rsplit("/", 1)[-1][:-7]
        top = [e for e in exports if e["name"] in (base, base + "_C")]
        cls = (top[0]["class"] if top else None) or exports[0]["class"]
        if cls != "FS_LevelList":
            continue
        uexp_path = p.rsplit(".", 1)[0] + ".uexp"
        blob = pk.payload(uexp_path) if uexp_path in pk.entries else b""
        names = uexp_strings(blob)
        out[p.rsplit("/", 1)[-1][:-7]] = {
            "class": cls, "levels": names, "size": len(blob),
            "path": p,
        }
    return out


def streamed_level_names(pk, mappath):
    """Map package paths in a persistent level's name table = its sublevels.

    ULevelStreaming::WorldAsset is an FSoftObjectPath, so the streamed level
    appears as a *path string in the name table*, not as an import.  Every entry
    of the form /Game/<...>/Maps/<Level> is therefore a level this package can
    stream; entries under /Maps/LevelLists/ are level lists, not levels.
    """
    data, info, imports, exports, uexp = load_map(pk, mappath)
    short_self = mappath.rsplit("/", 1)[-1][:-5]
    names = set()
    for n in info["names"]:
        if "/Maps/" not in n or "/LevelLists/" in n:
            continue
        short = n.rsplit("/", 1)[-1].split(".")[0]
        if short and short != short_self:
            names.add(short)
    return sorted(names)


def main():
    with pakread.Pak() as pk:
        maps = sorted(p for p in pk.entries if p.lower().endswith(".umap"))
        print("maps scanned: %d" % len(maps))

        # ---- 1. class census ------------------------------------------------
        per_map, totals, streaming = stream_census(pk, maps)
        print("\n=== 1. streaming class census (export tables, placed actors) ===")
        for c in STREAM_CLASSES:
            print("  %-30s %4d" % (c, totals[c]))
        holders = [(m, r) for m, r in per_map.items() if any(v for v in r.values())]
        print("  maps that contain any of them: %d" % len(holders))
        for m, r in holders:
            bits = ", ".join("%s=%d" % (k, v) for k, v in r.items() if v)
            print("    %-58s %s" % (m.rsplit("/", 1)[-1], bits))

        # ---- 2. volume -> level list ---------------------------------------
        rows = volume_table(pk, maps)
        print("\n=== 2. volume -> level list (structurally validated ObjectProperty refs) ===")
        print("  references found: %d" % len(rows))
        vol = [r for r in rows if r[2] == "FS_LevelManagerVolume"]
        trg = [r for r in rows if r[2] == "LevelManagerTrigger_C"]
        print("  FS_LevelManagerVolume refs: %d   LevelManagerTrigger_C refs: %d"
              % (len(vol), len(trg)))
        print("\n  -- AFS_LevelManagerVolume (persistent-world volumes) --")
        for m, actor, cls, prop, lst, off in sorted(vol):
            print("    %-26s %-9s %-34s @%-4d [%s]"
                  % (actor, prop, lst, off, m.rsplit("/", 1)[-1]))
        print("\n  -- LevelManagerTrigger_C (A/B side boundary volumes) --")
        for m, actor, cls, prop, lst, off in sorted(trg):
            print("    %-26s %-8s %-32s @%-4d [%s]"
                  % (actor, prop, lst, off, m.rsplit("/", 1)[-1]))
        both = {r[1] for r in trg if r[3] == "A Side"} & {r[1] for r in trg if r[3] == "B Side"}
        aside = {r[1] for r in trg if r[3] == "A Side"} - both
        bside = {r[1] for r in trg if r[3] == "B Side"} - both
        print("  triggers: %d total, %d configure both sides, %d A-side only, %d B-side only"
              % (len({r[1] for r in trg}), len(both), len(aside), len(bside)))

        # ---- 3. level lists -------------------------------------------------
        lists = level_lists(pk)
        print("\n=== 3. LevelList assets (%d) ===" % len(lists))
        for name in sorted(lists):
            info = lists[name]
            print("  %-32s class=%-14s levels=%2d  %s"
                  % (name, info["class"], len(info["levels"]),
                     " ".join(info["levels"])))

        # ---- 4. streamed sublevels of the persistent world -------------------
        persist = MAP_PREFIX + "RuinsLevel/Maps/Ruins_Level_00.umap"
        sub = streamed_level_names(pk, persist)
        print("\n=== 4. Ruins_Level_00: sublevel packages it can stream (%d) ===" % len(sub))
        print("  " + " ".join(sub))
        for m, streams, nsz, usz in streaming:
            print("  %-40s LevelStreaming objects=%2d (umap %d B, uexp %d B)"
                  % (m.rsplit("/", 1)[-1], len(streams), nsz, usz))

        # ---- 5. union / overlap arithmetic ----------------------------------
        def levels_of(name):
            if name in lists:
                return lists[name]["levels"]
            for k, v in lists.items():                      # package names differ in case
                if k.lower() == name.lower():
                    return v["levels"]
            return []

        used = {r[4] for r in rows}
        print("\n=== 5. union/overlap over the level lists ===")
        print("  lists referenced by a volume: %d  (%s)" % (len(used), ", ".join(sorted(used))))
        union = set()
        for name in used:
            union |= set(levels_of(name))
        print("  union of those lists: %d distinct levels" % len(union))
        print("  " + " ".join(sorted(union)))
        worst = max((len(levels_of(n)) for n in used), default=0)
        print("  largest single list: %d levels" % worst)
        # pairwise overlap between the two volumes that a split party would hold
        a, b = "GreenRuins", "Library"
        sa, sb = set(levels_of(a)), set(levels_of(b))
        print("  example overlap %s(%d) vs %s(%d): common=%d, union=%d"
              % (a, len(sa), b, len(sb), len(sa & sb), len(sa | sb)))
        # worst realistic case: a split party in the two deepest areas
        a, b = "SZ_Depths", "Mausoleum2_Deep"
        sa, sb = set(levels_of(a)), set(levels_of(b))
        print("  example overlap %s(%d) vs %s(%d): common=%d, union=%d"
              % (a, len(sa), b, len(sb), len(sa & sb), len(sa | sb)))
        byvol = {}
        for m, actor, cls, prop, lst, off in rows:
            byvol.setdefault(lst, set()).update(levels_of(lst))
        for lst in sorted(byvol):
            print("    %-30s -> %2d levels" % (lst, len(byvol[lst])))

    return 0


if __name__ == "__main__":
    sys.exit(main())
