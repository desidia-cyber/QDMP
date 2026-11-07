"""E5 evidence probe: does QueensDomain enforce a single instance?

Reproduces, from the shipped image only:
  1. every mutex/event/semaphore/mapping/window import and its IAT slot,
  2. every call site of those imports inside .text,
  3. the string (or the register/stack expression) passed as the object NAME,
  4. the mutex handle global and the first-instance flag global, with every
     verified reader of the flag, so "detected" can be separated from "refused".

Instruments: pefile (import table) + capstone (linear decode).  The call-site
scan is a raw byte scan for `FF 15`/`FF 25` + rel32, so it sees every indirect
call in the section rather than only the ones a recursive walk would follow.

Run:  python analysis/re/ops_probe.py [--json out.json]
"""
import argparse
import json
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from capstone.x86 import X86_OP_REG
from qdlib import (IMAGE_BASE, data, disasm, func_at, insn_target, iter_strings,
                   pe, read_rva, rva_to_off, sec_by_name, sections)

# API -> which argument carries the object name, in Win64 fastcall terms
TRACKED = [
    ("KERNEL32.dll", "CreateMutexW", "r8"),
    ("KERNEL32.dll", "CreateMutexExW", "stack5"),
    ("KERNEL32.dll", "OpenMutexW", "r8"),
    ("KERNEL32.dll", "CreateEventW", "r9"),
    ("KERNEL32.dll", "CreateEventExW", "stack5"),
    ("KERNEL32.dll", "OpenEventW", "r8"),
    ("KERNEL32.dll", "CreateSemaphoreA", "r9"),
    ("KERNEL32.dll", "CreateSemaphoreW", "r9"),
    ("KERNEL32.dll", "OpenSemaphoreW", "r8"),
    ("KERNEL32.dll", "CreateFileMappingW", "stack6"),
    ("KERNEL32.dll", "OpenFileMappingW", "r8"),
    ("KERNEL32.dll", "CreateNamedPipeW", "r8"),
    ("KERNEL32.dll", "CreateToolhelp32Snapshot", None),
    ("KERNEL32.dll", "CreateProcessW", None),
    ("KERNEL32.dll", "TerminateProcess", None),
    ("KERNEL32.dll", "ExitProcess", None),
    ("KERNEL32.dll", "WaitForSingleObject", None),
    ("KERNEL32.dll", "ReleaseMutex", None),
    ("KERNEL32.dll", "CloseHandle", None),
    ("KERNEL32.dll", "GetLastError", None),
    ("USER32.dll", "FindWindowW", "r8"),
    ("USER32.dll", "FindWindowExW", None),
]

ALIAS = {}
for _i in range(16):
    for _s in ("", "d", "w", "b"):
        ALIAS["r%d%s" % (_i, _s)] = "r%d" % _i
for _wide, _root in (("rax", "rax"), ("eax", "rax"), ("ax", "rax"), ("al", "rax"),
                     ("rbx", "rbx"), ("ebx", "rbx"), ("bx", "rbx"), ("bl", "rbx"),
                     ("rcx", "rcx"), ("ecx", "rcx"), ("cx", "rcx"), ("cl", "rcx"),
                     ("rdx", "rdx"), ("edx", "rdx"), ("dx", "rdx"), ("dl", "rdx"),
                     ("rsi", "rsi"), ("esi", "rsi"), ("si", "rsi"), ("sil", "rsi"),
                     ("rdi", "rdi"), ("edi", "rdi"), ("di", "rdi"), ("dil", "rdi"),
                     ("rbp", "rbp"), ("ebp", "rbp"), ("rsp", "rsp"), ("esp", "rsp")):
    ALIAS[_wide] = _root


def reg_root(name):
    return ALIAS.get(name, name)


def iat_index():
    """(dll, name) -> IAT thunk RVA, for the whole import table."""
    out = {}
    for e in pe().DIRECTORY_ENTRY_IMPORT:
        dll = e.dll.decode()
        for imp in e.imports:
            nm = imp.name.decode() if imp.name else "ord_%d" % imp.ordinal
            out[(dll, nm)] = imp.address - IMAGE_BASE
    return out


def text_callsites(iat_rva):
    """Every `call/jmp [rip+disp]` in .text whose target is that IAT slot."""
    name, va, vsz, raw, rawsz, ch = sec_by_name(".text")
    blob = data()[raw:raw + rawsz]
    hits = []
    for i in range(len(blob) - 6):
        if blob[i] != 0xFF or blob[i + 1] not in (0x15, 0x25):
            continue
        disp = struct.unpack("<i", blob[i + 2:i + 6])[0]
        if (va + i + 6 + disp) & 0xFFFFFFFF == iat_rva:
            hits.append(va + i)
    return hits


def insns_before(site, limit=0x300, maxn=120):
    """Instructions between the enclosing function start and `site`.

    `func_at` (.pdata) is unreliable on this binary -- some entries start inside
    another function -- so a candidate start is only accepted if the decode
    actually lands on `site`.  Candidates: the nearest int3-padded boundary, then
    the .pdata begin, then coarse backward offsets.
    """
    d = data()
    starts = []
    for back in range(4, limit):
        o = rva_to_off(site - back)
        if o is not None and d[o:o + 4] == b"\xCC\xCC\xCC\xCC":
            starts.append(site - back + 4)
            break
    f = func_at(site)
    if f and f[0] < site:
        starts.append(f[0])
    for back in (0x40, 0x60, 0x80, 0xC0, 0x100, 0x180, 0x200):
        starts.append(site - back)
    for start in starts:
        if start < site - limit:
            continue
        out = list(disasm(start, end_rva=site + 16))
        if any(ins.address == site for ins in out):
            return [i for i in out if i.address < site][-maxn:]
    return [i for i in disasm(site - 16, end_rva=site + 16) if i.address < site]


def read_ansi(rva, maxlen=96):
    b = read_rva(rva, maxlen)
    return None if not b else b.split(b"\x00")[0].decode("latin-1", "replace")


def read_wide(rva, maxlen=256):
    b = read_rva(rva, maxlen)
    if not b or len(b) < 2:
        return None
    txt = b.decode("utf-16-le", "replace").split("\x00")[0]
    return txt if txt and txt.isprintable() else None


def looks_wide(rva):
    b = read_rva(rva, 4)
    return bool(b) and len(b) == 4 and b[1] == 0 and 0x20 <= b[0] < 0x7F


NON_WRITERS = ("cmp", "test", "push", "pop", "nop", "jmp", "call", "ret",
               "bt", "ucomis", "comis", "prefetch")


def _write_of(ins):
    """(register_root, mnemonic) if this instruction writes a register."""
    if ins.mnemonic.startswith(NON_WRITERS):
        return None, None
    ops = ins.operands
    if not ops or ops[0].type != X86_OP_REG:
        return None, None
    return reg_root(ins.reg_name(ops[0].reg)), ins.mnemonic


def _value_of(ins, reg):
    """Classify the value an instruction puts in `reg`."""
    if ins.mnemonic == "lea":
        t = insn_target(ins)
        if t is None:
            return {"kind": "stack-buffer", "expr": ins.op_str,
                    "why": "lea %s" % ins.op_str}
        if looks_wide(t):
            return {"kind": "literal", "rva": "%08X" % t, "text": read_wide(t),
                    "why": "lea %s" % ins.op_str}
        return {"kind": "literal-ansi", "rva": "%08X" % t, "text": read_ansi(t),
                "why": "lea %s" % ins.op_str}
    if ins.mnemonic in ("xor", "sub") and len(ins.operands) == 2 and \
            ins.operands[1].type == X86_OP_REG and \
            reg_root(ins.reg_name(ins.operands[1].reg)) == reg:
        return {"kind": "null", "why": "%s %s" % (ins.mnemonic, ins.op_str)}
    if ins.mnemonic == "mov" and ins.operands[1].type == X86_OP_IMM:
        return {"kind": "immediate", "value": ins.operands[1].imm,
                "why": "mov %s" % ins.op_str}
    if ins.mnemonic == "mov" and ins.operands[1].type == 1:
        # register copy: follow one hop
        src = reg_root(ins.reg_name(ins.operands[1].reg))
        return {"kind": "register", "source_reg": src,
                "why": "mov %s" % ins.op_str}
    t = insn_target(ins)
    if t is not None:
        return {"kind": "global", "rva": "%08X" % t, "why": "mov %s" % ins.op_str}
    return {"kind": "other", "why": "%s %s" % (ins.mnemonic, ins.op_str)}


def _wcscpy_source(insns):
    """Resolve the literal behind a `movzx r,word [base+idx]` copy loop.

    This is the wcscpy shape MSVC emits for `TCHAR Buf[N]; wcscpy(Buf, L"...")`.
    Both registers of the load are followed: whichever one traces back to a
    `lea reg,[rip+X]` is the literal; if it instead comes from a
    `cmovne reg,[rsp+X]` the string is dynamic (an FString's data pointer) and
    the literal is only the empty-string default.
    """
    loop = None
    for ins in reversed(insns):
        if ins.mnemonic == "movzx" and ins.operands and ins.operands[1].type == 3:
            loop = ins
            break
    if loop is None:
        return None
    mem = loop.operands[1].mem
    regs = [reg_root(loop.reg_name(r)) for r in (mem.base, mem.index) if r]
    fallback = None
    for reg in regs:
        for ins in reversed(insns):
            if ins.address >= loop.address:
                continue
            root, how = _write_of(ins)
            if root != reg:
                continue
            v = _value_of(ins, reg)
            if v.get("kind") in ("literal", "literal-ansi"):
                return v
            if how and how.startswith("cmov"):
                return {"kind": "dynamic",
                        "why": "%s %s" % (ins.mnemonic, ins.op_str)}
            fallback = v
            break
    return fallback


def resolve_stack_arg(insns, slot):
    """Name passed in the Nth stack argument (CreateFileMappingW's lpName)."""
    from capstone.x86 import X86_REG_RSP
    n = int(slot.replace("stack", ""))
    disp = 0x20 + 8 * (n - 5)
    for ins in reversed(insns):
        if ins.mnemonic != "mov" or len(ins.operands) != 2:
            continue
        dst = ins.operands[0]
        if dst.type != 3 or dst.mem.base != X86_REG_RSP or dst.mem.disp != disp:
            continue
        src = ins.operands[1]
        if src.type == 2 and src.imm == 0:
            return {"kind": "null", "why": "mov [rsp + 0x%x], 0" % disp}
        if src.type == 1:
            reg = reg_root(ins.reg_name(src.reg))
            for back in reversed(insns):
                if back.address >= ins.address:
                    continue
                root, _how = _write_of(back)
                if root != reg:
                    continue
                v = _value_of(back, reg)
                v["why"] = "%s; then %s" % (v.get("why"), ins.op_str)
                if v.get("kind") == "dynamic":
                    v = _wcscpy_source(insns) or v
                return v
    return {"kind": "unresolved", "slot": slot,
            "why": "no `mov [rsp + 0x%x], ...` before the call" % disp}


def resolve_arg(site, which):
    """How the name argument was set, plus the evidence window."""
    insns = insns_before(site)
    ev = ["%08X %-8s %s" % (i.address, i.mnemonic, i.op_str) for i in insns[-14:]]
    if which is None:
        return {"kind": "not-requested"}, ev
    if which.startswith("stack"):
        return resolve_stack_arg(insns, which), ev

    target = reg_root(which)
    found = None
    for ins in reversed(insns):
        if ins.mnemonic.startswith(("call", "jmp")):
            break
        root, _how = _write_of(ins)
        if root != target:
            continue
        found = _value_of(ins, target)
        if found.get("kind") == "register":
            src = found["source_reg"]
            for back in reversed(insns):
                if back.address >= ins.address:
                    continue
                r2, _h2 = _write_of(back)
                if r2 == src:
                    found = _value_of(back, src)
                    break
        break

    if found and found.get("kind") == "stack-buffer":
        src = _wcscpy_source(insns)
        if src:
            found["stack_source"] = src
            if src.get("text"):
                found["text"] = src["text"]
        found["copy_loop"] = any("movzx" in e for e in ev)
    return found, ev


def xrefs_verified(global_rva):
    """Rip-relative references to a global, each re-decoded before it is kept.

    Uses the prebuilt .text index (every linearly decoded rip-relative operand)
    and drops any entry whose site does not decode to the expected target, so a
    desynced index entry cannot be reported as a reference that does not exist.
    """
    try:
        from hunt import refs_to
        from hunt2 import insn_at
    except Exception:
        return []
    out = []
    for site, fn, mn in refs_to(global_rva):
        ins = insn_at(site)
        if ins is None:
            continue
        t = insn_target(ins)
        if t is None or (t & 0xFFFFFFFF) != (global_rva & 0xFFFFFFFF):
            continue
        out.append({"site": "%08X" % site, "func": "%08X" % fn,
                    "insn": "%s %s" % (ins.mnemonic, ins.op_str)})
    return out


def find_strings(pats, wide=True):
    """ASCII (.rdata/_RDATA/.data, min 6) plus UTF-16LE (same sections)."""
    rx = [re.compile(p, re.I) for p in pats]
    hits = []
    for rva, txt in iter_strings(minlen=6):
        for p in rx:
            if p.search(txt):
                hits.append({"rva": "%08X" % rva, "text": txt[:120], "enc": "ascii"})
                break
    if wide:
        d = data()
        for name, va, vsz, raw, rawsz, ch in sections():
            if name not in (".rdata", ".data", "_RDATA"):
                continue
            blob = d[raw:raw + min(vsz, rawsz)]
            i, L = 0, len(blob)
            while i < L - 4:
                j, s = i, []
                while j + 1 < L and blob[j + 1] == 0 and 0x20 <= blob[j] < 0x7F:
                    s.append(chr(blob[j])); j += 2
                if len(s) >= 6:
                    txt = "".join(s)
                    for p in rx:
                        if p.search(txt):
                            hits.append({"rva": "%08X" % (va + i),
                                         "text": txt[:120], "enc": "utf16le"})
                            break
                    i = j
                else:
                    i += 2
    return hits


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--json", default=None)
    args = ap.parse_args()

    iat = iat_index()
    report = {"imports": [], "sites": [], "globals": {}, "strings": {}}

    print("=" * 96)
    print("E5.1  IMPORT TABLE - sync / single-instance surface")
    print("=" * 96)
    for dll, fn, reg in TRACKED:
        rva = iat.get((dll, fn))
        row = {"dll": dll, "name": fn, "imported": rva is not None,
               "iat_rva": ("%08X" % rva) if rva else None,
               "name_arg": reg, "sites": []}
        if rva is None:
            report["imports"].append(row)
            continue
        sites = text_callsites(rva)
        row["sites"] = ["%08X" % s for s in sites]
        report["imports"].append(row)
        print("\n%-12s %-26s IAT %08X  %d call site(s)"
              % (dll, fn, rva, len(sites)))
        for s in sites:
            f = func_at(s)
            arg, _ev = resolve_arg(s, reg)
            label = arg.get("text")
            if label:
                print("   site %08X in FUN_%s  name=%r  (%s)"
                      % (s, ("%08X" % f[0]) if f else "?", label,
                         arg.get("why", "")))
            elif arg.get("kind") == "null":
                print("   site %08X in FUN_%s  name=NULL  (%s)"
                      % (s, ("%08X" % f[0]) if f else "?", arg.get("why")))
            elif arg.get("kind") == "stack-buffer":
                src = arg.get("stack_source") or {}
                print("   site %08X in FUN_%s  name=<%s> from %r %s"
                      % (s, ("%08X" % f[0]) if f else "?", arg.get("expr"),
                         src.get("text"), src.get("why") or src.get("kind")))
            elif arg.get("kind") == "dynamic":
                print("   site %08X in FUN_%s  name=<dynamic> %s"
                      % (s, ("%08X" % f[0]) if f else "?", arg.get("why")))
            else:
                print("   site %08X in FUN_%s  name=unresolved %r"
                      % (s, ("%08X" % f[0]) if f else "?", arg))
            report["sites"].append({"api": fn, "site": "%08X" % s,
                                    "func": ("%08X" % f[0]) if f else None,
                                    "arg": arg})

    print()
    print("=" * 96)
    print("E5.2  NOT IMPORTED - exhaustive over the whole import table")
    print("=" * 96)
    for r in report["imports"]:
        if not r["imported"]:
            print("   %-12s %s" % (r["dll"], r["name"]))

    print()
    print("=" * 96)
    print("E5.3  THE SINGLE-INSTANCE MUTEX")
    print("=" * 96)
    mutex = [s for s in report["sites"] if s["api"] == "CreateMutexW"]
    for s in mutex:
        print("   site %s in FUN_%s  name=%r"
              % (s["site"], s["func"], (s["arg"] or {}).get("text")))
    if mutex:
        site = int(mutex[0]["site"], 16)
        print("\n   post-call handling:")
        for ins in disasm(site, end_rva=site + 0x60):
            print("     %08X %-8s %s" % (ins.address, ins.mnemonic, ins.op_str))
        touched = {}
        for ins in disasm(site - 0x30, end_rva=site + 0x70):
            t = insn_target(ins)
            if t is not None and 0x4900000 <= t < 0x4C00000:
                touched.setdefault("%08X" % t, []).append(
                    "%08X %s %s" % (ins.address, ins.mnemonic, ins.op_str))
        print("\n   globals touched around the mutex call:")
        for g, uses in sorted(touched.items()):
            xs = xrefs_verified(int(g, 16))
            report["globals"][g] = {"around_call": uses[:4], "xrefs": xs}
            print("     %s  refs=%d" % (g, len(xs)))
            for x in xs:
                print("        %s  FUN_%s  %s" % (x["site"], x["func"], x["insn"]))

    print()
    print("=" * 96)
    print("E5.4  THE FIRST-INSTANCE FLAG AND ITS CONSUMERS")
    print("=" * 96)
    flag = None
    if mutex:
        site = int(mutex[0]["site"], 16)
        for ins in disasm(site, end_rva=site + 0x60):
            if ins.mnemonic == "mov" and "byte ptr" in ins.op_str and \
                    ins.operands and ins.operands[0].type == 3:
                t = insn_target(ins)
                if t is not None:
                    flag = t
                    print("   flag stored at %08X  (%s at %08X)"
                          % (t, ins.op_str, ins.address))
                    break
    if flag is not None:
        xs = xrefs_verified(flag)
        report["globals"]["%08X" % flag] = {"role": "first-instance flag",
                                            "xrefs": xs}
        print("   %d reference(s) in the whole image:" % len(xs))
        for x in xs:
            print("     %s  FUN_%s  %-28s" % (x["site"], x["func"], x["insn"]))
        print("\n   consumer bodies (20 instructions before each read):")
        for x in xs:
            if not x["insn"].startswith(("cmp", "test", "movzx")):
                continue
            s = int(x["site"], 16)
            print("   --- %s ---" % x["site"])
            for ins in insns_before(s)[-8:]:
                print("     %08X %-8s %s" % (ins.address, ins.mnemonic, ins.op_str))
            print("     %08X %-8s %s   <-- the read" % (s, "cmp", "[flag]"))

    print()
    print("=" * 96)
    print("E5.5  STRINGS  (ASCII + UTF-16LE, .rdata/.data, min length 6)")
    print("=" * 96)
    report["strings"]["instance_messages"] = find_strings(
        ["already running", "another instance", "multiple instance",
         "single instance", "only one instance", "instance of the game"])
    report["strings"]["switches"] = find_strings(["Multiprocess", "NEVERFIRST"])
    for k, v in report["strings"].items():
        print("   [%s] %d hit(s)" % (k, len(v)))
        for h in v[:12]:
            print("      %s %-7s %s" % (h["rva"], h["enc"], h["text"]))

    if args.json:
        with open(args.json, "w", encoding="utf-8", newline="\n") as fh:
            json.dump(report, fh, indent=1)
            fh.write("\n")
        print("\nwrote %s" % args.json)
    return 0


if __name__ == "__main__":
    sys.exit(main())
