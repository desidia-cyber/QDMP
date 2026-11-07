import struct, os, json, collections

p = r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Content\Paks\QueensDomain-WindowsNoEditor.pak"
FDI = 1974670976

def read_fstring(f):
    ln = struct.unpack("<i", f.read(4))[0]
    if ln == 0:
        return ""
    if ln < 0:
        n = -ln
        s = f.read(n * 2)[:-2].decode("utf-16-le", "replace")
    else:
        s = f.read(ln)[:-1].decode("utf-8", "replace")
    return s

f = open(p, "rb")
f.seek(FDI)
num_dirs = struct.unpack("<i", f.read(4))[0]
print("num dirs:", num_dirs)

paths = []
dirs = []
for d in range(num_dirs):
    dpath = read_fstring(f)
    nf = struct.unpack("<i", f.read(4))[0]
    dirs.append((dpath, nf))
    for i in range(nf):
        fname = read_fstring(f)
        loc = struct.unpack("<i", f.read(4))[0]
        paths.append(dpath + fname)

print("total files:", len(paths))
print("sample:", paths[:5])
print("pos after parse:", f.tell(), "footer start:", os.path.getsize(p) - 204)

with open(r"G:\Freespace\Queens_Domain\analysis\pak-files.txt", "w", encoding="utf-8") as g:
    g.write("\n".join(paths))
print("wrote pak-files.txt")

ext = collections.Counter(os.path.splitext(x)[1].lower() for x in paths)
print("\nextensions:")
for e, c in ext.most_common(30):
    print(f"  {e or '(none)'}: {c}")
