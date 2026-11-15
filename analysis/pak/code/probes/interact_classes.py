#!/usr/bin/env python3
"""World-interact coverage -- every door / breakable / secret-mechanism class.

Run:  python interact_classes.py [--all]
Needs: ../pakread.py, ../blueprints.py and the pak.

Why this probe exists
---------------------
`qdinteract.h` classifies a placed actor by its generated class name and carries it
through the world-interact lane.  That is only safe if the name table in the header
and the shipped packages agree, so this probe derives the list from the pak instead
of trusting the header: it walks every `.uasset`, keeps the cooked
`BlueprintGeneratedClass` actors, and prints the class, its category and the state /
animation names the header uses for it.

The categories and their match rules are the SAME ones `qdinteract::Classify`
implements (case-insensitive substring, with the same rejections), so a class that
appears here is a class the lane claims, and a class the lane should reject does not
appear.  `--all` also prints the packages that were examined and rejected, which is
how the rejects (a DoorKey, a lamp, a dining plate, a lift) are auditable.

Re-run: python interact_classes.py            (the coverage table)
        python interact_classes.py --all      (plus examined/rejected rows)
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(HERE, "..", "..", ".."))
import pakread     # noqa: E402
import blueprints  # noqa: E402

# ---------------------------------------------------------------------------
# The classification, mirroring qdinteract.h
# ---------------------------------------------------------------------------
# A class is rejected when it is bookkeeping/helper geometry rather than a placed
# interactable.  The list is deliberately broader than the one in qdobjectstate.h:
# the coverage run surfaced a `DoorLamp`, a `SwordShrine_Gate_Key`, a `DS_Plate_BP`
# (a dining plate) and a `Lib_LiftSwitch_BP` (the lift crash of Session 26), and every
# one of them contains a category word without being that category.
REJECT = [
    "Manager", "Subsystem", "Component", "Data", "State", "Save", "Interface",
    "Helper", "Getter", "Utils", "Function", "__Delegate", "Default__",
    "_GEN_VARIABLE", "Settings", "Config", "Library", "Audio", "Sound", "Cue",
    "Mesh", "Material", "Texture", "Anim", "Montage", "Skeleton", "Physics",
    "Particle", "Icon", "HINT", "Lamp", "Key", "Dining", "Spotlight",
    "Spawner", "Trigger", "Volume", "Projectile", "Weapon",
    # UI widgets are not placed actors; NPCs and effects reuse a category word.
    "UI", "Widget", "ButtonStyle", "DefaultButton", "HighlightButton",
    "AspectRatio", "Wishlist", "Binding", "DialogueExit", "NPC",
    "DamageTarget", "Shake", "NoDoor", "CheckForReward", "TurnIn", "Reward",
    "Remnant", "Broken", "Blocker",
]

# Word-ish tests for the short words that are substrings of unrelated names
# (Return contains "urn", Spot contains "pot", Crate is safe, Jar is safe).
def has_word(hay, word):
    n = len(word)
    h = hay.lower(); w = word.lower()
    i = 0
    while True:
        i = h.find(w, i)
        if i < 0:
            return False
        before = h[i - 1] if i > 0 else "/"
        after = h[i + n] if i + n < len(h) else "/"
        if not before.isalnum() and not after.isalnum():
            return True
        i += 1

def has(hay, needle):
    return needle.lower() in hay.lower()

DOOR = ["Door", "Gate", "Shutter", "Portcullis", "Trapdoor"]
BREAK = ["Breakable", "Barricade", "Barrel", "Crate", "Permabreakable"]
BREAK_WORD = ["Pot", "Vase", "Urn", "Jar"]
SECRET = ["SecretWall", "SecretDoor", "Illusory", "Illusion", "HiddenWall",
          "FakeWall", "SecretPassage", "ScienceSecret"]
MECH = ["Lever", "Switch", "Button", "Valve", "Crank", "Rune", "PressurePlate"]


def classify(cls):
    if not cls:
        return None
    for r in REJECT:
        if has(cls, r):
            return None
    # The lift/elevator crash (Session 26) is excluded before the lever test.
    if has(cls, "Lift") or has(cls, "Elevator") or has(cls, "ElevSwitch"):
        return None
    if any(has(cls, w) for w in DOOR):
        return "door"
    if any(has(cls, w) for w in BREAK) or any(has_word(cls, w) for w in BREAK_WORD):
        return "breakable"
    if any(has(cls, w) for w in SECRET):
        return "door"            # a secret wall/door animates through the door visual
    if any(has(cls, w) for w in MECH):
        return "activatable"
    return None


# The names qdinteract.h probes for that category, printed so a reviewer can see the
# header's table agrees with the package's name table.
PROBES = {
    "door":        ["Opened", "DoorClosed", "SetToOpenState", "SetTOEndState",
                    "Activate", "Deactivate", "Restore State From Save Data"],
    "breakable":   ["Break", "BrokeBarricade", "AlreadyBroke", "PreviewBroken",
                    "SetActorEnableCollision", "Restore State From Save Data"],
    "activatable": ["Activated", "bActivated", "ToggledOn", "Activate", "Deactivate",
                    "ForceOpen", "Restore State From Save Data"],
}

NOISE_EXT = re.compile(r'(?i)(_MI\d*$|_TX$|_SM\d*$|_tx$|_phong|_BaseColor|_Normal|'
                       r'_Rough|_Cue$|_Cue_|AnimBP|Montage|Skeleton|PhysicsAsset|'
                       r'_MA$|_Anim$|Loop$|Particle|_Icon$|_Data$|_Logo|_Glow$|'
                       r'_MIB$|_Inst$|_Curve$|_Table$|_Font$|_Widget$)')


def main():
    show_all = "--all" in sys.argv
    pk = pakread.Pak()
    rows = []
    rejected = []
    seen = 0
    for path in pk.entries:
        if not path.startswith("QueensDomain/Content/") or not path.endswith(".uasset"):
            continue
        stem = path.rsplit("/", 1)[-1][:-len(".uasset")]
        if NOISE_EXT.search(stem):
            continue
        # A class can only classify if its name carries a category word (the rejects
        # only remove), so do not parse the other 18k packages.
        words = DOOR + BREAK + SECRET + MECH
        if not (any(has(stem, w) for w in words) or
                any(has_word(stem, w) for w in BREAK_WORD)):
            continue
        try:
            data = pk.payload(path)
            info = blueprints.parse(data)
            imports, exports = blueprints.try_imports_exports(data, info)
        except Exception:
            continue
        cls = None
        for e in exports:
            if e["class"] in ("BlueprintGeneratedClass", "WidgetBlueprintGeneratedClass"):
                cls = e["name"]
                break
        if not cls:
            continue
        seen += 1
        cat = classify(cls)
        if not cat:
            if show_all:
                rejected.append((path, cls))
            continue
        names = set(info["names"])
        hit = [p for p in PROBES[cat] if p in names]
        rows.append((cat, cls, path, hit))
    pk.close()

    rows.sort(key=lambda r: (r[0], r[1]))
    by_cat = {}
    for cat, cls, path, hit in rows:
        by_cat.setdefault(cat, []).append((cls, path, hit))
    print("== world-interact coverage: %d blueprint classes classified ==" % len(rows))
    for cat in ("door", "breakable", "activatable"):
        print("\n-- %s (%d) --" % (cat, len(by_cat.get(cat, []))))
        for cls, path, hit in by_cat.get(cat, []):
            print("  %-42s %s" % (cls, path))
            print("      names: %s" % (", ".join(hit) if hit else "-"))
    print("\nexamined %d BlueprintGeneratedClass packages" % seen)
    if show_all:
        print("\n-- examined and REJECTED by the classifier (%d) --" % len(rejected))
        for path, cls in rejected[:400]:
            print("  %-42s %s" % (cls, path))


if __name__ == "__main__":
    main()
