"""Find every instruction that writes a chosen struct displacement in .text.

Purpose: locate the fields that decide whether the world clock advances.
Candidates and why:
  0x5B0  UWorld::DeltaTimeSeconds   (float)  -- proven by GetWorldDeltaSeconds 0x028E1CE0
  0x5A4  UWorld::UnpausedTimeSeconds(float)  -- GetUnpausedTimeSeconds 0x028E1AF0
  0x5A8  UWorld::RealTimeSeconds    (float)  -- GetRealTimeSeconds 0x028E0850
  0x2E8  AWorldSettings time dilation (float) -- GetGlobalTimeDilation 0x028DF810

This is a full linear sweep of .text bounded by .pdata, same iteration the
existing build_calls.py uses. Writes are classified by operand role.
"""
import os
import sys
import struct
import json

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qdlib as q
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_OP_REG, X86_OP_IMM

DISPS = {0x5B0: "UWorld::DeltaTimeSeconds", 0x5A4: "UWorld::UnpausedTimeSeconds",
         0x5A8: "UWorld::RealTimeSeconds", 0x2E8: "AWorldSettings::TimeDilation"}

WRITE_MNEM = ("mov", "movss", "movsd", "movups", "movaps", "movdqu", "movdqa",
              "vmovss", "vmovups", "vmovaps", "and", "or", "xor", "add", "sub",
              "inc", "dec", "not", "neg", "shl", "shr", "imul", "movzx", "movsx")


def is_mem_disp0(ins, disp):
    if not ins.operands:
        return None
    op = ins.operands[0]
    if op.type != X86_OP_MEM:
        return None
    if op.mem.disp != disp:
        return None
    if op.mem.base == 0:          # absolute, not a struct field
        return None
    return op


def scan():
    d = q.data()
    _name, va, vsz, raw, rawsz, ch = q.sec_by_name(".text")
    text_end = va + vsz
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True
    hits = []
    funcs = q.functions()
    for i, (b, e, u) in enumerate(funcs):
        if b < va or b >= text_end:
            continue
        o = q.rva_to_off(b)
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
            if ins.mnemonic not in WRITE_MNEM:
                continue
            for frame in WRITE_MNEM:
                pass
            for disp, label in DISPS.items():
                op = is_mem_disp0(ins, disp)
                if op is None:
                    continue
                # confirm it is really this displacement, not a scaled index
                if op.mem.index != 0 and op.mem.scale != 1:
                    continue
                hits.append({"site": ins.address, "func": b, "disp": disp,
                             "label": label, "text": "%s %s" % (ins.mnemonic, ins.op_str),
                             "mnemonic": ins.mnemonic})
                break
        if i % 40000 == 0:
            print("  scanned %d/%d funcs  hits=%d" % (i, len(funcs), len(hits)), flush=True)
    return hits


def main():
    hits = scan()
    print("\ntotal writes: %d\n" % len(hits))
    by = {}
    for h in hits:
        by.setdefault((h["disp"], h["func"]), []).append(h)
    for (disp, func), hs in sorted(by.items(), key=lambda kv: (kv[0][0], kv[0][1])):
        f = q.func_at(func)
        size = (f[1] - f[0]) if f else -1
        print("%s  0x%X  FUN_%08X (size 0x%X)  %d write(s)"
              % (hs[0]["label"], disp, func, size, len(hs)))
        for h in hs:
            print("      %08X  %s" % (h["site"], h["text"]))
    with open(os.path.join(HERE, "_field_writes.json"), "w") as fh:
        json.dump(hits, fh, indent=1)
    print("\nwrote _field_writes.json")


if __name__ == "__main__":
    main()
