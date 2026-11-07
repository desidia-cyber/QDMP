import struct
PAK = r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Content\Paks\QueensDomain-WindowsNoEditor.pak"
f=open(PAK,'rb')
def rd(o,n):
    f.seek(o); return f.read(n)
d=rd(0x5d8c7b, 200)
for i in range(0,len(d),16):
    row=d[i:i+16]
    print("%04x  %-47s  %s"%(i,' '.join('%02x'%x for x in row),''.join(chr(x) if 32<=x<127 else '.' for x in row)))
