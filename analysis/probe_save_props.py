import struct, os, re, json, sys

D = os.path.expandvars(r"%LOCALAPPDATA%\QueensDomain\Saved\SaveGames")

class R:
    def __init__(s,b): s.b=b; s.p=0
    def i32(s):
        v=struct.unpack_from("<i",s.b,s.p)[0]; s.p+=4; return v
    def u32(s):
        v=struct.unpack_from("<I",s.b,s.p)[0]; s.p+=4; return v
    def i64(s):
        v=struct.unpack_from("<q",s.b,s.p)[0]; s.p+=8; return v
    def u16(s):
        v=struct.unpack_from("<H",s.b,s.p)[0]; s.p+=2; return v
    def f32(s):
        v=struct.unpack_from("<f",s.b,s.p)[0]; s.p+=4; return v
    def u8(s):
        v=s.b[s.p]; s.p+=1; return v
    def raw(s,n):
        v=s.b[s.p:s.p+n]; s.p+=n; return v
    def fstr(s):
        n=s.i32()
        if n==0: return ""
        if n<0: return s.raw(-n*2)[:-2].decode("utf-16-le","replace")
        return s.raw(n)[:-1].decode("utf-8","replace")
    def eof(s): return s.p>=len(s.b)

def read_tag(r):
    name = r.fstr()
    if name == "" or name == "None":
        return None
    typ = r.fstr()
    # size: UE4.27 uses int32 for SaveGame archives by default
    size = r.i32()
    if size < 0 or size > len(r.b):
        raise ValueError(f"bad size {size} for {name}")
    aidx = r.i32()
    tag = {"name":name,"type":typ,"size":size,"idx":aidx,"vstart":r.p}
    if typ == "StructProperty":
        tag["struct"]=r.fstr()
        tag["guid"]=r.raw(16).hex()
    elif typ == "BoolProperty":
        tag["bool"]=r.u8()
    elif typ in ("ByteProperty","EnumProperty"):
        tag["enum"]=r.fstr()
    elif typ == "ArrayProperty":
        tag["inner"]=r.fstr()
    elif typ == "MapProperty":
        tag["inner"]=r.fstr()
        tag["value_type"]=r.fstr()
    elif typ == "SetProperty":
        tag["inner"]=r.fstr()
    tag["vstart"]=r.p
    return tag

def dump(path, maxdepth=6):
    b=open(path,"rb").read(); r=R(b)
    assert r.raw(4)==b"GVAS"
    r.i32(); r.i32(); r.u16(); r.u16(); r.u16(); r.u32(); r.fstr()
    r.i32(); n=r.i32(); r.raw(n*20)
    cls=r.fstr()
    print(f"\n##### {os.path.basename(path)}  class={cls}")
    count=0
    out=[]
    while not r.eof():
        try:
            t=read_tag(r)
        except Exception as e:
            print("  [parse stopped]", e); break
        if t is None: break
        count+=1
        out.append(t)
        print(f"  {t['name']:<34} {t['type']:<15} size={t['size']:<7} idx={t['idx']} "
              + (f"struct={t.get('struct')} " if t.get('struct') else "")
              + (f"inner={t.get('inner')} " if t.get('inner') else "")
              + (f"valueType={t.get('value_type')}" if t.get('value_type') else ""))
        # skip value
        r.p = t["vstart"] + t["size"]
        if count>200: 
            print("  ... truncated"); break
    print(f"  -> {count} top-level properties")
    return out

dump(os.path.join(D,"World0.sav"))
