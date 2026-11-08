# SYNC-GAPS — what is still not synced, and what it costs

Written 2026-11-08, third round. The first round built enemy, dialogue and
level-streaming sync; the second made the peer's progression reach the host, gave enemies
ownership by proximity, fixed the enemy damage entry and invented the inventory mirror;
this round closed the four gaps that were left — one-shot world objects (G2), the peer's
own pickups (G3's other half), a host-run enemy hurting the distant partner (G4), and
travel coordination (G6).

Two documents belong beside this one:

* [`SYNC-RESEARCH.md`](SYNC-RESEARCH.md) — what the *game* actually does, measured from
  the shipped image and the cooked packages, with the probe that settles each claim.
* [`WIRE.md`](WIRE.md) — what the messages are.

"Built" still means **not proven live**. Every write path is behind a switch in
`qdcoop.ini` and every one of those is `0` except `FlagSync`. Nothing here has run on two
machines; §5 lists what a first live session has to show. The module passes 3 598
assertions across 12 off-target test suites, which proves the state machines and the wire
encoding — not the game.

## 0. What the module sends today

| Kind | Sent by | Handled by | Note |
|---|---|---|---|
| `PLAYER` | both | both | proven live |
| `WORLD_FLAGS` | host | peer | absolute set, chunked, 1 Hz + on change |
| `INVENTORY` | **both** | **both** | host: absolute set. Peer: `INTENT kind 2` for its own pickups — G3 |
| `ACTOR_STATES` | **both** | **both** | host: absolute set. Peer: `INTENT kind 6` — G2 |
| `ENEMY` | both | both | bidirectional; whoever owns an enemy publishes its rows |
| `ENEMY_DETAIL` | both | both | montage asset + status mask, reliable, on change |
| `EQUIPMENT` / `EFFECT` / `EFFECT_MOVE` / `STATUS` | both | both | proven live |
| `INTENT` | peer | host | kinds 1 (flag), 2 (item), 6 (actor state), 7 (enemy hit), 8 (enemy record) |
| `DAMAGE` | host | peer | **new**: a host-run enemy's hit, applied to the partner's own pawn |
| `DIALOGUE` | both | both | per-slot talk state |
| `LEVEL_UNION` | both | both | what each machine's own game wants |
| `TRAVEL` | both | both | map + the destination actor, 2 s + on change |

Every kind in `WIRE.md` §2 now has a producer and a consumer.

## 1. What is already fixed

**G1 — the peer's progression reaches the host.** A rise in `NamedSaveData` observed by the
peer's read-only mirror becomes an absolute `INTENT kind 1`, with per-key echo
suppression, a bounded retry and rate limiting (`qdpending.h`). Checkpoints ride the same
path: `CheckpointUnlockCount` is just another key. This uncovered a defect in the host's
gate — it compared against its *next* revision rather than the published one, which would
have refused every intent forever.

**G2 — chests, doors and barricades are synced.** `SavableActorStates` is a reflected
`Map<FGuid,int32>` on `FS_SaveSlot_C`, not an unlocated `save+0x168` lead, so the offset is
found at runtime and the geometry (stride `0x1C`, value `+0x10`) comes from the engine's
own `TMap` instantiations. Writes go through the game's own
`SavableActor_C.Save Data Index to World Save(Index, Data)` and are read back two ways
(container and getter). The host publishes the absolute set; a peer's own change is
`INTENT kind 6`. The peer applies the host's value **absolutely in either direction** — a
chest stays open, a door can close — but holds a disagreeing body while it has a local
change in flight, so a 1 Hz heartbeat cannot silently undo the peer's action.

**G3 — both directions of the inventory are shared.** The host publishes its whole
`InventoryContents`; the peer grants only what it lacks. The peer now also measures its own
contribution (`local − what the host granted this peer`, never `local − the host's total`,
which would cancel an item both players picked up independently) and sends it as a
**cumulative** `INTENT kind 2`. Cumulative rather than a delta, because the grant is
additive on the receiving side: a resent delta would add the item twice, a resent
cumulative value applies nothing. The host applies only the increase over what it has
already applied, which makes a reliable-lane retransmit exactly-once.

**G4 — a host-run enemy can hurt the distant partner.** The host sees the attack (it owns
the enemy, and the attack montage is already an event), takes the amount from
`GetAttackDamageForSelectedAttack` (measured: `EnemyData_C.BaseDamage` times the active
hitbox multiplier) and the range from `GetAttackRange`, and sends `DAMAGE`. The peer
applies it to its own pawn through `TryApplyDamage` and reads `Health` back. A failed
resolution degrades to `Data.BaseDamage × multiplier`, and only then to a stated estimate
carrying `kDamageGuessed` — never an estimate presented as measured. This also fixed the
pre-existing struct lookup that asked for the parameter name `DamageToApply` instead of the
type `FB_DamageAttributeSet`, which would have disabled all enemy damage again.

**G6 — the partner follows a travel.** `LocationData` is an `ObjectProperty` on the placed
carrier; checkpoints happen to also have a `GUID` but teleporters have none, so a GUID
scheme would silently skip every teleporter. `dest` is therefore the carrier's short object
name plus the announced map — both machines run the same cooked world. A local travel is
observed from the game's own `Teleporting` / `TeleportedRecently` state; the receiver
resolves *its own* copy of the carrier and drives only its own pawn through
`TeleportToLocation`. A busy player is skipped and not remembered; a wrong-world or
unresolvable destination is refused once, logged, and never retried.

**Earlier rounds, still standing:** enemies fight the player who can reach them (ownership
by proximity, `ENEMY` bidirectional — inside the 800 cm margin the tie is now broken by
role, so co-located players do not both simulate every enemy, measured live as R45 in
`LIVE-TEST-LOG`); enemy damage goes through `TryApplyDamage`, which is
the real path, not the inert `ApplyDamage`; enemy montages, status and projectiles are
synced; the dialogue block writes `NPC_Base.BlockInteract`, a bool property the old
code called as a function and therefore never blocked anything; the streaming union vetoes
both `SetShouldBeVisible` and the byte-verified `SetShouldBeLoaded`.

## 2. What still breaks the co-op promise

**G2's remaining question is visibility, not state.** The container and the actor's cached
`MySaveData` converge through the writer, but the *in-level* actor may only re-read its
visual state on the game's own load path (`RestoreFromSave`), which is not called because
`CacheSaveData` appends to `MySaveData` and the package imports no clear/remove — repeated
refreshes would grow the array. A live pass has to show whether a door or chest shows the
change without it. If it does not, the next step is a safe one-shot refresh, not a
different container. Until then `ActorStateSync` stays `0`.

**G3's remaining caveats are policy, not plumbing.** A peer's *consumption* is not sent and
is replenished by the next host body, so consumable counts drift upward on the peer. If
both players independently pick up the same item, the gains are added (the design's
"granted twice is harmless" side) — but if the host's body already reports the count the
peer independently reached before the peer detects its own gain, the two are
indistinguishable and the intent settles as already-satisfied. The host's own consumption
is a high-water floor, so it is never re-granted to the peer, who keeps its copy until it
uses it.

**G4's real fix is still blocked, and G5 with it.** The damage lane covers an enemy the host
runs attacking the partner. What it does *not* do is make the Blueprint *target* the remote
body: `TargetPlayer` is a Blueprint variable the AI reads directly (so the `ProcessEvent`
post-hook idea is refuted), and two cast gates admit only an `FS_PlayerChar_C` implementing
`FB_DamageableInterface`, which the puppet is not. That is the unproven spawn proof
(`API-GAPS` §5, B7), and it also blocks G5: `PlayerIsInActivationRange` and
`PlayerDistanceDespawnCheck` read the local player, so when the host walks up to an enemy
the peer was running, the host's spawner spawns a **fresh** enemy and ownership moves to it.

**G6 cannot cross a hard map switch.** Following works inside one persistent world, where
both machines run the same cooked world. After a transition to a different `UWorld`
(`900_SipehaWorld`, `990_B33sment`, the cutscene maps) the destination actor does not exist
on the follower, and that is the explicit once-only refusal. Actually moving the player
across a hard boundary is still out of scope: `OpenLevel` is not the game's travel path,
and no measured entry point takes a machine into another world on request.

**Enemies placed without a spawner are still not synced**, because identity is the
spawner's GUID. Summoned and boss-spawned enemies run independently on both machines.

## 3. Inside the systems built so far

**Enemies**
- **Bosses are still a row and nothing more.** The boss health bar (`ActivateBossHPBar` on
  `PlayerStatsAndUtils_C`) and the arena exit (`PlayerTeleportedOutOfBossArena`, a
  zero-argument spawner event) are both drivable on the peer and are not driven. Boss
  **phases** are not expressible at all: no cooked package contains `Phase`, `bBoss` or
  `OnSubStateChanged`.
- **`TryApplyDamage` is unproven.** Every field of the payload is walked and logged, and
  the amount is selected by name; if the payload needs a damage-type tag or a nested `Data`
  struct, HP will not move and the log says so. "enemy damage had NO effect" (and now
  "player damage had NO effect") must stop appearing.
- **Montage playback is unproven.** The follower resolves `Montage_Play` reflectively and
  reads `GetCurrentActiveMontage` back, so a failure is visible; an AnimBP that plays
  sequences rather than montages sends nothing.
- **Bandwidth with many enemies is unmeasured.** `ENEMY_DETAIL` is on change only, but a
  large fight is exactly when montages change most.
- **Hit attribution is noisy.** A peer hit is detected as an HP drop the mod did not cause,
  so damage-over-time arrives as a stream of small hits.

**Dialogue**
- **Concurrent start is logged, not prevented.** Both players can start the same NPC within
  one round trip, and a turn-in can pay out twice.
- **Death does not end a conversation.** The module only observes `RunningDialogue`; if the
  game does not end the talk on death, the partner's NPC stays blocked until it goes stale.
- **Choices are not sent as choices.** Progress flows through flags, by design.
- `entry` (the cursor within a sequence) is still read as 0.

**Streaming**
- **The two-levels-per-pump budget** can produce a visible hitch when the players separate.
- **Forced levels stay loaded after release** (only visibility is released), trading memory
  for not re-loading and re-spawning the level's enemies.
- **World divergence and travel following only exist when `StreamUnion` is on.** With the
  switch off, nothing tracks which map the partner is in and nobody follows anybody.

## 4. Not synced, lower stakes

- **Moving world objects** (elevators, platforms, `Activatable` movers): each machine runs
  its own, so a player riding one is shown floating on the other.
- **Loot pickups.** Each machine rolls its own drops; a drop taken on one machine stays on
  the floor on the other, and quest items can be doubled.
- **Shop, journal/bestiary, XP, weapon levels, grimoire ink.** Per-player by nature.
- **Peer vitals.** The partner's HP and death are not shown; a death shows as the body
  falling. This is also why the damage lane's dead check is receive-side only.
- **Cutscenes / sequencer** play only on the machine that triggers them.
- **Suspend saves.** Nothing holds `BlockSuspendSaves` or re-applies host state after a
  suspend load.

## 5. What the live two-instance run has to show

Ordered, because each one makes the next diagnosable.

1. **Load.** The `switches:` line; `NAMED-SAVE ORACLE PASS` (or a loud FAIL); the container
   offsets including a non-negative `SavableActorStates` and no geometry-validation refusal;
   `enemy sync: FS_EnemyCharacterBase found`; `hooked ULevelStreaming::SetShouldBeLoaded`.
2. **G1.** Peer opens a barricade: peer `flag intent sent … try 1/5` → `flag intent
   confirmed`; host `intent applied:`. No second send for the same value.
3. **Enemies.** `EnemySync=1`: a kill with the peer nearby shows `Paused ok=1`, an ownership
   handoff, and **no** `enemy damage had NO effect`.
4. **G4.** Peer far away, host-run enemy attacks: host `damage sent … (GetAttackDamageFor-
   SelectedAttack) range=…`, peer `damage from enemy … applied hp a -> b`. No
   `player damage had NO effect`.
5. **Dialogue.** `dialogue: BLOCK <npc> BlockInteract=1` on the partner's machine, and the
   NPC really does not start.
6. **G2.** Peer opens a door: `actor-state intent sent` → host `actor-state intent applied:`
   → peer `actor states applied=1 confirmed=1`, and both containers agree for that GUID.
   Then the visibility question: does the door look open on the peer **without** a load?
7. **G3.** Peer picks up the aqua key: peer `item intent sent … try 1/5` → host
   `inventory grant applied: … (peer total N)` → peer `item intent confirmed`.
8. **Streaming and travel.** `level name: <name> from <source>` with no `REFUSED`; a
   teleport produces `travel: local destination …=` on the sender and `travel: followed …`
   on the partner; a hard map switch produces one `WORLD DIVERGENCE` and one explicit
   `cannot follow … recorded`, and the session survives.

## 6. Suggested order from here

1. **Run §5.** Every correction so far came from measurement rather than from play, and the
   four systems added this round have never touched the game.
2. **B7, the player-class spawn.** It is now the single blocker for enemy targeting, for
   spawner activation, and for the puppet's appearance.
3. **G2's visual refresh**, if the live pass shows the in-level actor does not follow the
   container.
4. **Boss bar and arena exit** — two reflective calls, both measured.
5. **Hard map transitions**, which need a measured entry point into another `UWorld`; until
   then the refusal is the correct behaviour.
