import struct, os
D = os.path.expandvars(r"%LOCALAPPDATA%\QueensDomain\Saved\SaveGames")
class R:
    def __init__(s,b,p=0): s.b=b; s.p=p
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
def read_tag(r):
    name=r.fstr()
    if name in ("","None"): return None
    typ=r.fstr(); size=r.i32(); idx=r.i32(); t={"name":name,"type":typ,"size":size}
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
def rd(r,ty):
    if ty=="IntProperty": return r.i32()
    if ty=="FloatProperty":
        v=struct.unpack_from("<f",r.b,r.p)[0]; r.p+=4; return v
    if ty=="BoolProperty": return bool(r.u8())
    if ty in ("ObjectProperty","NameProperty","StrProperty","SoftObjectProperty"): return r.fstr()
    if ty=="StructProperty":
        sg=r.raw(16); return "S:"+sg.hex()
    return None
b=open(os.path.join(D,"World0.sav"),"rb").read(); r=R(b)
r.raw(4); r.i32(); r.i32(); r.u16(); r.u16(); r.u16(); r.i32(); r.fstr(); r.i32()
n=r.i32(); r.raw(n*20); r.fstr()
while True:
    t=read_tag(r)
    if t is None: break
    nm=t["name"]; typ=t["type"]; st=t["vstart"]
    if typ=="MapProperty" and nm in ("InventoryContents","Equipment","EquipmentSlots","WeaponCharges","DaggerCharges","SavableActorStates","NamedSaveData"):
        r.p=st+4; c=r.i32(); print(f"\n--- {nm} ({c} entries) ---")
        for _ in range(min(c,300)):
            k=rd(r,t["inner"]); v=rd(r,t["value_type"])
            print(f"    {k!r:<95} = {v}")
    elif typ=="ArrayProperty" and nm in ("CollectedNotes","SavePointsVisited"):
        r.p=st+4; c=r.i32(); print(f"\n--- {nm} ({c}) ---")
        for _ in range(min(c,120)):
            print("    ", rd(r, t["inner"]))
    elif typ=="SetProperty" and nm=="NPCsInJournal":
        r.p=st+4; c=r.i32(); print(f"\n--- {nm} ({c}) ---")
        for _ in range(min(c,120)):
            print("    ", rd(r, t["inner"]))
    elif nm in ("EnemySaveStates",):
        print(f"\n--- {nm} raw size={t['size']} ---")
        r.p=st+4; c=r.i32(); print("    declared entries:", c)
        for i in range(min(c,10)):
            k=rd(r,t["inner"]); print("    key:",k)
    r.p=t["vstart"]+t["size"]
