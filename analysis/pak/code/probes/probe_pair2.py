import sys, re, struct; sys.path.insert(0,'.')
import pakread, locres
pk=pakread.Pak(); g=pk.payload("QueensDomain/Content/Localization/Game/en/Game.locres")
r=locres.decode(g); strs=[s for _,s in r["records"]]
found=[]
for o in range(0x1D, r["table_start"]-5):
    n,=struct.unpack('<i',g[o:o+4])
    if n<4 or n>200 or o+4+n>r["table_start"]: continue
    if g[o+4+n-1]!=0: continue
    raw=g[o+4:o+4+n-1]
    if not all(0x20<=c<0x7f for c in raw): continue
    s=raw.decode('latin-1')
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_:./\-]*", s): continue
    if re.fullmatch(r"[0-9A-F]{32}", s) or s.startswith('/'): continue
    found.append((o,s))
print("keys",len(found))
import bisect
def idx_at(o):
    v,=struct.unpack('<I', g[o-4:o]); return v
cands=[idx_at(o) for o,_ in found]
print("preceding u32 < 2329:", sum(1 for c in cands if c<2329), "/", len(cands))
for target in ("key_josamired_desc","wep_woodclub_desc","item_rejuv_potion_desc","cha_quest_lib_name","binding_comma"):
    for o,s in found:
        if s==target:
            c=idx_at(o)
            print("\n%-24s off=0x%x  preceding u32=%d  -> %r"%(s,o,c, strs[c] if c<len(strs) else "OUT OF RANGE"))
            break
