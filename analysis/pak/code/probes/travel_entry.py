#!/usr/bin/env python3
"""Find the game's hard-map-travel entry point in the cooked corpus (SYNC-GAPS G6).

The question this answers is narrow and load-bearing:

    after a hard transition (900_SipehaWorld, 990_B33sment, the 997/998/999
    cutscene maps) which UFunction does the game itself call to move the local
    machine into another UWorld -- and is it OpenLevel, or a wrapper of it?

`SYNC-RESEARCH` section 7 showed OpenLevel is imported by menus, death and cheats
only, and that every placed teleporter/checkpoint travels in-world through
`FS_PlayerChar.TeleportToLocation`.  That leaves the hard switch unexplained.  The
name pool carries `LoadMapWithLoadingScreen`; this probe finds who declares and who
calls it, and prints the travel vocabulary of every package that mentions a hard
map name.

Run from the repo root:

    python analysis/pak/code/probes/travel_entry.py
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))

import pakread      # noqa: E402
import blueprints   # noqa: E402

PKG_MAGIC = b"\xc1\x83\x2a\x9e"

TARGETS = [
    "LoadMapWithLoadingScreen",
    "LoadMap",
    "OpenLevel",
    "OpenLevelBySoftObjectPtr",
    "QueueTeleportLevelLoad",
    "TeleportToLocation",
    "BeginTeleport",
    "FinishTeleport",
    "SetTeleportedRecently",
    "BeginCutsceneWithManager",
    "StartIntroCutscene",
    "LevelSequenceActor",
    "GetSequencePlayer",
    "BlockSuspendSaves",
    "SuspendSaveLoop",
    "RollItemDrop",
]

HARD_MAP_WORDS = ["900_SipehaWorld", "990_B33sment", "997_FinalZone",
                  "998_QueenCutscenes", "999_SipehaCutscenes"]


def names_of(pk, path):
    off, size, _ = pk.entries[path]
    base = off + pakread.ENTRY_HEADER_SIZE
    head = pk.stream.read(base, min(size, 128))
    if len(head) < 0x40 or head[:4] != PKG_MAGIC:
        return None
    (legacy,) = struct.unpack("<i", head[4:8])
    o = 8
    if legacy != -4:
        o += 4
    o += 8
    if legacy <= -2:
        (n_custom,) = struct.unpack("<i", head[o:o + 4])
        o += 4
        if n_custom < 0 or n_custom > 1000:
            return None
        o += n_custom * 20
    if legacy < 0:
        (total_header,) = struct.unpack("<i", head[o:o + 4])
        o += 4
    else:
        total_header = 0
    read_len = max(total_header, 4096)
    if read_len > size:
        read_len = size
    buf = pk.stream.read(base, read_len)
    try:
        return pakread.uasset_names(buf)["names"]
    except Exception:
        return None


def dump_refs(pk, path, words):
    data = pk.payload(path)
    info = blueprints.parse(data)
    imports, exports = blueprints.try_imports_exports(data, info)
    print("\n== %s ==" % path)
    print("  names=%d imports=%d exports=%d" % (len(info["names"]), len(imports), len(exports)))
    for e in exports:
        if any(w.lower() in str(e).lower() for w in words):
            print("  EXPORT", e)
    for i in imports:
        if any(w.lower() in str(i).lower() for w in words):
            print("  IMPORT", i)


def main():
    with pakread.Pak() as pk:
        paths = sorted(p for p in pk.entries if pakread.is_package(p))
        print("packages:", len(paths))
        hits = {}
        for n, path in enumerate(paths):
            names = names_of(pk, path)
            if not names:
                continue
            low = [x.lower() for x in names]
            found = [t for t in TARGETS if t.lower() in low]
            maps = [t for t in HARD_MAP_WORDS if t.lower() in low]
            if found or maps:
                hits[path] = (found, maps)
            if (n + 1) % 5000 == 0:
                print("  scanned %d/%d, hits %d" % (n + 1, len(paths), len(hits)))
        print("\n--- name-table hits ---")
        for path, (found, maps) in sorted(hits.items()):
            print("%-90s %s %s" % (path, ",".join(found), ("MAPS:" + ",".join(maps)) if maps else ""))

        # Who declares / calls the hard-load entry.
        print("\n--- reference dump for the hard-load candidate ---")
        for path in sorted(hits):
            found, maps = hits[path]
            if "LoadMapWithLoadingScreen" in found or "OpenLevel" in found or maps:
                dump_refs(pk, path, ["LoadMap", "OpenLevel", "Travel", "Sequencer",
                                     "Cutscene", "Suspend", "Drop"])


if __name__ == "__main__":
    main()
