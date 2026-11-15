"""The rest point's own teleport field, and the player's max-health fields, with TYPES.

The co-op death lane has to move a dead player to the rest point and restore a
penalised max HP.  Guessing the field names from an actor's name table is not
enough: `PlayerTPLocation` could be a Vector, a Transform or an Object, and a raw
write needs the size.  This prints the class default object's tagged properties
(name, type, size) for the two classes the lane touches.

Run: python probe_restpoint_props.py
"""
import os, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
CODE = os.path.abspath(os.path.join(HERE, "..", "pak", "code"))
sys.path.insert(0, CODE)
import pakread
import blueprints
import importlib.util

_spec = importlib.util.spec_from_file_location(
    "bpr", os.path.join(CODE, "probes", "bp_bytecode_refs.py"))
bpr = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(bpr)

TAG_EXTRA = {"ArrayProperty": 8, "SetProperty": 8, "StructProperty": 8,
             "ByteProperty": 8, "EnumProperty": 8, "MapProperty": 16}


def parse_tags(blob, names, limit=2000):
    def nm(k):
        return names[k] if 0 <= k < len(names) else None
    o, props = 0, []
    while o + 8 <= len(blob) and len(props) < limit:
        ni, nn = struct.unpack("<ii", blob[o:o + 8])
        N = nm(ni)
        if N is None or N == "None":
            break
        if o + 24 > len(blob):
            break
        ti, tn = struct.unpack("<ii", blob[o + 8:o + 16])
        sz, ai = struct.unpack("<ii", blob[o + 16:o + 24])
        T = nm(ti)
        if T is None:
            break
        vo = o + 24 + TAG_EXTRA.get(T, 0) + 1
        props.append({"name": N, "type": T, "size": sz, "index": ai,
                      "value_off": vo, "value": blob[vo:vo + sz]})
        o = vo + sz
    return props


def dump(pk, path, wanted=None):
    d, _split = bpr.combined(pk, path)
    names, imports, exp = bpr.tables(d)
    blob = b""
    print("\n=== %s  (%d exports) ===" % (path, len(exp)))
    for e in exp:
        if not e["name"].startswith("Default__"):
            continue
        lo, hi = e["serial_off"], e["serial_off"] + e["serial_size"]
        body = d[lo:hi] if 0 <= lo and hi <= len(d) else blob
        props = parse_tags(body, names)
        print("  CDO %s: %d tagged props" % (e["name"], len(props)))
        for p in props:
            if wanted and not any(w.lower() in p["name"].lower() for w in wanted):
                continue
            val = p["value"]
            extra = ""
            if p["type"] == "FloatProperty" and len(val) >= 4:
                extra = " = %r" % struct.unpack("<f", val[:4])[0]
            elif p["type"] in ("IntProperty",) and len(val) >= 4:
                extra = " = %d" % struct.unpack("<i", val[:4])[0]
            elif p["type"] == "BoolPropertyByteProperty" and val:
                extra = " = %d" % val[0]
            elif p["type"] == "BoolProperty" and val:
                extra = " = %d" % val[0]
            elif p["type"] == "VectorProperty" and len(val) >= 12:
                extra = " = %r" % (struct.unpack("<3f", val[:12]),)
            elif p["type"] == "StructProperty" and len(val) >= 12:
                extra = " first12=%r" % (struct.unpack("<3f", val[:12]),)
            print("    %-34s %-18s size=%-4d%s" % (p["name"], p["type"], p["size"], extra))


if __name__ == "__main__":
    pk = pakread.Pak()
    dump(pk, r"QueensDomain/Content/LD45/Blueprints/Checkpoint/Checkpoint.uasset",
         wanted=["Player", "Rest", "Interact", "Heal", "Save", "Modes", "Location",
                 "Teleport", "Sphere", "Area", "Checkpoint"])
    dump(pk, r"QueensDomain/Content/Player/FS_PlayerChar.uasset",
         wanted=["Health", "HP", "Dead", "Knock", "Max", "Rest", "Checkpoint", "Save",
                 "Input", "Movement", "Last"])
    pk.close()
