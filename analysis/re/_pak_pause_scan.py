"""Pak-wide search for packages that import a pause / time-dilation function.

Instrument: every cooked .uasset/.umap carries an import table naming each
external object it references, including native UFunction imports. So
"asset X imports SetGamePaused" is proof that X's bytecode calls or binds it.
Names alone prove existence, not behaviour -- this tool reports *which package*
binds the function, which is exactly what the state mapping needs.

Reads only the package header (total_header_size) from the pak, not the body.
"""
import os
import sys
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "analysis", "pak", "code"))
import pakread            # noqa: E402
import blueprints         # noqa: E402

TARGETS = [
    "SetGamePaused", "IsGamePaused",
    "SetGlobalTimeDilation", "GetGlobalTimeDilation", "SetTimeDilation",
    "GetActorTimeDilation", "SetActorTickEnabled", "SetComponentTickEnabled",
    "SetTickableWhenPaused", "SetActorTickInterval", "SetComponentTickInterval",
    "SetIgnoreTimeDilation", "GetIgnoreTimeDilation",
    "SetPaused", "Pause", "CanPause", "IsPaused",
]

PKG_MAGIC = b"\xc1\x83\x2a\x9e"


def header_bytes(pk, path):
    """Read exactly the package header (summary + tables), using total_header_size."""
    off, size, _ = pk.entries[path]
    head = pk.stream.read(off + pakread.ENTRY_HEADER_SIZE, min(size, 4096))
    if head[:4] != PKG_MAGIC:
        return None
    # total_header_size sits right after the fixed version block; parse it the
    # same way blueprints.parse does, on the small buffer.
    o = 4
    (legacy,) = struct.unpack("<i", head[o:o + 4]); o += 4
    if legacy != -4:
        o += 4
    o += 4 + 4
    if legacy <= -2:
        (n_custom,) = struct.unpack("<i", head[o:o + 4]); o += 4
        o += n_custom * 20
    if legacy < 0:
        (total_header,) = struct.unpack("<i", head[o:o + 4]); o += 4
    else:
        total_header = size
    if total_header <= 0 or total_header > size:
        return None
    return pk.stream.read(off + pakread.ENTRY_HEADER_SIZE, total_header)


def main():
    rows = []
    with pakread.Pak() as pk:
        paths = [p for p in pk.entries
                 if p.endswith((".uasset", ".umap")) and p.startswith("QueensDomain/")]
        print("packages under QueensDomain/:", len(paths))
        for i, p in enumerate(paths):
            try:
                data = header_bytes(pk, p)
                if not data:
                    continue
                info = blueprints.parse(data)
                imports, exports = blueprints.try_imports_exports(data, info)
            except Exception as e:
                continue
            names = {imp["name"] for imp in imports}
            hit = [t for t in TARGETS if t in names]
            if not hit:
                continue
            rows.append({
                "path": p,
                "hits": hit,
                "exports": [e["name"] for e in exports if e["class"] == "Function"],
            })
            if i % 2000 == 0:
                print("  %d/%d scanned, %d hit" % (i, len(paths), len(rows)), flush=True)

    print("\npackages importing a pause-surface name: %d\n" % len(rows))
    rows.sort(key=lambda r: (-len(r["hits"]), r["path"]))
    for r in rows:
        print("%-72s  %s" % (r["path"], ",".join(r["hits"])))
    import json
    with open(os.path.join(HERE, "_pak_pause_hits.json"), "w") as fh:
        json.dump(rows, fh, indent=1)
    print("\nwrote _pak_pause_hits.json")


if __name__ == "__main__":
    main()
