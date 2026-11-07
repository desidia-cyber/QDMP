"""Reward-item and ending-gating map.

- For each dialogue action asset, extract the `/Game/Items/...` soft paths it
  grants (the reward) plus any known NSD key it touches.
- For the ending assets/maps, find the writers of `EndingSeen` / `ending1..4`.
"""
import sys, os, re
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\pak\code")
import pakread, blueprints

ACTIONS = """Knight_GiveSword_Action Knight_Budaen_GiveKey_Action Scientist_GiveOfficeKey_Action
Raider_GiveNecklaceAction Raider_GiveKeyAction DesJosamiGiveKey NPC_GiveNextReward
Ghost_GiveItem Chef_TurnInAction FishWife_TurnInAction Scientist_TurninAction
SecretSkullDoor_TurnInAction Chef_CheckForReward Chef_Do_PQ_CrowCaw Chef_Do_Ghost_CrowCaw
FishWife_PostTurnIn Knight_Nu_DieAction Raider_TurnInNecklace Merchant_OpenShopScreen
Merchant_CheckAbleToSell Merchant_CheckForDucks Merchant_CheckForBoughtALot
GaveSwordGuideGiver Librarian_GreetSelector Scientist_SelectTurnedInDialog
Scientist_TurnInDialogue SecretSkullDoor_CheckForReward""".split()

ENDING_ROOTS = ("Cutscenes/Ending", "Maps/800_FinalZone", "Enemies/Queen",
                "NPCs/Sipeha", "Player/FS_PlayerChar", "Blueprints/Challenges/ending")


def runs(b):
    out = []; cur = []; s = 0
    for i, x in enumerate(b):
        if 32 <= x < 127:
            if not cur:
                s = i
            cur.append(chr(x))
        else:
            if len(cur) >= 4:
                out.append("".join(cur))
            cur = []
    if len(cur) >= 4:
        out.append("".join(cur))
    return out


def main():
    with pakread.Pak() as pk:
        entries = sorted(pk.entries)
        by_base = {}
        for p in entries:
            if p.endswith(".uasset"):
                by_base.setdefault(p.rsplit("/", 1)[-1][:-7], []).append(p)

        print("=== action assets: item grants / keys ===")
        for a in ACTIONS:
            paths = by_base.get(a, [])
            for p in paths:
                try:
                    ua = pk.payload(p)
                    ue = p[:-7] + ".uexp"
                    ux = pk.payload(ue) if ue in pk.entries else b""
                    d = ua + ux
                    info = blueprints.parse(d)
                except Exception:
                    continue
                rs = runs(d)
                items = sorted(set(r for r in rs if r.startswith("/Game/Items/")))
                keys = sorted(set(r for r in rs if r in (
                    "Arjaot_State", "RustySwordKnightState", "RustySwordKnightGaveSword",
                    "BrokeBarricade", "RaiderNeckacesTurnedIn", "MealTicketsCollected",
                    "ScientistDataGiven", "ShopAmountSpent")))
                print("  %-34s %-62s items=%s keys=%s" %
                      (a, p.split("Content/", 1)[-1], items[:8], keys))

        print("\n=== ending gating ===")
        for p in entries:
            if not p.endswith((".uasset", ".umap", ".uexp")):
                continue
            if not any(root.lower() in p.lower() for root in ENDING_ROOTS):
                continue
            try:
                d = pk.payload(p)
            except Exception:
                continue
            rs = runs(d)
            hits = sorted(set(r for r in rs if re.search(r"(?i)^ending[1-4]$|EndingSeen|Ending[1-4]Cutscene|EndingChoice", r)))
            if hits:
                print("  %-72s %s" % (p.split("Content/", 1)[-1], hits))


if __name__ == "__main__":
    main()
