#!/usr/bin/env python3
"""Build guard for the QueensDomain co-op mod.

Answers one question at load: *is this image the build the hook table was read
out of?*  If it is not, the mod must refuse rather than half-work -- a wrong RVA
does not fault, it silently hooks whatever now lives at that offset.

WHAT IS CHECKED
---------------
1. SizeOfImage against QD_IMAGE_SIZE (a structural check, independent of bytes).
2. SHA-256 of the file, recorded (and compared when QD_IMAGE_SHA256 is pinned).
3. For every `hook` / `util` target: the first 16 file bytes at its RVA.
4. For every `data` target: the same, EXCEPT that a zero-initialised global
   cannot be checked that way -- the file has no bytes for it, so comparing
   zeros would pass on any build.  Those are classified `runtime-validated` and
   reported as such instead of being counted as verified.
5. Structure: a `hook`/`util` target must be a .pdata function begin or a known
   direct-call target.  Failing that is reported as an anomaly (a warning, not a
   refusal -- the binary is known to have real functions with no unwind entry).

VERDICT
-------
PASS   -- every checkable target matches; exit 0.
REFUSE -- exit 1.  Any of: SizeOfImage mismatch, 16-byte mismatch, a `data`
          target whose file bytes are non-zero where zero was expected, a target
          outside the image, or an unparsable target table.

--selftest proves the guard can fail: it corrupts one byte of a byte-verified
target *in memory* and requires REFUSE, then corrupts one byte that no target
covers and requires PASS.  A guard that always passes fails the first; a guard
that always refuses fails the second.

Usage:
    python analysis/re/build_guard.py <path-to-exe>
    python analysis/re/build_guard.py <path-to-exe> --json out.json
    python analysis/re/build_guard.py --selftest
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_EXE = os.path.join(
    os.path.dirname(os.path.dirname(HERE)),
    "QueensDomain", "QueensDomain", "Binaries", "Win64",
    "QueensDomain-Win64-Shipping.exe")
DEFAULT_TARGETS = os.path.join(HERE, "targets.h")
DEFAULT_JSON = os.path.join(HERE, "build_guard.json")
CALLS_BIN = os.path.join(HERE, "calls.bin")
EXPECT_BYTES = 16


# --------------------------------------------------------------------- image

class Image:
    """Minimal PE reader that works on any path (or an in-memory buffer)."""

    def __init__(self, buf: bytes, label: str):
        import pefile
        self.buf = buf
        self.label = label
        self.pe = pefile.PE(data=buf, fast_load=True)
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.size_of_image = self.pe.OPTIONAL_HEADER.SizeOfImage
        self.sections = []
        for s in self.pe.sections:
            self.sections.append({
                "name": s.Name.rstrip(b"\x00").decode("latin-1"),
                "va": s.VirtualAddress,
                "vsize": s.Misc_VirtualSize,
                "raw": s.PointerToRawData,
                "rawsz": s.SizeOfRawData,
                "chars": s.Characteristics,
            })
        self.pdata_begins = self._pdata_begins()

    def section_of(self, rva: int):
        for s in self.sections:
            if s["va"] <= rva < s["va"] + max(s["vsize"], s["rawsz"]):
                return s
        return None

    def file_backed(self, rva: int) -> bool:
        s = self.section_of(rva)
        return bool(s) and (rva - s["va"]) < s["rawsz"]

    def read(self, rva: int, n: int):
        s = self.section_of(rva)
        if not s or (rva - s["va"]) >= s["rawsz"]:
            return None
        off = s["raw"] + (rva - s["va"])
        chunk = self.buf[off:off + n]
        return chunk if len(chunk) == n else None

    def _pdata_begins(self):
        begins = set()
        s = next((x for x in self.sections if x["name"] == ".pdata"), None)
        if not s or not s["rawsz"]:
            return begins
        n = min(s["vsize"], s["rawsz"]) // 12
        for i in range(n):
            o = s["raw"] + i * 12
            b, e, _u = struct.unpack("<III", self.buf[o:o + 12])
            if b and e > b:
                begins.add(b)
        return begins

    def sha256(self) -> str:
        return hashlib.sha256(self.buf).hexdigest().upper()


# -------------------------------------------------------------------- targets

TARGET_RE = re.compile(
    r'\{\s*"([^"]+)"\s*,\s*0x([0-9A-Fa-f]+)[uU]?\s*,\s*"([^"]+)"\s*,\s*\{([^}]*)\}\s*\}')


def parse_targets(path: str):
    """Read qd_targets[] and the two #defines out of the generated header.

    A regex is honest here: the file is generated and its shape is fixed, and
    anything that fails to parse is counted so a shortened table cannot look
    like a clean run.
    """
    text = open(path, encoding="utf-8", errors="replace").read()
    size = re.search(r"QD_IMAGE_SIZE\s+0x([0-9A-Fa-f]+)", text)
    sha = re.search(r'QD_IMAGE_SHA256\s+"([0-9A-Fa-f]+)"', text)
    targets, bad = [], []
    for m in TARGET_RE.finditer(text):
        name, rva, role, bytestr = m.groups()
        vals = [int(b.strip(), 16) for b in bytestr.split(",") if b.strip()]
        if len(vals) != EXPECT_BYTES or any(v > 0xFF for v in vals):
            bad.append(name)
            continue
        targets.append({"name": name, "rva": int(rva, 16), "role": role,
                        "expect": vals})
    return {
        "targets": targets,
        "bad": bad,
        "declared_image_size": int(size.group(1), 16) if size else None,
        "declared_sha256": sha.group(1).upper() if sha else None,
    }


def load_call_targets(path=CALLS_BIN):
    """RVAs that are the target of a direct call somewhere in the image."""
    if not os.path.isfile(path):
        return None
    try:
        import marshal
        with open(path, "rb") as fh:
            return set(marshal.load(fh).keys())
    except Exception:
        return None


# --------------------------------------------------------------------- guard

def check_target(img: Image, t: dict, call_targets) -> dict:
    row = {"name": t["name"], "rva": "0x%08X" % t["rva"], "role": t["role"],
           "expect": " ".join("%02X" % b for b in t["expect"]),
           "klass": None, "status": None, "actual": None, "note": None,
           "anomaly": None, "hard_fail": False}

    rva = t["rva"]
    if rva >= img.size_of_image:
        row.update(klass="outside", status="MISSING",
                   note="RVA beyond SizeOfImage 0x%X" % img.size_of_image,
                   hard_fail=True)
        return row

    zero_expect = all(b == 0 for b in t["expect"])
    backed = img.file_backed(rva)

    if t["role"] in ("hook", "util"):
        row["klass"] = "code-bytes"
        actual = img.read(rva, EXPECT_BYTES)
        if actual is None:
            row.update(status="MISSING", note="not file-backed", hard_fail=True)
            return row
        row["actual"] = " ".join("%02X" % b for b in actual)
        if actual != bytes(t["expect"]):
            row.update(status="MISMATCH", hard_fail=True)
        else:
            row["status"] = "OK"
        # structural check: a hook target should be a real entry point
        is_begin = rva in img.pdata_begins
        is_call = call_targets is not None and rva in call_targets
        if not is_begin and not is_call:
            row["anomaly"] = ("neither a .pdata begin nor a direct-call target"
                              if call_targets is not None
                              else "not a .pdata begin (call index not loaded)")
        elif not is_begin:
            row["note"] = "call target, no RUNTIME_FUNCTION (documented here)"
        return row

    if t["role"] == "data":
        if zero_expect:
            row["klass"] = "bss-runtime"
            if backed:
                actual = img.read(rva, EXPECT_BYTES)
                if actual and any(actual):
                    row.update(
                        klass="data-bytes",
                        status="MISMATCH",
                        actual=" ".join("%02X" % b for b in actual),
                        note="header says zero-initialised but the file has "
                             "bytes here -- stale target",
                        hard_fail=True)
                    return row
                row["status"] = "ZERO-IN-FILE"
                row["note"] = ("file image is zero at this RVA, so the 16-byte "
                               "compare cannot discriminate a rebuild; validated "
                               "at runtime instead")
                return row
            row["status"] = "RUNTIME-VALIDATED"
            row["note"] = ("no file bytes (section virtual tail); validated at "
                           "runtime, never by this guard")
            return row
        row["klass"] = "data-bytes"
        actual = img.read(rva, EXPECT_BYTES)
        if actual is None:
            row.update(status="MISSING", note="not file-backed", hard_fail=True)
            return row
        row["actual"] = " ".join("%02X" % b for b in actual)
        if actual != bytes(t["expect"]):
            row.update(status="MISMATCH", hard_fail=True)
        else:
            row["status"] = "OK"
        return row

    row.update(klass="unknown", status="UNKNOWN ROLE", hard_fail=True,
               note="role %r is not hook/util/data" % t["role"])
    return row


def run_guard(buf: bytes, label: str, targets_path: str, call_targets) -> dict:
    """Check `buf` (the image) against targets.h.  Pure: no I/O on the image."""
    parsed = parse_targets(targets_path)
    img = Image(buf, label)

    report = {
        "image": {"path": label, "sha256": img.sha256(),
                  "size_of_image": "0x%X" % img.size_of_image,
                  "file_size": len(buf), "image_base": "0x%X" % img.base},
        "declared": {"image_size": ("0x%X" % parsed["declared_image_size"])
                     if parsed["declared_image_size"] else None,
                     "sha256": parsed["declared_sha256"]},
        "targets": [], "failures": [], "anomalies": [], "warnings": [],
    }

    dsize = parsed["declared_image_size"]
    if dsize is not None and dsize != img.size_of_image:
        report["failures"].append(
            "SizeOfImage 0x%X != declared 0x%X" % (img.size_of_image, dsize))
    if not parsed["targets"]:
        report["failures"].append("parsed 0 targets from %s" % targets_path)
    for n in parsed["bad"]:
        report["failures"].append("unparsable target entry: %s" % n)

    dsha = parsed["declared_sha256"]
    if dsha and dsha != img.sha256():
        report["failures"].append(
            "sha256 %s != pinned %s" % (img.sha256()[:16], dsha[:16]))
    elif not dsha:
        report["warnings"].append(
            "sha256 not pinned in targets.h; the 16-byte checks are the authority")

    for t in parsed["targets"]:
        row = check_target(img, t, call_targets)
        report["targets"].append(row)
        if row["hard_fail"]:
            report["failures"].append("%s @ %s: %s"
                                      % (row["name"], row["rva"], row["status"]))
        if row["anomaly"]:
            report["anomalies"].append("%s @ %s: %s"
                                       % (row["name"], row["rva"], row["anomaly"]))

    report["counts"] = {
        "total": len(report["targets"]),
        "byte_verified": sum(1 for r in report["targets"] if r["status"] == "OK"),
        "runtime_validated": sum(1 for r in report["targets"]
                                 if r["status"] == "RUNTIME-VALIDATED"),
        "zero_in_file": sum(1 for r in report["targets"]
                            if r["status"] == "ZERO-IN-FILE"),
        "mismatch": sum(1 for r in report["targets"] if r["status"] == "MISMATCH"),
    }
    report["verdict"] = "REFUSE" if report["failures"] else "PASS"
    return report


def print_report(rep: dict) -> None:
    img = rep["image"]
    print("=" * 100)
    print("BUILD GUARD  %s" % img["path"])
    print("  sha256        %s" % img["sha256"])
    print("  SizeOfImage   %s   (%d bytes on disk)" % (img["size_of_image"],
                                                       img["file_size"]))
    if rep["declared"]["image_size"]:
        print("  declared      %s  (targets.h)" % rep["declared"]["image_size"])
    print("=" * 100)
    print("%-58s %-5s %-9s %-14s %s"
          % ("target", "role", "rva", "class", "status"))
    print("-" * 100)
    for r in rep["targets"]:
        flags = r["status"]
        if r["anomaly"]:
            flags += "  [anomaly]"
        print("%-58s %-5s %-9s %-14s %s"
              % (r["name"][:58], r["role"], r["rva"], r["klass"], flags))
        if r["status"] == "MISMATCH":
            print("    expect %s" % r["expect"])
            print("    actual %s" % r["actual"])
    c = rep["counts"]
    print("-" * 100)
    print("targets %d: %d byte-verified, %d runtime-validated, %d zero-in-file, "
          "%d mismatch"
          % (c["total"], c["byte_verified"], c["runtime_validated"],
             c["zero_in_file"], c["mismatch"]))
    for w in rep["warnings"]:
        print("WARN    %s" % w)
    for a in rep["anomalies"]:
        print("ANOMALY %s" % a)
    for f in rep["failures"]:
        print("FAIL    %s" % f)
    print("=" * 100)
    print("VERDICT: %s" % rep["verdict"])
    print("=" * 100)


# ------------------------------------------------------------------ selftest

def selftest(exe_path: str, targets_path: str) -> int:
    print("=" * 100)
    print("SELFTEST - the guard must be able to fail")
    print("=" * 100)
    buf = open(exe_path, "rb").read()
    calls = load_call_targets()
    parsed = parse_targets(targets_path)
    img = Image(buf, exe_path)

    base = run_guard(buf, exe_path, targets_path, calls)
    print("baseline on the real image       : %s" % base["verdict"])
    if base["verdict"] != "PASS":
        print("  cannot self-test a guard that refuses the genuine image:")
        for f in base["failures"]:
            print("    %s" % f)
        return 2

    # 1. corrupt one byte of a byte-verified target -> must REFUSE
    victim = next((r for r in base["targets"] if r["status"] == "OK"), None)
    if victim is None:
        print("  no byte-verified target to corrupt; test is vacuous -> FAIL")
        return 2
    rva = int(victim["rva"], 16)
    sec = img.section_of(rva)
    off = sec["raw"] + (rva - sec["va"])
    bad = bytearray(buf)
    bad[off] ^= 0xFF
    rep = run_guard(bytes(bad), exe_path + " (1 byte flipped @ %s)" % victim["rva"],
                    targets_path, calls)
    named = any(victim["name"] in f for f in rep["failures"])
    print("flip 1 byte at %s (%s)     : %s%s"
          % (victim["rva"], victim["name"][:34], rep["verdict"],
             "  <- caught" if named else "  <- NOT attributed"))
    ok1 = rep["verdict"] == "REFUSE" and named

    # 2. corrupt one byte that no target covers -> must still PASS, so the guard
    #    is not simply "any change = refuse"
    guarded = set()
    for t in parsed["targets"]:
        for d in range(EXPECT_BYTES):
            guarded.add(t["rva"] + d)
    pick = None
    for s in img.sections:
        if s["name"] in (".text", ".rdata", ".data", ".pdata") or not s["rawsz"]:
            continue
        for back in range(1, min(s["rawsz"], 0x4000)):
            rva2 = s["va"] + (s["rawsz"] - back)
            if rva2 not in guarded:
                pick = (s["raw"] + s["rawsz"] - back, rva2)
                break
        if pick:
            break
    if pick is None:
        print("  found no unguarded byte to flip; second half skipped")
        ok2 = False
    else:
        bad2 = bytearray(buf)
        bad2[pick[0]] ^= 0xFF
        rep2 = run_guard(bytes(bad2),
                         exe_path + " (1 byte flipped @ 0x%X, unguarded)" % pick[1],
                         targets_path, calls)
        print("flip 1 byte at 0x%X (unguarded) : %s%s"
              % (pick[1], rep2["verdict"],
                 "  <- correctly ignored" if rep2["verdict"] == "PASS"
                 else "  <- false positive"))
        ok2 = rep2["verdict"] == "PASS"

    print("-" * 100)
    print("SELFTEST: %s   (corrupt-target caught=%s, unguarded ignored=%s)"
          % ("PASS" if (ok1 and ok2) else "FAIL", ok1, ok2))
    print("=" * 100)
    return 0 if (ok1 and ok2) else 1


# ---------------------------------------------------------------------- main

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("exe", nargs="?", default=DEFAULT_EXE)
    ap.add_argument("--targets", default=DEFAULT_TARGETS)
    ap.add_argument("--json", default=DEFAULT_JSON)
    ap.add_argument("--no-call-index", action="store_true",
                    help="skip the direct-call-target structural check")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    if not os.path.isfile(args.targets):
        print("targets.h not found: %s" % args.targets, file=sys.stderr)
        return 2

    if args.selftest:
        if not os.path.isfile(args.exe):
            print("exe not found: %s" % args.exe, file=sys.stderr)
            return 2
        return selftest(args.exe, args.targets)

    if not os.path.isfile(args.exe):
        print("exe not found: %s" % args.exe, file=sys.stderr)
        return 2

    calls = None if args.no_call_index else load_call_targets()
    if calls is None and not args.no_call_index and not args.quiet:
        print("note: calls.bin unavailable; structural check degraded",
              file=sys.stderr)

    with open(args.exe, "rb") as fh:
        buf = fh.read()
    rep = run_guard(buf, os.path.abspath(args.exe), args.targets, calls)
    if not args.quiet:
        print_report(rep)

    if args.json:
        with open(args.json, "w", encoding="utf-8", newline="\n") as fh:
            json.dump(rep, fh, indent=1)
            fh.write("\n")
        if not args.quiet:
            print("wrote %s" % args.json)

    return 0 if rep["verdict"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
