#!/usr/bin/env python3
"""Decode World0.sav EnemySaveStates: Map<raw FGuid, tagged struct>."""
import collections, json, os, struct, sys
import gvas as G

WORLD = os.path.join(G.SAVES, "World0.sav")
data = open(WORLD, "rb").read()
h = G.parse_header(data)

# find the EnemySaveStates tag
r = G.R(data, h["stream_offset"], len(data))
target = None
while True:
    start = r.o
    t = G.read_tag(r)
    if t is None:
        break
    if t["name"] == "EnemySaveStates":
        target = (t, r.o)
        break
    G.parse_value(r, t)
assert target, "not found"
tag, body = target
print("EnemySaveStates tag:", tag)

r = G.R(data, body, body + tag["size"])
nrem = r.i32()
n = r.i32()
print("numToRemove", nrem, "count", n)
entries = []
for i in range(n):
    k = r.raw(16).hex()
    try:
        v = G.parse_tagged(r, body + tag["size"])
    except Exception as e:
        print("FAIL at entry", i, e)
        break
    entries.append((k, v))
print("decoded", len(entries), "of", n, "consumed", r.o - body, "of", tag["size"])
print("first 3:", json.dumps(entries[:3], indent=1))
print("last:", json.dumps(entries[-1], indent=1) if entries else None)

fields = collections.Counter()
for _, v in entries:
    for kk in v:
        fields[kk] += 1
print("value fields:", dict(fields))

valid = collections.Counter(v.get("Valid") for _, v in entries)
print("Valid distribution:", dict(valid))
hp = [v.get("RemainingHP") for _, v in entries if isinstance(v.get("RemainingHP"), float)]
print("RemainingHP: n=", len(hp), "min", min(hp) if hp else None, "max", max(hp) if hp else None)
hpc = collections.Counter(hp)
print("HP top:", hpc.most_common(8))
rt = collections.Counter(v.get("RespawnTime") for _, v in entries)
print("RespawnTime top:", rt.most_common(8))

json.dump([{"guid": k, **v} for k, v in entries],
          open(os.path.join(os.path.dirname(__file__), "enemy_states.json"), "w"), indent=1)
