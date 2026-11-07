import struct, collections
for p in (r'G:\Freespace\Queens_Domain\analysis\scratch\e.locres', r'G:\Freespace\Queens_Domain\analysis\scratch\l.locres'):
    d=open(p,'rb').read()
    print("\n#####",p,len(d))
    print("head:", d[:0x40].hex(' '))
    print("  magic",d[:4].hex(),"byte4",d[4])
    # scan for the (u32 prefix)(u32 len)(ascii) pattern in the last 60KB
    vals=collections.Counter(); cnt=0
    for o in range(max(0,len(d)-70000), len(d)-8):
        n,=struct.unpack('<i',d[o:o+4])
        if 1<=n<=400 and o+4+n<=len(d):
            s=d[o+4:o+4+n]
            if s.endswith(b'\x00') and all(9<=c<127 for c in s[:-1]) and len(s[:-1])>0:
                pre,=struct.unpack('<I',d[o-4:o])
                vals[pre]+=1; cnt+=1
    print("  ascii-len-prefixed strings in tail:",cnt," preceding u32 histogram:",vals.most_common(6))
