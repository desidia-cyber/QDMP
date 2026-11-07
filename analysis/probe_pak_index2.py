import re, struct, os

p = r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Content\Paks\QueensDomain-WindowsNoEditor.pak"
index_offset = 1973612650
index_size = 529218
size = os.path.getsize(p)

with open(p, "rb") as f:
    f.seek(index_offset)
    idx = f.read(index_size)

print("=== all printable runs >= 6 in index ===")
runs = re.findall(rb"[\x20-\x7E]{6,}", idx)
print("count:", len(runs))
for r in runs[:60]:
    print("   ", r[:120])

print("\n=== last 128 bytes of index ===")
print(idx[-128:].hex(" "))

print("\n=== region after index (gap) first 256 bytes ===")
with open(p, "rb") as f:
    f.seek(index_offset + index_size)
    gap = f.read(256)
print(gap.hex(" "))
print("printable:", re.findall(rb"[\x20-\x7E]{6,}", gap)[:20])

print("\n=== footer 204 bytes ===")
with open(p, "rb") as f:
    f.seek(size - 204)
    ft = f.read(204)
print(ft.hex(" "))
print("footer printable:", re.findall(rb"[\x20-\x7E]{4,}", ft)[:20])
