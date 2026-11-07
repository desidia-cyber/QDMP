import sys, struct; sys.path.insert(0,'.')
import pakread, blueprints
pk=pakread.Pak()
r=blueprints.Reader
for t in ["QueensDomain/Content/Blueprints/ActorQuestState.uasset",
          "QueensDomain/Content/Blueprints/SaveSystem/FS_SaveSlot.uasset",
          "QueensDomain/Content/Blueprints/QuestStateInterface.uasset",
          "QueensDomain/Content/Blueprints/ActorQuestStateManager.uasset"]:
    d=pk.payload(t); info=blueprints.parse(d)
    # find the 7 trailing ints: re-walk
    rr=r(d); o=4; legacy=rr.i32(o); o+=4
    if legacy!=-4: o+=4
    o+=8
    if legacy<=-2:
        c=rr.i32(o); o+=4; o+=c*20
    if legacy<0: o+=4
    _,o=rr.fstring(o); o+=4; o+=4; name_off=rr.i32(o-4)
    vals=struct.unpack('<7i', d[o:o+28])
    ec,eo,ic,io,dep = vals[2],vals[3],vals[4],vals[5],vals[6]
    names=info['names']
    imports=[]
    ok=True
    for i in range(ic):
        po=io+i*28
        cp,_,po=rr.fname(po); cn,_,po=rr.fname(po); outer=rr.i32(po); po+=4; on,_,po=rr.fname(po)
        if not all(0<=x<len(names) for x in (cp,cn,on)): ok=False; break
        imports.append((names[cn], names[on], names[cp]))
    exports=[]
    for i in range(ec):
        p2=eo+i*104
        cn,_,p2=rr.fname(p2); sup,_,p2=rr.fname(p2)
        if not (0<=cn<len(names) and 0<=sup<len(names)): ok=False; break
        exports.append((names[cn],names[sup]))
    print("\n####",t,"| exports",ec,"@",eo,"imports",ic,"@",io,"dep",dep,"ok",ok)
    print("   import classes:",sorted({c for c,n,p in imports}))
    print("   exports:",exports[:8])
    print("   import objects:",[n for c,n,p in imports][:14])
