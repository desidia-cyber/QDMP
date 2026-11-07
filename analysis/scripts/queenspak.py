#!/usr/bin/env python3
"""queenspak.py -- reader for the Queens Domain UE4.27 .pak (version 11).

Derived from `G:\\Freespace\\dollsMP\\docs\\sync\\evidence\\pak-content\\tools\\pakmine.py`
(itself derived from CUE4Parse `UE4/Pak/Objects/FPakEntry.cs`). Two things had to
change for this game, both measured rather than assumed:

1. FOOTER SIZE. Dolls Nest's footer is exactly 204 bytes (magic, version, 8+8
   index offset/size, 20-byte index SHA1, 5x32 compression-method names). Queens
   Domain's footer is ALSO 204 bytes from EOF -- measured by scanning the tail for
   0x5A6F12E1 -- but its 160-byte method table is entirely zero, i.e. the pak
   declares no named compression methods. This module still accepts 205/221/222
   byte variants (stock 4.27 with bEncryptedIndex / EncryptionKeyGuid /
   bIndexIsFrozen) so the same code runs on a re-packed build.

2. NO INDEX ENCRYPTION. Dolls Nest AES-256-ECB encrypts its index with a key
   lifted from a Steam thread. Queens Domain's index is PLAINTEXT: parsing the
   raw blob yields mount point '../../../' and NumEntries 44092, and the SHA1 of
   the blob equals the footer's IndexHash (d7e07fcc...). So plaintext is tried
   first and AES is only a fallback.

STRUCTURE (all measured on this pak):
    footer  @ EOF-204
    primary index   @ IndexOffset, IndexSize          plaintext
    PathHashIndex   @ follows primary index
    FullDirectoryIndex @ follows PathHashIndex       plaintext
    file data       [0 .. IndexOffset)
"""
from __future__ import annotations

import hashlib
import struct

MAGIC = b"\xe1\x12\x6f\x5a"
AES_BLOCK = 16


class Reader:
    """Little-endian cursor over an in-memory buffer."""

    def __init__(self, buf, pos=0):
        self.b = buf
        self.p = pos

    def i32(self):
        v = struct.unpack_from("<i", self.b, self.p)[0]
        self.p += 4
        return v

    def u32(self):
        v = struct.unpack_from("<I", self.b, self.p)[0]
        self.p += 4
        return v

    def i64(self):
        v = struct.unpack_from("<q", self.b, self.p)[0]
        self.p += 8
        return v

    def u64(self):
        v = struct.unpack_from("<Q", self.b, self.p)[0]
        self.p += 8
        return v

    def raw(self, n):
        v = self.b[self.p:self.p + n]
        self.p += n
        return v

    def fstring(self, limit=65536):
        n = self.i32()
        if n == 0:
            return ""
        if n > 0:
            if n > limit:
                raise ValueError("implausible FString len %d @0x%X" % (n, self.p - 4))
            return self.raw(n).rstrip(b"\x00").decode("latin-1")
        n = -n
        if n > limit:
            raise ValueError("implausible wide FString len %d @0x%X" % (n, self.p - 4))
        return self.raw(n * 2).decode("utf-16-le", "replace").rstrip("\x00")


def aes_ecb_decrypt(data, key):
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
    n = len(data) - (len(data) % AES_BLOCK)
    d = Cipher(algorithms.AES(key), modes.ECB()).decryptor()
    return d.update(data[:n]) + d.finalize()


def method_name(idx, methods):
    """Compression method index -> name.

    The index is 1-BASED: 0 means stored, 1 means methods[0]. Measured on Dolls
    Nest (where getting it wrong produced 67 TB of imaginary data); on Queens
    Domain every method slot is empty, so only 0 is ever expected.
    """
    if idx == 0:
        return "None"
    if 1 <= idx <= len(methods) and methods[idx - 1]:
        return methods[idx - 1]
    return "<method %d>" % idx


def decode_entry(enc, off, methods):
    """Decode one FPakEntry from the EncodedPakEntries blob at byte offset `off`."""
    r = Reader(enc, off)
    bits = r.u32()

    block_units = bits & 0x3F
    block_size = r.u32() if block_units == 0x3F else block_units << 11

    method_idx = (bits >> 23) & 0x3F
    method = method_name(method_idx, methods)

    encrypted = bool(bits & (1 << 22))
    off32 = bool(bits & (1 << 31))
    unc32 = bool(bits & (1 << 30))
    size32 = bool(bits & (1 << 29))

    offset = r.u32() if off32 else r.i64()
    uncompressed = r.u32() if unc32 else r.i64()
    compressed = (r.u32() if size32 else r.i64()) if method != "None" else uncompressed

    blocks_count = (bits >> 6) & 0xFFFF
    # Offset(8) Size(8) UncompressedSize(8) MethodIndex(4) Flags(1)
    # CompressionBlockSize(4) SHA1Hash(20) == 53 for a stored entry.
    struct_size = 8 * 3 + 4 + 1 + 4 + 20
    if method != "None":
        struct_size += 4 + blocks_count * 16

    blocks = []
    if blocks_count == 1 and not encrypted:
        start = offset + struct_size
        blocks.append((start, start + compressed))
    elif blocks_count > 0:
        align = AES_BLOCK if encrypted else 1
        cur = offset + struct_size
        for _ in range(blocks_count):
            ln = r.u32()
            blocks.append((cur, cur + ln))
            cur += ln + ((-ln) % align)

    return dict(
        offset=offset,
        compressed_size=compressed,
        uncompressed_size=uncompressed,
        method=method,
        method_index=method_idx,
        encrypted=encrypted,
        compression_block_size=block_size,
        blocks=blocks,
        struct_size=struct_size,
        data_offset=offset + struct_size,
        encoded_bits=bits,
    )


class Pak:
    def __init__(self, path):
        self.path = path
        self.f = open(path, "rb")
        self.file_size = self._size()
        self._read_footer()
        self._read_primary_index()
        self.entries = {}
        self._read_directory_index()

    def _size(self):
        self.f.seek(0, 2)
        return self.f.tell()

    def _read_footer(self):
        """Locate the FPakInfo footer, then work out which variant it is."""
        window = min(16384, self.file_size)
        self.f.seek(self.file_size - window)
        tail = self.f.read(window)
        base = self.file_size - window

        candidates = []
        i = tail.find(MAGIC)
        while i >= 0:
            if i + 44 <= len(tail):
                version = struct.unpack_from("<i", tail, i + 4)[0]
                idx_off, idx_size = struct.unpack_from("<qq", tail, i + 8)
                if (1 <= version <= 20 and 0 < idx_off < self.file_size
                        and 0 < idx_size <= self.file_size - idx_off):
                    candidates.append(dict(
                        file_offset=base + i,
                        distance_from_eof=self.file_size - (base + i),
                        version=version,
                        index_offset=idx_off,
                        index_size=idx_size,
                        index_hash=tail[i + 24:i + 44].hex(),
                    ))
            i = tail.find(MAGIC, i + 1)

        if not candidates:
            raise RuntimeError("%s: no plausible FPakInfo footer" % self.path)

        info = candidates[-1]
        self.footer_candidates = candidates
        dist = info["distance_from_eof"]
        # 204 = no flag bytes (measured here). 205/222 = stock 4.27 variants with
        # bEncryptedIndex [+16B key GUID] [+1B bIndexIsFrozen].
        methods_at = 44 if dist == 204 else 44 + 1 + (16 if dist >= 222 else 0)
        self.f.seek(info["file_offset"] + methods_at)
        table = self.f.read(160)
        self.methods = [table[32 * k:32 * k + 32].split(b"\x00")[0].decode("latin-1")
                        for k in range(5)]

        # Stock 4.27 carries bEncryptedIndex at footer+44 when the flag bytes exist.
        self.index_encrypted = None
        if dist > 204:
            self.f.seek(info["file_offset"] + 44)
            self.index_encrypted = bool(self.f.read(1)[0])

        info["footer_size"] = dist
        info["methods_offset"] = methods_at
        self.info = info

    def _read_primary_index(self):
        self.f.seek(self.info["index_offset"])
        raw = self.f.read(self.info["index_size"])
        self._used_key = None

        attempts = []
        if self.index_encrypted is not False:
            plain, ok = self._try_parse(raw)
            attempts.append(("plaintext", ok))
            if ok:
                self.index_encryption = "none"
                self.index_plain = plain
        if not hasattr(self, "index_plain"):
            for label, key in ((a, k) for a, k in self._candidate_keys()):
                try:
                    dec = aes_ecb_decrypt(raw, key)
                except Exception as exc:                     # noqa: BLE001
                    attempts.append((label, "aes failed: %s" % exc))
                    continue
                plain, ok = self._try_parse(dec)
                attempts.append((label, ok))
                if ok:
                    self.index_encryption = "aes-256-ecb (%s)" % label
                    self.index_plain = plain
                    self._used_key = key
                    break
        if not hasattr(self, "index_plain"):
            raise RuntimeError("could not decode pak index; attempts=%r" % attempts)

        self.index_sha1_matches = (
            hashlib.sha1(self.index_plain).hexdigest() == self.info["index_hash"])

        r = Reader(self.index_plain)
        # _try_parse already validated the layout; re-walk it for the fields.
        self.mount = r.fstring()
        self.num_entries = r.i32()
        self.path_hash_seed = r.u64()

        self.has_phi = r.i32()
        self.phi = None
        if self.has_phi:
            self.phi = dict(offset=r.i64(), size=r.i64(), sha1=r.raw(20).hex())

        self.has_fdi = r.i32()
        self.fdi = None
        if self.has_fdi:
            self.fdi = dict(offset=r.i64(), size=r.i64(), sha1=r.raw(20).hex())

        enc_size = r.i32()
        self.encoded = self.index_plain[r.p:r.p + enc_size]
        r.p += enc_size
        self.files_num = r.i32()
        self.index_tail_bytes = len(self.index_plain) - r.p
        self.index_attempts = attempts

    @staticmethod
    def _candidate_keys():
        # Only keys this workspace has on hand. Queens Domain needs none (plaintext
        # index); the Dolls Nest key is kept as a cheap fallback for a re-pack that
        # switches index encryption on with the same tooling.
        yield ("dolls-nest-key", bytes.fromhex(
            "68E2C3989B7562840EF9B58750D72CC0E4A1543336CFA95931BC1F68C191428A"))

    @staticmethod
    def _try_parse(blob):
        try:
            r = Reader(blob)
            mount = r.fstring()
            if not mount.startswith("../"):
                return blob, False
            num = r.i32()
            if not 0 < num < 4_000_000:
                return blob, False
            r.u64()
            if r.i32():
                r.i64(); r.i64(); r.raw(20)
            if r.i32():
                r.i64(); r.i64(); r.raw(20)
            enc = r.i32()
            if not 0 <= enc <= len(blob) - r.p:
                return blob, False
            return blob, True
        except Exception:                                    # noqa: BLE001
            return blob, False

    def _read_directory_index(self):
        if not self.has_fdi:
            return
        self.f.seek(self.fdi["offset"])
        plain = self.f.read(self.fdi["size"])
        if self.index_encryption != "none":
            plain = aes_ecb_decrypt(plain, self._used_key or b"")
        self.fdi_sha1_matches = hashlib.sha1(plain).hexdigest() == self.fdi["sha1"]

        d = Reader(plain)
        n_dirs = d.i32()
        self.n_dirs = n_dirs
        skipped_deleted = 0
        non_encoded = 0
        for _ in range(n_dirs):
            directory = d.fstring()
            n_files = d.i32()
            for _ in range(n_files):
                name = d.fstring()
                loc = d.i32()
                if loc == -2147483648:
                    skipped_deleted += 1
                    continue
                if loc < 0:
                    # Reference into the non-encoded FPakEntry array; this pak has
                    # FilesNum == 0, so it should never happen here.
                    non_encoded += 1
                    continue
                path = self.mount + directory + name
                try:
                    e = decode_entry(self.encoded, loc, self.methods)
                except Exception as exc:                     # noqa: BLE001
                    e = dict(error=str(exc))
                e["encoded_offset"] = loc
                self.entries[path] = e
        self.skipped_deleted = skipped_deleted
        self.skipped_non_encoded = non_encoded

    def read_file(self, path):
        e = self.entries[path]
        if "error" in e:
            raise RuntimeError("%s: entry did not decode: %s" % (path, e["error"]))
        method = e["method"] or "None"
        if method == "None":
            n = e["uncompressed_size"]
            read_n = ((n + AES_BLOCK - 1) // AES_BLOCK * AES_BLOCK
                      if e["encrypted"] else n)
            self.f.seek(e["data_offset"])
            data = self.f.read(read_n)
            if e["encrypted"]:
                data = aes_ecb_decrypt(data, self._used_key)
            return data[:n]

        import zlib
        out = bytearray()
        for start, end in e["blocks"]:
            ln = end - start
            read_n = ln + ((-ln) % AES_BLOCK) if e["encrypted"] else ln
            self.f.seek(start)
            chunk = self.f.read(read_n)
            if e["encrypted"]:
                chunk = aes_ecb_decrypt(chunk, self._used_key)[:ln]
            if method.lower() == "zlib":
                out += zlib.decompress(chunk)
            elif method.lower() == "gzip":
                out += zlib.decompress(chunk, 16 + zlib.MAX_WBITS)
            else:
                raise RuntimeError("%s: unsupported method %r" % (path, method))
        return bytes(out[:e["uncompressed_size"]])

    def close(self):
        self.f.close()

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.close()


def game_path(mount, raw):
    """pak-relative name -> UE package path where derivable."""
    full = raw
    if full.startswith("../../../"):
        full = full[len("../../../"):]
    full = full.lstrip("/")
    for prefix, root in (
        ("QueensDomain/Content/", "/Game/"),
        ("QueensDomain/Plugins/", "/Plugin/"),
        ("Engine/Content/", "/Engine/"),
        ("Engine/Plugins/", "/Plugin/"),
    ):
        if full.startswith(prefix):
            return root + full[len(prefix):]
    return full


def package_of(game_path_str):
    for ext in (".uasset", ".umap", ".uexp", ".ubulk", ".uptnl"):
        if game_path_str.endswith(ext):
            return game_path_str[: -len(ext)]
    return game_path_str
