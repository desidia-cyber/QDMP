#!/usr/bin/env python3
"""D9 - where boss arena / defeat state lives.

Run:  python boss_state.py
Needs: ../pakread.py, ../blueprints.py, ../../../gvas.py, the pak, the save files.

Method
------
The question is a *negative* first ("is there a BossStates container?") and a
*where is it then* second.  A negative has to name its instrument, so this runs
a real byte scan over the whole 1.97 GB pak (uncompressed payloads, one
sequential pass, hits attributed back to their owning entry by offset), and
states the detection range: exact byte substring, case sensitive, every payload
byte plus every name table.

Then it looks for the positive: the boss actors placed in the 51 levels, the
boss challenge records, and the NamedSaveData keys the boss blueprints write.
"""
import bisect
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(HERE, "..", "..", ".."))
import pakread      # noqa: E402
import blueprints   # noqa: E402

SAVE_ROOT = os.path.abspath(os.path.join(HERE, "..", "..", "..", "save-backup-before-live"))
LIVE = os.path.abspath(os.path.join(HERE, "..", "..", "..", "..", "_live", "saved",
                                    "Saved", "SaveGames"))

NEGATIVES = ["BossStates", "bossKilled", "BossDefeated", "BossState", "BossManager"]
POSITIVES = ["DemonBossesKilled", "BridgeLordDead", "DragonLordDead", "RedLordsDead",
             "ROSDragonDead", "GiantSwampFish_IsDead", "NecronimiconDefeated",
             "EndingSeen", "NecroWall00Destroyed", "NecroWall04Destroyed"]

CHUNK = 8 << 20


def scan(pk, needles):
    """Sequential byte scan over every payload; map each hit to its entry."""
    spans = sorted((off + pakread.ENTRY_HEADER_SIZE,
                    off + pakread.ENTRY_HEADER_SIZE + size, p)
                   for p, (off, size, _f) in pk.entries.items())
    idx = [s for s, _e, _p in spans]
    limit = pk.index_offset
    pats = [(n, n.encode("latin-1")) for n in needles]
    hits = {n: [] for n in needles}
    pos, prev = 0, b""
    overlap = max(len(b) for _, b in pats) - 1
    while pos < limit:
        pk.f.seek(pos)
        buf = pk.f.read(min(CHUNK, limit - pos))
        if not buf:
            break
        data = prev + buf
        base = pos - len(prev)
        for name, pat in pats:
            p0 = 0
            while True:
                k = data.find(pat, p0)
                if k < 0:
                    break
                abs_off = base + k
                i = bisect.bisect_right(idx, abs_off) - 1
                owner = "<gap>"
                if 0 <= i < len(spans):
                    s, e, pp = spans[i]
                    if s <= abs_off < e:
                        owner = pp
                ctx = data[max(0, k - 30):k + len(pat) + 30]
                hits[name].append((abs_off, owner,
                                   "".join(chr(c) if 32 <= c < 127 else "." for c in ctx)))
                p0 = k + 1
        prev = data[-overlap:] if overlap else b""
        pos = base + len(data)
    return hits, (limit)


def boss_actors(pk):
    """Placed actors whose class mentions 'Boss', across all 51 maps."""
    maps = sorted(p for p in pk.entries if p.lower().endswith(".umap"))
    found = {}
    for m in maps:
        d = pk.payload(m)
        info = blueprints.parse(d)
        _imp, exp = blueprints.try_imports_exports(d, info)
        for e in exp:
            cls = e["class"] or ""
            nm = e["name"]
            if nm.startswith("Default__"):
                continue
            if "Boss" in cls or cls.startswith("DemonBoss"):
                found.setdefault(cls, []).append((m.rsplit("/", 1)[-1], nm))
    return found


def save_keys():
    import gvas
    out = {}
    for base in (SAVE_ROOT, LIVE):
        if not os.path.isdir(base):
            continue
        for fn in sorted(os.listdir(base)):
            if not fn.endswith(".sav"):
                continue
            try:
                h = gvas.load(os.path.join(base, fn))
            except Exception:
                continue
            nsd = h["props"].get("NamedSaveData", {})
            out[os.path.join(os.path.basename(base), fn)] = sorted(nsd)
    return out


def main():
    with pakread.Pak() as pk:
        print("=== 1. negative search: is there a boss-state container? ===")
        print("  instrument: byte scan, all payload bytes of the %.2f GB pak"
              % (pk.size / 1e9))
        print("  range: exact ASCII substring match, case sensitive, payloads + name tables")
        hits, limit = scan(pk, NEGATIVES + POSITIVES)
        print("  bytes scanned: %d (index_offset)" % limit)
        print("  needles that MUST exist if a boss container existed:")
        for n in NEGATIVES:
            print("    %-22s %d hit(s)" % (n, len(hits[n])))

        print("\n=== 2. positive search: the boss defeat keys ===")
        for n in POSITIVES:
            hs = hits[n]
            owners = {}
            for off, owner, ctx in hs:
                owners.setdefault(owner, 0)
                owners[owner] += 1
            print("  %-26s %3d hit(s) in %d file(s)" % (n, len(hs), len(owners)))
            for owner in sorted(owners):
                print("      %-70s x%d" % (owner.split("Content/", 1)[-1], owners[owner]))

        print("\n=== 3. boss actors placed in the 51 maps ===")
        actors = boss_actors(pk)
        total = 0
        for cls in sorted(actors, key=lambda c: -len(actors[c])):
            total += len(actors[cls])
            maps = sorted({m for m, _n in actors[cls]})
            print("  %-34s %3d  in %d map(s)" % (cls, len(actors[cls]), len(maps)))
            for m, n in sorted(actors[cls])[:6]:
                print("      %-30s %s" % (m, n))
        print("  total placed boss-class actors: %d" % total)

        print("\n=== 3b. what the arena gates derive from (persistence path) ===")
        gates = ["Enemies/JosamiKnight_00/DesBoss/DesBoss_Gate",
                 "Enemies/Necronomicon/Necro_Boss_Door",
                 "RuinsLevel/AquaRuins/BossDoor/AquaBossDoor_BP",
                 "RuinsLevel/Library/BossDoor/Lib_BossDoor_BP",
                 "RuinsLevel/Web/BossWebWall/WebBossWall_BP",
                 "RuinsLevel/AquaRuins/GraveyardBoss_Gate",
                 "RuinsLevel/Library/BossDoor/Lib_TreasureGate_BP"]
        for g in gates:
            p = "QueensDomain/Content/%s.uasset" % g
            if p not in pk.entries:
                continue
            d = pk.payload(p)
            info = blueprints.parse(d)
            _imp, ex = blueprints.try_imports_exports(d, info)
            base = p.rsplit("/", 1)[-1][:-7]
            top = [e for e in ex if e["name"] in (base, base + "_C")]
            print("  %-24s parent=%s" % (base, top[0]["super"] if top else "?"))

        print("\n=== 3c. the encounter controller ===")
        p = "QueensDomain/Content/Enemies/DemonBosses/DemonBoss_FightController.uasset"
        d = pk.payload(p)
        info = blueprints.parse(d)
        imp, ex = blueprints.try_imports_exports(d, info)
        base = p.rsplit("/", 1)[-1][:-7]
        top = [e for e in ex if e["name"] in (base, base + "_C")]
        print("  %s  class=%s parent=%s" % (base, top[0]["class"], top[0]["super"]))
        print("  imports:", sorted({x["name"] for x in imp
                                    if x["class"] in ("BlueprintGeneratedClass", "Class")}))
        props, _fn, _o = blueprints.classify(info["names"])
        keep = [x for x in props if any(k in x for k in
                ("Puddle", "Spline", "Finished", "SourceGUID", "Damage", "Track",
                 "Spawn", "Managed"))]
        print("  encounter fields:", keep)

        print("\n=== 4. boss-related assets on disk (path filter) ===")
        groups = {
            "fight controller": [p for p in pk.entries if "FightController" in p and p.endswith(".uasset")],
            "arena gate/door": [p for p in pk.entries if p.endswith(".uasset")
                                and ("BossDoor" in p or "BossDoor_BP" in p
                                     or "Boss_Gate" in p or "Necro_Boss_Door" in p
                                     or "WebBossWall_BP" in p or "GraveyardBoss_Gate" in p
                                     or "DesBoss_Gate" in p)],
            "boss spawner": [p for p in pk.entries if p.endswith(".uasset")
                             and "Boss_Spawner" in p or p.endswith("_Spawner.uasset") and "Boss" in p],
            "boss HUD": [p for p in pk.entries if p.endswith(".uasset") and "BossHPBar" in p],
            "arena post-process": [p for p in pk.entries if p.endswith(".uasset") and "BossArena" in p],
            "boss music": [p for p in pk.entries if p.startswith("QueensDomain/Content/Music/3-Boss/")
                           and p.endswith(".uasset")],
        }
        for g, ps in groups.items():
            print("  %-20s %3d" % (g, len(ps)))
            for p in sorted(set(ps))[:6]:
                print("      ", p.split("Content/", 1)[-1])

        print("\n=== 5. the challenge namespace ===")
        d = pk.payload("QueensDomain/Content/Blueprints/Challenges/FS_ChallengeData.uasset")
        info = blueprints.parse(d)
        cha = sorted({n for n in info["names"] if n.startswith("cha_Bosses")})
        icons = sorted({p.rsplit("/", 1)[-1][:-7] for p in pk.entries
                        if "/Blueprints/Challenges/" in p and p.endswith(".uasset")
                        and p.rsplit("/", 1)[-1].startswith("boss_")})
        print("  challenge ids in FS_ChallengeData : %d  %s" % (len(cha), cha))
        print("  boss_* assets are TEXTURES, not keys: %d  %s" % (len(icons), icons))
        for i in icons:
            p = "QueensDomain/Content/Blueprints/Challenges/%s.uasset" % i
            dd = pk.payload(p)
            ii = blueprints.parse(dd)
            _imp, ex = blueprints.try_imports_exports(dd, ii)
            base = i
            top = [e for e in ex if e["name"] in (base, base + "_C")]
            print("    %-18s top-export class=%s" % (i, top[0]["class"] if top else "?"))

        print("\n=== 6. does any real save carry boss state? ===")
        try:
            keys = save_keys()
        except Exception as e:
            keys = {}
            print("  (save reader unavailable: %s)" % e)
        for f, ks in keys.items():
            bossy = [k for k in ks if "Boss" in k or "Dead" in k or "Necro" in k or "Ending" in k]
            print("  %-40s NamedSaveData keys=%d  boss-ish=%s"
                  % (f, len(ks), bossy if bossy else "none"))

    return 0


if __name__ == "__main__":
    sys.exit(main())
