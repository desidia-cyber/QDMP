import os, struct
PAK = r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Content\Paks\QueensDomain-WindowsNoEditor.pak"
f=open(PAK,'rb')
def rd(o,n):
    f.seek(o); return f.read(n)
di=1974670976
h=rd(di, 420)
def hd(b, base):
    for i in range(0,len(b),16):
        row=b[i:i+16]
        print("%08x  %-47s  %s"%(base+i,' '.join('%02x'%x for x in row),''.join(chr(x) if 32<=x<127 else '.' for x in row)))
hd(h, di)
