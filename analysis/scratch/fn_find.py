#!/usr/bin/env python3
"""Function-level hunt: .pdata function table + relaxed signatures + shape match."""
import struct
import capstone
import pefile

QD = (r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64"
      r"\QueensDomain-Win64-Shipping.exe")
REF = (r"G:\Freespace\dollsMP\testgame\Host\DollsNest\Binaries\Win64"
       r"\DollsNest-Win64-Shipping.exe")
X86 = capstone.x86
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True


class Img:
    def __init__(self, p):
        self.p = p
        self.pe = pefile.PE(p)
        self.raw = open(p, "rb").read()
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.secs = [(s.Name.rstrip(b"\0").decode("latin1"), s.VirtualAddress,
                      s.Misc_VirtualSize, s.PointerToRawData, s.SizeOfRawData)
                     for s in self.pe.sections]

    def off(self, rva):
        for n, va, vs, rp, rs in self.secs:
            if va <= rva < va + max(vs, rs) and (rva - va) < rs:
                return rp + (rva - va)
        return None

    def sec(self, rva):
        for n, va, vs, rp, rs in self.secs:
            if va <= rva < va + max(vs, rs):
                return n
        return None

    def read(self, rva, n):
        o = self.off(rva)
        return self.raw[o:o + n] if o is not None else b""

    def text(self):
        for n, va, vs, rp, rs in self.secs:
            if n == ".text":
                return va, self.raw[rp:rp + rs]
        raise SystemExit("no text")


def pdata_functions(img):
    pe = img.pe
    for d in pe.OPTIONAL_HEADER.DATA_DIRECTORY:
        if d.name == "IMAGE_DIRECTORY_ENTRY_EXCEPTION":
            rva, size = d.VirtualAddress, d.Size
            break
    else:
        return []
    blob = img.read(rva, size)
    fns = []
    for i in range(0, len(blob) - 11, 12):
        b, e, u = struct.unpack_from("<III", blob, i)
        if b and e > b:
            fns.append((b, e))
    fns.sort()
    return fns


def insns(img, rva, end):
    code = img.read(rva, end - rva)
    out = []
    for ins in md.disasm(code, img.base + rva):
        out.append(ins)
    return out


qd = Img(QD)
tva, text = qd.text()
fns = pdata_functions(qd)
print("=" * 78)
print("FUNCTION TABLE (.pdata)")
print("=" * 78)
print("functions: %d" % len(fns))
print(".text RVA 0x%X size 0x%X" % (tva, len(text)))
if fns:
    print("first 0x%X .. last 0x%X ; .text end 0x%X"
          % (fns[0][0], fns[-1][1], tva + len(text)))
sizes = [e - b for b, e in fns]
sizes.sort()
print("size median %d, mean %.1f, max %d" % (
    sizes[len(sizes) // 2], sum(sizes) / len(sizes), sizes[-1]))

# ------------------------------------------------------------------ relaxed sig
ref = Img(REF)


def sig(img, rva, length, wild_rel8=True):
    code = img.read(rva, length)
    pat = bytearray(code)
    msk = bytearray(b"\x01" * len(code))
    pos = 0
    for ins in md.disasm(code, rva):
        try:
            rel = (ins.group(capstone.CS_GRP_JUMP)
                   or ins.group(capstone.CS_GRP_CALL))
        except capstone.CsError:
            pos += ins.size
            continue
        dop, dsz = ins.disp_offset, ins.disp_size
        iop, isz = ins.imm_offset, ins.imm_size
        riprel = has_imm = False
        try:
            for op in ins.operands:
                if op.type == X86.X86_OP_MEM and op.mem.base == X86.X86_REG_RIP:
                    riprel = True
                if op.type == X86.X86_OP_IMM:
                    has_imm = True
        except capstone.CsError:
            pass
        if riprel and dsz:
            for k in range(dop, dop + dsz):
                msk[pos + k] = 0
        if rel and has_imm and isz:
            for k in range(iop, iop + isz):
                msk[pos + k] = 0
        pos += ins.size
    return bytes(pat), bytes(msk)


def scan(img, pat, msk):
    tva, text = img.text()
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
    hits, p = [], text.find(needle)
    while p != -1:
        s = p - lo
        if s >= 0 and s + n <= len(text) and all(
                not msk[i] or text[s + i] == pat[i] for i in range(n)):
            hits.append(tva + s)
        p = text.find(needle, p + 1)
    return hits


print()
print("=" * 78)
print("RELAXED SIGNATURES  (rel8 also wildcarded)")
print("=" * 78)
KNOWN = {"FName::ToString": 0x1060990,
         "FName::FName(wchar_t*)": 0x1053D00,
         "StaticConstructObject_Internal": 0x124EDB0,
         "FText::FText(FString&&)": 0xF7CEC0}
for name, rva in KNOWN.items():
    for ln in (48, 96, 160):
        pat, msk = sig(ref, rva, ln)
        hits = scan(qd, pat, msk)
        print("  %-32s len=%-3d fixed=%-3d hits=%d %s"
              % (name, ln, sum(msk), len(hits), ["0x%X" % h for h in hits[:3]]))
    print()

# ------------------------------------------------------- GMalloc shape search
print("=" * 78)
print("GMalloc wrapper shape search over real function starts")
print("=" * 78)
print("  shape: mov rax,[rip+G] / mov rax,[rax] / mov/jmp/call qword [rax+8]")
cands = {}
for b, e in fns:
    if e - b > 96:
        continue
    try:
        ins = insns(qd, b, e)
    except Exception:
        continue
    if len(ins) < 3 or len(ins) > 12:
        continue
    if ins[0].mnemonic != "mov" or "rip" not in ins[0].op_str:
        continue
    tgt = None
    try:
        op = ins[0].operands[1]
        if op.type == X86.X86_OP_MEM and op.mem.base == X86.X86_REG_RIP:
            tgt = ins[0].address + ins[0].size + op.mem.disp - qd.base
    except Exception:
        pass
    if tgt is None:
        continue
    rest = " ".join(i.op_str for i in ins[1:])
    if "[rax + 8]" in rest or "[rax + 0x8]" in rest:
        cands.setdefault(tgt, []).append((b, e, [i.mnemonic + " " + i.op_str for i in ins]))

for g, sites in sorted(cands.items(), key=lambda kv: -len(kv[1])):
    print("  global RVA 0x%X (%s) referenced by %d small function(s)"
          % (g, qd.sec(g), len(sites)))
    for b, e, body in sites[:3]:
        print("     fn RVA 0x%X: %s" % (b, " ; ".join(body)))

# --------------------------------------------------- GUObjectArray shape search
print()
print("=" * 78)
print("FUObjectArray shape search: 'shr reg,0x10' + 'movzx ...,ax' + .data load")
print("=" * 78)
print("  looking for the chunked index idiom inside functions that also load a")
print("  .data global and touch offset +0xC (InternalIndex)")
hits = []
for b, e in fns:
    if e - b > 0x600:
        continue
    try:
        ins = insns(qd, b, e)
    except Exception:
        continue
    txt = []
    has_shr10 = False
    has_movzx_ax = False
    data_globals = []
    has_c0c = False
    for i in ins:
        s = i.mnemonic + " " + i.op_str
        txt.append(s)
        if i.mnemonic == "shr" and "0x10" in i.op_str:
            has_shr10 = True
        if i.mnemonic == "movzx" and i.op_str.endswith(", ax"):
            has_movzx_ax = True
        if "+ 0xc]" in i.op_str:
            has_c0c = True
        if "rip" in i.op_str:
            try:
                for op in i.operands:
                    if op.type == X86.X86_OP_MEM and op.mem.base == X86.X86_REG_RIP:
                        data_globals.append(i.address + i.size + op.mem.disp - qd.base)
            except Exception:
                pass
    if has_shr10 and has_movzx_ax and data_globals:
        hits.append((b, data_globals, has_c0c, txt))
print("  candidate functions: %d" % len(hits))
for b, gs, c0c, txt in hits[:25]:
    dg = [g for g in gs if qd.sec(g) in (".data", "_RDATA", ".bss")]
    print("    fn RVA 0x%X  InternalIndex? %s  data globals %s"
          % (b, c0c, ["0x%X(%s)" % (g, qd.sec(g)) for g in dg[:4]]))
    if c0c and len(hits) <= 25:
        print("      %s" % " ; ".join(txt[:14]))
