import struct
PAK = r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Content\Paks\QueensDomain-WindowsNoEditor.pak"
f=open(PAK,'rb')
def rd(o,n):
    f.seek(o); return f.read(n)
def hd(o,n,label=""):
    print("---",label,hex(o))
    b=rd(o,n)
    for i in range(0,len(b),16):
        row=b[i:i+16]
        print("%08x  %-47s  %s"%(o+i,' '.join('%02x'%x for x in row),''.join(chr(x) if 32<=x<127 else '.' for x in row)))
hd(0x78AF800, 96, "DefaultEngine.ini start (decoded offset)")
hd(6130758, 96, "ScreenResolution.uasset (decoded offset)")
hd(6130758-64, 96, "64 bytes before it")
hd(0, 48, "pak offset 0")
