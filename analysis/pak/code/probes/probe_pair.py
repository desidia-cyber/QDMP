import sys, io, re, struct; sys.path.insert(0,'.')
import pakread, locres
pk=pakread.Pak(); g=pk.payload("QueensDomain/Content/Localization/Game/en/Game.locres")
r=locres.decode(g); strs=[s for _,s in r["records"]]
# strict FString key scan (same as build_localisation)
keys=[]
for o in range(0x1D, r["table_start"]-5):
    n,=struct.unpack('<i',g[o:o+4])
    if n<4 or n>200 or o+4+n>r["table_start"]: continue
    if g[o+4+n-1]!=0: continue
    raw=g[o+4:o+4+n-1]
    if not all(0x20<=c<0x7f for c in raw): continue
    s=raw.decode('latin-1')
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_:./\-]*", s): continue
    if re.fullmatch(r"[0-9A-F]{32}", s) or s.startswith('/'): continue
    keys.append(s)
print("keys",len(keys),"strings",len(strs))
for k in ("key_josamired_desc","wep_woodclub_desc","item_rejuv_potion_desc","cha_quest_lib_name"):
    if k in keys:
        ki=keys.index(k)
        print("\n key %-24s at key-index %d"%(k,ki))
        print("   string at same index: %r"%(strs[ki] if ki<len(strs) else None))
        print("   best text match:", [ (i,s[:70]) for i,s in enumerate(strs) if 'JOSAMI' in s.upper() or 'wooden club' in s.lower() or 'rejuven' in s.lower()][:3])
