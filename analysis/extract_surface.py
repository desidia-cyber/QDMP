#!/usr/bin/env python3
"""Pull the game-specific name surface out of the exe string pool, grouped."""
import json, os, re

d = os.path.dirname(os.path.abspath(__file__))
names = [l.strip() for l in open(os.path.join(d, "exe_names.txt"), encoding="utf-8") if l.strip()]
N = set(names)

GROUPS = {
    "quest_journal_progress": r"^(Journal|Note|Notes|Quest|Objective|Chapter|Progress|"
        r"Unlock|Achievement|Challenge|Trial|Reward|Discover)",
    "dialogue": r"^(Dialogue|DialogueSequence|DialogueAction|DialogueOptions|DialogueToJumpTo|"
        r"JumpToDialogue|Subtitle|Speech|EFS_Dialogue|S_Dialogue|OptionalDialogue|"
        r"PlayDialogue|SpawnDialogue)",
    "enemy_ai": r"^(Enemy|EFS_Enemy|FS_Enemy|UFS_Enemy|Spawner|EFS_Spawner|Aggro|"
        r"Boss|AIPerception|Awareness|Patrol|Combat|AttackConfig|Stagger|Poise)",
    "item_inventory": r"^(FS_Item|UFS_Item|Item|Inventory|Equipment|Weapon|Dagger|"
        r"Consumable|Material|Currency|Treasure|Artifact|Drop|Pickup|EFS_Pickup|"
        r"Shop|FS_Shop|UFS_ItemDrop|WeaponCharge|Accessory)",
    "save_progress": r"^(Save|FS_Save|UFS_Save|NamedSaveData|EnemySaveState|"
        r"Checkpoint|Suspend|Persist|ActiveSave|CurrentlyActive|CurrentSave|"
        r"DeleteSave|GetSave|SetSave|ForceReload|RestoreFrom|GenerateSuspend)",
    "level_world": r"^(Level|UFS_Level|FS_Level|Area|EFS_Location|Teleport|Door|"
        r"Barricade|Breakable|Altar|Elevator|FastTravel|MusicArea|UFS_MusicArea|"
        r"SafeLocation|LevelList|LevelManager)",
    "player_stats": r"^(FS_Stats|UFS_Stats|FS_WeaponStats|FS_Armor|S_Stat|"
        r"CurrentPlayerHealth|MaxHealth|Health|EnergyCell|Stamina|Damage|"
        r"PlayerHealth|Invuln|IFrame)",
    "input_ui": r"^(UFS_Input|UFS_GameViewport|InputAction|Interaction|Interact)",
}


def coll(pred):
    return sorted({n for n in names if pred(n)})


def rex(p):
    r = re.compile(p)
    return lambda n: bool(r.match(n))


out = {}
for g, p in GROUPS.items():
    out[g] = coll(rex(p))
    print(f"\n{'='*76}\n{g}  ({len(out[g])})")
    for n in out[g]:
        print("  " + n)

json.dump(out, open(os.path.join(d, "exe_surface_grouped.json"), "w"), indent=1)

# engine-vs-game split: game module names are those that only exist because of
# this game. Report which names look like property/function identifiers and are
# *not* in the stock engine vocabulary.
STOCK = re.compile(r"^(K2_|Receive|On|Get|Set|Is|Has|Can|Should|Add|Remove|"
                   r"Enable|Disable|Start|Stop|Play|Spawn|Destroy|Create|Update)")
