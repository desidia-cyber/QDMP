import sys; sys.path.insert(0,'.')
import pakread, blueprints
pk=pakread.Pak()
for t in ["QueensDomain/Content/Blueprints/ActorQuestState.uasset",
          "QueensDomain/Content/Blueprints/SaveSystem/FS_SaveSlot.uasset",
          "QueensDomain/Content/Blueprints/QuestStateInterface.uasset",
          "QueensDomain/Content/Blueprints/ActorQuestStateManager.uasset"]:
    d=pk.payload(t); info=blueprints.parse(d)
    imp,exp=blueprints.try_imports_exports(d,info)
    print("\n####",t,"names",info['name_count'],"tail",info['tail_fields'])
    print("  imports",len(imp),"exports",len(exp))
    if imp:
        print("  import classes:",sorted({i['class'] for i in imp}))
        print("  import objects:",[i['name'] for i in imp][:16])
    if exp: print("  exports:",[(e['class'],e['super']) for e in exp])
