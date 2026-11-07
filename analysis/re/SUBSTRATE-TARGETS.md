# QueensDomain-Win64-Shipping.exe - substrate targets

UE4.27.2 shipping, x86-64, ImageBase 0x140000000, SizeOfImage 0x5043000 (81374720 bytes on disk).

All addresses are **RVA** (add 0x140000000 for VA). Byte proof is read straight from the
file at that RVA (`analysis/re/qdlib.py`).

## Table

| Name | RVA | Bytes (16) | Evidence | Conf | Notes |
|---|---|---|---|---|---|
| **UObject::ProcessEvent** | `0x011A7AA0` | `40 55 56 57 41 54 41 55 41 56 41 57 48 81 EC F0` | Reads UObject+0x0C (InternalIndex), bounds-checks vs 0x4A64274 (GUObjectArray.NumElements), indexes 0x4A64260 (ObjObjects.Objects) with FUObjectItem stride 0x18 (lea rdx,[rax+rax*2]; lea rax,[rcx+rdx*8]), tests FUObjectItem.ObjectFlags>>29 (RF_Unreachable), then test dword [Function+0xB0],0x400 (FUNC_Native). Exactly UE4 ProcessEvent. | HIGH | Virtual, slot 68 / vtable offset 0x220 on every UObject-derived vtable (1818 identical entries in .rdata). Example vtable start 0x0376D038; its slot 68 sits at 0x0376D258 = 0x0376D038+0x220 and the byte before 0x0376D038 terminates the UTF-16 string pool, proving the vtable start. |
| **GWorld** | `0x04BA86B0` | `48 8B 15 E7 EE 56 02 48 85 D2 0F 84 5C 01 00 00 *(site 0x026397C2)*` | Loaded and passed as rcx to FUN_02D39CF0 (UWorld::InternalGetNetMode) in FUN_0077C620; loaded as rdx (UWorld*) together with GEngine as rcx in FUN_026397A0 -> UEngine::GetWorldContextFromWorld. Set to 0 in FUN_02D46A8B (world teardown) and assigned in FUN_02CE85B0 (UEngine::LoadMap). | HIGH | 8-byte pointer in .bss region of .data. Read by FEngineLoop::Tick (0x007753A0). |
| **GEngine** | `0x04BA4D88` | `48 89 05 F5 00 43 04 48 85 C9 74 05 E8 13 94 73 *(site 0x00774C8C)*` | Written at 0x00774C8C in FUN_00774A70 (FEngineLoop::Init) immediately after the 'Create GEngine' log string, and re-loaded at 0x00774CEB right after the 'GEngine->ParseCommandline()' string, at 0x00774DF9 ('GEngine->Init') and 0x00774FEE ('GEngine->Start()'). Independently: UGameplayStatics::GetWorldDeltaSeconds (0x028E1CE0) loads it as rcx=UEngine* and calls 0x02CDEDF0 (UEngine::GetWorldFromContextObject). | HIGH | 1164 rip-relative references / 896 distinct functions. |
| **FNameEntryAllocator (FNamePool)** | `0x04A27F00` | `4C 8D 05 E0 F0 A6 03 EB 16 48 8D 0D D7 F0 A6 03 *(site 0x00FB8E19)*` | FName::ToString (0x00FB8DE0) does: shr edi,0x10 (block); movzx ecx,ax (offset); lea r8,[0x04A27F00]; ... lea ecx,[rax+rax] (offset*2); add rcx,[r8+rdi*8+0x10]. 40 references, all in Core (0x00FAB..0x00FB9), all with the same lazy-init shape. | HIGH | Layout: +0x00 FRWLock Lock, +0x08 u32 CurrentBlock, +0x0C u32 CurrentByteCursor, +0x10 FNameEntry* Blocks[] -> 0x04A27F10. Resolve: Blocks[i>>16] + (i&0xFFFF)*2. |
| **FNameEntry::Blocks[]** | `0x04A27F10` | `49 03 4C F8 10 E8 5F 97 FF FF 48 8B C3 48 8B 5C *(site 0x00FB8E47)*` | Computed as FNamePool+0x10 from the 'add rcx,[r8+rdi*8+0x10]' in FName::ToString; r8=0x04A27F00. | HIGH | Array of FNameEntry* in .bss (all-zero on disk, filled at runtime). That is why scanning .data for it fails: .data raw data ends at RVA 0x4964C00, the pool lives above that in .bss. |
| **FName::ToString** | `0x00FB8DE0` | `48 89 5C 24 18 48 89 74 24 20 57 48 83 EC 30 8B` | Signature (rcx=&FName, rdx=&FString). Reads FName{+0 ComparisonIndex,+4 Number}; block=idx>>16, off=idx&0xFFFF; entry=Blocks[block]+off*2; Len = *(u16*)entry >> 6 (0x00FB8EB8 movzx eax,word [rdi] / 0x00FB8EBB shr eax,6). 1633 direct callers. | HIGH | This proves the entry header here is the stock 2-byte {bIsWide:1, ProbeHash:5, Len:10}, NOT a 4-byte hash + 4-byte length. See 'ruled out' section. |
| **UGameplayStatics::GetPlayerPawn** | `0x028E0640` | `48 83 EC 28 E8 F7 FC FF FF 48 85 C0 74 0C 48 8B` | Reached from execGetPlayerPawn (0x02DBF9E0) registered in the native-function pair table @0x03F9AC10 as {'GetPlayerPawn', 0x02DBF9E0}. Body: call 0x028E0340 (GetPlayerController), if null return 0 else tail-jmp 0x02B620C0 (AController::GetPawn). Returns APawn*. | HIGH | Blueprint-visible name; also reachable via UFUNCTION lookup at runtime. |
| **UGameplayStatics::BeginDeferredActorSpawnFromClass** | `0x028D3C90` | `48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48` | Reached from execBeginDeferredActorSpawnFromClass (0x02DBB1E0, pair table @0x03F9AC10). Loads GEngine (0x028D3D52) -> UEngine::GetWorldFromContextObject, builds FActorSpawnParameters, calls 0x029AD950 (the World spawn entry, see the row further down). | HIGH | Two overload-table copies exist; 0x028D3C90 is the one the exec thunk calls. |
| **AActor::FinishSpawning** | `0x02651580` | `40 55 56 57 41 56 41 57 48 8D 6C 24 80 48 81 EC` | Called from UGameplayStatics::FinishSpawningActor (0x028DCD50), which is called from execFinishSpawningActor (0x02DBDF80, pair table @0x03F9AC10). Tests Actor->bHasFinishedSpawning (bit 0x20 at Actor+0x5B) then spawns into the actor's level. | HIGH | 12 direct callers. |
| **UGameplayStatics::FinishSpawningActor** | `0x028DCD50` | `40 53 48 83 EC 20 48 8B D9 48 85 C9 74 0B 45 33` | Thin wrapper: if (Actor) { r9d=0; r8d=0; call 0x02651580 (AActor::FinishSpawning) } return Actor. | HIGH | 0x22 bytes; trivially hookable. |
| **UGameplayStatics::GetWorldDeltaSeconds** | `0x028E1CE0` | `48 83 EC 28 48 8B D1 41 B8 01 00 00 00 48 8B 0D` | mov rcx,[0x04BA4D88] (GEngine); mov rdx,WorldContextObject; mov r8d,1 (ReturnNull); call 0x02CDEDF0 (UEngine::GetWorldFromContextObject); if (W) return *(float*)(W+0x5B0). | HIGH | Gives UWorld::DeltaTimeSeconds offset 0x5B0. Good cheap per-frame poll. |
| **FEngineLoop::Tick** | `0x007753A0` | `48 89 4C 24 08 55 53 56 57 41 54 41 55 41 56 41` | Largest function referencing the GEngine lifecycle strings region; contains the 'DeferredTickTime', 'Input' and 'EventWait/Slate' literals; 0x10A8 bytes; single caller (FEngineLoop::RunLoop). This is the game-thread root tick. | HIGH | Best 'once per frame, game thread' hook. Alternative: UWorld::Tick (not isolated here). |
| **UEngine::GetWorldFromContextObject** | `0x02CDEDF0` | `48 89 5C 24 18 56 48 83 EC 40 41 8B D8 48 8B F2` | Called from every UGameplayStatics world-context node with rcx=GEngine, rdx=ContextObject, r8d=EGETWorldErrorMode. 278 direct callers. | HIGH | Maps any UObject* to UWorld*. |
| **UWorld::InitializeActorsForPlay** | `0x02D391C0` | `48 89 5C 24 10 44 88 44 24 18 55 56 57 41 54 41` | Function entry is marked by the DECLARE_SCOPE_CYCLE_COUNTER literal 'UWorld::InitializeActorsForPlay' at 0x03F05B00 referenced from 0x02D391E7. | HIGH | Only 4 callers; all are LoadMap/PIE paths. Useful for map-lifecycle hooks. |
| **UWorld::InternalGetNetMode** | `0x02D39CF0` | `40 53 48 81 EC 90 00 00 00 48 8B D9 48 8B 49 38` | Called as FUN_02D39CF0(rcx=GWorld) with no other args; result compared to 1 and 2 (NM_DedicatedServer / NM_ListenServer) at 0x0077C67C. | MEDIUM | Signature inferred from call sites only. |
| **UGameplayStatics::GetPlayerController** | `0x028E0340` | `48 89 5C 24 08 57 48 83 EC 30 8B FA 41 B8 01 00` | Called by GetPlayerPawn (0x028E0640) with (WorldContextObject, PlayerIndex); 18 callers. | MEDIUM | Return type inferred from caller. |
| **World spawn entry (UWorld::SpawnActor / SpawnActorDeferred<AActor>)** | `0x029AD950` | `40 55 53 56 57 41 54 41 55 41 56 41 57 48 8D AC` | Called by BeginDeferredActorSpawnFromClass (0x028D3C90) at 0x028D3DA3 with (rcx=World, rdx=Class, r8=&FTransform, r9=&FActorSpawnParameters), returns AActor* which the caller IsA-checks. Head: mov r13,rcx; mov rdi,r9; mov r15,r8; mov r12,rdx; mov rbx,[r13+0x30]; then on Class: mov ecx,[r12+0xCC]; shr eax,0x19; test al,1 (CLASS_Abstract, bit 25); test cl,1. | MEDIUM | Which of the two names applies is unresolved: the function contains NO reference to GUObjectArray 0x04A64250/0x04A64260, so object allocation is not inline here. Only 11 callers. |
| **GUObjectArray** | `0x04A64250` | `3B 05 98 C7 8B 03 4D 8B F8 48 8B F2 4C 8B E1 41 *(site 0x011A7AD6)*` | Pre-established anchor, re-verified from ProcessEvent's own code: +0x10=0x04A64260 Objects, +0x24=0x04A64274 NumElements, used with FUObjectItem stride 0x18. | HIGH | Kept for completeness; 736/717 referencing functions. |

### Supporting constants (offsets, not RVAs)

| Item | Value | Bytes | Evidence | Conf |
|---|---|---|---|---|
| **UObject::ProcessEvent vtable slot** | `0x0376D258` | `A0 7A 1A 41 01 00 00 00 20 6A 78 40 01 00 00 00` | Slot 68 (offset 0x220) of the UObject vtable at 0x0376D038; holds 0x011A7AA0. Identical in 1818 vtable copies in .rdata. | HIGH |
| **UFunction::FunctionFlags field** | `0x000000B0` | `F7 86 B0 00 00 00 00 04 00 00 74 3A 49 8B 04 24 *(site 0x011A7B28)*` | ProcessEvent does 'test dword ptr [rsi+0xB0],0x400' where rsi=UFunction* and FUNC_Native=0x0400. | HIGH |

For entries whose RVA is a `.bss` global (GWorld, GEngine, the FName pool, GUObjectArray)
the file image is zero-filled, so the byte proof quoted is from the **instruction that
references it** (marked *site NNNN*), which is the real evidence.

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

**FUObjectItem (0x18)**: `+0 Object*`, `+8 ObjectFlags` (RF_Unreachable = bit 29 Å®
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
