"""Draft: which placed actors reference which NSD keys / quest ids.

For every target actor export, scan its serialized bytes for the FName index of
a known NamedSaveData key (FName = {index, number}, number 0 or 1). This is a
pointer-free scan: cooked property values store the package-local name index.
"""
import sys, struct, collections, json, os
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\pak\code")
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
import pakread, blueprints
from _d4_multiguid_owner import tables, resolve_name

# known keys: challenge NSD keys + save/progression literals
KEYS = set()
chal = os.path.join(r"G:\Freespace\Queens_Domain\analysis\re", "_challenge_full.txt")
import re as _re
for line in open(chal, encoding="utf-8"):
    mm = _re.search(r"nsd=\[(.*?)\] \| inv=", line)
    if not mm:
        continue
    for m in _re.finditer(r"\('([A-Za-z0-9_]+)',", mm.group(1)):
        k = m.group(1)
        if k not in ("None", "TagName"):
            KEYS.add(k)
KEYS |= set("""Arjaot_State RustySwordKnightState RustySwordKnightGaveSword BrokeBarricade
TiniScytheHammerAP IntroductionSeen IntroMusicState PlayingMusicTrack EndingSeen
CheckpointUnlockCount FullEnergyCellsCount TotalAchievementTreasures
TotalAchievementWeapons TotalAchievementThrowables TotalAchievementAccessories
TotalWeaponsUpgraded CompletedChallenges TeleporterUnlockCount TotalChestsOpened
TotalSecretDoorsOpened FishWifeTotalEggs HasEverInscribedGrimoire LibBookPuzzleComplete
ROSBookPuzzleComplete LibrarianBestiaryCompletionPercent MealTicketsCollected
RaiderNeckacesTurnedIn ScientistDataGiven ShopAmountSpent ShopUnlockFinished
ShopkeepUnlock1 ShopkeepUnlock2 ShopkeepUnlock3 ShopkeepUnlock4
StoneGardenPuzzleFinished BridgeLordDead DragonLordDead RedLordsDead ROSDragonDead
GiantSwampFish_IsDead NecronimiconDefeated DemonBossesKilled WhaleKingIsDead
NecroWall00Destroyed NecroWall01Destroyed NecroWall02Destroyed NecroWall03Destroyed
NecroWall04Destroyed SR_GMS_Acquired SR_GreenKey_Acquired SR_RustedLeafBlade_Acquired
SR_Key_Blue_Acquired SR_Rusted_Greatsword_Acquired""".split())

TARGETS = ["ActorQuestState", "ActivatableNamedSaveDataSetter", "NamedSaveDataSetterVolume",
           "QuestForceUpdateVolume", "ActivatableGroup", "WorldEventReceiver",
           "ActivationTrigger"]


def main():
    maps_filter = sys.argv[1:]
    with pakread.Pak() as pk:
        maps = sorted(p for p in pk.entries if p.lower().endswith(".umap"))
        for m in maps:
            if maps_filter and not any(f in m for f in maps_filter):
                continue
            try:
                ua = pk.payload(m)
                ue = m[:-5] + ".uexp"
                ux = pk.payload(ue) if ue in pk.entries else b""
                d = ua + ux
                info = blueprints.parse(d)
                names = info["names"]
                imports, exports = tables(d, info)
            except Exception as e:
                continue
            idx = {}
            for i, s in enumerate(names):
                idx.setdefault(s, i)
            # key -> FName needle(s)
            needles = []
            for k in KEYS:
                if k in idx:
                    needles.append((k, struct.pack("<I", idx[k]) + struct.pack("<I", 0)))
            for e in exports:
                cls = resolve_name(e["class_idx"], imports, exports) or ""
                if not any(t in cls for t in TARGETS):
                    continue
                nm = e["name"]
                if nm.startswith("Default__"):
                    continue
                lo, sz = e["serial_off"], e["serial_size"]
                if sz <= 0 or lo < 0 or lo + sz > len(d):
                    continue
                blob = d[lo:lo + sz]
                hits = []
                for k, pat in needles:
                    if pat in blob:
                        hits.append(k)
                if hits:
                    print("%-24s %-34s %-30s %s" % (m.rsplit("/", 1)[-1], cls, nm, ",".join(sorted(hits))))


if __name__ == "__main__":
    main()
