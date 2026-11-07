import struct
d=open(r'G:\Freespace\Queens_Domain\analysis\scratch\l.locres','rb').read()
TAB=85689
def fstr(o):
    if o+4>len(d): raise ValueError
    n,=struct.unpack('<i',d[o:o+4]); o+=4
    if n==0: return "",o
    if n<0:
        nb=-n*2
        if o+nb>len(d): raise ValueError
        return d[o:o+nb].decode('utf-16-le','replace').split('\x00')[0], o+nb
    if n>3000 or o+n>TAB: raise ValueError
    return d[o:o+n].split(b'\x00')[0].decode('latin-1'), o+n
def walk(S, k, hascount):
    o=S; recs=[]
    try:
        cnt=None
        if hascount:
            cnt,=struct.unpack('<I',d[o:o+4]); o+=4
            if not (1<=cnt<=100000): return None
        while o < TAB:
            if o+k*4>TAB: return None
            vals=struct.unpack('<%dI'%k, d[o:o+k*4])
            nm,o2=fstr(o+k*4)
            if o2<=o: return None
            recs.append((vals,nm)); o=o2
        if o!=TAB: return None
        if cnt is not None and cnt!=len(recs): return None
        return recs
    except Exception:
        return None
hits=[]
for S in range(0x11,0x60):
    for k in (1,2,3,4,5,6):
        for hc in (False,True):
            r=walk(S,k,hc)
            if r: hits.append((S,k,hc,len(r)))
for h in hits: print("start=0x%02x fields=%d hascount=%s records=%d"%h)
print("total hits",len(hits))
