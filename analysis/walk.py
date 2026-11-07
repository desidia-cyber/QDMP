#!/usr/bin/env python3
"""Walk the top-level property stream of a save, printing each tag and the
offset where its body starts. Stops at the first parse failure so the broken
property is obvious."""
import os, sys
import gvas

p = sys.argv[1]
if not os.path.isabs(p):
    p = os.path.join(gvas.SAVES, p)
data = open(p, "rb").read()
h = gvas.parse_header(data)
print("class:", h["class_path"], "stream_offset:", h["stream_offset"],
      "file_size:", len(data))
r = gvas.R(data, h["stream_offset"], len(data))
i = 0
while r.o < len(data):
    start = r.o
    try:
        t = gvas.read_tag(r)
    except Exception as e:
        print(f"[{i}] TAG FAIL at {start} ({start:#x}): {e}")
        break
    if t is None:
        print(f"[{i}] None tag at {start}; stream ends at {r.o}")
        break
    body = r.o
    desc = t["type"]
    for k in ("struct", "inner", "value_type", "enum"):
        if k in t:
            desc += f"<{t[k]}>"
    print(f"[{i:2d}] {start:6d} ({start:#06x})  {t['name']:<40} {desc:<40} size={t['size']}")
    try:
        v = gvas.parse_value(r, t)
        got = r.o - body
        flag = "" if got == t["size"] else f"  !! consumed {got} != size {t['size']}"
        print(f"      -> ok, consumed {got}{flag}")
    except Exception as e:
        print(f"      -> VALUE FAIL: {e}")
        break
    i += 1
print("final offset", r.o, "of", len(data), "trailing", len(data) - r.o)
