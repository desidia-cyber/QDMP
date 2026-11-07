"""Full decode of FS_ChallengeData.ChallengeDefinitions.

Per entry: ID (Name), NSDCondition (Map<Name,int>), InventoryCondition
(Map<FPackageIndex,int>), ItemTagCondition (Map<FName,int>), and
CompleteConditionMode (EnumProperty value).

Tag layout in this build: 24-byte FPropertyTag + 1 property-GUID byte; a
NameProperty value follows at +25; a MapProperty count is at +45 and entries
follow at +49. Object keys are cooked FPackageIndex int32 values (negative =
import, positive = export).
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
        imports, exports = blueprints.try_imports_exports(d, info)
        names = info["names"]
        idx = {}
        for i, s in enumerate(names):
            idx.setdefault(s, i)

        def nm(i): return names[i] if 0 <= i < len(names) else "?%d" % i

        def pkg(idxval):
            if idxval < 0:
                k = -idxval - 1
                return imports[k]["name"] if 0 <= k < len(imports) else "imp%d" % idxval
            if idxval > 0:
                k = idxval - 1
                return exports[k]["name"] if 0 <= k < len(exports) else "exp%d" % idxval
            return "None"

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

        def nmap(j, stride, keykind):
            cnt = struct.unpack_from("<i", d, j + 45)[0]
            out = []
            for e in range(min(max(cnt, 0), 32)):
                base = j + 49 + e * stride
                if keykind == "fname":
                    ki = struct.unpack_from("<i", d, base)[0]
                    vi = struct.unpack_from("<i", d, base + 8)[0]
                    out.append((nm(ki), vi))
                else:
                    ki = struct.unpack_from("<i", d, base)[0]
                    vi = struct.unpack_from("<i", d, base + 4)[0]
                    out.append((pkg(ki), vi))
            return out

        # ItemTagCondition: the map key is a GameplayTag struct whose FName is
        # one of the ItemTags.* values; find it by index inside the tag block.
        tag_idx = {idx[k]: k for k in idx if k.startswith("ItemTags.")}

        def itemtag(j):
            out = []
            for off in range(j, j + 240):
                v = struct.unpack_from("<i", d, off)[0]
                if v in tag_idx:
                    out.append(tag_idx[v])
            return sorted(set(out))

        n = min(len(ids), len(nsds), len(invs), len(tags), len(modes))
        for i in range(n):
            # CompleteConditionMode: enum name FName at +24, value byte at +32
            enum_idx = struct.unpack_from("<i", d, modes[i] + 24)[0]
            enum_val = d[modes[i] + 32] if modes[i] + 32 < len(d) else -1
            print("%2d | %-30s | mode=%s(%d) | nsd=%s | inv=%s | tag=%s" % (
                i, nameval(ids[i]), nm(enum_idx), enum_val,
                nmap(nsds[i], 12, "fname"),
                nmap(invs[i], 8, "pkg"),
                itemtag(tags[i])))


if __name__ == "__main__":
    main()
