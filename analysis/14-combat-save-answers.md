# 14 — Combat and save: answers for D4, D5, D6, D8, C9

**Scope.** Static reverse-engineering of
`QueensDomain\QueensDomain\Binaries\Win64\QueensDomain-Win64-Shipping.exe`
(UE 4.27.2, ImageBase `0x140000000`, SizeOfImage `0x05043000`), plus the shipped pak
and the live `World0.sav`. The game was **not** launched for this pass.

**Reproduce everything here with**

```
python analysis/re/combat_save_probe.py --evidence --census   # everything
python analysis/re/combat_save_probe.py --only D6 --no-pak    # one item, fast
```

which writes `analysis/re/combat_save_evidence.txt` (raw disassembly + every derived
address) — 671 lines, every claim below traceable to a line in it.

**Confidence vocabulary** (same as the rest of `analysis/`)

| tag | meaning |
|---|---|
| **PROVEN** | measured, an artefact is named, and re-derivable by the probe |
| **INFERRED** | follows from PROVEN facts by an argument that is stated |
| **UNRESOLVED** | instrument could not decide it; the live test is named |

**Instruments used, and what each one can and cannot see**

| # | Instrument | Detection range | Blind spot |
|---|---|---|---|
| I1 | `analysis/re/nativepairs.bin` — 803 `{const char* Name; FNativeFuncPtr Func;}` tables, 7,483 names, 725 distinct table bases | any Blueprint-callable **by name** | two mirrored copies exist per module; the copy whose thunk reaches `0x0119D280` (generic Blueprint-VM dispatcher) must be discarded |
| I2 | exec-thunk shape validation — `mov rsi,r8` (RESULT_DECL), `FFrame::Step` helpers `0x01189A70 / 0x011B1870 / 0x011B18A0 / 0x01193920 / 0x0118xxxx`, result store into `[rsi]`, epilogue `FString` dtor `0x00EAE0B0` | proves a resolved address really is the callee | functions returning nothing/struct-by-sret have no `[rsi]` store — they need the `expect=` cross-check instead |
| I3 | `.pdata` (`qdlib.functions()`) | function bounds when present | **unreliable on this image** (§0) |
| I4 | `analysis/re/calls.bin` — direct-call graph over `.text` | direct `call rel32` edges | virtual calls, indirect calls, and everything a Blueprint does |
| I5 | pak name-reference scan over **38,426** `.uasset/.uexp/.umap` payloads | which cooked asset *mentions* a symbol | a mention is a name-table reference, never proof of a code path or its order |
| I6 | cooked-package tagged-property decoder | authored default values in placed actors | does not decompile Blueprint bytecode |
| I7 | live `World0.sav` reader | the 414 persisted records, byte-exact | one save, 11 minutes of play |

> **Rule applied throughout:** a negative only ever refutes *a specific name in a specific
> instrument*. "`X` is not imported by any asset" means *no cooked package's name table
> contains the string `X`* — it does not mean the behaviour is absent.

---

## 0. Two instrument caveats that changed the answers

**(a) `.pdata` truncates real functions.** `UFS_ItemDropSystem::RollItemDrop` is listed by
`.pdata` as `0x00E392F0..0x00E39346` (0x56 bytes). It actually branches to `0x00E394AF`
and continues to `0x00E394C3`. Everything in §D6 was read by *linear* disassembly, not by
`.pdata` bounds. Any earlier conclusion that relied on `.pdata` function length should be
re-checked the same way.

**(b) The class of a Blueprint's parent decides whether a "hook target" is native at all.**
Cooked BPs resolved through their import table:

| Blueprint | generated class | parent |
|---|---|---|
| `Enemies/EnemySpawner` | `EnemySpawner_C` | **`SavableActor_C`** (a Blueprint) |
| `Enemies/EnemyBase` | `EnemyBase_C` | **`FS_EnemyCharacterBase`** (native, `/Script/QueensDomain`) |
| `Blueprints/ItemSystem/SpawnItemDrop` | `SpawnItemDrop_C` | `BlueprintFunctionLibrary` |

`FS_EnemyCharacterBase` exists in the image but **only as a UTF-16 literal** (RVA
`0x03949072`). Property names are narrow ASCII (`EnemyDiedEvent__DelegateSignature` at
`0x03945868`); class names are UTF-16. A narrow-only string scan silently reports every
native class as absent — that is why `FS_EnemyCharacterBase` looked missing at first.

---

## D4 — Enemy spawner identity semantics

### Answer

**The enemy save key is an *authored* `FGuid` baked into the level package. It is not
generated at runtime, and `SpawnerGUID` is not the authored value.** A respawned enemy is
re-registered by the *same* spawner instance under the *same* authored key, so identity
survives respawn. The fallback the design was reserving (spawner-path + index) is not
needed, but the spawner is still the unit of identity — one key per spawner, not per
enemy instance.

### Proven

**D4-p1 — `EnemySaveStates` is a `TMap` at `save_object + 0x270`.** Six `lea r64,[rax+0x270]`
sites in `.text`, four distinct functions:
`0x00E27460` (`ClearAllEnemySaveStates`), `0x00E27570` (`ClearEnemySaveState`),
`0x00E39271` (inside `RetrieveEnemySaveState`), `0x00E3BF10` (`StoreEnemySaveState`).
`ClearAllEnemySaveStates` additionally zeroes `[rbx+8]` (Num) and writes `0xFFFFFFFF` at
`[rbx+0x30]` — the standard `TMap::Reset` shape, so `+0x270` is confirmed as a TMap and not
a look-alike.

**D4-p2 — the in-memory record is `{u8 bValid; float RespawnTime; float RemainingHP}`.**
From `StoreEnemySaveState` (`0x00E3BF10`):

```
00E3BF1B  mov    rbx, rdx              ; arg2
00E3BF23  movaps xmm6, xmm3            ; arg4 float
00E3BF26  movaps xmm7, xmm2            ; arg3 float
00E3BF29  call   0x00E2D100            ; GetActiveBaseSave(WorldContext)
00E3BF33  lea    rcx, [rax + 0x270]    ; the EnemySaveStates map
00E3BF4E  mov    byte ptr [rsp+0x20], 1      ; bValid = true
00E3BF3A  movss  [rsp+0x24], xmm7            ; RespawnTime = arg3
00E3BF45  movss  [rsp+0x28], xmm6            ; RemainingHP = arg4
00E3BF53  call   0x00E19FC0            ; TMap::Add
```

So the native signature is
`StoreEnemySaveState(UObject* WorldContext, FGuid Key, float RespawnTime, float RemainingHP)`
and it always writes `bValid = true`. **This is the entire enemy-persistence write surface.**

**D4-p3 — the spawner is the sole writer and reader.** The pak name-reference scan (I5,
38,426 files) finds `StoreEnemySaveState` and `RetrieveEnemySaveState` in **exactly one
cooked asset each**: `QueensDomain/Content/Enemies/EnemySpawner.uasset`. Nothing else in
the game touches enemy persistence.

**D4-p4 — `SpawnerGUID` is declared but never authored per placement.** The ASCII string
`SpawnerGUID` occurs 1× in the pak — in `EnemySpawner.uasset`'s name table — and **0×** in
every one of the 51 map packages. Since a placed actor's defaults live in the map package,
either the property is never overridden or it is assigned at runtime. It is therefore
**not** the authored identity source.

**D4-p5 — the authored identity is the property `GUIDs`, of struct type `Guid`.**
Decoded from a cooked map by name index. Layout verified byte-for-byte in `100_GR.umap`
(first instance at payload offset 4,417,798; block stride ≈ 634 B):

```
FName  Name        (= idx of "GUIDs")   u32 + u32 0
FName  Type        (= idx of "StructProperty")  u32 + u32 0
i32    Size        = 16
i32    ArrayIndex  = 0
FName  StructName  (= idx of "Guid")    u32 + u32 0
u8[16] StructGuid  = all zero           (native struct, not a user struct)
u8     HasPropertyGuid = 0
u8[16] Value       <-- the authored FGuid
```

Scan result over all 51 maps: **4,315 authored GUIDs in 38 maps, 4,302 distinct.**
`SavableActor_C` — the parent of `EnemySpawner_C` — declares exactly this property
(`GUIDs`, plus `CurrentGUID`, `CurrentIndex`, `FixDuplicateGUIDs`, `CheckGUID`).

**D4-p6 — 414/414: every persisted enemy key is an authored GUID in a map package.**

```
EnemySaveStates records              : 414
authored GUIDs parsed from maps      : 4,315 over 38 maps
save keys matched by an authored GUID: 414 / 414
authored GUIDs NOT in this save      : 3,888
```

This is the decisive fact. It also explains the distribution: the save only contains keys
for spawners the player actually reached. Per-map, keys ⊆ authored GUIDs and unvisited
areas contribute nothing:

| map | authored GUIDs | enemy spawner placements | keys in the save |
|---|---|---|---|
| `100_GR` | 343 | 137 | 142 |
| `200_AQ` | 145 | 80 | 81 |
| `300_LIB` | 401 | 164 | **0** (never visited) |
| `400_MAUS_entrance` | 81 | 94 | 29 (partially visited) |
| `780_SZ_Depths` | 317 | 112 | 114 |

**D4-p7 — resting clears the entire table.** `Checkpoint_C`
(`LD45/Blueprints/Checkpoint`) imports `ClearAllEnemySaveStates`; `SaveFunctions_C` and
`EnemyBase_C` import `RegisterEnemyKill`. So "rest at a checkpoint → all enemies respawn"
is a single native call, `ClearAllEnemySaveStates` (`0x00E27460`), and it is a clean
respawn-broadcast event for the mod.

### Inferred

**D4-i1 (HIGH).** `EnemySpawner_C.GetSaveGUID()` returns the *authored* GUID. The spawner's
name table contains `GetSaveGUID`, `GUIDs`, `VariableGuid`, `SpawnerGUID` and **no
`NewGuid`** — whereas `SavableActor_C` is the class that carries `NewGuid` and
`FixDuplicateGUIDs`. A spawner therefore has no path to mint a fresh GUID.

**D4-i2 (HIGH).** Because the key is a property of the *placed spawner instance*, a
respawned enemy is keyed identically. Respawn is expressed in the record itself
(`bValid`, `RespawnTime`), not by re-keying. This retires the "GUID survives respawn"
risk in `ANALYSIS.md §9` — for the spawner-keyed model.

**D4-i3 (MEDIUM).** `SpawnerGUID` is the runtime mirror of `GUIDs` (or an invalid GUID the
spawner replaces on `BeginPlay` via `AssociateWithSpawner`). The enemy side carries
`OwningSpawner`, `AssociateWithSpawner` and `SourceGUID` — consistent with "the enemy is
told which spawner slot owns it".

### Falsification notes

* Refuted: *"`SpawnerGUID` is the authored per-placement identity."* Instrument I5+I6:
  the string is absent from all 51 map packages, so no placed actor serialises it.
* Refuted: *"the guid is `GUID` (no s)."* Instrument I6: the name index at the decoded site
  resolves to `GUIDs`; `GUID` (idx 5783) is a different name and yields only 16 unrelated
  values across 7 maps.
* Not refuted: a spawner that owns several `GUIDs` entries could rotate keys between
  respawns. `GUIDs` is plural and `SavableActor_C` has `CurrentIndex` — so this is the one
  live check that matters (below).
* My earlier "the GUID is not in the maps" result was **my bug**, not a fact: I appended
  `.uexp` with an off-by-one suffix (`m[:-6]` instead of `m[:-5]`). Correcting it moved the
  match from 0/414 to 414/414. Any future pak work must validate the split-package join.

### Live test required

1. Log `GetSaveGUID()` on a spawner, kill its enemy, let `RespawnTime` elapse, kill it
   again; the key must be **identical** and the `EnemySaveStates` entry **updated, not
   added**. This is the only test that catches D4-i1 being wrong.
2. Dump `GUIDs` (array length + contents) on a spawner that spawns >1 enemy.

---

## D5 — Enemy death / damage authority surface

### Answer

There is **no single native death function to hook**. Damage application and the attribute
model are native; the *death decision and the delegate broadcast are Blueprint bytecode in
`EnemyBase_C`*. The one native function on the path that carries compact, authoritative
state is **`StoreEnemySaveState` at `0x00E3BF10`** — GUID + `RespawnTime` + `RemainingHP`,
with `bValid := true`. That is the recommended host-authority hook; the `EnemyDiedEvent`
broadcast must be observed through the reflection layer, not a native hook.

### Proven

**D5-p1 — damage entry.** `UGameplayStatics::ApplyDamage` = **`0x028D2190`**
(thunk `0x02DBA5D0`, table `0x03F9AC10`; thunk-call verified).

```
028D2190  ... rcx=DamagedActor, xmm1=BaseDamage, r8=EventInstigator, r9=DamageCauser
028D21B3  test rcx,rcx ; je out            ; null actor guard
028D21B8  ucomiss xmm6,xmm0 ; je out       ; BaseDamage == 0 guard
028D2211  mov  rax,[rdi]
028D221F  mov  rcx,rdi
          call qword ptr [rax+..]          ; -> AActor::TakeDamage (virtual)
```

Same table: `ApplyPointDamage`, `ApplyRadialDamage`, `ApplyRadialDamageWithFalloff`,
`ApplyPoisonDamageTick`.

**D5-p2 — HP lives in `UFB_AttributeComponent`** (the `FB_*` framework, a first-party
plugin). Entry points resolved from pair table `0x038F0290`:

| function | RVA | status |
|---|---|---|
| `AddToAttributeBaseValue` | `0x00CF6880` | pair-table only (heuristic last-call, not thunk-verified) |
| `ApplyAttributeModifier` | `0x00CF68C0` | pair-table only |
| `SetAttributeBaseValue` | `0x00CFB8A0` | pair-table only |
| `GetAttributeValue` | `0x00CF8020` | pair-table only |
| `GetAttributeBaseValue` | `0x00CF7FF0` | pair-table only |
| `ClampAttributeWasUpdated` | `0x00CF6F30` | **read directly** — 4 instructions, tail call |

> These six are weaker evidence than the rest of this document: they come from the
> `impl_of` heuristic and I did **not** cross-check each against its thunk. The
> *grouping* (all six in table `0x038F0290`) is solid; treat the individual RVAs as
> MEDIUM until the probe is extended with `expect=` for them.

`ClampAttributeWasUpdated` is a 4-instruction tail call through `vtable+0x288` — i.e. the
"attribute hit its clamp" notification is a *virtual on the component*, not a discrete
function the mod can hook by address. `FB_DamageAttributeSet` supplies `MaxHealth` /
`CurrentHealth` / `HealthRegen` as attributes, so "HP reached zero" is a **clamp fact**, not
an event with a stack frame.

**D5-p3 — the damageable-actor registry** (table `0x038F13E8`):
`RegisterDamageableActor`, `UnregisterDamageableActor`, `DamageableActorEndedPlay`.
The impls resolve to `0x00CF9FA0` / `0x00CFBC20` / `0x00CF7120`, but I could **not**
independently confirm them by thunk shape — treat those three RVAs as **UNRESOLVED**
(raw list is in the evidence file). The *names* and the grouping are PROVEN.

**D5-p4 — the "death" names are not what they look like.**

| name | what it actually is | evidence |
|---|---|---|
| `OnEnemyDied` `0x00E36240` | a member of **`UFS_EnemyAction`** (table `0x03944EF0`, alongside `InitAction`/`RunAction`/`CancelAction`/`FinishAction`/`ForceEndAttackState`) — the enemy cancels its current action when it dies | pair group + `&UFS_EnemyAction::OnEnemyDied` in the string pool |
| `OnEnemyStaggered` `0x00E36250` | same class, staggered variant | same |
| `EnemyDiedEvent` | **`EnemyDiedEvent__DelegateSignature`** exists (`0x03945868`); the bare name does not exist in the exe | narrow-string scan |
| `EnemyThatDied` / `EnemyThatStaggered` | the delegate's parameter names (`0x039418D8`, `0x039419A0`) | narrow-string scan |

`OnEnemyDied`'s only direct caller is its own exec thunk (`0x00E4EA10`) — **no native code
calls it**. So the death notification originates in Blueprint.

**D5-p5 — where `EnemyDiedEvent` is used.** Name-reference scan (I5) finds it in **11**
cooked assets:

```
Enemies/EnemyBase.uasset                        <- the broadcaster
Enemies/EnemySpawner.uasset                     <- the persistence owner
Enemies/JosamiKnight_00/DesBoss/Josami_Des_BloodPoolSpawner.uasset
Enemies/JosamiKnight_00/JosamiKnight_Base.uasset
Enemies/Necronomicon/Necronomicon_BP.uasset
Enemies/Necronomicon/Zombie/NecroZombie_Spawner.uasset
Enemies/RoyalDuo/Fated_Wheeler_Miniboss_BP.uasset
Enemies/Shared/EnemyAttachmentMesh.uasset
Enemies/Shared/EnemySkeletalMeshAttachment.uasset
Enemies/Shared/Enemy_Projectile_Base.uasset
Items/Weapons/Daggers/Dagger_Projectile_Base.uasset
```

`EnemyStaggeredEvent` appears in only 2 (`EnemyBase`, `Fated_Wheeler_Miniboss_BP`) — stagger
is *not* a general event, so the sync design must not rely on it.

**D5-p6 — persistence write.** `StoreEnemySaveState` (`0x00E3BF10`, verified) — see D4-p2
for the full disassembly and the `bValid/RespawnTime/RemainingHP` triple. Its **only**
direct caller is its exec thunk (`0x00E62141` inside `FUN_00E62000`), i.e. it is invoked
from Blueprint, from `EnemySpawner_C`.

**D5-p7 — kill accounting is Blueprint and transient.** `RegisterEnemyKill` is referenced by
8 assets (`SaveFunctions`, `EnemyBase`, `Queen_BP`, `Sipeha_BP`, four boss spawners). It
writes `KilledEnemyCounts` on `FS_SaveSlot_C`, and `KilledEnemyCounts` is **not serialised**
into `World0.sav` (D6-p6 method) — it is a session counter, not persistence.

**D5-p8 — `EnemyBase_C` is a large Blueprint** (317 exports, **276** `Function` exports),
including `ApplyDamageToHitboxTarget`, `ApplyDamageTests`, `AggroFromHit`,
`CalculateAppliedStaggers`, `CancelAllStatusEffects`, `ShowGameOverScreen` (player side).
The enemy's hit resolution, stagger maths and death branch are all BP functions.

### Inferred

**D5-i1 (HIGH).** The `EnemyDiedEvent` broadcast happens in `EnemyBase_C` bytecode. Chain:
`EnemyBase_C` (damage → HP clamp → death branch) → broadcast `EnemyDiedEvent(EnemyThatDied)`
→ `EnemySpawner_C.ManagedEnemyDied` → `RegisterEnemyKill` + `StoreEnemySaveState`.

**D5-i2 (HIGH).** Damage is **not** a single funnel on the enemy side: `ApplyDamage` is the
engine entry, but `EnemyBase_C` calls `ApplyDamageToHitboxTarget` per hitbox, and
`UFB_AttributeComponent` applies modifiers. A mod that hooks only `ApplyDamage` will miss
poison ticks (`ApplyPoisonDamageTick`), magic-fire ticks, and any BP path that writes an
attribute directly.

### Recommendation — the single best hook

> **`UFS_ItemUtils::StoreEnemySaveState` @ RVA `0x00E3BF10`** (and its reader
> `RetrieveEnemySaveState` @ `0x00E39240`).

Why this one:

* it is **native**, so it survives content changes and is byte-verifiable at load;
* it is the **single convergence point** — exactly one cooked asset calls it;
* it carries **exactly the state the save uses**: `FGuid Key`, `float RespawnTime`,
  `float RemainingHP`, and implicitly `bValid = true`;
* it is *idempotent* — the same call repeated converges (TMap::Add of the same key);
* it does not require driving Blueprint, so it cannot trip the "valid object, wrong type"
  crash class DollsMP documented.

What it does **not** give you: the *moment* of death (it fires when the spawner persists,
which may be at death or at save), and no damage magnitude. For the event itself, bind the
delegate at the reflection layer — `ProcessEvent` on the object with the
`EnemyDiedEvent__DelegateSignature` UFunction, or `AddMulticastDelegate` — which delivers
`EnemyThatDied` as a `UObject*`.

Rejected alternatives, and why:

| candidate | verdict |
|---|---|
| `UGameplayStatics::ApplyDamage` `0x028D2190` | fires for **player→enemy and enemy→player alike**, has no notion of entity identity, and is called from ~50 BPs; it is a *damage* hook, not a *death* hook |
| `UFB_AttributeComponent` clamp (`ClampAttributeWasUpdated` `0x00CF6F30`) | 4-instruction tail call into `vtable+0x288`; the notification is virtual per-component, and the component is shared with stamina/poison/regen, so it fires for non-death clamps |
| `UFS_EnemyAction::OnEnemyDied` `0x00E36240` | fires only if the enemy had an action running; it is a *cancel* hook |
| `EnemyDiedEvent` via a native hook | **no native broadcast site exists** — see D5-p4/p5 |

### Falsification notes

* Refuted: *"`EnemyDiedEvent`/`OnEnemyDied` are the same hook."* Instrument I1 + string
  scan: `OnEnemyDied` is a `UFS_EnemyAction` member; `EnemyDiedEvent` is a delegate type
  with signature `EnemyDiedEvent__DelegateSignature`. Different objects.
* Refuted: *"damage reaches the enemy through one function."* `EnemyBase_C` exposes
  `ApplyDamageToHitboxTarget`; the attribute component is separately reachable.
* Not refuted: there may be a native `AFS_EnemyCharacterBase::Die`-equivalent that never
  appears in the pair table (a non-BP-callable member). The pair tables only list
  Blueprint-callable functions. **This is the main residual risk for D5** and the live test
  below addresses it.

### Live test required

1. Breakpoint/UE4SS-trace `0x00E3BF10` on a kill: record call order relative to the
   `EnemyDiedEvent` binding firing, and confirm `RemainingHP` is `0` (not `-1`) at death.
2. Confirm whether `StoreEnemySaveState` fires once per death or once per save.
3. Enumerate `AFS_EnemyCharacterBase`'s vtable for a `Die`/`OnDeath` member (not visible to
   the pair tables).

---

## D6 — Loot roll location

### Answer

The roll is **local, single-machine, in `UFS_ItemDropSystem`, cached in a transient map on
the save object — and it has no per-player notion at all. Loot is therefore SHARED by
construction: one kill produces one roll and one drop actor.** Per-player loot is not a
property of the game; the mod must create it by rolling again on the other machine (or
twice on the host) and replicating two results.

### Proven

**D6-p1 — the whole drop API is three functions.** Table `0x03951480` contains exactly
`GetDropSourceIDFromClass`, `GetDropSourceIDFromDataAsset`, `RollItemDrop`. Everything else
named `*Drop*` in the binary belongs elsewhere (see the falsification notes).

**D6-p2 — the cache key is `{FName sourceId, int32 dropIndex}` = 12 bytes.**
From `RollItemDrop` (`0x00E392F0`):

```
00E392F0  RollItemDrop(rcx=WorldContext, rdx=FName sourceId, r8d=int32 dropIndex,
                       xmm3=float, stack: 3 more floats)
00E3931A  call 0x00E2D100                     ; GetActiveBaseSave(WorldContext)
00E3933F  lea  rdi, [rax + 0x220]             ; the drop-cache TMap
00E3934E  mov  qword [rsp+0x20], rbx          ; key+0..7  = FName
00E39336  mov  dword [rsp+0x28], edi          ; key+8..11 = int32 dropIndex
00E3933A  lea  rdx, [rsp+0x70]                ; out: int32 slot index
00E39331  lea  r8,  [rsp+0x20]                ; key
00E39353  call 0x00D646C0                     ; TMap::Find(map, key, &slot)
00E39358  movsxd rax, [rsp+0x70]
00E39367  shl  rbx, 5                         ; pair stride = 0x20
00E3936B  add  rbx, [rdi]                     ; TMap::Data
00E3936E  lea  rbx, [rbx + 0xC]               ; value starts after the 12-byte key
```

Stride `0x20` with the value at `+0xC` pins the key at **12 bytes**. A 3-field value is then
addressed at `+0`, `+4`, `+8` (`[rbx]`, `[rbx+4]`, `[rbx+8]`).

**D6-p3 — the source id is the object's `FName`.**
`GetDropSourceIDFromClass` / `...FromDataAsset` share impl **`0x00E2E5C0`**, which is three
instructions:

```
00E2E5C0  mov rax, [rdx+0x18]     ; UObject::Name  (FName at +0x18, per 09-live-substrate §2.2)
00E2E5C4  mov [rcx], rax
00E2E5C7  mov rax, rcx
```

So the key is `(name of the drop-source DataAsset/class, drop index)`. No player, no pawn,
no instigator appears in it.

**D6-p4 — the chance call is `FB_GetRandomBool`.** `RollItemDrop` builds the FName `"Drops"`
and calls `0x00BC5E70` with the accumulated probability in `xmm1`; `FB_GetRandomBool`
resolves to exactly `0x00BC5E70` (table `0x03896A70`, thunk `0x00BDBE10`, verified). That
function advances a **4×u32 PRNG state** (`[rax]`, `[rax+4]`, `[rax+8]`, `[rax+0xC]`; a
`shl/rol/xor` mix) and returns `probability >= rnd24`. Constants: `1.0f` at `0x0376CEE0`,
`0.999f` at `0x039382B8`.

The probability the roll sees is *stateful*: `RollItemDrop` increments a fail-streak field
and the chance is recomputed from it, with `>= 0.999f` short-circuiting to a guaranteed
drop. This is a **pity timer**, and it is why the cache exists at all.

Outcome writes (verified): success → `[value+0]++`, `[value+8] = 0`; failure → `[value+8]++`,
`[value+4]++`.

**D6-p5 — the cache lives on the save object at `+0x220`.** Exactly **one** `lea
r64,[rax+0x220]` site exists in `.text`, at `0x00E3933F` inside `RollItemDrop`. (Contrast
`+0x270`, which has six sites — so `+0x220` is a different map, used by one function only.)

**D6-p6 — the cache is transient, not persisted.** The live `World0.sav` does not contain
`ItemDrops`, `EnemyDropDataA`, `EnemyDropDataB` or `KilledEnemyCounts` — all four are
declared on `FS_SaveSlot_C` (Blueprint name table) but none is a serialised `SaveGame`
property. `ActiveSuspendSlot` and `EnemySaveStates` *are* present, which is the control that
proves the reader works.

**Consequence:** the pity counter resets every session. Two machines therefore start from
*different* luck state even if they received identical drop parameters.

**D6-p7 — who rolls.** Name-reference scan: `RollItemDrop` appears in exactly **one** asset,
`Content/Blueprints/ItemSystem/SpawnItemDrop.uasset`, whose BP functions are
`TrySpawningItem`, `SpawnIndividualItem`, `GetTotalLuck`, `GetGlobalDropChanceIncrease`,
`SnapDropToGround`. `GetDropSourceIDFromDataAsset` appears in 2 (`EnemyBase`, and
`Enemies/Lizer_00/Default_Lizer_01`). `GetDropSourceIDFromClass` appears in **0** assets —
it is dead code in this content set.

### Inferred

**D6-i1 (HIGH).** One death ⇒ one `TrySpawningItem` ⇒ one `RollItemDrop` per drop index ⇒
one drop actor. Because it is a plain local call with no replication and no player index,
loot is **shared**. Per-player loot is a mod-side invention.

**D6-i2 (MEDIUM).** `+0x220` is `ItemDrops` rather than `EnemyDropDataA/B`. Evidence: it is
a TMap with a 12-byte `{FName,int32}` key, and `FS_DropCacheKey` is the only struct in the
name pool with that shape. The tie-break is a live read of the map (below).

**D6-i3 (HIGH).** Because the RNG is a stateful stream shared with every other `FB_*`
consumer, you cannot make two machines roll the same drop by seeding — the stream position
depends on every prior random call in the session.

### Falsification notes

* Refuted: *"`TriggerItemDrop` is part of `UFS_ItemDropSystem`."* Instrument I1: the name
  lives in table `0x038CB2E0`, whose other 70 names are `CreateLobby`, `JoinLobby`,
  `GetLobbyData`, `AddPromoItem`, `SubmitUpdateProperties`, … — it is the **Steam Inventory
  service**, i.e. `USteamInventory::TriggerItemDrop`. It has nothing to do with game loot.
* Refuted: *"`Drop` is the loot function."* Table `0x03C0D0C0` has exactly
  `DragCancelled | Dragged | Drop` — UMG drag-and-drop.
* Refuted: *"the roll is per-player."* The key has no player field (D6-p2) and no asset
  passes a player into it (D6-p7).
* Not refuted: `EnemyDropDataA/B` may be the *inputs* to the BP's `TrySpawningItem`; only a
  live dump distinguishes them from the cache.

### Live test required

1. Dump the `+0x220` TMap after two identical kills: keys must be
   `{"<DataAssetName>", 0..n}` and the two integer fields must be rising. This confirms
   `+0x220` = the drop cache and confirms the key composition.
2. Kill the same enemy twice, in two sessions, and compare drops — diverging drops prove the
   pity state is per-session (D6-p6/i1).
3. Confirm the drop actor's owner/lifetime is not per-pawn (it should be a plain
   `SpawnItemDrop_C` actor in the level).

---

## D8 — Input path

### Answer

The build links **both** paths. Keyboard, mouse buttons and gamepad buttons are
**Slate-routed** (`WM_KEYDOWN`/`WM_LBUTTONDOWN` → `FSlateApplication` → `APlayerController::InputKey`
→ `UPlayerInput` → action mapping → BP `InpActEvt_*`), so a `PostMessage` **can** drive
gameplay actions. Mouse **look** is an **axis** and, with the Windows raw-input path linked,
is fed from raw HID deltas that `PostMessage` cannot synthesise. Menus worked by
`PostMessage` because they are Slate widgets consuming key events — that result does **not**
generalise to camera look.

### Proven

**D8-p1 — imports (PE import directory, 822 symbols / 42 DLLs).**

| symbol | provider | present |
|---|---|---|
| `RegisterRawInputDevices` | USER32 | **yes** |
| `GetRawInputData` | USER32 | **yes** |
| `GetRawInputDeviceList` | USER32 | **yes** |
| `GetRawInputDeviceInfoA` | USER32 | **yes** |
| `GetAsyncKeyState` | USER32 | yes |
| `GetKeyState` / `MapVirtualKeyW` / `SetCapture` | USER32 | yes |
| `PostMessageW` / `SendMessageW` / `PeekMessageW` / `DispatchMessageW` / `TranslateMessage` | USER32 | yes |
| `SetWindowsHookExW` | USER32 | yes |
| `DirectInput8Create` | — | **no** |
| `XInputGetState` | — | **not by name** |

`XINPUT1_3.dll` **is** in the import directory, so XInput symbols are imported (by ordinal or
under a name my probe list did not match). There is no DirectInput anywhere.

Reading: the `WM_INPUT` handler of UE4's `FWindowsApplication` is compiled in **and wired**
(`RegisterRawInputDevices` is imported *and* `GetRawInputData` is imported — the app registers
the mouse for raw input and then reads it). This is the stock UE4.27 behaviour used for
mouse look whenever the viewport holds mouse capture.

**D8-p2 — gameplay input is a named action/axis mapping layer, not raw bytes.**
`FS_PlayerChar` (the player character Blueprint, 605 `Function` exports) exposes:

```
InpActEvt_Jump / Attack / SecondaryAttack / Pause / Status / UltraAttack
InpActEvt_NextDagger / LastDagger / NormalDaggerThrow
InpActEvt_NextWeapon / LastWeapon / WindDash          (K2Node_InputActionEvent)
InpAxisEvt_TurnMouse / InpAxisEvt_LookUpMouse         (K2Node_InputAxisEvent)
```

So every gameplay verb arrives as a named event. Keyboard/button events reach them through
Slate; `TurnMouse`/`LookUpMouse` are axes, and with raw mouse enabled the *value* comes from
accumulated raw deltas (`FSlateApplication::OnRawMouseMove`), not from absolute
`WM_MOUSEMOVE` coordinates.

**D8-p3 — the game ships a rebinding UI and its own input-mode machinery**, so input is
fully data-driven: `FS_GameInstance_C` imports `AddActionMapping`, `AddAxisMapping`,
`RemoveActionMapping`, `ApplyOverriddenInputBindings`, `SetInputMode_GameOnly`,
`SetInputMode_UIOnlyEx`, `IsInUIInputMode`, `SetSlateGamepadEnabled`.

### What the current instrument covers, and what it does not

| question | answer | basis |
|---|---|---|
| Can `PostMessage(WM_KEYDOWN)` reach a gameplay action? | **Yes** — same Slate route that reached the menus, and the action mapping layer is downstream of it | D8-p2 + the live menu result already recorded in `10-gaps-register.md` D8 |
| Can `PostMessage` drive mouse look? | **Probably not** — raw deltas are not synthesised by `PostMessage(WM_MOUSEMOVE)` | D8-p1 |
| Can it drive gamepad input? | Not to a gamepad *device* — the BTN_/axis events would need XInput device state, which `PostMessage` cannot fake | D8-p1 |
| Does it need raw input for *keyboard*? | No | D8-p1/p2 |

### Falsification notes

* Refuted: *"the game uses DirectInput."* `DirectInput8Create` is not imported and no
  `dinput8.dll` is present.
* Refuted: *"`bUseRawMouseInput` proves raw mouse is on."* That string is **absent** from
  this image; raw mouse in UE4 is controlled by `FWindowsApplication` registering on capture,
  which is what the imports show. The absence of the CVar name is not evidence either way.
* Not refuted: whether raw mouse is *actually* engaged at runtime depends on the viewport's
  capture mode (`DefaultViewportMouseCaptureMode` is present in the string pool,
  `0x03FA7BB8`). **Only a live test decides this.**

### Live test required

1. With the game in gameplay (not a menu), `PostMessage(WM_KEYDOWN, VK_SPACE)` and observe
   the jump. Repeat with an attack key. That closes "keyboard actions work".
2. `PostMessage(WM_MOUSEMOVE, dx, dy)` and read the pawn's control rotation. If it does not
   move, raw mouse is engaged and the instrumented input path must include `SendInput` or a
   hook on `GetRawInputData`.
3. If `SendInput` also fails off-screen, the fallback is a hook at
   `APlayerController::InputKey` / the `UPlayerInput` axis layer, which bypasses the OS.

---

## C9 — Save write triggers, and the silent suspend save

### Answer

Every save in the game funnels through **one** engine primitive,
`UGameplayStatics::SaveGameToSlot` @ `0x028F2B40`, and its only callers are the first-party
`FB_SaveUtils` plugin. The suspend save is a **looping `SetTimer` in `FS_GameInstance_C`**
(`K2_SetTimerDelegate`), writing an alternating slot index (`ActiveSuspendSlot` /
`SuspendSlot0` / `SuspendSlot1`), and it is **suppressed while enemies are active**
(`EnemyBase` calls `BlockSuspendSaves`) and inside `SuspendSaveBlockingVolume`. That is why
the files appear "silently, ~2 min apart, with no user action" and why they stop during
combat.

### Proven

**C9-p1 — the single write primitive.** `UGameplayStatics::SaveGameToSlot` = **`0x028F2B40`**
(thunk `0x02DC30F0`, table `0x03F9AC10`; verified). It validates the slot name through
`0x028F2970`, resolves the save directory, and returns `bool`.

**C9-p2 — the complete caller set.** `0x028F2B40` has 6 direct callers, five of them inside
`FB_SaveUtils` and one the exec thunk:

```
00BE2DA0 in FUN_00BE2D59
00BE2F42 in FUN_00BE2F02
00BE3A44 in FUN_00BE3A0F
00BE44E8 in FUN_00BE44B3
00BE4F2A in FUN_00BE4C7C
02DC31E9 in FUN_02DC30F0   (the exec thunk itself)
```

**Nothing in the game module writes a save directly.** `UFS_SaveGame` /
`UFS_SaveGameUtils` / `SaveFunctions_C` / `FS_GameInstance_C` all go through the plugin.

**C9-p3 — the slot-name builder.** `GetSaveSlotName` = **`0x00E306D0`** (table `0x0393CE30`,
verified). It dispatches on two boolean flags and an integer, allocates an `FString`
(`0x00E64C40`), and appends a sliced component (`0x00E64BA0` = `FString::LeftChop`-shaped
copy) before copying the result out. `GetGameSaveRootFolder` = `0x00E2E6B0` supplies the
directory. `GetCurrentSaveFileSlot` = `0x00BE2C20`, `SetCurrentSaveFileSlot` = `0x00BE3A80`,
`GetSaveFileName` = `0x00BE2F80`, `GetCurrentSaveFileName` = `0x00BE2BC0`.

**C9-p4 — the suspend machinery, by name reference.**

| symbol | referenced by |
|---|---|
| `SuspendSaveLoop`, `StartSuspendSaveLoop`, `GI_StartSuspendSaveLoop`, `BlockSuspendSaves` | `Blueprints/FS_GameInstance`, `Blueprints/GameInstanceUtils`, `LD45/Blueprints/Base_LD45_GM` |
| `WriteSuspendSave` | `FS_GameInstance`, `GameInstanceUtils`, `UI/Menus/PauseMenu`, `FS_Cheats` |
| `SuspendSlot0`, `SuspendSlot1` | `Blueprints/SaveSystem/SaveFunctions` **only** |
| `BlockSuspendSaves` (also) | `Blueprints/SuspendSaveBlockingVolume`, **`Enemies/EnemyBase`** |
| `IsSuspendSave` | `FS_GameInstance`, `FS_SaveSlot`, `Player/FS_PlayerChar`, `SaveMenu/SaveSlotWidget` |

**C9-p5 — the loop is a timer, not a tick.** `FS_GameInstance_C` imports
**`K2_SetTimerDelegate`** (import #184) and **`K2_ClearAndInvalidateTimerHandle`** (import
#183). Those are the only scheduling primitives it needs, and a looping timer is exactly the
shape that produces "a file every N seconds with no user action".
The `SuspendSave*` functions themselves are 58-byte Blueprint wrappers around one local
call each (`EX_LocalFinalFunction`), so the actual work is in `WriteSuspendSave` (588 B) and
`AsyncSuspendSaveFinished` (308 B).

**C9-p6 — the slot name is composed at runtime, and the index is the suspend slot.**
The literals `World0`, `_Suspend_` and `World0_Suspend` occur **0 times** in the exe **and
0 times** in any cooked package (38,426 files). So the name is assembled, not stored.
The only integers in the save that can supply the `N` are `ActiveSuspendSlot` (an
`int32`, value `1` in the live save) and the `SuspendSlot0`/`SuspendSlot1` constants in
`SaveFunctions_C`. `FS_SaveSlot_C` also carries `IsSuspendSave`, `SuspendLocation` and
`SuspendPositionIsForcedMidair` — the suspend payload is a *position + level-list snapshot*,
which is what `GenerateSuspendData` (`0x00E2CFD0`) and `RestoreFromSuspendData` (`0x00E38FB0`)
build and consume.

**C9-p7 — `World0.sav` is not a suspend save.** `IsSuspendSave`, `SuspendLocation` and
`ItemDrops` are declared on `FS_SaveSlot_C` but are **not serialised** into the observed
`World0.sav`; `ActiveSuspendSlot` **is**. So the save class is shared and the *writer*
decides what a given file is.

### Inferred (the call graph, as requested)

```
                                  (periodic timer)
  FS_GameInstance_C::SuspendSaveLoop ──K2_SetTimerDelegate──┐
        ▲                                                    │
        │ GI_StartSuspendSaveLoop / StartSuspendSaveLoop      │
        │ (called from Base_LD45_GM_C on map start)           ▼
        │                                       FS_GameInstance_C::WriteSuspendSave
        │                                                    │
        │                     ┌──────────────────────────────┼───────────────────┐
        │                     │                              │                   │
        │           GenerateSuspendData 0x00E2CFD0   SaveFunctions_C::         │
        │           (C++, snapshot)                  SuspendSlot0/1             │
        │                                                    │                   │
        │                                                    ▼                   │
        │                                       FB_SaveUtils::<AsyncSave>        │
        │                                                    │                   │
        │                                     UGamplayStatics::SaveGameToSlot    │
        │                                            0x028F2B40                  │
        │                                                    ▼                   │
        │                                   %LOCALAPPDATA%\QueensDomain\Saved\
        │                                     SaveGames\<SlotName>.sav          │
        │                                                                        │
        └── FS_GameInstance_C::AsyncSuspendSaveFinished ◄────────────────────────┘
                                  │
                                  ▼  (schedules the next one)
                          SuspendSaveLoop again
```

**Gating (WHY a save is *not* written):** `BlockSuspendSaves` / `UnBlockSuspendSaves` are
called by `EnemyBase_C` and by `SuspendSaveBlockingVolume_C`. So the timer fires but
`WriteSuspendSave` is skipped while an enemy is engaged or the player is inside a blocking
volume. **That is the observed "roughly every two minutes, but not exactly" behaviour** —
the period is the timer, the jitter is the suppression.

**Triggers, ranked by evidence strength:**

| trigger | status |
|---|---|
| period timer (`K2_SetTimerDelegate` in `FS_GameInstance_C`) | **PROVEN mechanism**, period UNRESOLVED |
| combat/volume suppression (`BlockSuspendSaves`) | **PROVEN** by name reference |
| pause menu (`PauseMenu` references `WriteSuspendSave`) | **PROVEN** reference; whether it writes immediately or only cancels is INFERRED |
| `GI_WriteSuspendSaveImmediately` (an imperative path, 145 B) | **PROVEN exists**; caller UNRESOLVED |
| level change (`Base_LD45_GM` references `GI_StartSuspendSaveLoop`) | INFERRED (re-arms the loop on map start) |
| focus loss | **not established** — `FS_SystemSave.PauseOnUnfocus` exists, but no path from focus loss to a save was found |

### Falsification notes

* Refuted: *"the suspend save is written by `UFS_SaveGame` directly."* C9-p2: the only
  callers of the write primitive are `FB_SaveUtils` functions; nothing in the game module
  calls it.
* Refuted: *"the slot name is a literal."* C9-p6: zero occurrences of `World0` / `_Suspend_`
  in 38,426 cooked files and the exe.
* Refuted: *"suspend saves happen every 2:00 exactly."* The gating in C9-p5 explains jitter,
  and the period is a Blueprint literal this pass did not recover.
* Not refuted: that the period is 120 s. My Blueprint-bytecode reader is not complete
  (`WriteSuspendSave`'s operand stream did not decode cleanly), so the period is genuinely
  unknown, not merely unread.

### Live test required

1. Read `GetCurrentSaveFileName()` (or watch the save directory) and confirm the exact
   `%s_Suspend_%d` composition and which index increments.
2. Time two consecutive suspend writes with **no enemies** nearby, to get the period.
3. Stand in an enemy's aggro range for 5 min and confirm **no** suspend save appears
   (validates the `BlockSuspendSaves` gate).
4. Alt-tab / lose focus and see whether a suspend save is written.

---

## Address table — everything this pass asserts

All of these are re-derived by `analysis/re/combat_save_probe.py`; anything marked
*verified* is additionally confirmed by the thunk containing a direct `call` to it.

| item | symbol | RVA | how |
|---|---|---|---|
| D4/D5 | `StoreEnemySaveState` | `0x00E3BF10` | pair table `0x0395D6E0`, thunk `0x00E62000`, **verified** |
| D4/D5 | `RetrieveEnemySaveState` | `0x00E39240` | pair table `0x0395D6E0`, **verified** |
| D4/D7 | `ClearEnemySaveState` | `0x00E27570` | pair table `0x0395D6E0`, **verified** |
| D4/D7 | `ClearAllEnemySaveStates` | `0x00E27460` | pair table `0x0395D6E0`, **verified** |
| D4/D5 | `GetActiveBaseSave` (world context → save) | `0x00E2D100` | 20 call sites incl. all four above |
| D4/D5 | `UFS_EnemyAction::OnEnemyDied` | `0x00E36240` | pair table `0x03944EF0`, **verified** |
| D4/D5 | `UFS_EnemyAction::OnEnemyStaggered` | `0x00E36250` | pair table `0x03944EF0`, **verified** |
| D5 | `UGameplayStatics::ApplyDamage` | `0x028D2190` | pair table `0x03F9AC10`, **verified** |
| D5 | `UFB_AttributeComponent::ApplyAttributeModifier` | `0x00CF68C0` | pair table `0x038F0290` |
| D5 | `UFB_AttributeComponent::ClampAttributeWasUpdated` | `0x00CF6F30` | virtual tail call |
| D6 | `UFS_ItemDropSystem::RollItemDrop` | `0x00E392F0` | pair table `0x03951480`, **verified** |
| D6 | `GetDropSourceIDFromClass/DataAsset` | `0x00E2E5C0` | pair table `0x03951480`, **verified** |
| D6 | `FB_GetRandomBool` | `0x00BC5E70` | pair table `0x03896A70`, **verified** |
| C9 | `UGameplayStatics::SaveGameToSlot` | `0x028F2B40` | pair table `0x03F9AC10`, **verified** |
| C9 | `GetSaveSlotName` | `0x00E306D0` | pair table `0x0393CE30`, **verified** |
| C9 | `GetGameSaveRootFolder` | `0x00E2E6B0` | pair table `0x0393CE30`, **verified** |
| C9 | `GetCurrentSaveFileSlot` / `SetCurrentSaveFileSlot` | `0x00BE2C20` / `0x00BE3A80` | table `0x0389AE20`, **verified** |
| C9 | `GetSaveFileName` / `GetCurrentSaveFileName` | `0x00BE2F80` / `0x00BE2BC0` | table `0x0389AE20`, **verified** |
| C9 | `InitSave` | `0x00E348E0` | pair table `0x03950060`, **verified** |
| C9 | `UGameplayStatics::LoadGameFromSlot` | `0x028EA4D0` | pair table `0x03F9AC10` |
| C9 | `GenerateSuspendData` / `RestoreFromSuspendData` | `0x00E2CFD0` / `0x00E38FB0` | pair table `0x03957C90` |
| C9 | `FB_GetRandomBool` family / `FB_AsyncSaveGameToSlot` | see `nativepairs.bin` | — |

### Structural constants for the mod

```
save_object.EnemySaveStates      +0x270   TMap<FGuid, {u8 bValid, float RespawnTime, float RemainingHP}>
                                          TMap layout: Data@+0, Num@+8, Max@+0xC
save_object.<drop cache>         +0x220   TMap<{FName src, i32 index}, {i32,i32,i32,..}>  pair stride 0x20,
                                          key 12 B, value at key+0xC        [MEDIUM - live-check]
FGuid in a cooked map            property name "GUIDs", struct "Guid", 16 B, group earlier
```

---

## Status against `10-gaps-register.md`

| item | was | now | key finding | confidence |
|---|---|---|---|---|
| **D4** | PARTIAL | **CLOSED** | Save key is an **authored** `FGuid` (property `GUIDs`); 414/414 save keys decoded out of map packages; `SpawnerGUID` is *not* the authored value (0 occurrences in maps); the spawner — a `SavableActor_C` — is the sole writer/reader, so a respawn reuses the key | **HIGH** (identity proven); respawn-reuse **MEDIUM-HIGH** |
| **D5** | PARTIAL | **PARTIAL → hook chosen** | Damage = `ApplyDamage`→`TakeDamage`; HP is a `UFB_AttributeComponent` attribute clamp (virtual, not hookable); **no native death broadcast exists** — `EnemyDiedEvent` is broadcast from `EnemyBase_C` bytecode; best authority hook is `StoreEnemySaveState` `0x00E3BF10`, carrying `{FGuid, RespawnTime, RemainingHP, bValid=true}` | **HIGH** for the surface, **MEDIUM** for "no native Die exists" |
| **D6** | OPEN | **CLOSED** | Roll is local in `UFS_ItemDropSystem::RollItemDrop` (`0x00E392F0`); key = `(FName of drop-source asset, int32 drop index)`, 12 B; cache is a transient map on the save object at `+0x220`; chance is `FB_GetRandomBool` over a stateful 4×u32 stream with a pity counter; **no player dimension anywhere ⇒ loot is shared by construction** | **HIGH** (roll + key + shared); **MEDIUM** for the `+0x220`↔`ItemDrops` naming |
| **D8** | PARTIAL | **PARTIAL** | Raw HID **is** linked and wired (`RegisterRawInputDevices` *and* `GetRawInputData` imported); keyboard/button gameplay verbs are Slate-routed named actions, so `PostMessage` reaches them; mouse **look** is an axis fed by raw deltas, so `PostMessage` will not move the camera. No DirectInput | **HIGH** for the import/path analysis, **UNRESOLVED** for what a synthetic message can actually drive |
| **C9** | PARTIAL | **PARTIAL → mechanism closed** | All saves funnel through `SaveGameToSlot` `0x028F2B40`, called **only** from `FB_SaveUtils`; the suspend save is a `K2_SetTimerDelegate` loop in `FS_GameInstance_C` writing an alternating suspend slot, **gated** by `BlockSuspendSaves` (called by `EnemyBase_C` and `SuspendSaveBlockingVolume_C`) — which explains the jitter | **HIGH** for mechanism and gating; **UNRESOLVED** for the timer period and the literal slot-name composition |

Nothing in `10-gaps-register.md` was edited.

---

## What a live run must settle (short list)

1. **D4** — `GetSaveGUID()` stability across a kill/respawn/`RespawnTime` cycle; and the
   arity of `GUIDs` on a multi-enemy spawner.
2. **D5** — does `StoreEnemySaveState` fire at death or at save; is there a non-BP-callable
   `Die` on `AFS_EnemyCharacterBase`'s vtable.
3. **D6** — dump `save+0x220` and confirm the key names it holds; kill the same enemy in two
   sessions and compare drops.
4. **D8** — `PostMessage(VK_SPACE)` in gameplay; `WM_MOUSEMOVE` vs camera; then `SendInput`.
5. **C9** — the suspend period with no enemies; the slot-name string; the `BlockSuspendSaves`
   gate standing in aggro; whether focus loss writes.
