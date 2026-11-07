import struct, collections
PAK = r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Content\Paks\QueensDomain-WindowsNoEditor.pak"
f=open(PAK,'rb')
def rd(o,n):
    f.seek(o); return f.read(n)
def rdstr(o):
    n,=struct.unpack('<i', rd(o,4)); o+=4
    if n==0: return "",o
    if n<0:
        b=rd(o,-n); o+=-n
        return b.decode('utf-16-le','replace').split('\x00')[0],o
    b=rd(o,n); o+=n
    return b.split(b'\x00')[0].decode('latin-1'),o
def parse_dirindex(start):
    nd,=struct.unpack('<i', rd(start,4)); o=start+4
    dirs=[]; files=[]
    for i in range(nd):
        dname,o=rdstr(o)
        n,=struct.unpack('<i', rd(o,4)); o+=4
        if n<0 or n>200000: raise ValueError("bad n %d dir %d at %x"%(n,i,o))
        fl=[]
        for j in range(n):
            nm,o=rdstr(o)
            loc,=struct.unpack('<i', rd(o,4)); o+=4
            fl.append((nm,loc)); files.append((dname+nm,loc))
        dirs.append((dname,fl))
    return nd,dirs,files,o
for start in (1974670976, 1974752831):
    try:
        nd,dirs,files,o = parse_dirindex(start)
        print("index at",start,"dirs",nd,"files",len(files),"end",o)
        locs=[l for _,l in files]
        print("  loc range", min(locs), max(locs), "all mult of 12:", all(l%12==0 and 0<=l<529104 for l in locs))
        print("  sample:", files[0][0], files[0][1], "|", files[-1][0], files[-1][1])
    except Exception as e:
        print("index at",start,"FAILED:",e)
