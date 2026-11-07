import sys, collections; sys.path.insert(0,'.')
import pakread, blueprints
pk=pakread.Pak()
p="QueensDomain/Content/RuinsLevel/Maps/100_GR.umap"
d=pk.payload(p)
i=blueprints.parse(d)
print("tail",i['tail_fields'])
imp,exp=blueprints.try_imports_exports(d,i)
print("imports",len(imp),"exports",len(exp))
if exp:
    c=collections.Counter(e['class'] for e in exp)
    print("classes:",c.most_common(12))
    tp=[e for e in exp if e['class'] and 'Teleporter' in e['class']]
    print("teleporter exports:",len(tp))
    for e in tp[:15]: print("   ",e['name'],"|",e['class'])
