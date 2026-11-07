import struct, hashlib, random, collections
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
zero=[e for e in E if e[3]==0]
print("entries with c==0:", zero)
random.seed(42); samp=random.sample(E, 250)
okh=okhash=0; fails=[]
for n,a,b,c,l in samp:
    hdr=rd(a,53)
    i64_0, s1, s2 = struct.unpack('<qqq', hdr[0:24])
    cm, = struct.unpack('<I', hdr[24:28])
    h = hdr[28:48]
    tail = hdr[48:53]
    if s1==b and s2==b: okh+=1
    d=rd(a+53,b)
    if hashlib.sha1(d).digest()==h: okhash+=1
    else:
        if len(fails)<3: fails.append((n,b,hashlib.sha1(d).hexdigest()[:16],h.hex()[:16]))
print("inline Size==index size: %d/%d"%(okh,len(samp)))
print("SHA1(payload)==inline hash: %d/%d = %.2f%%"%(okhash,len(samp),100.0*okhash/len(samp)))
print("fails:",fails)
print()
# byte0 (first i64) nonzero count over all
nz0=0
for n,a,b,c,l in E[:2000]:
    if struct.unpack('<q', rd(a,8))[0]!=0: nz0+=1
print("first i64 nonzero in 2000:",nz0)
print("sample inline header tail bytes:")
for n,a,b,c,l in E[:3]:
    print("  ", rd(a,53)[48:53].hex(' '), "| hdr[24:28]", rd(a+24,4).hex(' '))
