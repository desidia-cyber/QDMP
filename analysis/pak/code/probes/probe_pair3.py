import sys, re, struct; sys.path.insert(0,'.')
import pakread, locres
pk=pakread.Pak(); g=pk.payload("QueensDomain/Content/Localization/Game/en/Game.locres")
r=locres.decode(g); strs=[s for _,s in r["records"]]
found=[]
for o in range(0x1D, r["table_start"]-24):
    n,=struct.unpack('<i',g[o:o+4])
    if n<4 or n>200 or o+4+n>r["table_start"]: continue
    if g[o+4+n-1]!=0: continue
    raw=g[o+4:o+4+n-1]
    if not all(0x20<=c<0x7f for c in raw): continue
    s=raw.decode('latin-1')
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_:./\-]*", s): continue
    if re.fullmatch(r"[0-9A-F]{32}", s) or s.startswith('/'): continue
    found.append((o,s,n))
print("keys",len(found))
post=[struct.unpack('<I',g[o+4+n:o+8+n])[0] for o,s,n in found]
print("post-u32 < 2329:", sum(1 for c in post if c<2329),"/",len(post))
hits=0; samples=[]
for (o,s,n),c in zip(found,post):
    if c<len(strs):
        hits+=1
        if len(samples)<12: samples.append((s,c,strs[c][:70]))
for x in samples: print("  %-26s -> [%4d] %r"%x)
