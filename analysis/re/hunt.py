import sys, os, marshal, collections
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
from qdlib import *

RE = r"G:\Freespace\Queens_Domain\analysis\re"
_xr = None

def xrefs():
    global _xr
    if _xr is None:
        with open(os.path.join(RE, "xrefs.bin"), "rb") as f:
            _xr = marshal.load(f)
    return _xr

def refs_to(rva):
    return xrefs().get(rva & 0xFFFFFFFF, [])

def ref_funcs(rva):
    return sorted(set(s[1] for s in refs_to(rva)))

def show_refs(rva, label=""):
    rs = refs_to(rva)
    print("xrefs to %08X %s : %d" % (rva, label, len(rs)))
    for site, func, mn in rs[:40]:
        print("    site %08X in FUN_%08X  %s" % (site, func, mn))
    return rs

def dump(rva, n=16, label=""):
    print("  %08X %-28s %s" % (rva, hexs(rva, n), label))

def glob_refs_in(func_rva, lo=0x4500000, hi=0x4C08000):
    """rip-relative targets inside .data/.bss that a function touches."""
    out = []
    for ins, t in func_scan(func_rva):
        if t is not None and lo <= t < hi:
            out.append((ins.address, t, ins.mnemonic, ins.op_str))
    return out
