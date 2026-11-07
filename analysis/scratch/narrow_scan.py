#!/usr/bin/env python3
"""Narrow scan: FUObjectArray chunk-table accessor, GName pool, GMalloc, and
context for the 'EAC' false-positive check."""
import struct
import re
import capstone
import pefile

QD = (r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64"
      r"\QueensDomain-Win64-Shipping.exe")
X86 = capstone.x86
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True

img = pefile.PE(QD)
raw = open(QD, "rb").read()
base = img.OPTIONAL_HEADER.ImageBase
secs = [(s.Name.rstrip(b"\0").decode("latin1"), s.VirtualAddress,
         s.Misc_VirtualSize, s.PointerToRawData, s.SizeOfRawData)
        for s in img.sections]


def off(rva):
    for n, va, vs, rp, rs in secs:
        if va <= rva < va + max(vs, rs) and (rva - va) < rs:
            return rp + (rva - va)
    return None


def sec(rva):
    for n, va, vs, rp, rs in secs:
        if va <= rva < va + max(vs, rs):
            return n
    return None


def read(rva, n):
    o = off(rva)
    return raw[o:o + n] if o is not None else b""


for n, va, vs, rp, rs in secs:
    if n == ".text":
        TEXT_RVA, TEXT = va, raw[rp:rp + rs]

# ---------------------------------------------------------------- .pdata
for d in img.OPTIONAL_HEADER.DATA_DIRECTORY:
    if d.name == "IMAGE_DIRECTORY_ENTRY_EXCEPTION":
        pdat = read(d.VirtualAddress, d.Size)
fns = []
for i in range(0, len(pdat) - 11, 12):
    b, e, u = struct.unpack_from("<III", pdat, i)
    if b and e > b:
        fns.append((b, e))
fns.sort()
print("functions:", len(fns))


def disas(rva, end):
    out = []
    try:
        for ins in md.disasm(read(rva, end - rva), base + rva):
            out.append(ins)
    except Exception:
        pass
    return out


def dataglobals(ins):
    gs = []
    try:
        for op in ins.operands:
            if op.type == X86.X86_OP_MEM and op.mem.base == X86.X86_REG_RIP:
                gs.append(ins.address + ins.size + op.mem.disp - base)
    except capstone.CsError:
        pass
    return gs


print()
print("=" * 78)
print("FUObjectArray chunk-table accessor candidates")
print("=" * 78)
print("  requires, inside one function: rip-load of a .data global G, then")
print("  'shr r,0x10', then an 8-byte scaled chunk load [r + r*8]")
hits = []
for b, e in fns:
    if e - b > 0x400 or e - b < 16:
        continue
    ins = disas(b, e)
    if len(ins) < 4:
        continue
    gs = []
    shr_at = -1
    chunk = -1
    for k, i in enumerate(ins):
        gs += [g for g in dataglobals(i) if sec(g) == ".data"]
        if i.mnemonic == "shr" and "0x10" in i.op_str and shr_at < 0:
            shr_at = k
        if i.mnemonic == "mov" and i.op_str.count("* 8") and "[r" in i.op_str \
           and chunk < 0:
            chunk = k
    if shr_at >= 0 and chunk > shr_at and gs:
        hits.append((b, e, sorted(set(gs)), ins))
print("  candidates: %d" % len(hits))
for b, e, gs, ins in hits[:20]:
    print("    fn RVA 0x%-8X size %-5d globals %s" % (
        b, e - b, ["0x%X" % g for g in gs[:5]]))
    print("      %s" % " ; ".join(i.mnemonic + " " + i.op_str for i in ins[:16]))

print()
print("=" * 78)
print("DISASSEMBLY OF GName CANDIDATES (near FName::FName @0xFABF20)")
print("=" * 78)
for rva in (0xFAAB80, 0xFAAD60, 0xFB0720, 0xFB08B0):
    print("\n### fn RVA 0x%X" % rva)
    for i in disas(rva, rva + 120):
        print("   0x%08X %-20s %s %s" % (i.address - base, i.bytes.hex(),
                                         i.mnemonic, i.op_str))
        for g in dataglobals(i):
            print("        ; -> RVA 0x%X (%s)" % (g, sec(g)))

print()
print("=" * 78)
print("'EAC' OCCURRENCES (context check)")
print("=" * 78)
for enc, label in ((b"EAC", "ansi"), ("EAC".encode("utf-16-le"), "wide")):
    for m in re.finditer(re.escape(enc), raw):
        o = m.start()
        ctx = raw[max(0, o - 30):o + 30]
        s = "".join(chr(c) if 32 <= c < 127 else "." for c in ctx)
        print("  %-5s 0x%X  %r" % (label, o, s))

print()
print("=" * 78)
print("NOTABLE IMPORTED NAMES (steam / net / drm)")
print("=" * 78)
IMPS = ["SteamAPI_Init", "SteamAPI_Shutdown", "SteamInternal", "SteamAPI_RestartAppIfNecessary",
        "SteamNetworking", "SteamMatchmaking", "SteamUser", "SteamApps",
        "SteamInput", "SteamUtils", "SteamFriends", "SteamRemoteStorage",
        "SteamClient", "SteamGameServer"]
for d in getattr(img, "DIRECTORY_ENTRY_IMPORT", []) + \
        getattr(img, "DIRECTORY_ENTRY_DELAY_IMPORT", []):
    names = [i.name.decode("latin1") for i in d.imports if i.name]
    keep = [n for n in names if any(k.lower() in n.lower() for k in IMPS)]
    if keep or d.dll.lower() in (b"steam_api64.dll",):
        print("  %-28s %s" % (d.dll.decode("latin1"), keep or names[:20]))
