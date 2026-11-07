#!/usr/bin/env python3
"""Derive byte signatures from the reference UE4.27 build's known-good RVAs.

Ground truth (UE4SS patternsleuth, ue4ss-injection-4.27.log), DollsNest legacy
Win64-Shipping, image base 0x7ff61f710000:
    GUObjectArray                    0x4A6AA80   (data)
    GMalloc                          0x49E6310   (data)
    FName::ToString                  0x1060990   (code)
    FName::FName(wchar_t*)           0x1053D00   (code)
    StaticConstructObject_Internal   0x124EDB0   (code)
    FText::FText(FString&&)          0xF7CEC0    (code)
"""
import struct
import capstone
import pefile

REF = (r"G:\Freespace\dollsMP\testgame\Host\DollsNest\Binaries\Win64"
       r"\DollsNest-Win64-Shipping.exe")
QD = (r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64"
      r"\QueensDomain-Win64-Shipping.exe")

KNOWN = {
    "FName::ToString": 0x1060990,
    "FName::FName(wchar_t*)": 0x1053D00,
    "StaticConstructObject_Internal": 0x124EDB0,
    "FText::FText(FString&&)": 0xF7CEC0,
}
DATA = {"GUObjectArray": 0x4A6AA80, "GMalloc": 0x49E6310}


class Img:
    def __init__(self, path):
        self.path = path
        self.pe = pefile.PE(path)
        self.raw = open(path, "rb").read()
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.sections = [(s.Name.rstrip(b"\0").decode("latin1"),
                          s.VirtualAddress, s.Misc_VirtualSize,
                          s.PointerToRawData, s.SizeOfRawData,
                          s.Characteristics) for s in self.pe.sections]

    def off(self, rva):
        for n, va, vs, rp, rs, ch in self.sections:
            if va <= rva < va + max(vs, rs) and (rva - va) < rs:
                return rp + (rva - va)
        return None

    def sec(self, rva):
        for n, va, vs, rp, rs, ch in self.sections:
            if va <= rva < va + max(vs, rs):
                return n
        return None

    def read(self, rva, n):
        o = self.off(rva)
        return self.raw[o:o + n] if o is not None else b""

    def rva_of_off(self, o):
        for n, va, vs, rp, rs, ch in self.sections:
            if rp <= o < rp + rs:
                return va + (o - rp)
        return None

    def text(self):
        for n, va, vs, rp, rs, ch in self.sections:
            if n == ".text":
                return va, self.raw[rp:rp + rs], rp, rs
        raise SystemExit("no .text")


md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True
md.skipdata = True

ref = Img(REF)
print("reference:", REF)
print("image base 0x%X  size 0x%X" % (ref.base, ref.pe.OPTIONAL_HEADER.SizeOfImage))
for name, rva in DATA.items():
    print("  %-28s RVA 0x%X  section %s  (from patternsleuth log)"
          % (name, rva, ref.sec(rva)))

print()
print("=" * 78)
print("REFERENCE FUNCTIONS: DISASSEMBLY + GLOBALS TOUCHED")
print("=" * 78)


def rip_targets(img, rva, length):
    code = img.read(rva, length)
    out = []
    for ins in md.disasm(code, img.base + rva):
        try:
            ops = ins.operands
        except capstone.CsError:
            continue
        for op in ops:
            if op.type == capstone.x86.X86_OP_MEM and \
               op.mem.base == capstone.x86.X86_REG_RIP:
                tgt_va = ins.address + ins.size + op.mem.disp
                out.append((ins.address - img.base, ins.mnemonic, ins.op_str,
                            tgt_va - img.base))
    return out


for name, rva in KNOWN.items():
    print("\n### %s  (RVA 0x%X, section %s)" % (name, rva, ref.sec(rva)))
    code = ref.read(rva, 96)
    print("  first 48 bytes:", code[:48].hex(" "))
    for ins in md.disasm(code, ref.base + rva):
        print("    0x%08X  %-20s %s %s"
              % (ins.address - ref.base, ins.bytes.hex(), ins.mnemonic, ins.op_str))
        if ins.address - ref.base > rva + 80:
            break
    print("  -> rip-relative targets:")
    for src, mn, ops, tgt in rip_targets(ref, rva, 96):
        print("     RVA 0x%X  %-6s %-32s -> RVA 0x%X (%s)"
              % (src, mn, ops, tgt, ref.sec(tgt)))

# ---------------------------------------------------------------------------
print()
print("=" * 78)
print("MASKED SIGNATURE DERIVATION + SEARCH IN QUEENSDOMAIN")
print("=" * 78)


def masked_sig(img, rva, length):
    """Bytes with every rel32 branch target and every disp32/immediate 4-byte
    field wildcarded.  Returns (pattern, mask, mnemonic list)."""
    code = img.read(rva, length)
    pat = bytearray(code)
    msk = bytearray(b"\x01" * len(code))
    pos = 0
    for ins in md.disasm(code, rva):
        b = ins.bytes
        try:
            rel = (ins.group(capstone.CS_GRP_JUMP)
                   or ins.group(capstone.CS_GRP_CALL))
        except capstone.CsError:
            pos += ins.size
            continue
        dop, dsz = ins.disp_offset, ins.disp_size
        iop, isz = ins.imm_offset, ins.imm_size
        riprel = False
        has_imm = False
        try:
            for op in ins.operands:
                if op.type == capstone.x86.X86_OP_MEM and \
                   op.mem.base == capstone.x86.X86_REG_RIP:
                    riprel = True
                if op.type == capstone.x86.X86_OP_IMM:
                    has_imm = True
        except capstone.CsError:
            pass
        if riprel and dsz == 4:
            for k in range(dop, dop + 4):
                msk[pos + k] = 0
        if rel and has_imm and isz == 4:
            for k in range(iop, iop + 4):
                msk[pos + k] = 0
        pos += ins.size
    return bytes(pat), bytes(msk)


def scan(img, pat, msk):
    tva, text, _, _ = img.text()
    n = len(pat)
    best = (-1, 0, 0)
    run = 0
    for i in range(n + 1):
        if i < n and msk[i]:
            run += 1
            if run > best[0]:
                best = (run, i - run + 1, i + 1)
        else:
            run = 0
    lo, hi = best[1], best[2]
    needle = pat[lo:hi]
    hits = []
    p = text.find(needle)
    while p != -1:
        s = p - lo
        if s >= 0 and s + n <= len(text) and all(
                not msk[i] or text[s + i] == pat[i] for i in range(n)):
            hits.append(tva + s)
        p = text.find(needle, p + 1)
    return hits


qd = Img(QD)
for name, rva in KNOWN.items():
    for ln in (40, 64, 96):
        pat, msk = masked_sig(ref, rva, ln)
        hits = scan(qd, pat, msk)
        print("  %-32s len=%-3d fixed=%-3d hits=%d %s"
              % (name, ln, sum(msk), len(hits),
                 ["0x%X" % h for h in hits[:4]]))
    print()

print("=" * 78)
print("CHUNKED UOBJECT-INDEX IDIOM  (FUObjectArray::IndexToObject)")
print("=" * 78)
tva, text, _, _ = qd.text()
IDIOMS = {
    "shr r32,0x10 ; and r32,0xFFFF": bytes.fromhex("25ffff0000"),
    "movsxd/shr/mov": bytes.fromhex("c1e810"),
    "shr ecx,0x10": bytes.fromhex("c1e910"),
    "shr edx,0x10": bytes.fromhex("c1ea10"),
    "shr r8d,0x10": bytes.fromhex("41c1e810"),
}
for label, needle in IDIOMS.items():
    idx = []
    p = text.find(needle)
    while p != -1 and len(idx) < 4000:
        idx.append(p)
        p = text.find(needle, p + 1)
    print("  %-32s occurrences: %d" % (label, len(idx)))

# The classic: mov eax,[rcx+0xC] (InternalIndex) ... shr/and -> chunk lookup
print()
print("  'mov r32,[r64+0xC]' followed within 24B by 'and r32,0xFFFF':")
cnt = 0
p = text.find(bytes.fromhex("25ffff0000"))
while p != -1:
    back = text[max(0, p - 24):p]
    if bytes.fromhex("c1e8") in back or bytes.fromhex("c1e9") in back:
        cnt += 1
        if cnt <= 15:
            print("    RVA 0x%X  ctx %s" % (tva + p, text[p - 16:p + 8].hex(" ")))
    p = text.find(bytes.fromhex("25ffff0000"), p + 1)
print("  total with shr before and: %d" % cnt)
