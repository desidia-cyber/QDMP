"""D4 residual: identify the owning actor of every multi-GUID `GUIDs` property.

For each .umap, find the `GUIDs` FName, read the property header (type FName,
size). If the header is an ArrayProperty with more than one GUID, map the
absolute occurrence back to the export whose serial range contains it and print
the actor's class. Also decode the GUID values and the `CurrentIndex`/`SpawnerGUID`
siblings where they fall inside the same export.
"""
import sys, struct, collections
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\pak\code")
import pakread, blueprints

IMP_NONE = 0


def _name(names, idx):
    return names[idx] if 0 <= idx < len(names) else "<bad %d>" % idx


def tables(data, info):
    """Import/export tables with serial offsets (FObjectExport @104 bytes)."""
    r = blueprints.Reader(data)
    f = info["tail_fields"]
    exp_count, exp_off, imp_count, imp_off = f[2], f[3], f[4], f[5]
    names = info["names"]
    n = len(data)
    if not (0 <= imp_count < 200000 and 0 <= exp_count < 200000):
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
        # this cooked build stores SerialSize/SerialOffset as int64 at +28/+36
        serial_size = r.i32(o + 28)
        serial_off = r.i32(o + 36)
        exports.append({"name": names[on], "class_idx": class_idx,
                        "super_idx": super_idx, "outer_idx": outer_idx,
                        "serial_off": serial_off, "serial_size": serial_size})
    return imports, exports


def resolve_name(idx, imports, exports):
    if idx == 0:
        return None
    if idx < 0:
        k = -idx - 1
        return imports[k]["name"] if 0 <= k < len(imports) else "<imp %d>" % idx
    k = idx - 1
    return exports[k]["name"] if 0 <= k < len(exports) else "<exp %d>" % idx


def owner_of(off, exports, ua_len, uexp_len):
    """Map an absolute offset (uasset+uexp concatenation) to an export.

    Try the conventions: SerialOffset relative to the .uexp start, relative to
    the .uasset start, and relative to the whole buffer.
    Return (export_or_None, convention).
    """
    cands = [
        (off, "whole"),
        (off - ua_len, "uexp-base"),
        (off - uexp_len, "uexp-size"),
    ]
    for lo, conv in cands:
        for e in exports:
            if e["serial_off"] <= lo < e["serial_off"] + max(e["serial_size"], 1):
                return e, conv
    return None, None


def main():
    hist = collections.Counter()
    rows = []
    with pakread.Pak() as pk:
        maps = sorted(p for p in pk.entries if p.lower().endswith(".umap"))
        for m in maps:
            try:
                ua = pk.payload(m)
                ue = m[:-len(".umap")] + ".uexp"
                ux = pk.payload(ue) if ue in pk.entries else b""
                d = ua + ux
                info = blueprints.parse(d)
                names = info["names"]
            except Exception as ex:
                continue
            ni = None
            for i, s in enumerate(names):
                if s == "GUIDs":
                    ni = i
                    break
            if ni is None:
                continue
            imports, exports = tables(d, info)
            needle = struct.pack("<I", ni) + struct.pack("<I", 0)
            pos = 0
            while True:
                q = d.find(needle, pos)
                if q < 0 or q + 40 > len(d):
                    break
                pos = q + 1
                ti = struct.unpack_from("<I", d, q + 8)[0]
                tn = _name(names, ti)
                size = struct.unpack_from("<i", d, q + 16)[0]
                e, conv = owner_of(q, exports, len(ua), len(ux))
                cls = resolve_name(e["class_idx"], imports, exports) if e else None
                nm = e["name"] if e else None
                n = None
                if tn == "ArrayProperty" and size > 53:
                    n = (size - 53) // 16
                hist[(tn, n, cls)] += 1
                if n and n > 1:
                    rows.append((m, q, n, cls, nm, conv, size))
    print("=== GUIDs headers by (type, count, owner class) ===")
    for (tn, n, cls), c in hist.most_common(40):
        print("  %-16s n=%-3s class=%-36s count=%d" % (tn, n, cls, c))
    print("\n=== multi-GUID placements ===")
    for r in rows:
        print("  %s  off=%X  n=%d  class=%s  obj=%s  conv=%s  size=%d" % r)
    print("\nmulti-guid rows:", len(rows))
    bycls = collections.Counter(r[3] for r in rows)
    print("by class:", bycls)


if __name__ == "__main__":
    main()
