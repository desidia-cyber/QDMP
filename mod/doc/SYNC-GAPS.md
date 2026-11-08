# SYNC-GAPS — what is still not synced, and what it costs

Written 2026-11-08, after the second round: peer progression, enemy combat, inventory,
the dialogue block and the streaming unload path were built on top of the first round's
enemy/dialogue/streaming work.

Two documents belong beside this one:

* [`SYNC-RESEARCH.md`](SYNC-RESEARCH.md) — what the *game* actually does, measured from
  the shipped image and the cooked packages. Where this document says "blocked", the
  reason and the passing probe are there, tagged `[P]`/`[I]`/UNKNOWN.
* [`WIRE.md`](WIRE.md) — what the messages are.

"Built" still means **not proven live**. Every write path is behind a switch in
`qdcoop.ini` and every one of those is `0` except `FlagSync`. Nothing in this document has
been run on two machines yet; §5 lists what a first live session has to show.

## 0. What the module sends today

| Kind | Sent by | Handled by | Note |
|---|---|---|---|
| `PLAYER` | both | both | proven live |
| `WORLD_FLAGS` | host | peer | absolute set, chunked, 1 Hz + on change |
| `INVENTORY` | host | peer | **new**: absolute set, chunked, gated `InventorySync` |
| `ACTOR_STATES` | host | peer | **new**: absolute set, chunked, gated `ActorStateSync`; the container is a reflected property (G2) |
| `ENEMY` | **both** | **both** | **new**: bidirectional; whoever owns an enemy publishes its rows |
| `ENEMY_DETAIL` | both | both | **new**: montage asset + status mask, reliable, on change |
| `EQUIPMENT` / `EFFECT` / `EFFECT_MOVE` / `STATUS` | both | both | proven live |
| `INTENT` | peer | host | kinds 1 (flag), 6 (actor state), 7 (enemy hit), 8 (enemy record) |
| `DIALOGUE` | both | both | per-slot talk state |
| `LEVEL_UNION` | both | both | what each machine's own game wants |
| `TRAVEL` | both | both | **new**: the map each machine is in, 2 s + on change |

No other kind is sent or received. `K_ACTOR_STATES` is decoded in the Rust crate and has
never had a producer.

## 1. What this round fixed

**G1 — the peer's progression now reaches the host.** A rise in `NamedSaveData` observed by
the read-only mirror on the peer becomes an absolute `INTENT kind 1`. Echo suppression (a
value the host's own body wrote is remembered per key and never sent back), a bounded
retry (the host's gate refuses a stale revision, so a refusal is not a loss) and rate
limiting live in `qdpending.h`. This also covers checkpoints: `CheckpointUnlockCount` is
just another key and rises like any other. The change uncovered a real defect in the gate
— the host compared against its *next* revision rather than the one it had published,
which would have refused every intent forever.

**Enemies now fight the player who can reach them.** Ownership follows the nearer player
(800 cm margin, 1.5 s hold, handoff completed only when the receiver claims it); the
non-owner freezes its copy and follows the owner's rows, so `ENEMY` is bidirectional.
Without this a peer standing next to an enemy the host was simulating got a statue,
because the Blueprint AI only ever targets the local player.

**Enemy damage now goes through a path that exists.** The old code called
`UGameplayStatics::ApplyDamage` at `0x028D2190`. That function is real, but `EnemyBase_C`
never imports it and a live pass recorded it never firing on a real kill — so HP
convergence, the host applying a peer's hit, and the dead-record kill all changed
*nothing*. It now calls `TryApplyDamage` (the `FB_Damageable` interface message) with a
`DamageToApply` struct filled by walking the struct's own fields, and is renamed
`ApplyEnemyDamage` so the name describes what runs.

**Enemy attacks and status effects are visible.** The owner reads the active montage,
classifies it and sends the asset path in `ENEMY_DETAIL` on change; the follower plays it
and reads back what is actually playing. Poison and stagger are mirrored only where the
property exists on the class.

**Enemy projectiles reach the partner.** `FxPump` only sent projectiles the *local pawn*
owned, so a fireball or a spit existed on one machine. A projectile owned or instigated by
an enemy is now sent, and the peer spawns the same collisionless visual copy the player's
thrown daggers already use.

**The dialogue block was never working.** `BlockInteract` is not a function anywhere in
this build: it is a boolean property on `NPC_Base_C` (`+0x0613`). The old code resolved a
UFunction of that name, found nothing, and left the partner's NPC fully interactable. It
is written as a property now; `ReenableInteract` (a real zero-argument UFunction) still
runs on release.

**Inventory is shared, host to peer.** The host publishes its whole `InventoryContents`
(chunked, absolute, 1 Hz + on change); the peer grants only the difference through the
read-back-verified `AddItemToInventory` path and never removes anything. `WrGrantItem`
lives in `qdwrite.h`, which also refuses to trust the `SaveNamedData` frame unless the same
walk reproduces the measured `GetNamedSaveData` layout.

**Streaming covers the unload path and the world it is in.** `SetShouldBeLoaded` is
byte-verified at `0x029ACAC0` (`+0xBA` bit 4, pinned by `ShouldBeLoaded`) and is vetoed
with the same pending-hide flip as the visibility hook, so a level the partner stands in
cannot be unloaded if the manager ever starts doing that. Level names are accepted only if
they look like a level name, with the source logged. And a new `MapBoard` declares **world
divergence** after 3 s of differing map names, after which the partner's union names,
enemy rows, enemy detail and dialogue are not applied at all.

## 2. Gaps that still break the co-op promise

**G2. One-shot world objects are now synced host→peer, with one measured caveat.**
Chests, doors, breakable barricades and `ActivatableGroup` setters live in
`SavableActorStates` on the save object. That container is **not** an unlocated
`save+0x168`: it is a reflected `MapProperty` on `FS_SaveSlot_C`, keyed `FGuid` with an
`int32` value. `FindPropertyOffsetIn(ObjClass(save), "SavableActorStates")` returns its
member offset at runtime, so no RVA and no lead is involved. The element geometry is the
engine's own `TMap` geometry, measured from `TMap::Find` (`0x007FB070`) and the GUID-keyed
`TMap::Add` instantiation (`0x00E19FC0`): key `+0x00`, value `+0x10`, `HashNextId`
`+0x14`, stride `0x1C`. `SYNC-RESEARCH` §A.8 is the probe and the tag dump.

The writer is the game's own actor function, not a direct `TMap::Add`:
`SavableActor_C.Save Data Index to World Save(Index, Data)` does
`Map_Add(GetCurrentlyActiveSave().SavableActorStates, GUIDs[Index], Data)` and
`Array_Set(MySaveData, Index, …)`, so it inserts *and* updates and keeps the actor's cache
in step. The mod finds the live `SavableActor_C` that owns a GUID through its `GUIDs`
array, calls that function, and reads the value back **two ways** — the direct container
walk and `GetSavableActorDataAtIndex(Index)` (which reads `MySaveData`). A key with no
live actor falls back to an in-place container update if the key already exists; a key
that exists nowhere is refused with the GUIDs named, because a raw insert would need the
engine's allocator and hash re-bucketing.

What is still not proven live: the *visible* reaction. `CacheSaveData` appends to
`MySaveData` (`Array_Add`, and the package imports no clear/remove), so calling the
refresh pair `RestoreFromSave` repeatedly would grow it; the mod deliberately does not run
it. The durable container therefore converges immediately, the actor's cached
`MySaveData` converges through the writer, and the visible door/chest state follows the
game's own load path. A live pass has to show whether the in-level actor reacts without
that refresh; until it does, `ActorStateSync` stays `0`.

**G3. Inventory is shared in one direction only.** The host's inventory reaches the peer; a
pickup the *peer* makes stays local and the host never learns of it. So SYNC-DESIGN §2.4's
acceptance test ("the peer picks up `AquaDoorKey_2`; the host gains it") does **not** pass
from this round's change — only its mirror image does. This needs a peer-side intent
producer (`I_GRANT_ITEM` is decoded and handled on the host; its sender does not exist),
plus a decision the design does not make: what happens when both players loot independently
and the counts disagree. Two smaller consequences of the one-way rule: the peer never
*loses* a consumable the host spent (it is replenished, not reduced), and a pending grant
whose item the host removed before it landed can over-grant by that difference.

**G4. A host-owned enemy still cannot hurt a distant peer.** Ownership by proximity solves
the common case — the player next to an enemy runs it, with the game's own AI. But when the
peer is far and the host owns an enemy, that enemy's attacks land on the host's player only
and the peer is invulnerable to it. The true fix is to make the Blueprint target the remote
body, and that is now measured to need a **player-class proxy pawn**: `TargetPlayer` is a
Blueprint variable the AI reads directly (so the `ProcessEvent` post-hook idea is refuted),
and two cast gates admit only an `FS_PlayerChar_C` implementing `FB_DamageableInterface`,
which the puppet is not. That puts the unproven spawn proof (`API-GAPS` §5, B7) on the
critical path of combat, not just of the puppet's appearance. The alternative lane — the
host computing damage and sending it for the peer to apply to its own pawn through
`TryApplyDamage` — is implementable but has no trigger until the host knows the peer was hit
by something it cannot see.

**G5. Spawners still activate only for their own machine's player.**
`PlayerIsInActivationRange` and `PlayerDistanceDespawnCheck` are Blueprint and read the local
player. When the host walks up to an enemy the peer was running, the host's spawner spawns a
**fresh** enemy (full HP, new position) and ownership moves to it. The workaround (the peer
runs what the host cannot) is in place; the cause is the same proxy-pawn problem as G4.

**G6. Travel is detected, not coordinated.** `TRAVEL` announces the map each machine is in
and refuses to cross-apply state while they differ, which turns a silent half-applied world
into one warning. It does not move anybody. The measured entry point is
`FS_PlayerChar.TeleportToLocation(LocationData, GreenFX)` with
`EnemySpawner_C.PlayerTeleported()` as the world notification — not `OpenLevel`, which only
menus and death use. Making the peer follow a host transition is the next step and needs a
`LocationData` identity both machines agree on.

## 3. Inside the systems built this round

**Enemies**
- **Bosses are still a row and nothing more.** A boss is position and HP. The boss health
  bar (`ActivateBossHPBar` on `PlayerStatsAndUtils_C`) and the arena exit
  (`PlayerTeleportedOutOfBossArena`, a zero-argument spawner event) are both drivable on the
  peer and are not driven. Boss **phases** are not expressible at all: no cooked package
  contains `Phase`, `bBoss` or `OnSubStateChanged`.
- **Enemies placed without a spawner are not synced**, because identity is the spawner's
  GUID. Summoned and boss-spawned enemies run independently on both machines.
- **`TryApplyDamage` is unproven.** The amount field is found by name in the struct and every
  field is logged, but if the payload needs a damage-type tag or a nested `Data` struct, HP
  will not move and the log will say so. The "enemy damage had NO effect" warning is the
  acceptance criterion for the next live run, and it must stop appearing.
- **Montage playback is unproven.** The follower resolves `Montage_Play` reflectively and
  reads `GetCurrentActiveMontage` back, so a failure is visible rather than silent; an AnimBP
  that plays sequences rather than montages sends nothing.
- **Bandwidth with many enemies is unmeasured.** `ENEMY_DETAIL` is on change only, but a
  large fight is exactly when montages change most.
- **Hit attribution is noisy.** A peer hit is detected as an HP drop on the copy the mod did
  not cause, so poison damage-over-time arrives as a stream of small hits. Correct, but
  noisy in the log.

**Dialogue**
- **Concurrent start is logged, not prevented.** If both players start the same NPC within
  one round trip, both conversations run, and turn-ins can pay out twice until the host owns
  the reward.
- **Death does not end a conversation.** The module only observes `RunningDialogue`; if the
  game does not end the talk on death, the partner's NPC stays blocked until the talk ends or
  goes stale (5 s).
- **Choices are still not sent as choices.** Progress flows through flags, which now works
  (G1) — that is the design, not a gap, but it means a dialogue option that writes no flag
  reaches nobody.
- `entry` (the cursor within a sequence) is still read as 0.

**Streaming**
- **The two-levels-per-pump budget** can produce a visible hitch when the players separate.
- **Forced levels stay loaded after release** (only visibility is released). That trades
  memory for not re-loading and re-spawning the level's enemies, and the manager has no
  unload path to piggy-back on — `[P]` `SYNC-RESEARCH` §3.
- **World divergence only exists when `StreamUnion` is on.** With the switch off, nothing
  tracks which map the partner is in.

## 4. Not synced, lower stakes

- **Moving world objects** (elevators, platforms, `Activatable` movers): each machine runs
  its own, so a player riding one is shown floating on the other.
- **Loot pickups.** Each machine rolls its own drops; a drop picked up on one machine stays
  on the floor on the other, and quest items can be doubled.
- **Shop, journal/bestiary, XP, weapon levels, grimoire ink.** Per-player by nature.
- **Peer vitals.** The partner's HP and death are not shown; a death shows as the body
  falling.
- **Cutscenes / sequencer** play only on the machine that triggers them.
- **Suspend saves.** Nothing holds `BlockSuspendSaves` or re-applies host state after a
  suspend load. The test rig's `-UserDir` isolation protects the saves; a player's normal
  launch shares `%LOCALAPPDATA%`.

Checkpoints are **not** on this list any more: they are a `NamedSaveData` counter, so the
peer-intent path covers them.

## 5. What a first live two-instance run has to show

Ordered, because each one makes the next diagnosable.

1. **Load.** The `switches:` line, `NAMED-SAVE ORACLE PASS` (or a loud FAIL), the container
   offsets, `enemy sync: FS_EnemyCharacterBase found`, and
   `hooked ULevelStreaming::SetShouldBeLoaded` when `StreamUnion=1`.
2. **G1.** Peer opens a barricade in `100_GR`: the peer logs `flag intent sent … try 1/5`
   then `flag intent confirmed`; the host logs `intent applied:`. No second send for the
   same value.
3. **Enemies.** With `EnemySync=1`: a kill with the peer nearby shows `Paused ok=1`, an
   ownership handoff, and **no** `enemy damage had NO effect`.
4. **Dialogue.** `dialogue: BLOCK <npc> BlockInteract=1` on the partner's machine, and the
   NPC really does not start.
5. **Streaming.** `level name: <name> from <source>` with no `REFUSED`, and a trip into a
   cutscene map producing one `WORLD DIVERGENCE` warning and one `worlds agree again`.
6. **Inventory.** With `InventorySync=1`: the host picking up the aqua key produces
   `inventory sent rev=N`, and on the peer `write api: AddItemToInventory … read back`.

## 6. Suggested order from here

1. **Live-prove this round** (§5). Nothing new should be built on a path that has never run;
   every correction in this round came from measurement rather than from play, and more will
   surface the same way.
2. **B7, the player-class spawn.** It is now the blocker for enemy targeting (G4/G5) as well
   as for the puppet body, and the enemy ownership work is the best test bed for it.
3. **The peer → host item lane** (G3): a small intent producer plus a policy decision that
   `SYNC-DESIGN` §2.4 does not make.
4. **G2's live reaction.** The read/write path is measured and implemented; one live pass
   must show a chest opened on the host changing the peer's container, and whether the
   in-level actor's visible state follows without `RestoreFromSave`. If it does not, the
   next step is a safe one-shot refresh, not a different container.
5. **Boss bar and arena exit**, which are two reflective calls, and then G6 travel.
