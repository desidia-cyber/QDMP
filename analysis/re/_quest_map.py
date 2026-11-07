"""Quest/progression map: challenge definitions + placed trigger census.

1. Decode FS_ChallengeData's `ChallengeDefinitions` array tag and, if the
   elements are per-property tagged (UserDefinedStruct), extract
   (ID, NSDCondition, CompleteConditionMode, InventoryCondition, ItemTagCondition)
   per challenge.
2. Count the placed quest/trigger/setter actors across all 51 maps.
"""
import sys, struct, collections
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\pak\code")
import pakread, blueprints

CHAL = "QueensDomain/Content/Blueprints/Challenges/FS_ChallengeData.uasset"
TARGETS = [
    "ActorQuestState", "ActorQuestStateManager", "QuestForceUpdateVolume",
    "ActivatableGroup", "NamedSaveDataSetterVolume", "ActivatableNamedSaveDataSetter",
    "WorldEventReceiver", "ActivationTrigger", "LevelManagerTrigger",
    "NotifyActivatables", "BreakableBarricade", "Door_Base", "Chest",
    "Teleporter_C", "Checkpoint",
]


def nameref(names, i):
    return names[i] if 0 <= i < len(names) else "<bad %d>" % i


def challenge_defs(pk):
    ua = pk.payload(CHAL)
    ue = CHAL[:-7] + ".uexp"
    ux = pk.payload(ue) if ue in pk.entries else b""
    d = ua + ux
    info = blueprints.parse(d)
    names = info["names"]
    idx = {}
    for i, s in enumerate(names):
        idx.setdefault(s, i)
    ni = idx.get("ChallengeDefinitions")
    print("names", len(names), "ChallengeDefinitions idx", ni)
    if ni is None:
        return
    needle = struct.pack("<I", ni) + struct.pack("<I", 0)
    pos = 0
    while True:
        q = d.find(needle, pos)
        if q < 0 or q + 48 > len(d):
            break
        pos = q + 1
        ti = struct.unpack_from("<i", d, q + 8)[0]
        size = struct.unpack_from("<i", d, q + 16)[0]
        ai = struct.unpack_from("<i", d, q + 20)[0]
        inner = struct.unpack_from("<i", d, q + 24)[0]
        st = struct.unpack_from("<i", d, q + 32)[0]
        print("tag @%X type=%s size=%d arrayidx=%d inner=%s struct=%s" %
              (q, nameref(names, ti), size, ai, nameref(names, inner), nameref(names, st)))
        val = q + 40
        cnt = struct.unpack_from("<i", d, val)[0]
        print("  count at +40 =", cnt, " first 96 bytes:", d[val:val + 96].hex(" "))
        return


def main():
    with pakread.Pak() as pk:
        challenge_defs(pk)

        counts = collections.Counter()
        per_class_maps = collections.defaultdict(set)
        maps = sorted(p for p in pk.entries if p.lower().endswith(".umap"))
        for m in maps:
            try:
                d = pk.payload(m)
                info = blueprints.parse(d)
                imp, exp = blueprints.try_imports_exports(d, info)
            except Exception:
                continue
            for e in exp or []:
                cls = e.get("class") or ""
                nm = e.get("name") or ""
                if nm.startswith("Default__"):
                    continue
                for t in TARGETS:
                    if t == "Teleporter_C":
                        ok = "Teleporter" in cls
                    elif t == "Checkpoint":
                        ok = cls.startswith("Checkpoint")
                    else:
                        ok = (t in cls)
                    if ok:
                        counts[t] += 1
                        per_class_maps[t].add(m)
                        break
        print("\n=== placed actors by class (all 51 maps) ===")
        for t in TARGETS:
            print("  %-32s %5d   in %d maps" % (t, counts[t], len(per_class_maps[t])))
        print("\n  (Teleporter_C is every Teleporter* class; Checkpoint is Checkpoint*)")


if __name__ == "__main__":
    main()
