"""Read the key written by every placed NamedSaveDataSetter/Activatable setter.

Map actor exports are tagged property streams. A property tag is
{FName name, FName type, int32 size, int32 arrayIndex, uint8 hasGuid, ...};
a NameProperty value follows at +25. We find the `NamedSaveData` / `NSDs` tags
inside each setter export and print the key they carry.
"""
import sys, struct
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\pak\code")
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
import pakread, blueprints
from _d4_multiguid_owner import tables, resolve_name

WANT = ["NamedSaveDataSetterVolume", "ActivatableNamedSaveDataSetter",
        "QuestForceUpdateVolume"]


def main():
    with pakread.Pak() as pk:
        maps = sorted(p for p in pk.entries if p.lower().endswith(".umap"))
        for m in maps:
            try:
                ua = pk.payload(m)
                ue = m[:-5] + ".uexp"
                ux = pk.payload(ue) if ue in pk.entries else b""
                d = ua + ux
                info = blueprints.parse(d)
                names = info["names"]
                imports, exports = tables(d, info)
            except Exception:
                continue
            idx = {}
            for i, s in enumerate(names):
                idx.setdefault(s, i)
            def nm(i): return names[i] if 0 <= i < len(names) else "?%d" % i
            type_prefix = tuple(k for k in idx if k.endswith("Property"))
            for e in exports:
                cls = resolve_name(e["class_idx"], imports, exports) or ""
                if not any(t in cls for w in WANT for t in (w,)):
                    continue
                nmn = e["name"]
                if nmn.startswith("Default__"):
                    continue
                lo, sz = e["serial_off"], e["serial_size"]
                if sz <= 0 or lo < 0 or lo + sz > len(d):
                    continue
                blob = d[lo:lo + sz]
                props = []
                for off in range(0, max(0, len(blob) - 16), 4):
                    ni = struct.unpack_from("<i", blob, off)[0]
                    ti = struct.unpack_from("<i", blob, off + 8)[0]
                    if 0 <= ni < len(names) and 0 <= ti < len(names) and names[ti].endswith("Property"):
                        props.append((off, names[ni], names[ti]))
                print("== %s %s %s @%s size=%d" % (m.rsplit("/", 1)[-1], cls, nmn, hex(lo), sz))
                for off, pn, pt in props:
                    line = "   +%-4X %-34s %-16s" % (off, pn, pt)
                    # NameProperty value at +25 (relative to the tag)
                    if pt == "NameProperty":
                        vi = struct.unpack_from("<i", blob, off + 25)[0] if off + 29 <= len(blob) else -1
                        line += " = " + nm(vi)
                    elif pt in ("ArrayProperty", "MapProperty"):
                        line += " raw=" + blob[off + 16:off + 40].hex(" ")
                    print(line)


if __name__ == "__main__":
    main()
