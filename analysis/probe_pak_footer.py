import struct, sys, os

p = r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Content\Paks\QueensDomain-WindowsNoEditor.pak"
size = os.path.getsize(p)
print("pak size:", size)

with open(p, "rb") as f:
    n = min(4096, size)
    f.seek(size - n)
    tail = f.read(n)

MAGIC = b"\xE1\x12\x6F\x5A"
pos = tail.rfind(MAGIC)
print("footer magic at abs offset:", size - n + pos if pos >= 0 else None)

if pos >= 0:
    foot = tail[pos:]
    off = 4
    version = struct.unpack_from("<i", foot, off)[0]; off += 4
    index_offset = struct.unpack_from("<q", foot, off)[0]; off += 8
    index_size = struct.unpack_from("<q", foot, off)[0]; off += 8
    index_hash = foot[off:off+20]; off += 20
    print("version:", version)
    print("index_offset:", index_offset, "index_size:", index_size)
    print("index_hash:", index_hash.hex())
    print("footer bytes available after hash:", len(foot) - off)

    # 4.27 pak footer after hash: encrypted_index bool(1) + 32-byte key guid, then compression methods
    enc = foot[off]; off += 1
    print("encrypted_index flag:", enc)
    guid = foot[off:off+32]; off += 32
    print("index encryption key guid:", guid.hex())
    if enc == 0:
        with open(p, "rb") as f:
            f.seek(index_offset)
            idx = f.read(min(index_size, 64))
        print("first 64 bytes of index:", idx[:64].hex())
        # FString: int32 len, then ascii
        ln = struct.unpack_from("<i", idx, 0)[0]
        print("mount point length:", ln)
        if 0 < ln < 256 and ln <= len(idx) - 4:
            print("mount point:", idx[4:4+ln-1].decode("utf-8", "replace"))
    # compression method table
    ncomp = struct.unpack_from("<i", foot, off)[0]; off += 4
    print("num compression methods:", ncomp)
    for i in range(min(ncomp, 8)):
        nm = foot[off:off+32].split(b"\x00")[0].decode("ascii", "replace"); off += 32
        print("  method", i, ":", nm)
