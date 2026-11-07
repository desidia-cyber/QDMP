"""Scan every cooked package for Blueprint use of the pause/time-dilation nodes.

A Blueprint that calls UGameplayStatics::SetGamePaused imports it, so the name
appears in the package name table. We first do a raw payload substring screen
(fast), then confirm hits by parsing the name table.
"""
import sys, os, time
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\pak\code")
import pakread
import blueprints

NEEDLES = [b"SetGamePaused", b"SetGlobalTimeDilation", b"IsGamePaused",
           b"SetPause", b"GetGlobalTimeDilation", b"SetTickableWhenPaused"]

def main():
    t0 = time.time()
    pk = pakread.Pak()
    paths = [p for p in pk.entries if p.lower().endswith((".uasset", ".umap"))]
    print("packages:", len(paths))
    hits = {}
    scanned = 0
    for i, p in enumerate(paths):
        try:
            d = pk.payload(p)
        except Exception:
            continue
        scanned += 1
        found = [n.decode() for n in NEEDLES if n in d]
        if found:
            hits[p] = found
        if i % 5000 == 0 and i:
            print("  ... %d/%d  hits=%d  %.1fs" % (i, len(paths), len(hits), time.time() - t0))
    print("\nscanned %d packages in %.1fs; %d with a pause-name hit" %
          (scanned, time.time() - t0, len(hits)))
    for p in sorted(hits):
        # confirm via the name table
        try:
            info = blueprints.parse(pk.payload(p))
            names = set(info["names"])
            conf = [n for n in hits[p] if n in names]
            imp, exp = blueprints.try_imports_exports(pk.payload(p), info)
            imp_hits = [(x["class"], x["name"]) for x in imp
                        if x["name"] in ("SetGamePaused", "SetGlobalTimeDilation",
                                         "IsGamePaused", "SetPause",
                                         "GetGlobalTimeDilation", "SetTickableWhenPaused")]
            print("\n%s" % p)
            print("   raw hits   : %s" % hits[p])
            print("   name-table : %s" % conf)
            print("   imports    : %s" % imp_hits[:10])
        except Exception as e:
            print("\n%s\n   raw hits %s ; parse failed %s" % (p, hits[p], e))
    pk.close()

if __name__ == "__main__":
    main()
