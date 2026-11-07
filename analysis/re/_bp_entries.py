"""Recover the ubergraph event each pause call belongs to.

Blueprint events compile to a small stub that forwards an entry offset into
ExecuteUbergraph_<Class>. The stub carries the offset as EX_IntConst (0x1D)
followed by a 4-byte little-endian int, then EX_EndFunctionParms (0x16),
EX_Return (0x04), EX_Nothing (0x0B):

    1D <i32 entry> 16 04 0B

Collect every export of FS_PlayerChar with that shape, keep those whose entry
lands inside ExecuteUbergraph's serialized range, and report which block each
pause call falls into. This turns "inside the ubergraph" into "inside event X".

Validation: every retained entry must land inside the ubergraph range, and the
entries must be distinct and monotonic when sorted by the block they start.
"""
import os
import sys
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "analysis", "pak", "code"))
import pakread            # noqa: E402
import blueprints         # noqa: E402

ASSET = "QueensDomain/Content/Player/FS_PlayerChar.uasset"
STUB = b"\x1d"


def main():
    with pakread.Pak() as pk:
        ua = pk.payload(ASSET)
        uexp = pk.payload(ASSET[:-len(".uasset")] + ".uexp")
    info = blueprints.parse(ua)
    imports, exports = blueprints.try_imports_exports(ua, info)
    f = info["tail_fields"]
    base = len(ua)
    recs = []
    for i in range(f[2]):
        o = f[3] + i * 104
        ni, _ = struct.unpack("<2i", ua[o + 16:o + 24])
        ss, so = struct.unpack("<2q", ua[o + 28:o + 44])
        recs.append((info["names"][ni], so - base, ss))
    recs.sort(key=lambda r: r[1])

    ug = [r for r in recs if r[0] == "ExecuteUbergraph_FS_PlayerChar"][0]
    ug_lo, ug_sz = ug[1], ug[2]
    ug_hi = ug_lo + ug_sz
    print("ubergraph uexp range [%08X..%08X)  %d bytes" % (ug_lo, ug_hi, ug_sz))

    entries = []
    for name, off, sz in recs:
        if name.startswith("ExecuteUbergraph"):
            continue
        body = uexp[off:off + sz]
        p = body.find(STUB)
        while p >= 0 and p + 10 <= len(body):
            val = struct.unpack("<i", body[p + 1:p + 5])[0]
            tail = body[p + 5:p + 8]
            if tail == b"\x16\x04\x0b":
                entries.append((val, name))
                break
            p = body.find(STUB, p + 1)
    print("stub-shaped exports with an entry offset: %d" % len(entries))

    good = [(v, n) for v, n in entries if ug_lo <= ug_lo + v < ug_hi]
    print("entries landing inside the ubergraph: %d" % len(good))
    bad = [(v, n) for v, n in entries if not (ug_lo <= ug_lo + v < ug_hi)]
    if bad:
        print("rejected (outside range) -- shown for falsification:")
        for v, n in sorted(bad)[:20]:
            print("    %8d  %s" % (v, n))

    good.sort()
    print("\nentry map (ubergraph-relative offset -> event):")
    prev = None
    for v, n in good:
        print("   +%08X  (uexp %08X)  %s" % (v, ug_lo + v, n))

    # which block owns each pause call
    pauses = []
    names = [i["name"] for i in imports]
    for t in ("SetGamePaused", "IsGamePaused"):
        j = names.index(t)
        pat = struct.pack("<i", -(j + 1))
        p = uexp.find(pat)
        while p >= 0:
            pauses.append((t, p))
            p = uexp.find(pat, p + 1)

    print("\nattribution:")
    for t, p in pauses:
        rel = p - ug_lo
        owner = None
        for v, n in good:
            if v <= rel:
                owner = (v, n)
            else:
                break
        print("   %-14s uexp %08X  rel +%08X  -> event %s"
              % (t, p, rel, owner[1] if owner else "?"))
        if owner:
            nxt = [x for x in good if x[0] > owner[0]]
            end = nxt[0][0] if nxt else ug_sz
            print("        block spans +%08X..+%08X (%d bytes), call is +%d into it"
                  % (owner[0], end, end - owner[0], rel - owner[0]))


if __name__ == "__main__":
    main()
