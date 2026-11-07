"""Build the per-quest trigger graph.

For every placed setter/quest actor:
  - `NSDs` is a Map<FName, NamedSaveDataSetter_Config>; the tag (MapProperty)
    carries the key at value+4; count is at tag+53, keys at tag+57 with stride
    8 + cfg where cfg=(size-4-8*count)/count.
  - `NamedSaveData` is a NameProperty; value at tag+25.
  - `States` is an ArrayProperty of Names; count at tag+33, values at tag+37.
Also lists the placed `ActorQuestState` names (the state vocabulary).
"""
import sys, struct, collections
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\pak\code")
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
import pakread, blueprints
from _d4_multiguid_owner import tables, resolve_name

SETTERS = ("NamedSaveDataSetterVolume", "ActivatableNamedSaveDataSetter")
OTHER = ("ActorQuestState", "QuestForceUpdateVolume", "ActivatableGroup")


def tags_in(blob, names, idx):
    """Yield (off, name, type, size) for byte-aligned property tags."""
    n = len(names)
    for off in range(0, max(0, len(blob) - 20)):
        ni = struct.unpack_from("<i", blob, off)[0]
        ti = struct.unpack_from("<i", blob, off + 8)[0]
        if 0 <= ni < n and 0 <= ti < n and names[ti].endswith("Property"):
            yield off, names[ni], names[ti], struct.unpack_from("<i", blob, off + 16)[0]


def main():
    state_names = []
    rows = []
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
            for e in exports:
                cls = resolve_name(e["class_idx"], imports, exports) or ""
                if not any(t in cls for t in SETTERS + OTHER):
                    continue
                nmn = e["name"]
                if nmn.startswith("Default__"):
                    continue
                lo, sz = e["serial_off"], e["serial_size"]
                if sz <= 0 or lo < 0 or lo + sz > len(d):
                    continue
                b = d[lo:lo + sz]
                keys = []
                states = []
                for off, pn, pt, psz in tags_in(b, names, idx):
                    if pn == "NamedSaveData" and pt == "NameProperty":
                        vi = struct.unpack_from("<i", b, off + 25)[0]
                        keys.append(nm(vi))
                    elif pn == "NSDs" and pt == "MapProperty" and off + 57 <= len(b):
                        cnt = struct.unpack_from("<i", b, off + 53)[0]
                        if 1 <= cnt <= 64:
                            cfg = (psz - 4 - 8 * cnt) // cnt
                            for ee in range(cnt):
                                ki = struct.unpack_from("<i", b, off + 57 + ee * (8 + cfg))[0]
                                keys.append(nm(ki))
                    elif pn == "States" and pt == "ArrayProperty" and off + 37 <= len(b):
                        cnt = struct.unpack_from("<i", b, off + 33)[0]
                        if 0 <= cnt <= 64:
                            for ee in range(cnt):
                                si = struct.unpack_from("<i", b, off + 37 + ee * 8)[0]
                                states.append(nm(si))
                if "ActorQuestState" in cls:
                    state_names.append((m.rsplit("/", 1)[-1], nmn, states))
                if keys or states:
                    rows.append((m.rsplit("/", 1)[-1], cls, nmn, sorted(set(keys)), states))
    print("=== setters and quest actors with keys ===")
    for r in rows:
        print("%-24s %-34s %-34s keys=%s states=%s" % r)
    print("\n=== ActorQuestState names (state vocabulary) ===")
    for mp, nmn, st in state_names:
        print("  %-24s %-40s %s" % (mp, nmn, st))


if __name__ == "__main__":
    main()
