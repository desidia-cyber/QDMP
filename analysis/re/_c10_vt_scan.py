"""C10: find functions that call the ULevelStreaming virtuals (+0x270/+0x278)
and that touch the visibility byte +0xBA. These are the engine streaming-update
functions (UWorld::UpdateLevelStreaming and friends)."""
import sys, collections
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
import qdlib as q
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_OP_REG

md = Cs(CS_ARCH_X86, CS_MODE_64); md.detail = True
TEXT = q.sec_by_name(".text")
va, vsz, raw, rawsz = TEXT[1], TEXT[2], TEXT[3], TEXT[4]
code_all = q.data()[raw:raw + rawsz]

hits = collections.defaultdict(list)
for b, e, u in q.functions():
    if b < va or b >= va + vsz:
        continue
    o = q.rva_to_off(b)
    if o is None:
        continue
    ln = min(e - b, va + vsz - b)
    if ln <= 0 or ln > 0x8000:
        continue
    acc = 0
    for ins in md.disasm(code_all[o:o + ln], b):
        if ins.address - b != acc:
            break
        acc += ins.size
        m = ins.mnemonic
        if m.startswith("call"):
            if ins.operands:
                op = ins.operands[0]
                if op.type == X86_OP_MEM and op.mem.disp in (0x270, 0x278, 0x298):
                    hits[(b, op.mem.disp)].append(ins.address)
        # memory operand on +0xba / +0xb8
        for op in ins.operands:
            if op.type == X86_OP_MEM and op.mem.disp == 0xBA:
                hits[(b, 0xBA)].append(ins.address)

print("=== functions calling virtual +0x270/+0x278/+0x298 or touching +0xBA ===")
for (b, disp), sites in sorted(hits.items(), key=lambda kv: -len(kv[1])):
    f = q.func_at(b)
    size = (f[1] - f[0]) if f else 0
    print("  FUN_%08X size=%-6d slot/+off=0x%X sites=%d %s" %
          (b, size, disp, len(sites), " ".join("%08X" % s for s in sites[:8])))
