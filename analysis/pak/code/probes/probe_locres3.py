import struct
for p,off,cnt in ((r'G:\Freespace\Queens_Domain\analysis\scratch\l.locres',85689,2388),
                  (r'G:\Freespace\Queens_Domain\analysis\scratch\e.locres',862401,20149)):
    d=open(p,'rb').read()
    print("\n####",p,"len",len(d),"offset",off,"claimed count",cnt)
    print("bytes at off:", d[off:off+48].hex(' '))
    print("bytes at off as text:", d[off:off+60])
    print("bytes BEFORE off(16):", d[off-16:off].hex(' '))
    v,=struct.unpack('<I',d[off:off+4]); print("u32 at off =",v, "== claimed count?", v==cnt)
