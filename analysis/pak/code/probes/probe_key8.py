import struct
d=open(r'G:\Freespace\Queens_Domain\analysis\scratch\l.locres','rb').read()
TAB=85689
def fstr(o):
    n,=struct.unpack('<i',d[o:o+4]); o+=4
    if n==0: return "",o
    if n<0:
        nb=-n*2
        if o+nb>TAB: raise ValueError
        return d[o:o+nb].decode('utf-16-le','replace').split('\x00')[0], o+nb
    if n>3000 or o+n>TAB: raise ValueError
    return d[o:o+n].split(b'\x00')[0].decode('latin-1'), o+n
def keyparse(o,cnt,pre,post):
    for _ in range(cnt):
        o+=pre
        nm,o=fstr(o)
        o+=post
    return o
def walk(S,pre,post,maxns=100000):
    o=S; ns=[]
    try:
        while o<TAB:
            nm,o=fstr(o)
            c,=struct.unpack('<I',d[o:o+4]); o+=4
            if c>30000: return None
            o=keyparse(o,c,pre,post)
            if o>TAB: return None
            ns.append((nm,c))
            if len(ns)>maxns: return None
        return ns if o==TAB else None
    except Exception: return None
for S in range(0x1d,0x100):
    for k,(pre,post) in {'u32+F':(4,0),'u32+F+i32':(4,4),'u32+u32+F':(8,0)}.items():
        r=walk(S,pre,post)
        if r: print("MATCH start=0x%x key=%s namespaces=%d keys=%d"%(S,k,len(r),sum(c for _,c in r)))
# trace Dialogue to see if it syncs
print("\n--- from Dialogue 0x6e5 with key=u32+F ---")
o=0x6e5; ns=[]
try:
    while o<TAB and len(ns)<12:
        nm,o=fstr(o); c,=struct.unpack('<I',d[o:o+4]); o+=4
        ns.append((nm,c)); o=keyparse(o,c,4,0)
    print("  parsed",len(ns),"ns; next 0x%x TAB 0x%x"%(o,TAB))
    for x in ns[:12]: print("   ",x)
except Exception as e: print("  fail at 0x%x"%o, e)
