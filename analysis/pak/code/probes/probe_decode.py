import struct, collections, random
PAK = r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Content\Paks\QueensDomain-WindowsNoEditor.pak"
f=open(PAK,'rb')
def rd(o,n):
    f.seek(o); return f.read(n)
def rdstr(o):
    n,=struct.unpack('<i', rd(o,4)); o+=4
    if n==0: return "",o
    if n<0:
        b=rd(o,-n); o+=-n; return b.decode('utf-16-le','replace').split('\x00')[0],o
    b=rd(o,n); o+=n; return b.split(b'\x00')[0].decode('latin-1'),o
BLOB=1973612764; IDX=1973612650
o=1974752831
nd,=struct.unpack('<i', rd(o,4)); o+=4
files=[]
for i in range(nd):
    dn,o=rdstr(o); n,=struct.unpack('<i', rd(o,4)); o+=4
    for j in range(n):
        nm,o=rdstr(o); loc,=struct.unpack('<i', rd(o,4)); o+=4
        files.append((dn+nm,loc))
blob=rd(BLOB, 44092*12)
E=[]
for name,loc in files:
    a,b,c = struct.unpack('<III', blob[loc:loc+12]); E.append((name,a,b,c,loc))
ivs=sorted((a,a+b) for _,a,b,c,_ in E)
ovl=0; gap=0; prev_end=0; maxgap=0
for s,e in ivs:
    if s<prev_end: ovl+=1
    if s>prev_end: gap+= s-prev_end; maxgap=max(maxgap,s-prev_end)
    prev_end=max(prev_end,e)
print("overlaps:",ovl,"gap bytes:",gap,"max gap:",maxgap,"max end:",prev_end)
uas=[(n,a,b) for n,a,b,c,_ in E if n.lower().endswith('.uasset')]
ok=0; fails=[]
for n,a,b in uas:
    if rd(a,4)==b'\xc1\x83\x2a\x9e': ok+=1
    else: fails.append((n,a,b,rd(a,4).hex()))
print("uasset magic pass: %d/%d = %.3f%%"%(ok,len(uas),100.0*ok/max(1,len(uas))))
print("fails:",fails[:8])
ums=[(n,a,b) for n,a,b,c,_ in E if n.lower().endswith('.umap')]
ok2=sum(1 for n,a,b in ums if rd(a,4)==b'\xc1\x83\x2a\x9e')
print("umap pass: %d/%d"%(ok2,len(ums)))
# .uexp: verify by checking following entry / plausibility
uex=[(n,a,b,c) for n,a,b,c,_ in E if n.lower().endswith('.uexp')]
# uexp should start with 4-byte 'uasset exports' ... check ZERO count? just check printable ratio
import statistics
ratios=[]
for n,a,b in uex[:300]:
    d=rd(a,min(b,512))
    if not d: ratios.append(0); continue
    pr=sum(1 for x in d if 32<=x<127)/len(d)
    ratios.append(pr)
print("uexp sample:",len(ratios),"mean printable ratio %.3f"%statistics.mean(ratios))
# strict random test
random.seed(7); samp=random.sample(E,300)
okc=0
for n,a,b,c,l in samp:
    if n.lower().endswith(('.uasset','.umap')):
        if rd(a,4)==b'\xc1\x83\x2a\x9e': okc+=1
print("random300 ok(after filtering pkg magic):",okc)
with open(r"G:\Freespace\Queens_Domain\analysis\pak\entries.tsv","w",encoding="utf-8") as g:
    for n,a,b,c,l in E: g.write("%s\t%d\t%d\t%d\n"%(n,a,b,c))
print("wrote entries.tsv")
