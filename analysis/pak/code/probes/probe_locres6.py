import struct
d=open(r'G:\Freespace\Queens_Domain\analysis\scratch\l.locres','rb').read()
o=85689; n=0; bad=None; prefixes=[]
ok=True
while o < len(d):
    if o+8>len(d): break
    x,=struct.unpack('<I',d[o:o+4]); l,=struct.unpack('<i',d[o+4:o+8])
    if l<0 or l>600 or o+8+l>len(d) or (l>0 and d[o+7+l-1]!=0):
        bad=(o,x,l,d[o:o+24]); break
    prefixes.append(x); o+=8+l; n+=1
print("parsed entries:",n,"stopped at",bad[0] if bad else o,"file len",len(d))
if bad: print("  at stop: x=%d len=%d bytes=%s"%(bad[1],bad[2],bad[3].hex(' ')))
print("last 8 prefixes:", prefixes[-8:])
import collections
print("prefix histogram top:", collections.Counter(prefixes).most_common(6))
