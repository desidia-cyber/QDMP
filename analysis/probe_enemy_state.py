import os,struct
p=os.path.expandvars(r"%LOCALAPPDATA%\QueensDomain\Saved\SaveGames\World0.sav")
b=open(p,"rb").read()
class R:
    def __init__(s,b,p): s.b=b; s.p=p
    def i32(s):
        v=struct.unpack_from("<i",s.b,s.p)[0]; s.p+=4; return v
    def u8(s):
        v=s.b[s.p]; s.p+=1; return v
    def raw(s,n):
        v=s.b[s.p:s.p+n]; s.p+=n; return v
    def fstr(s):
        n=s.i32()
        if n==0: return ""
        if n<0: return s.raw(-n*2)[:-2].decode("utf-16-le","replace")
        return s.raw(n)[:-1].decode("utf-8","replace")
def read_tag(r):
    name=r.fstr()
    if name in ("","None"): return None
    typ=r.fstr(); size=r.i32(); idx=r.i32(); t={"name":name,"type":typ,"size":size,"idx":idx}
    if typ=="StructProperty": t["struct"]=r.fstr(); r.raw(16)
    elif typ=="BoolProperty": t["bool"]=r.u8()
    elif typ in ("ByteProperty","EnumProperty"): t["enum"]=r.fstr()
    elif typ=="ArrayProperty": t["inner"]=r.fstr()
    elif typ=="MapProperty": t["inner"]=r.fstr(); t["value_type"]=r.fstr()
    elif typ=="SetProperty": t["inner"]=r.fstr()
    g=r.u8()
    if g: r.raw(16)
    t["vstart"]=r.p
    return t
start=0x39d4
r=R(b,start); r.i32(); n=r.i32()
print("entries:",n)
for i in range(2):
    guid=r.raw(16).hex(); vstart=r.p
    print(f"\n--- entry {i}: {guid} ---")
    while True:
        t=read_tag(r)
        if t is None: break
        val=None
        st=t["vstart"]
        try:
            if t["type"]=="BoolProperty": val=t.get("bool")
            elif t["type"]=="IntProperty": val=struct.unpack_from("<i",b,st)[0]
            elif t["type"]=="FloatProperty": val=struct.unpack_from("<f",b,st)[0]
            elif t["type"] in ("NameProperty","ObjectProperty","StrProperty","SoftObjectProperty"): val=R(b,st).fstr()
        except Exception as e: val=f"<{e}>"
        print(f"    {t['name']:<28} {t['type']:<16} sz={t['size']:<4} = {val!r}")
        r.p = st + t["size"]
    print(f"    [value struct bytes: {r.p-vstart}]")
