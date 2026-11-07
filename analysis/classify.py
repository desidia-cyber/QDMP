#!/usr/bin/env python3
"""Group the exe's identifier surface by gameplay domain keyword."""
import collections, json, os, re, sys

d = os.path.dirname(os.path.abspath(__file__))
names = [l.rstrip("\n") for l in open(os.path.join(d, "exe_names.txt"), encoding="utf-8")]
paths = [l.rstrip("\n") for l in open(os.path.join(d, "exe_paths.txt"), encoding="utf-8")]
ns = set(names)

GROUPS = {
    "QUEST_OBJECTIVE": r"quest|objective|mission|task|journal|goals?|step|chapter|progress|"
                       r"complete|unlock|achiev|trophy|reward|flag",
    "DIALOGUE": r"dialog|conversation|speech|subtitle|bark|voice|talk|npc|line|"
                r"speaker|choice|utterance|story",
    "ITEM_INVENTORY": r"item|inventor|loot|pickup|equip|weapon|consumable|currency|"
                      r"material|treasure|chest|container|slot|store|shop|stack|"
                      r"recipe|craft|upgrade",
    "COMBAT_AI": r"enemy|pawn|ai[A-Z_]|controller|behavio|spawn|boss|health|damage|"
                 r"attack|combat|hit|hurt|dead|death|respawn|aggro|threat|perce|"
                 r"patrol|nav|movement|melee|projectile|stagger|poise|block|parry|"
                 r"dodge|invuln|knock|stun|status|buff|debuff|element",
    "SAVE_PROGRESS": r"save|load|checkpoint|slot|persist|autosave|gamesave|"
                     r"serializ|snapshot|suspend|worldstate|resume",
    "INTERACT": r"interact|usable|door|lever|switch|terminal|chest|trap|button|"
                r"trigger|teleport|fasttravel|gate|elevator|platform|barricade|"
                r"breakable|destruct|pushable|cursechest|altar|shrine|gravestone",
    "PLAYER_STATS": r"player|character|vitals|stamina|mana|energy|cell|stat|"
                    r"attribute|level|experience|xp\b|ability|skill|perk|buff|"
                    r"modifier|resist|defen|armou?r|charactervalue",
    "MOVEMENT": r"camera|view|look|aim|pitch|yaw|rotation|transform|location|"
                r"velocity|input|axis|jump|crouch|sprint|dash|wallrun|climb|"
                r"ragdoll|animation|montage|sequence|blend|pant|footstep",
    "NET_ABSENT": r"replicat|netmulticast|servertravel|netconnection|network|rpc|"
                  r"netdriver|client|server|session|bandwidth|ping",
}


def classify(n):
    hits = []
    for gname, pat in GROUPS.items():
        if re.search(pat, n, re.I):
            hits.append(gname)
    return hits


buckets = collections.defaultdict(list)
for n in names:
    for g in classify(n):
        buckets[g].append(n)

for g in GROUPS:
    b = buckets[g]
    print(f"\n{'='*78}\n{g}  ({len(b)} names)")
    # prefer A*/U*/F*/E* class-shaped names first
    cls = sorted({x for x in b if re.match(r"^[AUFE][A-Z][A-Za-z0-9_]+$", x)})
    print(f"  class-shaped ({len(cls)}):")
    for c in cls[:120]:
        print("    " + c)
    rest = [x for x in b if x not in set(cls)]
    print(f"  other ({len(rest)}), first 120:")
    for c in rest[:120]:
        print("    " + c)

json.dump({g: sorted(set(buckets[g])) for g in GROUPS},
          open(os.path.join(d, "exe_by_domain.json"), "w"), indent=1)
