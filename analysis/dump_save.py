#!/usr/bin/env python3
"""Pretty-print the parsed World0 save, expanding the interesting maps."""
import json, os, sys
import gvas

p = sys.argv[1] if len(sys.argv) > 1 else "World0.sav"
if not os.path.isabs(p):
    p = os.path.join(gvas.SAVES, p)
h = gvas.load(p)
print("class:", h["class_path"])
print("stream:", h["stream_offset"], "->", h["stream_end"], "trailing", h["trailing"])

def short(v, n=260):
    s = json.dumps(v, default=str)
    return s if len(s) <= n else s[:n] + f"... ({len(s)} chars)"

for k, v in h["props"].items():
    if isinstance(v, dict) and ("items" in v or "__count" in v):
        print(f"\n{k}: struct={v.get('__struct')} count={v.get('__count', len(v.get('items', [])))}")
        items = v.get("items") or []
        for it in items[:4]:
            print("   ", short(it, 400))
        if len(items) > 4:
            print(f"    ... {len(items)-4} more")
    elif isinstance(v, dict) and len(v) > 12:
        items = list(v.items())
        print(f"\n{k}: map/struct with {len(items)} entries")
        for kk, vv in items[:12]:
            print(f"    {kk!r}: {short(vv, 120)}")
        if len(items) > 12:
            print(f"    ... {len(items)-12} more")
    elif isinstance(v, list):
        print(f"\n{k}: list[{len(v)}] {short(v)}")
    else:
        print(f"\n{k}: {short(v)}")
