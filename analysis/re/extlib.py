"""Differential disassembly: DollsNest (UE4.27.2) vs Queen's Domain, same engine line.

Loads an arbitrary PE, disassembles by RVA, optionally with xref-free heuristics.
"""
import hashlib, struct, sys
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

IMGBASE = 0x140000000


class Image:
    def __init__(self, path):
        self.path = path
        self.pe = pefile.PE(path, fast_load=True)
        self.pe.parse_data_directories()
        self.data = open(path, "rb").read()
        self.sections = []
        for s in self.pe.sections:
            name = s.Name.rstrip(b"\x00").decode("latin-1")
            self.sections.append((name, s.VirtualAddress, s.Misc_VirtualSize,
                                  s.PointerToRawData, s.SizeOfRawData))
        self.md = Cs(CS_ARCH_X86, CS_MODE_64)
        self.md.detail = True

    def sha256(self):
        return hashlib.sha256(self.data).hexdigest().upper()

    def off(self, rva):
        for n, va, vsz, raw, rawsz in self.sections:
            if va <= rva < va + max(vsz, rawsz) and rva - va < rawsz:
                return raw + (rva - va)
        return None

    def dis(self, rva, count=40):
        o = self.off(rva)
        out = []
        for ins in self.md.disasm(self.data[o:o + count * 16], rva):
            out.append(ins)
            if len(out) >= count:
                break
        return out

    def show(self, rva, count=40, label=""):
        print("=== %s  %08X [%s] ===" % (label, rva, self.path.split("\\")[-1]))
        for ins in self.dis(rva, count):
            note = ""
            for op in ins.operands:
                if op.type == 3:  # X86_OP_IMM
                    t = op.imm & 0xFFFFFFFF
                    if 0x1000 <= t < 0x5000000:
                        note = " ; -> %08X" % t
            print("  %08X %-9s %-46s%s" % (ins.address, ins.mnemonic, ins.op_str, note))
