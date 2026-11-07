import struct
PAK = r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Content\Paks\QueensDomain-WindowsNoEditor.pak"
f=open(PAK,'rb')
def rd(o,n):
    f.seek(o); return f.read(n)
def hd(o,n):
    b=rd(o,n)
    for i in range(0,len(b),16):
        row=b[i:i+16]
        print("%08x  %-47s  %s"%(o+i,' '.join('%02x'%x for x in row),''.join(chr(x) if 32<=x<127 else '.' for x in row)))
print("=== at 1974752831 (end of 287-dir index) ===")
hd(1974752831, 160)
print()
print("=== 4KB later ===")
hd(1974752831+4000, 64)
print()
print("=== near end, 200 bytes before footer ===")
hd(1976294166-200, 200)
