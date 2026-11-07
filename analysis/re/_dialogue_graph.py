"""Dialogue-side trigger graph: which NPC dialogue writes which NSD key and
runs which action (reward).

For each FS_DialogueData asset:
  - scan the .uexp for the FName index of every known NSD key (the
    NamedSaveDataValueChanges keys are stored as FNames),
  - read the `ActionToRun` / `ActionToRunAtEnd` object references
    (ObjectProperty value = cooked FPackageIndex at tag+25).
"""
import sys, struct, os, re
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\pak\code")
import pakread, blueprints

CHAL = r"G:\Freespace\Queens_Domain\analysis\re\_challenge_full.txt"


def known_keys():
    keys = set()
    for line in open(CHAL, encoding="utf-8"):
        for mm in re.finditer(r"nsd=\[(.*?)\] \| inv=", line):
            for m in re.finditer(r"\('([A-Za-z0-9_]+)',", mm.group(1)):
                keys.add(m.group(1))
        for mm in re.finditer(r"inv=\[(.*?)\] \| tag=", line):
            for m in re.finditer(r"\('([A-Za-z0-9_./-]+)',", mm.group(1)):
                keys.add(m.group(1))
    keys |= set("""Arjaot_State RustySwordKnightState RustySwordKnightGaveSword BrokeBarricade
TiniScytheHammerAP IntroductionSeen EndingSeen CheckpointUnlockCount
TotalAchievementTreasures TotalAchievementWeapons FullEnergyCellsCount
ShopkeepUnlock1 ShopkeepUnlock2 ShopkeepUnlock3 ShopkeepUnlock4
ShopUnlockFinished StoneGardenPuzzleFinished""".split())
    return keys


def main():
    keys = known_keys()
    with pakread.Pak() as pk:
        paths = sorted(p for p in pk.entries
                       if p.lower().endswith(".uasset") and "dialogue" in p.lower())
        found = 0
        for p in paths:
            try:
                ua = pk.payload(p)
                ue = p[:-7] + ".uexp"
                ux = pk.payload(ue) if ue in pk.entries else b""
                d = ua + ux
                info = blueprints.parse(d)
                imp, exp = blueprints.try_imports_exports(d, info)
            except Exception:
                continue
            cls = (exp[0]["class"] if exp else "") or ""
            if cls != "FS_DialogueData":
                continue
            found += 1
            names = info["names"]
            idx = {}
            for i, s in enumerate(names):
                idx.setdefault(s, i)
            def nm(i): return names[i] if 0 <= i < len(names) else "?%d" % i

            def pkg(v):
                if v < 0:
                    k = -v - 1
                    return imp[k]["name"] if 0 <= k < len(imp) else "imp%d" % v
                if v > 0:
                    k = v - 1
                    return exp[k]["name"] if 0 <= k < len(exp) else "exp%d" % v
                return "None"

            blob = d
            hits = []
            for k in keys:
                if k in idx:
                    pat = struct.pack("<I", idx[k]) + struct.pack("<I", 0)
                    if pat in blob:
                        hits.append(k)
            actions = []
            for prop in ("ActionToRun", "ActionToRunAtEnd", "OptionalDialogueToRunAfter"):
                pi = idx.get(prop)
                if pi is None:
                    continue
                pat = struct.pack("<I", pi) + struct.pack("<I", 0)
                pos = 0
                while True:
                    j = blob.find(pat, pos)
                    if j < 0:
                        break
                    pos = j + 1
                    ti = struct.unpack_from("<i", blob, j + 8)[0]
                    if 0 <= ti < len(names) and names[ti] == "ObjectProperty":
                        v = struct.unpack_from("<i", blob, j + 25)[0]
                        actions.append(pkg(v))
            if hits or actions:
                print("%-70s keys=%s actions=%s" % (p.split("Content/", 1)[-1], sorted(hits), actions))
        print("\nFS_DialogueData assets:", found)


if __name__ == "__main__":
    main()
