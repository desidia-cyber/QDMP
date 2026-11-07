import sys; sys.path.insert(0,'.')
import pakread, blueprints
pk=pakread.Pak()
tests=["QueensDomain/Content/Blueprints/ActorQuestState.uasset",
       "QueensDomain/Content/Blueprints/SaveSystem/FS_SaveSlot.uasset",
       "QueensDomain/Content/Blueprints/ActorQuestStateManager.uasset",
       "QueensDomain/Content/Blueprints/QuestStateInterface.uasset"]
for t in tests:
    d=pk.payload(t); info=blueprints.parse(d)
    imp,exp=blueprints.try_imports_exports(d,info)
    print("\n####",t,"size",len(d),"names",info['name_count'],"tail",info['tail_fields'])
    print("  imports",len(imp),"exports",len(exp))
    if imp: print("  import classes:",sorted({i['class'] for i in imp}))
    if exp: print("  export classes:",sorted({e['class'] for e in exp}))
    if imp: print("  first imports:",[(i['class'],i['name']) for i in imp[:8]])
