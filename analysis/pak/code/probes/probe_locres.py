import struct
p=r'G:\Freespace\Queens_Domain\analysis\scratch\l.locres'
d=open(p,'rb').read()
print("len",len(d))
def hd(o,n):
    for i in range(o,min(o+n,len(d)),16):
        row=d[i:i+16]
        print('%04x  %-47s %s'%(i,' '.join('%02x'%x for x in row),''.join(chr(x) if 32<=x<127 else '.' for x in row)))
print("--- head 0x00..0x60")
hd(0,0x60)
print("--- tail 0x47a00..")
hd(len(d)-160,160)
# look for plausible string table offset: i64 values in first 32 bytes
for o in range(0,32):
    if o+8<=len(d):
        v,=struct.unpack('<q',d[o:o+8])
        if 0 < v < len(d): print("i64 at 0x%02x = %d"%(o,v))
    if o+4<=len(d):
        v,=struct.unpack('<I',d[o:o+4])
        if 1000 < v < len(d): print("u32 at 0x%02x = %d (0x%x)"%(o,v,v))
