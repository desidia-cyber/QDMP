import sys; sys.path.insert(0,'.')
import pakread, blueprints
pk=pakread.Pak()
for t in ["QueensDomain/Content/Blueprints/ActorQuestState.uasset",
          "QueensDomain/Content/Blueprints/SaveSystem/FS_SaveSlot.uasset",
          "QueensDomain/Content/Blueprints/QuestStateInterface.uasset"]:
    d=pk.payload(t); info=blueprints.parse(d)
    imp,exp=blueprints.try_imports_exports(d,info)
    print("\n####",t,"names",info['name_count'],"imports",len(imp),"exports",len(exp))
    print("  import classes:",sorted({i['class'] for i in imp}))
    for e in exp: print("   EXPORT", e)
