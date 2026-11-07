import struct
PAK = r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Content\Paks\QueensDomain-WindowsNoEditor.pak"
f=open(PAK,'rb')
def rd(o,n):
    f.seek(o); return f.read(n)
def rdstr(o):
    n,=struct.unpack('<i', rd(o,4)); o+=4
    if n==0: return "",o
    if n<0:
        b=rd(o,-n); o+=-n; return b.decode('utf-16-le','replace').split('\x00')[0],o
    b=rd(o,n); o+=n; return b.split(b'\x00')[0].decode('latin-1'),o
BLOB = 1973612764
o=1974752831
nd,=struct.unpack('<i', rd(o,4)); o+=4
files=[]
for i in range(nd):
    dn,o=rdstr(o); n,=struct.unpack('<i', rd(o,4)); o+=4
    for j in range(n):
        nm,o=rdstr(o); loc,=struct.unpack('<i', rd(o,4)); o+=4
        files.append((dn+nm,loc))
print("files",len(files))
# ground truth: DefaultEngine.ini at 0x78AFB4E
TGT=0x78AFB4E
print("target", TGT, hex(TGT), "LE:", struct.pack('<I',TGT).hex(' '))
for name,loc in files:
    if name.lower().endswith('defaultengine.ini'):
        e=rd(BLOB+loc,12)
        print("%-60s loc=%6d  %s"%(name,loc,' '.join('%02x'%x for x in e)))
print()
print("=== check payload at 0x78AFB4E ===")
print(rd(TGT,80))
