import struct, os, sys, json

D = os.path.expandvars(r"%LOCALAPPDATA%\QueensDomain\Saved\SaveGames")

class R:
    def __init__(self, b): self.b=b; self.p=0
    def u8(self):
        v=self.b[self.p]; self.p+=1; return v
    def i32(self):
        v=struct.unpack_from("<i",self.b,self.p)[0]; self.p+=4; return v
    def u32(self):
        v=struct.unpack_from("<I",self.b,self.p)[0]; self.p+=4; return v
    def u64(self):
        v=struct.unpack_from("<Q",self.b,self.p)[0]; self.p+=8; return v
    def f32(self):
        v=struct.unpack_from("<f",self.b,self.p)[0]; self.p+=4; return v
    def raw(self,n):
        v=self.b[self.p:self.p+n]; self.p+=n; return v
    def fstr(self):
        n=self.i32()
        if n==0: return ""
        if n<0:
            return self.raw(-n*2)[:-2].decode("utf-16-le","replace")
        return self.raw(n)[:-1].decode("utf-8","replace")
    def guid(self):
        return self.raw(16).hex()

def parse(path):
    b=open(path,"rb").read()
    r=R(b)
    magic=r.raw(4)
    assert magic==b"GVAS", magic
    sv=r.i32(); pv=r.i32()
    major=r.u32() & 0xFFFF
    minor=r.u32() & 0xFFFF
    patch=r.u32() & 0xFFFF
    cl=r.u32()
    branch=r.fstr()
    cvf=r.i32()
    ncv=r.i32()
    cvs=[(r.guid(), r.i32()) for _ in range(ncv)]
    print(f"\n=== {os.path.basename(path)} ({len(b)} bytes) ===")
    print(f"  SaveGameFileVersion={sv} PackageFileVersion={pv} Engine={major}.{minor}.{patch} CL={cl} branch={branch!r}")
    print(f"  CustomVersionFormat={cvf} count={ncv}  header ends at 0x{r.p:X}")
    cls=r.fstr()
    print(f"  SaveGameClassName = {cls!r}")
    print(f"  object body starts at 0x{r.p:X}, remaining {len(b)-r.p} bytes")
    # dump property-like strings from the remainder
    tail=b[r.p:]
    import re
    strs=[m.group().decode('ascii','replace') for m in re.finditer(rb"[\x20-\x7E]{5,}", tail)]
    print(f"  tail ascii runs: {len(strs)}")
    for s in strs[:60]:
        print("     ", s)
    return b, r.p

for n in ["World0.sav","System0.sav","Challenges"]:
    try: parse(os.path.join(D,n))
    except Exception as e: print(n, "ERR", e)
