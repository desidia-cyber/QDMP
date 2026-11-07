import struct, collections
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
BLOB=1973612764
o=1974752831
nd,=struct.unpack('<i', rd(o,4)); o+=4
files=[]
for i in range(nd):
    dn,o=rdstr(o); n,=struct.unpack('<i', rd(o,4)); o+=4
    for j in range(n):
        nm,o=rdstr(o); loc,=struct.unpack('<i', rd(o,4)); o+=4
        files.append((dn+nm,loc))
blob=rd(BLOB, 44092*12)
E=[(n,)+struct.unpack('<III', blob[l:l+12])+(l,) for n,l in files]
E.sort(key=lambda x:x[1])
# gap analysis
gaps=collections.Counter()
for i in range(len(E)-1):
    n,a,b,c,l=E[i]; nn,na,_,_,_=E[i+1]
    gaps[na-(a+b)]+=1
print("gap hist (next_off - (off+size)):", gaps.most_common(6))
# now test payload at off+H
for H in (52,53,48,49):
    ok=sum(1 for n,a,b,c,l in E if n.lower().endswith(('.uasset','.umap')) and rd(a+H,4)==b'\xc1\x83\x2a\x9e')
    tot=sum(1 for n,a,b,c,l in E if n.lower().endswith(('.uasset','.umap')))
    print("H=%d magic pass %d/%d = %.2f%%"%(H,ok,tot,100.0*ok/tot))
# dump inline header for 3 entries varying
print()
for n,a,b,c,l in E[:3]:
    d=rd(a,56)
    print("%-55s off=%d size=%d c=%08x"%(n[:55],a,b,c))
    print("   ", ' '.join('%02x'%x for x in d))
