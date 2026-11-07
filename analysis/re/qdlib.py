"""Static-analysis helper library for QueensDomain-Win64-Shipping.exe (UE4.27).

Pure pefile + capstone. No debugger, no Ghidra.
"""
import os
import struct
import bisect
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64, CS_OP_IMM, CS_OP_MEM
from capstone.x86 import X86_OP_MEM, X86_OP_IMM, X86_REG_RIP

EXE = r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64\QueensDomain-Win64-Shipping.exe"
IMAGE_BASE = 0x140000000

_pe = None
_data = None
_sections = []          # list of (name, va, vsize, raw_off, raw_size, chars)
_funcs = None           # sorted list of (begin_rva, end_rva, unwind_rva)
_func_starts = None


def _load():
    global _pe, _data, _sections
    if _pe is not None:
        return
    if not os.path.exists(EXE):
        raise SystemExit("EXE not found: " + EXE)
    _pe = pefile.PE(EXE, fast_load=True)
    _pe.parse_data_directories()
    with open(EXE, "rb") as f:
        _data = f.read()
    _sections = []
    for s in _pe.sections:
        name = s.Name.rstrip(b"\x00").decode("latin-1")
        _sections.append((name, s.VirtualAddress, s.Misc_VirtualSize,
                          s.PointerToRawData, s.SizeOfRawData, s.Characteristics))


def data() -> bytes:
    _load()
    return _data


def pe():
    _load()
    return _pe


def sections():
    _load()
    return _sections


def sec_by_name(name):
    for s in sections():
        if s[0] == name:
            return s
    return None


def rva_to_off(rva):
    _load()
    for name, va, vsz, raw, rawsz, ch in _sections:
        if va <= rva < va + max(vsz, rawsz):
            if rva - va < rawsz:
                return raw + (rva - va)
    return None


def off_to_rva(off):
    _load()
    for name, va, vsz, raw, rawsz, ch in _sections:
        if raw <= off < raw + rawsz:
            return va + (off - raw)
    return None


def read_rva(rva, n):
    _load()
    o = rva_to_off(rva)
    if o is None:
        return None
    return _data[o:o + n]


def u32(rva):
    b = read_rva(rva, 4)
    return struct.unpack("<I", b)[0] if b and len(b) == 4 else None


def u64(rva):
    b = read_rva(rva, 8)
    return struct.unpack("<Q", b)[0] if b and len(b) == 8 else None


def ptr(rva):
    v = u64(rva)
    if v is None:
        return None
    return v - IMAGE_BASE if IMAGE_BASE <= v < IMAGE_BASE + 0x10000000 else None


# ---------------------------------------------------------------- .pdata

def functions():
    """.pdata RUNTIME_FUNCTION table -> sorted [(begin,end,unwind)] rvas."""
    global _funcs, _func_starts
    if _funcs is not None:
        return _funcs
    d = data()
    s = sec_by_name(".pdata")
    name, va, vsz, raw, rawsz, ch = s
    n = min(vsz, rawsz) // 12
    out = []
    base = raw
    for i in range(n):
        o = base + i * 12
        b, e, u = struct.unpack("<III", d[o:o + 12])
        if b == 0 and e == 0:
            continue
        if e <= b:
            continue
        out.append((b, e, u))
    out.sort()
    _funcs = out
    _func_starts = [x[0] for x in out]
    return _funcs


def func_at(rva):
    """Return (begin,end,unwind) for the .pdata function containing rva, or None."""
    functions()
    i = bisect.bisect_right(_func_starts, rva) - 1
    if i < 0:
        return None
    b, e, u = _funcs[i]
    if b <= rva < e:
        return (b, e, u)
    return None


def is_func_start(rva):
    functions()
    i = bisect.bisect_left(_func_starts, rva)
    return i < len(_func_starts) and _func_starts[i] == rva


def func_name(rva):
    f = func_at(rva)
    return ("FUN_%08x" % f[0]) if f else None


# ---------------------------------------------------------------- capstone

def disasm(rva, count=0, end_rva=None, max_insns=20000):
    d = data()
    o = rva_to_off(rva)
    if o is None:
        return []
    if end_rva is None:
        f = func_at(rva)
        end_rva = f[1] if f else rva + 0x400
    length = min(max(end_rva - rva, 1), 0x8000)
    code = d[o:o + length]
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True
    out = []
    for ins in md.disasm(code, rva):
        out.append(ins)
        if count and len(out) >= count:
            break
        if len(out) >= max_insns:
            break
    return out


def insn_target(ins):
    """For a rip-relative lea/mov/cmp: absolute RVA of the referenced operand."""
    for op in ins.operands:
        if op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP:
            return (ins.address + ins.size + op.mem.disp) & 0xFFFFFFFF
    return None


def insn_imm(ins):
    for op in ins.operands:
        if op.type == X86_OP_IMM:
            return op.imm
    return None


def rel_target(ins):
    """Branch target for a relative call/jmp."""
    m = ins.mnemonic
    if m.startswith("call") or m.startswith("jmp"):
        for op in ins.operands:
            if op.type == X86_OP_IMM:
                return op.imm & 0xFFFFFFFF
    return None


def func_scan(rva, end=None):
    """Disassemble a function; yield (ins, referenced_rva_or_None)."""
    f = func_at(rva)
    if end is None:
        end = f[1] if f else rva + 0x800
    for ins in disasm(rva, end_rva=end):
        yield ins, insn_target(ins)


# ---------------------------------------------------------------- xref scan

def build_rip_xref_index(exclude_secs=()):
    """Scan .text for every rip-relative operand and index target_rva -> [sites].

    Cheap heuristic: only decode instructions, walking linearly over .text.
    ~58 MB of code. Acceptable one-time cost.
    """
    d = data()
    s = sec_by_name(".text")
    name, va, vsz, raw, rawsz, ch = s
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True
    idx = {}
    code = d[raw:raw + rawsz]
    cur = 0
    n = len(code)
    while cur < n:
        addr = va + cur
        got = False
        for ins in md.disasm(code[cur:cur + 16], addr):
            got = True
            t = insn_target(ins)
            if t is not None:
                idx.setdefault(t, []).append((addr, ins.mnemonic))
            cur += ins.size
            break
        if not got:
            cur += 1
    return idx


def find_xrefs(target_rva, idx):
    return idx.get(target_rva & 0xFFFFFFFF, [])


# ---------------------------------------------------------------- strings

def iter_strings(minlen=4, secs=(".rdata", "_RDATA", ".data")):
    d = data()
    for name, va, vsz, raw, rawsz, ch in sections():
        if name not in secs:
            continue
        blob = d[raw:raw + min(vsz, rawsz)]
        cur = []
        start = 0
        for i, b in enumerate(blob):
            if 0x20 <= b < 0x7f:
                if not cur:
                    start = i
                cur.append(chr(b))
            else:
                if len(cur) >= minlen:
                    yield (va + start, "".join(cur))
                cur = []
        if len(cur) >= minlen:
            yield (va + start, "".join(cur))


def find_string(s, exact=False, limit=50):
    res = []
    for rva, text in iter_strings(minlen=4):
        if (text == s) if exact else (s in text):
            res.append((rva, text))
            if len(res) >= limit:
                break
    return res


def hexs(rva, n=16):
    b = read_rva(rva, n)
    if b is None:
        return "<unmapped>"
    return " ".join("%02X" % x for x in b)


if __name__ == "__main__":
    print("sections:")
    for s in sections():
        print("  %-10s RVA %08X vsz %08X raw %08X rsz %08X" % (s[0], s[1], s[2], s[3], s[4]))
    print("functions:", len(functions()))
    print(".text sample disasm at first func:", functions()[0])
