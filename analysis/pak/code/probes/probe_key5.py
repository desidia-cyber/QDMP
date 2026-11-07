import struct
d=open(r'G:\Freespace\Queens_Domain\analysis\scratch\l.locres','rb').read()
TAB=85689
def fstr(o):
    if o+4>len(d): raise ValueError("oob")
    n,=struct.unpack('<i',d[o:o+4]); o+=4
    if n==0: return "",o
    if n<0:
        nb=-n*2
        if o+nb>len(d): raise ValueError("oob16")
        return d[o:o+nb].decode('utf-16-le','replace').split('\x00')[0], o+nb
    if n>3000 or o+n>TAB: raise ValueError("badlen %d"%n)
    return d[o:o+n].split(b'\x00')[0].decode('latin-1'), o+n
def walk(S, keyfmt, tail):
    o=S; ns=[]; tot=0
    try:
        while o<TAB:
            h,=struct.unpack('<I',d[o:o+4]); o+=4
            name,o=fstr(o)
            cnt,=struct.unpack('<I',d[o:o+4]); o+=4
            if cnt>20000: return None
            keys=[]
            for _ in range(cnt):
                if keyfmt=='h+n+i':
                    kh,=struct.unpack('<I',d[o:o+4]); o+=4
                    kn,o=fstr(o)
                    si,=struct.unpack('<i',d[o:o+4]); o+=4
                    keys.append((kh,kn,si))
                elif keyfmt=='h+n':
                    kh,=struct.unpack('<I',d[o:o+4]); o+=4
                    kn,o=fstr(o)
                    keys.append((kh,kn,None))
            ns.append((h,name,keys)); tot+=len(keys)
        if o!=TAB: return None
        return ns,tot
    except Exception as e:
        return None
for S in range(0x1d,0x40):
    for kf in ('h+n+i','h+n'):
        r=walk(S,kf,None)
        if r: print("MATCH start=0x%x fmt=%s namespaces=%d keys=%d"%(S,kf,len(r[0]),r[1]))
# show partial progress for the best guess
print("\n--- partial trace start=0x1d fmt=h+n+i ---")
o=0x1d; nn=0; nk=0
try:
    while o<TAB:
        h,=struct.unpack('<I',d[o:o+4]); o+=4
        name,o=fstr(o)
        cnt,=struct.unpack('<I',d[o:o+4]); o+=4
        print("  ns %r hash=0x%08x keys=%d"%(name,h,cnt))
        for _ in range(cnt):
            kh,=struct.unpack('<I',d[o:o+4]); o+=4
            kn,o=fstr(o)
            si,=struct.unpack('<i',d[o:o+4]); o+=4
            nk+=1
        nn+=1
except Exception as e:
    print("  stop at 0x%x: %s (ns so far %d keys %d)"%(o,e,nn,nk))
