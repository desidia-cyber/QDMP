import sys, collections; sys.path.insert(0,'.')
import pakread, blueprints
pk=pakread.Pak()
tc=collections.Counter(); sc=collections.Counter(); ship_t=collections.Counter(); ship_s=collections.Counter()
DEV={'BirdsTestZone','VideoConfigMap','VideoConfig_2'}
for m in sorted(p for p in pk.entries if p.lower().endswith('.umap')):
    d=pk.payload(m); i=blueprints.parse(d); imp,exp=blueprints.try_imports_exports(d,i)
    if not exp: continue
    short=m.rsplit('/',1)[-1][:-5]
    for e in exp:
        cls=e['class'] or ''
        if e['name'].startswith('Default__'): continue
        if 'Teleporter' in cls:
            tc[cls]+=1
            if short not in DEV and short!='Ruins_Level_00': ship_t[cls]+=1
        elif cls.endswith('_Save_C') or cls.startswith('Checkpoint'):
            sc[cls]+=1
            if short not in DEV: ship_s[cls]+=1
print("ALL teleporters:",dict(tc),"total",sum(tc.values()))
print("SHIPPING teleporters (excl dev maps + persistent level):",dict(ship_t),"total",sum(ship_t.values()))
print("ALL savepoints:",dict(sc),"total",sum(sc.values()))
print("SHIPPING savepoints:",dict(ship_s),"total",sum(ship_s.values()))
# RPC-shaped function exports across all blueprint-ish assets
bad=[]
for p in sorted(pk.entries):
    if not p.lower().endswith('.uasset'): continue
    if '/Blueprints/' not in p and '/Player/' not in p: continue
    try:
        d=pk.payload(p); i=blueprints.parse(d); imp,exp=blueprints.try_imports_exports(d,i)
    except Exception: continue
    for e in exp:
        if e['class']=='Function' and (e['name'].startswith(('Server_','Client_','NetMulticast_')) or e['name'].endswith('_Implementation')):
            bad.append((p,e['name']))
print("RPC-shaped function exports:",len(bad), bad[:5])
