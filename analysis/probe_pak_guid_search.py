import os, struct, sys, time

PAK = r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Content\Paks\QueensDomain-WindowsNoEditor.pak"

# 7 SavableActorStates keys (raw FGuid bytes as stored in the save)
SAVABLE = [
 "8d7e161721ba9c4aabe58f83d3257452",
 "db324584ebf62e46882b9140bd2c803e",
 "cc38ef1138930c4a9ec9cb8deedf02d0",
 "4051b22259600b45ade8fb99bf657206",
 "d31ac888f131c547a1af5461fe7757da",
 "3021d4cf190d734a9c1a6651e3a9c82c",
 "5c92d4cfe099ea4a964701dab3bc5b04",
]
# first enemy keys
ENEMY = [
 "3b5db8ee49d50d4c8b52befb669a28cf",
 "43fc7b514f057445a0058f80331a1f62",
]

TEXTS = [
 b"DefaultPlatformService",
 b"[OnlineSubsystem",
 b"OnlineSubsystemSteam",
 b"OnlineSubsystemNull",
 b"bEnabled=True",
 b"steam_appid.txt",
]

needles = {}
for g in SAVABLE:
    raw = bytes.fromhex(g)
    needles[b"SAV:"+g.encode()] = [raw, raw[::-1]]
for g in ENEMY:
    raw = bytes.fromhex(g)
    needles[b"ENEMY:"+g.encode()] = [raw, raw[::-1]]
for t in TEXTS:
    needles[b"TXT:"+t] = [t]

hits = {k: [] for k in needles}
CH = 16 * 1024 * 1024
OVER = 64
size = os.path.getsize(PAK)
print(f"scanning {size/1e9:.2f} GB ...")
t0 = time.time()
prev = b""
off = 0
with open(PAK, "rb") as f:
    while True:
        buf = f.read(CH)
        if not buf:
            break
        data = prev + buf
        base = off - len(prev)
        for key, variants in needles.items():
            for v in variants:
                start = 0
                while True:
                    i = data.find(v, start)
                    if i < 0:
                        break
                    hits[key].append(base + i)
                    start = i + 1
                    if len(hits[key]) > 60:
                        break
        prev = data[-OVER:]
        off += len(buf)
print(f"done in {time.time()-t0:.1f}s\n")

print("=== results ===")
for key in needles:
    h = hits[key]
    label = key.decode("ascii", "replace")
    if h:
        print(f"  HIT  {label:<48} x{len(h)}  first offsets: {[hex(x) for x in h[:6]]}")
    else:
        print(f"  none {label}")
