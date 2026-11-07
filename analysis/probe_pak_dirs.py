import re, struct, os, json, collections

p = r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Content\Paks\QueensDomain-WindowsNoEditor.pak"
index_offset = 1973612650
index_size = 529218
size = os.path.getsize(p)

pathhash_off = index_offset + index_size
with open(p, "rb") as f:
    f.seek(pathhash_off)
    n = struct.unpack("<i", f.read(4))[0]
    print("PathHashIndex count:", n)
    pathhash_off_after = pathhash_off + 4 + n * 12
    print("FullDirectoryIndex offset candidate:", pathhash_off_after)

    f.seek(pathhash_off_after)
    dirs_n = struct.unpack("<i", f.read(4))[0]
    print("FullDirectoryIndex dir count:", dirs_n)
    print("bytes remaining to footer:", size - 204 - pathhash_off_after)

    if 0 < dirs_n < 1_000_000:
        entries = []
        bad = 0
        for i in range(dirs_n):
            raw = f.read(4)
            if len(raw) < 4:
                print("EOF at", i); break
            ln = struct.unpack("<i", raw)[0]
            if not (0 < ln < 4096):
                print("bad len at", i, ln); bad += 1; break
            s = f.read(ln)
            name = s[:-1].decode("utf-8", "replace") if ln > 0 else ""
            cnt = struct.unpack("<i", f.read(4))[0]
            if not (0 <= cnt < 1_000_000):
                print("bad count at", i, cnt); break
            f.read(cnt * 4)
            entries.append((name, cnt))
        print("parsed dirs:", len(entries))
        total_files = sum(c for _, c in entries)
        print("total files:", total_files)
        for nm, c in entries[:25]:
            print("   ", nm, "->", c)
        outj = r"G:\Freespace\Queens_Domain\analysis\pak-directories.json"
        with open(outj, "w", encoding="utf-8") as g:
            json.dump([{"dir": a, "count": b} for a, b in entries], g, indent=1)
        print("wrote", outj)
