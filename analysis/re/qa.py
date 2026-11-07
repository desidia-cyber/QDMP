"""QA: re-parse targets.h and verify every entry against the file bytes."""
import sys, re
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
from qdlib import *

H = open(r"G:\Freespace\Queens_Domain\analysis\re\targets.h").read()
rows = re.findall(r'\{"([^"]+)",\s*0x([0-9A-Fa-f]+)u,\s*"([^"]+)",\s*\{([^}]*)\}\}', H)
print("entries:", len(rows))
bad = 0
for name, rva, role, bytes_s in rows:
    r = int(rva, 16)
    exp = bytes(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", bytes_s))
    got = read_rva(r, 16)
    if got is None:
        # .bss -> the image is zero there
        ok = exp == b"\x00" * 16
        note = ".bss/zero"
    else:
        ok = got[:len(exp)] == exp
        note = ""
    st = is_func_start(r)
    call_t = len(callers_check := 0) if False else None
    if not ok:
        bad += 1
    print("%-62s %08X %-6s %s %s" % (name[:62], r, role,
          "OK " if ok else "MISMATCH", note))
    if role in ("hook", "util") and not st:
        print("      note: not a .pdata begin (may be a call target / funclet)")
print()
print("byte mismatches:", bad)
