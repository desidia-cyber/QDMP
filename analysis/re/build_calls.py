"""Direct-call (E8 rel32) and tail-jump (E9 rel32) xref index, .pdata bounded."""
import sys, os, marshal, time
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
from qdlib import *
from capstone.x86 import X86_OP_IMM

OUT = r"G:\Freespace\Queens_Domain\analysis\re\calls.bin"

def build():
    d = data()
    sec = sec_by_name(".text")
    va, vsz, raw, rawsz = sec[1], sec[2], sec[3], sec[4]
    text_end = va + vsz
    md = Cs(CS_ARCH_X86, CS_MODE_64); md.detail = True
    idx = {}
    t0 = time.time()
    for i, (b, e, u) in enumerate(functions()):
        if b < va or b >= text_end: continue
        o = rva_to_off(b)
        if o is None: continue
        ln = min(e - b, text_end - b)
        if ln <= 0 or ln > 0x40000: continue
        code = d[o:o + ln]
        acc = 0
        for ins in md.disasm(code, b):
            if ins.address - b != acc: break
            acc += ins.size
            m = ins.mnemonic
            if m == "call" or m == "jmp":
                for op in ins.operands:
                    if op.type == X86_OP_IMM:
                        t = op.imm & 0xFFFFFFFF
                        idx.setdefault(t, []).append((ins.address, b, m))
                        break
        if i % 50000 == 0:
            print("  %d  %.0fs targets=%d" % (i, time.time()-t0, len(idx)), flush=True)
    return idx

if __name__ == "__main__":
    ix = build()
    with open(OUT, "wb") as f:
        marshal.dump(ix, f)
    print("targets:", len(ix), "size:", os.path.getsize(OUT))
