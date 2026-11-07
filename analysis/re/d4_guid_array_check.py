"""Does any placed actor serialize `GUIDs` as an ARRAY (multi-GUID)?

For every .umap, locate the name-table index of "GUIDs" and read the property
header that follows: the Type FName and the Size. Report a histogram. A singular
struct GUID appears as Type=StructProperty, Size=16; a TArray<FGuid> would appear
as Type=ArrayProperty with a larger/different size.
"""
import sys, struct, collections
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\pak\code")
import pakread, blueprints

def main():
    hist = collections.Counter()
    examples = {}
    with pakread.Pak() as pk:
        for m in sorted(p for p in pk.entries if p.lower().endswith(".umap")):
            try:
                d = pk.payload(m)
                ue = m[:-len(".umap")] + ".uexp"
                if ue in pk.entries:
                    d += pk.payload(ue)
                info = blueprints.parse(d)
                names = info["names"]
            except Exception:
                continue
            idx = {}
            for i, s in enumerate(names):
                idx.setdefault(s, i)
            ni = idx.get("GUIDs")
            if ni is None:
                continue
            needle = struct.pack("<I", ni) + struct.pack("<I", 0)
            pos = 0
            while True:
                q = d.find(needle, pos)
                if q < 0 or q + 40 > len(d):
                    break
                pos = q + 1
                ti = struct.unpack_from("<I", d, q + 8)[0]
                tn = names[ti] if 0 <= ti < len(names) else "?%d" % ti
                size = struct.unpack_from("<i", d, q + 16)[0]
                key = (tn, size)
                hist[key] += 1
                if key not in examples:
                    examples[key] = m.rsplit("/", 1)[-1]
    print("GUIDs property header histogram over all .umap:")
    for (tn, size), n in hist.most_common():
        print("  type=%-18s size=%-5d count=%-6d e.g. %s" % (tn, size, n, examples[(tn, size)]))
    arrays = [(k, n) for k, n in hist.items() if k[0] != "StructProperty"]
    print("\nnon-struct (possible array) headers:", arrays if arrays else "NONE")

if __name__ == "__main__":
    main()
