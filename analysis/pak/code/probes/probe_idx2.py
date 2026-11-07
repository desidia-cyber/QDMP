import os, struct, collections
PAK = r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Content\Paks\QueensDomain-WindowsNoEditor.pak"
size=os.path.getsize(PAK); idx=1973612650; isz=529218
f=open(PAK,'rb')
def rd(o,n):
    f.seek(o); return f.read(n)
phi=idx+isz
cnt,=struct.unpack('<i', rd(phi,4))
print("PathHashIndex count", cnt)
raw=rd(phi+4, cnt*12)
pairs=[]
for i in range(cnt):
    h,off=struct.unpack('<Qi', raw[i*12:i*12+12])
    pairs.append((h,off))
offs=sorted(o for h,o in pairs)
print("min off",offs[0],"max off",offs[-1],"distinct",len(set(offs)))
d=[b-a for a,b in zip(offs,offs[1:])]
print("stride hist", collections.Counter(d).most_common(10))
print("first 10 sorted offsets", offs[:10])
print("last 10 sorted offsets", offs[-10:])
# is the offset list exactly 0,12,24,...?
uniform = all(offs[i]==i*12 for i in range(len(offs)))
print("uniform 12-byte slots:", uniform, " expected span", cnt*12)
# FullDirectoryIndex
di=phi+4+cnt*12
nd,=struct.unpack('<i', rd(di,4))
print("numDirs", nd, "at", di)
o=di+4
def rdstr(o):
    n,=struct.unpack('<i', rd(o,4)); o+=4
    if n==0: return "",o
    if n<0:
        s=rd(o,-n); o+=-n
        return s.split(b'\x00')[0].decode('latin-1'),o
    s=rd(o,n); o+=n
    return s.split(b'\x00')[0].decode('latin-1'),o
tot=0
for i in range(nd):
    dname,o=rdstr(o)
    n,=struct.unpack('<i', rd(o,4)); o+=4
    tot+=n
    if i<5: print("  dir",repr(dname),n)
    for j in range(n):
        nm,o=rdstr(o); loc,=struct.unpack('<i', rd(o,4)); o+=4
        if i<2 and j<3: print("    ",repr(nm),loc)
print("total files in dir index", tot, "end offset", o, "footer", 1976294166, "delta", 1976294166-o)
