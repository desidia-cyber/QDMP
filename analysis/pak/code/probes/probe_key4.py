import struct,re
d=open(r'G:\Freespace\Queens_Domain\analysis\scratch\l.locres','rb').read()
def hd(o,n):
    for i in range(o,min(o+n,len(d)),16):
        row=d[i:i+16]
        print('%04x  %-47s %s'%(i,' '.join('%02x'%x for x in row),''.join(chr(x) if 32<=x<127 else '.' for x in row)))
hd(0x5c0,0x150)
print()
# all printable runs >=6 in region 0x1d..0x14eb9 with their offsets
runs=[(m.start(),m.group().decode('latin-1')) for m in re.finditer(rb'[\x20-\x7e]{6,}', d[0x1d:0x14eb9])]
print("printable runs:",len(runs))
nonhex=[(o+0x1d,s) for o,s in runs if not re.fullmatch(r'[0-9A-F]{32}',s)]
print("non-32hex runs:",len(nonhex))
for o,s in nonhex[:40]: print("  0x%05x %r"%(o,s))
