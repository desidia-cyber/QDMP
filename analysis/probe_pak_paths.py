import re, os, json, collections

p = r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Content\Paks\QueensDomain-WindowsNoEditor.pak"
index_offset = 1973612650
index_size = 529218

with open(p, "rb") as f:
    f.seek(index_offset)
    idx = f.read(index_size)

print("index bytes:", len(idx))

# Heuristic: extract NUL-terminated printable ASCII runs that look like asset paths
runs = re.findall(rb"[\x20-\x7E]{4,}", idx)
paths = []
for r in runs:
    s = r.decode("ascii", "replace")
    if s.startswith("/") or s.startswith("../../../") or ("/" in s and "." in s):
        paths.append(s)

print("candidate path runs:", len(paths))
uniq = list(dict.fromkeys(paths))
print("unique:", len(uniq))
for s in uniq[:40]:
    print("  ", s)

# Also count extensions
ext = collections.Counter(os.path.splitext(s)[1].lower() for s in uniq)
print("\nextensions:", ext.most_common(20))

out = r"G:\Freespace\Queens_Domain\analysis\pak-paths-raw.txt"
with open(out, "w", encoding="utf-8") as f:
    for s in uniq:
        f.write(s + "\n")
print("\nwrote", out)
