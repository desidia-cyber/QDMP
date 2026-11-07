import struct
def try_parse(d, S, shape):
    try:
        n,=struct.unpack('<i', d[S:S+4]); S0=S+4
        if not (100 < n < 200000): return None
        o=S0
        for _ in range(n):
            if shape=='4str':
                o+=4
                l,=struct.unpack('<i',d[o:o+4]); o+=4
            else:
                l,=struct.unpack('<i',d[o:o+4]); o+=4
            if l<0 or l>2000 or o+l>len(d): return None
            s=d[o:o+l]
            if l and not s.endswith(b'\x00'): return None
            o+=l
        if o==len(d): return (n,o)
    except Exception:
        pass
    return None
for p,hint in ((r'G:\Freespace\Queens_Domain\analysis\scratch\l.locres',85689),
               (r'G:\Freespace\Queens_Domain\analysis\scratch\e.locres',862401)):
    d=open(p,'rb').read()
    print("\n####",p,"len",len(d),"hint",hint)
    found=[]
    for S in range(max(0,hint-120), min(len(d)-4, hint+120)):
        for shape in ('4str','str'):
            r=try_parse(d,S,shape)
            if r: found.append((S,shape,r))
    for f in found: print("  MATCH start=%d shape=%s count=%d end=%d"%(f[0],f[1],f[2][0],f[2][1]))
