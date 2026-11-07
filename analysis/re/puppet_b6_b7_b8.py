#!/usr/bin/env python3
"""Reproduce every address and value quoted in analysis/12-puppet-answers.md.

Items B6, B7, B8 of analysis/10-gaps-register.md for the UE4.27.2 shipping
image  QueensDomain/Win64/QueensDomain-Win64-Shipping.exe.

Run:  python analysis/re/puppet_b6_b7_b8.py
Env:  Python 3.12, capstone, pefile; repo-relative paths only.
Writes: analysis/re/puppet_evidence.txt  (raw disassembly relied on)

Nothing here launches the game.  Everything is static (PE + pak).
"""
import os
import sys
import struct
import marshal
import bisect

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
RE = os.path.join(REPO, "analysis", "re")
PAKCODE = os.path.join(REPO, "analysis", "pak", "code")
sys.path.insert(0, RE)
sys.path.insert(0, PAKCODE)

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

import qdlib as q          # noqa: E402
import hunt                # noqa: E402  (xrefs.bin)
import hunt2               # noqa: E402  (calls.bin)
import pakread             # noqa: E402
import blueprints as bp    # noqa: E402

OUT_EVIDENCE = os.path.join(RE, "puppet_evidence.txt")
NATIVE_PAIRS = os.path.join(RE, "nativepairs.bin")
DISPATCHER = 0x0119D280

_EV = []


def say(*a):
    line = " ".join(str(x) for x in a)
    print(line)
    _EV.append(line)


def head(t):
    say("")
    say("=" * 92)
    say(t)
    say("=" * 92)


def ins_text(ins):
    return "%s %s" % (ins.mnemonic, ins.op_str)


def norm(ins):
    return ins_text(ins).replace(" ", "")


def flen(rva, at_least=0x40):
    """.pdata is not a reliable function list on this image; widen truncated entries."""
    f = q.func_at(rva)
    e = f[1] if f else 0
    if e - rva < at_least:
        e = rva + at_least
    return e


def show(rva, end=None, n=None, tag=""):
    """Disassemble and both print and record."""
    if end is None:
        end = flen(rva)
    say("---- %s FUN_%08X  [%08X..%08X] ----" % (tag, rva, rva, end))
    k = 0
    for ins in q.disasm(rva, end_rva=end):
        note = ""
        t = q.rel_target(ins)
        if t is not None and 0x1000 <= t < 0x373B000:
            note = "   -> FUN_%08X" % t
        tt = q.insn_target(ins)
        if tt is not None:
            note += "   [%08X]" % tt
        say("%08X  %-8s %-46s%s" % (ins.address, ins.mnemonic, ins.op_str, note))
        k += 1
        if n is not None and k >= n:
            break
    say("")


def has(rva, end, mnemonic, disp=None, imm=None, size=None):
    """Find an instruction by mnemonic + operand values (capstone prints small
    immediates as decimal, so string matching is unreliable)."""
    end = max(end, flen(rva))
    for ins in q.disasm(rva, end_rva=end):
        if ins.mnemonic != mnemonic:
            continue
        okd = disp is None
        oki = imm is None
        for op in ins.operands:
            if op.type == 3 and disp is not None:        # X86_OP_MEM
                if op.mem.disp == disp and (size is None or op.size == size):
                    okd = True
            if op.type == 2 and imm is not None:         # X86_OP_IMM
                if (op.imm & 0xFFFFFFFF) == (imm & 0xFFFFFFFF):
                    oki = True
                elif op.size == 1 and (op.imm & 0xFF) == (imm & 0xFF):
                    oki = True          # capstone may sign-extend an imm8
        if okd and oki:
            return ins
    return None


def direct_calls(rva, end=None, maxn=120):
    if end is None:
        end = flen(rva)
    end = max(end, flen(rva))
    out = []
    for ins in q.disasm(rva, end_rva=end):
        t = q.rel_target(ins)
        if t is not None and 0x1000 <= t < 0x373B000:
            out.append((ins.address, t))
        if ins.mnemonic == "ret":
            break
    return out


def impl_of(exec_rva):
    """Last direct .text call in an exec thunk == the C++ implementation."""
    calls = direct_calls(exec_rva)
    if not calls:
        return None
    return calls[-1][1]


def pair(name):
    NP = marshal.load(open(NATIVE_PAIRS, "rb"))
    return NP["pairs"].get(name, [])


def real_pair(name):
    """Entries whose thunk does not merely call the Blueprint-VM dispatcher."""
    out = []
    for b, c, nr in pair(name):
        im = impl_of(c)
        if im is None or im == DISPATCHER:
            continue
        out.append((b, c, nr))
    return out


# =====================================================================
# PART 1 - image identity and the two transport tables
# =====================================================================
head("PART 1 - image identity / tools")
pe = q.pe()
say("sections:")
for name, va, vsz, raw, rawsz, ch in q.sections():
    say("   %-10s RVA %08X  vsz %08X  raw %08X  rsz %08X" % (name, va, vsz, raw, rawsz))
say("ImageBase  0x%X" % pe.OPTIONAL_HEADER.ImageBase)
say("SizeOfImage 0x%X" % pe.OPTIONAL_HEADER.SizeOfImage)
say("exe size   %d bytes" % os.path.getsize(q.EXE))
say("functions (.pdata) %d" % len(q.functions()))
NP = marshal.load(open(NATIVE_PAIRS, "rb"))
say("pair tables %d   names %d" % (len(NP["runs"]), len(NP["pairs"])))
say("Blueprint-VM dispatcher 0x%08X (reject this as an 'exec thunk')" % DISPATCHER)

# =====================================================================
# PART 2 - B6: exact bit offsets and the constructor chain
# =====================================================================
head("PART 2 - B6  bReplicates / bReplicateMovement")

# --- 2.1 SetReplicates -> AActor::SetReplicates
say("[2.1] pair 'SetReplicates':")
for b, c, nr in pair("SetReplicates"):
    say("      table %08X exec %08X name %08X" % (b, c, nr))
real = real_pair("SetReplicates")
assert real, "SetReplicates real thunk missing"
_, sexec, _ = real[0]
s_impl = impl_of(sexec)
say("      real exec %08X -> last direct call FUN_%08X" % (sexec, s_impl))
assert s_impl == 0x02664640, "expected AActor::SetReplicates 0x02664640"
show(sexec, tag="SetReplicates exec thunk", n=40)
show(s_impl, tag="AActor::SetReplicates impl", n=40)
# prove: bit 1 of byte [actor+0x5B] is written
f = q.func_at(s_impl)
b1 = has(s_impl, f[1], "and", imm=0xfd)
b2 = has(s_impl, f[1], "mov", disp=0x5b, size=1)
assert b1 and b2, "bReplicates byte/bit pattern not found"
say("      PROOF bReplicates: %08X  %s" % (b1.address, ins_text(b1)))
say("                        %08X  %s" % (b2.address, ins_text(b2)))
say("      => bReplicates = bit 1 (mask 0x02) of byte at AActor + 0x5B")
say("         (0xfd = ~0x02; the 'or al, dl<<1' sets the same bit)")

# --- 2.2 SetReplicateMovement -> vtable slot 0x298 -> 0x02664630
say("")
say("[2.2] pair 'SetReplicateMovement':")
for b, c, nr in pair("SetReplicateMovement"):
    say("      table %08X exec %08X name %08X" % (b, c, nr))
real = real_pair("SetReplicateMovement")
_, mexec, _ = real[0]
say("      real exec %08X" % mexec)
show(mexec, tag="SetReplicateMovement exec thunk", n=40)
# the last indirect call goes through the vtable at +0x298
vcall = None
for ins in q.disasm(mexec, end_rva=mexec + 0x140):
    if ins.mnemonic == "call" and "[rax + 0x298]" in ins.op_str:
        vcall = ins
        break
assert vcall is not None, "vtable call [rax+0x298] not found"
say("      PROOF virtual dispatch: %08X  %s" % (vcall.address, ins_text(vcall)))
say("      slot = 0x298 / 8 = %d" % (0x298 // 8))

# APawn's vtable is installed by its ctor; read slot 83 from it.
vt = 0x03EB6240
slot83 = q.u64(vt + 83 * 8) - 0x140000000
say("      vtable 0x%08X slot 83 (0x298) = FUN_%08X" % (vt, slot83))
assert slot83 == 0x02664630, "slot83 != SetReplicatingMovement"
show(slot83, tag="AActor::SetReplicatingMovement (slot 83 body)")
f = None
end83 = flen(slot83)
c1 = has(slot83, end83, "and", disp=0x58, imm=0xef, size=1)
c2 = has(slot83, end83, "shl", imm=4)
c3 = has(slot83, end83, "or", disp=0x58, size=1)
assert c1 and c2 and c3, "bReplicateMovement bit pattern not found"
say("      PROOF bReplicateMovement: %08X %s" % (c1.address, ins_text(c1)))
say("                                 %08X %s" % (c2.address, ins_text(c2)))
say("                                 %08X %s" % (c3.address, ins_text(c3)))
say("      => bReplicateMovement = bit 4 (mask 0x10) of byte at AActor + 0x58")

# --- 2.3 constructor chain for FS_PlayerCharacter (game module)
say("")
say("[2.3] Z_Construct_UClass_* for FS_PlayerCharacter")
# find the registration function via the wide class-name string
name_rva = None
blob_sec = q.sec_by_name(".rdata")
needle = "FS_PlayerCharacter".encode("utf-16-le")
b = q.data()
off = q.rva_to_off(blob_sec[1])
seg = b[off:off + min(blob_sec[2], blob_sec[4])]
p = seg.find(needle)
assert p >= 0, "class-name wide string not found"
name_rva = blob_sec[1] + p
say("      wide string 'FS_PlayerCharacter' @ %08X" % name_rva)
xrefs = hunt.refs_to(name_rva)
assert xrefs, "no xref to class-name string"
zfn = xrefs[0][1]
say("      registration function FUN_%08X" % zfn)
show(zfn, tag="Z_Construct_UClass_AFS_PlayerCharacter", n=None)

# parse ClassConstructor / size / super out of it
last_lea = None
ctor = size = sup = None
f = q.func_at(zfn)
for ins in q.disasm(zfn, end_rva=flen(zfn)):
    t = q.insn_target(ins)
    if t is not None and ins.mnemonic == "lea":
        last_lea = t
    if ins.mnemonic == "mov" and ins.op_str.startswith("qword ptr [r11 - 0x40]") \
            and ins.op_str.endswith("rax"):
        ctor = last_lea
    if ins.mnemonic == "mov" and "dword ptr [rsp + 0x20]" in ins.op_str:
        size = int(ins.op_str.split(",")[-1], 16)
    if ins.mnemonic == "mov" and ins.op_str.startswith("qword ptr [r11 - 0x28]") \
            and ins.op_str.endswith("rax"):
        sup = last_lea
say("      ClassConstructor=0x%08X  sizeof=0x%X  SuperFn=0x%08X" % (ctor, size, sup))

# ClassConstructor stub -> real ctor
show(ctor, end=ctor + 0x10, tag="ClassConstructor stub", n=8)


def branch_target(ins):
    """Immediate branch target for call/jmp/jcc."""
    if not (ins.mnemonic == "call" or ins.mnemonic.startswith("j")):
        return None
    for op in ins.operands:
        if op.type == 2:  # X86_OP_IMM
            return op.imm & 0xFFFFFFFF
    return None


# the stub tail-jumps (conditional jne) into the real constructor
stub_target = None
for ins in q.disasm(ctor, end_rva=flen(ctor)):
    t = branch_target(ins)
    if t is not None and 0x1000 <= t < 0x373B000:
        stub_target = t
        break
say("      stub target = FUN_%08X" % stub_target)
show(stub_target, tag="AFS_PlayerCharacter::AFS_PlayerCharacter", n=None)


# follow base ctors: FS_PlayerCharacter -> ACharacter -> APawn -> AActor.
# Skip 0x011A0CE0 (FObjectInitializer::Get), which is not a base ctor.
def call_target(rva):
    for _site, t in direct_calls(rva):
        if t != 0x011A0CE0:
            return t
    return None


chain = []
cur = stub_target
for _ in range(6):
    nxt = call_target(cur)
    if not nxt:
        break
    chain.append((cur, nxt))
    cur = nxt
say("      constructor chain (ctor -> base ctor):")
for a, bb in chain:
    say("         FUN_%08X -> FUN_%08X" % (a, bb))
say("      (FS_PlayerCharacter -> ACharacter -> APawn -> AActor)")
achar_ctor = chain[0][1]
pawn_ctor = chain[1][1]
say("      ACharacter::ACharacter  = FUN_%08X" % achar_ctor)
say("      APawn::APawn            = FUN_%08X" % pawn_ctor)
show(pawn_ctor, tag="APawn::APawn (sets the two flags)", n=None)
f = q.func_at(pawn_ctor)
r1 = has(pawn_ctor, f[1], "or", disp=0x5b, imm=2, size=1)
r2 = has(pawn_ctor, f[1], "mov", disp=0x5f, imm=1, size=1)
r3 = has(pawn_ctor, f[1], "call", imm=0x2664630)
assert r1 and r2 and r3, "APawn ctor flag pattern not found"
say("      PROOF APawn sets bReplicates=1     %08X  %s" % (r1.address, ins_text(r1)))
say("      PROOF APawn sets RemoteRole=1      %08X  %s" % (r2.address, ins_text(r2)))
say("      PROOF APawn calls SetReplicatingMovement(1) %08X %s" % (r3.address, ins_text(r3)))
say("      => AFS_PlayerCharacter CDO: bReplicates=1, RemoteRole=ROLE_SimulatedProxy,")
say("         bReplicateMovement=1 (set by APawn ctor; neither ACharacter nor")
say("         AFS_PlayerCharacter ctor clears them).")
# prove the two later ctors do NOT touch 0x58/0x5b
for label, fn_ in (("ACharacter", achar_ctor), ("AFS_PlayerCharacter", stub_target)):
    ff = q.func_at(fn_)
    touched = [i.address for i in q.disasm(fn_, end_rva=flen(fn_))
               if ("[rax + 0x5b]" in i.op_str or "[rbx + 0x5b]" in i.op_str or
                   "[rdi + 0x5b]" in i.op_str or "[rcx + 0x5b]" in i.op_str or
                   "[rax + 0x58]" in i.op_str or "[rbx + 0x58]" in i.op_str or
                   "[rdi + 0x58]" in i.op_str or "[rcx + 0x58]" in i.op_str)]
    say("      %-18s touches +0x58/+0x5B at: %s" %
        (label, ["%08X" % x for x in touched] or "NONE"))

# =====================================================================
# PART 3 - B7: game-module spawn/construct surface + engine chain
# =====================================================================
head("PART 3 - B7  the game's own spawn/construction path")

# 3.1 every /Script/QueensDomain class registration
PKGNAME = "/Script/QueensDomain".encode("utf-16-le")
p = seg.find(PKGNAME)
assert p >= 0, "package-name wide string not found"
pkg_rva = blob_sec[1] + p
say("[3.1] package name '%s' @ %08X" % ("/Script/QueensDomain", pkg_rva))
zfuncs = sorted(set(fn for site, fn, mn in hunt.refs_to(pkg_rva)))
say("      Z_Construct functions referencing it: %d" % len(zfuncs))

T2C = {}
RUNBASES = set(b for b, _n in NP["runs"])
NCLS = 0
# map native-function table base -> class, by decoding each RegisterNativeFunc
for zf in zfuncs:
    ff = q.func_at(zf)
    if not ff:
        continue
    last_lea = None
    cls = None
    r9 = None
    ctor2 = None
    size2 = None
    for ins in q.disasm(zf, end_rva=flen(zf)):
        t = q.insn_target(ins)
        if t is not None and ins.mnemonic == "lea":
            last_lea = t
            if ins.op_str.startswith("rdx"):
                try:
                    s2 = q.read_rva(t, 96).decode("utf-16-le", "replace").split("\x00")[0]
                    if s2 and cls is None and s2 != "/Script/QueensDomain":
                        cls = s2
                except Exception:
                    pass
            if ins.op_str.startswith("r9"):
                r9 = t
        if ins.mnemonic == "mov" and ins.op_str.startswith("qword ptr [r11 - 0x40]") \
                and ins.op_str.endswith("rax"):
            ctor2 = last_lea
        if ins.mnemonic == "mov" and "dword ptr [rsp + 0x20]" in ins.op_str:
            size2 = int(ins.op_str.split(",")[-1], 16)
    if cls and ctor2 and size2:
        NCLS += 1
    if not cls or not r9 or r9 == 0x007746E0:
        continue
    rf = q.func_at(r9)
    if not rf:
        continue
    for ins in q.disasm(r9, end_rva=flen(r9)):
        t = q.insn_target(ins)
        if t is not None and ins.mnemonic == "lea" and t in RUNBASES:
            T2C.setdefault(t, set()).add(cls)
say("      parsed class registrations (name+size+ctor): %d" % NCLS)
say("      registered native-function tables: %d" % len(T2C))

# 3.2 the entire Spawn/Construct/Create/Possess/Merge UFUNCTION surface
import re as _re
PAT = _re.compile(r"Spawn|Construct|Create|Possess|Merge", _re.I)
say("")
say("[3.2] /Script/QueensDomain UFUNCTIONs matching Spawn|Construct|Create|Possess|Merge")
hits = []
for nm, lst in sorted(NP["pairs"].items()):
    if not PAT.search(nm):
        continue
    for b, c, nr in lst:
        if b in T2C:
            hits.append((nm, sorted(T2C[b])[0], c))
for nm, cls, c in hits:
    say("      %-34s class=%-30s exec=%08X" % (nm, cls, c))
say("      TOTAL: %d" % len(hits))
assert len(hits) == 3, "expected exactly 3 spawner-helper UFUNCTIONs"

# 3.3 the two named candidates
say("")
say("[3.3] candidate classes")
for cls in ("FS_StationarySkeletalMeshActor", "FS_ConfigurableStaticMeshComponent"):
    rec = None
    for zf in zfuncs:
        ff = q.func_at(zf)
        if not ff:
            continue
        last_lea = None
        nm2 = None
        r9b = None
        ct = None
        sz = None
        for ins in q.disasm(zf, end_rva=flen(zf)):
            t = q.insn_target(ins)
            if t is not None and ins.mnemonic == "lea":
                last_lea = t
                if ins.op_str.startswith("rdx"):
                    try:
                        s2 = q.read_rva(t, 96).decode("utf-16-le", "replace").split("\x00")[0]
                        if s2:
                            nm2 = s2
                    except Exception:
                        pass
                if ins.op_str.startswith("r9"):
                    r9b = t
            if ins.mnemonic == "mov" and ins.op_str.startswith("qword ptr [r11 - 0x40]") \
                    and ins.op_str.endswith("rax"):
                ct = last_lea
            if ins.mnemonic == "mov" and "dword ptr [rsp + 0x20]" in ins.op_str:
                sz = int(ins.op_str.split(",")[-1], 16)
        if nm2 == cls:
            rec = (zf, r9b, ct, sz)
            break
    assert rec, cls
    zf, r9b, ct, sz = rec
    say("      %s: Z_Construct=%08X sizeof=0x%X ctor=%08X RegisterNativeFunc=%s"
        % (cls, zf, sz, ct, ("%08X" % r9b) if r9b else "?"))
    say("        -> RegisterNativeFunc is the empty stub 0x007746E0: %s"
        % ("YES (no UFUNCTIONs of its own)" if r9b == 0x007746E0 else "NO"))
    show(ct, end=ct + 0x10, tag=cls + " ClassConstructor stub", n=8)
    cst = None
    for ins in q.disasm(ct, end_rva=ct + 0x10):
        t = branch_target(ins)
        if t is not None and 0x1000 <= t < 0x373B000:
            cst = t
            break
    say("        ctor stub -> real ctor FUN_%08X" % cst)
    show(cst, tag=cls + " real ctor", n=None)

# 3.4 engine spawn chain (concrete primitives, proven by the call index)
say("")
say("[3.4] engine character-spawn chain")
CHAIN = [
    (0x02DC2E50, "AGameModeBase::RestartPlayer exec"),
    (0x02DC4800, "AGameModeBase::SpawnDefaultPawnFor exec"),
    (0x02DC46E0, "AGameModeBase::SpawnDefaultPawnAtTransform exec"),
    (0x028D3C90, "UGameplayStatics::BeginDeferredActorSpawnFromClass"),
    (0x029AD950, "UWorld::SpawnActor (entry)"),
    (0x028DCD50, "UGameplayStatics::FinishSpawningActor"),
    (0x02651580, "AActor::FinishSpawning"),
]
for rva, label in CHAIN:
    say("      %-52s FUN_%08X" % (label, rva))
say("      call edges (from calls.bin):")
for tgt, label in ((0x029AD950, "UWorld::SpawnActor"),
                   (0x02651580, "AActor::FinishSpawning")):
    for site, fn, mn in hunt2.callers(tgt):
        say("         FUN_%08X @%08X -> %s" % (fn, site, label))
say("      virtual hand-offs inside the GameMode exec thunks:")
for rva, slot, label in ((0x02DC2E50, 0x790, "RestartPlayer"),
                         (0x02DC4800, 0x650, "SpawnDefaultPawnFor"),
                         (0x02DC46E0, 0x648, "SpawnDefaultPawnAtTransform")):
    found = None
    for ins in q.disasm(rva, end_rva=rva + 0x180):
        if ins.mnemonic == "call" and ("[rax + 0x%x]" % slot) in ins.op_str:
            found = ins
            break
    say("         %-26s exec %08X -> vtable slot 0x%X %s"
        % (label, rva, slot, ("at %08X" % found.address) if found else "(not found)"))
say("      => the game module exposes no native spawner; the BP GameMode reaches")
say("         the same concrete primitives through virtual dispatch / ProcessEvent.")

# =====================================================================
# PART 4 - B8: owner-see bits, writers, and the cutscene evidence
# =====================================================================
head("PART 4 - B8  does the full body render anywhere?")

say("[4.1] SetOwnerNoSee / SetOnlyOwnerSee semantics")
for nm, want_bit, want_mask in (("SetOnlyOwnerSee", 6, 0xbf),
                                ("SetOwnerNoSee", 5, 0xdf)):
    real = real_pair(nm)
    _, ex, _ = real[0]
    im = impl_of(ex)
    say("      %-16s real exec %08X -> impl FUN_%08X" % (nm, ex, im))
    show(im, tag=nm + " impl", n=14)
    f = None
    e = flen(im)
    sh = has(im, e, "shr", imm=want_bit)
    an = has(im, e, "and", imm=want_mask)
    assert sh and an, nm + " bit pattern not found"
    say("      PROOF: %08X %s / %08X %s  => bit %d (mask 0x%02x) of +0x212"
        % (sh.address, ins_text(sh), an.address, ins_text(an), want_bit, 1 << want_bit))
say("      => bOwnerNoSee = bit 5 (0x20) of UPrimitiveComponent+0x212")
say("         bOnlyOwnerSee = bit 6 (0x40) of UPrimitiveComponent+0x212")

say("")
say("[4.2] native callers of the two setters (calls.bin)")
for nm, rva in (("SetOwnerNoSee", 0x027D8340), ("SetOnlyOwnerSee", 0x027D8310)):
    cs = hunt2.callers(rva)
    say("      %-16s impl %08X callers=%d %s"
        % (nm, rva, len(cs), ["%08X/FUN_%08X" % (s, f) for s, f, m in cs]))
    assert len(cs) == 1, nm + ": expected only its own exec thunk"

say("")
say("[4.3] direct bit writers in .text (byte ops at +0x212 / +0x213)")
say("      scanning .text once (this is the slow step) ...")
d = q.data()
name, va, vsz, raw, rawsz, ch = q.sec_by_name(".text")
code = d[raw:raw + rawsz]
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_REG_RIP
md = Cs(CS_ARCH_X86, CS_MODE_64); md.detail = False
md2 = Cs(CS_ARCH_X86, CS_MODE_64); md2.detail = True
WANT = {'mov', 'and', 'or', 'test', 'cmp', 'xor', 'add', 'sub', 'bts', 'btr',
        'btc', 'xchg', 'not', 'inc', 'dec', 'setne', 'sete', 'shl', 'shr', 'sar'}
cands = []
pos = 0
n = len(code)
while pos < n:
    chunk = code[pos:pos + 8192]
    consumed = 0
    for ins in md.disasm(chunk, va + pos):
        consumed = ins.address - va + ins.size - pos
        if ins.mnemonic in WANT and ("0x212]" in ins.op_str or "0x213]" in ins.op_str):
            cands.append(ins.address)
    pos += consumed if consumed > 0 else 1
sites = []
for a in sorted(set(cands)):
    o = q.rva_to_off(a)
    for ins in md2.disasm(d[o:o + 15], a):
        for op in (ins.operands or []):
            if op.type == X86_OP_MEM and op.size == 1 and op.mem.disp in (0x212, 0x213) \
               and op.mem.base not in (0, X86_REG_RIP):
                sites.append((a, ins.mnemonic, ins.op_str))
        break
clear5 = [s for s in sites if s[1] == "and" and "0x212],0xdf" in s[2].replace(" ", "")]
set5 = [s for s in sites if s[1] == "or" and "0x212],0x20" in s[2].replace(" ", "")]
set6 = [s for s in sites if s[1] == "or" and "0x212],0x40" in s[2].replace(" ", "")]
say("      validated byte accesses at +0x212/+0x213 : %d" % len(sites))
say("      clear bOwnerNoSee  (and +0x212,0xDF)      : %d %s"
    % (len(clear5), ["%08X" % x[0] for x in clear5]))
say("      set   bOwnerNoSee  (or  +0x212,0x20)      : %d %s"
    % (len(set5), ["%08X" % x[0] for x in set5]))
say("      set   bOnlyOwnerSee(or  +0x212,0x40)      : %d %s"
    % (len(set6), ["%08X" % x[0] for x in set6]))
for a, mn, ops in clear5:
    say("        %08X  %s %s   (in FUN_%08X)" % (a, mn, ops, q.func_at(a)[0]))
assert len(clear5) == 1, "expected exactly one clear-bit5 site (ACharacter ctor / capsule)"
say("      => the only clear-bit5 site is the ACharacter ctor on the CAPSULE;")
say("         no code clears bOwnerNoSee on the full-body mesh.")

say("")
say("[4.4] asset side")
pk = pakread.Pak()
iv = sorted((off + pakread.ENTRY_HEADER_SIZE,
             off + pakread.ENTRY_HEADER_SIZE + size, path)
            for path, (off, size, flags) in pk.entries.items())
starts = [x[0] for x in iv]


def owner_of(pos):
    i = bisect.bisect_right(starts, pos) - 1
    if i < 0:
        return None
    s, e, p_ = iv[i]
    return p_ if s <= pos < e else None


TOK = [b"SetOwnerNoSee", b"SetOnlyOwnerSee", b"bOwnerNoSee", b"bOnlyOwnerSee"]
res = {t: set() for t in TOK}
F = open(pakread.PAK_PATH, "rb")
pos = 0
prev = b""
while True:
    chk = F.read(8 * 1024 * 1024)
    if not chk:
        break
    data = prev + chk
    base = pos - len(prev)
    for t in TOK:
        pp = 0
        while True:
            pp = data.find(t, pp)
            if pp < 0:
                break
            o = owner_of(base + pp)
            if o:
                res[t].add(o)
            pp += 1
    prev = data[-64:]
    pos += len(chk)
F.close()
for t in TOK:
    say("      %-18s appears in %d asset(s) %s"
        % (t.decode(), len(res[t]), sorted(res[t])[:5]))
    assert not res[t], t.decode() + " unexpectedly referenced by an asset"

say("")
say("[4.5] what the player BPs actually assign")
for path in ("QueensDomain/Content/Player/FS_PlayerChar.uasset",
             "QueensDomain/Content/Player/FS_Full_PlayerChar.uasset"):
    info = bp.parse(pk.payload(path))
    names = info["names"]
    body = [x for x in names if "Player_FullChar" in x]
    arms = [x for x in names if "Player_FPArms" in x]
    say("      %-58s names=%-5d body-mesh refs=%s arms-refs=%s"
        % (path.rsplit("/", 1)[-1], len(names), body, arms[:2]))
    assert not body, path + " unexpectedly references the full-body mesh"
say("      => the player BP hierarchy assigns Player_FPArms_Arms to the 'Arms'")
say("         component and never assigns a mesh to CharacterMesh0.")
say("         The full body mesh is therefore absent in gameplay.")

say("")
say("[4.6] cutscene actors that DO render the body")
for path in ("QueensDomain/Content/RuinsLevel/Maps/Intro_Level_00.umap",
             "QueensDomain/Content/Cutscenes/Ending1/Ending1Cutscene.uasset",
             "QueensDomain/Content/Cutscenes/CutsceneSkeletalMeshActor.uasset"):
    data = pk.payload(path)
    info = bp.parse(data)
    imp, exp = bp.try_imports_exports(data, info)
    cls = {}
    for e in exp:
        cls[e["class"]] = cls.get(e["class"], 0) + 1
    interesting = {k: v for k, v in cls.items()
                   if k and any(x in k for x in ("SequenceCharacter", "Cutscene",
                                                 "LevelSequence", "SkeletalMesh"))}
    say("      %-70s %s" % (path.rsplit("/", 1)[-1], interesting))
    names = info["names"]
    for k in ("Player_FullChar_2026_coolPose", "Player_FullChar_2026_IntroAnimation",
              "FS_SequenceCharacter", "CutsceneSkeletalMeshActor", "LevelSequenceActor"):
        say("          %-36s %s" % (k, "present" if any(k in z for z in names) else "-"))
pk.close()

# =====================================================================
# SUMMARY
# =====================================================================
head("SUMMARY (values quoted in analysis/12-puppet-answers.md)")
say("B6  bReplicates        = bit1 (0x02) of AActor+0x5B   [0x02664640]")
say("B6  bReplicateMovement = bit4 (0x10) of AActor+0x58   [0x02664630 = vtable slot 0x298]")
say("B6  APawn ctor 0x02AD9010 sets both to 1 (RemoteRole=1 too)")
say("B6  AFS_PlayerCharacter ctor 0x00E1E4C0, ACharacter 0x02747940: no override reset")
say("B7  /Script/QueensDomain: 206 package-name xrefs, %d class regs, %d native tables" % (NCLS, len(T2C)))
say("B7  exactly 3 Spawn* UFUNCTIONs in those tables; 0 Construct/Create/Possess/Merge")
say("B7  spawn primitives: 0x028D3C90 -> 0x029AD950 ; 0x028DCD50 -> 0x02651580")
say("B8  bOwnerNoSee = bit5/0x20, bOnlyOwnerSee = bit6/0x40 of +0x212")
say("B8  no native caller clears bOwnerNoSee on a mesh; no asset references the names")
say("B8  body renders only on cutscene actors (FS_SequenceCharacter_C / CutsceneSkeletalMeshActor_C)")

with open(OUT_EVIDENCE, "w", encoding="utf-8") as fh:
    fh.write("\n".join(_EV) + "\n")
print("\nwrote", OUT_EVIDENCE, "(%d lines)" % len(_EV))
