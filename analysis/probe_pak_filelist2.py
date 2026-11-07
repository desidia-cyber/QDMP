import struct, os, re, collections

p = r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Content\Paks\QueensDomain-WindowsNoEditor.pak"
FDI = 1974670976
END = os.path.getsize(p) - 204

data = open(p, "rb").read()
region = data[FDI:END]
print("region size:", len(region))

# Validate-by-length-prefix string scan
paths = []
pos = 0
cur_dir = ""
n = len(region)
i = 0
found = 0
while i + 4 <= n:
    ln = struct.unpack_from("<i", region, i)[0]
    if 1 <= ln <= 600 and i + 4 + ln <= n:
        raw = region[i+4:i+4+ln]
        if raw[-1] == 0:
            body = raw[:-1]
            if body and all(0x20 <= b < 0x7f for b in body):
                s = body.decode("ascii")
                i += 4 + ln
                found += 1
                if s.endswith("/"):
                    cur_dir = s
                else:
                    paths.append(cur_dir + s)
                continue
    i += 1

print("strings found:", found)
print("file paths:", len(paths))
print("sample:")
for s in paths[:10]:
    print("   ", s)
print("...")
for s in paths[-10:]:
    print("   ", s)

ext = collections.Counter(os.path.splitext(x)[1].lower() for x in paths)
print("\nextensions:")
for e, c in ext.most_common(30):
    print(f"  {e or '(none)'}: {c}")

with open(r"G:\Freespace\Queens_Domain\analysis\pak-files.txt", "w", encoding="utf-8") as g:
    g.write("\n".join(paths))
print("wrote pak-files.txt", len(paths))
