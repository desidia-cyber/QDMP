"""Find AHUD::DrawHUD's vtable slot.

Approach: locate the AHUD vtable as the run of .text pointers in .rdata that
contains AHUD::DrawText (0x02DCE0E0) and the other AHUD draw helpers. Then, for
each slot, disassemble the target and look for ProcessEvent dispatch
(call 0x11A7AA0 or call qword ptr [reg+0x220]). AHUD's only BlueprintImplementable
event is ReceiveDrawHUD, so the slot whose body dispatches is DrawHUD.
"""
import sys, struct
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
import qdlib as q
from capstone.x86 import X86_OP_MEM, X86_OP_IMM, X86_OP_REG, X86_REG_RIP

DRAWTEXT = 0x02DCE0E0
DRAWRECT = 0x02942160
DRAWLINE = 0x02941C10
PROJECT = 0x02958890
PROCESSEVENT = 0x011A7AA0

starts = {b for b, e, u in q.functions()}
_n, rl, rvsz, rraw, rrawsz, _ch = q.sec_by_name(".rdata")
d = q.data()
blob = d[rraw:rraw + min(rvsz, rrawsz)]

def rva_at(i):
    return rl + i

# collect maximal runs of 8-byte values that are all .text function starts
runs = []
i = 0
n = len(blob) - 8
while i <= n:
    v = struct.unpack_from("<Q", blob, i)[0]
    r = v - q.IMAGE_BASE if v >= q.IMAGE_BASE else None
    if r in starts:
        j = i
        vals = []
        while j <= n:
            w = struct.unpack_from("<Q", blob, j)[0]
            rr = w - q.IMAGE_BASE if w >= q.IMAGE_BASE else None
            if rr not in starts:
                break
            vals.append(rr)
            j += 8
        if len(vals) >= 20:
            runs.append((rva_at(i), vals))
        i = j
    else:
        i += 8

print("vtable-like runs:", len(runs))
cands = [r for r in runs if DRAWTEXT in r[1]]
print("runs containing AHUD::DrawText:", len(cands))

def dispatches(fn):
    hits = []
    for ins in q.disasm(fn, count=200):
        if ins.mnemonic == "call" and ins.operands:
            op = ins.operands[0]
            if op.type == X86_OP_IMM and (op.imm & 0xFFFFFFFF) == PROCESSEVENT:
                hits.append("direct ProcessEvent")
            if op.type == X86_OP_MEM and op.mem.disp == 0x220:
                hits.append("call [reg+0x220]")
    return hits

for base, vals in cands:
    print("\nrun base 0x%08X  slots=%d  DrawText slot=%d" %
          (base, len(vals), vals.index(DRAWTEXT)))
    for k, fn in enumerate(vals):
        h = dispatches(fn)
        if h:
            print("   slot %3d  target 0x%08X  %s" % (k, fn, h))
