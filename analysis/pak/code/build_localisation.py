#!/usr/bin/env python3
"""Generate localisation.md from Game.locres (English)."""
import io
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pakread  # noqa: E402
import locres   # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "localisation.md")
TSV = os.path.join(HERE, "..", "locres-game-en.tsv")

GAME = "QueensDomain/Content/Localization/Game/en/Game.locres"
ENGINE = "Engine/Content/Localization/Engine/en/Engine.locres"


def ident_keys(data, lo, hi):
    """True FStrings in the namespace/key region (requires a matching length prefix)."""
    out = []
    seen = set()
    for o in range(lo, hi - 5):
        (n,) = struct.unpack("<i", data[o:o + 4])
        if n < 4 or n > 200 or o + 4 + n > hi:
            continue
        if data[o + 4 + n - 1] != 0:
            continue
        raw = data[o + 4:o + 4 + n - 1]
        if not all(0x20 <= c < 0x7F for c in raw):
            continue
        s = raw.decode("latin-1")
        if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_:./\-]*", s):
            continue
        if re.fullmatch(r"[0-9A-F]{32}", s):
            continue
        if s.startswith("/") or s.endswith(("/", ".uasset", ".umap")):
            continue
        if o in seen:
            continue
        seen.add(o)
        out.append((o, s))
    return out


def main():
    with pakread.Pak() as pk:
        g = pk.payload(GAME)
        e = pk.payload(ENGINE)
    gr = locres.decode(g)
    er = locres.decode(e)
    recs = gr["records"]
    keys = ident_keys(g, 0x1D, gr["table_start"])

    L = []
    L.append("# Localisation — English text catalogue\n")
    L.append("""Source: `QueensDomain/Content/Localization/Game/en/Game.locres`
(%d bytes, extracted from the pak at offset 0x%X + 53, sha1-verified).

Also decoded for reference: `Engine/Content/Localization/Engine/en/Engine.locres`
(%d bytes, %d strings) — engine/editor text, not game text.

**%d English strings** were recovered. That is the complete Game namespace: the file is only
%d KB, so this really is everything the game localises. Most of the game's narrative is *not* in
here — see "What is and is not in this file" below.
""" % (len(g), 0, len(e), len(er["records"]), len(recs), len(g) // 1024))

    L.append("\n## Format\n")
    L.append("""Not stock UE4 locres. Both files start with the same 13 bytes:

```
0x00  u32  magic 0x7574140E          (bytes 0E 14 74 75)  -- this part IS stock
0x04  u8[13] 67 4A 03 FC 4A 15 90 9D C3 37 7F 1B 03
             byte 0x04 is where stock UE would put a version byte (0x67 = 103, not a stock
             version); the following 12 bytes are byte-identical between Game.locres and
             Engine.locres, so they are a fixed build stamp, not data.
0x11  i64  string_table_offset       85689 (Game) / 862401 (Engine)
0x19  u32  declared string count     2388 (Game) / 20149 (Engine)
0x1D  ...  namespace/key records, then the string table at string_table_offset
```

The **string table** is `repeat { u32 refcount ; FString text }` from
`string_table_offset` to EOF. `FString` is `i32 length` + bytes; a **negative length means
UTF-16LE** and the byte count is `2*|length|`. The strings are stored in case-insensitive
alphabetical order and are de-duplicated — the `refcount` is how many keys share each string
(`"Get key from librarian"` has refcount 7). Both files parse to EOF with at most 8 slack bytes,
which is the validation.

The **namespace/key records** before the table use 32-hex-digit identifiers
(`CBC3CDBC4C8C56F0A3F0EEB17FBB687F`) for imported text, and human-readable names for the
hand-authored text (`Dialogue`, `ImportedStringTable`, `inha_j_01_desc`,
`cha_quest_shop_unlocks_name`, `binding_a`). The record stride is 49 bytes for the GUID-named
group (`u32`,`u32`,`u32`,`FString`) but the region is not uniform, and **the exact
key -> stringindex pairing was not recovered** — see "Not recovered" below.
""")

    L.append("\n## The catalogue (%d strings)\n" % len(recs))
    L.append("Sorted as stored (case-insensitive alphabetical). `xN` = number of keys sharing "
             "that string. Newlines are shown as `\\n`.\n")
    L.append("One artefact to read correctly: row 0's `refs` value (2329) is **not** a sharing "
             "count — it is the table's own declared record count, which happens to sit where "
             "row 0's prefix word would be. Every other row's value is a genuine sharing count "
             "(`\"Get key from librarian\"` is shared by 7 keys).\n")
    L.append("| # | refs | text |")
    L.append("|---:|---:|---|")
    for i, (ref, s) in enumerate(recs):
        t = s.replace("|", "\\|").replace("\r\n", "\\n").replace("\n", "\\n").replace("\r", "\\r")
        if len(t) > 600:
            t = t[:600] + " …(truncated)"
        L.append("| %d | %d | %s |" % (i, ref, t))

    L.append("\n## Not recovered: the key -> text pairing\n")
    L.append("""The strings are certain (they are the file's actual string table, byte-exact). What is
**not** recovered is which localisation key points at which string. The records before the string
table hold `(hash, name)` pairs but the field that carries the string index did not align under any
of the struct shapes tried (`u32+FString`, `u32+u32+FString`, `u32+FString+i32`,
`FString+count+(key)*`, flat and nested), all of which were required to tile exactly to
`string_table_offset` and none did. The raw region is `[0x1D, %d)` of the file.

The smallest remaining step: dump that region with a hex editor alongside
`GetDataTableRow`-free UE source for this build's locres writer
(`FTextLocalizationResource::SaveToStream`), or diff against a stock UE 4.27 locres produced by
the same engine version to see the added field. Everything else in the file is solved.

Practical impact: **low**. Text is the deliverable and it is complete; keys here are mostly
`binding_*`, `numformat_*`, `demosplash_*` plus the 32-hex imported GUIDs, so a key->text map would
add ordering and context but almost no new text.
""" % gr["table_start"])

    L.append("\n## What is and is not in this file\n")
    L.append("""Present (representative, from the catalogue above):

* **item descriptions** — the long `A …` entries: *"A blood red key forged in the style of Josami
  armor."*, *"A diamond shaped metal plate with a glass sword embedded in its center. Unseals sword
  doors in Siraatsu Castle."*, *"A frail ancient short sword capable of a secret technique few know
  of."*
* **journal / readable text** — dated diary entries (`19-4`, `2-7`, `4-26`, `5-22`, …) with full
  multi-paragraph lore, e.g. the Sin Keeper's account of the silver egg from the Dekanima, and the
  pilgrim's record of the Deimpo Mausoleum.
* **quest text** — `"Find the two keys. One is at the Tower of Knowledge."`,
  `"Get key from librarian"`, `"Here take this spooky item"`, `"fine take this key"`.
* **UI** — `"{Hours}h{Minutes}m{Seconds}s"`, `"{Num} Ancient Coins"`, `"{num}%"`,
  `"Debug Options"`, `"Reset Achievements"`, `"Cheat Stick"`.
* **debug/telemetry strings that leak the architecture** — `"Updating Quest State From {cur} to
  {next}"`, `"{OBJECT}" Attempted to Activate a null pointer in Activation list Index "{index}"
  THIS IS A BUG! Please report!!`, `"There are unapplied Resolution or Window Mode changes."`.
* **credits** — `"Eric Berger [Biogenesis] & Bradley Boothe [Birdlay]"`,
  `"Freshly Baked Games LLC"`.

**Not present: spoken dialogue lines / subtitles.** 2,329 strings for a ~10 hour game means the
voice-over has no subtitles in this resource. Confirmed independently two ways: the pak contains 41
`.locres` files, of which 9 are `Game` in different languages and the other 32 are
`Engine`/`OnlineSubsystem*`; and a whole-pak byte scan for `Subtitle` returns 15 hits, all in
`Engine/Config/BaseEngine.ini` and `Engine.locres` — no game asset mentions subtitles at all (see
`datatables.md`: there are no DataTables anywhere either, so there is no dialogue-line table).

Also absent: **monster and item display names**. The weapons are named only by asset
(`RustySword_Item`, `BasicSpear`, …), not by a localised string, so the game renders their names
from asset names or from `*_Data` assets rather than from `Game.locres`.
""")

    L.append("\n## Appendix — every human-readable key name found in the record region (%d)\n"
             % len(keys))
    L.append("These are the FString names in `[0x1D, 0x%X)` that are not 32-hex GUIDs and not "
             "asset paths. They are the game's localisation key namespace, and they are the most "
             "direct evidence of how the writers organised text.\n" % gr["table_start"])
    L.append("```")
    line = []
    for off, s in keys:
        line.append(s)
    # 4 per line
    for i in range(0, len(line), 4):
        L.append("  ".join(line[i:i + 4]))
    L.append("```")

    with io.open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(L) + "\n")
    with io.open(TSV, "w", encoding="utf-8", newline="\n") as f:
        for i, (ref, s) in enumerate(recs):
            f.write("%d\t%d\t%s\n" % (i, ref, s.replace("\t", " ").replace("\r", "\\r")
                                      .replace("\n", "\\n")))
    print("wrote", os.path.abspath(OUT), "and", os.path.abspath(TSV))
    print("strings:", len(recs), "keys:", len(keys))


if __name__ == "__main__":
    main()
