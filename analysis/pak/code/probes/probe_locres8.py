import struct, collections
def parse(d,S):
    o=S; n=0; prefixes=[]; utf16=0
    while o < len(d):
        if o+8>len(d): return None
        x,=struct.unpack('<I',d[o:o+4]); l,=struct.unpack('<i',d[o+4:o+8])
        if l==0:
            prefixes.append(x); o+=8; n+=1; continue
        if l<0:
            nb=-l*2; utf16+=1
            if o+8+nb>len(d): return None
            if d[o+8+nb-1]!=0 or d[o+8+nb-2]!=0: return None
        else:
            nb=l
            if l>2000 or o+8+nb>len(d) or d[o+8+nb-1]!=0: return None
        prefixes.append(x); o+=8+nb; n+=1
    return n,o,prefixes,utf16
for p,hint in ((r'G:\Freespace\Queens_Domain\analysis\scratch\l.locres',85689),
               (r'G:\Freespace\Queens_Domain\analysis\scratch\e.locres',862401)):
    d=open(p,'rb').read()
    r=parse(d,hint)
    print("\n####",p,"hint",hint,"->",None if not r else ("count=%d end=%d EOF=%s utf16=%d"%(r[0],r[1],r[1]==len(d),r[3])))
    if r:
        print("  prefix hist:",collections.Counter(r[2]).most_common(5))
        print("  first 4 prefixes:",r[2][:4])
    # try a few nearby starts
    for S in range(hint-16,hint+16):
        rr=parse(d,S)
        if rr and rr[1]==len(d): print("  EXACT start",S,"count",rr[0])
