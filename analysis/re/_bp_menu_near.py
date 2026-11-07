"""Which menus does the player blueprint reference, and how close to a pause call?

FS_PlayerChar contains exactly two SetGamePaused references. If a menu-open path in
the same package is far from both, that path contains no pause call -- which is a
usable *negative*: the menu opens without touching the world pause.

The ubergraph is one merged blob, so distance is a proximity signal only, not
proof of enclosure. It is reported as such.
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
LIKE = ("Menu", "Journal", "Inventory", "Shop", "Map", "Bestiary", "Note",
        "Option", "Save", "Inspect", "Equip", "Status", "Challeng", "DieUI",
        "ReadText", "Dialogue", "Photo")


def main():
    with pakread.Pak() as pk:
        ua = pk.payload(ASSET)
        uexp = pk.payload(ASSET[:-len(".uasset")] + ".uexp")
    info = blueprints.parse(ua)
    imports, exports = blueprints.try_imports_exports(ua, info)
    f = info["tail_fields"]
    recs = []
    for i in range(f[2]):
        o = f[3] + i * 104
        ni, _ = struct.unpack("<2i", ua[o + 16:o + 24])
        ss, so = struct.unpack("<2q", ua[o + 28:o + 44])
        recs.append((info["names"][ni], so, ss))
    base = len(ua)

    def owner(off):
        for n, so, ss in recs:
            if so <= off < so + ss:
                return n
        return None

    names = [i["name"] for i in imports]
    # pause call offsets
    pauses = []
    for t in ("SetGamePaused", "IsGamePaused"):
        j = names.index(t)
        pat = struct.pack("<i", -(j + 1))
        p = uexp.find(pat)
        while p >= 0:
            pauses.append((t, p))
            p = uexp.find(pat, p + 1)
    print("pause-related call sites:")
    for t, p in pauses:
        print("   %-14s @%08X  export=%s" % (t, p, owner(p + base)))

    print("\nwidget/menu class references in the same package:")
    rows = []
    for j, nm in enumerate(names):
        if not (nm.endswith("_C") or any(k in nm for k in LIKE)):
            continue
        pat = struct.pack("<i", -(j + 1))
        p = uexp.find(pat)
        while p >= 0:
            d = min(abs(p - q) for _, q in pauses) if pauses else -1
            rows.append((d, p, nm, owner(p + base)))
            p = uexp.find(pat, p + 1)
    rows.sort()
    for d, p, nm, own in rows[:45]:
        print("   dist=%6d  @%08X  %-46s in %s" % (d, p, nm, own))
    print("\n   (%d total reference sites)" % len(rows))


if __name__ == "__main__":
    main()
