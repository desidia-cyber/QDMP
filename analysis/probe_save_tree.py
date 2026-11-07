import struct, os, json

D = os.path.expandvars(r"%LOCALAPPDATA%\QueensDomain\Saved\SaveGames")

class R:
    def __init__(s, b, p=0): s.b=b; s.p=p
    def i32(s):
        v=struct.unpack_from("<i",s.b,s.p)[0]; s.p+=4; return v
    def u8(s):
        v=s.b[s.p]; s.p+=1; return v
    def u16(s):
        v=struct.unpack_from("<H",s.b,s.p)[0]; s.p+=2; return v
    def raw(s,n):
        v=s.b[s.p:s.p+n]; s.p+=n; return v
    def fstr(s):
        n=s.i32()
        if n==0: return ""
        if n<0: return s.raw(-n*2)[:-2].decode("utf-16-le","replace")
        return s.raw(n)[:-1].decode("utf-8","replace")
    def left(s): return len(s.b)-s.p

def read_tag(r):
    name = r.fstr()
    if name in ("", "None"): return None
    typ = r.fstr()
    size = r.i32()
    idx = r.i32()
    t = {"name":name,"type":typ,"size":size,"idx":idx}
    if typ=="StructProperty":
        t["struct"]=r.fstr()
        t["sguid"]=r.raw(16).hex()
    elif typ=="BoolProperty":
        t["bool"]=r.u8()
    elif typ in ("ByteProperty","EnumProperty"):
        t["enum"]=r.fstr()
    elif typ=="ArrayProperty":
        t["inner"]=r.fstr()
    elif typ=="MapProperty":
        t["inner"]=r.fstr(); t["value_type"]=r.fstr()
    elif typ=="SetProperty":
        t["inner"]=r.fstr()
    hasguid = r.u8()          # empirical: 1 pad/guid flag byte
    if hasguid:
        t["propguid"]=r.raw(16).hex()
    t["vstart"]=r.p
    return t

def decode_value(r, t, depth=0, log=None):
    """Best-effort decode of a property value."""
    typ=t["type"]; start=r.p; end=t["vstart"]+t["size"]
    try:
        if typ=="IntProperty":
            return r.i32()
        if typ=="FloatProperty":
            return struct.unpack_from("<f", r.b, r.p)[0]
        if typ=="BoolProperty":
            return t.get("bool")
        if typ in ("StrProperty","NameProperty","TextProperty"):
            return r.fstr()
        if typ=="MapProperty":
            r.p = start + 4              # skip removed-count
            n = r.i32()
            items=[]
            for _ in range(min(n, 4000)):
                if t["inner"]=="NameProperty" or t["inner"]=="StrProperty":
                    k=r.fstr()
                elif t["inner"]=="StructProperty":
                    k=r.raw(16).hex()
                elif t["inner"]=="IntProperty":
                    k=r.i32()
                else:
                    k="?"
                if t["value_type"]=="IntProperty":
                    v=r.i32()
                elif t["value_type"]=="BoolProperty":
                    v=bool(r.u8())
                elif t["value_type"]=="FloatProperty":
                    v=struct.unpack_from("<f", r.b, r.p)[0]; r.p+=4
                elif t["value_type"] in ("NameProperty","StrProperty"):
                    v=r.fstr()
                else:
                    v=None
                    break
                items.append((k,v))
            return items
        if typ=="ArrayProperty":
            r.p = start + 4
            n = r.i32()
            return f"<{t['inner']} array n={n}>"
    except Exception as e:
        return f"<decode error {e}>"
    return None

def dump(path):
    b=open(path,"rb").read(); r=R(b)
    assert r.raw(4)==b"GVAS"
    r.i32(); r.i32(); r.u16(); r.u16(); r.u16(); r.i32(); r.fstr()
    r.i32(); n=r.i32(); r.raw(n*20)
    cls=r.fstr()
    print(f"\n########## {os.path.basename(path)}  ({len(b)} B)  class = {cls}")
    while True:
        try:
            t=read_tag(r)
        except Exception as e:
            print("  [stop]", e); break
        if t is None: break
        val = decode_value(r, t)
        desc = f"  {t['name']:<32} {t['type']:<15} sz={t['size']:<6}"
        if t.get('struct'): desc += f" struct={t['struct']} "
        if t.get('inner'): desc += f" k={t['inner']} "
        if t.get('value_type'): desc += f" v={t['value_type']}"
        print(desc)
        if isinstance(val, list):
            print(f"      -> {len(val)} entries")
            for k,v in val[:80]:
                print(f"         {k!r:<60} = {v}")
            if len(val)>80: print(f"         ... +{len(val)-80} more")
        elif val is not None:
            print(f"      -> {val!r}")
        r.p = t["vstart"] + t["size"]
        if r.left() <= 0: break

for f in ["System0.sav","Challenges","World0.sav"]:
    dump(os.path.join(D,f))
