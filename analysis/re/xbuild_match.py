"""Cross-image signature matcher.

DollsMP ships targets.h for DollsNest-Win64-Shipping.exe (UE 4.27.2) where every
target carries its first 16 image bytes.  Queen's Domain is the same engine line,
so an *engine* function should begin with the same bytes even though the link
order (and therefore the RVA) differs.

We match only at .pdata function starts. That is the strong form: a 16-byte prefix
that occurs at a function entry is overwhelmingly unlikely to be coincidence.

Outputs a table:  target -> QD function RVA (or NO MATCH), plus whole-.text hit count
so an ambiguous match is visible rather than silently accepted.
"""
import re, sys, struct, json, os
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
import qdlib as q

DOLLS_TARGETS = r"G:\Freespace\dollsMP\mod\native\src\targets.h"


def parse_targets(path):
    src = open(path, encoding="utf-8", errors="replace").read()
    rx = re.compile(
        r'\{\s*"([^"]+)"\s*,\s*0x([0-9A-Fa-f]+)\s*,\s*"([^"]*)"\s*,\s*([01])\s*,\s*\{([^}]*)\}',
        re.S)
    out = []
    for m in rx.finditer(src):
        name, rva, role, check, body = m.groups()
        bs = bytes(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", body))
        out.append(dict(name=name, dolls_rva=int(rva, 16), role=role,
                        check=int(check), expect=bs))
    return out


def section_bytes(name):
    s = q.sec_by_name(name)
    n, va, vsz, raw, rawsz, ch = s
    d = q.data()
    return va, d[raw:raw + min(vsz, rawsz)]


def main():
    targets = parse_targets(DOLLS_TARGETS)
    # function-start index: first 16 bytes -> [rvas]
    starts = {}
    d = q.data()
    for b, e, u in q.functions():
        o = q.rva_to_off(b)
        if o is None:
            continue
        starts.setdefault(d[o:o + 16], []).append(b)

    va, text = section_bytes(".text")
    print("QD function starts: %d   distinct 16-byte prefixes: %d" % (len(q.functions()), len(starts)))
    print()
    results = []
    for t in targets:
        pat = t["expect"]
        if len(pat) < 16:
            print("%-58s  (short pattern)" % t["name"])
            continue
        hits = starts.get(pat, [])
        whole = text.count(pat)
        exact_only = [h for h in hits if h == t["dolls_rva"]]  # never true across images
        status = "MATCH" if hits else "no-match"
        note = ""
        if hits and len(hits) > 1:
            note = "  *%d starts*" % len(hits)
        results.append(dict(name=t["name"], role=t["role"], dolls_rva=t["dolls_rva"],
                            qd_rvas=hits, text_hits=whole))
        print("%-58s %-6s qd=%s  text_hits=%d%s" % (
            t["name"], status,
            " ".join("0x%08X" % h for h in hits) or "-", whole, note))
    with open(os.path.join(os.path.dirname(__file__), "xbuild_match.json"), "w") as f:
        json.dump(results, f, indent=1)
    print("\nwrote xbuild_match.json")


if __name__ == "__main__":
    main()
