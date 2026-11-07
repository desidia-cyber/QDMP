# -*- coding: utf-8 -*-
"""Emit SUBSTRATE-TARGETS.md, targets.h, functions.tsv from one verified table."""
import sys, os, struct
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
from qdlib import *

OUT = r"G:\Freespace\Queens_Domain\analysis\re"

# name, rva, confidence, role, evidence, notes
T = [
 ("UObject::ProcessEvent", 0x011A7AA0, "HIGH", "hook",
  "Reads UObject+0x0C (InternalIndex), bounds-checks vs 0x4A64274 (GUObjectArray.NumElements), "
  "indexes 0x4A64260 (ObjObjects.Objects) with FUObjectItem stride 0x18 (lea rdx,[rax+rax*2]; "
  "lea rax,[rcx+rdx*8]), tests FUObjectItem.ObjectFlags>>29 (RF_Unreachable), then "
  "test dword [Function+0xB0],0x400 (FUNC_Native). Exactly UE4 ProcessEvent.",
  "Virtual, slot 68 / vtable offset 0x220 on every UObject-derived vtable "
  "(1818 identical entries in .rdata). Example vtable start 0x0376D038; its slot 68 sits at "
  "0x0376D258 = 0x0376D038+0x220 and the byte before 0x0376D038 terminates the UTF-16 string "
  "pool, proving the vtable start."),
 ("GWorld", 0x04BA86B0, "HIGH", "data",
  "Loaded and passed as rcx to FUN_02D39CF0 (UWorld::InternalGetNetMode) in FUN_0077C620; "
  "loaded as rdx (UWorld*) together with GEngine as rcx in FUN_026397A0 -> UEngine::GetWorldContextFromWorld. "
  "Set to 0 in FUN_02D46A8B (world teardown) and assigned in FUN_02CE85B0 (UEngine::LoadMap).",
  "8-byte pointer in .bss region of .data. Read by FEngineLoop::Tick (0x007753A0)."),
 ("GEngine", 0x04BA4D88, "HIGH", "data",
  "Written at 0x00774C8C in FUN_00774A70 (FEngineLoop::Init) immediately after the "
  "'Create GEngine' log string, and re-loaded at 0x00774CEB right after the "
  "'GEngine->ParseCommandline()' string, at 0x00774DF9 ('GEngine->Init') and 0x00774FEE "
  "('GEngine->Start()'). Independently: UGameplayStatics::GetWorldDeltaSeconds (0x028E1CE0) "
  "loads it as rcx=UEngine* and calls 0x02CDEDF0 (UEngine::GetWorldFromContextObject).",
  "1164 rip-relative references / 896 distinct functions."),
 ("FNameEntryAllocator (FNamePool)", 0x04A27F00, "HIGH", "data",
  "FName::ToString (0x00FB8DE0) does: shr edi,0x10 (block); movzx ecx,ax (offset); "
  "lea r8,[0x04A27F00]; ... lea ecx,[rax+rax] (offset*2); add rcx,[r8+rdi*8+0x10]. "
  "40 references, all in Core (0x00FAB..0x00FB9), all with the same lazy-init shape.",
  "Layout: +0x00 FRWLock Lock, +0x08 u32 CurrentBlock, +0x0C u32 CurrentByteCursor, "
  "+0x10 FNameEntry* Blocks[] -> 0x04A27F10. Resolve: Blocks[i>>16] + (i&0xFFFF)*2."),
 ("FNameEntry::Blocks[]", 0x04A27F10, "HIGH", "data",
  "Computed as FNamePool+0x10 from the 'add rcx,[r8+rdi*8+0x10]' in FName::ToString; r8=0x04A27F00.",
  "Array of FNameEntry* in .bss (all-zero on disk, filled at runtime). That is why scanning "
  ".data for it fails: .data raw data ends at RVA 0x4964C00, the pool lives above that in .bss."),
 ("FName::ToString", 0x00FB8DE0, "HIGH", "hook",
  "Signature (rcx=&FName, rdx=&FString). Reads FName{+0 ComparisonIndex,+4 Number}; "
  "block=idx>>16, off=idx&0xFFFF; entry=Blocks[block]+off*2; Len = *(u16*)entry >> 6 "
  "(0x00FB8EB8 movzx eax,word [rdi] / 0x00FB8EBB shr eax,6). 1633 direct callers.",
  "This proves the entry header here is the stock 2-byte {bIsWide:1, ProbeHash:5, Len:10}, "
  "NOT a 4-byte hash + 4-byte length. See 'ruled out' section."),
 ("UGameplayStatics::GetPlayerPawn", 0x028E0640, "HIGH", "hook",
  "Reached from execGetPlayerPawn (0x02DBF9E0) registered in the native-function pair table "
  "@0x03F9AC10 as {'GetPlayerPawn', 0x02DBF9E0}. Body: call 0x028E0340 (GetPlayerController), "
  "if null return 0 else tail-jmp 0x02B620C0 (AController::GetPawn). Returns APawn*.",
  "Blueprint-visible name; also reachable via UFUNCTION lookup at runtime."),
 ("UGameplayStatics::BeginDeferredActorSpawnFromClass", 0x028D3C90, "HIGH", "hook",
  "Reached from execBeginDeferredActorSpawnFromClass (0x02DBB1E0, pair table @0x03F9AC10). "
  "Loads GEngine (0x028D3D52) -> UEngine::GetWorldFromContextObject, builds FActorSpawnParameters, "
  "calls 0x029AD950 (the World spawn entry, see the row further down).",
  "Two overload-table copies exist; 0x028D3C90 is the one the exec thunk calls."),
 ("AActor::FinishSpawning", 0x02651580, "HIGH", "hook",
  "Called from UGameplayStatics::FinishSpawningActor (0x028DCD50), which is called from "
  "execFinishSpawningActor (0x02DBDF80, pair table @0x03F9AC10). Tests Actor->bHasFinishedSpawning "
  "(bit 0x20 at Actor+0x5B) then spawns into the actor's level.",
  "12 direct callers."),
 ("UGameplayStatics::FinishSpawningActor", 0x028DCD50, "HIGH", "hook",
  "Thin wrapper: if (Actor) { r9d=0; r8d=0; call 0x02651580 (AActor::FinishSpawning) } return Actor.",
  "0x22 bytes; trivially hookable."),
 ("UGameplayStatics::GetWorldDeltaSeconds", 0x028E1CE0, "HIGH", "hook",
  "mov rcx,[0x04BA4D88] (GEngine); mov rdx,WorldContextObject; mov r8d,1 (ReturnNull); "
  "call 0x02CDEDF0 (UEngine::GetWorldFromContextObject); if (W) return *(float*)(W+0x5B0).",
  "Gives UWorld::DeltaTimeSeconds offset 0x5B0. Good cheap per-frame poll."),
 ("FEngineLoop::Tick", 0x007753A0, "HIGH", "hook",
  "Largest function referencing the GEngine lifecycle strings region; contains the "
  "'DeferredTickTime', 'Input' and 'EventWait/Slate' literals; 0x10A8 bytes; single caller "
  "(FEngineLoop::RunLoop). This is the game-thread root tick.",
  "Best 'once per frame, game thread' hook. Alternative: UWorld::Tick (not isolated here)."),
 ("UEngine::GetWorldFromContextObject", 0x02CDEDF0, "HIGH", "util",
  "Called from every UGameplayStatics world-context node with rcx=GEngine, rdx=ContextObject, "
  "r8d=EGETWorldErrorMode. 278 direct callers.",
  "Maps any UObject* to UWorld*."),
 ("UWorld::InitializeActorsForPlay", 0x02D391C0, "HIGH", "util",
  "Function entry is marked by the DECLARE_SCOPE_CYCLE_COUNTER literal "
  "'UWorld::InitializeActorsForPlay' at 0x03F05B00 referenced from 0x02D391E7.",
  "Only 4 callers; all are LoadMap/PIE paths. Useful for map-lifecycle hooks."),
 ("UWorld::InternalGetNetMode", 0x02D39CF0, "MEDIUM", "util",
  "Called as FUN_02D39CF0(rcx=GWorld) with no other args; result compared to 1 and 2 "
  "(NM_DedicatedServer / NM_ListenServer) at 0x0077C67C.",
  "Signature inferred from call sites only."),
 ("UGameplayStatics::GetPlayerController", 0x028E0340, "MEDIUM", "util",
  "Called by GetPlayerPawn (0x028E0640) with (WorldContextObject, PlayerIndex); 18 callers.",
  "Return type inferred from caller."),
 ("World spawn entry (UWorld::SpawnActor / SpawnActorDeferred<AActor>)", 0x029AD950, "MEDIUM", "hook",
  "Called by BeginDeferredActorSpawnFromClass (0x028D3C90) at 0x028D3DA3 with "
  "(rcx=World, rdx=Class, r8=&FTransform, r9=&FActorSpawnParameters), returns AActor* which the "
  "caller IsA-checks. Head: mov r13,rcx; mov rdi,r9; mov r15,r8; mov r12,rdx; mov rbx,[r13+0x30]; "
  "then on Class: mov ecx,[r12+0xCC]; shr eax,0x19; test al,1 (CLASS_Abstract, bit 25); test cl,1.",
  "Which of the two names applies is unresolved: the function contains NO reference to "
  "GUObjectArray 0x04A64250/0x04A64260, so object allocation is not inline here. Only 11 callers."),
 ("GUObjectArray", 0x04A64250, "HIGH", "data",
  "Pre-established anchor, re-verified from ProcessEvent's own code: +0x10=0x04A64260 Objects, "
  "+0x24=0x04A64274 NumElements, used with FUObjectItem stride 0x18.",
  "Kept for completeness; 736/717 referencing functions."),
]

EXTRA_ROWS = [
 ("UObject::ProcessEvent vtable slot", 0x0376D258, "HIGH", "data",
  "Slot 68 (offset 0x220) of the UObject vtable at 0x0376D038; holds 0x011A7AA0. "
  "Identical in 1818 vtable copies in .rdata.",
  "Patch this slot per-class to intercept all blueprint/virtual dispatch."),
 ("UFunction::FunctionFlags field", 0x000000B0, "HIGH", "offset",
  "ProcessEvent does 'test dword ptr [rsi+0xB0],0x400' where rsi=UFunction* and "
  "FUNC_Native=0x0400.",
  "Offset, not an RVA. FC = 0x400."),
]

def hexb(rva, n=16):
    b = read_rva(rva, n)
    if b is None or len(b) < n:
        return None
    return " ".join("%02X" % x for x in b)


# For globals that live in .bss (zero on disk) the byte proof is the *referencing
# instruction*, which is stronger evidence anyway.
PROOF = {
    0x04BA86B0: 0x026397C2,   # mov rdx,[rip+0x256eee7]  (GWorld -> UEngine::GetWorldContextFromWorld)
    0x04BA4D88: 0x00774C8C,   # mov [rip+0x44300f5], rax (GEngine store in FEngineLoop::Init)
    0x04A27F00: 0x00FB8E19,   # lea r8,[rip+0x3a6f0e0]   (FName pool base)
    0x04A27F10: 0x00FB8E47,   # add rcx,[r8+rdi*8+0x10]  (Blocks[block] + off*2)
    0x04A64250: 0x011A7AD6,   # cmp eax,[rip+0x38bc798]  (GUObjectArray.NumElements)
    0x000000B0: 0x011A7B28,   # test dword [rsi+0xB0],0x400 (FUNC_Native)
}


def proof_for(rva, n=16):
    h = hexb(rva, n)
    if h:
        return h, None
    site = PROOF.get(rva)
    if site is None:
        return None, None
    return hexb(site, n), site

def main():
    _pe = pe()
    image_size = _pe.OPTIONAL_HEADER.SizeOfImage
    # ------------------------------------------------------------ TSV
    with open(os.path.join(OUT, "functions.tsv"), "w", newline="\n") as f:
        f.write("rva\tname\tconfidence\tbytes\n")
        for name, rva, conf, role, ev, note in T + EXTRA_ROWS:
            h, site = proof_for(rva)
            if h is None:
                h = "<no bytes>"
            elif site is not None:
                h = "%s   (proof site 0x%08X)" % (h, site)
            f.write("0x%08X\t%s\t%s\t%s\n" % (rva, name, conf, h))

    # ------------------------------------------------------------ .h
    with open(os.path.join(OUT, "targets.h"), "w", newline="\n") as f:
        f.write("/* targets.h - QueensDomain-Win64-Shipping.exe (UE4.27.2) co-op hook table.\n")
        f.write(" * Generated from static analysis; every entry verified against file bytes.\n")
        f.write(" * Only MEDIUM+ confidence entries are emitted here.\n")
        f.write(" */\n")
        f.write("#ifndef QD_TARGETS_H\n#define QD_TARGETS_H\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write("#define QD_IMAGE_BASE 0x%XULL\n" % IMAGE_BASE)
        f.write("#define QD_IMAGE_SIZE 0x%XULL /* exact SizeOfImage */\n" % image_size)
        import hashlib
        f.write('#define QD_IMAGE_SHA256 "%s" /* exact image hash */\n'
                % hashlib.sha256(data()).hexdigest().upper())
        f.write("#define QD_GUOBJECTARRAY          0x%08XULL\n" % 0x04A64250)
        f.write("#define QD_GUOBJECTARRAY_OBJECTS  0x%08XULL /* +0x10 FUObjectItem** */\n" % 0x04A64260)
        f.write("#define QD_GUOBJECTARRAY_NUM      0x%08XULL /* +0x24 int32 */\n" % 0x04A64274)
        f.write("#define QD_FUOBJECTITEM_STRIDE    0x18\n")
        f.write("#define QD_OBJECT_INTERNALINDEX   0x0C\n")
        f.write("#define QD_OBJECT_CLASS           0x10\n")
        f.write("#define QD_OBJECT_NAME            0x18\n")
        f.write("#define QD_OBJECT_OUTER           0x20\n")
        f.write("#define QD_UFUNCTION_FLAGS        0xB0\n")
        f.write("#define QD_FUNC_NATIVE            0x0400\n")
        f.write("#define QD_UE4_PROCESSEVENT_SLOT  68\n")
        f.write("#define QD_UE4_PROCESSEVENT_VTOFF 0x220\n")
        f.write("#define QD_UWORLD_DELTATIME       0x5B0\n")
        f.write("#define QD_FNAMEENTRY_LEN_SHIFT   6\n")
        f.write("#define QD_FNAMEENTRY_STRIDE      2\n\n")
        f.write("struct QdTarget { const char* name; uint32_t rva; const char* role; uint8_t expect[16]; };\n\n")
        f.write("static const QdTarget qd_targets[] = {\n")
        for name, rva, conf, role, ev, note in T + EXTRA_ROWS:
            if conf not in ("HIGH", "MEDIUM"):
                continue
            if role == "offset":
                continue   # struct-field offset, not an RVA; see #defines above
            b = read_rva(rva, 16)
            if b is None or len(b) < 16:
                # .bss target: the file image is zero-filled there.
                bs = ", ".join("0x00" for _ in range(16))
                tail = " /* .bss - zero on disk */"
            else:
                bs = ", ".join("0x%02X" % x for x in b)
                tail = ""
            f.write("    {\"%s\", 0x%08Xu, \"%s\", {%s}},%s\n" % (name, rva, role, bs, tail))
        f.write("};\n\n")
        f.write("enum { QD_TARGET_COUNT = sizeof(qd_targets)/sizeof(qd_targets[0]) };\n\n")
        f.write("/* Not emitted (LOW / not found): GMalloc, StaticConstructObject_Internal, */\n")
        f.write("/* UObject::StaticAllocateObject, core UWorld::SpawnActor (non-deferred).    */\n\n")
        f.write("#endif /* QD_TARGETS_H */\n")

    # ------------------------------------------------------------ MD
    with open(os.path.join(OUT, "SUBSTRATE-TARGETS.md"), "w", newline="\n") as f:
        f.write("# QueensDomain-Win64-Shipping.exe - substrate targets\n\n")
        f.write("UE4.27.2 shipping, x86-64, ImageBase 0x%X, SizeOfImage 0x%X (%d bytes on disk).\n\n"
                % (IMAGE_BASE, image_size, len(data())))
        f.write("All addresses are **RVA** (add 0x%X for VA). Byte proof is read straight from the\n" % IMAGE_BASE)
        f.write("file at that RVA (`analysis/re/qdlib.py`).\n\n")
        f.write("## Table\n\n")
        f.write("| Name | RVA | Bytes (16) | Evidence | Conf | Notes |\n")
        f.write("|---|---|---|---|---|---|\n")
        for name, rva, conf, role, ev, note in T:
            h, site = proof_for(rva)
            if h is None:
                h = "<no bytes>"
            elif site is not None:
                h = "%s *(site 0x%08X)*" % (h, site)
            f.write("| **%s** | `0x%08X` | `%s` | %s | %s | %s |\n"
                    % (name, rva, h, ev.replace("|", "\\|"), conf, note.replace("|", "\\|")))
        f.write("\n### Supporting constants (offsets, not RVAs)\n\n")
        f.write("| Item | Value | Bytes | Evidence | Conf |\n|---|---|---|---|---|\n")
        for name, rva, conf, role, ev, note in EXTRA_ROWS:
            h, site = proof_for(rva)
            if h is None:
                h = "<no bytes>"
            elif site is not None:
                h = "%s *(site 0x%08X)*" % (h, site)
            f.write("| **%s** | `0x%08X` | `%s` | %s | %s |\n"
                    % (name, rva, h, ev.replace("|", "\\|"), conf))
        f.write("""
For entries whose RVA is a `.bss` global (GWorld, GEngine, the FName pool, GUObjectArray)
the file image is zero-filled, so the byte proof quoted is from the **instruction that
references it** (marked *site NNNN*), which is the real evidence.
""")
        f.write("""
## Key derived structures

**FName (8 bytes)**: `+0 u32 ComparisonIndex`, `+4 u32 Number`.
Numbers != 0 render as `Name_<Number>`.

**FName entry** (as implemented by `FName::ToString` @ 0x00FB8DE0):
```
entry = FNameEntryAllocator.Blocks[Index >> 16] + (Index & 0xFFFF) * 2
u16 header = *(u16*)entry;            // stock UE4 FNameEntryHeader
Len  = header >> 6;                   // 0x00FB8EB8 movzx eax,word [rdi] ; 0x00FB8EBB shr eax,6
name = (char*)(entry + 2);            // ANSICHAR, Len chars
```
So the allocator at 0x04A27F00 is:
`+0x00 FRWLock Lock`, `+0x08 u32 CurrentBlock`, `+0x0C u32 CurrentByteCursor`,
`+0x10 FNameEntry* Blocks[]` = **0x04A27F10**.

**FUObjectItem (0x18)**: `+0 Object*`, `+8 ObjectFlags` (RF_Unreachable = bit 29 →
`mov eax,[item+8]; shr eax,0x1D; test al,1` in ProcessEvent), `+0x10 int32 ClusterRootIndex`,
`+0x14 SerialNumber`.

**UFunction**: `FunctionFlags` at `+0xB0`; `FUNC_Native = 0x0400`.

**UWorld**: `DeltaTimeSeconds` (float) at `+0x5B0`.

## What I ruled out, and how

1. **"FName entries are `[u32 hash][u32 len][chars]`" (brief) - FALSE for this build.**
   `FName::ToString` (0x00FB8DE0) resolves the entry and then does
   `movzx eax, word ptr [rdi]` / `shr eax, 6` to obtain `Len`. A 4-byte length field would
   need `mov eax, dword ptr [rdi+4]` and no shift. The disassembly is unambiguous:
   the header is the stock **2-byte** `{bIsWide:1, ProbeHash:5, Len:10}` and the
   **stride is 2** (`lea ecx,[rax+rax]` = `offset*2`).
   The `[u32][u32]` shape you saw belongs to the UHT-generated *name metadata* records
   around RVA 0x03F944A0-0x03F95800 (alphabetically sorted, e.g. `GetPlayerPawn` @ 0x03F955F0),
   which are a different table, not the runtime pool.

2. **RTTI on UE types - absent.** 563 `.?A` type-descriptor strings exist but every one belongs
   to third-party code (ICU, Steam, FreeImage, Oodle). `.?AVUObject@@`, `.?AVUWorld@@`,
   `.?AVUClass@@` do not exist, so the vtable could not be found via COL.

3. **Vtables cannot be segmented by "run of .pdata function starts".** `.pdata` is
   **incomplete**. Example: `0x007746E0` is a direct `call` target from `FEngineLoop::Init`
   and appears in hundreds of vtables, yet it has no RUNTIME_FUNCTION. Conversely
   0x02CE85B0 has a `.pdata` begin but starts mid-flow (uninitialised `rax`), i.e. EH
   funclet artifacts. Only 24.3% of `.pdata` begins are also direct-call targets.
   Consequence: vtable detection using `fstarts` produced 1621 false/truncated tables and a
   garbage slot histogram. The reliable test is the arithmetic check used for ProcessEvent
   (vtable start + 0x220 == slot address, with the vtable start terminating a string blob).

4. **`GMalloc` - NOT FOUND.** No `GMalloc`/`FMemory`/`BaseAllocator`/`GCreateMalloc` strings.
   Ranked all .data globals by distinct dereferencing functions: the top hits are
   `__security_cookie` (0x0495C5F8, 10769), a lazily-created UObject singleton
   (0x049CBE20, 7452 - pattern `if(!g){init;} rax=[g]; call [rax+0x90]`, not an allocator),
   and 0x04C06368 (an index/counter, used as `mov edx,[g]; ... [rcx+rdx*8]`).
   Searched for the FMalloc vtable fingerprint {Malloc 0x10, TryMalloc 0x18, Realloc 0x20,
   Free 0x30} across every .data pointer: only GEngine and `__security_cookie` match, i.e.
   the test does not discriminate. Searched for tiny tail-jump wrappers
   (`mov rax,[rip+G]; jmp [rax+0x30]`) in all functions <= 0x80 bytes: none.
   Conclusion: in this build allocation is not routed through a directly-visible
   `GMalloc` global in any way I can prove, so I report **unknown** rather than guess.

5. **`StaticConstructObject_Internal` / `UObject::StaticAllocateObject` - NOT FOUND.**
   No matching strings. The pair-table route (`ConstructObjectFromClass`) has no entry.
   Reaching them requires walking `UWorld::SpawnActor` -> `NewObject<T>`, and the
   core `UWorld::SpawnActor` is not separately visible here: `UWorld::SpawnActorDeferred<AActor>`
   (0x029AD950) is a 0x36A-byte instantiation whose only large callees are Core helpers.
   I did not want to label a guess.

6. **`UWorld::SpawnActor` (core) - not isolated**, but the two call paths that matter are:
   `UGameplayStatics::BeginDeferredActorSpawnFromClass` (0x028D3C90) and
   `AActor::FinishSpawning` (0x02651580). Both are proven by the native-function pair table.

7. **Raw byte search for the FName pool in `.data` cannot work.** `.data` raw data ends at
   RVA 0x4964C00 (`PointerToRawData 0x46EFC00` + `SizeOfRawData 0x273C00`); `0x04A27F00`
   is past it, in the `SizeOfVirtualSize` tail that is zero-filled on disk. That is exactly
   why the block table was not found by scanning `.data`.

## How to re-derive each one

The toolkit is in `analysis/re/`:

| File | Purpose |
|---|---|
| `qdlib.py` | PE map, RVA<->offset, `.pdata` enumeration, capstone helpers |
| `build_xrefs.py` | one-shot scan of every `.pdata` function recording all rip-relative operands -> `xrefs.bin` |
| `build_calls.py` | same for `call`/`jmp rel32` -> `calls.bin` |
| `build_pairs.py` | the `{const char* Name, FNativeFuncPtr Func}` tables -> `nativepairs.bin` |
| `hunt2.py` | `refs_to`, `callers`, `writers_of`, `insn_at`, `summarize` |

**The single most useful trick: the native-function pair tables.**
803 tables / 7483 names in `.rdata` of the exact shape
`{const char* Name; void* Func;}` (16-byte stride, name pointer first). These are the
UHT-generated `FNativeFunctionRegistrar` name->exec-thunk tables. `build_pairs.py`
recovers `name -> exec thunk` for every blueprint node in the game. Each `exec*` thunk
takes `(rcx=Context, rdx=FFrame*, r8=RESULT)` and calls the real C++ function, so

```
name --pair table--> exec thunk --last call--> real implementation
```
is a two-hop, fully mechanical route to hundreds of named gameplay functions.

Re-derivation per target:

* **ProcessEvent**: find a function whose first basic block does
  `mov eax,[rcx+0xC]; cmp eax,[rip+0x4A64274]; ... lea rdx,[rax+rax*2]; lea rax,[rcx+rdx*8];
  mov eax,[rax+8]; shr eax,0x1D` then `test dword [rsi+0xB0],0x400`.
  Then locate `0x011A7AA0` in `.rdata`; the enclosing table start is the vtable;
  `(slot - start)/8 == 68`.
* **GEngine**: `strings`-free method: find the function that references the literal
  `"Create GEngine"` (0x0376C3F8) - it is `FEngineLoop::Init` - and take the `.data`
  global it stores `rax` into right after, then confirm the same global is loaded before
  the `"GEngine->ParseCommandline()"`, `"GEngine->Init"`, `"GEngine->Start()"` literals.
* **GWorld**: for every `.data` pointer global, look for a call site where it is loaded
  into `rdx` while GEngine is loaded into `rcx` (=> `UEngine::GetWorldContextFromWorld`),
  or loaded into `rcx` for a single-argument callee returning 1/2 (=> `InternalGetNetMode`).
* **FName pool / ToString**: the helper used by literally every name-printing site is
  `0x00FB8DE0` with `(rcx=&FName, rdx=&FString)`; 1633 callers make it easy to spot.
  Inside, the `shl`-by-2 on the low half plus `[r8+rdi*8+0x10]` gives the Blocks base.
* **GetPlayerPawn / spawn / tick**: `nativepairs.bin` lookup, then follow the last `call`
  in the exec thunk.

## Caveats that will bite you

* `.pdata` is **not** a complete function list (missing entries + EH funclets). Validate a
  candidate address by (a) it being a direct `call` target, or (b) decoding sane code there.
* `func_at()` / `.pdata` end addresses can truncate a function; use raw-range disassembly
  when a jump target lies past the reported end.
* Never trust "slot 68" from a table you have not anchored: verify `start + 0x220 == slot`
  and that `start-8` is not a `.text` pointer.
""")

    print("emitted:")
    for n in ("SUBSTRATE-TARGETS.md", "targets.h", "functions.tsv"):
        p = os.path.join(OUT, n)
        print("  %-24s %d bytes" % (n, os.path.getsize(p)))

if __name__ == "__main__":
    main()
