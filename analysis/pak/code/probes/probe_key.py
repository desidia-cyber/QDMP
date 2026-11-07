import struct, collections
d=open(r'G:\Freespace\Queens_Domain\analysis\scratch\l.locres','rb').read()
TAB=85689
def fstr(o):
    n,=struct.unpack('<i',d[o:o+4]); o+=4
    if n==0: return "",o
    if n<0: nb=-n*2; return d[o:o+nb].decode('utf-16-le','replace').split('\x00')[0], o+nb
    return d[o:o+n].split(b'\x00')[0].decode('latin-1'), o+n
def try_start(S):
    o=S; recs=[]
    while o < TAB:
        if o+12>len(d): return None
        a,b,c = struct.unpack('<III', d[o:o+12])
        try: nm,o2 = fstr(o+12)
        except Exception: return None
        if o2> TAB or o2<=o: return None
        recs.append((a,b,c,nm)); o=o2
    return recs if o==TAB else None
for S in range(0x1d,0x40):
    r=try_start(S)
    if r: print("start 0x%02x -> %d records, exact end %s"%(S,len(r),"yes"))
