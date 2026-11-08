# SYNC-RESEARCH — the seven assumptions, settled from the corpus

Analysis only. No source file was edited for this document, and none should be changed
because of it without reading §8 (which says, per item, either "no change needed" or the
exact change). Written 2026-11-08 on branch `w-research`.

Scope: the seven questions raised against `SYNC-GAPS.md` (G4, G5, §2, §3). Every claim is
tagged the way `API-GAPS.md` tags them — `[P]` proven with an artefact, `[I]` inference
with the chain stated, **UNKNOWN** with the one measurement that would close it. Tier
vocabulary and offsets are inherited from `API-GAPS.md`/`SYNC-DESIGN.md` unchanged.

**The single most important result** is that four of the seven items reduce to one
measurement the corpus already contains but the mod does not use: the cooked Blueprint
**export table plus each export's serialized property tags**. That reader is described in
§0 and reproduced in §A. It turns "is `TargetPlayer` a variable or a function?" from a
guess into a table lookup, and it answers Q4 outright.

---

## 0. The instrument that made this pass possible

`analysis/pak/code/blueprints.py` already parses a cooked `.uasset`: the name table, the
import table (28-byte records) and the export table (104-byte records), and
`try_imports_exports` resolves each export's `Class`, `SuperStruct` and `Outer` to names
`[P]`. What it did **not** read is the export *payload*, which lives in the sibling
`.uexp` at `SerialOffset` (`i64` at entry `+36`) with `SerialSize` (`i64` at `+28`).

A cooked `UStruct`/`UFunction` payload begins with a tagged property list. Each tag is
`{FName Type, FName Name, …}` and the list is terminated by the FName `None`:

```
ReenableInteract (NPC_Base)  payload, 58 bytes
  +00  FName(408)='None'          <- end of an empty property list: NO parameters
  +10  int32  SuperStructRef      (0, or a negative import index for an override)
  +18  …bytecode…
  +24  46 02 00 00 00   EX_LocalFinalFunction(export 2 = ExecuteUbergraph_NPC_Base)
       1d 55 07 00 00   EX_IntConst(0x755)        <- ubergraph entry
       16 04 0b 53      EndFunctionParms, Return, Nothing, EndOfScript
```

The reading is validated by functions whose signatures are already known from the corpus
and from their own names, all reproduced by the same walk `[P]`:

| Function | Tags recovered | Cross-check |
|---|---|---|
| `EnemySpawner_C.GetSaveGUID` | `StructProperty:Output` | corpus says it returns via an out-GUID (`qdsave.h`, `SYNC-DESIGN` §2.6) |
| `EnemySpawner_C.GetRespawnTime` | `FloatProperty:ReturnValue` | name |
| `NPC_Base_C.Interact` | `ObjectProperty:PlayerInteracting` | name + `K2Node_Event_PlayerInteracting` in the same package |
| `NPC_Base_C.GetInteractInfo` | `ObjectProperty:InteractingPlayer`, `BoolProperty:IsInteractible`, `BoolProperty:EnableInteractDistanceOverride`, `FloatProperty:InteractDistanceOverride` | the interface's own name table lists exactly those fields |
| `FS_PlayerChar.TeleportToLocation` | `ObjectProperty:Location Data`, `BoolProperty:GreenFX` | the class's own name table |
| `EnemyBase_C.ApplyDamageToHitboxTarget` | `ObjectProperty:Target`, `StructProperty:Hit`, `StructProperty:PositionOverride`, `BoolProperty:UseOverride`, `BoolProperty:HitTarget`, `StructProperty:DamageToApply` | see §2 |

Two other read-only artefacts from the main tree are used for the native half and were
**not** rebuilt:

| Artefact | Shape | Used for |
|---|---|---|
| `analysis/re/nativepairs.bin` | `{name → [(table_rva, code_rva, name_rva)]}` | which pair table a native UFUNCTION lives in, and whether its entry is a real exec thunk or a lazy resolver stub |
| `analysis/re/calls.bin` | `{target_rva → [(site_rva, func_rva, 'call'\|'jmp')]}` | caller sets, e.g. "who can call `SetShouldBeLoaded`" |

`analysis/re/qdlib.py` hardcodes the exe path to the **main** checkout
(`G:\Freespace\Queens_Domain\…`), so these runs are read-only against the shared tree and
do not need a copy in the worktree.

Everything below is reproducible from §A with:
`python -` fed the listed snippet (PowerShell: `@'…'@ | python -`). Each claim's command
is named in its own paragraph.

---

## 1. G4/G5 — enemy targeting

### (a) Is `TargetPlayer` a variable, a `ProcessEvent`-dispatched function, or a VM-internal call?

**It is a Blueprint variable, and separately there is a native accessor that the Blueprint
never calls.** The AI reads the variable directly.

`[P]` `Enemies/EnemyBase.uasset`, class export `EnemyBase_C` (export 277, serial 14 213):
its property tag list contains

```
+0x0C90  ObjectProperty  TargetPlayer   flags=0x200001  dim=1  size=8
```

in the same run as `FloatProperty:HP`, `FloatProperty:AggroRange`, `FloatProperty:AggroTimer`,
`ObjectProperty:OwningSpawner`, `BoolProperty:Paused`, `BoolProperty:BlockChase`,
`ByteProperty:InitialBehaviourMode`, … — 183 tag records, 180 distinct names. Command: §A.2.

`[P]` The name `TargetPlayer` occurs **18 times inside EnemyBase's own bytecode**, and
every occurrence lands in a function that is the enemy's AI:

```
TargetPlayer  idx=1846  ->  ExecuteUbergraph_EnemyBase(+37928)
                            UpdateCanSeePlayer(+197)
                            Kill(+2584)
                            GetLastSeenPlayerLoc(+174)
                            EvaluateAwarenessCones(+815, +917)
                            TickMagicFire(+2195)
                            GetPlayerFacingDot(+462, +537)
                            ReceiveTick(+369)
                            … (18 total)
```

Command: §A.3. The import `GetTargetPlayer` does exist in `EnemyBase` (import 410, class
`Function`, the native base's) — but its object reference occurs **exactly once in the whole
`.uexp`, at `0x02A8DF` inside `GetTargetPlayer(+12)`**, i.e. in the export payload's own
`SuperStruct` field and nowhere in any function body `[P]`. There is no call site.

`[P]` `GetTargetPlayer` is nevertheless a real UFunction of the **native** base, in pair
table `0x03947F58` at code `0x00E4FE90`:

```
00E4FE90  sub rsp,0x28
00E4FE94  mov rax,[rip+0x3b3968d]      ; cached FNativeFuncPtr, .data
00E4FE9E  jne 0xE4FEBA
00E4FEA0  lea rdx,[rip+0x38e6449]      ; 0x047362F0
00E4FEA7  lea rcx,[rip+0x3b3967a]      ; &cache
00E4FEAE  call 0x0119D280              ; resolve-by-name, then return the pointer
```

— the same **lazy resolver stub** shape `API-GAPS.md` §3 records for `FinishAction`
(`0x00E4AED0`, `0x00E4B440`). `EnemyBase_C` exports an *override* of it (export 228,
`Class=Function`, and its `SuperStructRef` field is `-410`, i.e. the import of the native
`GetTargetPlayer`) `[P]`. The UTF-16 literal `GetTargetPlayer` sits at `0x03946668` in the
same `.rdata` registration block that holds the wide string `FS_EnemyCharacterBase` at
`0x03949072` `[P]`.

`[P]` The Blueprint never calls it: the *only* occurrence of the name in the whole package
name table is inside the class payload's own function list
(`EnemyBase_C(+13545)`), not inside any function body. Command: §A.3.

`[P]` A same-named **native** property also exists — the UTF-8 property-name block in
`.rdata` contains `TargetPlayer` at `0x03947EA0`, and the 8-byte pointer to that string
appears at `0x03947DE0` as a `UECodeGen_Private` property-params record, in the block
whose class-name literal is `FS_EnemyCharacterBase` `[P]` bytes, `[I]` ownership. So the
name is used twice: the C++ base declares *some* `TargetPlayer`, and `EnemyBase_C`
declares its own Blueprint variable of the same name. The BP one is what the AI reads.

**Consequence for the design.** `SYNC-GAPS.md` G4 option (b) — "override the target in the
Blueprint's `GetTargetPlayer` through a `ProcessEvent` post-hook" — is **refuted as
written** `[P]`: the Blueprint path never goes through `GetTargetPlayer`, so a hook on it
changes nothing the AI sees. Option (a), *write the variable per enemy*, is the only
mechanism that touches the AI.

### (b) Is an attack gated on a cast to the player class?

**Yes, twice, and both gates are cast gates.**

`[P]` `EnemyBase_C.TickIndividualHitbox` (serial 3 580) references the import
`FS_PlayerChar_C` (import 48) **seven times**, and the site at `.uexp 0x1F4DB` is
immediately preceded by `0x2E` = `EX_DynamicCast` `[P]` (opcode table in §A.4; command
§A.3). Its parameter list is
`ObjectProperty:InputPin, StructProperty:PositionOverride, BoolProperty:UseOverride,
BoolProperty:Hit, …`.

`[P]` `EnemyBase_C.ApplyDamageToHitboxTarget` (serial 1 856) declares
`K2Node_DynamicCast_AsFB_Damageable_Interface` (`InterfaceProperty`) and
`K2Node_DynamicCast_bSuccess` (`BoolProperty`) among its tags, i.e. it casts its `Target`
to the **native interface `FB_DamageableInterface`** (`/Script/FBAttributes`) before
applying anything. Its full parameter list is
`Target:Object, Hit:HitResult, PositionOverride:struct, UseOverride:bool,
HitTarget:bool, DamageToApply:struct` `[P]`.

So a non-player actor can only be hit if it is an `FS_PlayerChar_C` **and** implements
`FB_DamageableInterface`. The mod's puppet is a `SkeletalMeshActor`; it satisfies neither.

**Decision.** A proxy pawn *of the player class* is the only value that satisfies both
gates, so writing `TargetPlayer` to a spawned `FS_Full_PlayerChar_C` standing at the
puppet's position is the only route that makes the host's AI attack the remote player with
the game's own code. Spawning a player-class actor is **proven-failed once and unproven
since**: `_live/d9_spawn.txt:16-21` records the `AV 0x1200000012` from a direct
`SpawnActor`, and `API-GAPS.md` §5 B7 is the deferred-pair probe that would settle it.
**B7 is therefore no longer a "nice to have" for the puppet body — it is on the critical
path of G4.**

The alternative that needs no spawn: keep the host's enemies attacking the host's own
player and ship the *damage* to the peer (§1c). That changes who is shot at, not whether
the peer can be hurt.

### (c) Is there a cheaper seam for the damage itself?

**Yes: the peer can apply the damage to its own player, because the player class exposes
the same BP-level damage entry the enemy would have used.**

`[P]` Neither class can be reached through the engine damage event:

```
EnemyBase.uasset    AnyDamage in names: False   ReceiveAnyDamage: False   TakeDamage: False
FS_PlayerChar.uasset AnyDamage in names: False   ReceiveAnyDamage: False   TakeDamage: False
```

Command: §A.5. So `AActor::TakeDamage` → `ReceiveAnyDamage` has **no handler on either
side** (see §2 for the native half).

`[P]` Both classes instead declare the `FB_DamageableInterface` message

```
TryApplyDamage(Hit: HitResult, DamageToApply: <FB struct>, Applier: Object,
               AppliedDamage: <FB struct> out, HitLocation: Vector,
               HitLocationIsValid: bool) -> ReturnValue: bool
```

— identical tag lists in `NPC_Base_C`, `EnemyBase_C` and `FS_PlayerChar_C`, which is the
signature of a shared interface implementation, and `EnemyBase_C.ApplyDamageToHitboxTarget`
is where `EnemyBase` references it `[P]` (name-site owners: `TickMagicFire`,
`ApplyDamageToHitboxTarget`, `ApplyPoisonDamageTick`).

`[P]` The player's health is not "written only by the local game tick": `FS_PlayerChar`
exports `TryApplyDamage`, `ProcessDamageDealing`, `AddAmountTOHealth`, `Set Health Points`,
`Cap HP`, `HealForAmount`, `HealForPercent`, `GetMaxHP`, `Kill`, `ShowGameOverScreen`,
`DeathAnim`, and the `Damaged` / `HealthChanged` / `OnDiedEvent` delegates. `[P]` The
concrete HP store is a `UFB_AttributeComponent` (`analysis/14-combat-save-answers.md`
D5-p2).

**Decision — implementable with today's machinery.** The host computes the hit (it already
has position, attack and HP), and the peer calls `TryApplyDamage` **on its own local
`FS_PlayerChar_C`** through `ProcessEvent`, using the mod's existing `Call`/layout code.
That is a new `INTENT` in the host→peer direction: `WIRE.md` §2 fixes `INTENT` as
"peer → host" and the only enemy intent that exists (kind 8) is peer→host. It needs no
spawn, no cast, and no native RVA.

**What it costs.** The mod must build the `DamageToApply` struct. Its type is a
`ScriptStruct` in `/Script/FBAttributes` — imported by `NPC_Base` as
`FB_DamageAttributeSet` — and the cooked names around it are `DamageAmount`,
`DamageTags`, `Damage.Tags.Responses.ResistedDamage`, `CalculatedAppliedDamage`. `[I]`
`{float DamageAmount, GameplayTagContainer DamageTags}` is the shape; **do not hardcode
it.** The mod already refuses to write unless a layout reproduces a measured oracle
(`qdreflect.h`); the same discipline applies here: walk the UScriptStruct, require a
`FloatProperty` named `DamageAmount`, and fail closed otherwise.

---

## 2. The damage path — what `0x028D2190` actually is

**`0x028D2190` is `UGameplayStatics::ApplyDamage`, exactly as `mod/native/src/targets.h:75`
labels it. The label is right; the *use* is wrong.**

`[P]` Pair tables (command §A.6):

```
ApplyDamage   0x02DB91A0 @ table 0x03F98D28   <- lazy resolver stub (same shape as §1a)
              0x02DBA5D0 @ table 0x03F9AC10   <- real exec thunk (copies params, calls the body)
```

`[P]` Body, linear disassembly past the `.pdata` end (the `.pdata` entry is truncated at
`0x028D21BD`; the body continues to `0x028D222D`):

```
028D2190  rcx = DamagedActor, xmm1 = BaseDamage, r8 = EventInstigator, r9 = DamageCauser
028D21B3  test rcx,rcx   ; je out          <- null-actor guard
028D21B8  ucomiss xmm6,xmm0 ; je out       <- BaseDamage == 0 guard
028D21C2  mov rbx,[rsp+0x80]               <- DamageTypeClass (5th argument, on the stack)
028D21CF  call 0x02D9E270                  <- [I] the null-DamageTypeClass fallback
028D2211  mov rax,[rdi]
028D2222  call qword ptr [rax+0x598]       <- AActor::TakeDamage, vtable slot 179
```

So the function is a thin engine wrapper whose only effect is the **virtual
`TakeDamage`** call. Nothing else.

`[P]` Neither class binds the engine damage event: the cooked name tables of `EnemyBase`
and of `FS_PlayerChar` contain no `TakeDamage`, `ReceiveAnyDamage` and no `AnyDamage`
(§1c), so there is no Blueprint handler to run. `[I]` HIGH that the native bases do not
override it either — the vtable stored by the registration block's constructor
(`0x0394A140`, stored at `0x00E508B0`) resolves slot 179 (`+0x598`) to `0x007764F0`, a
core-engine RVA rather than a game-module override. That vtable is a **lead, not proof of
ownership**: the class association is inferred from the block it sits in, and the
constructor candidate is `0x00E50890`. The decisive evidence is behavioural, below.

`[P]` And it is never *used* on the enemy path. `EnemyBase.uasset` does not import
`ApplyDamage` at all (`NOT IMPORTED by this package`), and the live pass recorded the
decisive fact:

> "**`ApplyDamage` and `OnEnemyDied` never fired** during the player's sword kill. … the
> game's damage/death path is the FB attribute component plus `EnemyBase_C` bytecode, not
> `AActor::TakeDamage`" — `analysis/17-d5-live.md:60-63`

`[P]` `ApplyDamageToHitboxTarget` is **not** a native engine function and **not** "the
identical API with another name": it is a **Blueprint function of `EnemyBase_C`**
(`Class=Function`, `SuperStruct = None`) overridden by 20 enemy subclasses (declared in 21
packages, imported by 20 of them).

`[P]` The game's own damage funnel, with the call edges read out of the bytecode:

```
TickIndividualHitbox                       (serial 3 580)
   .uexp +0x0D58  EX_LocalVirtualFunction(0x45) "ApplyDamageToHitboxTarget"
   .uexp +0x1F4DB EX_DynamicCast(0x2E) <FS_PlayerChar_C>
   3 × EX_InterfaceContext(0x51)
        -> ApplyDamageToHitboxTarget(Target, Hit, PositionOverride, UseOverride,
                                     HitTarget, DamageToApply)
              casts Target to FB_Damageable_Interface
              -> TryApplyDamage on the target
```

One design-level consequence that matters for every hook the mod owns: an
`EX_LocalVirtualFunction` is a **call by name on `this`, executed inline by
`ProcessInternal`** — it does not pass through `UObject::ProcessEvent` `[P]` (`0x45` in the
bytecode, and `ProcessInternal` is reached from `ProcessEvent`, not the other way round).
So a `ProcessEvent` hook at `+0x220` sees native→Blueprint calls and cross-object calls,
and **never** a Blueprint→Blueprint call on the same actor. That is why §1a's option (b)
cannot work even in principle. It does **not** stop the mod *making* such a call: a direct
`ProcessEvent(fn, frame)` still executes the function (`ProcessEvent` is the entry point,
`ProcessInternal` its executor), which is how `EnemyDrive`'s `CallNoArgs` reaches
`SetEnemiesPaused`/`ReenableInteract`.

### Decision

`proxy.cpp::DamageEnemy` (lines 2445–2461) must stop calling `0x028D2190`. Two honest
replacements, in order:

1. **`EnemyBase_C.TryApplyDamage`** — resolve the `UFunction` by name on the enemy's class
   and dispatch through `ProcessEvent` with the six-parameter frame. This is the same
   message the game itself sends to whatever the enemy hits, so hit reactions, resist, VFX
   and the death path all run. The `DamageToApply` struct problem is §1c's problem.
2. If the struct cannot be built safely, **write the attribute**: the FB framework's
   `AddToAttributeBaseValue` (`0x00CF6880`, pair-table only, `[I]` MEDIUM) /
   `ApplyAttributeModifier` (`0x00CF68C0`, same tier) with a read-back through the mod's
   existing `GetHP`. `[P]` The read-back oracle already exists in `DamageEnemy` and is the
   right pass/fail test; it is `GetHP`/`GetMaxHP`, both `FloatProperty:HP` out-params (§0).

The "enemy damage had NO effect" warning at line 2457 is therefore the *expected* result,
not a bug to chase. Do not remove the warning — repoint it.

**Why this matters more than it looks.** Every HP movement the mod makes goes through
`DamageEnemy`:

```
proxy.cpp:2564  peer copy converges to the host's row   -> DamageEnemy(r.actor, …)
proxy.cpp:2590  peer copy convergence (second path)      -> DamageEnemy(r.actor, d)
proxy.cpp:2626  host applies a peer hit  (I_ENEMY_HIT)   -> DamageEnemy(r->actor, dmg, …)
proxy.cpp:2640  host applies a peer kill (I_ENEMY_RECORD) -> DamageEnemy(r->actor, …)
```

If the call is inert, then "health converges downward only" (`qdenemy.h:27`) cannot
converge at all, and a peer's `INTENT kind 8` hits and kills never reach the host's enemy.
So this is not a cosmetic path swap: it is the difference between enemy sync working and
appearing to work. The peer-copy callers at 2626/2640 are the *first* place the failure
will show (the host's enemy survives a peer kill), which is exactly `SYNC-GAPS` G5's
"host spawns a fresh enemy when it walks up later".

---

## 3. Level unloading — does `AFS_LevelManager` ever unload?

**No. The manager only hides. The mod's existing veto is complete.**

`[P]` The `ULevelStreaming` bit setters are a family at `0x029AC810`–`0x029ACDDD`, all
operating on the byte at `+0xBA`:

```
0x029AC810  bit 0   and al,1 ; and cl,0xFE ; or cl,dl   -> bShouldBeLoaded
0x029ACB20  bit 3   shr al,3 ; and al,1 ; and cl,0xF7   -> bShouldBeVisible  (corpus RVA)
0x029ACAC0  bit 4   shr al,4 ; and cl,0xEF              -> bShouldBeVisibleInEditor
0x029ACB80 / 0x029ACDDD   and [rbx+0xBA],0xF9           -> bits 1+2, plus a +0xB8 store
```

`[P]` Callers of the two setters that matter (command §A.7, from `calls.bin`):

```
SetShouldBeVisible (0x029ACB20)   15 callers, of which 3 are in the game module:
    0x00E21EB8  in FUN_00E21E40   <- the flush applier (SYNC-DESIGN 2.8 chain)
    0x00E3CFE1  in FUN_00E3CF9E   <- the per-frame manager update (twice)
    0x00E3CFFA  in FUN_00E3CF9E
bShouldBeLoaded  (0x029AC810)      2 callers in the whole image, both engine:
    site 0x02CD481E  in FUN_02CD457D   <- the same function that also calls SetShouldBeVisible
    site 0x02E3D7E2  in FUN_02E3D770   <- the SetShouldBeLoaded UFUNCTION thunk cluster
    -> game-module callers: 0
```

`[P]` Every game-module call into the streaming-setter region, exhaustively:

```
FUN_00E21E40  site 00E21EB8  call -> 0x029ACB20   (SetShouldBeVisible)
FUN_00E3CF9E  site 00E3CFE1  call -> 0x029ACB20
FUN_00E3CF9E  site 00E3CFFA  call -> 0x029ACB20
FUN_00E23C80  site 00E23C96  jmp  -> 0x00BB4780   (a jmp thunk to 0x02649B40, engine)
FUN_00E23CA0  site 00E23CCC  call -> 0x00BB4780
FUN_00E2AD80  site 00E2ADB6  call -> 0x00BB4F40   (does not reach 0x029AC810)
```

`[P]` No game-module function writes `ULevelStreaming+0xBA` directly (a scan of all 2 059
`disp32 0xBA` sites in `.text` leaves only one in the game module, at `0x00E3B52F`, and it
is a misaligned `movabs` false positive). `[P]` No cooked package imports
`SetShouldBeLoaded`; the name appears in only two (`FS_CommonGM`, `TitleScreenUI`).

**Verdict.** `SYNC-GAPS.md` §2's worry ("if the manager also unloads levels
(`SetShouldBeLoaded(false)`), that path is not vetoed") is retired: it does not, and
nothing else on this build does either. `HK_SetShouldBeVisible` — no change needed.

The one residual is a *clip* rather than an unload: a level that the game hides and the mod
forces visible can be hidden again by the engine's own per-frame pass, which is why the
mod re-asserts visibility; that is already the design (`SYNC-DESIGN` §2.8) and is not a
new finding.

---

## 4. `NPC_Base.BlockInteract` / `ReenableInteract` — the real signatures

**`ReenableInteract` takes no arguments. `BlockInteract` is not a function at all: it is a
`bool` Blueprint variable on `NPC_Base_C`. The mod's block path can never resolve.**

`[P]` `ReenableInteract` — export 3 of `NPC_Base.uasset`, `Class=Function`, 58-byte
payload, **zero property tags** (§0's payload reader), body = a stub into the ubergraph:

```
+00 FName 'None'                      <- end of the parameter list: nothing follows
+24 46 02 00 00 00                    EX_LocalFinalFunction(ExecuteUbergraph_NPC_Base)
    1d 55 07 00 00                    EX_IntConst(0x755)
    16 04 0b 53                       EndFunctionParms / Return / Nothing / EndOfScript
```

It is called from the ubergraph by `EX_LocalVirtualFunction (0x45)` at `.uexp 0x13BB`.
Arity 0, return value none `[P]`.

`[P]` `BlockInteract` is **declared nowhere in the build**:

| check | result |
|---|---|
| Function exports named `BlockInteract`, all 19 213 QueensDomain packages | **0** |
| Function imports named `BlockInteract`, all packages | **0** |
| UTF-16 or ANSI literal `BlockInteract` in `QueensDomain-Win64-Shipping.exe` | **0** (both encodings) |
| packages whose *name table* mentions it | 2: `NPCs/NPC_Base`, `SecretSwordAltar/SecretSword_Altar_BP` |

`[P]` Instead it appears as a **property** of `NPC_Base_C`, in the class payload's tag list,
next to the NPC's other top-level variables:

```
… +0x05DE  BoolProperty  DestroyOnEndTalk
  +0x0613  BoolProperty  BlockInteract          flags=0x200001  dim=1  size=1
  (+0x037C  BoolProperty  PlayerIsTalkingTo      flags=0x200001  dim=1  size=1)
```

and it is **read** — not called — in `NPC_Base_C.GetInteractInfo(+1402)` and twice in
`ExecuteUbergraph_NPC_Base` (+3538, +4634) `[P]` (name-site scan; the same function returns
`IsInteractible`, so this is the interaction gate `[I]` HIGH).

`[P]` The mod's `proxy.cpp:2738` does

```cpp
bool ok = block ? CallNoArgs(npc, fBlock, "BlockInteract") : CallNoArgs(npc, fUnblock, "ReenableInteract");
```

`CallNoArgs` resolves a `UFunction` by name on the NPC's class. There is no such UFunction,
so the **block half has never done anything** on any build, and the log line at 2743
("NPC_Base.BlockInteract signature logged on first use") could only ever fire if it did.
`[I]` HIGH: the mod's own `FindFunction` therefore returns null and `ok` is `false`.

### Decision

Replace the pair with **one property write** — the same mechanism `EnemyDrive` already uses
for `bIsAIDisabled` (`WriteBytePropC`), which is safe here because a Blueprint bool is a
whole byte, not a bitfield (the class tag is `BoolProperty`, and `proxy.cpp:1955` states
the assumption; for BP bools it is correct `[I]` HIGH):

* **block**: `WriteBytePropC(npc, "BlockInteract", 1)` — and, if the HUD should also stop
  offering the prompt, `"PlayerIsTalkingTo"` is the only other class-level bool in the same
  list (`BoolProperty:PlayerIsTalkingTo`, read 3× in the ubergraph).
* **unblock**: `WriteBytePropC(npc, "BlockInteract", 0)`. The `ReenableInteract` call may be
  kept (proven 0-arg, harmless) or dropped; it is not what restores interactability.

---

## 5. Enemy AI off — is `bIsAIDisabled` the gate?

**`bIsAIDisabled` exists and is honoured, but it is a *native* flag; the game's own
AI-off switch is the Blueprint bool `Paused`, which `EnemySpawner_C.SetEnemiesPaused`
writes.**

`[P]` `bIsAIDisabled` is **not** among `EnemyBase_C`'s property tags (180 distinct names; the
same check rejects `bIsStaggered`, `bIsParalyzed`, `Phase` and `bBoss`, and accepts
`TargetPlayer`, `Paused`, `BlockChase`), i.e. it is not a Blueprint variable. It is a native
property of the native base: `FS_EnemyCharacterBase`'s
UTF-16 class-name literal is at `.rdata 0x03949072`, and a `UECodeGen_Private`
property-params record pointing at the UTF-8 string `bIsAIDisabled` (string at
`0x03948DD8`) sits at `0x039485A8`, inside the same registration block `[P]`. (`bIsAIDisabled`
also appears in the exe's UTF-8 property-name block at `0x03948D80`–`0x03948DF8`, together
with `CurrentMovementInput`, `bIsParalyzed`, `bIsJumping`, `bIsStaggered`,
`bDisableFallingCheckForAIBlock`, `PoisonDamagePool`, `WeakenDuration`, … — and `Phase`,
`bBoss`, `bDisableUprighting` at `0x03947E60`–`0x03947F08`. Together that is the native
`FS_EnemyCharacterBase` property vocabulary.)

`[P]` The Blueprint **does** read it, in the functions that decide AI behaviour:

```
bIsAIDisabled  idx=260  ->  AnythingIsBlockignAI(+746)
                            Configure Behaviour Mode(+187)
                            AggroFromHit(+433, +691)
                            ExecuteUbergraph_EnemyBase(+22947, +55141, +55165, +55192, +55215)
```

So it is honoured by `EnemyBase_C`'s own logic `[P]` — `EnemyDrive`'s write is not a no-op.

`[P]` But the game's own "turn these enemies off" API is different and BP-facing:

* `EnemySpawner_C` exports `SetEnemiesPaused(Paused: bool)` (export 14; one `BoolProperty:Paused`
  parameter) `[P]` §0's reader.
* The name `Paused` occurs **inside** that function at `+32` and `+107`, so the function
  writes a `Paused` property `[P]`. `[I]` HIGH that the receiver is the spawner's managed
  enemy: `EnemySpawner_C` has exactly one (`ObjectProperty:CurrentManagedEnemy`, `+0x0561`,
  `size=8`) and no `Paused` property of its own.
* `Paused` **is** a `BoolProperty` on `EnemyBase_C` (`+0x1F4E`, `dim=1`, `size=1` — a
  whole-byte Blueprint bool), and `EnemyBase_C.SetPauseTimeDilation` writes it too
  (`Paused` at `+32`, `+107`) `[P]`.

### Decision

Prefer the game's own path, because it is one call and it is a Blueprint bool (whole byte,
safe to write):

```
CallBool1(spawner, "SetEnemiesPaused", on)      // proxies to CurrentManagedEnemy.Paused
```

and keep the tick disables (`SetActorTickEnabled(false)` on actor + controller) and the
movement-mode save/restore that `EnemyDrive` already does. Keep
`WriteBytePropC(a,"bIsAIDisabled",…)` only if it is verified safe — see the risk below.

**Risk, stated as a measurement.** `bIsAIDisabled` is a native bool. If it is a *bitfield*
(`uint8 bIsAIDisabled:1`), `WriteBytePropC` writes the whole containing byte and clobbers
its seven neighbours. `proxy.cpp:1955`'s comment ("Blueprint bools occupy a whole byte")
is true for Blueprint bools and does not cover this one. UNKNOWN. One probe settles it:
on the game thread, read the `FBoolProperty`'s `FieldMask` bytes (`ByteOffset`/`ByteMask`/
`FieldMask` follow the property's `GetCppProperty` in the 4.27 `FBoolPropertyParams`);
pass = all-ones mask (`FieldMask == 0xFF`) for `Paused`, and the bitfield question answered
for `bIsAIDisabled`; fail = a sub-byte mask, in which case read-modify-write with that mask
instead of a byte store.

---

## 6. Bosses — what per-machine state the enemy row cannot express

The enemy row carries `{guid, x/y/z, yaw, hp, flags}` (`SYNC-DESIGN` §2.6). Everything
below is *not* in that row, and each line says whether a host can drive it on the peer.

| State | Where it lives | Kind | Host-drivable on the peer? |
|---|---|---|---|
| boss health bar | `PlayerStatsAndUtils_C.ActivateBossHPBar(Enemy: Object, Group: Object, __WorldContext: Object)`; also `FS_PlayerChar.ActivateBossHPbar` / `DeactivateBossHPbar` (0 args) `[P]` | BP function library call, `__WorldContext` = **the caller's** world | **Yes** — peer calls it locally on its own copy: `ActivateBossHPBar(copy, GroupDA, ctx)` and the paired `Deactivate` `[I]` |
| arena exit | `EnemySpawner_C.PlayerTeleportedOutOfBossArena()` — 0 params, declared on the base spawner, overridden by 20 boss-spawner BPs `[P]` | local BP event; the *cause* is computed per machine by `Shoulddo Boss Teleport Out` / `Local_CheckBossTPOut(TeleportOut, AnyWereTeleportOut)` from `PairedBossSpawners` `[P]` | **Yes** — the fact is one bool ("a player left the arena"); send it and the peer calls the event on its spawner |
| paired spawners | `EnemySpawner_C.PairedBossSpawners` — `ArrayProperty` at `+0x0DAD` (`size=16`, a `TArray`) whose element is the `ObjectProperty` at `+0x0DDC` (`size=8`); set per placed spawner in `450_MAUS_darkmaze`, `600_Castle`, `740_SZ_Prison`, `780_SZ_Depths` `[P]` | authored level data | read-only; no sync needed |
| phases | `[P]` `NewPhase` exists in exactly one cooked package (`Queen/Queen_BP`); `Phase`, `bBoss`, `OnSubStateChanged` exist **only** in the exe name block and in **zero** cooked packages | per-boss BP/native variable | **No** — there is no shared phase field to drive; the enemy row cannot express it and nothing else can |
| pause / freeze during a cinematic | `EnemySpawner_C.SetEnemiesPaused(Paused)` → `EnemyBase_C.Paused` `[P]` | BP bool | **Yes** — same call as §5 |
| boss defeat persistence | `NamedSaveData["DragonLordDead"] = 1`, written by the spawner's `LordWasKilled` (`analysis/22-close-out.md` §1, live-observed in a suspend save) | progression flag | via `WORLD_FLAGS` — **blocked by G1** |

`[P]` `EnemyBase_C` also exports `Enemy_GetBossInfo` and `Enemy_IsPartOfBossEncounter`, so
boss membership is queryable on the enemy itself (name-site owners in the import scan).

**Verdict.** Two of the five are worth syncing and both are cheap: the health bar (a local
call the peer makes with its own copy) and the arena-exit event (one bool each way). Phases
are out of reach and should be left out of the design rather than faked. `PairedBossSpawners`
needs no work. The defeat flag is G1's problem, not the boss lane's.

---

## 7. Cutscenes, sequencer and travel

**Yes — there is a measured entry point, and it is not `OpenLevel`.**

`[P]` `FS_PlayerChar` exports the whole travel vocabulary (770 exports; these are the
relevant ones, with the parameter lists §0's reader recovers):

```
TeleportToLocation(Location Data: Object, GreenFX: bool)      <- the in-world move
BeginTeleport(Teleporter: Object)                            <- teleporter actor hand-off
MovePlayerToTempTeleporter()   FinishTeleport()              <- the two halves
TeleportToSave(Target: Object)
TeleportToTeleporterData   TeleportCrystalGoToTarget   SuperFastTeleport
FindTeleporterActorFromData   FindCheckpointWithLocationData
QueueTeleportLevelLoad                                       <- the level-load half
IsPlayerTeleporting   DidRecentlyTeleport   SetTeleportedRecently   TeleportToSipehaArena
```

`[P]` The world is *told*: `EnemySpawner_C.PlayerTeleported()` (export 7, 0 params) is
mentioned by `EnemySpawner.uasset` and `OtherWorld/OW_TP/OW_TP_Building_00_BP.uasset`, and
the spawner's own `DidPlayerRecentlyTeleport` / `Shoulddo Boss Teleport Out` /
`Local_CheckBossTPOut` code reads the recently-teleported state. Any teleport is therefore
observable at a BP entry point on each machine.

`[P]` `OpenLevel` is imported by exactly seven packages — `FS_GameInstance`,
`QueensDomain_GameMode`, `PauseMenu`, `DieUI`, `TitleScreenUI`, `DemoEndSplash`,
`FS_Cheats` — and by **no** teleporter, spawner, level actor, or `AFS_LevelManager`
function. So the common "travel" is in-world, as `API-GAPS.md` §4 already argued; `OpenLevel`
is the menu/death/cheat path.

`[P]` Sequencer is Blueprint-driven, not a manager: cooked names include
`LevelSequenceActor`, `LevelSequencePlayer`, `MovieSceneSequencePlayer`, `GetSequencePlayer`,
`PlayAnimation`, `Play`; `FS_PlayerChar` exports `BeginCutsceneWithManager`,
`EndManagerCutscene`, `StartIntroCutscene`, `Intro_StartSequence`, `SkipIntroCutscene`,
`Player_SkipCutscene`, `LockInputForLoadingOrCutscene`, `UnlockInputForLoadingOrCutscene`.
`[I]` HIGH: a cutscene is per-machine BP state; there is no host-side switch that makes the
peer play one — the peer must be told to call the same player-side function.

**Verdict.** `SYNC-GAPS` G6 and `SYNC-DESIGN` §2.8's "hard transition" are implementable
without `OpenLevel`:

* **in-world travel** (the common case): host announces `{target}`; the peer calls
  `TeleportToLocation(locationData, false)` on its own pawn; both then call
  `SetTeleportedRecently` if the game would have.
* **map switch** (`SipehaWorld`, `B33sment`, the `997/998/999_*` cutscene maps): the
  GameInstance path is the only one; `OpenLevel`'s premises from `API-GAPS.md` §4 stand
  (cached save invalidation unproven), so keep the refusal-with-rejoin fallback.
* **cutscene**: treat as a dialogue-like per-machine presentation; the host sends "play
  sequence S", the peer calls `BeginCutsceneWithManager`/the sequence player locally.

**UNKNOWN, and the one probe that closes it.** Which of `TeleportToLocation`,
`TeleportToTeleporterData` or the teleporter actor's own path the fast-travel UI actually
takes. Probe: hook `UObject::ProcessEvent` (`0x011A7AA0`), filter on the resolved function's
name, walk to a `Ruins_Teleporter` and press `E`. **Pass:** exactly one of the three names
appears, preceded by `BeginTeleport` and followed by `FinishTeleport`+`QueueTeleportLevelLoad`.
**Fail:** none appears (then the teleporter moves the pawn by transform only and the mod
should do the same). This is one teleport, one log line.

---

## 8. What this changes in the mod today

| # | Item | Verdict | Exact change |
|---|---|---|---|
| 1 | `proxy.cpp::DamageEnemy` (2445) and all four call sites (2564, 2590, 2626, 2640) | **change** | Delete the `0x028D2190` call. Proven inert on enemies (§2), so today's HP convergence and the peer's `INTENT kind 8` hits do nothing on the host. Resolve and `ProcessEvent` `TryApplyDamage` on the enemy with a `DamageToApply` built from the `FB_DamageAttributeSet` script struct; keep `EnemyHp` before/after as the oracle and keep the "NO effect" warning as the fail signal. |
| 2 | `proxy.cpp::EnemyDrive` (2464) | **change** | Call `SetEnemiesPaused(on)` on the spawner (game's own API; writes `CurrentManagedEnemy.Paused`, a BP bool) instead of, or before, `WriteBytePropC(a,"bIsAIDisabled",…)`. Keep the tick and movement-mode work unchanged. |
| 3 | `proxy.cpp` dialogue block (2738) | **change** | `BlockInteract` is a **bool property**, not a function (§4). Replace `CallNoArgs(...,"BlockInteract")` with `WriteBytePropC(npc,"BlockInteract", block?1:0)`; keep or drop the proven-0-arg `ReenableInteract`. Update the comment at `qddialogue.h:212`. |
| 4 | `proxy.cpp::HK_SetShouldBeVisible` (2145) | **no change needed** | The manager has no unload path at all (§3). |
| 5 | `qdenemy.h` header comment ("the game's own `bIsAIDisabled`") | **change (comment only)** | The flag is native and honoured, but the game's own switch is `Paused`/`SetEnemiesPaused`. Fix the prose so the next reader does not re-derive §5. |
| 6 | G4/G5 enemy targeting | **blocked on B7** | `TargetPlayer` is writable per enemy, but two cast gates (§1b) admit only an `FS_PlayerChar_C` that implements `FB_DamageableInterface`. A puppet of that class is the only value that works, so `API-GAPS.md` §5 B7 moves from "puppet body" to "enemy targeting". Until it passes, do not write `TargetPlayer`. |
| 7 | peer damage (new lane) | **change (new code)** | Add a host→peer damage intent carrying `{float, causer?}`; the peer applies it with `TryApplyDamage` on its own pawn (§1c). This is the only way the peer can be hurt without a proxy pawn. |
| 8 | boss health bar / arena exit | **change (new, small)** | Peer calls `ActivateBossHPBar(copy, groupDA, ctx)` when its driven copy becomes the boss; send the arena-exit bool so the peer can raise `PlayerTeleportedOutOfBossArena()` on its spawner (§6). |
| 9 | travel | **change** | Use `FS_PlayerChar.TeleportToLocation`, not `OpenLevel`, for in-world travel (§7). |

Nothing above requires a new native RVA. Every change is either a `ProcessEvent` call the
mod already knows how to make, or a property write the mod already knows how to make.

---

## 9. Where this contradicts the existing documents

Reported, not silently reconciled.

1. **`SYNC-GAPS.md` G4 option (b) is wrong.** "Overriding the target in the Blueprint's
   `GetTargetPlayer` through a `ProcessEvent` post-hook … (b) only works if that call is a
   real `ProcessEvent` dispatch and not a VM-internal call, and that is unmeasured." It is
   measured now: the Blueprint never calls `GetTargetPlayer` (one name-table reference, in
   the class's function list), it reads the `TargetPlayer` **field** `[P]` §1a. A
   `ProcessEvent` hook on `GetTargetPlayer` cannot change the AI's target. Option (a) is the
   only mechanism, and it needs the cast proof in §1b.
2. **`SYNC-GAPS.md` §2 and `qddialogue.h:212`** describe `BlockInteract`/`ReenableInteract`
   as the game's pair. Half of that is true: `ReenableInteract` is a 0-arg UFunction;
   `BlockInteract` is a bool variable `[P]` §4.
3. **`qdenemy.h:6`** says AI-off is "the game's own `bIsAIDisabled`". The flag exists and is
   read, but the game's own pause API is `SetEnemiesPaused`/`Paused` `[P]` §5.
4. **`API-GAPS.md` §5 B7** treats spawning a player-class actor as a *puppet-appearance*
   problem. It is now also the *enemy-targeting* problem (§1b), which raises its priority
   above "puppet body". The AV it records (`0x1200000012`) is unaffected by this document.
5. **`API-GAPS.md` §0's "D5 says damage goes ApplyDamage → TakeDamage"** is correct about the
   *engine* and is not the path this game takes: the live pass already found the function
   never fires (`17-d5-live.md:60`), and §2 shows why — no Blueprint binds
   `ReceiveAnyDamage`, on either class, and the native slot resolves to core engine code.
   The same wrong premise is written into the wire spec: `WIRE.md` §3.10 says the host
   applies a peer's enemy hit "through the game's own damage path
   (`UGameplayStatics::ApplyDamage`)". That sentence must be repointed to
   `TryApplyDamage` — otherwise the peer's hits converge the peer's *row* but never the
   host's *enemy*.
6. **`TargetPlayer` is shadowed.** There is a Blueprint variable `TargetPlayer` on
   `EnemyBase_C` (`+0x0C90`) *and* a native property of the same name on the native base
   (§1a). A name-based reflective lookup (`PropOff`/`ObjPropC`) returns the **Blueprint**
   one, because `UStruct::ChildProperties` is walked before `SuperStruct`; that is the one
   the AI reads, so the behaviour is right — but any future code that assumes it is writing
   the native field is writing a different address. Say which one is meant.

---

## 10. The measurements that would close what is still open

Each is one probe with a pass/fail, in the `API-GAPS.md` style. None requires a code change
to answer.

| # | UNKNOWN | Probe | Pass | Fail |
|---|---|---|---|---|
| 1 | Can a player-class actor be spawned? (B7, now on the G4 critical path) | the deferred pair, as written in `API-GAPS.md` §5 B7 | actor of class `FS_Full_PlayerChar_C` alive 60 s, mesh at `+0x480`, transform within 1 cm | any AV, `PendingKill`, or a null mesh |
| 2 | Is `bIsAIDisabled` a bitfield? | read the `FBoolProperty` mask fields at runtime (§5) | mask all-ones → byte write is safe | sub-byte mask → switch to masked read-modify-write |
| 3 | Can `TryApplyDamage` be invoked from native? | build `FB_DamageAttributeSet{DamageAmount = 1.0}` from the UScriptStruct, call on a live enemy, read `GetHP` | HP falls by exactly 1 and the death path runs at 0 | no HP change, or a crash (then the struct layout is wrong — refuse) |
| 4 | Which function is a fast travel? | `ProcessEvent` name-hook, one teleport (§7) | one of `TeleportToLocation`/`TeleportToTeleporterData`+`BeginTeleport`/`FinishTeleport` | none → the teleporter sets the pawn transform directly |
| 5 | Does the peer's boss HP bar appear when driven? | peer calls `ActivateBossHPBar(copy, groupDA, ctx)` on its driven copy | bar appears, values track the host's row | no bar → the bar reads HP from a local component the copy lacks |

---

## A. Reproduction — the probes, in full

All were run as `python -` with the snippet on stdin from the worktree root
(`G:\Freespace\Queens_Domain\.worktrees\w-research`). Python 3.12, `pefile` 2024.8.26,
`capstone` 5.0.7. Nothing was written outside `mod/doc/`.

### A.1 Common preamble

```python
import sys, struct, bisect
sys.path.insert(0, r"analysis\pak\code")
import pakread, blueprints
TYPES = {"BoolProperty","ByteProperty","IntProperty","FloatProperty","ObjectProperty",
 "StructProperty","ArrayProperty","MapProperty","SetProperty","NameProperty","StrProperty",
 "TextProperty","InterfaceProperty","SoftObjectProperty","ClassProperty","EnumProperty",
 "DelegateProperty","MulticastInlineDelegateProperty","MulticastDelegateProperty",
 "DoubleProperty","Int64Property","UInt16Property","UInt32Property","UInt64Property","Int8Property"}

def open_assets(asset):
    pk = pakread.Pak()
    ua = pk.payload(asset); ux = pk.payload(asset[:-7] + ".uexp")
    info = blueprints.parse(ua)
    imps, exps = blueprints.try_imports_exports(ua, info)
    return pk, ua, ux, info, imps, exps
```

### A.2 A class's Blueprint variables (the tags)

```python
for i, e in enumerate(exps):
    if e["class"] != "BlueprintGeneratedClass": continue
    o = info["tail_fields"][3] + i * 104
    ss, so = struct.unpack("<2q", ua[o+28:o+44])          # SerialSize, SerialOffset
    body = ux[so-len(ua): so-len(ua)+ss]
    seen = set()
    for k in range(len(body) - 24):
        t, n2 = struct.unpack("<ii", body[k:k+8])
        if not (0 <= t < len(names) and names[t] in TYPES) or n2: continue
        nm, num = struct.unpack("<ii", body[k+8:k+16])
        if num or not (0 <= nm < len(names)): continue
        fl, dim, sz = struct.unpack("<iii", body[k+16:k+28])
        if not (0 <= dim < 64 and 0 <= sz < 100000): continue
        if (names[t], names[nm]) in seen: continue
        seen.add((names[t], names[nm]))
        print("   %-28s %s" % (names[t], names[nm]))
```

Output used here: `Enemies/EnemyBase.uasset` → 183 tag records / 180 distinct names,
including `ObjectProperty TargetPlayer`, `BoolProperty Paused`, and **no** `bIsAIDisabled`;
`NPCs/NPC_Base.uasset` → 28 tags ending `BoolProperty DestroyOnEndTalk`,
`BoolProperty BlockInteract`, and including `BoolProperty PlayerIsTalkingTo`;
`Enemies/EnemySpawner.uasset` → 64 tags including `ArrayProperty PairedBossSpawners`,
`ObjectProperty Boss Group DA`, `BoolProperty AllowBossDespawn`, `ByteProperty BehaviourMode`.

### A.3 FName sites and their owning function

```python
f = info["tail_fields"]; base = len(ua); rng = []
for i, e in enumerate(exps):
    o = f[3] + i*104
    ss, so = struct.unpack("<2q", ua[o+28:o+44])
    rng.append((so-base, so-base+ss, e["name"], e["class"]))
rng.sort(); starts = [r[0] for r in rng]
def owner(p):
    k = bisect.bisect_right(starts, p) - 1
    s, en, nm, cl = rng[k]
    return "%s(+%d)" % (nm, p - s) if s <= p < en else "?"
for w in ("TargetPlayer","GetTargetPlayer","bIsAIDisabled","Paused","BlockInteract"):
    idx = names.index(w)                       # 8-byte FName = (idx, 0)
    pat = struct.pack("<ii", idx, 0); p = ux.find(pat)
    while p != -1:
        print("  %-18s %06X -> %s" % (w, p, owner(p))); p = ux.find(pat, p+1)
```

### A.4 Import-reference sites and their owning function

Same `owner()` as A.3, searching for the 4-byte object reference instead of an FName
(import *k* is referenced as the int32 `-(k+1)`, an export as `+(i+1)`):

```python
for k, im in enumerate(imps):
    if im["name"] not in ("ApplyDamage","TakeDamage","GetPlayerCharacter","FS_PlayerChar_C"): continue
    pat = struct.pack("<i", -(k+1)); p = ux.find(pat)
    while p != -1:
        print("  %-20s import %d  %06X -> %s  (prev bytes %s)"
              % (im["name"], k+1, p, owner(p), ux[max(0,p-2):p].hex()))
        p = ux.find(pat, p+1)
```

Opcode values used to read the context (`EExprToken`, UE 4.27): `0x16` EndFunctionParms,
`0x1B` VirtualFunction, `0x1C` FinalFunction, `0x19` Context, `0x1D` IntConst, `0x2E`
DynamicCast, `0x42` StructMemberContext, `0x45` LocalVirtualFunction, `0x46`
LocalFinalFunction, `0x48` LocalOutVariable, `0x51` InterfaceContext, `0x53` EndOfScript.

### A.5 String presence in a cooked package

```python
for w in ("AnyDamage","ReceiveAnyDamage","TakeDamage","TryApplyDamage"):
    print(w, w in info["names"])
```

### A.6 Native pair tables and callers

```python
import marshal
sys.path.insert(0, r"analysis\re")          # qdlib hardcodes the MAIN checkout's exe
import qdlib as Q
P = marshal.load(open(r"G:\Freespace\Queens_Domain\analysis\re\nativepairs.bin","rb"))
C = marshal.load(open(r"G:\Freespace\Queens_Domain\analysis\re\calls.bin","rb"))
print(P["pairs"].get("ApplyDamage"))         # -> [(table, code, name_rva), …]
print(P["pairs"].get("GetTargetPlayer"))
print(C.get(0x029AC810, []))                 # callers of the bShouldBeLoaded setter
print(Q.disasm(0x028D2190, end_rva=0x028D222D))
```

### A.7 The streaming-setter census

```python
# every call from the game module into the ULevelStreaming setter region
for tgt, cs in C.items():
    if not (0x029AC000 <= tgt < 0x029AE000): continue
    for site, func, kind in cs:
        if 0x00D00000 <= func < 0x01000000:
            print("FUN_%08X %s -> 0x%08X (%s)" % (func, kind, tgt, "site %08X" % site))
```

Output is reproduced verbatim in §3.

### A.8 G2 — `SavableActorStates`: a reflected property, its geometry, and its writer

Run from the worktree root with the §A.1 preamble. Every claim below is the output of one
of the three snippets, not an inference from the `NamedSaveData`/`InventoryContents`
geometry.

**A.8.1 The class declares the container (so no RVA is needed).** The class export of
`FS_SaveSlot.uasset` (`BlueprintGeneratedClass`, serial 8 592) carries these tags:

```python
for i, e in enumerate(exps):
    if e["class"] != "BlueprintGeneratedClass": continue
    o = info["tail_fields"][3] + i*104
    ss, so = struct.unpack("<2q", ua[o+28:o+44])
    body = ux[so-len(ua): so-len(ua)+ss]
    # the §A.2 tag scan over `body`
```

```
+0x0024  MapProperty     SavableActorStates            flags=0x200001 size=80
+0x0053  StructProperty  SavableActorStates            flags=0x000001 size=16
+0x0086  IntProperty     SavableActorStates_Value      flags=0x000001 size=4
```

A `MapProperty` with a 16-byte `StructProperty` key and a 4-byte `IntProperty` value is
`TMap<FGuid,int32>`. `FindPropertyOffsetIn(ObjClass(save), "SavableActorStates")` therefore
returns the runtime member offset; `save+0x168` is retired as a lead (it was never a
`SavableActorStates` measurement — the only `+0x168` in the corpus is an unrelated pause
surface read). The class also declares `Inventory` (the same shape) separately from
`InventoryContents`, which is why the inventory lane's offset must not be reused here.

**A.8.2 Geometry — the engine's own `TMap::Find` pins it.** The measured inventory reader
`GetInventoryItemCount` (`0x00E2EA40`) calls `0x007FB070` with the map pointer and a key.
That body is a `TMap::Find` instantiation and reads the whole header:

```
007FB07A  eax = [rcx+8]        ; Data.Num
007FB086  cmp eax, [rcx+0x34]  ; NumFreeIndices -> map empty when equal
007FB08F  rdx = [rcx+0x40]     ; hash array (allocated), else inline at rcx+0x38
007FB119  rax = [rcx+0x48]     ; HashSize (a power of two; mask = HashSize-1)
007FB140  rcx = element index
007FB143  lea rdx,[rcx+rcx*2]  ; index*3
007FB147  cmp [Data+rdx*8], key          ; key at element+0
007FB14D  eax = [Data+rdx*8+0x10]        ; HashNextId at element+sizeof(TPair)
```

For `TMap<UObject*,int32>` (this instantiation) the element stride is `0x18`: `TPair` is
8+4 rounded to 16, and `HashNextId`/`HashIndex` add 8. The same rule on `TMap<FGuid,int32>`
gives:

```
key            FGuid          16 bytes, alignment 4
value          int32           4 bytes
sizeof(TPair)                 20 (0x14)
HashNextId    int32          +0x14
HashIndex     int32          +0x18
stride                        28 (0x1C)
```

This is confirmed independently by the game's own GUID-keyed writer:
`StoreEnemySaveState` (`0x00E3BF10`) calls the `TMap::Add` instantiation at
`0x00E19FC0` with `save+0x270`. That body bakes a 36-byte stride (`lea r8,[rdi+rdi*8]`
then `+4`), writes the 16-byte key with `movups [rsi]`, and writes the value at
`[rsi+0x10]` -- i.e. `sizeof(TPair)=16+12=28` plus the same 8 bytes. So value offset =
key size, stride = `sizeof(TPair)+8`, exactly as for `TMap<FGuid,int32>`.

Header layout (also cross-checked: the cooked tag's `size=80` is `sizeof(FScriptMap)`):

```
map+0x00 Data        map+0x08 Num         map+0x0C Max
map+0x10 AllocationFlags (inline bits)  map+0x20 flags secondary pointer
map+0x30 FirstFreeIndex                 map+0x34 NumFreeIndices
map+0x38 inline hash array              map+0x40 hash array pointer
map+0x48 HashSize
```

Enumerating through the bucket array and `HashNextId` visits only live elements, so no
free-slot heuristic is needed; a free slot in the sparse array never has a bucket.

**A.8.3 The writer is the actor's own `Save Data Index to World Save(Index, Data)`.**
`SavableActor_C`'s class tags are `GUIDs : Array<FGuid>`, `SaveDataCount : int`,
`MySaveData : Array<SavableActorState>`. The function payloads (the same §A.2 scan
applied to each `Function` export) give:

```
Save Data Index to World Save  Index:int, Data:int, then CallFunc_* locals
  Array_Get(GUIDs, Index) -> CurrentGUID
  Map_Add(GetCurrentlyActiveSave().SavableActorStates, CurrentGUID, Data)
  Array_Set(MySaveData, Index, MakeStruct_SavableActorState)
GetSavableActorDataAtIndex     Index:int -> IsSet:bool, ValueAsBool:bool, Value:int
RestoreFromSave                (0 params) -> CacheSaveData(); Restore State From Save Data(MySaveData)
CacheSaveData                  Map_Find(SavableActorStates, GUIDs[i]) -> Array_Add(MySaveData, …)
```

So `Save Data Index to World Save(Index, Data)` is the game's own setter: it **inserts or
updates** the map entry (handling a key that does not exist yet) and keeps `MySaveData`
in step. The read-back oracle is the other direction: `CacheSaveData` copies the map into
`MySaveData`, and `GetSavableActorDataAtIndex(Index)` returns `MySaveData[Index].Value`.

`CacheSaveData` uses `Array_Add` and the package imports no `Array_Clear`/`Array_Remove`;
calling `RestoreFromSave` repeatedly would grow `MySaveData`. The mod therefore calls the
one-shot writer, not the refresh pair, and says so in the G2 notes.

The actor family is checked against the classes the design names:

```
Checkpoint_C            parent = Actor           Map_Clear only; no savable-state writer
BreakableBarricade_C    parent = SavableActor_C  inherits the writer; no own map imports
Door_Base_C             parent = SavableActor_C  inherits the writer; Map_Find only
```

So `Checkpoint_C` is **not** a `SavableActor_C`: checkpoints persist through
`NamedSaveData`/`SaveGameToDisk`, which is why §2.4 and G1 already cover them. The
one-shot objects this lane exists for are the `SavableActor_C` subclasses, and they all
reach the base class's writer through `FindFunction`'s super-chain walk.

The static evidence is in the corpus this file already describes
(`analysis/pak/blueprint-surface.md` §SavableActor); the three probes above are the
reproduction, and the raw tag dumps are the artefacts the G2 implementation quotes.
