# SYNC-DESIGN — per-system implementation spec (QDC1)

Companion to `WIRE.md` (canonical byte layout) and `API-GAPS.md` (the four unresolved
write APIs, four runtime proofs). This file says, per system: **who is authoritative,
which container/API it rides, the exact hook, the rate, and the failure it prevents** —
and ends each section with an acceptance test that can fail.

Sources distilled: `analysis/06-sync-design.md`, `23-quest-progression-sync-map.md`,
`24-quest-closeout.md`, `13b-pause-surface.md`, `13c-pause-surface.md`,
`21-streaming-ai-lesson.md`, `22-close-out.md`, `16-live-pass.md`,
`20-residual-verification.md`, `14-combat-save-answers.md`, `13-dialogue-ui-answers.md`.

Tier discipline carried forward: **measured** (artefact named), `[P]` proven from bytes,
`[I]` inference with the chain stated, HIGH/MEDIUM/LOW as in the source. A claim with no
tier is measured in this pass and reproducible with the command given.

---

## 0. The four rules every section obeys

1. **One-way authority.** Every connected player's world follows the **host's** world,
   never the other way round. A peer's action is a **request** (an `INTENT`); the host
   applies it to its own world and the peer receives the result back like everybody else.
   [P] `06-sync-design.md` §1, DollsMP doc 27.
2. **State, not events.** `WORLD_FLAGS` / `ACTOR_STATES` / `INVENTORY` bodies are the
   **complete** current set, re-sent on change and at 1 Hz. A lost or duplicated datagram
   changes nothing; a peer with no memory converges within a second. `WIRE.md` §2, §4.
3. **Nothing on the wire is `x += d`.** Every effect is a set insertion, an absolute
   assignment, or a transition guarded by a monotonic counter. `WIRE.md` §4 (rule E2).
4. **A menu that pauses is a divergence, not a UI detail.** Measured: pausing states
   freeze `UWorld::TimeSeconds` (+0x5A0) on the machine that opens them, and `SetGamePaused`
   targets the **caller's own** world (§2.10). Dialogue does **not** pause.

Container geometry measured for this game (all on the live `FS_SaveSlot_C` object):

| Container | Offset | Shape | Tier |
|---|---|---|---|
| `InventoryContents` | `save + 0x28` | `TMap<UObject*, int32>`; element stride **0x18**, value at `+8`; lookup via `TMap::Find` `0x007FB070` | measured (disassembly of `0x00E2EA40`, `GetInventoryItemCount`) |
| `Equipment` | `save + 0x78` | map, same find helper; reached from the `IsItemEquipped` path (`0x00E34720` called by thunk `0x00E61BB0`) | MEDIUM (one path, not two instruments) |
| item-drop cache | `save + 0x220` | `TMap<{FName,i32}, {i32,i32,i32}>`, key 12 B, pair stride 0x20, value at key+0xC; **transient, not serialised** | HIGH `14-combat-save-answers.md` D6-p2/p5/p6 |
| `EnemySaveStates` | `save + 0x270` | `TMap<FGuid, {u8 bValid, f32 RespawnTime, f32 RemainingHP}>` | HIGH `14` D4-p1/p2; live `17-d5-live.md` |
| `NamedSaveData` | **not located natively** | `Map<FName,int32>`; the only native read path is the reflective helper `0x00E60250` | see `API-GAPS.md` §1 |
| `SavableActorStates` | lead only: `save + 0x168` is a map walked with a 0x10-stride hash array by a function reachable from `SavableActor` code (`0x00E24D35`, `0x00E2AF47`) | `Map<FGuid,int32>` per the save schema | LOW/MEDIUM — do not use without an oracle |

> **Warning for `qdsave.h`.** Its `MapProbe` tries strides `{12,16}` and value offsets
> `{8,12}`. The measured `InventoryContents` element stride is **0x18** (24) with the
> value at `+8`. `12` is the *drop-cache key* size, not a TMap element size. Extend the
> candidate set before trusting an enumeration pass.

---

## 1. Authority at a glance

| # | System | Authority | Lane | Rate |
|---|---|---|---|---|
| 2.1 | player transform, body, puppet | **each player owns itself** | `PLAYER` 0x10 | 30 Hz (60 allowed) |
| 2.2 | quest/progression flags | **host only**, never lowered | `WORLD_FLAGS` 0x11 | on change + 1 Hz |
| 2.3 | one-shot world state | **host**, first-wins | `ACTOR_STATES` 0x12 | on change + 1 Hz |
| 2.4 | inventory / key items | **host**, shared | `INVENTORY` 0x13 + `INTENT` | on change + 1 Hz |
| 2.5 | loot | **shared by construction**; per-player is a mod invention | none (local roll) | per death |
| 2.6 | enemies | **host simulates**; peer copies driven, AI off | `ENEMY` 0x14 | 20 Hz |
| 2.7 | dialogue | **host progression**, per-player view | `DIALOGUE` 0x21 + `INTENT` kind 4 | on change |
| 2.8 | level streaming | **union of both**, applied by host | `LEVEL_UNION` 0x22 | 2 Hz + on change |
| 2.9 | checkpoints/death/respawn | respawn per-player; rest is a world event | `INTENT` kinds 5/6 | event |
| 2.10 | menus that pause | **each machine suppresses its own pause** | `PLAYER.flags` bit5 | on change |
| 2.11 | suspend saves | **local only; must not lift shared state** | none | 120 s (local) |

---

## 2.1 Player transform, visible body, puppet

**Authority.** Each player owns its own transform, aim, vitals and action state. Nobody
writes another player's pawn. [P] `06` §1.

**Container/API.** `PLAYER` 0x10, 30 B (`WIRE.md` §3.5): `sample_ms`, i32 position in
1/8 cm, i16 yaw/pitch in 1/65536 turn, i16 velocity in 1/8 cm/s, `move_mode`, `action`,
`flags` (bit0 grounded, bit1 aiming, bit2 sprinting, bit3 crouched, bit4 dialogue,
bit5 menu).

**Hook.** Local pawn: `UGameplayStatics::GetPlayerPawn` `0x028E0640` (HIGH). Pawn class is
`FS_Full_PlayerChar_C`, controller `FS_PlayerController_BP_C` (live). Sample on the local
pawn's own tick through the `ProcessEvent` hook — not "any actor's tick", which changes
phase every frame (DollsMP F-24-4); the QD fallback anchor is `FEngineLoop::Tick`
`0x007753A0` with `UWorld::DeltaTimeSeconds` at `+0x5B0`.

Puppet construction (the hard part; `API-GAPS.md` proof B7): the game module has **no**
character-construction path — 78 class registrations, 20 native tables, exactly three
`Spawn*` UFUNCTIONs, zero `Construct`/`Create`/`Possess`/`Merge` [P]. Use the engine pair:

```
UGameplayStatics::BeginDeferredActorSpawnFromClass 0x028D3C90  (thunk 0x02DB93C0)
    -> UWorld::SpawnActor                          0x029AD950   (call site 0x028D3DA3)
UGameplayStatics::FinishSpawningActor              0x028DCD50  (thunk 0x02DBDF80)
    -> AActor::FinishSpawning                      0x02651580   (call site 0x028DCD64)
```

**The body must be assigned, not un-hidden.** `06` §4.1 said the remote body "must be
told to *not* be hidden" `[I]`. That is refuted: live measurement shows
`CharacterMesh0` holds **no** `SkeletalMesh` in 0x600 bytes while the FP `Arms` component
holds one at `+0x480` (positive control). So the gameplay body is **unassigned, not
hidden** — no `bOwnerNoSee` bit is involved [P] `12-puppet-answers.md` B8, `20-residual-
verification.md`. The puppet must load and assign `Player_FullChar_2026` to
`CharacterMesh0` (or spawn a visible mesh actor), or it will be an invisible capsule.

The flag offsets, for the cases where they *do* matter (hiding the host's own body from
its own camera, or suppressing a duplicated first-person rig): `bOwnerNoSee` = bit 5
(0x20) of the component byte at `+0x212`; `bOnlyOwnerSee` = bit 6 (0x40) of the same byte;
setters `0x027D8340` / `0x027D8310`. `bReplicates` = bit 1 of `AActor+0x5B` (live value
106 → set); `bReplicateMovement` = bit 4 of `AActor+0x58` (live value 16 → set) — both set
by `APawn::APawn` `0x02AD9010`, and the game has **no networking layer**, so these flags
buy nothing; they only mean a spawned pawn will not be "replicated" by a system that does
not exist.

Playback: `WIRE.md` §5 fixes render delay 100 ms and extrapolation cap 250 ms
(`qdmotion.h`). Port `smoothing.rs` with DollsMP's constants except the implied-speed
guard (100 000 cm/s → ~3 000 cm/s) [I]. Snap, do not interpolate, across a teleport
(threshold 2 000 cm) or a respawn.

**Rate.** 30 Hz default; 60 Hz allowed and measured under 10 KB/s for two players.
Actor ticks are suspended on the puppet once: its movement component re-derives velocity
toward zero and the mode back out of whatever was set — "the kind of change that looks
applied in the log and has no effect in game" (Dolls F-12.16). `SetMovementMode` needs
**both** arguments; write velocity to the root **and** the movement component.

**Failure it prevents.** Extrapolating from "now" turns every late packet into a visible
correction; interpolating across a teleport drags the body through the level; sampling
after "any actor's tick" makes the sample phase wander; and the DollsMP class of "the
other player is invisible" is usually a hang, not a sync bug — check `Responding` before
diagnosing.

**Acceptance test.** Two machines in `100_GR`. Move the peer in a 30-second circle at
walk speed with 40 ms RTT and 5 % loss injected (`NetImpair`). Pass: on the watcher, the
puppet's position error against the sender's reported position stays < 50 cm at the 95th
percentile, no visible snap occurs, and the puppet's `CharacterMesh0` has a non-null
`SkeletalMesh` `+0x480`. Fail: any snap without a teleport, an invisible puppet, or 95th-
percentile error ≥ 50 cm.

---

## 2.2 Quest and progression flags — `NamedSaveData`

**Authority.** Host only, monotone, never lowered. A peer write is an `INTENT` the host
validates. [P] `06` §4.2/§5, `23` §6, `24` §5b rule 7.

**Container.** `NamedSaveData : Map<FName,int32>` on `FS_SaveSlot_C`. 11 keys in the
operator's 11-minute save; the progression-relevant universe is 58 `cha_*`/stat keys plus
composed per-actor keys (`Arjaot_State`, `RustySwordKnightState`,
`RustySwordKnightGaveSword`, `BrokeBarricade`, `CheckpointUnlockCount`,
`TiniScytheHammerAP`, …). Keys are **composed** as `<instance or actor name>` +
`<property/state name>` [P] `23` §7. Model the whole thing as **one** bank, never sharded
(DollsMP defect 6: a one-bit change costs the same ≈17 B as a 64-bit word when sharded).

**The read chain, fully measured** (`API-GAPS.md` §1 has the write side):

```
GetNamedSaveData  impl 0x00E2F6D0   (pair table 0x0395D6E0, thunk 0x00E61800)
  -> GetActiveBaseSave 0x00E2D100            ; WorldContext -> live FS_SaveSlot_C
  -> per-object helper 0x00E60250
       frame at [rsp+0x20]: { FName key @+0, bool wasSet @+8, int32 value @+0xC }
       UFunction resolved by 0x011A0050   (rcx = save, rdx = FName; reads save->Class @+0x10)
       dispatch: call qword ptr [save->vtable + 0x220]      ; UObject::ProcessEvent
```

`ProcessEvent` itself is `0x011A7AA0`, vtable slot 68 / offset `+0x220`; `0x011A0040` and
`0x011A0050` are the null-returning and checked `FindFunction` pair (shape HIGH, names
MEDIUM — they call `0x103DB10` with `(Class, FName, 1)`).

**Hook.** Read: call `0x00E2F6D0` on the game thread with any world context. Change
notification: `FS_NamedSaveDataChange` is a **delegate type**, and `WorldEventReceiver`
(223 placed actors across 27 maps) is the bus that consumes it; `ActorQuestStateManager`
refreshes off `OnQuestStateUpdated` [P] `23` §5. Prefer the delegate over per-frame
diffing — that is the whole reason the design is event-driven for the discrete half.

**Rate.** On change, coalesced, plus 1 Hz. The full 11-key body is ~0.1 KB; the enumerated
universe still fits one `WORLD_FLAGS` body (§3.6 header is 6 B + n×(2+len+4)).

**Failure it prevents.** Double-applied progression (both machines owning the same fact),
and rollback (a late snapshot lowering a value). The trigger graph itself is **not** the
mod's job: the game's own
`ActivationTrigger → ActivatableGroup → ActivatableNamedSaveDataSetter → SaveNamedData`
chain runs locally on each machine, and the mod only has to make the *result* agree [P]
`23` §5/§8. The one real risk is double-application on travel: a level's `BeginPlay`
re-runs and can re-trigger a volume, so the host's absolute value must be re-applied after
arrival and the peer must not re-request a write it already got.

**Acceptance test.** Peer completes `FlyingSwordNSDSetter` in `100_GR` (key
`FlyingSwordPickedUp`). Pass: host's `NamedSaveData["FlyingSwordPickedUp"] == 1`, the
peer's mirror equals the host's whole bank, and the peer's own save still holds the key
after a restart. Fail: the key appears twice with different values, the peer's value is
lowered by a later snapshot, or the host never sees it (intent dropped without retry —
`based_on_rev` must not silently swallow).

---

## 2.3 One-shot world state — `SavableActorStates`

**Authority.** Host, **first-wins**. A peer interaction is an intent. [P] `23` §6,
`24` §5a.

**Container.** `SavableActorStates : Map<FGuid,int32>` — 7 entries in the sample save;
keys are authored `FGuid` cooked into level packages (421/421 of all persisted GUIDs were
found in the pak) [P] `07-verification.md`.

**Hook.** The writer is Blueprint, on `SavableActor_C` descendants (`Chest`, `Door_Base`,
`BreakableBarricade`, `ActivatableGroup` via a setter). No isolated native writer has been
identified; the only lead is a map at `save+0x168` visited by functions reachable from
`SavableActor` code (`0x00E24D35`, `0x00E2AF47` — the latter walks a 0x10-stride hash
array). **Do not write that offset without an oracle**: `0x168` is not confirmed as
`SavableActorStates` (LOW/MEDIUM). Read it only after an independent confirmation (see
`API-GAPS.md` §2's read-back pattern — the collection member offset is discovered, then
validated against a value the game wrote).

**Rate.** On change + 1 Hz. `ACTOR_STATES` body: `u32 rev, u16 n, n × {guid(16), i32}`.

**Failure it prevents.** Double pickup / double door-open, and the "it is taken on my
machine but present on yours" split. Correctness is a **set insertion on an id**, and the
id is the game's own authored GUID, so no negotiation is needed [P] `06` §4.7.

**Acceptance test.** Open one chest in `100_GR` simultaneously on both machines. Pass:
exactly one `SavableActorStates` entry for that GUID on the host, the peer mirrors it, the
container actor is gone on both, and inventory gained exactly one item. Fail: two entries
for two different GUIDs (the peer used a runtime GUID), one machine keeps the chest, or the
item count is 2.

---

## 2.4 Inventory and key items

**Authority.** Host, shared. A key granted twice is harmless; **a key the peer lacks locks
the peer out of a region alone** — that is why this is host-owned and not per-player [P]
`04-sync-surface.md` #14, `23` §4/§8.

**Container.** `InventoryContents : Map<Object,int32>` at `save + 0x28` (measured this
pass). Identity is the **full soft object path** the save stores, e.g.
`/Game/Items/KeyItems/AquaDoorKey/AquaDoorKey_2.AquaDoorKey_2` [P] `10-gaps-register.md`
C7. Send text paths, never pointers (the DollsMP rule, and the reason `WIRE.md` §3.8 uses
`str item_path`).

**Hook.** Readers: `GetInventoryItemCount` thunk `0x00E61310` (table `0x0395D6E0`) →
impl `0x00E2EA40`; `GetInventoryItemCountSoft` thunk `0x00E613E0`. Impl body, linear
disassembly past the `.pdata` truncation:

```
0x00E2EA40  GetInventoryItemCount(worldctx, item) -> int32
  call 0x00E2D100            ; GetActiveBaseSave
  lea  rbx, [rax + 0x28]     ; InventoryContents
  0x00E2EA6D call 0x007FB070 ; TMap::Find(map, item, &index)
  0x00E2EA7C lea  rdx,[rax+rax*2] ; mov rax,[rbx]
  0x00E2EA83 lea  rcx,[rax+rdx*8] ; element = data + index*0x18
  0x00E2EA8C mov  eax,[rcx+8]     ; value
```

Writer: `SaveFunctions_C.AddItemToInventory` — a Blueprint function with **no** pair-table
entry (measured scan of `nativepairs.bin`: `AddItemToInventory` yields `[]`, as do
`SaveNamedData` and `SaveNamedData_Advanced`). Must be reached reflectively; see
`API-GAPS.md` §2.

**Rate.** On change + 1 Hz, whole body. 503 key-item assets in 26 families ride this lane
— they are ordinary inventory entries, not a separate quest lane [P] `23` §4.

**Failure it prevents.** A player standing at a door they cannot open because the key was
granted on the other machine; and double-grant on the same pickup (solved by §2.3's GUID).

**Acceptance test.** Peer picks up `AquaDoorKey_2`. Pass: host inventory gains it, the
peer's mirrored `INVENTORY` body contains the exact soft path with count 1, the door opens
on both, and a restart preserves it on both saves. Fail: the door opens on one machine, the
path is truncated or aliased to a different asset, or the count is 2 on the host.

---

## 2.5 Loot — shared by construction

**Authority.** The game's roll is **local and has no player dimension**. `UFS_ItemDropSystem::
RollItemDrop` @ `0x00E392F0` reads `GetActiveBaseSave` (`0x00E3931A` → `0x00E2D100`),
indexes the transient cache at `save+0x220` (`0x00E3933F`), key = `{FName sourceId(8),
int32 dropIndex(4)}` = 12 B, pair stride 0x20, value at key+0xC, and takes the chance from
`FB_GetRandomBool` `0x00BC5E70` over a stateful 4×u32 stream with a per-session pity
counter. `sourceId` is the drop-source object's `UObject::Name` (`0x00E2E5C0`: `mov rax,
[rdx+0x18]`) [P] `14` D6-p1/p2/p3/p4.

Consequence, stated as the measured fact it is: **one death ⇒ one `TrySpawningItem` ⇒ one
`RollItemDrop` per drop index ⇒ one drop actor. Loot is shared.** Per-player loot would be
a **mod invention** (roll twice, replicate two results) and is not a property of the game.

**Container/API.** None. Do not put loot on the wire. The cache is transient and not
serialised (`ItemDrops`/`EnemyDropDataA/B`/`KilledEnemyCounts` are all declared on
`FS_SaveSlot_C` and none is a `SaveGame` property), so it resets every session and two
machines start from different luck state even with identical parameters [P] `14` D6-p6 [I]
D6-i3.

**Hook.** If per-player loot is ever wanted, the only honest implementation is: host kills
⇒ host rolls; for the peer, call `RollItemDrop` a second time on the host's machine with
the peer's context (or have the peer roll locally) and deliver the result through
`INVENTORY`. Decide per-item-class whether a drop is a quest item (§2.4), because a quest
item must NOT be doubled.

**Rate.** Per death.

**Failure it prevents.** Double-grant (the classic two-machine loot bug) — but note it is
prevented *by not inventing* a roll, not by arbitration. The failure mode being accepted
is the DollsMP one: "a host kill the peer never saw alive gives the peer nothing
(ghosted)".

**Acceptance test.** Kill the same `Dragonfly_Base_BP_C` on both machines from the same
save state. Pass: each machine's drop set is whatever its own roll produced, the *host's*
drop actor is the one both players can pick up, and picking it up produces exactly one
`INVENTORY` increment. Fail: two drop actors for one death on one machine, or an item
granted twice.

---

## 2.6 Enemy simulation and `StoreEnemySaveState`

**Authority.** Host simulates AI; peer copies are **driven** (transform, animation, HP,
death) with their AI controller disabled. [P] `06` §4.6.

**Identity.** The enemy save key is the **authored `FGuid` of the placed spawner**
(`GUIDs` property, struct `Guid`, 16 B). 414/414 persisted keys were found cooked into map
packages; `SpawnerGUID` is *not* the authored value (0 occurrences in all 51 maps) [P]
`14` D4-p5/p6. **Rule: key on the spawner's runtime `GetSaveGUID()`, never `GUIDs[0]`.**
Multi-GUID placements exist (4 315 × 1, 62 × 2, 1 × 3, 3 × 5); the only multi-GUID *enemy
spawner* is `LargeRobot_Rusted_Spawner` in `500_AUT` (export `0x1584D79`, two GUIDs) [P]
`22-close-out.md` §3. One key per spawner, not per enemy instance — `06` §4.6's
"spawner GUID + spawn index" is the earlier, superseded phrasing.

**Hook — the persistence write.** `StoreEnemySaveState` @ `0x00E3BF10` (pair table
`0x0395D6E0`, thunk `0x00E62000`):

```
signature: StoreEnemySaveState(UObject* WorldContext, const FGuid* Key, float RespawnTime, float RemainingHP)
body: call 0x00E2D100 (GetActiveBaseSave); lea rcx,[rax+0x270]; bValid=true;
      TMap::Add (0x00E19FC0)
```

It always writes `bValid = true`; a kill is `RemainingHP = 0`, **observed live** (five
calls, same GUID `5ae3b37b-4f721b25-bd55a3b5-958ab1c2`, `hp 0`, `WorldContext` = the
`Dragonfly_Spawner_C`) [P] `17-d5-live.md`. Repeated calls are `TMap::Add` of the same key
and converge, so applying it as a set-assignment is safe. Readers:
`RetrieveEnemySaveState` `0x00E39240`; rest clears the whole table with
`ClearAllEnemySaveStates` `0x00E27460` (imported by `Checkpoint_C`) — a clean
respawn-broadcast event for the mod [P] `14` D4-p7.

**Rate.** `ENEMY` 0x14 at 20 Hz, row = `{guid(16), i32 x/y/z, i16 yaw, u16 hp, u8 flags}`
= 33 B. 20 rows per datagram at N=2 (`WIRE.md` §3.9), 24 rows in flight, remaining rows
round-robin to the next tick. Never split a row. 24 rows × 33 B + 4 = 796 B, inside the
1200 B body cap.

**Failure it prevents.** Double simulation (double damage, double loot, two bars), and the
"DollsMP symptom" where a streamed area is removed and every enemy "loses its brain" and
gets a new switched-off one — see §2.8.

**Acceptance test.** Host kills a `Dragonfly` while the peer watches. Pass: exactly one
`StoreEnemySaveState` key on the host (GUID matches the spawner's authored `GUIDs`
entry), the peer's enemy dies at the same position with the same HP trace ending at 0, and
after a full rest cycle (`ClearAllEnemySaveStates`) both machines respawn it. Fail: the
peer's enemy survives, two different GUIDs appear, or the peer's copy takes damage while
AI is enabled.

---

## 2.7 Dialogue — lock, first answer, host progression

**Authority.** Two different things, and conflating them was a DollsMP bug:

- **Progression** written by a dialogue is host-owned, joined to §2.2/§2.4.
- **The conversation itself** is a per-player view. The corpus's final owner decision:
  "both players may talk to NPCs … overall progression is host-authoritative" [P]
  `24-quest-closeout.md` §5a/§5b.

**This diverges from `06-sync-design.md` §4.5**, which proposed "one player per NPC" as a
global lock. Both are in the corpus; `24` §5 is later and labelled "owner's decision /
RECORDED". Resolution used here: **no global NPC lock**. `DIALOGUE` 0x21 is still used —
carrying `slot` and `seq` — but only to arbitrate NPCs whose dialogue writes world flags or
pays a one-time reward (`*TurnIn*`, `*Give*`, reward actions in `24` §4b). Pure-flavour
NPCs run freely on both machines. This is a refinement of the lane's *use*, not a byte
change to `WIRE.md`.

**Container/API.** `DIALOGUE` 0x21 (`u32 seq, guid npc, u8 slot (0xFF free), u16 line,
u16 entry`). Options are **derivable** from already-synced flags and inventory, so they
are not sent: a dialogue is a cursor `(asset, sequenceIndex, entryIndex)` and the branch
language is `EDCT_Inventory` / `EDCT_NamedSaveData` with `EFS_Comparator` (6) and
Show/Hide × AND/OR [P] `10-gaps-register.md` D2.

**Hook — local.** The game already has three cooperating local guards; drive them, do not
reinvent them [P] `13-dialogue-ui-answers.md` D3.2: `RunningDialogue` (player bool, read by
`DialogueManager`), `PlayerIsTalkingTo` (`NPC_Base`, the only package), and
`BlockInteract`/`ReenableInteract`/`DisableInteraction` plus `DisableAllPlayerInput`. The
native dialogue layer is **stateless** with respect to "who is talking" — no byte field
access on `this` in `InitAction`/`RunAction`/`FinishAction`/`CancelAction`/`JumpToDialogue`
[P].

**Hook — the action dispatch (why actions need a reflective call).** The native layer is a
*driver* that calls into Blueprint and stops. Measured this pass:

```
0x00E2BE50  FinishAction(this, bool)
    obj = resolve(this+0x38);  name = *(FName*)(this+0x40)
    fn  = FindFunctionChecked(obj, name)        0x011A0050
    frame at [rsp+0x50] = { bool @ 0 }
    ProcessEvent(obj, fn, &frame)               call [vtable+0x220]  @0x00E2BEDE

0x00E2BD90  (target of the 0x00E4E1F0 shim)     same shape, frame = NULL (zero args)
0x00E34E10  JumpToDialogue                      ProcessEvent @0x00E34E9A
```

So the action object carries `{target @+0x38ish, event FName @+0x40ish}` and the BP event
takes at most one bool. `RunActionWithinContext` has **no** pair-table entry at all
(measured: `[]`), i.e. it is Blueprint-defined on the action BP — see `API-GAPS.md` §3.

**Rate.** On change. `seq` is what makes a lost blob cost a beat rather than the session:
the lock is published as *state* and the newer state wins.

**Failure it prevents.** The DollsMP failure verbatim: both machines run the same talk
Blueprint and **a race decides the choice**. Also the "dialogue open at death leaves an
invisible unanswered choice" bug — end the talk with the game's own end-talk path on death.

**Acceptance test.** Both players start the same reward NPC (`Chef_TurnInAction`) within
1 s of each other. Pass: neither is blocked, both advance independently, the host writes
`MealTicketsCollected` **once**, the peer receives the echo and does not re-write it, and
`TimeSeconds` on both machines keeps a slope of ~1.0 while both dialogues are open. Fail:
one player is locked out, the counter increments twice, or either world clock freezes.

---

## 2.8 Level streaming union

**Authority.** The union of both players' streaming sets, **applied by the host**, with the
peer's set as an input. [P] `21-streaming-ai-lesson.md`, `22-close-out.md` §4.

**Container/API.** `LEVEL_UNION` 0x22: `u16 bits_len, bytes bits`, plus a name list on
first send (`u8 has_names, u16 n, n × str`). Bits index the *sender's own* list order, so
no shared index is ever negotiated; a receiver keeps the last name list per slot.

**Hook.** QD streams through `AFS_LevelManager` (a game-level stack of lists) at
`GameInstance + 0x3B0`, created lazily by `GetLevelManager` `0x00E2EE50`
(`world+0x118` → GameInstance). The persistent world is `Ruins_Level_00` with **42 dynamic
sublevels**; QD has **0** engine `ALevelStreamingVolume`s, so the DollsNest 1.3.1
volume-pass bug cannot fire here [P].

```
AddLevelListToStack (UFUNCTION)           0x00E215E0
  -> worker                              0x00E37800  (SEH-split body 0x00E378CD)
       arg3 selects stack A +0x298 (0) or B +0x2A8 (1)
       appends {UFS_LevelList*, int32 prio}; dedup by {list, prio}
       prio = arg5 when arg4=1 else [list+0x30]
  -> 0xE42950 merge+resolve -> 0xE1D3D0 -> +0x258 = top list
FlushLevelManagerActions                  0x00E2C270
  immediate=true: copy +0x288 -> +0x320 -> 0x00E2C306 -> 0x00E21E40
       for each {ULevel*, u8 visible}: SetShouldBeVisible(level, visible)
ULevelStreaming::SetShouldBeVisible       0x029ACB20   (writes byte +0xBA bit 3)
  tail-calls UWorld::UpdateStreamingLevelShouldBeConsidered 0x02D4A0E0
per-frame manager update                  0x00E3CF9E   (one +0x320 entry per frame)
```

Manager fields: `+0x288` resolved list, `+0x290` pending-action count, `+0x298`/`+0x2A8`
the two `{list,prio}` stacks, `+0x320` resolved level entries, `+0x330` current entry,
`+0x338` wanted-visible byte, `+0x340` applied result, `+0x378` previous done/busy state.

**Do not use `FlushLevelManagerActions` as the union's apply.** Measured live: `Add` (with
the corrected argument — a `UFS_LevelList*`, not a `{list,prio}` entry) plus
`Flush(ctx,1)` **replaced** the visible set, 21 → 11, leaving the GreenRuins levels
loaded-hidden [P] `22` §4. The union must drive `0xE42950` / the per-frame path and then
re-assert `SetShouldBeVisible(true)` for the union after the engine pass. **Show, not just
load**: a loaded-but-hidden level's actors are absent — measured with the shop merchant in
`140_GR_ShopIsleOnly`, which is why the shop was "missing" until the level was shown [P].

**Rate.** 2 Hz + on change. A hard transition (intro, cutscene map `997/998/999_*`,
`SipehaWorld`, `B33sment`) is a host-driven, **acknowledged** event: host announces, both
arrive, both confirm, then play resumes.

**Failure it prevents.** The "enemies lose their brain" family: a sublevel that is removed
while one player still needs it destroys its `EnemySpawner_C` and its enemy; on return the
spawner respawns from the save record with a fresh, off AI. Also: a player falling through
a hole into an unloaded area.

**Acceptance test.** Players stand in `100_GR` and `500_AUT` respectively. Pass: after 10 s,
the visible set is a **superset** of both baselines, `500_AUT` and the `100_GR` levels are
both loaded and visible (`+0xBA` bit 3 set), and the enemy count in each area is unchanged
across a 60 s window. Fail: the visible count drops to 11, either area becomes
loaded-hidden, or any enemy in an area is re-created with a new actor address.

---

## 2.9 Checkpoints, death, respawn

**Authority.** A **respawn is a position**; a **rest is a world event**. Do not route a
respawn through the checkpoint rest path, or the survivor's world resets mid-fight
(DollsMP rev-12 correction, paid for there). [P] `06` §4.9.

**Container/API.** `SavePointsVisited : Array<Object>` (61 in the sample),
`LastTouchedCheckpoint` / `SavePointID`, and `NamedSaveData["CheckpointUnlockCount"]`
(a counter; the `cha_Challenge_AllTeleporters` definition wants it at 32). 34
`Checkpoint_C` placements across 18 maps [P] `10` C11. A checkpoint touch is `INTENT`
kind 5 (`str save_point_path`).

**Hook.** Enemy respawn is data-driven: resting calls `ClearAllEnemySaveStates` `0x00E27460`
(imported by `Checkpoint_C`), and every enemy's `{bValid, RespawnTime, RemainingHP}` is
then re-read from `save+0x270`. Both machines therefore derive the same respawn set from
the same values — no message needed beyond the flag and the enemy lane.

Death: the **Game Over screen pauses** — measured `TimeSeconds` 440.50644 → 440.50650 over
2 s while `RealTimeSeconds` advanced 575.27 → 578.89 [P] `20-residual-verification.md`. So
a death is a menu-pause case (§2.10) *and* a per-player state, not a world event. Do not
let a player's death stop the partner's world.

**Rate.** Event (`INTENT` kind 5), idempotent by `save_point_path` + `based_on_rev`.

**Failure it prevents.** The boss-fight reset: one player dies, the survivor's enemies
respawn and the fight restarts. And a respawn teleporting the *other* player.

**Acceptance test.** Peer dies during a `Dragon_Green_BP_C` fight. Pass: the peer respawns
at the last touched checkpoint, the host's fight continues uninterrupted (the boss's HP
does not reset, `EnemySaveStates` keys unchanged), and the peer's world clock resumes on
respawn. Fail: the host's boss resets, the peer respawns at the host's position, or any
enemy respawns because of the death.

---

## 2.10 Menu pause suppression

**Measured, and this settles the corpus's open row:** the pausing states are **Status &
Inventory, Pause, Photo mode, Checkpoint/Save, Game Over** and (via the pause hub) Journal
/ Guide. Live `TimeSeconds` slopes while open: Status/Inventory **0.080**, Pause **0.000**,
Photo **0.016**, Checkpoint **0.081**, against a 0.999–1.001 gameplay baseline [P]
`16-live-pass.md` §1, `20-residual-verification.md`, `22-close-out.md` §2. **Shop does not
pause** (3.01 s of world time per 3 s wall) and **dialogue does not pause** (`13b` HIGH,
`13c`, `13-dialogue-ui-answers.md` HIGH 0.85).

**Authority.** Each machine suppresses **its own** pause. `SetGamePaused` resolves the
**caller's** world `0x028F3180` → `UEngine::GetWorldFromContextObject` `0x02CDEDF0`
(`028F3199`) → world `+0x180` → `+0x38`/`+0x40` → `+0x30` controller → virtual
`[vtable + 0xA88]` with `(bool, &FCanUnpause)`. Nothing replicates it (no networking in
this build), so A pausing stops A's ticks and leaves B's running; divergence equals A's
wall-clock time in the state [P] `13b` §1.2/§5.

**Hook.** Patch `APlayerController` vtable `+0xA88` (better than the GameplayStatics
wrapper: it catches every pause request regardless of origin) and **veto** it when co-op is
active, while still running the UI/input side. `EnterUIMode`/`ExitUIMode` are separate
exports and do **not** contain the pause call, so suppressing the pause does not break the
menu.

**Do not probe with `IsGamePaused`.** `0x029A43C0` is a compound "paused **or** pause-delay
**or** seek-freeze" predicate consulting `[world+0x10C]&0x40`, `[world+0x620]`,
`AWorldSettings+0x370`, and `PauseDelay +0x5B4` vs `+0x5A0`. Use `TimeSeconds` `+0x5A0`.
`DeltaTimeSeconds` `+0x5B0` is **not** a reliable signal (it stayed ~0.007 while frozen).

**Rate.** `PLAYER.flags` bit5 (`menu`) on change, so the partner can see "you are in a menu"
without a separate lane.

**Failure it prevents.** A 30-second inventory visit diverging the two worlds by 30 s of
game time — which then shows up as every scripted trigger, enemy position and timer being
wrong for the rest of the session.

**Acceptance test.** Host opens Status/Inventory for 20 s while the peer fights an enemy.
Pass: the host's `TimeSeconds` keeps advancing at ~1.0 (pause suppressed), the peer's world
is unaffected, and closing the menu does not teleport or rewind anything. Fail: either
`TimeSeconds` slope is < 0.9 during the visit, or the host's enemies are frozen when the
menu closes.

---

## 2.11 Suspend saves

**Authority.** Local only. A suspend writes one machine's world; in co-op it must **not**
lift shared state into the peer's world, and it must not block the host's tick.

**Container/API.** `ActiveSuspendSlot : int32` plus `SuspendSlot0`/`SuspendSlot1` constants
in `SaveFunctions_C`; the slot name is **composed at runtime** (`GetSaveSlotName`
`0x00E306D0`) — the literals `World0`, `_Suspend_` occur 0 times in the exe and 0 times in
38 426 cooked files [P] `14` C9-p6. Measured cadence: **120 s, alternating 0/1, with no
user action** (`World0_Suspend_1` 20:43:20 → `World0_Suspend_0` 20:45:20) [P]
`16-live-pass.md` §2.

**Hook.** The writer is a looping `K2_SetTimerDelegate` in `FS_GameInstance_C`; the actual
write funnels through `UGameplayStatics::SaveGameToSlot` `0x028F2B40`, whose only callers
are `FB_SaveUtils` (5) plus its own exec thunk — **nothing in the game module writes a save
directly** [P] `14` C9-p2. It is gated by `BlockSuspendSaves`, called by `EnemyBase_C` and
`SuspendSaveBlockingVolume_C` — that gate is why the cadence is "roughly every two minutes,
not exactly" [P] `14` C9-p4/p5.

**Design.** Three options, in order of preference:

1. **Per-instance save directory** (`-UserDir=`) makes the suspend file local by
   construction; that is already the rig's behaviour and the launcher's save guard.
2. **Hold `BlockSuspendSaves` while co-op is active** (the game's own gate) so no
   mid-session suspend is written at all; suspend becomes a "leave co-op" action.
3. If a suspend is written, it must be followed by a **re-apply of the host's containers**
   on load (the §2.2/§2.3/§2.4 bodies), because a suspend snapshot captures the *local*
   machine's view and may be behind.

**Rate.** Local, 120 s, never on the wire. `WIRE.md` has no suspend kind and must not gain
one: a suspend is a session event, not a replicated datum.

**Failure it prevents.** One player's silent 2-minute autosave overwriting the other's
progress — the single worst outcome in the whole design (DollsMP's `dnsaveedit.py --root`
incident is the same class of bug).

**Acceptance test.** Co-op session left idle for 5 minutes with both machines running.
Pass: no `World*_Suspend_*.sav` appears in either instance's directory (option 2), or if
one does (option 1/3), the host's `NamedSaveData` key count and inventory set are unchanged
after loading it. Fail: a suspend file appears in the *shared* `%LOCALAPPDATA%` save dir, or
loading one rolls back any flag.

---

## 3. Ordering rules that cut across systems

| Situation | Order | Why |
|---|---|---|
| Peer action | peer sends `INTENT(based_on_rev)` → host validates → host writes → host publishes the absolute body → peer applies | one writer; `based_on_rev` is what makes double-pickup structurally impossible |
| Travel / hard transition | host flushes containers → host sends the transition event → both load → both ack → resume, then re-apply all three bodies | `BeginPlay` can re-trigger volumes; re-applying after arrival is the guard |
| Level union | peer publishes its set → host unions → host applies via the manager path → host re-asserts visibility | `Flush` replaces the set (21→11 measured) |
| Death | local pause suppression → per-player respawn position → no world reset | a respawn is not a rest |
| Suspend | block it, or isolate it, then re-apply bodies on load | a local snapshot must never become shared truth |

## 4. What this document does not settle

Four write-side APIs and four runtime proofs are in `API-GAPS.md`: the `NamedSaveData`
write entry, the inventory grant, the dialogue action invocation, and travel; plus the
puppet spawn, the level-union end-to-end, the multi-GUID rotation, and two-player dialogue.
Each has a stated probe and a pass/fail criterion there.
