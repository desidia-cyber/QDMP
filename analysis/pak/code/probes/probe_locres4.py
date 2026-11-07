import struct
d=open(r'G:\Freespace\Queens_Domain\analysis\scratch\l.locres','rb').read()
def hd(o,n):
    for i in range(o,min(o+n,len(d)),16):
        row=d[i:i+16]
        print('%04x  %-47s %s'%(i,' '.join('%02x'%x for x in row),''.join(chr(x) if 32<=x<127 else '.' for x in row)))
hd(0x2a,0x120)
