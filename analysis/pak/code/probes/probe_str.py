import struct
d=open(r'G:\Freespace\Queens_Domain\analysis\scratch\l.locres','rb').read()
o=85689; out=[]
while o+8<=len(d):
    x,=struct.unpack('<I',d[o:o+4]); l,=struct.unpack('<i',d[o+4:o+8])
    if l==0: o+=8; out.append((x,"")); continue
    if l<0:
        nb=-l*2; s=d[o+8:o+8+nb].decode('utf-16-le','replace').split('\x00')[0]; o+=8+nb
    else:
        if o+8+l>len(d) or d[o+8+l-1]!=0: break
        s=d[o+8:o+8+l-1].decode('latin-1'); o+=8+l
    out.append((x,s))
print("strings:",len(out),"end",o,"len",len(d))
for i,(x,s) in enumerate(out[:25]): print("%4d id=%-6d %r"%(i,x,s))
print("...")
for i,(x,s) in enumerate(out[-15:]): print("%4d id=%-6d %r"%(len(out)-15+i,x,s))
import statistics
print("id range", min(a for a,_ in out), max(a for a,_ in out))
