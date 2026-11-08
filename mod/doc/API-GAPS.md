# API-GAPS — the four unresolved write-side APIs and the four runtime proofs

Everything here is a **build blocker or a live proof**, not a summary. For each item:
what is known (with RVA and artefact), what is missing, the exact resolution mechanism,
and the fallback if it cannot be resolved. Every item ends with a probe and a pass/fail
criterion — "a check that cannot fail is not a check."

Tiers: **measured** = reproduced this pass by disassembly or by reading the shipped
files; `[P]` = proven in the corpus with an artefact; `[I]` = inference with the chain
stated; HIGH/MEDIUM/LOW as used in the source documents.

Reproduce the machine-code claims with:
`python analysis/re/combat_save_probe.py --evidence` (save paths) and the pair-table scan
in `analysis/re/build_pairs.py`. The disassembly excerpts below were re-read for this
document from `QueensDomain-Win64-Shipping.exe` (sha256 `79CCDC6C…D1E6`,
`SizeOfImage 0x05043000`).

---

## 0. The one pattern all four APIs share

There is no native writer for any of the four. The game itself does the same thing in
these cases: **resolve a `UFunction` by name on an object's class, build a parameter
frame, and call `UObject::ProcessEvent`.** Three measured instances of the pattern:

```
0x00E60250  (NamedSaveData read helper)
    rcx = save object, rdx = FName key, r8 = out bool
    frame at [rsp+0x20]: { FName key @+0, bool wasSet @+8, int32 value @+0xC }
    fn = 0x011A0050(save, keyFName)      ; reads save->Class @+0x10
    call qword ptr [save->vtable + 0x220]        ; ProcessEvent

0x00E2BE50  (UFS_DialogueAction::FinishAction, bool overload)
    obj   = resolve(this+0x38);  name = *(FName*)(this+0x40)
    fn    = 0x011A0050(obj, name)
    frame at [rsp+0x50] = { bool @0 }
    call qword ptr [obj->vtable + 0x220]          @0x00E2BEDE

0x00E2BD90  (the other FinishAction body; target of the 0x00E4E1F0 shim)
    obj   = resolve(this+0x30);  name = *(FName*)(this+0x38)
    fn    = 0x011A0050(obj, name)
    frame = NULL                                  ; zero-argument BP event
    call qword ptr [obj->vtable + 0x220]
```

`ProcessEvent` = `0x011A7AA0`, vtable slot 68 / byte offset `+0x220` (verified: slot
address `0x0376D258` = vtable start `0x0376D038` + `0x220`, 1 818 identical copies).
`0x011A0040` (null-returning) and `0x011A0050` (checked) are the `FindFunction` pair —
they call `0x103DB10(Class, FName, 1)`. **Shape HIGH, names MEDIUM** (derived from call
shape, not from a symbol).

`MOD/native/src/qdreflect.h` already implements this pattern and uses the measured
`0x00E60250` frame as the **oracle**: it refuses to write unless a candidate
`UStruct`/`FProperty` layout reproduces `{FName@0, bool@8, i32@0xC}` byte for byte. That
refusal is the correct behaviour and should not be relaxed.

---

## 1. `NamedSaveData` write entry

**Known.**

- Read chain, measured end to end:
  `GetNamedSaveData` impl `0x00E2F6D0` (pair table `0x0395D6E0`, thunk `0x00E61800`)
  → `GetActiveBaseSave` `0x00E2D100` → `0x00E60250`, which builds
  `{FName key; bool wasSet; int32 value}` and dispatches through `ProcessEvent`.
  `GetNamedSaveData`'s own signature is `int32 (UObject* WorldContext, FName Key,
  bool* outWasSet)` — `rcx` is untouched before the `GetActiveBaseSave` call and the
  `int32` returns in `eax` [measured].
- Writer: `SaveFunctions_C.SaveNamedData` and `SaveFunctions_C.SaveNamedData_Advanced`
  are **Blueprint functions with no native implementation**. The 24 exported UFUNCTIONs
  are listed in `analysis/pak/blueprint-surface.md` §SaveFunctions (`AddItemToInventory`,
  `TryConsumingItemFromInventory`, `SaveNamedData`, `SaveNamedData_Advanced`,
  `GetNamedSaveData`, `RegisterEnemyKill`, …). A pair-table scan of `nativepairs.bin`
  returns `[]` for `SaveNamedData`, `SaveNamedData_Advanced` and `SaveNamedData`'s
  `SetNamedSaveData` alias [measured].
- The game's own write path is
  `ActivatableGroup/NamedSaveDataSetterVolume → ActivatableNamedSaveDataSetter
  .SaveNamedData_Advanced(NSDs[]) → SaveFunctions_C → FS_SaveSlot.NamedSaveData[key] =
  value → FS_NamedSaveDataChange → WorldEventReceiver.OnQuestStateUpdated →
  ActorQuestStateManager.RefreshStates → ActorQuestState.SetQuestStateActive/Inactive`
  `[P]` `23` §5.
- The container offset is **not** located natively: every observed native access to
  `NamedSaveData` goes through the reflective helper, not a `lea save+off`. (Contrast
  `InventoryContents` at `+0x28`, `EnemySaveStates` at `+0x270`, the drop cache at
  `+0x220`, all with direct `lea` sites.)

**Missing.** The parameter list and order of `SaveNamedData`; whether it takes a
`UObject*` world context, an `FName`, an `int32`, and in what order; whether it fires
`FS_NamedSaveDataChange` itself (it must, or the quest manager would never refresh);
whether it uses `GetActiveBaseSave` internally or needs the save object as `self`;
whether `SaveNamedData_Advanced` is the one that takes `NSDs[]` plus a mode
(`ENSDCM_Add` / `ENSDCM_Set`).

**Resolution mechanism.** In this order, and stop at the first failure:

1. Resolve the class: scan `GUObjectArray` (`0x04A64250`, stride `0x18`) for
   `Default__SaveFunctions_C`, or walk the FName pool (`0x04A27F00`) for the class and
   take `UClass*` from the object header `+0x10`.
2. `FindFunctionChecked(SaveFunctionsClass, FName("SaveNamedData"))` via `0x011A0050`.
3. Walk the `UFunction`'s `PropertyLink` chain to recover each parameter's name, type and
   `Offset_Internal`, i.e. the parameter frame. **Accept the layout only if the same walk
   applied to `GetNamedSaveData` reproduces the measured `{FName@0, bool@8, i32@0xC}`
   frame.** This is the oracle; without it, offsets are guesses.
4. Build the frame, call `ProcessEvent` on the `SaveFunctions_C` CDO (or on the live save
   object if step 1 shows the writer is an instance method).
5. **Read back with `0x00E2F6D0`** and require `wasSet == true` and `value ==` what was
   written. "The call returned" is not evidence: a wrong-class `ProcessEvent` returns
   successfully and changes nothing `[P]` DollsMP F-12.17 / `03-dollsmp-reuse.md` §C.8.3.
6. Bind `FS_NamedSaveDataChange` before the call and require it to fire; that is what
   proves the *game* noticed, not just the memory.

**Fallback.** Two, in order:

1. **Direct container write**, once the `NamedSaveData` member offset and element stride
   are discovered with an oracle (write with the game's own function once, then scan for
   the map that changed; or read a key the game wrote and find which map returns it).
   Element geometry for the sibling `InventoryContents` is stride `0x18`, value `+8`
   [measured] — do **not** assume the same stride for `Map<FName,int32>`.
2. **Degrade to host-only progression**: the peer's dialogue still runs as a view, but
   any flag write is refused and the peer is told so on the HUD. The session remains
   consistent; only the peer cannot advance a quest. This is the only fallback that is
   safe without the write path, and it should be the default while the switch is off
   (`qdconfig.h`: writes default off).

Never write the `.sav` file from disk mid-session. DollsMP measured that the module must
not save mid-play — it crashed `[P]` doc 50 / `03` §C.6.

---

## 2. Inventory grant

**Known.**

- `AddItemToInventory` is Blueprint: no pair-table entry [measured], one of the 24
  `SaveFunctions_C` exports. Siblings: `TryConsumingItemFromInventory`,
  `FULLY Delete Item From Inventory`.
- Native **readers** exist and are callable:
  `GetInventoryItemCount` thunk `0x00E61310` (table `0x0395D6E0`) → impl `0x00E2EA40`;
  `GetInventoryItemCountSoft` thunk `0x00E613E0`. The impl body is measured and pins the
  container:

  ```
  0x00E2EA40  (worldctx, item) -> int32
    call 0x00E2D100                  ; GetActiveBaseSave
    lea  rbx, [rax + 0x28]           ; InventoryContents
    call 0x007FB070                  ; TMap::Find(map, item, &index)
    lea  rdx, [rax + rax*2]          ; 3*index
    mov  rax, [rbx]                  ; map.Data
    lea  rcx, [rax + rdx*8]          ; element = data + index*0x18
    mov  eax, [rcx + 8]              ; value = count
  ```

  So `InventoryContents` = `TMap<UObject*, int32>` at `save+0x28`, **element stride
  0x18**, value at `+8`. (`12` is the *drop-cache key* size, not a TMap element size —
  `qdsave.h`'s `MapProbe` candidates `{12,16}` must be extended to include `0x18`.)
- `ItemChangedEvent` is a live `MulticastInlineDelegateProperty` on the save object, and
  `GI_NotifyInventoryChanged` is in `SaveFunctions_C`'s import list `[P]` `05` §4,
  `analysis/pak/blueprint-surface.md`.
- Identity is the full soft object path (e.g. `/Game/Items/KeyItems/AquaDoorKey_2
  .AquaDoorKey_2`); the int is the stack count in `InventoryContents`, an equip-slot token
  in `Equipment`, and `EquipmentSlots` is its exact inverse `[P]` C7.

**Missing.** The BP writer's parameter list (item class vs soft object ref vs path text;
count; world context?), whether it merges stacks, whether it fires `ItemChangedEvent`
itself, and whether it applies equipment tags/bestiary side effects that the quest and
challenge layers read (`cha_Quest_Mushroom` is an inventory condition; `cha_Item_GotAllAcc`
is an item-tag condition).

**Resolution mechanism.** Same recipe as §1, with a different oracle — here the oracle is
the native getter:

1. Resolve `SaveFunctions_C`, find `AddItemToInventory`.
2. Recover the parameter layout by the property walk; sanity-check against the known fact
   that both a `Count` and a soft reference variant exist (two readers), so the function
   probably takes `(item, count)` rather than a path string.
3. Call, then **read back** with `0x00E2EA40` / the `GetInventoryItemCountSoft` thunk and
   require the count to rise by exactly the requested amount.
4. Bind `ItemChangedEvent` before the call and require it to fire, so the HUD and the
   challenge evaluator see it.

**Fallback.**

1. **Direct map write**: `TMap::Add` (`0x00E19FC0`, the same primitive
   `StoreEnemySaveState` uses) on `save+0x28` with the measured 0x18 stride, then
   broadcast `ItemChangedEvent` by hand. Risk: anything that recomputes from
   `ItemTags`/bestiary rather than from the map will not update (MEDIUM).
2. **Host-driven pickup**: spawn/drive the pickup actor in front of the peer's pawn on the
   host so the game's own pickup path runs. No reflective call, no direct memory write;
   costs a visible item and a frame of duplication.

---

## 3. Dialogue action invocation

**Known.**

- The native layer is a **driver that dispatches into Blueprint and stops**. Measured
  pair-table entries (exec thunks) for `FinishAction`: `0x00E4AED0` @ table `0x039407A8`,
  `0x00E4B440` @ `0x03941A68`, `0x00E4E1F0` @ `0x03944A98`, `0x00E4E210` @ `0x03944EF0`.
  For `RunAction`: `0x00E4AF60` @ `0x039407A8`, `0x00E4B650` @ `0x03941A68`,
  `0x00E4ECA0` @ `0x03944A98`, `0x00E4EE00` @ `0x03944EF0`. For `InitAction`:
  `0x00E4B590` @ `0x03941A68`, `0x00E4E6A0` @ `0x03944EF0` [measured].
- **Three of those four are not implementations.**
  `0x00E4AED0` and `0x00E4B440` are **lazy resolver stubs** (`call 0x0119D280`, return the
  cached function pointer) — the "unusable copy" the corpus warns about;
  `0x00E4E1F0` is a shim that `jmp 0x00E2BD90`. The only two real bodies are
  **`0x00E2BE50`** (bool overload) and **`0x00E2BD90`** (zero-arg). Both build a frame and
  call `ProcessEvent` (see §0).
- The two bodies read the BP event **name from the action object's own data**
  (`+0x40` for the bool variant, `+0x38` for the zero-arg variant) and the target object
  from the adjacent field. So an action object *names* its own Blueprint event; it is data,
  not a build constant.
- `JumpToDialogue` `0x00E34E10` dispatches to BP at `0x00E34E9A`; the condition evaluator
  is `0x00EAE0B0` (shared by `EvaluateConditionSet` and `EvaluateSingleCondition`)
  `[P]` `13-dialogue-ui-answers.md` D1.1.
- `RunActionWithinContext` has **no pair-table entry at all** [measured: `[]`]. It exists
  in the exe name pool (`analysis/exe_names.txt:18279`) and is referenced by action assets
  (`Sipeha_BanishDialogueAction.uasset`) together with `FinishAction`, as a begin/end
  fence `[P]` `analysis/pak/answers-2.md` §D2. It is therefore a **Blueprint function** on
  the action BP, not a native callable.
- The native bodies contain **no byte-sized field access on `this`** for
  `InitAction`/`RunAction`/`FinishAction`/`CancelAction`/`JumpToDialogue`, i.e. the action
  object is stateless with respect to "who is talking"; exclusivity lives in Blueprint
  `[P]`.

**Missing.** For each action BP, the pair `(UClass, UFunction)` to call and the arity of
`RunActionWithinContext`; whether it needs a context argument (the name says "within
context", and `UFS_DialogueAction::RunAction` has two class copies, so an overload with a
context is plausible); and whether calling `FinishAction` after `RunActionWithinContext`
is mandatory for cleanup (the begin/end fence suggests yes).

**Resolution mechanism.**

1. Read the action object's event-name fields (`+0x38`/`+0x40` depending on the class
   generation) to learn *which* BP event the game itself would call. This is a data read,
   not a guess, and it collapses the search space to one name.
2. Resolve the action object's class, then `FindFunctionChecked` on the named event **and**
   on `RunActionWithinContext`.
3. Recover `RunActionWithinContext`'s parameter layout by the property walk; validate the
   walk against the measured `{bool@0}` frame of `0x00E2BE50` (same object family, so the
   property-walk offsets must agree).
4. Call `InitAction` → `RunActionWithinContext` → `FinishAction` in that order, on the game
   thread, and verify by **effect**: the flag/inventory change the action is supposed to
   produce, read back through §1/§2.

**Fallback.** Do not invoke actions. Treat a dialogue action as an **intent producer**:
the peer's dialogue runs presentation-only; when it reaches a choice or a reward, it emits
`INTENT` kinds 1/2/4 and the host applies the effect through its own locally-driven
dialogue (the host walks the same NPC with the game's own input path, which is proven:
`PostMessage` reaches keyboard verbs, `SendInput` reaches mouse buttons). The peer sees the
conversation, the world advances on the host, and the peer converges on the echo. This is
the safe default and is what §2.7 of `SYNC-DESIGN.md` assumes.

---

## 4. Travel — `UGameplayStatics::OpenLevel`

**Known.**

- Pair-table entries: `0x02DB8360` @ table `0x03F98D28` (dispatcher copy) and
  `0x02DC1510` @ `0x03F9AC10` (the real copy, same table as `ApplyDamage`,
  `SaveGameToSlot`, `SetGamePaused`). `OpenLevelBySoftObjectPtr` also exists in the name
  pool `[measured]`.
- The world is **one persistent level**, `Ruins_Level_00`, with 42 dynamically streamed
  sublevels; hard transitions are separate maps (`997_FinalZoneCutscene`,
  `998_QueenCutscenes`, `999_SipehaCutscenes`, `900_SipehaWorld`, `990_B33sment`). So the
  common "travel" is streaming (§2.8 of `SYNC-DESIGN.md`), not `OpenLevel` `[P]` `05` §3.
- The live save object is cached: `GetActiveBaseSave` `0x00E2D100` returns
  `[GameInstance + 0x210]` and only constructs/loads when that is null (its body branches
  on `cmp qword ptr [rbx+0x210], 0`). All four container paths depend on it.
- Every save write funnels through `SaveGameToSlot` `0x028F2B40`, called only from
  `FB_SaveUtils`; the slot name is composed at runtime by `GetSaveSlotName` `0x00E306D0`
  `[P]` `14` C9-p1/p2/p3.
- Fast travel is a placed-actor system: 43 teleporter actors, 10 real destinations
  matching the 10 `cha_Area_*` areas; 34 `Checkpoint_C` save points `[P]` `10` C11.

**Missing.** Whether `OpenLevel` invalidates the cached save object at
`GameInstance+0x210` (and the streaming manager at `+0x3B0`); the exact level-name form
(FName vs `FSoftObjectPath` overload) and the options string; and the ordering against
(a) the streaming union, (b) an in-flight save write, (c) the peer's acknowledged
transition.

**Resolution mechanism.**

1. **Prefer the game's own transition path.** Teleporters and checkpoints already do
   travel correctly on one machine; the mod's job is to make both machines take the same
   transition, not to call `OpenLevel` itself. That is `INTENT` kind 5 plus an
   acknowledged transition event, and it needs no new API.
2. If `OpenLevel` is unavoidable, call the **real** copy (`0x02DC1510`'s target, not the
   `0x03F98D28` dispatcher) on the game thread, and then **prove the cache was
   invalidated**: `GetActiveBaseSave` must return non-null, `[GI+0x210]` must not still
   point at the pre-travel save, and `GetLevelManager` `0x00E2EE50` must return a manager
   whose `+0x320` list matches the new world.
3. Verify with `SaveLevel` on both machines (the join handshake key, `06` §4.8 rule 4) and
   by re-applying the three absolute bodies (§2.2/§2.3/§2.4) after arrival.

**Fallback.** Refuse hard travel in co-op v1: both players quit to menu, the host travels,
the peer re-joins (the `reopen_barrier` path — readiness cleared, `started` latched, so a
stale `ClientReady` cannot re-close it). Honest, cheap, and consistent with the fact that
the persistent level makes streaming the common case.

---

## 5. The four runtime proofs

### B7 — the puppet spawn (deferred pair)

**Known.** The game module has **no** character-construction path: 78 class
registrations, 20 native tables, exactly three `Spawn*` UFUNCTIONs
(`CheckSpawnerMaxDist`, `CheckSpawnerVis`, `DoSpawnerUpdate`), zero
`Construct`/`Create`/`Possess`/`Merge`; both named candidates
(`AFS_StationarySkeletalMeshActor`, `UFS_ConfigurableStaticMeshComponent`) declare no
UFUNCTIONs and only configure visuals `[P]` `12-puppet-answers.md` B7. The engine pair:

```
BeginDeferredActorSpawnFromClass 0x028D3C90  (thunk 0x02DB93C0) -> UWorld::SpawnActor 0x029AD950 (site 0x028D3DA3)
FinishSpawningActor              0x028DCD50  (thunk 0x02DBDF80) -> AActor::FinishSpawning 0x02651580 (site 0x028DCD64)
```

Spawn `FS_Full_PlayerChar_C` (the live possessed class). The body mesh must be assigned
(§SYNC 2.1): `CharacterMesh0` holds no `SkeletalMesh` while `Arms` holds one at `+0x480`.

**A direct `SpawnActor` already failed.** `_live/d9_spawn.txt` line 16-21: the close-out
probe's `spawn` command returned `{"err":"Error: access violation accessing
0x1200000012"}` and produced no actor; only the naturally-spawned `Dragon_Green_BP_C`
appeared afterwards. The probe called `0x029AD950` as
`(world, class, loc*, rot*, zeroed 0x100-byte parms)` from a live `NativeFunction`
(`_live/frida_close.js:372-382`). The fault address `0x1200000012` is a garbage pointer
read, not a null deref, which is what a wrong argument class produces: two suspects, both
`[I]` — (a) `FActorSpawnParameters` is a `const&`, and the probe's zeroed 0x100-byte block
was never validated against the game's own use of the struct, so a field read out of it
can be garbage; (b) the provenance of the `0x17cf950a880` argument as a `UClass*` (rather
than a CDO or a default object) was not proven. The AV is **measured**; the diagnosis is
**`[I]` MEDIUM**. What it does *not* prove is that spawning is impossible — the deferred
pair is a different entry with a documented argument layout (world@0, class@8,
transform@16 align 16, collision@64, owner@72, ret@80 in the `ProcessEvent` parameter
buffer; the direct call uses registers instead).
(Artefact also records the corpus lessons: a Blueprint-class `SpawnActor` crashes unless it
runs inside the game thread, a game-thread call is **queued** so the actor does not exist
when the caller looks, and destroying a BP actor tens of ms after `SpawnActor` returns is
an AV.)

**Probe (closes it).** On the game thread, from a tick hook:

1. `BeginDeferredActorSpawnFromClass(world, FS_Full_PlayerChar_C, &transform, &params)`
   on the game thread, calling the native body `0x028D3C90` directly (registers, not a
   `ProcessEvent` frame), with `FActorSpawnParameters` sized from the game's own use.
2. Read back the returned actor **on the next tick**, not immediately.
3. Assign the full-body mesh and call `FinishSpawningActor(actor, &transform)`.
4. Hold it alive for 60 s, applying a transform each second.

**Pass:** the actor exists in `GUObjectArray` (`0x04A64250`) with class
`FS_Full_PlayerChar_C`, `CharacterMesh0` resolves a `SkeletalMesh` at `+0x480`, the
transform follows the target within 1 cm, no AV, and the actor is still alive after 60 s.
**Fail:** any AV, the actor is `PendingKill`/absent on the next tick, the mesh stays null,
or the process loses a frame (> 500 ms) during construction.

**Status: implemented behind `PlayerBody=1`, unproven.** The code path exists
(`proxy.cpp`: `SpawnPlayerBodyActor` / `SpawnPlayerBody`, `qdplayerbody.h`), and it departs
from the probe above in three ways, each with a reason:

- it spawns `FS_PlayerChar_C` (the class the enemy's cast gate names) and falls back to
  `FS_Full_PlayerChar_C`, because the "Full" class is the one the live game possesses but
  the gate is written against the base name;
- the visual body is **not** taken from `CharacterMesh0`: a poseable component carrying
  `Player_FullChar_2026` is attached to the capsule and posed every frame by the same code
  the mesh-actor puppet uses, so the appearance the live pass already accepted does not
  depend on this spawn at all. The Blueprint's own `Arms`/`CharacterMesh0`/`Mesh` are made
  invisible;
- `AutoPossessPlayer` and `AutoPossessAI` are cleared between `BeginDeferred` and
  `FinishSpawning`. That is the one catastrophic failure mode: if the CDO auto-possessed
  player 0, the host's own player would be unpossessed and the mod would read the proxy as
  "local" in every lane. A post-spawn `LocalPawn() == actor` check destroys the body and
  logs if it happened anyway.

The actor is unpossessed, tickless, non-colliding and has its movement disabled. What the
live run has to prove is the part no test can: that the engine's construction of a full
gameplay Blueprint (its `ReceiveBeginPlay`) neither faults nor damages the session, that
the two cast gates now pass, and that a hit on it arrives at `TryApplyDamage` where the mod
can forward it. `SYNC-GAPS` §5.1 lists the exact log lines, in order.

### C10 — level-union end-to-end

**Known.** The full manager chain is pinned (`AddLevelListToStack` `0x00E215E0` → worker
`0x00E37800`; `FlushLevelManagerActions` `0x00E2C270` → `0x00E2C306` → `0x00E21E40`;
`SetShouldBeVisible` `0x029ACB20` → `0x02D4A0E0`; per-frame `0x00E3CF9E`), and the naive
apply was measured to **fail**:

```
stack after adds: {GreenRuins, AutumnIsland(100), AutumnIslandCastle(100)}
visible before:  21
visible after Add + Flush(ctx,1): 11
visible set:     500_AUT 510_AUT_droptunnel 610_Castle_Ext 760_SZ_AUT
                 990_B33sment Intro_Level_01 000_Shared_Base_Lighting
loaded-hidden:   39  (the GreenRuins levels)
```

So `Add` + `Flush` **replaces** the visible set with the last action's resolved list even
though the stack holds the union `[P]` `22` §4.

**Probe.** Two players forced into different areas (host `100_GR`, peer `500_AUT`).
Record each machine's baseline visible-level set and loaded-level set. Then, on the host:
`AddLevelListToStack(worldctx, peerList, /*sel*/0, /*usePrio*/1, /*prio*/100)`, drive the
per-frame path (`0x00E3CF9E` / `0xE42950`), and finally re-assert
`SetShouldBeVisible(true)` for every level in the union.

**Pass:** visible set after ≥ 10 s is a **superset** of both baselines; `+0xBA` bit 3 is
set on every union level; the enemy count in each area is unchanged over 60 s; the union
levels' actors exist (e.g. `Merchant_NPC_BP_C` present when `140_GR_ShopIsleOnly` is in the
union). **Fail:** visible count drops below either baseline, any union level is
loaded-hidden, or an enemy in a union area is re-created with a new address (the "lost its
brain" symptom).

### D4 — multi-GUID spawner rotation

**Known.** Among 66 multi-GUID `GUIDs` placements, exactly **one** is an enemy spawner:
`LargeRobot_Rusted_Spawner` in `500_AUT` (export `0x1584D79`, `ArrayProperty` size 85 = 2
GUIDs). Read live: `GUIDs` = `dfdda115-4ccfdafe-ee80c696-71bf25cc` and
`5071a6ed-41b1d2d3-201eaf9b-64a32d73`; `SaveDataCount = 2` (two `MySaveData` slots);
`minSpawnDist 5000`, `maxSpawnDist 10000`, `maxKill 1`, `useMaxKill 1`, at
`(-48756.8, 60032.5, -33470.1)` `[P]` `22` §3. The tested single-GUID `Dragonfly_Spawner_C`
did **not** rotate across two kills 121 s apart and across a process restart (same GUID
`5ae3b37b-…`) `[P]` `18-d4-respawn.md`. The multi-GUID spawner never entered
`playerInRange` on a teleport (it wants the game's own arena activation), so rotation was
**not** exercised — that is a limit of the test, not of the identity.

**Probe.** Show `500_AUT`, activate the arena through the game's own path, then kill its
enemy twice with a `RespawnTime` gap. Hook `StoreEnemySaveState` `0x00E3BF10` and record
the `FGuid` argument each time; read `GetSaveGUID()` on the spawner before kill 1, between
kills, and after kill 2.

**Pass:** the key written is one of the spawner's two authored GUIDs, matches
`GetSaveGUID()` at the moment of the write, and is stable across the two kills. **Fail:**
the written GUID is not in the spawner's `GUIDs` array, or `GetSaveGUID()` and the written
key disagree (then keying by spawner is wrong and the design needs rework). If the key
*does* rotate between the two authored GUIDs, that is not a fail — it is a finding that the
mod must key on the runtime value and must re-read it on every death.

**Fallback if it cannot be resolved:** keep one key per spawner (the design rule
`GetSaveGUID()`, never `GUIDs[0]`), and treat the mod's copy of the enemy as attached to
the *live spawner actor address* plus the GUID it reported at spawn time; a rotation then
appears as "the old key stops being updated, a new key starts", which the host mirrors
naturally because it publishes absolute state.

### Two-player dialogue

**Known.** Dialogue does **not** pause: no native caller of `SetGamePaused`; the native
dialogue layer reaches zero pause/dilation/tick sinks over 73 nodes to depth 6; the
dialogue packages reference no pause entry point; and the game freezes enemies
individually via a `TimeDilationManager` → `EnemyBase.CustomTimeDilation` while locking
player input with `DisableAllPlayerInput` `[P]` `13b`, `13-dialogue-ui-answers.md`
(HIGH 0.85). The local lock is three cooperating per-machine guards: `RunningDialogue`
(player bool), `PlayerIsTalkingTo` + `BlockInteract`/`ReenableInteract` (`NPC_Base`), and
the input gate `[P]`. There is **no cross-machine arbitration** anywhere `[P]`.

One falsifier is **not** retired: a `SetGamePaused` node could hide inside
`FS_PlayerChar::SetUpForDialogue` / `PrepareForDialogue` (bytecode not parsed). It is one
bit of runtime evidence, and this probe settles it.

**Probe.** Both machines open the same NPC within 1 s (use `PostMessage` `E` — keyboard
verbs are injectable; `SendMessage`/`SendInput` are not needed for interact). Sample
`UWorld::TimeSeconds` `+0x5A0` at 10 Hz on both, for 30 s; bind `FS_NamedSaveDataChange`;
count writes of the flag the NPC's action writes.

**Pass:** both dialogues open (neither `BlockInteract` blocks the other machine), both
advance independently, `TimeSeconds` slope stays in 0.95–1.05 on **both** machines, the
host writes the flag exactly once, and the peer does not duplicate the write on the echo.
**Fail:** either clock freezes (then `SetUpForDialogue` does pause and §2.10 must cover
dialogue too), one player is locked out (a global lock leaked in), or the flag is written
twice.

---

## 5A. The two write paths that failed live, and what the game says the shape is

LIVE-TEST-LOG Session 6 recorded two failures that the transport did not explain: the G3
inventory grant read `count did not rise by the requested amount: 0 -> 0` for every key
item, and the G4 damage lane read `player damage had NO effect: TryApplyDamage ran asked
20.0 returned 0.00 hp 400.0 -> 400.0`. Both are now root-caused from the **cooked
packages**, not from a guess, and both were in the module's assumption about the game
rather than in the state machine — the pattern every previous round found.

### 5A.1 G3 — the `SoftObjectProperty` frame was written 16 bytes too low

**What the live log measured.** The signature block is recorded verbatim on both
instances, and it is the same on both:

```
write api: GetNamedSaveData=... AddItemToInventory=1DF5D64F040
signature AddItemToInventory props=3 parms=56 ret@-1
    Item : SoftObjectProperty @0 size=40 flags=00000094
    Count : IntProperty @40 size=4 flags=40000294
    __WorldContext : ObjectProperty @48 size=8 flags=40000294
```

So the parameter list, the order and the `__WorldContext` position the module already
handles are **right**. `size=40` is the tell: this build reflects a 40-byte
`FSoftObjectPtr`, whose first field is a *weak object pointer*, not the path.

**What `FSoftObjectPtr` is on this build (40 bytes, measured three ways in this tree):**

```
+0x00  FWeakObjectPtr   int32 ObjectIndex; int32 ObjectSerialNumber     (8)
+0x08  int32 TagAtLastTest + 4 bytes padding                            (8)
+0x10  FSoftObjectPath  FName AssetPathName                             (8)
+0x18                   FString SubPathString { wchar_t* Data; int32 Num; int32 Max } (16)
                                                               total 0x28 = 40
```

The three confirmations are not new work — they were already correct in the same source
file and its neighbours, which is why the differential is visible:

* `qdavatar.h` `LoadAsset` (`KismetSystemLibrary::LoadAsset_Blocking`) writes the FName at
  `p + 16` and documents the class as "FWeakObjectPtr(8), TagAtLastTest(4)+pad(4), then
  FSoftObjectPath{...} — 40 bytes".
* `proxy.cpp` `DevGive` calls **this same** `AddItemToInventory` and writes the FName at
  `at + 16`.
* `proxy.cpp` `LevelNameOf` reads `TSoftObjectPtr<UWorld>::AssetPathName` at `+0x10`.

**Root cause.** `qdwrite.h`'s `PutSoftItem` wrote the FName at `+0` and the `FString` at
`+8/+16/+20`:

```c
*(QdName*)raw = an;                     // +0x00  <- should be +0x10
*(wchar_t**)(raw + 8) = ... wide;       // +0x08  <- should be +0x18
*(int32_t*)(raw + 16) = wn;             // +0x10  <- inside AssetPathName
*(int32_t*)(raw + 20) = wideCap;        // +0x14  <- inside AssetPathName
```

Every field landed 16 bytes early. The engine therefore read the asset name as
`ObjectIndex|ObjectSerialNumber` plus the sub-path's length (~`0x0000000C` for a
5-character object name) — a package that does not exist — and the soft reference resolved
to nothing. `ProcessEvent` still returned successfully, which is exactly
`API-GAPS` §0's "a wrong frame returns successfully and changes nothing", and the native
counter oracle correctly reported `0 -> 0` instead of claiming success.

**Second, independent reason the add could fail even with a correct path.** The game's own
`AddItemToInventory` script (`SaveFunctions.uasset`, 1654-byte script, disassembled from
the cooked package) opens with

```
@0x00B NewItem   @0x02B IMP#77 IsValidSoftObjectReference   @0x01E CallFunc_..._ReturnValue
```

i.e. its whole body sits behind `IsValidSoftObjectReference(Item)`. A soft record with a
correct path but a **zero weak pointer** is only valid if the engine has already resolved
that path; an unresolved one is rejected and the function returns with nothing done. The
item assets are loaded (`FS_ItemData_C` instances), so the weak pointer can be filled
exactly as the engine would: `{InternalIndex, FUObjectItem.SerialNumber}`.

**Third: the read-back key.** The script then does
`Conv_SoftObjectReferenceToObject(Item)` → `K2Node_DynamicCast_AsFS_Item_Data` and drops
the result when the cast fails. So `InventoryContents` is keyed on the **`FS_ItemData_C`
asset object**. The old `ResolveItemObject` asked for `FindClassByName(<last segment>)`
first, which returns a `*_C` class — a value the map can never hold — turning an
unresolved item into a permanent `0`.

**Fix (qdwrite.h), all three parts:**

1. `PutSoftItem` writes `FName @+0x10` and `FString{Data @+0x18, Num @+0x20, Max @+0x24}`,
   with the named constants `kSoftWeakOff/kSoftPathNameOff/kSoftSubPathOff/kSoftMinSize`
   and a size check against `kSoftMinSize` (40).
2. `PutWeakObject` fills `{InternalIndex, SerialNumber}` at `+0`; a record with neither a
   weak pointer nor a sub-path is **refused** rather than passed to the game.
3. `ResolveItemObject` prefers an `FS_ItemData_C` object and never returns a class.

**What the third-party reference proves, and what it does not.** A working reflection-only
mod for this build reaches the same function as `Default__SaveFunctions_C ->
AddItemToInventory(UObject* item, int count)` and then calls
`FullyRefreshInventoryTagCount` + `GI_NotifyInventoryChanged` + `GI_QueueSystemSave`.
Reading the game's own script settles the post-call question: `AddItemToInventory` already
ends with `GetGameInstance` → cast → **`GI_NotifyInventoryChanged`** and nothing else, and
`FullyRefreshInventoryTagCount` is not owned by `SaveFunctions_C` at all (it is a bare
`Function` import, resolved by whichever caller wants it). So the grant made through
`ProcessEvent` has already run the game's notification; calling it again would be a second
broadcast per pickup. **No post-call refresh is added for that reason** (§5A.3 keeps the
call sites so a later build can be checked against them).

**Oracle.** Unchanged and correct: the native counter
`0x00E2EA40(world, item) -> int32` (`Map_Find` on `save+0x28`, element stride `0x18`,
value `+8`), read before and after and required to rise by exactly the requested amount.
It is the reason this bug was *reported* instead of silently corrupting the save.

### 5A.2 G4 — the player's `TryApplyDamage` returns 0 because of its own gate chain

**What is not the difference.** `FS_PlayerChar_C.TryApplyDamage` is a real Function export
(`FS_PlayerChar.uasset` export #578, 8044-byte serial, 4929-byte script), and its parameter
list is the same shape as the enemy's — read out of the cooked package:

| class | struct #1 | struct #2 | object | struct #3 (out) |
|---|---|---|---|---|
| `FS_PlayerChar` | `Hit` | `DamageToApply` | `Applier` | `AppliedDamage` |
| `EnemyBase` | `Hit` | `DamageToApply` | `Applier` | `AppliedDamage` |
| `NPC_Base` | `Hit` | `DamageToApply` | `Applier` | — |

The payload is `FB_DamageAttributeSet`, resolved from reflection once and shared by both
directions: `DamageAmount : FloatProperty @0 size 4`, `DamageTags : StructProperty @8
size 32`, `DataTags : MapProperty @40`, `AdditionalData : SetProperty @120`,
`SourceGUID : StructProperty @200` (live log, `damage: FB_DamageAttributeSet fields=5`).
The enemy lane applies through it in full (`hp 34.9 -> -165.0`).

**What is the difference.** The player's script is a long gate chain, and the HP write is
behind all of it. Disassembling the script and reading the `EX_Let` targets and the
`EX_JumpIfNot`s gives the order:

```
0x0005  IncomingDamageMult            <- multiplicative pipeline, read first
0x0104  AppliedDamage                 <- the out parameter, initialized
0x0206  Applier (dynamic cast)        <- cast Applier to Enemy_Base
0x0308  HasPitInvincibility / 0x0319 KnockedDown
0x0393  Damage.Tags.IgnorePitInvincibility
0x03A6  Damage.Tags.IgnoreKnockdownInvincibility
0x041E  IsDead
0x0589  EX_JumpIfNot -> 0x0F35        <- skips everything below
0x059F  CalculatedAppliedDamage       <- GetSimpleSumStat / Multiply / Add
0x0698  TakenDamageMultiplier
0x0831  BooleanAND                    0x083A  bCanBeDamaged (Actor)
0x0858  EX_JumpIfNot -> 0x0EAE        <- skips the Health write
0x086E  EX_Let Health                 <- the ONLY HP assignment
```

So `AppliedDamage` stays `0.0` — the live line's `returned 0.00` is the function's own
answer, not a missed return slot — and `Health` is never assigned. Note also that
`ret` in the module was reading the *ReturnValue* slot, which this function does not have
(`AppliedDamage` is an out parameter), so `returned 0.00` is a diagnostic, not an oracle;
the code now says so.

**Why the module cannot fix the gate chain by reflection.** Every gate reads a stat, a
status or a gameplay tag the module does not own: `IncomingDamageMult`,
`TakenDamageMultiplier`, `CalculatedAppliedDamage` (from `GetSimpleSumStat`),
`IsDead`, `KnockedDown`, `HasPitInvincibility`, and `bCanBeDamaged` on the actor. Faking
them would mean writing gameplay state from the sync lane, which is precisely what this
document forbids (§0). And there is no second entry point to use instead: `Kill` is a
death verb, not a damage one.

**The fix: try the message, then write the one scalar the game writes.**
`proxy.cpp`'s `ApplyDamageToActor` already tried the message first; it now, **for the
local player only** and **only when the message produced no HP movement at all**, writes
the class's own `Health` `FloatProperty` to the clamped result and calls the game's death
entry when it reaches 0. The clamp is a pure rule, `enemydmg::ApplyHpDamage(before,
amount)`, so it is unit-tested (`test_enemydmg.cpp`, 8 assertions) instead of discovered
in a game run.

The `Health` property is the right scalar and is demonstrably live and writable: the test
rig's own `DevGod` reads and restores it, and it has watched real gameplay damage arrive
through it (`god: health refilled 140.0 -> 400.0`, `god: health refilled 100.0 -> 400.0`),
and the class's own `Set Health Points(NewVal: float)` writes the same property. The
death entry is `FS_PlayerChar_C:Kill(B33smentDeath: bool)` — a 347-byte script whose only
parameter is that bool (measured from the player package); `DeathAnim` and
`FellInDeathPit` exist as separate verbs and are deliberately not used, because `Kill` is
the ordinary death and the other two are the animation and the pit, not a damage outcome.

**Consistency with B7.** `PlayerBodyObserve` is untouched. It still intercepts
`TryApplyDamage` on the **proxy** body, still forwards `K_DAMAGE` to the machine that owns
that player, and still answers the Blueprint's own question honestly. The receive lane on
that machine is the one that now has the fallback, so the two halves stay symmetrical:
each machine owns exactly one player's HP, and the path that moves it is logged.

**What remains unproven** — see §5A.3.

### 5A.3 What is still a claim

* That filling the weak pointer makes `IsValidSoftObjectReference` true for an
  already-loaded `FS_ItemData_C` asset. It follows from the class's own definition and
  from the object being live, but no live run has yet read `PASS` on
  `write api: AddItemToInventory ... read back 0 -> 1`.
* That `Health` reaching 0 through the write is equivalent to the game dying the player.
  The rig's floor (400) deliberately prevents the player reaching 0, so this path cannot
  be exercised by the current scenario without turning the floor off.
* That `AppliedDamage` is what a *partial* application would report. The module now treats
  "HP moved" as success and "HP did not move" as failure, so it does not depend on the
  answer.
* Whether `FullyRefreshInventoryTagCount` is needed for the *tag* counts (challenge and
  bestiary conditions) as opposed to the item count. The game's own `AddItemToInventory`
  does not call it, so a pickup's own Interact path must do it; a grant that skips that
  caller could leave a tag count stale even though the count rose. This is logged as a
  follow-up, not fixed by calling a function the game does not call here.

---

## 6. Cross-references and the facts this document had to reconcile

Where this document silently needed a decision because two analysis files disagree:

| Item | Disagreement | Resolution used here |
|---|---|---|
| `FinishAction` implementations | `13-dialogue-ui-answers.md` labels `0x00E4E1F0` (and `0x00E4AED0`/`0x00E4B440`) as implementations; `dialogue_run.txt` calls them "impl" | Measured: `0x00E4AED0`/`0x00E4B440` are resolver stubs, `0x00E4E1F0` is a shim to `0x00E2BD90`; the real bodies are `0x00E2BE50` (bool param) and `0x00E2BD90` (no param) |
| `RunActionWithinContext` | `answers-2.md` calls it the action contract; no pair-table entry exists | It is a **Blueprint** function; resolve it on the action object's class, do not hunt an RVA |
| NamedSaveData writer name | `23` §5's diagram says `SaveFunctions_C.SetNamedSaveData`; `blueprint-surface.md` lists `SaveNamedData` and `SaveNamedData_Advanced` | Use `SaveNamedData` / `SaveNamedData_Advanced` — `SetNamedSaveData` is not in the 24-export list |
| Map element stride | `qdsave.h` probes `{12,16}`; the corpus's "12-byte" figure is the drop-cache **key** | Measured `InventoryContents` element stride is `0x18`, value `+8` |
| Menu pause | `13b`/`13c` mark Status/Inventory `U` (undetermined, LOW); `16-live-pass.md` measures them as pausing | Live measurement wins: **they pause** (`TimeSeconds` slope 0.080) |
| Level-union apply | `21` says a raw add + `Flush` "did not change the level set"; `22` says it changed it 21→11 | `22` wins — the earlier add passed a `{list,prio}` entry instead of a `UFS_LevelList*` |
| Puppet body | `06` §4.1 says the body is hidden and must be told not to be; `12`/`20` measured `CharacterMesh0` is **unassigned** | Unassigned, not hidden: assign the mesh |
