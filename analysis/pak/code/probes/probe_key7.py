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
shapes={
 'u32+F+i32':(4,4),'u32+F+u32':(4,4),'u32+u32+F':(8,0),'u32+u32+F+i32':(8,4),
 'u32+u32+F+u32':(8,4),'F+u32':(0,4),'F':(0,0),'u32+F':(4,0),
}
def walk(S,pre,post):
    o=S; n=0
    try:
        while o<TAB:
            o+=pre
            nm,o=fstr(o)
            o+=post
            n+=1
            if n>60000: return None
        return n if o==TAB else None
    except Exception: return None
found=False
for S in range(0x1d,0x90):
    for k,(pre,post) in shapes.items():
        r=walk(S,pre,post)
        if r: print("MATCH start=0x%x shape=%-16s records=%d"%(S,k,r)); found=True
print("done, found:",found)
