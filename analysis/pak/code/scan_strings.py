#!/usr/bin/env python3
"""Search every payload in the pak for literal byte patterns, and report which
asset each hit belongs to.

Payloads are plaintext, so a single sequential pass over the file is enough.
For each needle we record the owning entry (via the decoded offset table) and a
short ASCII context window.

    python scan_strings.py Arjaot_State RustyBrokeBarricade NetOwner Replicate
    python scan_strings.py --from-file needles.txt
"""
import bisect
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pakread  # noqa: E402

CHUNK = 8 << 20


def main(argv):
    needles = [a for a in argv[1:] if not a.startswith("--")]
    if not needles:
        print(__doc__)
        return 1
    with pakread.Pak() as pk:
        spans = sorted((off + pakread.ENTRY_HEADER_SIZE,
                        off + pakread.ENTRY_HEADER_SIZE + size, p)
                       for p, (off, size, _f) in pk.entries.items())
        idx = [s for s, _e, _p in spans]
        limit = pk.index_offset
        pats = [(n, n.encode("latin-1")) for n in needles]
        hits = {n: [] for n in needles}
        pos = 0
        f = pk.f
        overlap = max(len(b) for _, b in pats) - 1
        prev_tail = b""
        while pos < limit:
            f.seek(pos)
            buf = f.read(min(CHUNK, limit - pos))
            if not buf:
                break
            data = prev_tail + buf
            base = pos - len(prev_tail)
            for name, pat in pats:
                p0 = 0
                while True:
                    k = data.find(pat, p0)
                    if k < 0:
                        break
                    abs_off = base + k
                    i = bisect.bisect_right(idx, abs_off) - 1
                    owner = "<idx %d>" % abs_off
                    if 0 <= i < len(spans):
                        s, e, pp = spans[i]
                        if s <= abs_off < e:
                            owner = pp
                    ctx = data[max(0, k - 40):k + len(pat) + 40]
                    hits[name].append((abs_off, owner,
                                       "".join(chr(c) if 32 <= c < 127 else "." for c in ctx)))
                    p0 = k + 1
            prev_tail = data[-overlap:] if overlap else b""
            pos = base + len(data)

        for name in needles:
            hs = hits[name]
            print("\n=== %r  %d hit(s) ===" % (name, len(hs)))
            seen = {}
            for off, owner, ctx in hs:
                seen.setdefault(owner, []).append((off, ctx))
            for owner in sorted(seen):
                items = seen[owner]
                print("  %s   (%d hit%s)" % (owner, len(items), "" if len(items) == 1 else "s"))
                for off, ctx in items[:4]:
                    print("      0x%08x  %s" % (off, ctx))
                if len(items) > 4:
                    print("      ... %d more" % (len(items) - 4))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
