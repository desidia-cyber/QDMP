# PAK-DECODE — QueensDomain-WindowsNoEditor.pak solved

Target: `G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Content\Paks\QueensDomain-WindowsNoEditor.pak`
1,976,294,370 bytes. UE4 pak **version 11**, `bEncryptedIndex = 0`, `EncryptionKeyGuid = 0…0`,
compression-method table all zero (nothing compressed, nothing encrypted).

Everything below is produced by `code/pakread.py`, which is re-runnable:

```
python pakread.py verify --count 300      # re-runs the validation quoted below
python pakread.py list /Game/Blueprints   # path inventory
python pakread.py info  <pakpath>         # offset / size / inline header / sha1
python pakread.py get   <pakpath> <out>   # extract one payload
```

---

## 1. What was already known, and what it is worth

The footer and the path list were pre-solved. Re-measured here, unchanged:

```
footer magic           E1 12 6F 5A  at abs 1 976 294 166
version                11
index_offset           1 973 612 650
index_size             529 218
index_hash             d7e07fcc b0a097ab a197b98e c23cb277 002ba1bb
bEncryptedIndex        0
EncryptionKeyGuid      00000000-00000000-00000000-00000000
compression methods    4 x 32 zero bytes
```

Two numbers in the brief did not survive contact with the bytes and are corrected here:

| brief said | actual | why |
|---|---|---|
| `NumEntries = 44156` | **44 092** (`3c ac 00 00` at index+14) | 44 156 is not present anywhere in the header |
| PathHashIndex count 44 092 | 44 092 — correct | matches NumEntries exactly, so nothing is missing from the hash index |

The pre-extracted `analysis/pak-files.txt` (47 716 lines) is a *string scan* of the tail region, not
the index. It over-counts by 3 624 because ICU/locale resources and a few doubled runs are picked up.
The authoritative list is the **44 092** paths in `analysis/pak/code/entries.tsv`, which come straight out
of the FullDirectoryIndex.

---

## 2. The solved index layout

The index is **four consecutive regions**. Each one was verified to start and end exactly on the next
one, and the last one ends 17 bytes before the footer.

```
index_offset            1 973 612 650   primary index (index_size = 529 218 bytes)
  +0      FString  mount point            "../../../"
  +14     i32      NumEntries             44 092
  +18     u64      PathHashSeed           0x0000000073C690C8
  +26     u8       bEncodedPakEntries     1
  +27     u8[87]   reserved                zeros, except two 40-byte FPakEntry-shaped
                                           records (Offset/Size/Hash/compression) used by
                                           the pak builder; not needed to read entries
  +114    u8[529104] encoded entry blob    44 092 x 12 bytes
                                         114 + 529 104 = 529 218 = index_size   <- exact

index_offset+index_size 1 974 141 868   PathHashIndex
  i32 count = 44 092, then count x (u64 path_hash, i32 blob_offset)
  = 4 + 44 092*12 = 529 108 bytes -> ends 1 974 670 976
  the blob_offsets are exactly 0, 12, 24, ... 529 092: uniform 12-byte slots,
  44 092 distinct values, histogram of consecutive differences = {12: 44 091}

1 974 670 976           PrunedDirectoryIndex   i32 numDirs=287, 3625 files, ends 1 974 752 831
1 974 752 831           FullDirectoryIndex     i32 numDirs=1401, 44 092 files, ends 1 976 294 149
1 976 294 149..166      17 bytes padding
1 976 294 166           footer
```

Directory index entry:

```
i32 numDirs
repeat numDirs:
    FString dir_path                 e.g. "Engine/Plugins/FX/Niagara/"
    i32 n
    repeat n:
        FString file_name            basename only
        i32     blob_offset          byte offset into the encoded entry blob
```

`blob_offset` is a plain `i32`, **not** the packed `FPakEntryLocation` bitfield (31-bit index +
`bIsEncoded` flag) that stock UE 4.27 writes: every value is a multiple of 12 in `[0, 529092]`,
which is only consistent with it being a byte offset into the encoded blob. Reading it as a
bitfield would still work here because bit 31 is never set, but it is not a *count* — it is an
offset.

There are two directory indexes (287 dirs / 3 625 files, then 1401 dirs / 44 092 files). The
second is the complete one and is the one used. The 287-dir index is a subset that the 44 092-entry
blob also covers; nothing is reachable only through it (checked: its 3 625 names are all present
in the full index).

---

## 3. The encoded entry — 12 bytes, and how it was pinned down

Each encoded entry at `blob[blob_offset]` is 12 bytes:

```
u32 offset    absolute pak offset of the entry's inline header
u32 size      payload size in bytes
u32 flags     0xE0000000 for 44 091 of the 44 092 entries; 0 for exactly one
```

And at `offset` there is a **53-byte inline `FPakEntry` header**, then the payload:

```
+0   i64   reserved             always 0 in all 2 000 entries checked
+8   i64   size                 == the u32 size from the encoded entry
+16  i64   uncompressed_size    == size (nothing is compressed)
+24  u32   compression_method   0
+28  u8[20] sha1 of the payload  (the *stored* bytes, not the uncompressed form)
+48  u32   0
+52  u8    0
--- payload starts here, at offset + 53, length = size
```

### How the 53-byte header was found (and why it is 53 and not 52/48/49)

The naive assumption "payload is at `offset`" is wrong: the bytes at `offset` begin with
`00 00 00 00 00 00 00 00` and then a little-endian i64 equal to the entry size. Sliding the payload
start gives a one-shot answer:

| header size tried | `.uasset`/`.umap` package magic `C1 83 2A 9E` found at `offset+H` |
|---|---|
| 48 | 0 / 19 213 |
| 49 | 0 / 19 213 |
| 52 | 0 / 19 213 |
| **53** | **19 213 / 19 213 = 100.00 %** |

Independent corroboration, without using the magic at all:

* **Gap histogram.** Sort all entries by offset and compute `next_offset - (offset + size)`:
  `{53: 20 804, 2048: 1 710, 781: 797, 817: 310, 745: 180, 85: 128, ...}`.
  The mode is 53 = the header size, so adjacent entries differ by exactly `53 + size`.
  The 2048 and other values are the pak builder's alignment padding.
* **Perfect tiling.** Treating each entry as `[offset, offset+size)` gives **0 overlaps** over all
  44 092 entries, total slack 25 767 596 bytes (0.7 % of the payload area, all of it alignment),
  and the maximum end offset is `offset+size = 1 973 612 597` = `index_offset - 53`, i.e. the last
  payload stops exactly where the last 53-byte header would begin. Nothing is unaccounted for.
* **Two independent ground truths.** `QueensDomain/Config/DefaultEngine.ini` decodes to
  offset `0x078AF800`, size 58 759, and its payload really is INI text — the `[OnlineSubsystemSteam]`
  block the brief quoted at `0x78AFB4E` sits 846 bytes into it. The `.uproject` entry decodes to
  offset 1 973 604 352 / 8 245 bytes and is valid JSON.

### The one exception

Exactly one entry has `flags == 0` instead of `0xE0000000`:

```
QueensDomain/QueensDomain.uproject   offset 1973604352   size 8245   flags 0
```

It is **not** a tombstone: the payload at `offset+53` is the real `.uproject`, 8 245 bytes of valid
JSON, and its inline header size and SHA-1 both check out. Whatever the flag bit means here, the
entry is live.

### What I did NOT solve, honestly

The 12-byte form above is *empirically derived for this pak*, not decoded from UE's bit-packing
algorithm. The stock UE 4.27 reader reads the encoded entry as a variable-width **bit stream**
(`DecodePakEntry`: a compression-method bit group, an encrypted bit, then bit-width-limited
offset/size fields). In this pak every entry is uncompressed and unencrypted, every offset is below
2³² and every size below 2³², so the bit-packer emits the same field widths for all 44 092 entries —
which is exactly why all 44 092 slots are 12 bytes, why the third word is a constant
`0xE0000000` (the width/flags descriptor), and why a plain `u32 offset, u32 size` read is correct.
**The decode is therefore valid for this file and would NOT be safe on a pak with mixed compression
or >4 GiB offsets.** The smallest remaining step to make it general is to implement the bit widths
from `IPlatformFilePak.h::DecodePakEntry`; nothing in this archive needs it.

That is why the validation below matters more than the derivation: it proves the bytes, not the theory.

---

## 4. Validation

### 4.1 SHA-1 of every payload vs the inline header hash — the strongest check

The inline header stores `SHA1(stored bytes)`. Recomputing it proves offset, size and header size all
at once.

```
random sample of 300 entries (seed 1234)   payload sha1 matches header sha1 : 300 / 300 = 100.00 %
   of those, 146 were .uasset/.umap and all 146 also had the package magic : 146 / 146 = 100.00 %
```

An earlier dedicated run on 250 entries with `seed 42` also gave 250/250 sha1 and 250/250
`inline_size == encoded_size`.

### 4.2 Package magic over the whole archive

```
.uasset   19 162 entries, magic C1 83 2A 9E at offset+53 : 19 162 / 19 162 = 100.00 %
.umap         51 entries, magic at offset+53              :      51 /     51 = 100.00 %
```

### 4.3 Structural checks

```
overlapping entries                       0
max(offset+size)                          1 973 612 597  (index_offset - 53)
sum(gap between consecutive entries)      25 767 596 bytes (alignment)
largest single gap                        2 100 bytes
decoded blob length == NumEntries * 12    yes, ends exactly at index_offset + index_size
directory indexes chain end-to-end        yes, last ends 17 bytes before the footer
```

### 4.4 What was rejected

* **`NumEntries = 44156`** — not a value stored anywhere in the header; the actual field is 44 092.
  Rejected because the encoded blob is exactly `44092·12` bytes and filling `44156·12` bytes would
  overrun `index_size` by 768 bytes into the PathHashIndex.
* **`loc` as an `FPakEntryLocation` bitfield** — rejected because 44 092 distinct values all land
  on multiples of 12 in `[0, 529092]`; a 31-bit entry *index* would never look like that.
* **Payload at `offset`** — rejected by the 0/19 213 magic result and by the 53-byte gap mode.
* **A string-scan file list** — rejected in favour of the FullDirectoryIndex (3 624 phantom paths).

### 4.5 The scan fallback was not needed

The brief allowed falling back to scanning for the package magic. It was not used: the primary index
decode is exact and validated at 100 %, so a scan would only add risk. (A scan would also be unable
to recover `.uexp`/`.res`/`.ubulk` boundaries, since those have no magic at all — its limits would
have been real.)

---

## 5. Cost of extraction

Because nothing is compressed, extraction is a single `seek` + `read` per file. Only named assets are
extracted; nothing is copied wholesale. The full 44 092-row table (path, offset, size) is
`code/entries.tsv`, regenerate with the one-liner in §1.
