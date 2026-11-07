#!/usr/bin/env python3
"""QueensDomain pak footer/index probe + full file listing (UE4 pak)."""
import struct
import sys
import json
import zlib

PAK = r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Content\Paks\QueensDomain-WindowsNoEditor.pak"
MAGIC = b"\xe1\x12\x6f\x5a"

f = open(PAK, "rb")
f.seek(0, 2)
size = f.tell()
print("pak size: %d (%.2f GB)" % (size, size / 2**30))

# find magic; footer is 44 bytes without index hash? UE4 vary. Search last 512 bytes.
f.seek(size - 512)
tail = f.read()
i = tail.rfind(MAGIC)
print("magic at abs offset 0x%X (distance from EOF %d)" % (
    size - 512 + i, size - (size - 512 + i)))
ver, = struct.unpack_from("<I", tail, i + 4)
print("version:", ver)
idx_off, idx_size = struct.unpack_from("<qq", tail, i + 8)
print("index offset: 0x%X  size: %d (0x%X)" % (idx_off, idx_size, idx_size))
h = tail[i + 24:i + 44]
print("index hash:", h.hex())
cm = tail[i + 44:i + 44 + 160]
print("compression methods:", [cm[k * 32:(k + 1) * 32].split(b"\0")[0]
                              .decode("latin1") for k in range(5)])
rest = tail[i + 44 + 160:]
print("trailing after methods (%d bytes): %s" % (len(rest), rest[:64].hex()))
# encryption guid flags may follow
if len(rest) >= 17:
    print("  possible bEncryptedIndex:", rest[16] if len(rest) > 16 else "?")

f.seek(idx_off)
idx = f.read(idx_size)
print("\nfirst 64 bytes of index:", idx[:64].hex())
print("entropy-ish preview:", idx[:32])

# Try to read MountPoint as FString (int32 len incl NUL)
ln, = struct.unpack_from("<i", idx, 0)
print("mount point len field:", ln)
if 0 < ln < 512:
    mp = idx[4:4 + ln].decode("latin1", "replace")
    print("mount point:", repr(mp))
    n, = struct.unpack_from("<i", idx, 4 + ln)
    print("num entries:", n)
    seed, = struct.unpack_from("<Q", idx, 4 + ln + 4)
    print("path hash seed: 0x%X" % seed)
