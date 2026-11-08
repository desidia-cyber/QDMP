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
machines; §5 lists what a first live session has to show. The module passes 3 600+
assertions across 14 off-target test suites, which proves the state machines and the wire
encoding — not the game.

## 0. What the module sends today

| Kind | Sent by | Handled by | Note |
|---|---|---|---|
| `PLAYER` | both | both | proven live |
| `WORLD_FLAGS` | host | peer | absolute set, chunked, 1 Hz + on change |
| `INVENTORY` | **both** | **both** | host: absolute set. Peer: `INTENT kind 2` for its own pickups and `kind 3` for its consumptions — G3 |
| `ACTOR_STATES` | **both** | **both** | host: absolute set. Peer: `INTENT kind 6` — G2 |
| `ENEMY` | both | both | bidirectional; whoever owns an enemy publishes its rows |
| `ENEMY_DETAIL` | both | both | montage asset + status mask, reliable, on change |
| `EQUIPMENT` / `EFFECT` / `EFFECT_MOVE` / `STATUS` | both | both | proven live |
| `INTENT` | peer | host | kinds 1 (flag), 2 (item pickup), 3 (item use), 6 (actor state), 7 (enemy hit), 8 (enemy record) |
| `DAMAGE` | host | peer | **new**: a host-run enemy's hit, applied to the partner's own pawn |
| `DIALOGUE` | both | both | per-slot talk state |
| `LEVEL_UNION` | both | both | what each machine's own game wants |
| `TRAVEL` | both | both | map + the destination actor, 2 s + on change; the hard handshake (`BEGIN`/`ARRIVED`/`RESUME`) rides the same kind |
| `CUTSCENE` | both | both | **new**: which manager each machine is running, 1 Hz; play-or-hold |
| `MOVERS` | owner → follower | follower | **new**: owner-authoritative elevator/platform transforms, 10 Hz |
| `LOOT` | both | both | **new**: the shared drop set plus permanent tombstones, 1 Hz |
| `SUSPEND` | both | both | **new**: the peer's `BlockSuspendSaves` hold and a suspend-load repair |

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

The **in-level actor now follows the container** on the frame the value changes. The
game's only apply path is `SavableActor_C.RestoreFromSave()`, which does
`CacheSaveData(); Restore State From Save Data(MySaveData)`; calling it repeatedly is
unsafe because `CacheSaveData` uses `Array_Add`, so each call appends another copy of
the GUIDs. The mod therefore clears only the `MySaveData` array's logical length (its
allocation is left alone), calls the game's own `RestoreFromSave`, and reads three
things back: `GetSavableActorDataAtIndex(Index).IsSet`, the rebuilt
`MySaveData`/`GUIDs` lengths, and the actor's own `Opened` bool when it has one. That is
the same code the game runs on every load, not a hand-driven "open" call, so a door or
chest opened by the partner visibly opens here, becomes non-interactable, and a chest
looted by one player cannot be looted again by the other. `WrRefreshActorState` is
called once per changed GUID on both machines (peer `K_ACTOR_STATES` apply and host
`INTENT kind 6` apply); the 1 Hz heartbeat skips unchanged values, so it does not replay.
`ActorStateSync` remains `0` in the shipped config, ready to flip.

**G3 — both directions of the inventory are shared.** The host publishes its whole
`InventoryContents`; the peer grants only what it lacks. The peer now measures its own
contribution (`local − what the host granted this peer`, never `local − the host's total`,
which would cancel an item both players picked up independently) and sends it as a
**cumulative** `INTENT kind 2`. Cumulative rather than a delta, because the grant is
additive on the receiving side: a resent delta would add the item twice, a resent
cumulative value applies nothing. The host applies only the increase over what it has
already applied, which makes a reliable-lane retransmit exactly-once.

**Keys and doors use both lanes, and neither needed a third mechanism.** A key is an
ordinary `InventoryContents` entry, so the G3 lane already gives it to both players: the
host grants it to the peer, or the peer's pickup becomes a cumulative `INTENT kind 2`.
`Door_Base_C.CheckRequiredItems` (and `Chest_C.TryUseKey`) call the native
`GetInventoryItemCount` on the interacting player, which reads that same container; a
granted key therefore satisfies the door's own check, and the key is not consumed by
opening (a door that does consume it lowers the local count, which is now `INTENT kind
3`). The door's open/closed state is the `SavableActorStates` entry, so G2 carries it and
the `RestoreFromSave` refresh makes the other player see it. Nothing in the door reads a
GUID or a path: it reads the local inventory and its own savable state, and both are now
correct on both machines.

**A consumption is sent too.** A fall in the peer's local count raises a second,
independent cumulative counter and goes out as `INTENT kind 3` (`I_CONSUME_ITEM`). The
host removes only the increase through the game's own
`SaveFunctions_C.TryConsumingItemFromInventory`, with the native counter as the oracle
(the count must fall by exactly the amount removed). While a use is outstanding, the
peer's grant target is `hostCount − outstandingUse`, so a host body that crossed the use
on the wire cannot hand the item straight back. That is what turns the old
"peer consumes, next host body replenishes, counts drift upward" into exact convergence;
a host that holds fewer than requested removes what it has and the use is settled, never
invented. The peer's own use is what the counter means; the host's own use still lowers
only the host's stock and is not sent (SYNC-DESIGN section 2.4).

**G4 — a host-run enemy can hurt the distant partner.** The host sees the attack (it owns
the enemy, and the attack montage is already an event), takes the amount from
`GetAttackDamageForSelectedAttack` (measured: `EnemyData_C.BaseDamage` times the active
hitbox multiplier) and the range from `GetAttackRange`, and sends `DAMAGE`. The peer
applies it to its own pawn through `TryApplyDamage` and reads `Health` back. A failed
resolution degrades to `Data.BaseDamage × multiplier`, and only then to a stated estimate
carrying `kDamageGuessed` — never an estimate presented as measured. This also fixed the
pre-existing struct lookup that asked for the parameter name `DamageToApply` instead of the
type `FB_DamageAttributeSet`, which would have disabled all enemy damage again.
That is the *described* lane, for a hit the host cannot apply. With `PlayerBody=1` the hit
is no longer described at all: the game resolves it against the partner's player-class body
and the mod forwards the game's own `FB_DamageAttributeSet` (see §2 and §5.1).

**G6 — the partner follows a travel.** `LocationData` is an `ObjectProperty` on the placed
carrier; checkpoints happen to also have a `GUID` but teleporters have none, so a GUID
scheme would silently skip every teleporter. `dest` is therefore the carrier's short object
name plus the announced map — both machines run the same cooked world. A local travel is
observed from the game's own `Teleporting` / `TeleportedRecently` state; the receiver
resolves *its own* copy of the carrier and drives only its own pawn through
`TeleportToLocation`. A busy player is skipped and not remembered; a wrong-world or
unresolvable destination is refused once, logged, and never retried.

**G6b — a HARD map switch is crossed, and both arrive before either resumes.** The
in-world follow cannot cross a different `UWorld` (`900_SipehaWorld`, `990_B33sment`, the
`997/998/999_*` cutscene maps): the destination actor does not exist on the follower. The
game's own entry for that is the native `UFS_GameInstanceBase.LoadMapWithLoadingScreen`
UFunction (registration table `0x03950090`, exec thunk `0x00E58F70`, body `0x00E34F30` —
it sets `bLoadingMap`, records `CurrentlyLoadingMapPackage`, and starts an async world
load). The mod calls it on the follower through `ProcessEvent`, with the map asset
resolved by the same `LoadAsset_Blocking` path the body uses; `OpenLevel` is not called.
The handshake is `HARD_BEGIN` → each machine `ARRIVED` → host `RESUME`; until both have
arrived, the existing `MapBoard` divergence gate keeps absolute state unapplied. A load
that fails or exceeds 60 s is refused once and never retried. `TravelSync` is independent
of `StreamUnion`, so this works with the streaming union off.

**Cutscenes are played or held, and both resume together.** A cutscene is per-machine
Blueprint state, so the sender publishes which manager it is running. The receiver
resolves the same manager in its own cooked world and calls
`BeginCutsceneWithManager`, or — when the manager cannot be resolved — is held
consistently with `EnterCutsceneMode` / `LockInputForLoadingOrCutscene` until the sender
reports `ENDED`, then released with `ExitCutsceneMode` /
`UnlockInputForLoadingOrCutscene`. A silent sender goes stale in 5 s, so a crash cannot
leave the partner's input locked.

**Moving world objects are owner-authoritative.** The nearer player's machine runs an
elevator / platform / `Activatable` mover and publishes its absolute transform; the
other machine applies the transform and never re-runs the mover's own logic. Ownership
follows the enemy rule (nearer player, host breaks an exact tie).

**Loot is one shared set.** The game's roll key `{sourceId, dropIndex}` has no player
dimension; the host publishes the set under that key and a drop taken by either player
becomes a permanent tombstone that hides the copy on the other machine. A tombstone can
never be resurrected by a later body that still lists the item.

**Dialogue: a concurrent start is prevented, and death ends the conversation.** A
per-NPC reservation is taken before a local start is published; if the partner holds the
NPC (or the reservations crossed), the peer ends its own talk through the game's
`DialogueManager.End` / `PlayerChar.DialogueEnded`, and the host keeps it. A local death
releases the lock and ends the talk immediately instead of waiting for the 2-minute
expiry.

**Suspend saves are held, and a load is repaired.** While a session is live the peer
calls the game's own `BlockSuspendSaves`, so its timed suspend save cannot advance past
the host. If a suspend load happens anyway, the peer reports `LOADED` and the host
immediately re-sends `WORLD_FLAGS`, `ACTOR_STATES` and `INVENTORY` (forced dirty rather
than waiting for the 1 Hz cycle), then reports `REAPPLIED`; a repair that never lands is
reported at 15 s.

**Earlier rounds, still standing:** enemies fight the player who can reach them (ownership
by proximity, `ENEMY` bidirectional — inside the 800 cm margin the tie is now broken by
role, so co-located players do not both simulate every enemy, measured live as R45 in
`LIVE-TEST-LOG`); enemy damage goes through `TryApplyDamage`, which is
the real path, not the inert `ApplyDamage`; enemy montages, status and projectiles are
synced; the dialogue block writes `NPC_Base.BlockInteract`, a bool property the old
code called as a function and therefore never blocked anything; the streaming union vetoes
both `SetShouldBeVisible` and the byte-verified `SetShouldBeLoaded`.

## 2. What still breaks the co-op promise

**G2 is implemented but unproven.** The container, the actor's cached `MySaveData` and
the in-level visual state now converge through the writer plus the one-shot
`RestoreFromSave` refresh described in §1. The remaining risk is behavioural, not
structural: a subclass' `Restore State From Save Data` is the game's load path, so it
must not award loot or replay a one-time effect; the read-back logs `opened=1` (or
`opened=0`) so the live run can see whether the actor actually followed. `ActorStateSync`
stays `0` until §5.6 shows the door/chest open on the partner's screen and a looted
chest refuses a second interaction.

**G3's remaining caveats are policy, not plumbing.** A peer's *pickup* is still
cumulative, so if both players independently pick up the same item the gains are added
(the design's "granted twice is harmless" side) — but if the host's body already reports
the count the peer independently reached before the peer detects its own gain, the two
are indistinguishable and the intent settles as already-satisfied. A peer's *use* is now
sent and removed exactly once; the one remaining asymmetry is the host's own use, which
lowers the host's stock and is not propagated (the peer keeps its copy until it uses it).
A host that holds fewer than the requested amount removes what it has and logs
`consume partial`; the use is settled, never invented. Consumables therefore converge
after a peer use, but a host use is still per-player by design.

**G4's targeting half and G5 are now built behind one switch, and nothing about them is
proven.** The damage lane covers an enemy the host runs attacking the partner, but the
Blueprint still *chose* the host every time: `TargetPlayer` is a Blueprint variable the AI
reads directly (so the `ProcessEvent` post-hook idea is refuted), and two cast gates admit
only an `FS_PlayerChar_C` implementing `FB_DamageableInterface`, which the puppet is not.
`PlayerBody=1` now spawns the partner's body as that class (unpossessed, tickless) and
drives it with the same poseable/animation/weapon mirroring, which makes both levers
available:

- the host writes `TargetPlayer` to whichever of the two players is nearer, but only when
  the AI's own perception has already picked one of them (`qdplayerbody.h`), so an enemy
  chasing an NPC keeps its decision;
- a hit the game itself resolves on that body is read out of its own
  `FB_DamageAttributeSet`, sent to the machine that owns the player through `K_DAMAGE`, and
  the local application is suppressed — one authority per player's HP, as before.

**G5 is fixed at the state, not at the range check.** `PlayerIsInActivationRange` and
`PlayerDistanceDespawnCheck` are Blueprint functions on the spawner that read
`GetPlayerCharacter` — player 0 of *this* machine — so the host's spawner cannot see that
the partner is standing next to the enemy. The mod cannot change that answer, and does not
pretend to: when the host's spawner re-creates an enemy the partner is already fighting,
the partner's own row for that GUID carries the HP it has taken off, and the difference is
applied as the game's own damage, so the fight continues instead of restarting. A spawner
that despawns while the partner is near is still the game's decision; ownership has
already moved to the partner's machine by then, so its copy keeps running.

What is unproven is the whole of B7: the engine constructing a full gameplay Blueprint.
Everything above is off until that is answered (`API-GAPS` §5, B7), and `SYNC-GAPS` §5
now lists the exact log lines that answer it.

**The arrow still only points one way.** A body exists for the partner on *both* machines,
but `DAMAGE` is host → peer. So a peer-run enemy that picks the host's body hurts nothing:
the hit is applied to the dummy on the peer's machine and is not forwarded. In practice
ownership keeps that case rare (an enemy with the host nearest is run by the host), and the
described lane never covered it either, so it is not a regression — but it is the next
arrow to add if a live run shows the host being attacked by an enemy the partner runs.

**G6 crosses a hard map switch now, but the load itself is unproven live.** The native
`LoadMapWithLoadingScreen` UFunction is identified and called, the handshake is built and
tested, and `LoadAsset_Blocking` resolves the map package. What a live run has to show is
that the game accepts the call and actually lands in the new `UWorld`; the parameter list
is discovered at runtime (and logged), so a signature surprise is a named refusal rather
than a wrong call. Until that line is seen, `TravelSync` stays `0`.

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
- **Concurrent start is prevented now.** A per-NPC reservation and the slot rule end the
  loser's conversation through the game's own `DialogueManager.End` /
  `PlayerChar.DialogueEnded`. The remaining risk is the game not accepting that end call;
  the mod then logs the prevention once per 2 s and the next absolute state settles it.
- **Death ends a conversation.** A local death releases the lock and ends the talk
  explicitly, so the partner's NPC is unblocked immediately rather than at the 2-minute
  expiry. The live question is whether `End` / `DialogueEnded` are enough to clear the
  game's own `RunningDialogue`.
- **Choices are not sent as choices.** Progress flows through flags, by design.
- `entry` (the cursor within a sequence) is still read as 0.

**Streaming**
- **The two-levels-per-pump budget** can produce a visible hitch when the players separate.
- **Forced levels stay loaded after release** (only visibility is released), trading memory
  for not re-loading and re-spawning the level's enemies.
- **Travel following no longer depends on `StreamUnion`.** `TravelSync` carries the map
  announcement and the hard handshake on its own; the union's divergence gate is still
  consulted when both are on.

## 4. Not synced, lower stakes

- **Moving world objects** are synced owner-authoritatively (`MoverSync`). The engine seam
  is `ReadActorXform` / `K2_SetActorLocationAndRotation`; the unproven part is the
  class-name filter that decides which actors are movers, so a live run must show the
  `movers: tracking …` lines for a real elevator.
- **Loot pickups** share one host-authoritative drop set keyed by `{sourceId, dropIndex}`
  (`LootSync`). A take is a permanent tombstone and the local copy is hidden, not
  destroyed. The unproven part is recovering the key/path from a `SpawnItemDrop_C` actor;
  a class that exposes none is reported once and skipped.
- **Shop, journal/bestiary, XP, weapon levels, grimoire ink.** Per-player by nature.
- **Peer vitals.** The partner's HP and death are not shown; a death shows as the body
  falling. This is also why the damage lane's dead check is receive-side only.
- **Cutscenes / sequencer** are synced as a play-or-hold (`CutsceneSync`); the receiver
  either resolves the same manager or is held consistently. A cutscene whose manager is
  spawned at runtime is held, not replicated.
- **Suspend saves** are held on the peer with the game's own `BlockSuspendSaves`, and a
  suspend load is repaired by re-applying the host's absolute state (`SuspendGuard`).

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
   → host `actor-state refresh ok … opened=1` → peer `actor states applied=1 confirmed=1`
   and `actor-state refresh ok … opened=1`, and both containers agree for that GUID. If
   the refresh line says `INCOMPLETE`, the actor class' `Restore State From Save Data`
   override did not follow — the exact sentence the log prints is the next thing to read.
   The door must visibly open on the partner **without** a load, and a chest the partner
   looted must refuse a second interaction.
7. **G3.** Peer picks up the aqua key: peer `item intent sent … try 1/5` → host
   `inventory grant applied: … (peer total N)` → peer `item intent confirmed`. Then the
   key opens the matching door for the other player (G3 supplies the key; G2 supplies the
   door state). Peer uses a potion: peer `item consume queued` → `item consume sent …` →
   host `inventory consume applied: … -1 (peer total 1)` → peer `item consume confirmed`,
   and the count does not rise again on the next host body.
8. **Streaming and travel.** `level name: <name> from <source>` with no `REFUSED`; a
   teleport produces `travel: local destination …=` on the sender and `travel: followed …`
   on the partner.
9. **G6 hard switch (`TravelSync=1`).** A hard map switch produces
   `travel: HARD transition … announcing` on the sender, `travel: hard transition: loading
   … through GameInstance.LoadMapWithLoadingScreen` on the partner (with the
   `signature LoadMapWithLoadingScreen props=…` line once), then
   `travel: the partner arrived in …`, both maps agree, and the host logs
   `travel: both machines arrived … host releasing the pair to resume`. No
   `WORLD DIVERGENCE` line survives the switch. **Pass:** both machines are in the same
   map and the partner keeps playing. **Fail:** the load line is followed by a named
   refusal (`unhandled parameter type`, `not found`, `faulted`) and the session stays in
   the old world: that is the signature surprise, not a crash.
10. **Cutscenes (`CutsceneSync=1`).** `cutscene: local started <manager>` on the
    sender; `cutscene: playing the partner's sequence <manager>` or
    `cutscene: holding the local player …` on the partner; then
    `cutscene: partner's cutscene ended; resuming together (exit=1 unlock=1)`. **Pass:**
    both players are held for the same interval and both regain input.
11. **Movers (`MoverSync=1`).** `movers: tracking <path> id=…` for an elevator/platform;
    the rider's machine logs owner rows, the other logs
    `movers: applied N owner row(s)`. **Pass:** the platform is under the rider on both
    screens.
12. **Loot (`LootSync=1`).** `loot: drop <hash>#<n> = <path>` on the roller; the other
    machine merges it; when one player picks it up, `loot: local pickup … -- tombstoned`
    and `loot: hiding local drop … -- the partner took it`. **Pass:** the drop disappears
    on both screens and only one copy enters an inventory.
13. **Dialogue (`DialogueSync=1`).** A crossed start logs
    `dialogue: PREVENTED a concurrent start on the same NPC … ending the local conversation`
    (peer) or `dialogue: the partner also started this NPC … the host keeps it` (host);
    a death while talking logs `dialogue: local death ended the conversation …; lock
    released`. **Pass:** the turn-in pays out once and the partner's NPC unblocks at once.
14. **Suspend (`SuspendGuard=1`).** `suspend: peer holds BlockSuspendSaves=1`; on a
    suspend load, `suspend: peer loaded a suspend save (slot …); announcing it …`, then
    `suspend: host re-applying WORLD_FLAGS, ACTOR_STATES and INVENTORY …`, then
    `suspend: partner re-applied the host's state`. **Pass:** the peer's progression
    does not rewind after the load.

### 5.1 B7 / G4 / G5 — the exact lines a run with `PlayerBody=1` must show

`PlayerBody=1` needs `PuppetSync=1` and `EnemySync=1`; the load line warns if either is
missing (`PlayerBody=1 needs PuppetSync=1 and EnemySync=1`). Prefix every line with the
machine it came from. **Order matters: 1 and 2 decide whether the rest can happen at
all.**

1. **The class resolves, and only once.**
   `player body: class FS_PlayerChar_C resolved`
   (or `… class FS_Full_PlayerChar_C resolved (FS_PlayerChar_C is not in this build)`).
   If instead: `player body: neither FS_PlayerChar_C nor FS_Full_PlayerChar_C resolved;
   PlayerBody cannot work on this build` — the switch cannot work here; stop and re-read
   the class names. No further `player body:` line appears until the peer is present.
2. **The engine constructs it.** `player body: possession cleared (AutoPossessPlayer 0,
   AutoPossessAI 0)` then `player body: spawned unpossessed at x,y,z (tick off, movement
   off/mode set, controller none, collision off, own meshes hidden); the poseable body is
   the visible one`, then `puppet spawned as FS_PlayerChar_C at x,y,z`. A `MISSING` in that
   parenthesis is a real finding, not noise: the property is absent on this build.
   On the first hit only: `player body: damage hook armed on the proxy (DamageToApply
   size=…, amount -> DamageAmount @…, applier Applier)`.
   Failures, each fatal to the proof: `player body: spawn of the player class failed;
   falling back to the mesh body`, `player body: the engine's own spawn pair faulted on the
   player class (B7 fails on this build)`, `player body: the spawn took over THIS machine's
   player slot (AutoPossessPlayer was not honoured); destroying it`, or
   `player body: the hit payload is not buildable (…); hits on the proxy will be applied
   locally`.
3. **The AI is steered, and only when it already picked a player.**
   `enemy <guid>: TargetPlayer local player -> the remote body (read back) dLocal=… dRemote=…`
   when the partner is nearer, and the matching `… remote body -> the local player (read
   back) …` on the way back. Both must be `read back`; `WRITE FAILED` means the property
   offset is wrong for that Blueprint class. **No line at all is the correct result for an
   enemy chasing an NPC** — the mod never rewrites such a target.
4. **The game's own hit reaches the partner.** On the host:
   `player body hit guid=… amount=… centi=… (melee|ranged) -> sent to the machine that owns
   the player; local damage suppressed`; on the partner the existing receiver line:
   `damage from enemy <guid> (melee|ranged): N applied hp a -> b`. No
   `player damage had NO effect`, and — the new invariant — **no** `damage sent guid=… seq=…`
   for the same swing, because the game's own hit supersedes the described one. If both
   appear for one swing the partner was hit twice; that is the double-description guard
   failing.
   A `player body: hit not forwarded (…)` line names why a hit was applied to the proxy
   instead (no session, no enemy to attribute it to, unbuildable payload); it is DEBUG and
   rate-limited, so it is only expected in a run at `LogLevel=3`.
5. **G5's adoption.** Host walks up to an enemy the partner has been fighting:
   `enemy spawn adopted guid=… hp a -> b: the partner was already fighting it (local Ncm,
   partner Mcm; the spawner's range check reads the local player only)` on the host, with
   `b < a` and `M` well under `N` — those two numbers are the evidence that the spawner's
   decision and the partner's body disagreed. Without the adoption the line is absent and
   the host's copy starts at full HP, which is the old bug.
6. **Nothing regressed.** The puppet lines stay as they were: `puppet at x,y,z … err=0.0cm`,
   `peer action kind …`, `body: hand R now holds <path>`, and no
   `player damage had NO effect` / `enemy damage had NO effect` anywhere.

The proof fails if any of these is missing: the class line, the spawn line, at least one
`TargetPlayer … (read back)` in each direction, one `player body hit … -> sent`, and one
`enemy spawn adopted`. A run that shows them but where the partner's HP does not move is a
`FB_DamageAttributeSet` fill problem, and the `damage from enemy … hp a -> b` line is where
the amount and the result are both visible.

### 5.2 The exact G2/G3 proof lines

Grep these literally; each one is emitted by the path it names.

```
actor-state refresh: RestoreFromSave resolved at 0x... frame=0 (MySaveData cleared, cache rebuilt, state re-applied)
actor-state refresh ok <FS_..._C /Game/...> index=0 value=1 mySaveData=0->1 guids=1 cached=ok opened=1
actor-state refresh ok <FS_..._C /Game/...> index=0 value=1 mySaveData=1->1 guids=1 cached=ok opened=1 forced=1
actor-state refresh INCOMPLETE <FS_..._C /Game/...> index=0 value=1 mySaveData=1->1 guids=1 cached=NOT SET opened=0
actor-state refresh failed rev=<R> guid=<g0>-<g1>-<g2>-<g3>: <reason>
item consume queued <path> x1
item consume sent <path> x1 rev=<R> try 1/5
item consume confirmed: <path> host=4
inventory consume applied: <path> -1 (peer total 1 rev=<R>)
write api: TryConsumingItemFromInventory <path> -1 read back 5 -> 4
inventory consume partial: <path> asked -2 but the host held 1; the rest is settled, never invented
```

The first `actor-state refresh: RestoreFromSave resolved` line is printed once per
process. A successful door/chest must show `actor-state refresh ok` on **both** machines
on the frame the value crosses; `INCOMPLETE` (getter disagrees) or `failed` (no
`RestoreFromSave`, signature mismatch, or fault) is the actionable failure. On the peer,
`item consume sent` followed by `inventory consume applied` and `item consume confirmed`
is the proof the drift is gone; the next `inventory sent rev=…` line must carry the lower
count, not the old one.

## 6. Suggested order from here

1. **Run §5.** Every correction so far came from measurement rather than from play, and the
   four systems added this round have never touched the game.
2. **B7's live answer** (`PlayerBody=1`, §5.1). It is now the single blocker for enemy
   targeting, for spawner activation, and for the puppet's appearance — and the code path
   exists, so the run is a decision rather than a piece of work.
3. **G2's live follow-up**, if §5.6 shows `INCOMPLETE`: inspect that subclass' own
   `Restore State From Save Data` override before changing the container path.
4. **Boss bar and arena exit** — two reflective calls, both measured.
5. **Hard map transitions** are built against the game's own
   `GameInstance.LoadMapWithLoadingScreen`; the live run has to confirm the call lands in
   the new `UWorld` (§5 item 9). If it does not, the refusal is named and the session
   survives, which is the correct fallback until the signature is proven.
