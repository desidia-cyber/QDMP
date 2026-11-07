#!/usr/bin/env python3
"""Static probe for gap-register items D1 (dialogue pause), D3 (dialogue lock) and
D7 (partner HUD) on QueensDomain-Win64-Shipping.exe (UE4.27.2).

Everything here is reproducible from two artefacts that already exist in the repo:

  * the shipping exe                       -> analysis/re/qdlib.py   (pefile + capstone)
  * the native-function pair tables        -> analysis/re/nativepairs.bin
  * the direct-call index                  -> analysis/re/calls.bin  (via hunt2.callers)
  * the rip-relative xref index            -> analysis/re/xrefs.bin  (via hunt.refs_to)
  * the cooked content                     -> analysis/pak/code/pakread.py + the .pak

No game launch, no debugger.  Run:

    python dialogue_ui_probe.py all               # everything, evidence to stdout
    python dialogue_ui_probe.py d1                # pause / time-dilation sinks
    python dialogue_ui_probe.py d1graph           # dialogue entry -> sink reachability
    python dialogue_ui_probe.py d3                # dialogue exclusivity state
    python dialogue_ui_probe.py d7                # HUD class + widget creation path
    python dialogue_ui_probe.py pak               # name-table scan of all packages
    python dialogue_ui_probe.py classes           # QueensDomain UClass registrations

Key methodological notes (these are load-bearing for the answers)
------------------------------------------------------------------
1. The pair table has TWO copies per module.  In one copy every entry is a lazy
   **resolver stub** that ends in `call 0x0119D280` (the generic Blueprint-VM
   dispatcher) and returns a function pointer instead of running the function.
   That copy yields 0x0119D280 for every name and is useless.  `resolve_native`
   below therefore disassembles each candidate thunk and keeps the one whose
   last direct call (after the FFrame stack helpers are dropped) lands on real
   code.  The bad copy is reported explicitly, never silently discarded.
2. `.pdata` is NOT a reliable function list on this image (see
   analysis/re/SUBSTRATE-TARGETS.md "ruled out" #3), so "is a call target" is
   used as the sanity test, not "has a RUNTIME_FUNCTION".
3. ProcessEvent is vtable slot 68 = offset 0x220 on every UObject-derived
   class.  The dialogue code reaches Blueprint code through that slot, which is
   how a C++->BP edge is recognised.
"""
import os
import re
import sys
import json
import struct
import marshal
import collections

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from qdlib import (disasm, func_at, read_rva, data, off_to_rva, find_string,
                   iter_strings, insn_target, sec_by_name, hexs)
from hunt import refs_to
from hunt2 import callers

PAIRS_BIN = os.path.join(HERE, "nativepairs.bin")
PAK_CODE = r"G:\Freespace\Queens_Domain\analysis\pak\code"

# Frame-stack / string / FName boilerplate that every Blueprint exec thunk
# calls.  They are never the native implementation.
BOILERPLATE = {
    0x01189A70,  # FFrame::Step / property-walk helper (no-arg stack probe)
    0x01188D60,  # FFrame stack probe (int32 variant)
    0x01189390,  # FFrame stack probe (dword variant)
    0x01144EF0,  # FFrame stack probe (bool variant)
    0x011B1870,  # FFrame::StepExplicitProperty (out-param path)
    0x011B18A0,  # FFrame::StepExplicitProperty (default path)
    0x011BB360,  # FMemory::Memzero / FString reset
    0x007764F0,  # FFrame::SerializeBits-ish helper
}
GENERIC_VM_DISPATCHER = 0x0119D280   # the useless copy's terminal call

# The UObject script-VM machinery (FFrame/Step/FName plumbing) lives in one
# contiguous slab of CoreUObject.  A "last call" that lands in here is a
# property-walk helper, not the implementation, so it is not accepted.
VM_LO, VM_HI = 0x01140000, 0x011D0000
CORE_STRLIB_LO, CORE_STRLIB_HI = 0x036B4C00, 0x036B5000


def is_plumbing(addr):
    if addr in BOILERPLATE:
        return True
    if VM_LO <= addr < VM_HI:
        return True
    if CORE_STRLIB_LO <= addr < CORE_STRLIB_HI:
        return True
    return False

PE = data()


# --------------------------------------------------------------------------- #
# pair tables
# --------------------------------------------------------------------------- #

_PAIRS = None


def pairs():
    global _PAIRS
    if _PAIRS is None:
        with open(PAIRS_BIN, "rb") as f:
            _PAIRS = marshal.load(f)["pairs"]
    return _PAIRS


def _thunk_facts(thunk):
    """(all direct calls, last non-boilerplate call, tail jmp, body bytes)."""
    f = func_at(thunk)
    end = f[1] if f else thunk + 0x300
    calls, tail = [], None
    for ins in disasm(thunk, end_rva=end):
        if ins.mnemonic == "call" and ins.operands and ins.operands[0].type == 2:
            calls.append((ins.address, ins.operands[0].imm & 0xFFFFFFFF))
        elif ins.mnemonic == "jmp":
            if ins.operands and ins.operands[0].type == 2:
                tail = ("direct", ins.operands[0].imm & 0xFFFFFFFF)
            elif "ptr" in ins.op_str:
                tail = ("virtual", ins.op_str)
            else:
                tail = ("indirect", ins.op_str)
    real = [c for _s, c in calls if not is_plumbing(c)]
    return calls, (real[-1] if real else None), tail, end


def _looks_like_exec_thunk(thunk, body, size):
    """True for the standard FNativeFunc thunk.

    Shape (see GetPlayerPawn, 0x02DBF9E0): prologue saves rbx, does
    `mov rbx, rdx` (rdx = FFrame&), calls a script-VM helper, then repeatedly
    tests `[rbx + 0x20]` (FFrame::Code) and finally makes ONE direct call to
    the implementation.  Anything else means the pair table stores the
    implementation itself (AHUD::DrawText at 0x02DCE0E0 is such a case).
    """
    if not body or size > 0x200:
        return False
    ops = [ins.op_str for ins in body]
    has_frame = any("qword ptr [rbx + 0x20]" in o for o in ops)
    has_helper = any(
        ins.mnemonic == "call" and ins.operands and ins.operands[0].type == 2
        and is_plumbing(ins.operands[0].imm & 0xFFFFFFFF)
        for ins in body)
    return has_frame and has_helper


def resolve_native(name, verbose=False):
    """name -> list of {table, thunk, impl, kind, note} for every pair copy."""
    out = []
    for table, thunk, name_rva in pairs().get(name, []):
        f = func_at(thunk)
        end = f[1] if f else thunk + 0x300
        size = end - thunk
        body = disasm(thunk, end_rva=end)
        calls, last, tail, _end = _thunk_facts(thunk)
        via_generic = any(c == GENERIC_VM_DISPATCHER for _s, c in calls)
        is_thunk = _looks_like_exec_thunk(thunk, body, size)
        if via_generic and not is_thunk:
            kind, impl = "resolver-stub (unusable)", GENERIC_VM_DISPATCHER
        elif via_generic and is_thunk:
            kind, impl = "exec-thunk->BP-event", GENERIC_VM_DISPATCHER
        elif is_thunk:
            impl = last
            if impl is None and tail and tail[0] == "direct":
                impl = tail[1]
            kind = "exec-thunk"
        elif tail and tail[0] == "direct" and not (thunk <= tail[1] < end):
            kind, impl = "tailcall-thunk", tail[1]
        elif tail and tail[0] == "virtual":
            kind, impl = "virtual-thunk", None
        else:
            kind, impl = "direct-impl", thunk
        rec = {
            "name": name, "table": table, "thunk": thunk, "name_rva": name_rva,
            "size": size, "kind": kind, "impl": impl,
            "calls": calls, "tail": tail,
            "impl_is_call_target": bool(impl and callers(impl)),
            "impl_has_pdata": bool(impl and func_at(impl)),
        }
        out.append(rec)
        if verbose:
            print("  %-22s table %08X thunk %08X size %3d  impl %s  (pdata=%s calltarget=%s)"
                  % (kind, table, thunk, size,
                     ("%08X" % impl) if impl else "-",
                     rec["impl_has_pdata"], rec["impl_is_call_target"]))
    return out


def best_impl(name):
    """The implementation behind the *usable* copy of a pair-table name."""
    for rec in resolve_native(name):
        if rec["kind"] in ("exec-thunk", "tailcall-thunk", "direct-impl") and rec["impl"]:
            return rec["impl"]
    return None


# --------------------------------------------------------------------------- #
# call graph
# --------------------------------------------------------------------------- #

def direct_calls(fn):
    """Direct call/jmp targets inside a function, minus boilerplate."""
    f = func_at(fn)
    if not f:
        return []
    out = []
    for ins in disasm(fn, end_rva=f[1]):
        if ins.mnemonic in ("call", "jmp") and ins.operands and ins.operands[0].type == 2:
            t = ins.operands[0].imm & 0xFFFFFFFF
            if t not in BOILERPLATE:
                out.append((ins.address, ins.mnemonic, t))
    return out


def virtual_calls(fn):
    """Virtual dispatches, reported as the vtable byte offset."""
    f = func_at(fn)
    if not f:
        return []
    out = []
    for ins in disasm(fn, end_rva=f[1]):
        if ins.mnemonic == "call" and "ptr" in ins.op_str:
            m = re.search(r"\[(\w+)\s*\+\s*(0x[0-9a-f]+)\]", ins.op_str)
            out.append((ins.address, ins.op_str, int(m.group(2), 16) if m else None))
    return out


def reach(start, sinks, max_depth=6, max_nodes=4000):
    """BFS over direct calls.  Returns {sink: path} for the first hit of each sink.

    This is a *direct-call* graph only: it cannot see Blueprint bytecode edges
    (a BP->native edge is ProcessEvent -> an exec thunk, so it appears here only
    as a ProcessEvent call, never as a call to the target).  Every reported
    negative is therefore scoped to native direct calls.
    """
    seen = {start}
    frontier = [(start, [start])]
    found = {}
    while frontier and len(seen) < max_nodes:
        nxt = []
        for node, path in frontier:
            if len(path) > max_depth:
                continue
            for site, _mn, tgt in direct_calls(node):
                if tgt in sinks and tgt not in found:
                    found[tgt] = path + [tgt]
                if tgt not in seen:
                    seen.add(tgt)
                    nxt.append((tgt, path + [tgt]))
        frontier = nxt
    return found, len(seen)


# --------------------------------------------------------------------------- #
# D1 - pause / time-dilation / tick-disable
# --------------------------------------------------------------------------- #

# Blueprint-visible names that can stop time or stop an actor.  Unambiguous
# only: "Pause"/"CanPause"/"SetPaused" exist on a dozen unrelated classes
# (UMediaPlayer, UAnimInstance, UAudioComponent, ...) and are probed separately.
D1_NAMES = [
    "SetGamePaused", "IsGamePaused", "SetGlobalTimeDilation", "GetGlobalTimeDilation",
    "SetActorTickEnabled", "SetComponentTickEnabled", "SetTickableWhenPaused",
    "GetTickableWhenPaused", "SetIgnoreTimeDilation", "GetIgnoreTimeDilation",
    "GetActorTimeDilation",
]
# Secondary: every pair-table entry whose only copy is a virtual tail-call, i.e.
# APlayerController::Pause / ::CanPause / ::SetPause.  Reported with a caveat.
D1_SECONDARY = ["Pause", "CanPause", "ServerPause", "SetPaused"]

# Native functions the game's own dialogue layer is built from.
DIALOGUE_ENTRY_NAMES = [
    "InitAction", "InitAction_BP", "RunAction", "FinishAction", "CancelAction",
    "CanExecuteAction", "JumpToDialogue",
    "EvaluateConditionSet", "EvaluateSingleCondition",
]


def d1():
    print("=" * 78)
    print("D1  pause / time-dilation / tick-disable: implementations")
    print("=" * 78)
    print("  (for each name: the usable pair-table copy, its exec thunk, and the")
    print("   implementation the thunk's last real call lands on)")
    print()
    impls = {}
    for n in D1_NAMES:
        recs = resolve_native(n)
        if not recs:
            print("  %-26s -- no pair-table entry" % n)
            continue
        good = [r for r in recs
                if r["kind"] in ("exec-thunk", "tailcall-thunk", "direct-impl")]
        bad = [r for r in recs if r["kind"] == "resolver-stub (unusable)"]
        if not good:
            print("  %-26s UNRESOLVED  (%d pair copies: %s)"
                  % (n, len(recs), ", ".join(r["kind"] for r in recs)))
            continue
        r = good[0]
        print("  %-26s impl %08X   via %s thunk %08X (table %08X, %d bytes)"
              % (n, r["impl"], r["kind"], r["thunk"], r["table"], r["size"]))
        if bad:
            print("  %-26s   [other copy table %08X is the unusable resolver stub -> %08X]"
                  % ("", bad[0]["table"], GENERIC_VM_DISPATCHER))
        impls.setdefault(r["impl"], n)
    print()
    print("  secondary - NOT used as evidence: these names are ambiguous across a")
    print("  dozen unrelated classes and resolve only to a vtable slot, not a function:")
    for n in D1_SECONDARY:
        for r in resolve_native(n):
            if r["kind"] == "virtual-thunk":
                print("    %-24s thunk %08X (table %08X) -> %s"
                      % (n, r["thunk"], r["table"], r["tail"]))
    print()

    print("-" * 78)
    print("D1  direct callers of each implementation (whole .text call index)")
    print("    'exec-thunk' below means the ONLY caller is a Blueprint exec thunk,")
    print("    i.e. the function is reachable only through ProcessEvent / BP bytecode")
    print("-" * 78)
    for impl in sorted(impls):
        cs = callers(impl)
        native = [(s, f) for s, f, _m in cs if not _is_exec_thunk(f)]
        print("  %08X  %-26s callers %-5d native callers %d"
              % (impl, impls[impl], len(cs), len(native)))
        for site, fn, mn in cs[:4]:
            print("      site %08X in FUN_%08X %s" % (site, fn,
                  "exec-thunk" if _is_exec_thunk(fn) else "NATIVE"))
        if len(cs) > 4:
            print("      ... %d more" % (len(cs) - 4))
        for site, fn in native[:8]:
            print("      NATIVE caller site %08X in FUN_%08X" % (site, fn))
    return impls


def _is_exec_thunk(fn):
    """A native-function exec thunk: <0x120 bytes and ends in a single ret."""
    f = func_at(fn)
    if not f or (f[1] - f[0]) >= 0x140:
        return False
    ops = [ins.mnemonic for ins in disasm(fn, end_rva=f[1])]
    return ops.count("ret") == 1


def d1_body(impl_map):
    print()
    print("-" * 78)
    print("D1  bodies of the two GameplayStatics entry points (to name the")
    print("    deeper sink each one uses)")
    print("-" * 78)
    for label, name in (("SetGamePaused", "SetGamePaused"),
                        ("SetGlobalTimeDilation", "SetGlobalTimeDilation"),
                        ("IsGamePaused", "IsGamePaused")):
        impl = best_impl(name)
        if not impl:
            print("  %s: unresolved" % label)
            continue
        print("  === %s @ %08X ===" % (label, impl))
        for ins in disasm(impl, end_rva=func_at(impl)[1]):
            note = ""
            t = insn_target(ins)
            if t is not None and 0x4400000 <= t < 0x4C08000:
                note = "   ; GLOBAL %08X" % t
            print("    %08x %-9s %-44s%s" % (ins.address, ins.mnemonic, ins.op_str, note))
        print()


def d1_graph():
    print("=" * 78)
    print("D1  dialogue entry -> pause/dilation reachability (native direct calls)")
    print("=" * 78)
    sinks = {}
    for n in ("SetGamePaused", "SetGlobalTimeDilation", "SetActorTickEnabled",
              "SetComponentTickEnabled", "SetIgnoreTimeDilation"):
        i = best_impl(n)
        if i:
            sinks[i] = n
    print("  sink set: %s" % ", ".join("%08X=%s" % (a, b) for a, b in sorted(sinks.items())))
    print()
    # Every usable (non-resolver) copy of every dialogue-entry name is explored,
    # because a class can override a name more than once.
    entries = []
    for n in DIALOGUE_ENTRY_NAMES:
        for r in resolve_native(n):
            if r["kind"] in ("exec-thunk", "tailcall-thunk", "direct-impl") and r["impl"]:
                entries.append((n, r["table"], r["impl"]))
    print("  entry points (name, pair table, implementation):")
    for n, t, i in entries:
        print("    %-26s table %08X  impl %08X" % (n, t, i))
    print()
    total = 0
    for n, t, i in entries:
        found, nodes = reach(i, set(sinks))
        total += nodes
        print("  %-26s %08X  sinks: %-4s  (%d nodes explored)"
              % (n, i, ("NONE" if not found else
                        ", ".join("%s@%s" % (sinks[k], "->".join("%08X" % x for x in v))
                                  for k, v in found.items())), nodes))
    print()
    print("  total distinct native nodes explored: %d" % total)
    print()
    print("  Blueprint dispatch (ProcessEvent == vtable byte offset 0x220) inside")
    print("  the dialogue bodies - this is what a C++->BP edge looks like:")
    for n, t, i in entries:
        v = [x for x in virtual_calls(i) if x[2] == 0x220]
        if v:
            print("    %-26s %08X  ProcessEvent at %s"
                  % (n, i, ", ".join("%08X" % a for a, _o, _d in v)))
    print("  (an absent line means the body never dispatches to Blueprint)")
    print()


# --------------------------------------------------------------------------- #
# D3 - dialogue exclusivity
# --------------------------------------------------------------------------- #

def d3():
    print("=" * 78)
    print("D3  dialogue exclusivity: state, guards, and where they are written")
    print("=" * 78)
    # 1. Every native dialogue name, with its impl.
    print("  native dialogue classes (pair table):")
    for n in DIALOGUE_ENTRY_NAMES:
        i = best_impl(n)
        print("    %-26s %s" % (n, ("%08X" % i) if i else "unresolved"))
    print()

    # 2. Class registration stubs -> UClass caches.  Gives the C++ surface a
    #    runtime address to inspect later, and proves the classes exist here.
    print("  UClass registrations for the dialogue classes:")
    for cls in ("UFS_DialogueAction", "UFS_DialogueData", "AFS_HUD_Base",
                "AFS_PlayerController", "AFS_PlayerCharacter"):
        _stub, info = class_registration(cls)
        print("    %-22s %s" % (cls, info))
    print()

    # 3. Is there a "busy"/"in dialogue" flag in the native layer?  Scan every
    #    native dialogue function for reads/writes of a bool at a constant
    #    offset on `this` (rcx) - the shape of an exclusivity guard.
    print("  bool-sized field access on `this` inside native dialogue bodies")
    print("  (an exclusivity guard shows up as a byte load/store at this+0xNN)")
    for n in DIALOGUE_ENTRY_NAMES:
        i = best_impl(n)
        if not i:
            continue
        f = func_at(i)
        hits = []
        for ins in disasm(i, end_rva=(f[1] if f else i + 0x200)):
            if "byte ptr [rcx" in ins.op_str or "byte ptr [rbx" in ins.op_str:
                hits.append("%08X %s %s" % (ins.address, ins.mnemonic, ins.op_str))
        print("    %-26s %08X  %s" % (n, i, ("; ".join(hits) if hits else "no byte field access")))
    print()
    print("  NOTE: the exclusivity state is not a native field.  It lives in the")
    print("  Blueprint layer - run `pak` / `pkg` for the name-table evidence that")
    print("  names the guards.  Summary of what those modes find:")
    pak_focus()
    print()


# --------------------------------------------------------------------------- #
# D7 - HUD
# --------------------------------------------------------------------------- #

D7_NAMES = [
    "AddToViewport", "AddToPlayerScreen", "Create", "GetHUD", "ShowHUD",
    "InitializeHUDForPlayer", "ClientSetHUD", "ReceiveDrawHUD",
    "SpawnObject", "GetUserWidgetObject", "SetWidget", "AddWidget",
]


def d7():
    print("=" * 78)
    print("D7  HUD: class registration + widget creation path")
    print("=" * 78)
    for n in D7_NAMES:
        recs = resolve_native(n)
        if not recs:
            print("  %-26s -- no pair entry" % n)
            continue
        for r in recs:
            print("  %-26s table %08X thunk %08X thunkbytes %3d  %-14s impl %s"
                  % (n, r["table"], r["thunk"], r["size"], r["kind"],
                     ("%08X" % r["impl"]) if r["impl"] else r["tail"]))
    print()

    print("  AFS_HUD_Base / AFS_PlayerController / AFS_PlayerCharacter registration:")
    for cls in ("AFS_HUD_Base", "AFS_PlayerController", "AFS_PlayerCharacter",
                "AFS_GameModeCore", "UFS_GameViewport"):
        _stub, info = class_registration(cls)
        print("    %-22s %s" % (cls, info))
    print()

    impl = best_impl("AddToViewport")
    if impl:
        print("  UUserWidget::AddToViewport body @ %08X  (tail call = the real" % impl)
        print("  add-to-screen entry point; the offset printed is a vtable byte offset)")
        for ins in disasm(impl, end_rva=func_at(impl)[1] if func_at(impl) else impl + 0x10):
            print("    %08x %-9s %s" % (ins.address, ins.mnemonic, ins.op_str))
        print()

    impl = best_impl("Create")
    if impl:
        print("  UWidgetBlueprintLibrary::Create body head @ %08X" % impl)
        f = func_at(impl)
        for i, ins in enumerate(disasm(impl, end_rva=f[1] if f else impl + 0x200)):
            if i > 26:
                print("    ...")
                break
            t = insn_target(ins)
            print("    %08x %-9s %-44s%s" % (ins.address, ins.mnemonic, ins.op_str,
                                             ("   -> %08X" % t) if t else ""))
        print()


def class_registration(class_name):
    """Find the UClass registration stub for a class name and its UClass* cache.

    Shape (verified on AFS_HUD_Base, 0x00605C20):
        sub rsp,0x48 ; xor eax,eax
        lea r9,  <package wide string>     "/Script/QueensDomain"
        lea r8,  <class   wide string>     "AFS_HUD_Base"
        lea rdx, <Z_Construct fn>
        lea rcx, <StaticClass getter fn>
        call 0x011B2330                     ; UClass registration
    """
    wide = (class_name + "\x00").encode("utf-16-le")
    I = PE.find(wide)
    if I < 0:
        return None, "class-name wide string not found"
    name_rva = off_to_rva(I)
    sites = refs_to(name_rva)
    if not sites:
        return None, "name string %08X has no xref" % name_rva
    site, stub, _mn = sites[0]
    f = func_at(stub)
    getter = zc = cache = package = None
    for ins in disasm(stub, end_rva=(f[1] if f else stub + 0x60)):
        t = insn_target(ins)
        if t is None:
            continue
        if ins.op_str.startswith("rcx"):
            getter = t
        elif ins.op_str.startswith("rdx"):
            zc = t
        elif ins.op_str.startswith("r8"):
            pass
        elif ins.op_str.startswith("r9"):
            package = t
    # the UClass* cache is the .bss qword the Z_Construct tests
    if zc:
        for ins in disasm(zc, end_rva=(func_at(zc)[1] if func_at(zc) else zc + 0x120)):
            t = insn_target(ins)
            if t is not None and 0x4900000 <= t < 0x4C08000:
                cache = t
                break
    return stub, ("stub %08X  name@%08X  StaticClass %s  Z_Construct %s  UClassCache %s"
                  % (stub, name_rva,
                     ("%08X" % getter) if getter else "-",
                     ("%08X" % zc) if zc else "-",
                     ("%08X" % cache) if cache else "-"))


# --------------------------------------------------------------------------- #
# pak name-table scan
# --------------------------------------------------------------------------- #

PAK_PROBES = [
    # D1 sinks, as they appear to a Blueprint
    "SetGamePaused", "IsGamePaused", "SetGlobalTimeDilation", "GetGlobalTimeDilation",
    "SetActorTickEnabled", "SetComponentTickEnabled", "SetTickableWhenPaused",
    "SetIgnoreTimeDilation", "CustomTimeDilation", "bIgnoreTimeDilation",
    # the game's own pause-by-dilation subsystem
    "SetPauseTimeDilation", "SetPausedEnemyTimeDilation", "IsTimeDilationPaused",
    "IsPausedTimeDilationEnabled", "IsEnemyPauseTDLEnabled",
    # D1/D3 dialogue state
    "StartDialogue", "DialogueEnded", "PrepareForDialogue", "SetUpForDialogue",
    "EndDialogue", "RunningDialogue", "DialogueManager", "PlayerIsTalkingTo",
    # D1/D3 input gating
    "DisableAllPlayerInput", "EnableAllPlayerInput", "SetIgnoreMoveInput",
    "SetIgnoreLookInput", "SetInputMode_UIOnlyEx", "SetInputMode_GameOnly",
    "bShowMouseCursor", "BlockInteract", "ReenableInteract", "DisableInteraction",
    "IsInteractible",
    # auto-pause on unfocus (the other SetGamePaused user)
    "TryAutoPause", "PauseOnUnfocus", "OpenPauseMenu", "CheckForOverlayPause",
    "ForceNoOverlayPause",
    # D7
    "AddToViewport", "AddToPlayerScreen", "RemoveFromParent",
    "WidgetBlueprintGeneratedClass", "HUD", "ShouldHideHUD",
]


def pak_scan(probes=None):
    sys.path.insert(0, PAK_CODE)
    from pakread import Pak, uasset_names            # noqa: E402
    probes = probes or PAK_PROBES
    pk = Pak()
    paths = [p for p in pk.entries
             if p.startswith("QueensDomain/Content/") and p.endswith((".uasset", ".umap"))]
    res = {k: [] for k in probes}
    bad = 0
    for p in paths:
        try:
            n = set(uasset_names(pk.payload(p))["names"])
        except Exception:
            bad += 1
            continue
        for k in probes:
            if k in n:
                res[k].append(p)
    print("=" * 78)
    print("pak name-table scan: %d packages under QueensDomain/Content, %d unparsed"
          % (len(paths), bad))
    print("(instrument: FPackageFileSummary name table; see pakread.uasset_names)")
    print("=" * 78)
    for k in probes:
        print("  %-30s %4d package(s)" % (k, len(res[k])))
    print()
    print("-" * 78)
    print("full lists for the rare (<=10 package) hits - these are the load-bearing ones")
    print("-" * 78)
    for k in probes:
        if 0 < len(res[k]) <= 10:
            print("  %s:" % k)
            for p in res[k]:
                print("      " + p)
    print()
    out = os.path.join(HERE, "pak_name_scan.json")
    with open(out, "w") as f:
        json.dump(res, f, indent=1)
    print("wrote", out)
    return res


def pak_package(relpath):
    """Dump the interesting subset of one package's name table."""
    sys.path.insert(0, PAK_CODE)
    from pakread import Pak, uasset_names            # noqa: E402
    pk = Pak()
    key = [p for p in pk.entries if p.endswith(relpath) and p.startswith("QueensDomain/")]
    if not key:
        print("  not found:", relpath)
        return
    p = key[0]
    n = uasset_names(pk.payload(p))["names"]
    print("== %s  (%d names)" % (p, len(n)))
    pats = [r"dialog", r"talk", r"paus", r"dilat", r"interact", r"widget",
            r"viewport", r"hud", r"common", r"input", r"running"]
    hits = sorted({x for x in n if any(re.search(a, x, re.I) for a in pats)})
    for h in hits:
        print("    ", h)


# The four packages that decide D1 and D3.  Each is dumped as: which of the
# probe names are present, so the reader sees exactly the evidence.
FOCUS_PACKAGES = {
    "DialogueManager (the dialogue UMG widget)":
        "QueensDomain/Content/UI/DialogueSystem/DialogueManager.uasset",
    "DialogueScreen":
        "QueensDomain/Content/UI/DialogueSystem/DialogueScreen.uasset",
    "DialogueOptionEntry":
        "QueensDomain/Content/UI/DialogueSystem/DialogueOptionEntry.uasset",
    "DialogueExitButton":
        "QueensDomain/Content/UI/DialogueSystem/DialogueExitButton.uasset",
    "NPC_Base (the base NPC blueprint - where dialogue starts)":
        "QueensDomain/Content/NPCs/NPC_Base.uasset",
    "FS_PlayerChar (the player - hosts the dialogue state)":
        "QueensDomain/Content/Player/FS_PlayerChar.uasset",
    "PlayerInteractionComponent (the interactor that starts dialogue)":
        "QueensDomain/Content/Player/PlayerInteractionComponent.uasset",
    "FS_HUD (AHUD blueprint - owns the dialogue widget)":
        "QueensDomain/Content/Player/Assets/Input/FS_HUD.uasset",
    "FS_PlayerController_BP":
        "QueensDomain/Content/Player/FS_PlayerController_BP.uasset",
    "EnemyBase (the enemy side of the pause-by-dilation system)":
        "QueensDomain/Content/Enemies/EnemyBase.uasset",
    "TimeDilationManager":
        "QueensDomain/Content/LD45/Blueprints/TimeDilationManager.uasset",
}


def pak_prefix_test():
    """Cooked packages store native class names WITHOUT the U/A prefix.

    This is a trap for anyone reading pak name tables on this title: searching for
    `UFS_DialogueData` finds nothing, `FS_DialogueData` finds 182 packages.  It is
    also what proves `FS_HUD_C`'s parent is the native `AFS_HUD_Base`: the string
    `FS_HUD_Base` occurs in exactly one package, and that package is FS_HUD.
    """
    sys.path.insert(0, PAK_CODE)
    from pakread import Pak, uasset_names        # noqa: E402
    pk = Pak()
    probes = ["UFS_DialogueData", "FS_DialogueData",
              "UFS_DialogueAction", "FS_DialogueAction",
              "AFS_HUD_Base", "FS_HUD_Base",
              "AFS_PlayerCharacter", "FS_PlayerCharacter",
              "AFS_PlayerController", "FS_PlayerController"]
    cnt = {k: 0 for k in probes}
    where = {}
    for p in pk.entries:
        if not (p.startswith("QueensDomain/Content/") and p.endswith((".uasset", ".umap"))):
            continue
        try:
            n = set(uasset_names(pk.payload(p))["names"])
        except Exception:
            continue
        for k in probes:
            if k in n:
                cnt[k] += 1
                where.setdefault(k, []).append(p)
    print("  -- prefix test: cooked packages strip the U/A prefix from class names")
    for k in probes:
        print("     %-24s %4d package(s)%s"
              % (k, cnt[k],
                 ("   e.g. " + ", ".join(x.split("/")[-1] for x in where[k][:3]))
                 if where.get(k) and cnt[k] <= 3 else ""))


def pak_focus():
    sys.path.insert(0, PAK_CODE)
    try:
        from pakread import Pak, uasset_names        # noqa: E402
    except Exception as e:                            # noqa: BLE001
        print("  (pak reader unavailable: %s)" % e)
        return
    probes = ["SetGamePaused", "IsGamePaused", "SetGlobalTimeDilation",
              "SetPauseTimeDilation", "SetPausedEnemyTimeDilation",
              "IsTimeDilationPaused", "CustomTimeDilation", "TryAutoPause",
              "PauseOnUnfocus", "CheckForOverlayPause", "ForceNoOverlayPause",
              "OpenPauseMenu", "SetTickableWhenPaused", "SetActorTickEnabled",
              "DisableAllPlayerInput", "EnableAllPlayerInput", "SetIgnoreMoveInput",
              "SetIgnoreLookInput", "SetInputMode_UIOnlyEx", "SetInputMode_GameOnly",
              "bShowMouseCursor", "RunningDialogue", "PlayerIsTalkingTo",
              "BlockInteract", "ReenableInteract", "DisableInteraction",
              "IsInteractible", "StartDialogue", "EndDialogue", "DialogueEnded",
              "SetUpForDialogue", "PrepareForDialogue", "DialogueManager",
              "AddToViewport", "ReceiveDrawHUD", "WidgetBlueprintGeneratedClass",
              "CallFunc_SetGamePaused_ReturnValue"]
    pk = Pak()
    for label, path in FOCUS_PACKAGES.items():
        if path not in pk.entries:
            print("  == %-56s MISSING" % label)
            continue
        n = set(uasset_names(pk.payload(path))["names"])
        present = [p for p in probes if p in n]
        absent = [p for p in probes if p not in n]
        print("  == %s" % label)
        print("     %s" % path)
        print("     PRESENT: %s" % (", ".join(present) if present else "(none)"))
        print("     absent : %s" % (", ".join(absent) if absent else "(none)"))
    print()
    pak_prefix_test()


# --------------------------------------------------------------------------- #
# class registry dump
# --------------------------------------------------------------------------- #

def classes():
    """All 2599 UClass registrations, filtered to /Script/QueensDomain."""
    print("=" * 78)
    print("QueensDomain UClass registrations found by name-string xref")
    print("=" * 78)
    rows = []
    names = set()
    for m in re.finditer(rb"(?:[\x20-\x7e]\x00){4,64}", PE):
        try:
            s = m.group().decode("utf-16-le")
        except Exception:
            continue
        if re.match(r"^(A|U)FS_[A-Za-z0-9_]+$", s) or re.match(r"^(A|U)QD_[A-Za-z0-9_]+$", s):
            names.add(s)
    for s in sorted(names):
        stub, info = class_registration(s)
        print("  %-40s %s" % (s, info))
    print("  %d class names" % len(names))


# --------------------------------------------------------------------------- #
# raw disassembly of every function a claim in 13-dialogue-ui-answers.md rests on
# --------------------------------------------------------------------------- #

RAW_TARGETS = [
    # D1 sinks
    (0x028F3180, "UGameplayStatics::SetGamePaused  (sink)"),
    (0x028F3410, "UGameplayStatics::SetGlobalTimeDilation  (sink)"),
    (0x028E8DB0, "UGameplayStatics::IsGamePaused  (oracle)"),
    (0x029A43C0, "UWorld::IsPaused  (the cheapest pause oracle)"),
    (0x02D37250, "UWorld::GetWorldSettings"),
    (0x02413EA0, "UGameplayStatics::GetGameInstance (proves UWorld::OwningGameInstance = +0x180)"),
    (0x026636F0, "AActor::SetActorTickEnabled"),
    (0x02CC8110, "AActor::SetIgnoreTimeDilation"),
    # D1 dialogue layer
    (0x00E39500, "UFS_DialogueAction::RunAction  (table 0x03944EF0 copy)"),
    (0x00E2BE50, "UFS_DialogueAction::FinishAction (C++ -> BP via ProcessEvent)"),
    (0x00E34E10, "UFS_DialogueAction::JumpToDialogue (C++ -> BP via ProcessEvent)"),
    (0x00E336E0, "UFS_DialogueAction::InitAction"),
    (0x00EAE0B0, "shared helper (terminal call of several BP thunks)"),
    # D7
    (0x02E7E300, "APlayerController::GetHUD  (MyHUD at +0x2B0)"),
    (0x023F7BB0, "UUserWidget::AddToViewport (tails into UUserWidget vtable +0x300)"),
    (0x023F6C40, "UUserWidget::AddToPlayerScreen (GetOwningLocalPlayer = slot 0x2A0)"),
    (0x02402C00, "UWidgetBlueprintLibrary::Create"),
    (0x02DCE0E0, "AHUD::DrawText"),
    (0x02942160, "AHUD::DrawRect"),
    (0x02958890, "AHUD::Project"),
    (0x00605C20, "AFS_HUD_Base UClass registration stub"),
    (0x00E55B70, "AFS_HUD_Base Z_Construct (via FUN_01041040)"),
]


def raw():
    for addr, label in RAW_TARGETS:
        f = func_at(addr)
        print("=" * 78)
        print("%s  @ %08X   pdata=%s" % (label, addr,
              ("%08X..%08X" % (f[0], f[1])) if f else "NONE"))
        print("=" * 78)
        for ins in disasm(addr, end_rva=(f[1] if f else addr + 0x140)):
            note = ""
            t = insn_target(ins)
            if t is not None and 0x4400000 <= t < 0x4C08000:
                note = "   ; GLOBAL %08X" % t
            if ins.mnemonic in ("call", "jmp") and ins.operands and ins.operands[0].type == 2:
                note = "   ; FUN_%08X" % (ins.operands[0].imm & 0xFFFFFFFF)
            print("  %08X  %-9s %-46s%s" % (ins.address, ins.mnemonic, ins.op_str, note))
        print()


# --------------------------------------------------------------------------- #

if __name__ == "__main__":
    argv = list(sys.argv[1:])
    out_path = None
    if "--out" in argv:
        i = argv.index("--out")
        out_path = argv[i + 1]
        del argv[i:i + 2]
    if out_path:
        sys.stdout = open(out_path, "w", encoding="utf-8", newline="\n")
    cmd = argv[0] if argv else "all"
    if cmd == "d1":
        m = d1()
        d1_body(m)
    elif cmd == "d1graph":
        d1_graph()
    elif cmd == "d3":
        d3()
    elif cmd == "d7":
        d7()
    elif cmd == "pak":
        pak_scan()
    elif cmd == "focus":
        pak_focus()
    elif cmd == "raw":
        raw()
    elif cmd == "classes":
        classes()
    elif cmd == "pkg":
        pak_package(sys.argv[2])
    elif cmd == "all":
        m = d1()
        d1_body(m)
        d1_graph()
        d3()
        d7()
        print("=" * 78)
        print("pak-side evidence")
        print("=" * 78)
        pak_focus()
        pak_scan()
        raw()
    else:
        print(__doc__)
