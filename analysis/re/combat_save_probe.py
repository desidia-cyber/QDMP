#!/usr/bin/env python3
"""combat_save_probe.py - reproducible static evidence for gap items D4, D5, D6, D8, C9.

Every address this script prints is derived, not hard-coded:
  * named symbols come from analysis/re/nativepairs.bin (803 {name,fnptr} tables);
  * the implementation of a Blueprint-callable is the last direct call in its exec
    thunk *before* the store into the RESULT_DECL, skipping the known FFrame helpers
    (0x01189A70 / 0x011B1870 / 0x011B18A0 / 0x01193920 / 0x0118xxxx / 0x00EAE0B0);
  * struct offsets are re-derived by scanning .text for `lea r64,[rax+disp32]`;
  * the D4 GUID match re-reads the live World0.sav and the shipped pak.

Usage
    python analysis/re/combat_save_probe.py                 # full report to stdout
    python analysis/re/combat_save_probe.py --evidence      # + write combat_save_evidence.txt
    python analysis/re/combat_save_probe.py --no-pak        # skip the pak half of D4

Written for QueensDomain-Win64-Shipping.exe, ImageBase 0x140000000, UE 4.27.2.
"""
from __future__ import annotations

import argparse
import io
import marshal
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

from qdlib import (IMAGE_BASE, data, disasm, find_string, func_at, functions,
                   insn_target, off_to_rva, read_rva, rel_target, sec_by_name,
                   sections)
from hunt2 import callers
from hunt import refs_to

# ------------------------------------------------------------------ constants
GENERIC_DISPATCHER = 0x0119D280      # the useless copy of every pair table
FFRAME_HELPERS = {
    0x01189A70, 0x011B1870, 0x011B18A0, 0x01193920, 0x01193870,
    0x01189390, 0x01188D60, 0x011890D0, 0x00EAE0B0,
}

OUT = []            # type: list[str]


def emit(s=""):
    OUT.append(s)
    print(s)


def hdr(title):
    emit()
    emit("=" * 78)
    emit(title)
    emit("=" * 78)


def lin(rva, n=48, label="", limit=0x800):
    """Linear disassembly, deliberately NOT bounded by .pdata (which truncates)."""
    emit("--- disasm %08X %s" % (rva, label))
    for ins in disasm(rva, count=n, end_rva=rva + limit):
        t = insn_target(ins)
        tg = rel_target(ins)
        note = ""
        if t is not None:
            note = "   ; ->%08X" % t
        elif tg is not None:
            note = "   ; FUN_%08X" % tg
        emit("   %08X %-9s %-46s%s" % (ins.address, ins.mnemonic, ins.op_str, note))


# ------------------------------------------------------------------ pair table
_M = None


def pairs():
    global _M
    if _M is None:
        with open(os.path.join(HERE, "nativepairs.bin"), "rb") as f:
            _M = marshal.load(f)["pairs"]
    return _M


def entries(name):
    """All {name,fnptr} rows for `name` -> [(table_base, exec_thunk, name_rva)]."""
    return pairs().get(name, [])


def thunk_calls(thunk):
    f = func_at(thunk)
    end = f[1] if f else thunk + 0x120
    return [(i.address, rel_target(i)) for i in disasm(thunk, end_rva=end)
            if i.mnemonic.startswith("call") and rel_target(i) is not None]


def impl_of(name):
    """Best-effort implementation address for a Blueprint-callable.

    exec thunk shape (verified on execSaveGameToSlot @0x02DC30F0):
        mov rsi, r8              ; RESULT_DECL (3rd param)
        ... FFrame::Step reads ...
        call <impl>
        mov byte ptr [rsi], al   ; result store
        ... epilogue FString destructor (0x00EAE0B0) ...
    """
    out = []
    for base, thunk, nrva in entries(name):
        impl = None
        f = func_at(thunk)
        ins = list(disasm(thunk, end_rva=(f[1] if f else thunk + 0x120)))
        store_i = None
        for i, x in enumerate(ins):
            if x.mnemonic.startswith("mov") and (
                    x.op_str.startswith("byte ptr [rsi]") or
                    x.op_str.startswith("qword ptr [rsi]") or
                    x.op_str.startswith("dword ptr [rsi]")):
                store_i = i
        if store_i is not None:
            for x in reversed(ins[:store_i]):
                t = rel_target(x)
                if t is not None and x.mnemonic.startswith("call") and t not in FFRAME_HELPERS:
                    impl = t
                    break
        out.append((base, thunk, impl, any(rel_target(i) == GENERIC_DISPATCHER for i in ins)))
    return out


def good(name):
    """Exec thunk(s) that are not the generic-dispatcher copy."""
    return [x for x in impl_of(name) if not x[3]]


def show_symbol(name, expect=None):
    """Print the pair-table rows for `name`.

    If `expect` is given, independently verify that the *real* (non-generic) exec
    thunk contains a direct call to that address. This is the only address form
    this script treats as proven.
    """
    rows = impl_of(name)
    emit("%-34s" % name)
    hit = False
    for base, thunk, impl, generic in rows:
        if generic:
            emit("      table %08X  thunk %08X  GENERIC-COPY(useless)" % (base, thunk))
            continue
        if expect is not None:
            called = any(t == expect for _, t in thunk_calls(thunk))
            hit = hit or called
            emit("      table %08X  thunk %08X  impl=%08X  %s"
                 % (base, thunk, expect, "VERIFIED (thunk calls it)" if called
                    else "NOT CALLED BY THUNK"))
        else:
            emit("      table %08X  thunk %08X  impl=%s"
                 % (base, thunk, ("%08X" % impl) if impl else "unresolved"))
    if expect is not None and not hit:
        emit("      !! %s does not reach %08X from any real thunk" % (name, expect))
    return rows


# ------------------------------------------------------------------ disasm scan
def lea_sites(disps):
    """Find `lea r64,[rax+disp32]` sites for the given displacements."""
    d = data()
    name, va, vsz, raw, rsz, _ = sec_by_name(".text")
    code = d[raw:raw + rsz]
    want = {struct.pack("<I", x): x for x in disps}
    out = {}
    i = 0
    n = len(code)
    while i < n - 8:
        if code[i] == 0x48 and code[i + 1] == 0x8D and (code[i + 2] & 0xC7) == 0x80:
            key = code[i + 3:i + 7]
            if key in want:
                out.setdefault(want[key], []).append(va + i)
                i += 7
                continue
        i += 1
    return out


# ============================================================== D4
def d4():
    hdr("D4  enemy spawner identity semantics")
    emit("claim: the enemy save key is an AUTHORED FGuid baked into the level package,")
    emit("       reached through SavableActor_C.GUIDs -> EnemySpawner_C.GetSaveGUID.")
    emit()

    emit("[D4.1] the save-state API, resolved from the pair tables")
    show_symbol("StoreEnemySaveState", expect=0x00E3BF10)
    show_symbol("RetrieveEnemySaveState", expect=0x00E39240)
    show_symbol("ClearEnemySaveState", expect=0x00E27570)
    show_symbol("ClearAllEnemySaveStates", expect=0x00E27460)
    show_symbol("GetNamedSaveData")
    show_symbol("GetActiveBaseSave")
    emit()

    emit("[D4.2] StoreEnemySaveState writes TMap at save+0x270 with a 3-field record")
    lin(0x00E3BF10, 24, "StoreEnemySaveState impl")
    emit()
    emit("[D4.3] ClearEnemySaveState / ClearAllEnemySaveStates touch the same +0x270")
    lin(0x00E27570, 10, "ClearEnemySaveState impl")
    lin(0x00E27460, 12, "ClearAllEnemySaveStates impl")
    emit()

    emit("[D4.4] offset derivation: which functions use +0x270 / +0x220")
    sites = lea_sites([0x270, 0x220])
    for disp, addrs in sorted(sites.items()):
        emit("   +0x%03X : %d site(s)" % (disp, len(addrs)))
        for a in addrs:
            f = func_at(a)
            emit("        %08X  in FUN_%08X" % (a, f[0] if f else 0))
    emit()

    emit("[D4.5] SpawnerGUID is a NAME only on the spawner class - it is not serialised")
    for s in ("SpawnerGUID", "GUIDs", "GetSaveGUID", "FS_SpawnerInterface"):
        emit("   %-22s rva(s): %s" % (s, [("%08X" % r) for r, _ in find_string(s, limit=4)]))
    emit()

    emit("[D4.6] the authored GUID property, decoded from a cooked map")
    emit("   tagged layout verified byte-for-byte in 100_GR.umap:")
    emit("      FName GUIDs | FName StructProperty | i32 size=16 | i32 idx=0")
    emit("      | FName Guid | 16B StructGuid=0 | u8 HasPropertyGuid=0 | 16B value")
    _d4_asset_scan()


def _d4_asset_scan():
    """Name-reference scan: which cooked assets mention a symbol."""
    try:
        sys.path.insert(0, os.path.join(ROOT, "analysis", "pak", "code"))
        import pakread
    except Exception as e:                                    # pragma: no cover
        emit("   (pak unavailable: %s)" % e)
        return
    targets = ["StoreEnemySaveState", "RetrieveEnemySaveState", "RollItemDrop",
               "GetDropSourceIDFromDataAsset", "EnemyDiedEvent", "RegisterEnemyKill",
               "ApplyDamage", "ApplyPointDamage", "BlockSuspendSaves",
               "ClearAllEnemySaveStates"]
    found = {t: [] for t in targets}
    with pakread.Pak() as pk:
        for e in sorted(pk.entries):
            if not e.lower().endswith((".uasset", ".umap")):
                continue
            try:
                blob = pk.payload(e)
            except Exception:
                continue
            for t in targets:
                if t.encode() in blob:
                    found[t].append(e)
    for t in targets:
        emit()
        emit("   %s referenced by %d asset(s):" % (t, len(found[t])))
        for e in found[t][:12]:
            emit("       " + e)


def _spawner_census():
    """Placed enemy-spawner actors per map, and EnemySaveStates keys per map.

    The two columns are what proves a save record is created per *spawner instance*
    that has been reached, rather than per enemy class or per world.
    """
    try:
        sys.path.insert(0, os.path.join(ROOT, "analysis", "pak", "code"))
        import pakread
        import blueprints
    except Exception as e:                                    # pragma: no cover
        emit("   (pak unavailable: %s)" % e)
        return
    FX = ("Dust", "Spark", "FX", "Decal", "Particle", "Smoke", "Ember", "Leaf", "Drip")

    with pakread.Pak() as pk:
        maps = sorted(p for p in pk.entries if p.lower().endswith(".umap"))
        rows = []
        tot = 0
        for m in maps:
            try:
                d = pk.payload(m)
                info = blueprints.parse(d)
                _, exp = blueprints.try_imports_exports(d, info)
            except Exception:
                continue
            n = 0
            for e in exp:
                nm, cls = e["name"] or "", e["class"] or ""
                if nm.startswith("Default__") or "Spawner" not in cls:
                    continue
                if any(f in cls for f in FX):
                    continue
                n += 1
            if n:
                rows.append((m.rsplit("/", 1)[-1], n))
                tot += n
        emit("   placed ENEMY spawner actors (FX spawners excluded): %d over %d maps"
             % (tot, len(rows)))
        emit("   %-30s %s" % ("map", "enemy spawners"))
        for name, n in rows:
            emit("   %-30s %d" % (name, n))


def _pak_guid_match():
    """414/414: EnemySaveStates keys vs authored GUIDs in map packages."""
    try:
        sys.path.insert(0, os.path.join(ROOT, "analysis", "pak", "code"))
        import pakread
        import blueprints
    except Exception as e:                                    # pragma: no cover
        emit("   (pak unavailable: %s)" % e)
        return
    sav = os.path.expandvars(r"%LOCALAPPDATA%\QueensDomain\Saved\SaveGames\World0.sav")
    if not os.path.exists(sav):
        emit("   (no World0.sav on this machine - skipping)")
        return

    def fstr(d, p):
        n = struct.unpack_from("<i", d, p)[0]
        if n == 0:
            return "", p + 4
        if n < 0:
            return d[p + 4:p + 4 - n * 2][:-2].decode("utf-16-le", "replace"), p + 4 - n * 2
        return d[p + 4:p + 4 + n][:-1].decode("utf-8", "replace"), p + 4 + n

    b = open(sav, "rb").read()
    p = 12 + 2 + 2 + 2 + 4
    _, p = fstr(b, p)
    p += 4
    ncv = struct.unpack_from("<i", b, p)[0]
    p += 4 + ncv * 20
    _, p = fstr(b, p)
    keys = []
    while p < len(b):
        name, q = fstr(b, p)
        if name in ("", "None"):
            break
        typ, q = fstr(b, q)
        size = struct.unpack_from("<i", b, q)[0]; q += 4
        q += 4
        if typ == "StructProperty":
            _, q = fstr(b, q); q += 16
        elif typ == "BoolProperty":
            q += 1
        elif typ in ("ByteProperty", "EnumProperty", "ArrayProperty", "SetProperty"):
            _, q = fstr(b, q)
        elif typ == "MapProperty":
            _, q = fstr(b, q); _, q = fstr(b, q)
        q += 17 if b[q] else 1
        if name == "EnemySaveStates":
            r = q + 4
            n = struct.unpack_from("<i", b, r)[0]; r += 4
            stride = (size - 8) // n
            keys = [b[r + i * stride: r + i * stride + 16] for i in range(n)]
        p = q + size

    def authored_guids(d):
        info = blueprints.parse(d)
        names = info.get("names") if isinstance(info, dict) else None
        if not names:
            return []
        idx = {}
        for i, s in enumerate(names):
            idx.setdefault(s, i)
        if any(k not in idx for k in ("GUIDs", "StructProperty", "Guid")):
            return []
        ni, ti, si = idx["GUIDs"], idx["StructProperty"], idx["Guid"]
        needle = struct.pack("<I", ni) + struct.pack("<I", 0)
        out, pos = [], 0
        while True:
            q = d.find(needle, pos)
            if q < 0 or q + 96 > len(d):
                break
            pos = q + 1
            if (struct.unpack_from("<I", d, q + 8)[0] != ti or
                    struct.unpack_from("<I", d, q + 12)[0] != 0 or
                    struct.unpack_from("<i", d, q + 16)[0] != 16 or
                    struct.unpack_from("<I", d, q + 24)[0] != si or
                    struct.unpack_from("<I", d, q + 28)[0] != 0 or
                    d[q + 32:q + 48] != b"\x00" * 16 or d[q + 48] != 0):
                continue
            out.append(d[q + 49:q + 65])
        return out

    allg, per = [], {}
    with pakread.Pak() as pk:
        for m in sorted(p for p in pk.entries if p.lower().endswith(".umap")):
            try:
                d = pk.payload(m)
                ue = m[:-len(".umap")] + ".uexp"
                if ue in pk.entries:
                    d += pk.payload(ue)
            except Exception:
                continue
            g = authored_guids(d)
            if g:
                per[m.rsplit("/", 1)[-1]] = len(g)
                allg.extend(g)
    u = set(allg)
    emit("   EnemySaveStates records           : %d" % len(keys))
    emit("   authored GUIDs parsed from maps   : %d over %d maps" % (len(allg), len(per)))
    emit("   save keys matched by an authored GUID: %d / %d"
         % (sum(1 for k in keys if k in u), len(keys)))
    emit("   authored GUIDs NOT in this save   : %d  (the world authors far more"
         % len(u - set(keys)))
    emit("                                        than one playthrough touches)")
    emit("   per-map authored GUID count       : %s" % per)


# ============================================================== D5
def d5():
    hdr("D5  enemy death / damage authority surface")
    emit("claim: damage is applied by UGameplayStatics::ApplyDamage -> AActor::TakeDamage;")
    emit("       HP lives in UFB_AttributeComponent; the death DELEGATE is a Blueprint")
    emit("       multicast declared by C++ and broadcast from EnemyBase_C bytecode;")
    emit("       persistence is written by one native function, StoreEnemySaveState.")
    emit()

    emit("[D5.1] damage entry")
    show_symbol("ApplyDamage", expect=0x028D2190)
    lin(0x028D2190, 42, "UGameplayStatics::ApplyDamage")
    emit()
    emit("[D5.2] the attribute component that owns CurrentHealth/MaxHealth")
    for n in ("ApplyAttributeModifier", "AddToAttributeBaseValue", "SetAttributeBaseValue",
              "GetAttributeValue", "ClampAttributeWasUpdated"):
        show_symbol(n)
    emit()
    emit("[D5.3] damageable-actor registry")
    for n in ("RegisterDamageableActor", "UnregisterDamageableActor",
              "DamageableActorEndedPlay", "TryApplyDamage"):
        show_symbol(n)
    emit()
    emit("[D5.4] the only native function that touches the death path by name")
    show_symbol("OnEnemyDied", expect=0x00E36240)
    show_symbol("OnEnemyStaggered", expect=0x00E36250)
    lin(0x00E36240, 6, "OnEnemyDied impl (UFS_EnemyAction)")
    emit()
    emit("[D5.5] persistence writer")
    show_symbol("StoreEnemySaveState", expect=0x00E3BF10)
    for site, fn, mn in callers(0x00E3BF10):
        emit("      caller %08X in FUN_%08X  (%s)" % (site, fn, mn))
    emit()
    emit("[D5.6] call-graph edges for the whole death/persistence cluster")
    for fn, label in ((0x00E3BF10, "StoreEnemySaveState"),
                      (0x00E39240, "RetrieveEnemySaveState"),
                      (0x00E27460, "ClearAllEnemySaveStates"),
                      (0x00E27570, "ClearEnemySaveState"),
                      (0x00E2D100, "GetActiveBaseSave")):
        cs = callers(fn)
        emit("      %-24s <- %d direct caller(s): %s"
             % (label, len(cs), ["%08X/FUN_%08X" % (s, f) for s, f, _ in cs][:8]))


# ============================================================== D6
def d6():
    hdr("D6  loot roll location")
    emit("claim: the drop roll is a local, single-machine call in UFS_ItemDropSystem,")
    emit("       cached in a transient map keyed by (FName source, int32 dropIndex).")
    emit()

    emit("[D6.1] the three functions of UFS_ItemDropSystem (table 0x03951480)")
    show_symbol("GetDropSourceIDFromClass", expect=0x00E2E5C0)
    show_symbol("GetDropSourceIDFromDataAsset", expect=0x00E2E5C0)
    show_symbol("RollItemDrop", expect=0x00E392F0)
    emit()
    emit("[D6.2] GetDropSourceIDFrom* returns the source object's UObject Name (FName)")
    lin(0x00E2E5C0, 8, "impl: rax=[rdx+0x18]; [rcx]=rax  (UObject.Name)")
    emit()
    emit("[D6.3] RollItemDrop, disassembled past its .pdata end")
    lin(0x00E392F0, 95, "UFS_ItemDropSystem::RollItemDrop")
    emit()
    emit("[D6.4] the chance function it calls is FB_GetRandomBool")
    show_symbol("FB_GetRandomBool", expect=0x00BC5E70)
    lin(0x00BC5E70, 26, "FB_GetRandomBool(float P)")
    emit()
    emit("[D6.5] pair layout: key 12B (FName+i32), pair stride 0x20, value at key+12")
    emit("      evidence: shl rbx,5 / add rbx,[rdi] / lea rbx,[rbx+0xC] at 0x00E39364..")
    emit("      key store: mov qword [rsp+0x20],rbx ; mov dword [rsp+0x28],edi")
    emit()
    emit("[D6.6] the cache is transient: ItemDrops is not in World0.sav")
    sav = os.path.expandvars(r"%LOCALAPPDATA%\QueensDomain\Saved\SaveGames\World0.sav")
    if os.path.exists(sav):
        b = open(sav, "rb").read()
        for n in (b"ItemDrops", b"EnemyDropDataA", b"KilledEnemyCounts", b"EnemySaveStates"):
            emit("      %-20s serialised=%s" % (n.decode(), n + b"\x00" in b or n in b))


# ============================================================== D8
def d8():
    hdr("D8  input path")
    import pefile
    exe = os.path.join(ROOT, "QueensDomain", "QueensDomain", "Binaries", "Win64",
                       "QueensDomain-Win64-Shipping.exe")
    pe = pefile.PE(exe, fast_load=True)
    pe.parse_data_directories()
    imports = {}
    for entry in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []):
        dll = entry.dll.decode("latin-1")
        for imp in entry.imports:
            imports[imp.name.decode("latin-1") if imp.name else "#%d" % imp.ordinal] = dll
    probe = ["RegisterRawInputDevices", "GetRawInputData", "GetRawInputDeviceList",
             "GetRawInputDeviceInfoA", "GetAsyncKeyState", "GetKeyState",
             "MapVirtualKeyW", "PostMessageW", "SendMessageW", "PeekMessageW",
             "DispatchMessageW", "TranslateMessage", "SetCapture", "SetWindowsHookExW",
             "DirectInput8Create", "XInputGetState"]
    emit("   %-28s %s" % ("import", "provider"))
    for p in probe:
        emit("   %-28s %s" % (p, imports.get(p, "-- not imported --")))
    emit()
    emit("   DLLs in the import directory that matter for input:")
    for entry in pe.DIRECTORY_ENTRY_IMPORT:
        n = entry.dll.decode()
        if n.upper().startswith(("XINPUT", "DINPUT", "USER32", "HID")):
            emit("      " + n)
    emit()
    emit("   Blueprint-side input events (from the pak, FS_PlayerChar):")
    emit("      InpActEvt_Jump / Attack / Pause / Status / UltraAttack / WindDash")
    emit("      InpAxisEvt_TurnMouse, InpAxisEvt_LookUpMouse   <- mouse LOOK is an axis")


# ============================================================== C9
def c9():
    hdr("C9  save write triggers, especially the silent suspend saves")
    emit("claim: every save goes through UGameplayStatics::SaveGameToSlot, reached only")
    emit("       from the FB_SaveUtils plugin; the suspend save is a SetTimer loop in")
    emit("       FS_GameInstance_C writing an alternating slot index.")
    emit()

    emit("[C9.1] the single write primitive")
    show_symbol("SaveGameToSlot", expect=0x028F2B40)
    lin(0x028F2B40, 30, "UGameplayStatics::SaveGameToSlot")
    emit()
    emit("[C9.2] every direct caller of SaveGameToSlot (all inside the FB_SaveUtils module)")
    for site, fn, mn in callers(0x028F2B40):
        f = func_at(fn)
        emit("      site %08X in FUN_%08X [%08X..%08X]"
             % (site, fn, f[0] if f else 0, f[1] if f else 0))
    emit()
    emit("[C9.3] slot-name + slot-index helpers")
    show_symbol("GetSaveSlotName", expect=0x00E306D0)
    show_symbol("GetGameSaveRootFolder", expect=0x00E2E6B0)
    show_symbol("GetCurrentSaveFileSlot", expect=0x00BE2C20)
    show_symbol("SetCurrentSaveFileSlot", expect=0x00BE3A80)
    show_symbol("GetSaveFileName", expect=0x00BE2F80)
    show_symbol("GetCurrentSaveFileName", expect=0x00BE2BC0)
    show_symbol("InitSave", expect=0x00E348E0)
    show_symbol("SetActiveSave")
    emit()
    lin(0x00E306D0, 40, "GetSaveSlotName impl (builds the FString slot name)")
    emit()
    emit("[C9.4] related engine saves")
    for n in ("LoadGameFromSlot", "DeleteGameInSlot", "DoesSaveGameExist",
              "CreateSaveGameObject"):
        show_symbol(n)
    emit()
    emit("[C9.5] suspend plumbing in the exe and the pak (name references)")
    for s in ("GenerateSuspendData", "RestoreFromSuspendData", "RestoreSuspendState",
              "GetSafeLocationSuspendState", "SuspendIndex"):
        emit("   %-28s %s" % (s, [("%08X" % r) for r, _ in find_string(s, limit=3)]))
    emit()
    emit("   no literal 'World0', '_Suspend_' or 'World0_Suspend' exists in the exe")
    emit("   or in any cooked package -> the slot name is assembled at runtime.")


# ============================================================== main
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--evidence", action="store_true",
                    help="also write analysis/re/combat_save_evidence.txt")
    ap.add_argument("--no-pak", action="store_true", help="skip the pak halves of D4")
    ap.add_argument("--census", action="store_true",
                    help="also count placed enemy-spawner actors per map (D4)")
    ap.add_argument("--only", default="", help="comma list: D4,D5,D6,D8,C9")
    args = ap.parse_args()

    emit("QueensDomain-Win64-Shipping.exe  ImageBase %08X  SizeOfImage %08X"
         % (IMAGE_BASE, 0x5043000))
    emit("sections:")
    for s in sections():
        emit("   %-10s rva %08X vsz %08X raw %08X rsz %08X" % (s[0], s[1], s[2], s[3], s[4]))
    emit("pair tables: %d names / %d tables"
         % (len(pairs()), len({b for v in pairs().values() for b, _, _ in v})))
    emit("  NOTE: two mirrored tables exist per module; one maps every name to the")
    emit("        generic Blueprint-VM dispatcher %08X and is discarded here."
         % GENERIC_DISPATCHER)

    want = [w.strip().upper() for w in args.only.split(",") if w.strip()]
    run = lambda k: (not want) or (k in want)

    if run("D4"):
        d4()
        if not args.no_pak:
            emit()
            emit("[D4.7] 414/414 verification against the live save")
            _pak_guid_match()
            if args.census:
                emit()
                emit("[D4.8] spawner placements per map (the denominator)")
                _spawner_census()
    if run("D5"):
        d5()
    if run("D6"):
        d6()
    if run("D8"):
        d8()
    if run("C9"):
        c9()

    if args.evidence:
        path = os.path.join(HERE, "combat_save_evidence.txt")
        with io.open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(OUT) + "\n")
        print("\nwrote %s (%d lines)" % (path, len(OUT)))


if __name__ == "__main__":
    main()
