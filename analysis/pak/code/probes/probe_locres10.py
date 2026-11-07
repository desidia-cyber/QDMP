import struct
d=open(r'G:\Freespace\Queens_Domain\analysis\scratch\l.locres','rb').read()
def fstr(o):
    n,=struct.unpack('<i',d[o:o+4]); o+=4
    if n==0: return "",o
    if n<0:
        nb=-n*2; return d[o:o+nb].decode('utf-16-le','replace').split('\x00')[0], o+nb
    return d[o:o+n].split(b'\x00')[0].decode('latin-1'), o+n
for S in (85685,85689):
    cnt,=struct.unpack('<i',d[S:S+4])
    o=S+4; ok=True; strs=[]
    try:
        for i in range(cnt):
            s,o=fstr(o); strs.append(s)
    except Exception as e:
        ok=False
    print("S=%d count=%d -> end=%d len=%d exact=%s trailing=%s"%(S,cnt,o,len(d),o==len(d),d[o:o+8].hex(' ') if ok else 'ERR'))
    if ok and o<=len(d):
        print("   empty strings:",sum(1 for s in strs if s=='') ,"first5:",strs[:5])
