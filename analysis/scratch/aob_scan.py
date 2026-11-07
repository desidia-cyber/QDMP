#!/usr/bin/env python3
"""UE4.27 global/signature hunt in the QueensDomain shipping image.

Two independent techniques, reported separately so neither is overread:
  A. name presence  -- projected NAME strings (ANSI + UTF-16) in the image
  B. AOB scan       -- masked byte patterns for the classic UE4.25-4.27 anchors
"""
import re
import struct
import sys
import pefile

PATH = r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64\QueensDomain-Win64-Shipping.exe"
raw = open(PATH, "rb").read()
pe = pefile.PE(PATH)
BASE = pe.OPTIONAL_HEADER.ImageBase


# ---------------------------------------------------------------- PE mapping
def rva_of_off(off):
    for s in pe.sections:
        rp = s.PointerToRawData
        if rp <= off < rp + s.SizeOfRawData:
            return s.VirtualAddress + (off - rp)
    return None


def sec_of_rva(rva):
    for s in pe.sections:
        va, vs = s.VirtualAddress, max(s.Misc_VirtualSize, s.SizeOfRawData)
        if va <= rva < va + vs:
            return s.Name.rstrip(b"\0").decode("latin1")
    return None


def text_bounds():
    for s in pe.sections:
        if s.Name.rstrip(b"\0") == b".text":
            return s.VirtualAddress, s.PointerToRawData, s.SizeOfRawData
    raise SystemExit("no .text")


TEXT_RVA, TEXT_OFF, TEXT_SIZE = text_bounds()
TEXT = raw[TEXT_OFF:TEXT_OFF + TEXT_SIZE]


def decode_sig(text):
    pat, msk = [], []
    for tok in text.replace(" ", "").split("/"):
        if not tok:
            continue
        if tok == "??":
            pat.append(0)
            msk.append(0)
        else:
            pat.append(int(tok, 16))
            msk.append(0xFF)
    return bytes(pat), bytes(msk)


def aob(data, pat, msk):
    n = len(pat)
    anchored = None
    best = -1
    # anchor on the longest fixed run
    run = 0
    for i in range(n + 1):
        if i < n and msk[i]:
            run += 1
            if run > best:
                best = run
                anchored = (i - run + 1, i + 1)
        else:
            run = 0
    if anchored is None:
        return []
    lo, hi = anchored
    needle = pat[lo:hi]
    out = []
    p = data.find(needle)
    while p != -1:
        start = p - lo
        if start >= 0 and start + n <= len(data):
            ok = True
            for i in range(n):
                if msk[i] and data[start + i] != pat[i]:
                    ok = False
                    break
            if ok:
                out.append(start)
        p = data.find(needle, p + 1)
    return out


def disasm(data, off, length=48, va=0):
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    nd = 0
    lines = []
    for ins in md.disasm(data[off:off + length], va):
        lines.append("      0x%08X  %-22s %s %s"
                     % (ins.address, ins.bytes.hex(), ins.mnemonic, ins.op_str))
        nd += ins.size
        if nd >= length:
            break
    return "\n".join(lines)


def report(name, pat, msk, note=""):
    hits = aob(TEXT, pat, msk)
    print("\n--- %s   (%d byte pattern, %d fixed)" %
          (name, len(pat), sum(1 for m in msk if m)))
    print("    %s" % " ".join("??" if not m else "%02X" % b for b, m in zip(pat, msk)))
    if note:
        print("    note: %s" % note)
    print("    hits in .text: %d" % len(hits))
    for h in hits[:8]:
        rva = TEXT_RVA + h
        print("      RVA 0x%X  (file 0x%X)  section %s" % (rva, TEXT_OFF + h, sec_of_rva(rva)))
    return hits


print("=" * 78)
print("A. NAME PRESENCE (projected names in image)")
print("=" * 78)
NAMES = [
    "ProcessEvent", "ProcessInternal", "CallFunction", "StaticConstructObject",
    "StaticConstructObject_Internal", "GNatives", "GMalloc", "FMemory",
    "FName", "GName", "FUObjectArray", "GObjectArray", "GUObjectArray",
    "FNamePool", "FNameEntry", "TNameEntryArray", "UObject", "UClass",
    "UFunction", "FNativeFunctionRegistrar", "GetPrivateStaticClass",
    "StaticClass", "FObjectInitializer", "FMemory::Malloc", "Malloc",
    "GIsServer", "GIsClient", "GIsEditor", "FPlatformMisc",
    "SkeletalMeshComponent", "CharacterMovementComponent",
    "PlayerController", "PlayerCameraManager", "ACharacter",
    "GameInstance", "GameViewportClient", "LocalPlayer",
]
for n in NAMES:
    a = [m.start() for m in re.finditer(re.escape(n.encode("latin1")), raw)]
    w = [m.start() for m in re.finditer(re.escape(n.encode("utf-16-le")), raw)]
    print("  %-32s ansi=%-5d wide=%-5d" % (n, len(a), len(w)))

print()
print("=" * 78)
print("B. AOB PATTERNS")
print("=" * 78)

# 1) GUObjectArray, UE4SS 4.27 signature.  The load we want is `4C 8B 05` at
#    match+0x1A; GUObjectArray = (match+0x1A+7) + disp32 - 0x10.
goa = report(
    "GUObjectArray (UE4SS 4.27)",
    *decode_sig("8B/51/04/85/D2/74/5A/48/63/01/85/C0/78/53/44/8B/??/??/??/??/??/"
                "41/3B/C0/7D/47/4C/8B"),
    note="derivation: GOA = (match+0x1A+7)+disp32-0x10")
for h in goa:
    mov = h + 0x1A
    disp = struct.unpack_from("<i", TEXT, mov + 3)[0]
    tgt_rva = TEXT_RVA + mov + 7 + disp
    print("      -> mov r8,[rip+%+#x] at RVA 0x%X ; target RVA 0x%X (section %s)"
          % (disp, TEXT_RVA + mov, tgt_rva, sec_of_rva(tgt_rva)))
    print("         GUObjectArray = RVA 0x%X (section %s)"
          % (tgt_rva - 0x10, sec_of_rva(tgt_rva - 0x10)))
    print(disasm(TEXT, h - 16, 80, TEXT_RVA + h - 16))

# 2) FUObjectArray index accessor used by Dumper-7-ish scanners
report("GUObjectArray alt (mov rax,[rip]; cmp dword [rax+0x??],...)",
       *decode_sig("48/8B/05/??/??/??/??/8B/48/0C/85/C9/7E"))

# 3) UObject::ProcessEvent prologue, UE4.25-4.27 x64
pe_hits = report(
    "UObject::ProcessEvent prologue (4.25-4.27)",
    *decode_sig("40/55/56/57/41/54/41/55/41/56/41/57/48/8D/AC/24/??/??/??/??/"
                "48/81/EC/??/??/??/00"),
    note="push rbp;rsi;rdi;r12-r15; lea rbp,[rsp-x]; sub rsp,imm32")
for h in pe_hits[:4]:
    print(disasm(TEXT, h, 160, TEXT_RVA + h))

# 4) UObject::ProcessEvent alternative (no rbp frame variant)
report("ProcessEvent alt prologue (sub rsp,0x?? then GMalloc load)",
       *decode_sig("48/89/5C/24/??/55/56/57/41/54/41/55/41/56/41/57/48/8D/AC/24"))

# 5) FMemory::Malloc -> mov rax,[rip+GMalloc] ; mov r8,... ; jmp [rax+..]
report("GMalloc load (mov reg,[rip] then call [reg+0x8])",
       *decode_sig("48/8B/05/??/??/??/??/48/8B/08/48/85/C9"))

# 6) StaticConstructObject_Internal prologue
report("StaticConstructObject_Internal (4.27)",
       *decode_sig("48/89/5C/24/??/55/56/57/41/54/41/55/41/56/41/57/48/81/EC"))

# 7) FName::ToString -- 4.27 uses FNameEntry; the GetPlainNameString path
report("FName pool access (GName + 0x?? style)",
       *decode_sig("48/8B/0D/??/??/??/??/8B/C2/25/FF/FF/00/00"))

# 8) UObject vtable: locate a vtable whose slot 68 points into .text and whose
#    ProcessEvent-shaped function is there.  Done separately below.
print()
print("=" * 78)
print("C. SCAN OF .text FOR ProcessEvent-LIKE FUNCTION USING GMALLOC")
print("=" * 78)
print("    (heuristic: functions that load a global FMalloc* then call [rax+0x8])")
gm = aob(TEXT, *decode_sig("48/8B/05/??/??/??/??/48/8B/08"))
print("    mov rax,[rip+GMalloc]; mov rcx,[rax] candidates: %d" % len(gm))
for h in gm[:20]:
    print("      RVA 0x%X -> target RVA 0x%X (%s)" % (
        TEXT_RVA + h,
        TEXT_RVA + h + 7 + struct.unpack_from("<i", TEXT, h + 3)[0],
        sec_of_rva(TEXT_RVA + h + 7 + struct.unpack_from("<i", TEXT, h + 3)[0])))
