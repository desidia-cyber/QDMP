"""Blueprint function-level index for the pause / modal / menu surface.

Pass over every cooked package and record:
  * every export whose class resolves to `Function`   -> "this package declares F"
  * every import whose class resolves to `Function`   -> "this package binds F"
restricted to a vocabulary of pause / modal / menu / mode-switch names, plus the
full export list (so a caller of a custom helper can be resolved to its declarer).

Two packages may share a name; the index keeps both directions so the report can
say "declared by X, called by Y".
"""
import os
import sys
import re
import json
import struct
import collections

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "analysis", "pak", "code"))
import pakread            # noqa: E402
import blueprints         # noqa: E402

PKG_MAGIC = b"\xc1\x83\x2a\x9e"

# function-name vocabulary worth indexing on the import side
VOCAB = re.compile(
    r"(Pause|TimeDilation|Modal|Cutscene|PhotoMode|GameMenu|MenuOpened|MenuClosed|"
    r"OpenMenu|CloseMenu|ExitMenu|EnterMenu|IsOpen|Inspect|Journal|Bestiary|"
    r"Shop|Inventory|Equipment|Teleport|MapMenu|PauseGame|ResumeGame)", re.I)


def header_bytes(pk, path):
    off, size, _ = pk.entries[path]
    head = pk.stream.read(off + pakread.ENTRY_HEADER_SIZE, min(size, 4096))
    if head[:4] != PKG_MAGIC:
        return None
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
    decl = collections.defaultdict(set)     # function name -> packages declaring it
    bind = collections.defaultdict(set)     # function name -> packages binding it
    exports_by_pkg = {}
    n_ok = 0
    with pakread.Pak() as pk:
        paths = [p for p in pk.entries
                 if p.endswith((".uasset", ".umap")) and p.startswith("QueensDomain/")]
        for i, p in enumerate(paths):
            try:
                data = header_bytes(pk, p)
                if not data:
                    continue
                info = blueprints.parse(data)
                imports, exports = blueprints.try_imports_exports(data, info)
            except Exception:
                continue
            n_ok += 1
            fn_exports = [e["name"] for e in exports if e["class"] == "Function"]
            exports_by_pkg[p] = fn_exports
            for f in fn_exports:
                decl[f].add(p)
            for imp in imports:
                if imp["class"] == "Function" and VOCAB.search(imp["name"]):
                    bind[imp["name"]].add(p)
            if i % 3000 == 0:
                print("  %d/%d  decl=%d bind=%d" % (i, len(paths), len(decl), len(bind)),
                      flush=True)

    print("\nparsed %d packages" % n_ok)
    print("declared function names: %d   bound (vocab): %d\n" % (len(decl), len(bind)))

    print("=== who DECLARES a pause/dilation function ===")
    for name in sorted(decl):
        if re.search(r"(Pause|TimeDilation)", name, re.I):
            print("  %-40s %s" % (name, sorted(decl[name])))
    print()
    print("=== who BINDS (calls) a pause/dilation function ===")
    for name in sorted(bind):
        if re.search(r"(Pause|TimeDilation)", name, re.I):
            print("  %-40s %d caller(s)" % (name, len(bind[name])))
            for p in sorted(bind[name]):
                print("       ", p)

    out = {
        "decl": {k: sorted(v) for k, v in decl.items()},
        "bind": {k: sorted(v) for k, v in bind.items()},
        "exports_by_pkg": exports_by_pkg,
    }
    with open(os.path.join(HERE, "_bp_fn_index.json"), "w") as fh:
        json.dump(out, fh)
    print("\nwrote _bp_fn_index.json")


if __name__ == "__main__":
    main()
