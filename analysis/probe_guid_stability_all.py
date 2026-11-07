import os, struct, re, time

SAV = os.path.expandvars(r"%LOCALAPPDATA%\QueensDomain\Saved\SaveGames\World0.sav")
PAK = r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Content\Paks\QueensDomain-WindowsNoEditor.pak"
b = open(SAV, "rb").read()

def fstr(d, p):
    n = struct.unpack_from("<i", d, p)[0]
    if n == 0: return "", p + 4
    if n < 0: return d[p+4:p+4-n*2][:-2].decode("utf-16-le","replace"), p+4-n*2
    return d[p+4:p+4+n][:-1].decode("utf-8","replace"), p+4+n

def read_tag(d, p):
    name, p = fstr(d, p)
    if name in ("", "None"): return None
    typ, p = fstr(d, p)
    size, p = struct.unpack_from("<i", d, p)[0], p + 4
    idx = struct.unpack_from("<i", d, p)[0]; p += 4
    if typ == "StructProperty":
        _, p = fstr(d, p); p += 16
    elif typ == "BoolProperty":
        p += 1
    elif typ in ("ByteProperty", "EnumProperty"):
        _, p = fstr(d, p)
    elif typ in ("ArrayProperty", "SetProperty"):
        _, p = fstr(d, p)
    elif typ == "MapProperty":
        _, p = fstr(d, p); _, p = fstr(d, p)
    if d[p]: p += 17
    else: p += 1
    return {"name": name, "type": typ, "size": size, "vstart": p}

# locate the two maps
sav_title = []
p = 0
# skip header
assert b[:4] == b"GVAS"
p = 12                       # past magic(4) + SaveGameFileVersion(4) + PackageUE4Version(4)
p += 2 + 2 + 2 + 4           # EngineVersion Major/Minor/Patch + Changelist
_, p = fstr(b, p)            # branch
p += 4
ncv = struct.unpack_from("<i", b, p)[0]; p += 4 + ncv * 20
cls, p = fstr(b, p)

maps = {}
while p < len(b):
    t = read_tag(b, p)
    if t is None: break
    if t["name"] in ("SavableActorStates", "EnemySaveStates"):
        maps[t["name"]] = t
    p = t["vstart"] + t["size"]

def keys_fixed(t, stride):
    """Map<Struct(16), T> with a fixed per-entry size: 16-byte key + (stride-16) value."""
    p = t["vstart"] + 4          # skip the map header int
    n = struct.unpack_from("<i", b, p)[0]; p += 4
    assert 8 + n * stride == t["size"], (n, stride, t["size"])
    return [b[p + i*stride: p + i*stride + 16].hex() for i in range(n)]

sav = keys_fixed(maps["SavableActorStates"], 20)     # 16 key + 4 int
en = keys_fixed(maps["EnemySaveStates"], 157)        # 16 key + 141 property stream
print(f"SavableActorStates keys: {len(sav)}")
print(f"EnemySaveStates keys   : {len(en)}")

sample = [(f"SAV{i+1}", g) for i, g in enumerate(sav)] + \
         [(f"EN{i+1}", g) for i, g in enumerate(en)]
needles = {}
for label, g in sample:
    needles[label] = bytes.fromhex(g)

print(f"searching {len(needles)} ids across the pak ...")
hits = {k: 0 for k in needles}
CH, OVER = 16*1024*1024, 64
t0 = time.time(); off = 0; prev = b""
with open(PAK, "rb") as f:
    while True:
        buf = f.read(CH)
        if not buf: break
        data = prev + buf
        for k, v in needles.items():
            if hits[k] == 0 and data.find(v) >= 0:
                hits[k] = 1
        prev = data[-OVER:]
        off += len(buf)
print(f"scan took {time.time()-t0:.1f}s")

found = sum(hits.values())
print(f"\nFOUND {found} / {len(needles)} ids in the pak")
missing = [k for k, v in hits.items() if v == 0]
print("missing:", missing if missing else "(none)")
