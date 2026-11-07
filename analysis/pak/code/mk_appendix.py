import sys, io; sys.path.insert(0,'.')
import pakread, blueprints
pk=pakread.Pak()
tele=[]; save=[]
for m in sorted(p for p in pk.entries if p.lower().endswith('.umap')):
    d=pk.payload(m); i=blueprints.parse(d); imp,exp=blueprints.try_imports_exports(d,i)
    if not exp: continue
    short=m.rsplit('/',1)[-1][:-5]
    pkg="/Game/"+m.split("Content/",1)[1][:-5]
    for e in exp:
        cls=e['class'] or ''
        if e['name'].startswith('Default__'): continue
        path="%s.%s:PersistentLevel.%s"%(pkg,short,e['name'])
        if 'Teleporter' in cls: tele.append((cls,e['name'],path))
        elif cls.endswith('_Save_C') or cls.startswith('Checkpoint'): save.append((cls,e['name'],path))
L=[]
L.append("## Appendix — teleporter object paths (%d)\n"%len(tele))
L.append("```")
L.append("%-30s %-30s %s"%("CLASS","ACTOR NAME","OBJECT PATH"))
for c,n,p in tele: L.append("%-30s %-30s %s"%(c,n,p))
L.append("```\n")
L.append("## Appendix — save-point object paths (%d)\n"%len(save))
L.append("```")
L.append("%-30s %-30s %s"%("CLASS","ACTOR NAME","OBJECT PATH"))
for c,n,p in save: L.append("%-30s %-30s %s"%(c,n,p))
L.append("```")
io.open(r'G:\Freespace\Queens_Domain\analysis\scratch\appendix.txt','w',encoding='utf-8',newline='\n').write("\n".join(L)+"\n")
# patch answers.md
ap=r'G:\Freespace\Queens_Domain\analysis\pak\answers.md'
a=io.open(ap,encoding='utf-8').read()
k=a.find("## Appendix — teleporter object paths")
a=a[:k]+"\n".join(L)+"\n"
io.open(ap,'w',encoding='utf-8',newline='\n').write(a)
print("teleporters",len(tele),"savepoints",len(save),"answers.md bytes",len(a))
