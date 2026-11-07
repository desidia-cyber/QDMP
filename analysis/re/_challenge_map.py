"""Decode FS_ChallengeData.ChallengeDefinitions.

Each entry is a per-property-tagged FS_ChallengeData. The property tag in this
build is 24 bytes + a 1-byte property-GUID flag; a NameProperty value follows at
+25. An NSDCondition is a Map<Name,int32> whose count is at tag+45 and whose
entries are {FName key, int32 value} at 12-byte stride.
"""
import sys, struct
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\pak\code")
import pakread, blueprints

P = "QueensDomain/Content/Blueprints/Challenges/FS_ChallengeData.uasset"


def main():
    with pakread.Pak() as pk:
        ua = pk.payload(P)
        ue = P[:-7] + ".uexp"
        ux = pk.payload(ue) if ue in pk.entries else b""
        d = ua + ux
        info = blueprints.parse(d)
        names = info["names"]
        idx = {}
        for i, s in enumerate(names):
            idx.setdefault(s, i)

        def nm(i): return names[i] if 0 <= i < len(names) else "?%d" % i

        q = d.find(struct.pack("<I", idx["ChallengeDefinitions"]) + struct.pack("<I", 0))

        def occ(name):
            pat = struct.pack("<I", idx[name]) + struct.pack("<I", 0)
            out = []; pos = q; end = q + 34000
            while True:
                j = d.find(pat, pos)
                if j < 0 or j > end:
                    break
                out.append(j); pos = j + 1
            return out

        ids = occ("ID")
        nsds = occ("NSDCondition")
        invs = occ("InventoryCondition")
        tags = occ("ItemTagCondition")
        modes = occ("CompleteConditionMode")

        def nameval(j): return nm(struct.unpack_from("<i", d, j + 25)[0])

        def mapval(j, h=45):
            cnt = struct.unpack_from("<i", d, j + h)[0]
            out = []
            for e in range(min(max(cnt, 0), 16)):
                base = j + h + 4 + e * 12
                ki = struct.unpack_from("<i", d, base)[0]
                vi = struct.unpack_from("<i", d, base + 8)[0]
                out.append((nm(ki), vi))
            return out

        n = min(len(ids), len(nsds), len(invs), len(tags), len(modes))
        print("# id | mode | NSD conditions | inventory | item-tag")
        for i in range(n):
            print("%2d | %-30s | nsd=%s | inv=%s | tag=%s" %
                  (i, nameval(ids[i]), mapval(nsds[i], 45),
                   mapval(invs[i], 45), mapval(tags[i], 45)))
        print("\ncounts:", len(ids), len(nsds), len(invs), len(tags), len(modes))


if __name__ == "__main__":
    main()
