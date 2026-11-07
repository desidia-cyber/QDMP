"""Build a rip-relative xref index over .text using .pdata function bounds.

Output: xrefs.bin  (marshal)  ->  dict{ target_rva : [ (site_rva, func_rva, kind), ... ] }
kind: 'lea' | 'mov' | 'cmp' | 'other'
"""
import sys, os, marshal, time
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
from qdlib import *
from capstone.x86 import X86_OP_MEM, X86_REG_RIP

OUT = r"G:\Freespace\Queens_Domain\analysis\re\xrefs.bin"


def build():
    d = data()
    sec = sec_by_name(".text")
    name, va, vsz, raw, rawsz, ch = sec
    text_end = va + vsz
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True

    funcs = functions()
    idx = {}
    n_ok = 0
    n_bad = 0
    t0 = time.time()
    for i, (b, e, u) in enumerate(funcs):
        if b < va or b >= text_end:
            continue
        o = rva_to_off(b)
        if o is None:
            continue
        ln = min(e - b, text_end - b)
        if ln <= 0 or ln > 0x40000:
            continue
        code = d[o:o + ln]
        # disassemble whole function; validate full coverage
        acc = 0
        sites = []
        for ins in md.disasm(code, b):
            if ins.address - b != acc:
                break
            acc += ins.size
            for op in ins.operands:
                if op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP:
                    t = (ins.address + ins.size + op.mem.disp) & 0xFFFFFFFF
                    sites.append((ins.address, b, ins.mnemonic))
                    break
        if acc >= ln - 16:      # covered to (near) the end
            n_ok += 1
        else:
            n_bad += 1
        for s in sites:
            idx.setdefault(s[0 + 2 - 2] if False else None, None)  # noop
        for site, fb, mn in sites:
            # target recompute (cheap; stored above implicitly)
            pass
        if i % 20000 == 0:
            print("  %d/%d  %.0fs  ok=%d bad=%d  targets=%d"
                  % (i, len(funcs), time.time() - t0, n_ok, n_bad, len(idx)),
                  flush=True)
    return idx


def build_fast():
    """Single pass, correct implementation."""
    d = data()
    sec = sec_by_name(".text")
    name, va, vsz, raw, rawsz, ch = sec
    text_end = va + vsz
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True

    funcs = functions()
    idx = {}
    n_ok = 0
    n_bad = 0
    t0 = time.time()
    for i, (b, e, u) in enumerate(funcs):
        if b < va or b >= text_end:
            continue
        o = rva_to_off(b)
        if o is None:
            continue
        ln = min(e - b, text_end - b)
        if ln <= 0 or ln > 0x40000:
            continue
        code = d[o:o + ln]
        acc = 0
        for ins in md.disasm(code, b):
            if ins.address - b != acc:
                break
            acc += ins.size
            tgt = None
            for op in ins.operands:
                if op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP:
                    tgt = (ins.address + ins.size + op.mem.disp) & 0xFFFFFFFF
                    break
            if tgt is not None:
                idx.setdefault(tgt, []).append((ins.address, b, ins.mnemonic))
        if acc >= ln - 16:
            n_ok += 1
        else:
            n_bad += 1
        if i % 20000 == 0:
            print("  %d/%d  %.0fs  ok=%d bad=%d targets=%d"
                  % (i, len(funcs), time.time() - t0, n_ok, n_bad, len(idx)),
                  flush=True)
    print("done: ok=%d bad=%d targets=%d  %.1fs" % (n_ok, n_bad, len(idx), time.time() - t0))
    return idx


if __name__ == "__main__":
    ix = build_fast()
    with open(OUT, "wb") as f:
        marshal.dump(ix, f)
    print("wrote", OUT, os.path.getsize(OUT))
