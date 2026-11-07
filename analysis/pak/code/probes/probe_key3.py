import struct
d=open(r'G:\Freespace\Queens_Domain\analysis\scratch\l.locres','rb').read()
TAB=85689
o=0x26; n=0
while o<TAB:
    a,b,c=struct.unpack('<III', d[o:o+12])
    ln,=struct.unpack('<i',d[o+12:o+16])
    if ln<0 or ln>200 or o+16+ln>TAB or d[o+16+ln-1]!=0:
        print("STOP at 0x%x after %d recs; a=%d b=%d c=0x%x lenfield=%d"%(o,n,a,b,c,ln))
        print("  bytes:", d[o-8:o+56].hex(' '))
        print("  text :", d[o-8:o+56])
        break
    n+=1; o+=16+ln
print("parsed",n,"next 0x%x"%o,"TAB 0x%x"%TAB)
