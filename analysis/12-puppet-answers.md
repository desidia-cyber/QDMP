# 12 — Puppet answers: B6, B7, B8

Static answers for the three open items in `10-gaps-register.md` §B, derived from the
shipping image only (the game was never launched for this note).

**Image.** `QueensDomain/QueensDomain/Binaries/Win64/QueensDomain-Win64-Shipping.exe`,
81,374,720 bytes, `ImageBase 0x140000000`, `SizeOfImage 0x5043000`. 725 native-function
pair tables / 7,483 names; 242,788 `.pdata` records (unreliable, see *Instruments*).

**Reproduce.** `python analysis/re/puppet_b6_b7_b8.py` re-derives every address and value
below and writes the raw disassembly to `analysis/re/puppet_evidence.txt`.

Confidence vocabulary: **HIGH** = two independent reads agree (e.g. thunk → impl →
vtable slot), **MEDIUM** = one read + standard UE semantics, **LOW** = inference only.

---

## B6 — Does `AFS_PlayerCharacter` / `FS_PlayerCharacter` set `bReplicates` / `bReplicateMovement`?

### Answer

**Yes, both are set, to 1.** They are set by `APawn::APawn`, which is in the class's
constructor chain; neither `ACharacter` nor `AFS_PlayerCharacter` clears them, and no
packaged asset overrides them. `RemoteRole` is `ROLE_SimulatedProxy` (1).

```
bReplicates        = bit 1 (mask 0x02) of byte at AActor + 0x5B   -> 1
bReplicateMovement = bit 4 (mask 0x10) of byte at AActor + 0x58   -> 1
RemoteRole   (+0x5F, byte)                                        -> 1   [inference, HIGH]
Role         (+0xF0, byte)                                        -> 3   [ROLE_Authority]
```
Confidence **HIGH** for the two bit offsets and the values; **MEDIUM-HIGH** for naming
`+0x5F` as `RemoteRole` (the byte is written 0 by the AActor ctor, 1 by APawn, and `dl`
by `SetReplicates`).

### Proven facts (address chain)

1. **`SetReplicates` exec thunk → implementation.**
   `nativepairs.bin` has two copies:
   `{table 0x03F0DA28, exec 0x02D53150}` (calls the Blueprint-VM dispatcher `0x0119D280`,
   useless) and `{table 0x03F0E8C0, exec 0x02D58FE0}` (real).
   `0x02D58FE0` reads the bool parameter and its last direct call is **`0x02664640`**
   (`02D59052 call 0x2664640`).

2. **`AActor::SetReplicates` body `0x02664640`** starts `cmp byte ptr [rcx + 0xF0], 3`
   (Role == `ROLE_Authority`) and then:
   ```
   0266466C  and al, 0xFD          ; clear bit 1
   0266466E  mov byte ptr [rcx + 0x5F], dl
   02664671  add dl, dl            ; dl = bool << 1
   02664673  or  al, dl            ; set bit 1
   02664675  mov byte ptr [rcx + 0x5B], al
   ```
   ⇒ `bReplicates` is **bit 1 of `+0x5B`**.

3. **`SetReplicateMovement` is virtual.** Real exec `0x02D58F50` ends with
   `call qword ptr [rax + 0x298]` (`02D58FC5`) where `rax = [Context]` (the vtable):
   slot `0x298 / 8 = 83`.

4. **The slot-83 body is `0x02664630`.**
   `APawn::APawn` (`0x02AD9010`) installs vtable `0x03EB6240` (`02AD902B` → `[rbx]`), and
   `u64(0x03EB6240 + 83*8) = 0x02664630`. The function is:
   ```
   02664630  and byte ptr [rcx + 0x58], 0xEF   ; clear bit 4
   02664634  shl dl, 4
   02664637  or  byte ptr [rcx + 0x58], dl     ; set bit 4
   0266463A  ret
   ```
   ⇒ `bReplicateMovement` is **bit 4 of `+0x58`**.

5. **`FS_PlayerCharacter` registration.** The literal `/Script/QueensDomain` is at
   `0x039390F8`; the wide class name `FS_PlayerCharacter` is at `0x0395BB92`; the only
   xref is `FUN_00E5E0B0` (`Z_Construct_UClass_AFS_PlayerCharacter`). Its
   `GetPrivateStaticClassBody` call gives `sizeof = 0x520`, `ClassConstructor = 0x00E5EBD0`,
   `SuperFn = 0x00E51590`.

6. **Constructor chain.** The ClassConstructor stub `0x00E5EBD0`
   (`mov rcx,[rcx]; test; jne 0xE1E4C0`) reaches:
   ```
   0x00E1E4C0  AFS_PlayerCharacter::AFS_PlayerCharacter
     -> 0x02747940  ACharacter::ACharacter
       -> 0x02AD9010  APawn::APawn
         -> 0x026452D0  AActor::AActor
   ```
   `APawn::APawn` then does exactly:
   ```
   02AD912E  or  byte ptr [rbx + 0x5B], 2         ; bReplicates = 1
   02AD9137  mov byte ptr [rbx + 0x5F], 1         ; RemoteRole = ROLE_SimulatedProxy
   02AD914F  call 0x02664630   (dl = 1)           ; bReplicateMovement = 1
   ```

7. **Neither later constructor resets them.** A full disassembly of
   `0x00E1E4C0` and `0x02747940` (0x8F / 0x491 bytes) contains **no** access to
   `+0x58` or `+0x5B` at all (the script prints `touches +0x58/+0x5B at: NONE`).

8. **No asset overrides.** `FS_PlayerChar.uasset` (4,556 names) and
   `FS_Full_PlayerChar.uasset` (211 names) contain neither `bReplicates` nor
   `bReplicateMovement` in their name tables. Across all 44,092 pak payloads the literal
   `bReplicateMovement` appears in **0** assets; `bReplicates` appears only in 32 `.umap`
   files (stock engine actor name tables). A cooked BP CDO that set either field would
   have to store the property name; none does.

### Inference vs. fact

- **Fact:** the constructors write those two bits to 1, and the BP assets contain no tag
  that changes them.
- **Inference (standard UE4 delta serialization):** therefore `Default__FS_PlayerCharacter`,
  `Default__FS_PlayerChar_C` and `Default__FS_Full_PlayerChar_C` all carry
  `bReplicates = true`, `bReplicateMovement = true`, `RemoteRole = ROLE_SimulatedProxy`.

### What would falsify this

- A BP CDO export containing an `FPropertyTag` named `bReplicates` / `bReplicateMovement`
  with `BoolVal = 0` (the names are absent from both assets and from the whole pak).
- A `PostInitializeComponents` / `BeginPlay` / cheat path that clears bit 1 of `+0x5B` or
  bit 4 of `+0x58` on the player. The `+0x5B` scan found `and …, 0xFD` only in the AActor
  default-init function `FUN_026599F0` (`0x02659A10`, called by the AActor ctor) and in
  per-class `SetReplicates` implementations; `+0x58` was not exhaustively swept. **This is
  the one remaining hole** and it is about runtime state, not class defaults.

---

## B7 — The game's own spawn/construction path for a configured character

### Answer

**The game module has no native character spawner.** `/Script/QueensDomain` registers
**78** classes with **20** native-function tables, and the only `Spawn*` UFUNCTIONs in
those tables are three spawner **helpers**; there are **zero** `Construct` / `Create` /
`Possess` / `Merge` UFUNCTIONs. Character spawning is engine-standard
`AGameModeBase` virtual dispatch plus Blueprint. The two named candidates
(`AFS_StationarySkeletalMeshActor`, `UFS_ConfigurableStaticMeshComponent`) are visual
configuration classes with **no UFUNCTIONs of their own** — they are not the
`ReceiveMerge` analogue.

**Best hook / spawn path for a remote-player body (recommendation, MEDIUM):**
call `UGameplayStatics::BeginDeferredActorSpawnFromClass` (`0x028D3C90`) with the
`FS_Full_PlayerChar_C` UClass, then `UGameplayStatics::FinishSpawningActor`
(`0x028DCD50`) — the same two primitives the engine exposes to Blueprint. For passive
observation, hook `AActor::FinishSpawning` (`0x02651580`, one funnel, class already
resolved) or `UWorld::SpawnActor` (`0x029AD950`).

### Proven facts

1. **Module census.** 206 `.text` functions reference the literal `/Script/QueensDomain`
   (`0x039390F8`); 78 of them decode as full `GetPrivateStaticClassBody` calls
   (class-name wide string, `sizeof`, `ClassConstructor`); 20 classes carry a native
   function table in `nativepairs.bin`.

2. **The entire Spawn/Construct/Create/Possess/Merge surface.** The pair table has
   544 `(name, table, exec)` rows matching the pattern across the whole image (engine +
   plugins included). Filtering to the 20 game-module tables leaves exactly **three**,
   all spawner helpers:
   | Name | Class | exec thunk |
   |---|---|---|
   | `CheckSpawnerMaxDist` | `BlueprintUtils` | `0x00E46EC0` |
   | `CheckSpawnerVis` | `BlueprintUtils` | `0x00E46F90` |
   | `DoSpawnerUpdate` | `FS_EnemyUtils` | `0x00E521B0` |

   Zero `Construct`, `Create`, `Possess`, `Merge` in the game module.

3. **`AFS_StationarySkeletalMeshActor`** — `Z_Construct = 0x00E5E600`,
   `sizeof = 0x2A8`, ctor stub `0x00E5EBF0` → real ctor **`0x00E1E670`**,
   `RegisterNativeFunc = 0x007746E0` (the empty stub, i.e. no UFUNCTIONs).
   Its constructor calls the `SkeletalMeshActor` base (`0x026D98F0`), installs vtable
   `0x0392BFB0`, and then only tunes the mesh component at `actor+0x230`:
   `and [rax+0x89],0xF7`, `or [rax+0x8C1],0x40`, `and [rax+0x3A],0xFB`,
   `and [rax+0x213],0x7F`, `and [rax+0x213],0xFE`, `or [rax+0x8C1],0x20`,
   `and [rax+0x607],0xFD`, `mov [rax+0x604],3`, then a virtual call `[rax+0x358]`.
   It is a poseable/visible mesh actor, not a character factory.

4. **`UFS_ConfigurableStaticMeshComponent`** — `Z_Construct = 0x00E4A020`,
   `sizeof = 0x4E0`, ctor stub `0x00E4BAD0` → real ctor **`0x00E1EFF0`**,
   `RegisterNativeFunc = 0x007746E0` (no UFUNCTIONs). The ctor only installs
   three vtable pointers and zero-initialises two sub-objects. It is a
   widget/UI-facing configurable mesh component.

5. **Engine character-spawn chain (RVAs + dispatch slots).**
   ```
   AGameModeBase::RestartPlayer                 exec 0x02DC2E50 -> vtable +0x790 (site 0x02DC2EC3)
     AGameModeBase::SpawnDefaultPawnFor         exec 0x02DC4800 -> vtable +0x650 (site 0x02DC48B8)
       AGameModeBase::SpawnDefaultPawnAtTransform exec 0x02DC46E0 -> vtable +0x648 (site 0x02DC47D9)

   UGameplayStatics::BeginDeferredActorSpawnFromClass 0x028D3C90
       -> UWorld::SpawnActor                           0x029AD950   (call site 0x028D3DA3)
   UGameplayStatics::FinishSpawningActor              0x028DCD50
       -> AActor::FinishSpawning                       0x02651580   (call site 0x028DCD64)
   ```
   `UWorld::SpawnActor` is the concrete funnel: it has 11 direct callers, including
   `BeginDeferredActorSpawnFromClass` (`0x028D3C90 @0x028D3DA3`) plus three `0x0310xxxx`
   spawn helpers (`0x03100640`, `0x031007B0`, `0x031008F0`; the last two are
   `BeginSpawningActor` implementations). `AActor::FinishSpawning` has 12 direct callers,
   including `FinishSpawningActor` (`0x028DCD50 @0x028DCD64`).

6. **The live pawn class is the game's own.** `11-live-object-graph.md` measured the
   possessed pawn as `FS_Full_PlayerChar_C`; `B5` lists `CharacterMesh0` (body) and
   `Arms` (FP) on it. That is the class to spawn.

### Inference vs. fact

- **Fact:** no native game function spawns a character, and the two candidate classes are
  not spawners.
- **Inference (MEDIUM):** the Blueprint GameMode reaches the same concrete primitives
  (`BeginDeferredActorSpawnFromClass` / `FinishSpawningActor`, or `UWorld::SpawnActor`)
  through `ProcessEvent`. BP bytecode is not decoded here, so the exact node is not proven.
- **Recommendation (MEDIUM):** spawn `FS_Full_PlayerChar_C` unpossessed. Because
  `bOwnerNoSee` only hides a component from its *owner*, an unowned puppet renders its
  body without touching any flag (and the class has no body mesh assigned in the BP —
  see B8, so a mesh must be assigned to `CharacterMesh0`).

### What would falsify this

- A Blueprint function (invisible to the pair table) that spawns the character through a
  different route — e.g. a container/archetype spawn or a UWorld helper not in the table.
- A native game function whose UFUNCTION table was missed because it has fewer than three
  entries (`nativepairs.bin` only records runs of ≥ 3). The 20 tables above are those the
  pair builder found; a one-entry table would not appear.
- The GameMode overriding `SpawnDefaultPawnAtTransform` in BP so that the concrete call is
  not one of the two primitives. The virtual slots are proven; the override target is not.

---

## B8 — Does the game render the full body anywhere?

### Answer

**Yes — but only in cutscenes, on dedicated cutscene actors. The gameplay pawn never
enables its body, and no code clears `bOwnerNoSee`.**

- `bOwnerNoSee = bit 5 (0x20)` and `bOnlyOwnerSee = bit 6 (0x40)` of `+0x212`.
- The two setters have **exactly one direct caller each: their own exec thunk**. No native
  code calls them.
- The only direct `clear bit 5` in the image is the **ACharacter constructor clearing it on
  the capsule** (`0x02747C6D` inside `0x02747940`) — not on the body mesh.
- **No asset** in the pak contains the literal `SetOwnerNoSee`, `SetOnlyOwnerSee`,
  `bOwnerNoSee` or `bOnlyOwnerSee`.
- The player BP hierarchy assigns the **FP arms** mesh, never `Player_FullChar_2026`; the
  native `CharacterMesh0` has no `SkeletalMesh` in gameplay.
- The full body is rendered by `FS_SequenceCharacter_C` (8 actors in `Intro_Level_00.umap`)
  and by `CutsceneSkeletalMeshActor_C` subclasses (`PrinceCutsceneMesh_C`,
  `QueenCutsceneMesh_C` in `Ending1Cutscene.uasset`), driven by `LevelSequenceActor`s.

### Proven facts

1. **Bit semantics.**
   `SetOwnerNoSee` exec `0x02E8F860` → impl **`0x027D8340`**:
   `shr al, 5` (`0x027D834C`), `and r8b, 0xDF` (`0x027D8355`), store to `[rcx+0x212]`.
   `SetOnlyOwnerSee` exec `0x02E8F7D0` → impl **`0x027D8310`**:
   `shr al, 6` (`0x027D831C`), `and r8b, 0xBF` (`0x027D8325`), store to `[rcx+0x212]`.
   (The `0x02E899xx` pair entries are the dispatcher copy.)

2. **No native caller.**
   `calls.bin` gives `SetOwnerNoSee 0x027D8340 -> callers = 1` (`0x02E8F8D2` in the exec
   thunk `0x02E8F860`) and `SetOnlyOwnerSee 0x027D8310 -> callers = 1`
   (`0x02E8F842` in `0x02E8F7D0`). Nothing else calls them directly.

3. **Direct byte-op writers.**
   A resynchronising linear sweep of `.text` (0x03739800 bytes) found 74 validated
   byte-sized accesses at `+0x212`/`+0x213`:
   | effect | sites |
   |---|---|
   | clear `bOwnerNoSee` (`and +0x212, 0xDF`) | **1** — `0x02747C6D` in the ACharacter ctor (`0x02747940`), on the **capsule** at `[rdi+0x280]` |
   | set `bOwnerNoSee` (`or +0x212, 0x20`) | `0x0285A27F` in `FUN_0285A0A0` (an engine class ctor derived from `APawn`, not on the player chain) and `0x02E89EC0` (a generic bit-setter stub) |
   | set `bOnlyOwnerSee` (`or +0x212, 0x40`) | `0x02A49365` in `FUN_02A49160` and `0x02E89ED0` (a generic bit-setter stub) |

   There is therefore **no code path that clears `bOwnerNoSee` on a body mesh**, and none
   that clears `bOnlyOwnerSee` outside the setter's own register op.

4. **No asset references.** A single streaming pass over the 1.9 GB pak (44,092 payloads)
   found **0 assets** containing `SetOwnerNoSee`, `SetOnlyOwnerSee`, `bOwnerNoSee` or
   `bOnlyOwnerSee`. (Contrast: `bHiddenInGame` appears in 71 assets, `SetVisibility` in 196,
   so the instrument is live.)

5. **The player BP assigns the FP arms, not the body.**
   `FS_PlayerChar.uasset` (4,556 names) contains `/Game/Player/Assets/FPModel/Player_FPArms_Arms`,
   `Arms`, `Arms_GEN_VARIABLE`, `FPArmsComp` — the FP rig.
   `FS_Full_PlayerChar.uasset` (211 names) contains `CharacterMesh0`, `Mesh`,
   `SkeletalMeshComponent`, `Arms_GEN_VARIABLE` but **no mesh asset path at all**.
   Neither asset contains the string `Player_FullChar` (checked: `body-mesh refs = []`).
   The C++ constructors do not set a mesh either. So the native `ACharacter::Mesh`
   (`CharacterMesh0`) is empty in gameplay.

6. **Cutscene actors that do render the body.**
   - `QueensDomain/Content/RuinsLevel/Maps/Intro_Level_00.umap`: **8 × `FS_SequenceCharacter_C`**
     (`FS_SequenceCharacter`, `FS_SequenceCharacter2/3`, `DragonCharacter`,
     `Intro_Boat_SM`, `Prince_Shot8_Character`, `Prince_Shot9_Character`, `Queen_Char`),
     **1 × `LevelSequenceActor`** (`00_Intro_MasterSequence`), 12 × `FB_SequenceCamera_C`,
     13 × `SkeletalMeshComponent`; name refs to `Player_FullChar`,
     `Player_FullChar_2026_coolPose`, `Player_FullChar_2026_coolPose_Animated`.
   - `QueensDomain/Content/Cutscenes/Ending1/Ending1Cutscene.uasset`: `LevelSequence`,
     2 × `MovieSceneSkeletalAnimationTrack`, `PrinceCutsceneMesh_C`, `QueenCutsceneMesh_C`;
     name ref to `Player_FullChar_2026_IntroAnimation`.
   - `QueensDomain/Content/Cutscenes/CutsceneSkeletalMeshActor.uasset`:
     `CutsceneSkeletalMeshActor_C` with super `SkeletalMeshActor`.
   - `Shot8_Prince.uasset` references `Player_FullChar_2026_coolPose`;
     `Shot9_PrinceFlyingSword.uasset` references `Player_FullChar_2026_IntroAnimation`.

### Inference vs. fact

- **Fact:** no code or asset uses the owner-see names to enable the player's body, and the
  player BPs do not assign the body mesh.
- **Inference (MEDIUM-HIGH):** the gameplay `CharacterMesh0` is empty rather than hidden,
  which is why the body does not render. The full-body art is used only by cutscene actors.
- **Cross-check for B4:** the live byte-diff at `0x4F4` cannot be `bOwnerNoSee` in this
  build — the named bit is at `+0x212`, and nothing writes it on the body. `0x4F4` is a
  different field (visibility/render state), so B4's flag still needs naming.

### What would falsify this

- A `bts`/`btr` (or a 32-bit `and`/`or`) on a word covering `+0x212`, which the byte-size
  scan would miss; or a write through a register that aliases the component (block copy,
  property reflection).
- A Blueprint call to `SetOwnerNoSee`/`SetOnlyOwnerSee` whose name is not stored literally
  in the asset (it would be, for a BP call node — the FName is in the name table).
- An asset that assigns a `SkeletalMesh` to `CharacterMesh0` through a component template
  **without** putting the mesh path in the package name table (cooked import tables do
  list it, and the path is absent).

---

## Instruments and what each can and cannot see

| Instrument | Detects | Cannot detect |
|---|---|---|
| `nativepairs.bin` (725 tables / 7,483 names) | native `UFUNCTION` registrations and their exec thunks | Blueprint-defined functions; tables with < 3 entries |
| `.pdata` function bounds | prologue bounds | it truncates real functions on this image (e.g. `0x02664640` ends at `0x02664656` in `.pdata`); the script widens entries shorter than 0x40 bytes |
| `calls.bin` (direct E8/E9) | direct call/tail-jump edges inside `.pdata` functions | virtual dispatch, `ProcessEvent`, Blueprint VM, indirect calls |
| `xrefs.bin` (rip-relative) | data/code references to an address | struct-field offsets |
| `.text` byte-op sweep at `+0x212`/`+0x213` | byte-sized read-modify-writes on that field, with linear resync | wider `bts`/`btr`, aliased/reflected writes, block copies |
| whole-pak literal token scan | exact ASCII token anywhere in an uncompressed payload | names built at runtime; (the pak is uncompressed, so no decompression blind spot) |
| package name-table parse | every name/import the cooked asset stores | property *values* — no `FPropertyTag` reader was written |

No `FPropertyTag` parser was attempted: the player CDO question is already answered from
the C++ constructor chain, and the asset answers here are *name-presence* questions (does
the asset reference this field/setter at all), which the name table settles without
decoding values. A CDO value parser would only be needed to read a *value* that is
serialised as a delta, and the names we care about are absent.

---

## Status

| Item | Status | Key facts | Confidence |
|---|---|---|---|
| **B6** | **CLOSED** | `bReplicates` = bit1/0x02 of `+0x5B` = **1**; `bReplicateMovement` = bit4/0x10 of `+0x58` = **1**; set by `APawn::APawn` `0x02AD9010`; no BP override | **HIGH** |
| **B7** | **CLOSED (negative), recommendation MEDIUM** | no native game spawner (3 spawner helpers only); spawn via `0x028D3C90` + `0x028DCD50` (→ `0x029AD950` / `0x02651580`) on `FS_Full_PlayerChar_C` | **HIGH** for the negative/RVAs; **MEDIUM** for the BP node |
| **B8** | **CLOSED** | body renders only on cutscene actors (`FS_SequenceCharacter_C`, `CutsceneSkeletalMeshActor_C`); no code clears `bOwnerNoSee`; player BP assigns FP arms only | **HIGH** for the code/asset negatives; **MEDIUM-HIGH** for "mesh empty, not hidden" |
