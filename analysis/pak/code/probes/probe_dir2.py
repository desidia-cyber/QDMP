import struct
PAK = r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Content\Paks\QueensDomain-WindowsNoEditor.pak"
f=open(PAK,'rb')
def rd(o,n):
    f.seek(o); return f.read(n)
di=1974670976
nd,=struct.unpack('<i', rd(di,4)); o=di+4
def rdstr(o):
    n,=struct.unpack('<i', rd(o,4)); o+=4
    if n==0: return "",o
    if n<0:
        b=rd(o,-n); o+=-n
        return b.decode('utf-16-le','replace').split('\x00')[0],o
    b=rd(o,n); o+=n
    return b.split(b'\x00')[0].decode('latin-1'),o
for i in range(nd):
    dname,o=rdstr(o)
    npos=o
    n,=struct.unpack('<i', rd(o,4)); o+=4
    if n<0 or n>1000:
        print("ANOMALY dir",i,"at",hex(npos),"n=",n,"dirname",repr(dname[:60])); break
    for j in range(n):
        nm,o=rdstr(o)
        loc,=struct.unpack('<i', rd(o,4)); o+=4
    if i<8 or n>20: print(i, repr(dname[:50]), n)
    if o>1976294166: print("OVERRUN at dir",i); break
else:
    print("done")
print("end",o,"total dirs",nd, "remaining to footer", 1976294166-o)
