#!/usr/bin/env python3
"""Generate mod/native/src/targets.h from the shipped Queen's Domain image.

WHY A GENERATOR AND NOT A HAND-WRITTEN TABLE
--------------------------------------------
DollsMP's targets.h says, in its own header comment, "The expected bytes below
were read out of that image, not typed, so a mismatch at runtime means this is a
different build -- not a typo here."  That property is worth more than the
convenience: a hand-typed byte string turns a build-detection guard into a
typo-detector, and the guard then refuses to run on a good build.

So this script reads the bytes from the file.  Two target classes exist and they
are NOT equivalent:

  check=1  the bytes live in the image on disk, so the file can be compared at
           load.  A mismatch means a different build.  Refuse to run.

  check=0  the target is a ZERO-INITIALISED global (.bss).  The file has no
           bytes for it, so any recorded constant would differ from the live
           value by design.  These must be validated at RUNTIME instead --
           and until they have been, they must not be used.

That distinction is the whole reason for the `check` byte: an earlier pass
compared zeros against a live pointer and produced a false PASS.

Usage:
  python tools/gen_targets.py                       # write native/src/targets.h
  python tools/gen_targets.py --verify              # check, do not write
"""
import argparse
import hashlib
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pe_imports import PE  # noqa: E402

EXE = (r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain"
       r"\Binaries\Win64\QueensDomain-Win64-Shipping.exe")
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                   "..", "native", "src", "targets.h")

EXPECT = 16

# (name, rva, role).  RVAs and roles are the product of the analysis corpus
# (analysis/re/functions.tsv, analysis/re/SUBSTRATE-TARGETS.md, 10-gaps-register);
# the BYTES come from the image below, which is what makes the guard meaningful.
TARGETS = [
    # --- Phase 0 substrate: everything the mod needs in order to start --------
    ("UObject::ProcessEvent",                     0x011A7AA0, "hook"),
    ("GWorld",                                    0x04BA86B0, "data"),
    ("GEngine",                                   0x04BA4D88, "data"),
    ("FNameEntryAllocator",                       0x04A27F00, "data"),
    ("FNameEntry::Blocks",                        0x04A27F10, "data"),
    ("FName::ToString",                           0x00FB8DE0, "call"),
    ("FName::FName(wchar_t*)",                    0x00FABF20, "call"),
    ("UObject::FindFunction",                     0x011A0050, "call"),
    ("UStruct::FindFunctionByName",               0x0103DB10, "call"),
    ("GUObjectArray",                             0x04A64250, "data"),
    ("ProcessEvent vtable slot",                  0x0376D258, "data"),
    ("FEngineLoop::Tick",                         0x007753A0, "hook"),
    ("GMalloc",                                   0x049DFB30, "data"),
    ("StaticConstructObject_Internal",            0x011AF050, "call"),

    # --- player lookup and the actor spawn pair (B7) -------------------------
    ("UGameplayStatics::GetPlayerPawn",           0x028E0640, "call"),
    ("UGameplayStatics::GetPlayerController",     0x028E0340, "call"),
    ("UGameplayStatics::GetWorldDeltaSeconds",    0x028E1CE0, "call"),
    ("UEngine::GetWorldFromContextObject",        0x02CDEDF0, "call"),
    ("UGameplayStatics::BeginDeferredActorSpawnFromClass", 0x028D3C90, "call"),
    ("UGameplayStatics::FinishSpawningActor",     0x028DCD50, "call"),
    ("AActor::FinishSpawning",                    0x02651580, "call"),
    ("World spawn entry",                         0x029AD950, "call"),

    # --- save containers (C2/C4/D5) -----------------------------------------
    ("GetActiveBaseSave",                         0x00E2D100, "call"),
    ("GetNamedSaveData",                          0x00E2F6D0, "call"),
    ("StoreEnemySaveState",                       0x00E3BF10, "call"),
    ("RetrieveEnemySaveState",                    0x00E39240, "call"),
    ("RollItemDrop",                              0x00E392F0, "call"),
    ("GetInventoryItemCount",                     0x00E2EA40, "call"),
    ("UGameplayStatics::SaveGameToSlot",          0x028F2B40, "call"),

    # --- level streaming (C10) ----------------------------------------------
    ("AddLevelListToStack",                       0x00E215E0, "call"),
    ("FlushLevelManagerActions",                  0x00E2C270, "call"),
    ("GetLevelManager",                           0x00E2EE50, "call"),
    ("Level visibility apply",                    0x00E21E40, "call"),
    ("ULevelStreaming::SetShouldBeVisible",       0x029ACB20, "call"),

    # --- pause surface (D1) --------------------------------------------------
    ("UGameplayStatics::SetGamePaused",           0x028F3180, "hook"),
    ("UGameplayStatics::IsGamePaused",            0x028E8DB0, "call"),

    # --- owner-see flags (B4) ------------------------------------------------
    ("UPrimitiveComponent::SetOwnerNoSee",        0x027D8340, "call"),
    ("UPrimitiveComponent::SetOnlyOwnerSee",      0x027D8310, "call"),

    # --- combat --------------------------------------------------------------
    ("UGameplayStatics::ApplyDamage",             0x028D2190, "call"),
    ("AActor::K2_SetActorLocation",               0x0265B400, "call"),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default=EXE)
    ap.add_argument("--out", default=OUT)
    ap.add_argument("--verify", action="store_true")
    args = ap.parse_args()

    if not os.path.exists(args.exe):
        raise SystemExit("exe not found: %s" % args.exe)

    pe = PE(args.exe)
    if not pe.ok:
        raise SystemExit("PE parse failed: %s" % "; ".join(pe.notes))
    with open(args.exe, "rb") as fh:
        blob = fh.read()

    sha = hashlib.sha256(blob).hexdigest().upper()
    size_of_image = pe.size_of_image if hasattr(pe, "size_of_image") else None
    if size_of_image is None:
        # PE optional header: SizeOfImage at +0x38 of the optional header, which
        # starts at e_lfanew + 4 + 20 for PE32+.
        e_lfanew = struct.unpack_from("<I", blob, 0x3C)[0]
        opt = e_lfanew + 24
        size_of_image = struct.unpack_from("<I", blob, opt + 56)[0]

    # Section virtual ranges, so a target beyond a section's raw data can be
    # classified as zero-initialised (check=0) instead of reported as unmapped.
    # Measured on this image: .data has vsz 0x516174 against raw 0x273C00, and
    # every global the mod needs (GWorld, GEngine, GUObjectArray, FNamePool,
    # GMalloc) lives in that tail -- which is exactly why the first RE pass that
    # compared recorded zeros against them produced a false PASS.
    secs = [(s["vaddr"], s["vsize"], s["rawptr"], s["rawsize"])
            for s in pe.sections]

    def classify(rva):
        for va, vsz, raw, rawsz in secs:
            if va <= rva < va + vsz:
                if rva - va < rawsz:
                    return (raw + (rva - va)) if rva - va + EXPECT <= rawsz else None
                return "bss"
        return None

    rows = []
    problems = []
    for name, rva, role in TARGETS:
        where = classify(rva)
        if where is None:
            problems.append("%s @ %08X: not inside any section's raw data "
                            "(or straddles the raw/virtual boundary)" % (name, rva))
            rows.append((name, rva, role, 0, bytes(EXPECT)))
            continue
        if where == "bss":
            rows.append((name, rva, role, 0, bytes(EXPECT)))
            continue
        off = where
        if off + EXPECT > len(blob):
            problems.append("%s @ %08X: past end of file" % (name, rva))
            rows.append((name, rva, role, 0, bytes(EXPECT)))
            continue
        raw = blob[off:off + EXPECT]
        # A run of zeros means this is .bss or padding: nothing to compare.
        if raw.count(0) == EXPECT:
            rows.append((name, rva, role, 0, raw))
        else:
            rows.append((name, rva, role, 1, raw))

    n_check = sum(1 for r in rows if r[3] == 1)
    n_runtime = len(rows) - n_check

    lines = []
    w = lines.append
    w("// GENERATED FILE -- DO NOT EDIT BY HAND.")
    w("// Produced by tools/gen_targets.py from the shipped image.")
    w("//")
    w("// image : QueensDomain-Win64-Shipping.exe")
    w("// sha256: %s" % sha)
    w("//")
    w("// The expected bytes below were read out of that image, not typed, so a")
    w("// mismatch at runtime means this is a different build -- not a typo here.")
    w("//")
    w("// `check` is 1 where the bytes below are compared at load, and 0 where they")
    w("// cannot be: the target is a ZERO-INITIALISED global, so the file has no")
    w("// bytes for it and the live value would differ from any recorded constant by")
    w("// design.  A check=0 target is validated at runtime instead, and until it")
    w("// has been, it must not be used -- see QdSubstrate::RuntimeValidate().")
    w("#pragma once")
    w("#include <stdint.h>")
    w("")
    w('#define QD_IMAGE_SHA256 "%s"' % sha)
    w("#define QD_IMAGE_SIZE   0x%08XULL" % size_of_image)
    w("#define QD_EXPECT_BYTES %d" % EXPECT)
    w("")
    w("struct QdTarget {")
    w("    const char* name;")
    w("    uint32_t    rva;")
    w("    const char* role;")
    w("    uint8_t     check;   // 1 = compare expect[] at load; 0 = runtime-validated only")
    w("    uint8_t     expect[QD_EXPECT_BYTES];")
    w("};")
    w("")
    w("static const QdTarget kQdTargets[] = {")
    for name, rva, role, check, raw in rows:
        bs = ", ".join("0x%02X" % b for b in raw)
        w('    { "%-46s", 0x%08Xu, "%-5s", %d, { %s } },'
          % (name, rva, role, check, bs))
    w("};")
    w("")
    w("static const unsigned kQdTargetCount =")
    w("    sizeof(kQdTargets) / sizeof(kQdTargets[0]);")
    w("")
    w("// %d byte-verified, %d runtime-validated." % (n_check, n_runtime))
    w("")
    text = "\n".join(lines)

    if args.verify:
        cur = ""
        if os.path.exists(args.out):
            with open(args.out, "r", encoding="utf-8") as fh:
                cur = fh.read()
        if cur == text:
            print("OK  targets.h is current (%d targets, %d byte-verified)"
                  % (len(rows), n_check))
            return 0
        print("STALE  targets.h differs from the image")
        return 1

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(text + "\n")
    print("wrote %s" % os.path.normpath(args.out))
    print("  sha256           : %s" % sha)
    print("  SizeOfImage      : 0x%08X" % size_of_image)
    print("  targets          : %d (%d byte-verified, %d runtime-validated)"
          % (len(rows), n_check, n_runtime))
    for p in problems:
        print("  PROBLEM: %s" % p)
    return 0


if __name__ == "__main__":
    sys.exit(main())
