import struct
d=open(r'G:\Freespace\Queens_Domain\analysis\scratch\l.locres','rb').read()
TAB=85689
def fstr(o):
    n,=struct.unpack('<i',d[o:o+4]); o+=4
    if n==0: return "",o
    if n<0:
        nb=-n*2
        if o+nb>len(d): raise ValueError
        return d[o:o+nb].decode('utf-16-le','replace').split('\x00')[0], o+nb
    if n>3000 or o+n>TAB: raise ValueError
    return d[o:o+n].split(b'\x00')[0].decode('latin-1'), o+n
def walk(S,ks):
    o=S; ns=[]; tot=0
    try:
        while o<TAB:
            nm,o=fstr(o)
            c,=struct.unpack('<I',d[o:o+4]); o+=4
            if c>30000: return None
            if o+c*ks>TAB: return None
            o+=c*ks; ns.append((nm,c)); tot+=c
        if o!=TAB: return None
        return ns,tot
    except Exception: return None
for S in range(0x1d,0x60):
    for ks in (4,8,12,16,20):
        r=walk(S,ks)
        if r: print("MATCH start=0x%x keysize=%d ns=%d keys=%d"%(S,ks,len(r[0]),r[1]))
# partial trace of the most plausible: start at first namespace
print("\n--- trace start=0x2e keysize=8 ---")
o=0x2e
# skip a u32 then treat next as count? instead just walk from the known 'Dialogue'
o=0x6e5
for _ in range(3):
    nm,o=fstr(o); c,=struct.unpack('<I',d[o:o+4]); o+=4
    print("  ns %r count=%d"%(nm,c))
    print("   first key bytes:", d[o:o+16].hex(' '))
    o+=c*8
print("  next at 0x%x"%o)
