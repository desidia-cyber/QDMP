import struct
def trace(d,S,verbose=False):
    o=S; n=0
    while o < len(d):
        if o+8>len(d): return ("eof-short",o,n)
        x,=struct.unpack('<I',d[o:o+4]); l,=struct.unpack('<i',d[o+4:o+8])
        if l==0: o+=8; n+=1; continue
        if l<0:
            nb=-l*2
            if o+8+nb>len(d): return ("utf16-overrun",o,n,l)
            if d[o+8+nb-1]!=0 or d[o+8+nb-2]!=0: return ("utf16-noterm",o,n,l)
        else:
            nb=l
            if l>4000: return ("len-too-big",o,n,l)
            if o+8+nb>len(d): return ("overrun",o,n,l)
            if d[o+8+nb-1]!=0: return ("noterm",o,n,l)
        o+=8+nb; n+=1
    return ("OK",o,n)
d=open(r'G:\Freespace\Queens_Domain\analysis\scratch\l.locres','rb').read()
r=trace(d,85689); print("game.locres:",r, "file", len(d))
print("bytes at stop:", d[r[1]-8:r[1]+40].hex(' '))
print("as text:", d[r[1]-8:r[1]+80])
