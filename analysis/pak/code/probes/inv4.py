paths=[l.rstrip('\n').split('\t')[0] for l in open('entries.tsv',encoding='utf-8')]
want=["ActorQuestState","ActorQuestStateManager","QuestStateInterface","FS_SaveSlot","SavableActor",
      "SavableActorState","WorldEventReceiver","ActivatableGroup","ActivatableNamedSaveDataSetter",
      "NamedSaveDataSetterVolume","LevelManagerTrigger","AreaQuestState","SaveHelper","SaveFunctions",
      "NamedSaveDataSetter_Config","FS_SaveGame","FS_SystemSave","Challenges/FS_ChallengeData"]
for w in want:
    hits=[p for p in paths if w in p and p.endswith('.uasset')]
    print("%-32s %s"%(w, hits[:3] if hits else "*** NOT PRESENT ***"))
print()
# teleporters / save points
import re
tp=[p for p in paths if 'Teleporter' in p and p.endswith('.uasset')]
print("Teleporter-named uassets:",len(tp))
sp=[p for p in paths if re.search(r'_Save\.uasset$', p)]
print("_Save.uasset count:",len(sp))
for p in sorted(sp)[:40]: print("   "+p)
