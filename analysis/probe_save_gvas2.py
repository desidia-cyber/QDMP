import struct, os, re
D = os.path.expandvars(r"%LOCALAPPDATA%\QueensDomain\Saved\SaveGames")
class R:
    def __init__(s,b): s.b=b; s.p=0
    def i32(s):
        v=struct.unpack_from("<i",s.b,s.p)[0]; s.p+=4; return v
    def u16(s):
        v=struct.unpack_from("<H",s.b,s.p)[0]; s.p+=2; return v
    def u32(s):
        v=struct.unpack_from("<I",s.b,s.p)[0]; s.p+=4; return v
    def raw(s,n):
        v=s.b[s.p:s.p+n]; s.p+=n; return v
    def fstr(s):
        n=s.i32()
        if n==0: return ""
        if n<0: return s.raw(-n*2)[:-2].decode("utf-16-le","replace")
        return s.raw(n)[:-1].decode("utf-8","replace")

for name in ["World0.sav","System0.sav","Challenges"]:
    p=os.path.join(D,name); b=open(p,"rb").read(); r=R(b)
    assert r.raw(4)==b"GVAS"
    sv=r.i32(); pv=r.i32(); major=r.u16(); minor=r.u16(); patch=r.u16(); r.u32(); branch=r.fstr()
    cvf=r.i32(); ncv=r.i32(); r.raw(ncv*20)
    cls=r.fstr()
    print(f"\n=== {name} len={len(b)} ver={sv} pkg={pv} eng={major}.{minor}.{patch} cvf={cvf} ncv={ncv}")
    print(f"  class={cls!r} body@{r.p} remaining={len(b)-r.p}")
    tail=b[r.p:]
    runs=[m.group().decode('ascii','replace') for m in re.finditer(rb"[\x20-\x7E]{4,}",tail)]
    print(f"  runs={len(runs)}")
    for s in runs[:50]: print("     ",s)
