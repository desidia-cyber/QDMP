#!/usr/bin/env python3
"""Probe the primary index of the Queens Domain pak without assuming encryption.

Reads the index blob and reports whether it looks like a plaintext FPakFile
index (FString mount point then NumEntries) or like AES ciphertext, then dumps
the head of the decoded structure either way.
"""
import hashlib
import struct
import sys

PAK = r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Content\Paks\QueensDomain-WindowsNoEditor.pak"
MAGIC = b"\xe1\x12\x6f\x5a"
DOLLS_KEY = bytes.fromhex("68E2C3989B7562840EF9B58750D72CC0E4A1543336CFA95931BC1F68C191428A")


def aes_ecb(data, key):
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
    n = len(data) - (len(data) % 16)
    d = Cipher(algorithms.AES(key), modes.ECB()).decryptor()
    return d.update(data[:n]) + d.finalize()


def fstring(buf, pos, limit=65536):
    n = struct.unpack_from("<i", buf, pos)[0]
    pos += 4
    if n == 0:
        return "", pos
    if n > 0:
        if n > limit:
            raise ValueError("implausible len %d" % n)
        return buf[pos:pos + n].rstrip(b"\x00").decode("latin-1"), pos + n
    n = -n
    if n > limit:
        raise ValueError("implausible wide len %d" % n)
    return (buf[pos:pos + n * 2].decode("utf-16-le", "replace").rstrip("\x00"),
            pos + n * 2)


def main(path):
    with open(path, "rb") as fh:
        fh.seek(0, 2)
        size = fh.tell()
        fh.seek(size - 204)
        footer = fh.read(204)
    magic, ver = struct.unpack_from("<4si", footer, 0)
    idx_off, idx_size = struct.unpack_from("<qq", footer, 8)
    idx_hash = footer[24:44]
    methods_raw = footer[44:44 + 160]
    print("magic        : %s (ok=%s)" % (magic.hex(), magic == MAGIC))
    print("version      : %d" % ver)
    print("index offset : %d (0x%X)" % (idx_off, idx_off))
    print("index size   : %d" % idx_size)
    print("index hash   : %s" % idx_hash.hex())
    print("methods table : %r" % methods_raw.split(b"\x00")[:8])

    with open(path, "rb") as fh:
        fh.seek(idx_off)
        blob = fh.read(idx_size)

    print("\n--- raw head ---")
    for i in range(0, 64, 16):
        c = blob[i:i + 16]
        print("%4d  %-47s  %s" % (i, " ".join("%02x" % b for b in c),
                                  "".join(chr(b) if 32 <= b < 127 else "." for b in c)))

    for label, data in (("plaintext", blob), ("dolls-AES", aes_ecb(blob, DOLLS_KEY))):
        print("\n--- as %s ---" % label)
        try:
            mount, p = fstring(data, 0)
            print("  mount point : %r" % mount[:80])
            num = struct.unpack_from("<i", data, p)[0]
            print("  NumEntries  : %d" % num)
            seed = struct.unpack_from("<Q", data, p + 4)[0]
            print("  pathhashseed: 0x%X" % seed)
            ok = mount.startswith("../") and 0 < num < 2_000_000
            print("  plausible   : %s" % ok)
            if ok:
                q = p + 4 + 8
                has_phi = struct.unpack_from("<i", data, q)[0]; q += 4
                print("  bHasPathHashIndex: %d" % has_phi)
                if has_phi:
                    po, ps = struct.unpack_from("<qq", data, q); q += 16
                    ph = data[q:q + 20].hex(); q += 20
                    print("    phi off=%d size=%d hash=%s" % (po, ps, ph))
                has_fdi = struct.unpack_from("<i", data, q)[0]; q += 4
                print("  bHasFullDirectoryIndex: %d" % has_fdi)
                if has_fdi:
                    fo, fsz = struct.unpack_from("<qq", data, q); q += 16
                    fh_ = data[q:q + 20].hex(); q += 20
                    print("    fdi off=%d size=%d hash=%s" % (fo, fsz, fh_))
                enc_size = struct.unpack_from("<i", data, q)[0]; q += 4
                print("  EncodedPakEntriesSize: %d" % enc_size)
                print("  bytes left after encoded: %d" % (len(data) - (q + enc_size)))
                print("  sha1(blob) = %s  (footer says %s)"
                      % (hashlib.sha1(data).hexdigest()[:16], idx_hash.hex()[:16]))
        except Exception as exc:
            print("  parse failed: %s: %s" % (type(exc).__name__, exc))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else PAK)
