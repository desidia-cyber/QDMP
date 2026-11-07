#!/usr/bin/env python3
"""Reproduce the entire pause-surface audit for Queen's Domain.

Three instruments, each with an explicitly stated detection range:

  A. exe, pair table + .pdata
       Resolves every reflected pause / time-dilation UFunction to its exec thunk and
       to the native implementation the thunk calls. Detection range: *native* call
       sites only. It cannot see blueprint calls, which run through ProcessEvent.

  B. exe, direct-call index (calls.bin)
       Enumerates direct call/jmp callers of each implementation. Detection range:
       direct E8/E9 transfers inside .text. Indirect calls and blueprint dispatch are
       invisible.

  C. pak, package import tables
       An import of a *static* / native UFunction is created only when a blueprint
       actually binds it, so "package X imports SetGamePaused" proves X calls it.
       Detection range: native static calls from blueprint bytecode. It is blind to
       blueprint-to-blueprint calls, which are name-resolved virtual calls and create
       no import.

  D. pak, package name tables
       Catches the blueprint-to-blueprint layer: a virtual call needs the function name
       in the calling package's name table. Weaker than an import (a name can also be a
       declaration or a property), so imports and mentions are reported separately.

  E. pak, bytecode object references
       Cooked blueprint bytecode references a native UFunction as int32 -(import+1).
       Searching the serialized body and mapping the offset to the export that owns it
       attributes the call to a named blueprint function. Validated against
       Photomode_Pawn / WeaponUpgradeMachine before being trusted.

Run:  python analysis/re/pause_surface.py            (all sections)
      python analysis/re/pause_surface.py --exe      (exe only, fast)
Cache: analysis/re/_pak_pause_hits.json, _mech_mentions.json are reused when present
       unless --refresh is given.
"""
import os
import sys
import json
import marshal
import struct
import argparse

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "analysis", "pak", "code"))

import qdlib as q                                    # noqa: E402

VM_DISPATCH = 0x0119D280
PKG_MAGIC = b"\xc1\x83\x2a\x9e"

# ---------------------------------------------------------------------------
# A. reflected names and their exec thunks
# ---------------------------------------------------------------------------
PAUSE_NAMES = [
    "SetGamePaused", "IsGamePaused",
    "SetGlobalTimeDilation", "GetGlobalTimeDilation",
    "SetTickableWhenPaused", "GetTickableWhenPaused",
    "SetActorTickEnabled", "IsActorTickEnabled",
    "SetComponentTickEnabled", "IsComponentTickEnabled",
    "GetActorTimeDilation", "GetIgnoreTimeDilation",
    "GetUnpausedTimeSeconds", "GetRealTimeSeconds", "GetWorldDeltaSeconds",
]

# proven anchors from analysis/re/SUBSTRATE-TARGETS.md, re-verified here
ANCHOR_GETWORLDFROMCTX = 0x02CDEDF0
ANCHOR_GETPLAYERCONTROLLER = 0x028E0340
ANCHOR_GETWORLDDELTASECONDS = 0x028E1CE0
UWORLD_DELTATIMESECONDS = 0x5B0
AWORLDSETTINGS_TIMEDILATION = 0x2E8


def load_pairs():
    with open(os.path.join(HERE, "nativepairs.bin"), "rb") as f:
        return marshal.load(f)["pairs"]


def load_calls():
    with open(os.path.join(HERE, "calls.bin"), "rb") as f:
        return marshal.load(f)


def thunk_target(thunk):
    """The implementation behind a native exec thunk: its last direct call/jmp."""
    f = q.func_at(thunk)
    if f is None:
        return None
    last = None
    for ins in q.disasm(thunk, end_rva=f[1]):
        if ins.mnemonic in ("call", "jmp"):
            t = q.rel_target(ins)
            if t is not None:
                last = t
    return last


def section_exe():
    pairs = load_pairs()
    calls = load_calls()
    print("=" * 78)
    print("A. reflected pause surface -> exec thunk -> native implementation")
    print("=" * 78)
    print("%-26s %-10s %-10s %-9s %-6s %s"
          % ("name", "exec", "impl", "pdata", "size", "direct callers"))
    rows = []
    for name in PAUSE_NAMES:
        ents = pairs.get(name, [])
        usable = []
        for base, thunk, _nrva in ents:
            tgt = thunk_target(thunk)
            if tgt and tgt != VM_DISPATCH:
                usable.append((thunk, tgt))
        if not usable:
            print("%-26s %-10s %-10s %-9s %-6s %s"
                  % (name, "-", "-", "-", "-", "no usable copy"))
            rows.append((name, None, None, 0))
            continue
        thunk, tgt = usable[0]
        f = q.func_at(tgt)
        ncall = len(calls.get(tgt, []))
        print("%-26s %08X   %08X   %-9s %-6s %d"
              % (name, thunk, tgt, "yes" if f else "no",
                 ("%X" % (f[1] - f[0])) if f else "-", ncall))
        rows.append((name, thunk, tgt, ncall))

    print()
    print("NOTE %08X is the generic blueprint-VM dispatcher. The second copy of" % VM_DISPATCH)
    print("     each name table points there and carries no information; only the")
    print("     copy whose thunk ends in a real .pdata function is reported above.")
    print("NOTE SetComponentTickEnabled resolves to 011B18A0, a shared bit-set")
    print("     helper used by thousands of thunks -- that row is not a distinct")
    print("     implementation and is excluded from the conclusions.")
    print()
    return rows


def section_exe_listing(rows):
    print("=" * 78)
    print("B. implementations that decide whether the world clock advances")
    print("=" * 78)
    keep = {"SetGamePaused", "IsGamePaused", "SetGlobalTimeDilation",
            "GetGlobalTimeDilation", "GetWorldDeltaSeconds", "GetUnpausedTimeSeconds",
            "GetRealTimeSeconds", "GetActorTimeDilation", "SetTickableWhenPaused"}
    for name, thunk, impl, ncall in rows:
        if name not in keep or impl is None:
            continue
        f = q.func_at(impl)
        end = f[1] if f else impl + 0x60
        print("\n-- %s  impl %08X  [%08X..%08X)" % (name, impl, impl, end))
        for ins in q.disasm(impl, end_rva=end):
            mark = ""
            if ins.mnemonic in ("call", "jmp"):
                t = q.rel_target(ins)
                if t is not None:
                    mark = "   -> %08X" % t
                    if t == ANCHOR_GETWORLDFROMCTX:
                        mark += "  UEngine::GetWorldFromContextObject"
                    elif t == ANCHOR_GETPLAYERCONTROLLER:
                        mark += "  UGameplayStatics::GetPlayerController"
            print("   %08X  %-9s %-42s%s" % (ins.address, ins.mnemonic, ins.op_str, mark))

    print("\n-- proof that UWorld+0x%X is the clock GameplayStatics reads" % UWORLD_DELTATIMESECONDS)
    for ins in q.disasm(ANCHOR_GETWORLDDELTASECONDS, count=16):
        print("   %08X  %-9s %s" % (ins.address, ins.mnemonic, ins.op_str))

    print("\n-- IsGamePaused tail target (a compound predicate, not a bare flag read)")
    for ins in q.disasm(0x029A43C0, count=48):
        print("   %08X  %-9s %s" % (ins.address, ins.mnemonic, ins.op_str))


# ---------------------------------------------------------------------------
# C/D. pak scans
# ---------------------------------------------------------------------------
STATIC_TARGETS = [
    "SetGamePaused", "IsGamePaused", "SetGlobalTimeDilation", "GetGlobalTimeDilation",
    "SetTimeDilation", "GetActorTimeDilation", "SetActorTickEnabled",
    "SetComponentTickEnabled", "SetTickableWhenPaused", "SetActorTickInterval",
    "SetComponentTickInterval", "SetIgnoreTimeDilation", "GetIgnoreTimeDilation",
    "SetPaused", "Pause", "CanPause", "IsPaused",
]

MECH = [
    "SetGamePaused", "IsGamePaused", "SetGlobalTimeDilation", "GetGlobalTimeDilation",
    "SetTickableWhenPaused", "SetActorTickEnabled", "CustomTimeDilation",
    "bIgnoreTimeDilation", "OpenPauseMenu", "TryAutoPause", "CheckForOverlayPause",
    "ForceNoOverlayPause", "IsInGameplayModal", "EnterCutsceneMode", "ExitCutsceneMode",
    "InCutscene", "InPhotoMode", "EndPhotoMode", "CurrentGameMenu", "ExitMenu", "IsOpen",
    "SetPauseTimeDilation", "IsTimeDilationPaused", "SetPausedEnemyTimeDilation",
    "DisableAllPlayerInput", "EnableAllPlayerInput", "bShowMouseCursor",
    "SetIgnoreLookInput", "SetIgnoreMoveInput", "EnterUIMode", "ExitUIMode",
]


def _header(pk, path):
    off, size, _ = pk.entries[path]
    head = pk.stream.read(off + 53, min(size, 4096))
    if head[:4] != PKG_MAGIC:
        return None
    o = 4
    legacy = struct.unpack("<i", head[o:o + 4])[0]; o += 4
    if legacy != -4:
        o += 4
    o += 8
    if legacy <= -2:
        n = struct.unpack("<i", head[o:o + 4])[0]; o += 4
        o += n * 20
    total = struct.unpack("<i", head[o:o + 4])[0]; o += 4
    if total <= 0 or total > size:
        return None
    return pk.stream.read(off + 53, total)


def pak_scan(refresh=False):
    """Return (import_rows, mech_rows) — cached to json next to this script."""
    imp_cache = os.path.join(HERE, "_pak_pause_hits.json")
    mech_cache = os.path.join(HERE, "_mech_mentions.json")
    if not refresh and os.path.exists(imp_cache) and os.path.exists(mech_cache):
        return json.load(open(imp_cache)), json.load(open(mech_cache))

    import pakread
    import blueprints

    imp_rows, mech_rows = [], []
    with pakread.Pak() as pk:
        paths = [p for p in pk.entries
                 if p.endswith((".uasset", ".umap")) and p.startswith("QueensDomain/")]
        print("scanning %d packages in the pak ..." % len(paths), file=sys.stderr)
        for i, p in enumerate(paths):
            try:
                d = _header(pk, p)
                if not d:
                    continue
                info = blueprints.parse(d)
                imports, exports = blueprints.try_imports_exports(d, info)
            except Exception:
                continue
            names = {imp["name"] for imp in imports if imp["class"] == "Function"}
            hit = [t for t in STATIC_TARGETS if t in names]
            if hit:
                imp_rows.append({"path": p, "hits": hit})
            nm = set(info["names"])
            ment = sorted(m for m in MECH if m in nm)
            if ment:
                mech_rows.append({"path": p,
                                  "mentions": ment,
                                  "imports": [m for m in hit]})
            if i % 4000 == 0:
                print("  %d/%d" % (i, len(paths)), file=sys.stderr)
    json.dump(imp_rows, open(imp_cache, "w"), indent=1)
    json.dump(mech_rows, open(mech_cache, "w"), indent=1)
    return imp_rows, mech_rows


def section_pak(imp_rows, mech_rows):
    print()
    print("=" * 78)
    print("C. every package that *imports* a clock/pause entry point (proven call)")
    print("=" * 78)
    focus = ["SetGamePaused", "SetGlobalTimeDilation", "SetTickableWhenPaused",
             "IsGamePaused", "GetGlobalTimeDilation"]
    for t in focus:
        hits = [r["path"] for r in imp_rows if t in r["hits"]]
        print("\n%-24s %d package(s)" % (t, len(hits)))
        for h in sorted(hits):
            print("    ", h.split("/Content/")[-1])

    print()
    print("=" * 78)
    print("D. package name tables: the blueprint-to-blueprint layer")
    print("=" * 78)
    for t in ["EnterUIMode", "ExitUIMode", "OpenPauseMenu", "TryAutoPause",
              "CheckForOverlayPause", "ForceNoOverlayPause", "IsInGameplayModal",
              "SetPauseTimeDilation", "IsTimeDilationPaused"]:
        hits = sorted(r["path"].split("/Content/")[-1] for r in mech_rows
                      if t in r["mentions"])
        print("%-24s %s" % (t, hits or "-"))


# ---------------------------------------------------------------------------
# E. bytecode attribution inside the player character
# ---------------------------------------------------------------------------
PLAYER = "QueensDomain/Content/Player/FS_PlayerChar.uasset"


def section_bytecode(refresh=False):
    import pakread
    import blueprints

    cache = os.path.join(HERE, "_player_pause_refs.json")
    if not refresh and os.path.exists(cache):
        data = json.load(open(cache))
    else:
        with pakread.Pak() as pk:
            ua = pk.payload(PLAYER)
            uexp = pk.payload(PLAYER[:-len(".uasset")] + ".uexp")
        info = blueprints.parse(ua)
        imports, exports = blueprints.try_imports_exports(ua, info)
        f = info["tail_fields"]
        base = len(ua)
        recs = []
        for i in range(f[2]):
            o = f[3] + i * 104
            ni, _ = struct.unpack("<2i", ua[o + 16:o + 24])
            ss, so = struct.unpack("<2q", ua[o + 28:o + 44])
            recs.append((info["names"][ni], so, ss))
        names = [i["name"] for i in imports]
        idx = {-(j + 1): n for j, n in enumerate(names)}

        def owner(off):
            for n, so, ss in recs:
                if so <= off < so + ss:
                    return n
            return None

        data = {"sites": [], "neighbourhood": {}}
        for target in ("SetGamePaused", "IsGamePaused"):
            j = names.index(target)
            pat = struct.pack("<i", -(j + 1))
            p = uexp.find(pat)
            while p >= 0:
                data["sites"].append({"target": target, "uexp_off": p,
                                      "export": owner(p + base)})
                near = []
                for x in range(max(0, p - 200), min(len(uexp) - 4, p + 200)):
                    v = struct.unpack("<i", uexp[x:x + 4])[0]
                    nm = idx.get(v)
                    if nm and nm != target:
                        near.append([x - p, nm])
                data["neighbourhood"]["%s@%X" % (target, p)] = near
                p = uexp.find(pat, p + 1)
        json.dump(data, open(cache, "w"), indent=1)

    print()
    print("=" * 78)
    print("E. bytecode attribution: which export owns each pause call")
    print("=" * 78)
    for s in data["sites"]:
        print("  %-14s uexp+%08X  owner=%s" % (s["target"], s["uexp_off"], s["export"]))
    for key, near in data["neighbourhood"].items():
        print("\n  neighbourhood of %s" % key)
        for d, nm in sorted(near, key=lambda kv: kv[0]):
            print("     %+6d  %s" % (d, nm))


# ---------------------------------------------------------------------------
# F. the deliverable table
# ---------------------------------------------------------------------------
TABLE = [
    # state, pauses, dilates, evidence, co-op, confidence
    ("Pause menu (Esc)", "Y", "N", "FS_PlayerChar ubergraph SetGamePaused; PauseMenu_C in the unpause path", "Y", "HIGH"),
    ("Options / Settings (from pause hub)", "Y*", "N", "reached from PauseMenu_OptionsButton; world already paused", "Y", "MEDIUM"),
    ("Challenges / Guides (from pause hub)", "Y*", "N", "reached from PauseMenu_Challenges / _Guides buttons", "Y", "MEDIUM"),
    ("Journal / bestiary / notes", "Y*", "N", "Journal_RWM_BP + Journal3DVisualizer call SetTickableWhenPaused", "Y", "MEDIUM-HIGH"),
    ("Photo mode", "Y", "N", "Photomode_Pawn imports SetGamePaused (direct)", "Y", "HIGH"),
    ("Weapon upgrade machine", "Y", "N", "WeaponUpgradeMachine imports SetGamePaused (direct)", "Y", "HIGH"),
    ("Save menu (from checkpoint)", "Y*", "N", "Checkpoint calls SetTickableWhenPaused; OpenSaveMenu owned by player", "Y", "MEDIUM"),
    ("Key-use menu", "Y*", "N", "OpenKeymenu owned by player; UI-mode pair", "Y", "LOW-MEDIUM"),
    ("Shop / vendor", "U", "N", "ShopMenu imports nothing; no SetGamePaused outside the 3 packages", "? if paused", "LOW"),
    ("Inventory / equipment", "U", "N", "no menu widget imports any pause entry point", "?", "LOW"),
    ("Item inspect / merge", "N", "Y", "CustomTimeDilation on the merge anim components; no SetGamePaused", "N", "MEDIUM"),
    ("Item pickup / give animation", "N", "Y", "GetGlobalTimeDilation + CustomTimeDilation; drives its own slow-mo", "N", "MEDIUM"),
    ("Dialogue", "N", "N", "DialogueManager imports no pause entry point", "N", "MEDIUM"),
    ("Cutscene / LevelSequence", "N", "N", "cutscene = level load (map 997/998/999), not a pause", "N/A (map change)", "MEDIUM"),
    ("Death / game over", "U", "N", "DieUI imports no pause entry point", "?", "LOW"),
    ("Read-text / notes overlay", "U", "N", "ReadTextUI imports nothing; IsInReadTextMenu is a player-side flag", "?", "LOW"),
    ("Steam-overlay / focus loss", "Y", "N", "CheckForOverlayPause + TryAutoPause in the player, ForceNoOverlayPause gate", "Y", "MEDIUM"),
    ("Enemy pause / hit-stop", "N", "Y", "SetPausedEnemyTimeDilation via Base_LD45_GM 'Calculate and Apply Full TD'", "N (per-world)", "HIGH"),
    ("Shader compile (loading)", "N", "Y", "ShaderCompileManager imports SetGlobalTimeDilation", "N", "MEDIUM"),
]


def section_table():
    print()
    print("=" * 78)
    print("F. per-state table   (Y = yes, N = no, U = undetermined, * = inherited)")
    print("=" * 78)
    print("%-40s %-3s %-3s %-24s %-6s %s"
          % ("state", "pau", "dil", "co-op clock divergence", "", "conf"))
    for st, pz, dl, ev, coop, conf in TABLE:
        print("%-40s %-3s %-3s %-24s %-6s %s" % (st, pz, dl, coop, "", conf))
    print("\nEvidence per row is expanded in analysis/13b-pause-surface.md.")


def section_entries():
    """Negative result: the owning ubergraph event could NOT be recovered.

    Blueprint events forward an entry offset into ExecuteUbergraph_<Class>. The
    recovery below finds stub-shaped exports carrying EX_IntConst(0x1D) with an
    in-range value. It yields a plausible map for 352 events, but:
      * the map covers only the first half of the ubergraph,
      * the bytes at the claimed entry offsets do not decode as bytecode,
    so the map is reported and NOT used for attribution.
    """
    import pakread
    import blueprints

    with pakread.Pak() as pk:
        ua = pk.payload(PLAYER)
        uexp = pk.payload(PLAYER[:-len(".uasset")] + ".uexp")
    info = blueprints.parse(ua)
    imports, exports = blueprints.try_imports_exports(ua, info)
    f = info["tail_fields"]
    base = len(ua)
    recs = {}
    for i in range(f[2]):
        o = f[3] + i * 104
        ni, _ = struct.unpack("<2i", ua[o + 16:o + 24])
        ss, so = struct.unpack("<2q", ua[o + 28:o + 44])
        recs[info["names"][ni]] = (so - base, ss)

    print()
    print("=" * 78)
    print("G. FS_PlayerChar export geometry (why EnterUIMode is not the pause site)")
    print("=" * 78)
    for n in ("EnterUIMode", "ExitUIMode", "OpenPauseMenu", "OpenSaveMenu",
              "OpenShopmenu", "OpenKeymenu", "TryAutoPause", "EnterPhotoMode"):
        if n in recs:
            off, sz = recs[n]
            print("   %-18s uexp[%08X..%08X)  %d bytes" % (n, off, off + sz, sz))
    print("   pause call sites are at uexp 00033DF3 and 000342FC, i.e. inside")
    print("   ExecuteUbergraph_FS_PlayerChar, not inside any of the above.")

    ug = recs.get("ExecuteUbergraph_FS_PlayerChar")
    if not ug:
        return
    ug_lo, ug_sz = ug
    small = [(n, o, s) for n, (o, s) in recs.items()
             if s <= 100 and not n.startswith("ExecuteUbergraph")]
    vals = []
    for n, off, sz in small:
        b = uexp[off:off + sz]
        for p in range(max(0, len(b) - 5)):
            if b[p] == 0x1D:
                v = struct.unpack("<i", b[p + 1:p + 5])[0]
                if 0 <= v < ug_sz:
                    vals.append((v, n))
                break
    vals.sort()
    print("\n   stub entry offsets recovered: %d of an unknown total" % len(vals))
    print("   covered range: +%08X .. +%08X   ubergraph size +%08X"
          % (vals[0][0], vals[-1][0], ug_sz))
    for t, p in ([("SetGamePaused@33DF3", 0x33DF3), ("SetGamePaused@342FC", 0x342FC)]):
        print("   %-22s rel +%08X  -> %s the recovered map"
              % (t, p - ug_lo,
                 "INSIDE" if vals[0][0] <= p - ug_lo <= vals[-1][0] else "OUTSIDE"))
    print("   => event-level attribution rejected; see 13b-pause-surface.md.")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", action="store_true", help="exe sections only (fast)")
    ap.add_argument("--refresh", action="store_true", help="ignore pak-scan caches")
    a = ap.parse_args()

    rows = section_exe()
    section_exe_listing(rows)
    if not a.exe:
        imp_rows, mech_rows = pak_scan(a.refresh)
        section_pak(imp_rows, mech_rows)
        section_bytecode(a.refresh)
        section_entries()
        section_table()


if __name__ == "__main__":
    main()
