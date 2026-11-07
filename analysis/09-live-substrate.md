# 09 — Live substrate: UE4SS compatibility and the engine targets

This is the file the mod's Phase 0 is built from. Everything here was **measured on the
running game**, not inferred.

---

## 1. Headline: UE4SS 3.0.1 works on this game, unmodified

No custom game config, no signature override. UE4SS's `patternsleuth` scanner resolved the
engine in **237 ms**:

```
[PS] Reading image
[PS] Starting scan
[PS] Scan finished in 237.4811ms
[PS] Found EngineVersion: 4.27
[PS] Found GUObjectArray: 0x7ff6f3cd4250
[PS] Found GMalloc: 0x7ff6f3c4fb30
[PS] Found FName::ToString: 0x7ff6f0228de0
[PS] Found FName::FName(wchar_t*): 0x7ff6f021bf20
[PS] Found StaticConstructObject_Internal: 0x7ff6f041f050
[PS] Found FText::FText(FString&&): 0x7ff6f013d300
PS scan successful
```

Also confirmed working in the same run:

- `HookProcessEvent`, `HookBeginPlay`, `HookInitGameState` enabled by default and the game
  did not die.
- `ConsoleEnablerMod` resolved the console key (`F10`), so the game **has a console binding**
  and a `GameViewport`/`ViewportConsole` was reachable.
- `BPModLoaderMod`, `ConsoleCommandsMod`, `Keybinds` all started.

**This retires the biggest open risk.** The engine line is hookable by the standard tool,
which means Phase 0 does not have to invent a bootstrap.

> ⚠️ It does **not** mean ship UE4SS. DollsMP removed it for good reasons (no sockets, a
> 525 s `ForEachUObject`, ghost objects that crash on touch, four distinct crash families).
> UE4SS is the **discovery instrument**; the shipping mod should be the native proxy.
> Note that this run also reproduced one of those failure modes: a Lua probe that touched a
> `FindFirstOf` result crashed the game with a `Fatal Error` dialog — the exact
> "non-nil but invalid object" trap DollsMP documented.

## 2. The substrate targets, with RVAs

Module base in that run was `0x7ff6EF270000` (derived, see §3). RVAs are what the mod needs:

| Target | RVA | How it was established | Confidence |
|---|---|---|---|
| `GUObjectArray` | **`0x4A64250`** | UE4SS pattern scan **and** independently by Frida structural scan | **HIGH** |
| `GMalloc` | **`0x49DFB30`** | UE4SS pattern scan | HIGH |
| `FName::ToString` | **`0xFB8DE0`** | UE4SS pattern scan | HIGH |
| `FName::FName(wchar_t*)` | **`0xFABF20`** | UE4SS pattern scan | HIGH |
| `StaticConstructObject_Internal` | **`0x11AF050`** | UE4SS pattern scan | HIGH |
| `FText::FText(FString&&)` | **`0xECD300`** | UE4SS pattern scan | HIGH |
| **`FNamePool` object** | **`0x4A27F00`** | capstone disassembly of `FName::ToString` | **HIGH** |
| **`FNamePool.Blocks[]`** | **`0x4A27F10`** (= pool + 0x10) | same disassembly | **HIGH** |

### 2.1 The two independent confirmations of GUObjectArray

This matters because a wrong global returns plausible garbage instead of faulting.

1. **Frida, structural scan** of `.data`: found `0x7ff7eaf94250` in one process; the module
   base there was `0x7ff7e6530000`, giving `0x4A64250`.
2. **UE4SS patternsleuth**, different process, different base (`0x7ff6f3cd4250` with base
   `0x7ff6EF270000`), giving `0x4A64250`.

Two unrelated methods, two processes, same RVA. That is the standard this project should
hold every target to.

### 2.2 `FUObjectArray` / `UObject` layout, verified live

```
FUObjectArray
  +0x10  ObjObjects.Objects   (FUObjectItem**)
  +0x20  MaxElements          (i32)
  +0x24  NumElements          (i32)
  +0x28  MaxChunks            (i32)
  +0x2C  NumChunks            (i32)

FUObjectItem stride 0x18:  +0x00 UObject*, +0x08 Flags, +0x0C ClusterRootIndex

UObject
  +0x00  vtable          (checked: lands inside the module on every accepted object)
  +0x08  ObjectFlags
  +0x0C  InternalIndex
  +0x10  Class           (UClass*)
  +0x18  Name            (FName: i32 comparison index, i32 number)
  +0x20  Outer           (UObject*)
```

`NumElements` measured:

| State | NumElements |
|---|---|
| Title screen | **89,189** |
| Gameplay, one level loaded | **491,540 – 509,285** (grows as it streams) |

### 2.3 `FName` — the layout that actually ships here

Two facts, both derived from disassembly rather than from documentation, and both
**different from the common UE4 notes**:

**(a) Entry decoding.** `FName::ToString` calls a helper at RVA `0xFB25B0` which does:

```asm
movzx edi, word ptr [rcx]   ; header  = u16 at entry+0
lea   rsi, [rcx + 2]        ; name    = entry+2
movzx eax, di
shr   edi, 6                ; LEN     = header >> 6
and   ax, 1                 ; WIDE    = header & 1
```

So: **`Len = header >> 6`**, **`bIsWide = header & 1`**, **name at `entry + 2`**, and the
name is **not** NUL-terminated (the length is authoritative).

Verified against real memory — block 0 begins:

```
offset 0   : 1e 01 | "None"                header 0x011E >> 6 = 4  = strlen("None")
offset 6   : 10 03 | "ByteProperty"        header 0x0310 >> 6 = 12
offset 20  : c0 02 | "IntProperty"         header 0x02C0 >> 6 = 11
offset 34  : 34 03 | "BoolProperty"        header 0x0334 >> 6 = 12
offset 48  : 76 03 | "FloatProperty"       header 0x0376 >> 6 = 13
```

**(b) Entry addressing.** From `FName::ToString`:

```asm
lea r8, [rip + 0x3a6f0e0]            ; r8 = FNamePool  (RVA 0x4A27F00)
...
shr edi, 0x10                        ; block = comparisonIndex >> 16
add rcx, [r8 + rdi*8 + 0x10]         ; entry = Blocks[block] + <offset>
```

`Blocks[]` is at **pool + 0x10**, indexed by `comparisonIndex >> 16`.

**This is now solved, and the answer is `2 ×`.** The instruction `lea ecx, [rax + rax]` in
`FName::ToString` means the low 16 bits of the index are a count of **2-byte units**, so:

```
entry = Blocks[idx >> 16] + 2 * (idx & 0xFFFF)
```

With that single change the resolve rate over a 60,000-object sample went to
**46,819 / 46,819 = 100 %**, producing `Object`, `Package`, `Class`, `/Script/CoreUObject`,
`SkeletalMeshComponent`, `FS_Full_PlayerChar_C`, …

The earlier failures were **not** block arithmetic — they were **wide (UTF-16) names**,
which the `header & 1` bit selects, plus the missing `2×`. Both are now handled.

> Independently confirmed by the static pass, which reached the same conclusion from the
> same three instructions: stock 2-byte header, `Len = header >> 6`, **stride 2**.
> It also notes that `.data`'s raw bytes end at RVA `0x4964C00` — the pool at `0x4A27F00`
> lives in the zero-filled `VirtualSize` tail, which is why scanning the *file* for it can
> never work and it must be found from code.

## 2.4 The runtime globals — found live, then confirmed statically

These were found using the now-working `FName` decoder: scan `.data` for a qword that
points at a live `UObject`, then resolve that object's **class name** and keep the ones
of interest. Self-validating, because a candidate is only accepted if it is a real UObject
of the expected class.

| Target | RVA | Evidence |
|---|---|---|
| **`UObject::ProcessEvent`** | **`0x11A7AA0`** (vtable slot **68**) | see §2.5 — disassembly matches the UE4.27 implementation exactly |
| **`GWorld`** | **`0x4BA4D60`** | `.data` qword → object whose class resolves to `World` |
| (second World slot) | `0x4BA86B0` | same `UWorld*`; one is `GWorld`, the other a second world-context global — the mod should read both and prefer the one whose class is `World` and that is non-null |
| **`GEngine`** | **`0x4BA4D88`** | `.data` qword → object of class `GameEngine` |

`GWorld` at `0x4BA4D60` and `GEngine` at `0x4BA4D88` are **0x28 apart**, which is the
expected shape for adjacent engine globals in `.data` — a consistency check, not a proof.

Scan cost: 56,920 candidate pointers tested, which is the whole qword sweep of `.data`.

### 2.5 Why `0x11A7AA0` is ProcessEvent — the disassembly

```
mov   eax, dword ptr [rcx + 0xc]      ; UObject::InternalIndex   (our +0x0C)
...
movsxd rcx, ecx                       ; block   = idx >> 16
lea   rdx, [rax + rax*2]              ;         = (idx & 0xffff) * 3
mov   rax, qword ptr [rip + ...]      ; GUObjectArray
mov   rcx, qword ptr [rax + rcx*8]    ; Objects[block]
lea   rax, [rcx + rdx*8]              ; + (idx*3) * 8  == idx * 0x18   <-- FUObjectItem stride
mov   eax, dword ptr [rax + 8]        ; FUObjectItem::Flags
shr   eax, 0x1d
test  al, 1                           ; test bit 29 (arrays/GC flags)
...
test  dword ptr [rsi + 0xb0], 0x400   ; UFunction::FunctionFlags & FUNC_Native
```

Every constant matches what we independently measured: `InternalIndex` at `+0x0C`, the
`0x18` item stride, and the `Objects[block]` indirection. The final test is the signature
line of `ProcessEvent` — it asks whether the function is native before invoking it.

**Bonus finding, useful to the mod:** `UFunction::FunctionFlags` is at **`+0xB0`**.
That is how you can tell, for any reflected function, whether it is native and which
flags (BlueprintCallable, etc.) it carries — and it is a cheap validity check before
calling it.

## 3. Deriving the module base

`GUObjectArray`'s logged absolute address minus the RVA the Frida scan produced:

```
0x7FF6F3CD4250 - 0x4A64250 = 0x7FF6EF270000      (page-aligned)
```

Page alignment is itself a check that the RVA is right. All RVAs in §2 were computed this way.

## 4. What this changes for the plan

**Phase 0 shrinks substantially.** It was: "find GObjects, find ProcessEvent, measure the
object stride, prove injection". Now:

- `GUObjectArray`, `GMalloc`, `FName`, `StaticConstructObject_Internal` and the `FNamePool`
  are all located and cross-checked.
- The object model and stride are measured.
- Injection is proven twice (once per method).

**All Phase-0 targets are now closed.** `ProcessEvent` `0x011A7AA0`, `GWorld`
`0x04BA86B0`, `GEngine` `0x04BA4D88`, the tick anchor `FEngineLoop::Tick` `0x007753A0`,
`GetPlayerPawn` `0x028E0640`, and the spawn pair (`0x028D3C90`/`0x029AD950`,
`0x028DCD50`/`0x02651580`). The merged table is §6; `10-gaps-register.md` A1–A15 are all
CLOSED.

## 5. Reproducing this

```
# 1. isolated instance with UE4SS (no change to the real install)
_live\make-inst1.ps1
#    -> junctions Engine and Content\Paks|Movies, hard-links the exe,
#       copies UE4SS beside it, forces ConsoleEnabled=0 / GuiConsoleVisible=0

# 2. seed a windowed, off-screen, low-spec profile  (REQUIRED - otherwise borderless fullscreen)
_live\inst1saved\Saved\Config\WindowsNoEditor\GameUserSettings.ini
#    FullscreenMode=2, 640x360, WindowPosX=2200, WindowPosY=1400

# 3. launch held by a job (or the game dies when the parent console closes)
# 4. park off-screen + mute:   _live\hide-qd.ps1 -Mode offscreen -Mute
# 5. screenshot:               dollsMP\tools\shot.ps1 -ProcessId <pid> -NoScreenFallback
# 6. drive the menu:           _live\qd-input.ps1 -Key 13    (PostMessage, no focus steal)
# 7. read the substrate:       _live\inst1\...\Win64\UE4SS.log  ("[PS] Found ...")
```

Artifacts from this session are in `_live\` and `analysis\re\`.

---

## 6. Complete target table — Phase 0 is closed

Merged result of the live work (this file) and the static pass
(`analysis/re/SUBSTRATE-TARGETS.md`, 17 entries, byte-verified). ImageBase `0x140000000`,
SizeOfImage `0x5043000`.

| Target | RVA | Conf | Source |
|---|---|---|---|
| `GUObjectArray` | `0x04A64250` | HIGH | live (2 methods) + static |
| `FNameEntryAllocator` (FNamePool) | `0x04A27F00` | HIGH | static + live |
| `FNameEntry::Blocks[]` | `0x04A27F10` | HIGH | static + live |
| `FName::ToString` | `0x00FB8DE0` | HIGH | UE4SS + static |
| `FName::FName(wchar_t*)` | `0x00FABF20` | HIGH | UE4SS |
| **`UObject::ProcessEvent`** | **`0x011A7AA0`** | HIGH | static + live disassembly |
| UObject vtable start / slot 68 | `0x0376D038` / `+0x220` | HIGH | static |
| `GEngine` | `0x04BA4D88` | HIGH | static + live |
| **`GWorld`** | **`0x04BA86B0`** | HIGH | static (live also saw `0x04BA4D60`) |
| `GMalloc` | `0x049DFB30` | **single source** | UE4SS only — verify at runtime |
| `StaticConstructObject_Internal` | `0x011AF050` | **single source** | UE4SS only |
| `UGameplayStatics::GetPlayerPawn` | `0x028E0640` | HIGH | static |
| `UGameplayStatics::GetPlayerController` | `0x028E0340` | MEDIUM | static |
| `UGameplayStatics::GetWorldDeltaSeconds` | `0x028E1CE0` | HIGH | static |
| **`FEngineLoop::Tick`** (game-thread root) | `0x007753A0` | HIGH | static |
| `BeginDeferredActorSpawnFromClass` | `0x028D3C90` | HIGH | static |
| `UGameplayStatics::FinishSpawningActor` | `0x028DCD50` | HIGH | static |
| `AActor::FinishSpawning` | `0x02651580` | HIGH | static |
| `UWorld::SpawnActor` entry | `0x029AD950` | MEDIUM | static |
| `UEngine::GetWorldFromContextObject` | `0x02CDEDF0` | HIGH | static |
| `UWorld::InitializeActorsForPlay` | `0x02D391C0` | HIGH | static |
| `UWorld::InternalGetNetMode` | `0x02D39CF0` | MEDIUM | static |

### Structural constants (carry these into the mod)

```
FName (8 B)              +0 u32 ComparisonIndex, +4 u32 Number
FName entry              Blocks[Index>>16] + (Index & 0xFFFF)*2
                         Len = *(u16*)entry >> 6 ; wide = *(u16*)entry & 1 ; chars at entry+2
FUObjectItem             0x18 stride ; ObjectFlags at +0x8 ; RF_Unreachable = bit 29
UObject                  +0 vtable, +0x08 ObjectFlags, +0x0C InternalIndex,
                         +0x10 Class, +0x18 FName, +0x20 Outer
UFunction::FunctionFlags +0xB0 ; FUNC_Native = 0x0400
UWorld::DeltaTimeSeconds +0x5B0 (float)
```

### The accelerator worth knowing about

The static pass found **803 native-function pair tables / 7,483 names** in `.rdata`, of the
shape `{const char* Name; FNativeFuncPtr Func;}`. That gives a mechanical two-hop route to
any blueprint node's C++ implementation:

```
name --> pair table --> exec thunk --> (last call) --> real implementation
```

This is how `GetPlayerPawn`, `FinishSpawningActor` and `BeginDeferredActorSpawnFromClass`
were found without a single internal string. **For the remaining work — especially the
gameplay systems D1–D9 — this is the cheapest way to find any function by name.**

### Caveat carried forward

`.pdata` is **not** a reliable function list on this binary: only 24.3 % of `.pdata` begins
are also direct-call targets, some real functions have no `RUNTIME_FUNCTION` at all, and
some entries decode mid-flow. Vtable detection keyed on `.pdata` produced 1,621 truncated
tables and a garbage slot histogram, and was discarded. Use the pair tables instead.
