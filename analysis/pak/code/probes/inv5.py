import re
paths=[l.rstrip('\n').split('\t')[0] for l in open('entries.tsv',encoding='utf-8')]
area=[p for p in paths if p.startswith('QueensDomain/Content/Blueprints/Areas/') and p.endswith('.uasset')]
print("Areas/ uassets:",len(area))
tp=[p for p in area if 'Teleporter' in p]
sv=[p for p in area if p.endswith('_Save.uasset')]
ic=[p for p in area if p.endswith('_Icon_SP.uasset')]
print("Areas teleporters:",len(tp),"  save points:",len(sv),"  icon:",len(ic))
print("\n-- teleporters --")
for p in sorted(tp): print("  "+p)
print("\n-- non-teleporter non-save area assets --")
for p in sorted(set(area)-set(tp)-set(sv)-set(ic)): print("  "+p)
