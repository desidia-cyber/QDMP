import struct, collections
d=open(r'G:\Freespace\Queens_Domain\analysis\scratch\l.locres','rb').read()
o=85689; n=0; prefixes=[]; bad=None
while o < len(d):
    if o+8>len(d): bad=(o,None,None,d[o:o+16]); break
    x,=struct.unpack('<I',d[o:o+4]); l,=struct.unpack('<i',d[o+4:o+8])
    if l<0 or l>600 or o+8+l>len(d) or (l>0 and d[o+8+l-1]!=0):
        bad=(o,x,l,d[o:o+24]); break
    prefixes.append(x); o+=8+l; n+=1
print("parsed:",n,"end at",o,"file",len(d),"match EOF:",o==len(d))
if bad: print("stop at %d x=%r len=%r %s"%(bad[0],bad[1],bad[2],bad[3].hex(' ')))
print("prefix hist:",collections.Counter(prefixes).most_common(5))
print("first prefixes:",prefixes[:6],"last:",prefixes[-6:])
