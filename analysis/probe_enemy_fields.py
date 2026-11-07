import os, struct, collections

SAV = os.path.expandvars(r"%LOCALAPPDATA%\QueensDomain\Saved\SaveGames\World0.sav")
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
    size = struct.unpack_from("<i", d, p)[0]; p += 4
    p += 4  # arrayIndex
    if typ == "StructProperty":
        _, p = fstr(d, p); p += 16
    elif typ == "BoolProperty":
        p += 1
    elif typ in ("ByteProperty", "EnumProperty", "ArrayProperty", "SetProperty"):
        _, p = fstr(d, p)
    elif typ == "MapProperty":
        _, p = fstr(d, p); _, p = fstr(d, p)
    if d[p]: p += 17
    else: p += 1
    return {"name": name, "type": typ, "size": size, "vstart": p}

# locate EnemySaveStates tag
p = 12 + 10
_, p = fstr(b, p)                       # branch
p += 4
ncv = struct.unpack_from("<i", b, p)[0]; p += 4 + ncv*20
_, p = fstr(b, p)                       # class name

target = None
while p < len(b):
    t = read_tag(b, p)
    if t is None: break
    if t["name"] == "EnemySaveStates":
        target = t; break
    p = t["vstart"] + t["size"]

start = target["vstart"] + 8
n = struct.unpack_from("<i", b, target["vstart"] + 4)[0]
STRIDE = 157
props = collections.Counter()
vals = collections.defaultdict(collections.Counter)
patterns = collections.Counter()

for i in range(n):
    base = start + i * STRIDE
    q = base + 16
    names = []
    while q < base + STRIDE:
        t = read_tag(b, q)
        if t is None: break
        names.append(t["name"])
        props[t["name"]] += 1
        st = t["vstart"]
        if t["type"] == "BoolProperty":
            vals[t["name"]][b[st]] += 1
        elif t["type"] == "FloatProperty":
            v = struct.unpack_from("<f", b, st)[0]
            vals[t["name"]]["%g" % v] += 1
        elif t["type"] == "IntProperty":
            vals[t["name"]][struct.unpack_from("<i", b, st)[0]] += 1
        q = st + t["size"]
    patterns[tuple(names)] += 1

print(f"records: {n}   stride: {STRIDE}\n")
print("=== property name -> how many of the 414 records carry it ===")
for k, c in props.most_common():
    print(f"  {k:<24} {c}")
print("\n=== distinct property-name patterns ===")
for pat, c in patterns.most_common():
    print(f"  x{c}: {pat}")
print("\n=== value distributions ===")
for k, c in vals.items():
    top = c.most_common(6)
    print(f"  {k}: {top}")
