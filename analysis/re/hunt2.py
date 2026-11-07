import sys, os, marshal
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
from hunt import *
from capstone.x86 import X86_OP_MEM, X86_OP_REG, X86_REG_RIP

_calls = None

def calls():
    global _calls
    if _calls is None:
        with open(r"G:\Freespace\Queens_Domain\analysis\re\calls.bin", "rb") as f:
            _calls = marshal.load(f)
    return _calls

def callers(fn):
    return [(s, f, m) for s, f, m in calls().get(fn & 0xFFFFFFFF, [])]

def insn_at(rva):
    f = func_at(rva)
    if not f:
        return None
    for ins in disasm(f[0], end_rva=min(rva + 15, f[1])):
        if ins.address == rva:
            return ins
        if ins.address > rva:
            return None
    return None

def is_write(ins, global_rva):
    """True if this instruction stores to [rip+global]."""
    if not ins.operands:
        return False
    dst = ins.operands[0]
    if dst.type != X86_OP_MEM or dst.mem.base != X86_REG_RIP:
        return False
    tgt = (ins.address + ins.size + dst.mem.disp) & 0xFFFFFFFF
    if tgt != (global_rva & 0xFFFFFFFF):
        return False
    # access type not exposed; treat mov/and/or/xor/add/sub with mem dst as write
    return True

def writers_of(global_rva):
    out = []
    for site, func, mn in refs_to(global_rva):
        ins = insn_at(site)
        if ins is None:
            continue
        if is_write(ins, global_rva):
            out.append((site, func, ins.mnemonic + " " + ins.op_str))
    return out

def summarize(fn, maxn=40):
    """Compact: calls + data writes + strings touched."""
    f = func_at(fn)
    if not f:
        print("  not a function")
        return
    end = f[1]
    DL = sec_by_name(".data")[1]; DH = DL + sec_by_name(".data")[2]
    RL = sec_by_name(".rdata")[1]; RH = RL + sec_by_name(".rdata")[2]
    print("--- FUN_%08X [%08X..%08X] 0x%X bytes ---" % (fn, fn, end, end - fn))
    n = 0
    for ins, t in func_scan(fn, end):
        if t is not None:
            if DL <= t < DH and (is_write(ins, t) and len(ins.operands) > 1 and
                                 ins.operands[1].type != X86_OP_MEM):
                print("   %08X  WRITE %08X  %s %s" % (ins.address, t, ins.mnemonic, ins.op_str))
                n += 1
        elif ins.mnemonic.startswith("call") and ins.operands and ins.operands[0].type == 2:
            tg = ins.operands[0].imm & 0xFFFFFFFF
            if 0x1000 <= tg < 0x373B000:
                print("   %08X  call FUN_%08X" % (ins.address, tg))
                n += 1
        if n > maxn:
            print("   ...")
            break
