"""Build a full rip-relative xref index over .text -> any section, cached to disk.

Slower than the .data-only index but lets us follow reflection name strings
(.rdata) to the code that registers or uses them.
"""
import sys, os, marshal, time
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
import qdlib as q

OUT = os.path.join(os.path.dirname(__file__), "xrefs_all.bin")

if __name__ == "__main__":
    t = time.time()
    idx = q.build_rip_xref_index()
    with open(OUT, "wb") as f:
        marshal.dump(idx, f)
    print("entries:", len(idx), "elapsed %.1fs" % (time.time() - t))
    print("wrote", OUT, os.path.getsize(OUT))
