import sys, re; sys.path.insert(0,'.')
import pakread, blueprints
pk=pakread.Pak()
p="QueensDomain/Content/RuinsLevel/Maps/100_GR.umap"
print("size",pk.size_of(p))
d=pk.payload(p)
i=blueprints.parse(d)
names=i['names']
print("names",len(names))
tp=[n for n in names if 'Teleporter' in n or 'Teleport' in n]
print("teleporter-ish names:",len(tp))
for n in sorted(tp)[:40]: print("   ",n)
