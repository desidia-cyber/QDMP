import os, struct
PAK = r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Content\Paks\QueensDomain-WindowsNoEditor.pak"
size=os.path.getsize(PAK); idx=1973612650; isz=529218
f=open(PAK,'rb')
def rd(o,n):
    f.seek(o); return f.read(n)
h=rd(idx, 256)
def hd(b, base):
    for i in range(0,len(b),16):
        row=b[i:i+16]
        print("%08x  %-47s  %s"%(base+i,' '.join('%02x'%x for x in row),''.join(chr(x) if 32<=x<127 else '.' for x in row)))
hd(h, idx)
print()
print("=== region at index_end (PathHashIndex?) ===")
hd(rd(idx+isz, 64), idx+isz)
c,=struct.unpack('<i', rd(idx+isz,4))
print("count at index_end:", c)
print()
print("=== last 64 bytes before footer ===")
foot=1976294166
hd(rd(foot-64,64), foot-64)
print()
# what is at the two suspicious offsets
for off in (1974268844, 1974213183):
    print("at",off, rd(off,32).hex(' '), rd(off,32)[:16])
