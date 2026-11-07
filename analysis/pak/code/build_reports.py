#!/usr/bin/env python3
"""Generate blueprint-surface.md for the co-op mod's hook surface."""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pakread      # noqa: E402
import blueprints   # noqa: E402

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "blueprint-surface.md")

TARGETS = [
    ("ActorQuestState", "QueensDomain/Content/Blueprints/ActorQuestState.uasset"),
    ("ActorQuestStateManager", "QueensDomain/Content/Blueprints/ActorQuestStateManager.uasset"),
    ("QuestStateInterface", "QueensDomain/Content/Blueprints/QuestStateInterface.uasset"),
    ("FS_SaveSlot", "QueensDomain/Content/Blueprints/SaveSystem/FS_SaveSlot.uasset"),
    ("SavableActor", "QueensDomain/Content/Blueprints/SaveSystem/SavableActor.uasset"),
    ("SavableActorState", "QueensDomain/Content/Blueprints/SaveSystem/SavableActorState.uasset"),
    ("WorldEventReceiver", "QueensDomain/Content/Blueprints/WorldEventReceiver.uasset"),
    ("ActivatableGroup", "QueensDomain/Content/Blueprints/ActivatableGroup.uasset"),
    ("ActivatableNamedSaveDataSetter", "QueensDomain/Content/Blueprints/ActivatableNamedSaveDataSetter.uasset"),
    ("NamedSaveDataSetterVolume", "QueensDomain/Content/Blueprints/NamedSaveDataSetterVolume.uasset"),
    ("LevelManagerTrigger", "QueensDomain/Content/Blueprints/LevelManagerTrigger.uasset"),
    ("(AreaQuestState)", None),
    ("-- supporting, added because the answers needed them --", None),
    ("FS_GameInstance", "QueensDomain/Content/Blueprints/FS_GameInstance.uasset"),
    ("SaveFunctions", "QueensDomain/Content/Blueprints/SaveSystem/SaveFunctions.uasset"),
    ("SaveHelper", "QueensDomain/Content/Blueprints/SaveSystem/SaveHelper.uasset"),
    ("FS_SystemSave", "QueensDomain/Content/Blueprints/SaveSystem/FS_SystemSave.uasset"),
    ("FS_CoinsSave", "QueensDomain/Content/Blueprints/SaveSystem/FS_CoinsSave.uasset"),
    ("NamedSaveDataSetter_Config", "QueensDomain/Content/Blueprints/NamedSaveDataSetter_Config.uasset"),
    ("FS_ChallengeData", "QueensDomain/Content/Blueprints/Challenges/FS_ChallengeData.uasset"),
    ("ChallengeCompleteVolume", "QueensDomain/Content/Blueprints/ChallengeCompleteVolume.uasset"),
    ("Teleporter", "QueensDomain/Content/Blueprints/Teleporter/Teleporter.uasset"),
    ("TeleporterData", "QueensDomain/Content/Blueprints/Teleporter/TeleporterData.uasset"),
    ("Checkpoint (LD45/Blueprints/Checkpoint)", "QueensDomain/Content/LD45/Blueprints/Checkpoint/Checkpoint.uasset"),
    ("FS_PlayerChar", "QueensDomain/Content/Player/FS_PlayerChar.uasset"),
    ("FS_PlayerController_BP", "QueensDomain/Content/Player/FS_PlayerController_BP.uasset"),
    ("Activatable (LD45 shared)", "QueensDomain/Content/LD45/Blueprints/Objects/Shared/Activatable.uasset"),
]

# engine / Blueprint-VM noise that is in the name table but is not a hook
NOISE_RE = re.compile(
    r"^(CallFunc_|K2Node_|Default__|BndEvt__|ExecuteUbergraph_|Timeline_|AsyncTask_|"
    r"On[A-Za-z]+__DelegateSignature|EdGraph|SCS_Node|SimpleConstructionScript|"
    r"UserConstructionScript|UberGraphFrame|PointerToUberGraphFrame|InheritableComponentHandler|"
    r"Temp_[a-z]|Temp_int|Value_[0-9]|Data_[0-9]|Mode_[0-9]|OnBlink|ROC_)"
)
NOISE_EXACT = set("""
Actor ActorComponent AnimInstance AnimBlueprintGeneratedClass Array ArrayProperty BoolProperty
ByteProperty Class ClassProperty ComponentTemplate ComponentClass DelegateProperty DelegateFunction
DoubleProperty EnumProperty Field FloatProperty Function Guid IntProperty Interface InterfaceProperty
MapProperty MulticlassDelegate MulticlassDelegateProperty Name NameProperty None NumericProperty
Object ObjectProperty Package SceneComponent ScriptStruct SetProperty SoftClassProperty
SoftObjectProperty StrProperty StructProperty TextProperty UInt16Property UInt32Property UInt64Property
BlueprintGeneratedClass GameplayStatics Engine CoreUObject UserDefinedStruct UserDefinedEnum
FloatCurve RichCurve RichCurveKey FloatCurves FloatTracks LinearColorTracks Keys CurveFloat
CurveLinearColor Timeline TimelineComponent TimelineTemplate TimelineLength TimelineGuid Timelines
ETimelineDirection TrackName Translation Rotation Scale3D Quat Vector LinearColor Transform
ReturnValue RootNodes AllNodes EntryPoint InternalVariableName VariableGuid VariableName
ParentComponentOrVariableName ParentComponentOwnerClassName ComponentClass ChangedPropertyList
CookedComponentInstancingData BlueprintComponentChangedPropertyInfo BlueprintCookedComponentInstancingData
bHasValidCookedData UberGraphFunction UberGraphFrame DelegateFunction ClassProperty
ActorTickFunction ActorUROManager ActorUROManager_GEN_VARIABLE TimelineTemplate
""".split())

# names that are pure UMG/engine plumbing even though they look user-ish
KEEP_ALWAYS = re.compile(
    r"(Quest|Challenge|Save|Named|Activatable|Savable|Teleport|Player|Replicate|Host|Client|"
    r"Health|Damage|Item|Weapon|Achievement|Checkpoint|Event|State|Manager|World)"
)


def clean(names, exclude=()):
    out = []
    for n in names:
        if not n or n.startswith("/"):
            continue
        if n in NOISE_EXACT or NOISE_RE.match(n):
            continue
        if n in exclude:
            continue
        if n.endswith(("_GEN_VARIABLE", "_ReturnValue", "_Value")) and len(n.split("_")) > 1:
            continue
        if re.fullmatch(r"[0-9A-F]{32}", n):
            continue
        if n.endswith("_C"):
            out.append(n)
            continue
        out.append(n)
    # de-dup, keep order
    seen = set()
    res = []
    for n in out:
        if n in seen:
            continue
        seen.add(n)
        res.append(n)
    return res


def section(title, path, pk, lines):
    if path is None:
        lines.append("\n## %s\n" % title)
        if title.startswith("(AreaQuestState)"):
            lines.append("**Not present in the pak.** The decoder indexes 44,092 exact paths out of the "
                         "FullDirectoryIndex; no asset whose path contains `AreaQuestState` exists. "
                         "Search run: `entries.tsv` (produced by `pakread.py`) filtered on the substring "
                         "`AreaQuestState` -> 0 rows. The nearest real assets are `ActorQuestState` and "
                         "`ActorQuestStateManager`, both documented above.\n")
        else:
            lines.append("_Supporting assets, included because the Task 3 answers depend on them._\n")
        return
    try:
        d = pk.payload(path)
        info = blueprints.parse(d)
        imp, exp = blueprints.try_imports_exports(d, info)
    except Exception as e:
        lines.append("\n## %s\n\nPARSE FAILED: %s\n" % (title, e))
        return
    lines.append("\n## %s\n" % title)
    lines.append("`%s`  — .uasset %d bytes, %d names\n" % (path, len(d), info["name_count"]))
    cls = [e for e in exp if e["class"] == "BlueprintGeneratedClass"]
    if cls:
        lines.append("| | |")
        lines.append("|---|---|")
        lines.append("| generated class | `%s` |" % cls[0]["name"])
        lines.append("| **parent class** | `%s` |" % (cls[0]["super"] or "(none)"))
        ifs = [i["name"] for i in imp if i["class"].endswith("_C") and i["name"].endswith("_C")
               and i["name"] != cls[0]["name"] and not i["name"].startswith("Default__")]
        if ifs:
            lines.append("| referenced blueprint classes (interfaces/companions) | %s |"
                         % ", ".join("`%s`" % x for x in sorted(set(ifs))))
        lines.append("| exports / imports | %d / %d |" % (len(exp), len(imp)))
        lines.append("")
    funcs = [e["name"] for e in exp if e["class"] == "Function"]
    if funcs:
        lines.append("**UFUNCTION / event exports (%d)**\n" % len(funcs))
        lines.append("```")
        for f in funcs:
            lines.append(f)
        lines.append("```\n")
    scripts = [i["name"] for i in imp if i["class"] == "Function"
               and not i["name"].startswith("Default__")]
    if scripts:
        lines.append("**C++ functions this blueprint calls (%d imported)**\n" % len(scripts))
        lines.append("```")
        for f in sorted(set(scripts)):
            lines.append(f)
        lines.append("```\n")
    cn = clean(info["names"], exclude={i["name"] for i in imp} | set(funcs))
    lines.append("**Property / variable / component names (%d)**\n" % len(cn))
    lines.append("```")
    lines.append("  ".join(cn))
    lines.append("```\n")


def main():
    lines = []
    lines.append("# Blueprint surface — property and function names\n")
    lines.append("""Generated by `code/build_reports.py` from the shipped pak. For each asset:

* **parent class** and generated class name come from the export table
  (`ClassIndex` / `SuperIndex` resolved through the import table), not guessed;
* **UFUNCTION exports** are every export whose class resolves to `Function` — these are the
  blueprint's own declared functions and event entry points;
* **imported C++ functions** are the native functions the blueprint calls;
* **property names** are the package name table with engine/Blueprint-VM noise removed
  (`CallFunc_*` call-result pins, `*_GEN_VARIABLE` component instancing records, `K2Node_*`
  editor nodes, `*_ReturnValue`, `Default__*` CDOs, `*Property` type names, timeline internals,
  delegate signatures, and the two `u16` casing hashes are not names).

Caveat that matters for a mod: these are *names*, and names prove a thing exists, never how it
behaves. Every blueprint here is compiled bytecode; nothing in this file was decompiled.
""")
    with pakread.Pak() as pk:
        for title, path in TARGETS:
            section(title, path, pk, lines)
        lines.append("\n## Totals\n")
        lines.append("| asset | names | exports | imports |")
        lines.append("|---|---|---|---|")
        for title, path in TARGETS:
            if not path:
                continue
            d = pk.payload(path)
            info = blueprints.parse(d)
            imp, exp = blueprints.try_imports_exports(d, info)
            lines.append("| `%s` | %d | %d | %d |" % (title, info["name_count"], len(exp), len(imp)))
    with open(OUT, "w", encoding="utf-8") as g:
        g.write("\n".join(lines) + "\n")
    print("wrote", os.path.abspath(OUT))


if __name__ == "__main__":
    main()
