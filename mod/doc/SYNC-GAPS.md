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
| `ENEMY` | both | both | bidirectional; whoever owns an enemy publishes its rows. `flags` bits 4-5 carry the identity source (spawner / placed / summon) |
| `ENEMY_DETAIL` | both | both | montage asset + status mask + boss phase, reliable, on change, relevance-rate-limited |
| `BOSS` | both | both | **new**: boss bar on/off, observed phase, arena exit — each machine makes its own local call |
| `EQUIPMENT` / `EFFECT` / `EFFECT_MOVE` / `STATUS` | both | both | proven live |
| `INTENT` | peer | host | kinds 1 (flag), 2 (item pickup), 3 (item use), 6 (actor state), 7 (enemy hit), 8 (enemy record), **9 (placed object state)** |
| `DAMAGE` | host | peer | **new**: a host-run enemy's hit, applied to the partner's own pawn |
| `DIALOGUE` | both | both | per-slot talk state |
| `LEVEL_UNION` | both | both | what each machine's own game wants |
| `TRAVEL` | both | both | map + the destination actor, 2 s + on change; the hard handshake (`BEGIN`/`ARRIVED`/`RESUME`) rides the same kind |
| `CUTSCENE` | both | both | **new**: which manager each machine is running, 1 Hz; play-or-hold |
| `MOVERS` | owner → follower | follower | **new**: owner-authoritative elevator/platform transforms, 10 Hz |
| `LOOT` | both | both | **new**: the shared drop set plus permanent tombstones, 1 Hz |
| `SUSPEND` | both | both | **new**: the peer's `BlockSuspendSaves` hold and a suspend-load repair |
| `OBJECTS` | host | peer | **new**: placed interactables that are NOT SavableActors — the chest R67 measured, levers, placed pickups, gates — keyed by the cooked path hash; the partner is opened through the class' visual entry and its own state bool, never the interaction verb |

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

**G2 has two halves now, and the measured half was the wrong lane.** Session 8 R67
answered the identity question with one line: `SG_Chest_BP2 [SG_Chest_BP_C] savable=no
guid=- index=-1 ... opened=0 mySaveData=1 guids=0`. The chest has no GUID and no
`SavableActorStates` entry, so `ACTOR_STATES` **cannot represent it at all** — its identity
is a GUID and there is none. That is what `OBJECTS` (0x2A, §5.5) is for: a placed actor's
cooked path hash, the object's own state bool, and the class' visual entry. The container
half above remains the right lane for the actors that *are* savables (doors with an authored
GUID, barricades, a chest the Blueprint itself registered), and the two lanes do not overlap:
a class instance with a non-empty `GUIDs` array is skipped by the new lane and belongs to
`ACTOR_STATES`.

The `ACTOR_STATES` half is still unproven, and the remaining risk there is behavioural:
a subclass' `Restore State From Save Data` is the game's load path, so it must not award
loot or replay a one-time effect; the read-back logs `opened=1` (or `opened=0`) so the live
run can see whether the actor actually followed. `ActorStateSync` stays `0` until §5.6 shows
the door/chest open on the partner's screen and a looted chest refuses a second interaction.
`ObjectSync` stays `0` until §5.5 shows the placed chest open on the partner **and the
partner's `TotalChestsOpened` unchanged**.

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

**Enemies placed without a spawner, and summons, are now synced — by identity, not by
spawner.** Identity used to be the spawner's GUID, which only exists for enemies a
spawner created, so a placed enemy and a summoned one ran independently on each machine.
A row's key is now *(source, 128-bit key)*, with the source in the row's spare flag bits
and three deterministic sources: the spawner GUID where one exists and the spawner says
this actor is its `CurrentManagedEnemy` (the persisted-key path, unchanged), a hash of the
actor's cooked `"<world>/<level>/<name>"` path for a placed enemy, and a hash of *(summoner
key, spawn point, coincidence index)* for a runtime spawn. The summon key uses the **spawn
position, not a counter**, because the object scan discovers enemies in a per-machine order
and an ordinal assigned in scan order would differ between the two. A row for an unknown
hash-derived key is matched against the local position-derived records within 3 m and the
local record adopts the sender's key; an ambiguous match is refused, never guessed. See
`qdenemyid.h`, `WIRE.md` §3.9.

Two assumptions in that paragraph are unproven and each is one probe:

- **`CurrentManagedEnemy` decides who owns a spawner GUID.** This is the game's own answer
  to "which enemy does this spawner own" (SYNC-RESEARCH 5), and it is what makes the
  decision independent of scan order. If the property turns out unreadable, the code falls
  back to "this spawner owns this enemy", which is correct for the ordinary one-enemy case
  and only conflates a genuine multi-spawn.
- **A cooked level's placed actors have FName number 0.** That is what `ActorIsPlaced`
  uses to keep a runtime spawn off the placed path (its outer chain also reaches the world,
  so "the path resolves" is not the test). If a placed actor does carry a number, it is
  simply treated as a summon — still a stable key — and the receiver's adoption repairs the
  pair. The probe: on one machine, log `ObjNameRaw(...).number` for the placed enemies of a
  level and check they are all 0.

## 3. Inside the systems built so far

**Enemies**
- **A key's HP is reported only for a copy the engine will still RUN a function on.** The
  per-key dump used to print `hp=?` for a "live" key and leave it there; Session 21 asked
  it why and the answer was the mod's own `ObjectCallable`: the object item's deny bit
  (`FUObjectItem::Flags` bit 29, `EInternalObjectFlags::PendingKill`) or
  `RF_BeginDestroyed`/`RF_FinishDestroyed`. In the hub, **428 of 569 records were such
  ghosts and 141 were usable**, so "shared HP equal" was being asked of copies that were
  mid-teardown. Such a key is now reported `GONE` — neither `ALIVE` (which made the
  comparison unmeasurable) nor `DEAD` (which would invent a kill no lane broadcast) — and
  skipped by both the kill diff and the HP comparison. The same rule decides `qd.hit`'s
  target (`ran=0 hp 0.0 -> 0.0` on every swing before; `ran=1 hp 2590.0 -> -97410.0`
  after) and `near:enemy`, which used to park the player beside a ghost. Measured result:
  **71 shared-HP pairs compared, 0 unequal, max delta 0.1 HP** over 16 paired dumps in one
  39-minute run. `why=nofn|callfail|noout|zero|unseen`, with `seen=`, `inworld=` and both
  flag words, are on the line, so the next session reads the cause instead of re-deriving
  it.
- **Bosses share one bar, and the shared bar is now NEGOTIATED.** The game has one boss
  HP bar and `ActivateBossHPBar` binds it to the actor it is given, so the host is the
  single selector. Session 20 made the selection a round trip: the host asks the peer
  whether it holds a live copy (`ASK`), the peer answers `ACK`/`NACK`, a NACK (or 1.5 s of
  silence) makes the key ineligible for 3 s and the next candidate is tried, and `BAR_ON`
  is only sent after an ACK. **If nothing is showable on both machines, no bar is raised
  on either** — the honest agreement, not a full bar on one screen and an empty one on
  the other. Liveness is the OWNER's (`enemyown::OwnerSet::Running`): a follower takes the
  owner's row verbatim, and an explicit bar-on from the owner retracts a stale local
  death. A candidate also needs a READABLE HP (measured: a boss 71 m away whose encounter
  was not running was ACKed, bound and sampled `hp=0` forever while the dragon being
  fought was never selected). A NACK also re-arms that key's `ENEMY_SPAWN` class
  description, so a peer that gains a copy joins the fight. Live proof in
  `780_SZ_Depths`: the same key `17181166` on both bars, identical HP at every sample
  (65535 → 50555 → 35576 → 20596 → 5616), and both bars down 28 ms apart at 0.
  **Phases remain unexpressible**: `Phase`, `bBoss` and `OnSubStateChanged` exist only in
  the exe name block and in zero cooked packages, so `ENEMY_DETAIL.boss` carries only an
  observed ordinal and is 0 when nothing could be observed — it is never fabricated,
  because a guessed phase would run the wrong Blueprint branch on one machine only.
- **Boss fights start together (Session 26, `K_TRIGGER` 0x2C).** A boss fight starts when
  ONE player overlaps a placed `ActivationTrigger_C` or a spawner's own trap volume, and the
  volume answers only its own machine's player. The trigger lane carries that overlap
  (key = the trigger's placed path) and the partner replays the same bound event on its
  own copy; for a boss trigger the partner is first pulled in beside the player (`BossPullIn`,
  1500 cm). Ownership of a boss is sticky (no unilateral take, no mid-fight hand-off), which
  removed the "boss flickers, visible on one screen" fault. The bar is raised only for a boss
  in the fight (its level shown, and damaged or within 60 m of a player). Live, end to end on
  both machines -- one key, one HP, both bars up and down, the death on both, and the death
  flag in both banks: **green dragon + rider (`DragonLordDead`), Red Lords (`RedLordsDead`)**.
  Shared HP and the death also proven for the Shrine Boss, Whale King and Demon Bosses.
  **Open:** the death flag for Whale King / Demon Bosses; the 760 dragon, Necronomicon,
  Bridge Lord and Giant Fish did not spawn from the rig (their spawners do not spawn on
  activation -- a rig gap, the gates are "not yet dead" and pass); boss phases are still not
  synced as such (each machine runs its own copy's phase script on shared HP).
- **`TryApplyDamage` is unproven.** Every field of the payload is walked and logged, and
  the amount is selected by name; if the payload needs a damage-type tag or a nested `Data`
  struct, HP will not move and the log says so. "enemy damage had NO effect" (and now
  "player damage had NO effect") must stop appearing.
- **Montage playback is hardened but still unproven.** The follower resolves
  `Montage_Play` reflectively, sets its arguments by name only when the signature has
  them, guards the call, reads `GetCurrentActiveMontage` back, and logs the failure at
  most three times per enemy with the distinction that matters: "none playing" means the
  AnimBP plays **sequences rather than montages**, so there is nothing to mirror, while "a
  different montage" means the copy re-selected something of its own. A verified playback
  sets `playedOnce`, which the apply line reports. Until a live run shows
  `enemy anim: Montage_Play verified by read-back`, an AnimBP that uses sequences sends
  nothing.
- **Bandwidth with many enemies is now bounded, and unit-tested.** `ENEMY_DETAIL` was on
  change only with a flat 10-per-second bucket, which ignored *who* the detail was for: the
  enemy that changed first won the token whether or not the partner could see it.
  `qdenemybw.h` now ranks each change by its distance from the receiver's player and
  applies a per-enemy interval by band plus the global rolling-second budget; a refused
  change stays pending and is retried, so the lane degrades to "later". The bound is
  asserted in `test_enemybw.cpp` (48 assertions) rather than measured.
- **A death is a fact the observing machine can state, even when it does not run the
  enemy.** Session 19's 601 s run left one key (`FA2FC279`, a spawner GUID) dead on the
  host and absent from the peer. The death sweep only ran for keys the machine OWNED, so a
  death seen on a non-owned copy was in no outgoing lane at all; and the per-key dump
  called "no live actor" a death even for a record it had never seen alive, which is how a
  claim could exist that the lane never sent. Now an actor this machine saw alive and can
  no longer find, or that carries the game's own dead flag, is broadcast as a death claim
  by the machine that observed it (no save write — the owner's spawner owns persistence),
  and a record with no live actor and no evidence is reported as `stale=`, not DEAD. The
  mass-disappearance guard counts every non-mirror record, so a level unload is still one
  event rather than N kills.
- **Hit attribution is noisy.** A peer hit is detected as an HP drop the mod did not cause,
  so damage-over-time arrives as a stream of small hits.
- **Enemy projectiles are gated on ownership.** Only the machine running the shooter
  publishes its shot, so "one shot, one visual" is a property of the ownership model
  rather than of the freeze. Damage lands once, on the owner: its own projectile collides
  and traces normally, and the partner's copy has collision and actor tick disabled.

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
- **Entering an AREA now follows, or is refused by name.** `780_SZ_Depths`, `760_SZ_AUT`
  and the rest are sublevels of the persistent world, so entering one changes no `UWorld`
  name and a local lever moves only one machine. Session 19's run 3 ended with the host in
  `780_SZ_Depths` and the peer in the hub. `TP_AREA` (travel phase 4) announces the area in
  `dest` — `map` stays the `UWorld`, because the divergence board consumes it first — and
  the receiver streams the same area through the game's own level-list path
  (`FollowHardTravel`'s sublevel branch) and pins it. A name it cannot enter is
  `travel: REFUSED the partner's area <name>: <why>`, never silence. Measured: the peer
  followed the host into `780_SZ_Depths` in every measured entry, both machines reported
  `level engine state 780_SZ_Depths LoadedLevel=1 actuallyVisible=1`, and both stood 0-3 cm
  apart at the anchor.
  **A SECOND area in one session did not follow (Session 21, R150a) — CLOSED in Session 22.**
  The host streamed `770_SZ_Swamp` and the peer answered `travel: area 780_SZ_Depths is
  already pinned on this machine` — an announcement for the area it already held. Three
  causes, all measured:
  1. the pin was a one-way latch. `g_rigLevelPin` was set and never cleared, so "back to the
     hub" (no area) could never be expressed at all;
  2. both machines re-announced their own pin every 2 s, so a STALE pin beat a fresh change:
     the host streamed `770_SZ_Swamp` at 13:53:55.575 and was back in `780_SZ_Depths` at
     13:53:57.167 because the peer's echo of its old area was applied;
  3. a pin that came from the partner was echoed back, which is what closes the loop.
  Now: leaving an area is a real operation (`ClearAreaPin` hides the sublevel through the
  same `ForceLevel` the pin used), the leave is announced as `dest = "-"`, a
  partner-applied pin is never announced back, a machine that chose its own area within the
  last 8 s keeps it against an in-flight announcement, and the heal interval is 10 s.
  Measured in Session 22, `780_SZ_Depths` -> `770_SZ_Swamp` -> hub, both machines:
  `travel: entered the partner's area 770_SZ_Swamp by streaming it` and
  `travel: left the area 770_SZ_Swamp (the partner left it)` 24 ms after the host.
- **A hard travel is reachable from `Ruins_Level_00` again (Session 21 R150b, CLOSED in
  Session 22).** This game is ONE persistent world plus streamed area sublevels; the only
  separate `UWorld`s are the intro, the cutscenes and the title screen, and the hub's level
  table lists all of them, so `FollowHardTravel` streamed every name the rig tried:

  ```
  rig: hard lever -> 990_B33sment    streamed=1 (a sublevel of this world ...)
  rig: hard lever -> 900_SipehaWorld streamed=1 (a sublevel of this world ...)
  rig: hard lever -> Ruins_Level_00  LoadMapWithLoadingScreen ran=0: already in Ruins_Level_00
  ```

  `qd.hard auto` now walks that list with the streaming path disabled and keeps going until
  one really LOADS, and the FOLLOWER of a `HARD_BEGIN` does the same (it used to stream the
  announced world, report ARRIVED, and leave the pair in two different worlds). Measured,
  three times, out and back:

  ```
  rig: hard lever -> auto picked 997_FinalZoneCutscene (LoadMapWithLoadingScreen, not a sublevel)
  travel: HARD transition to 997_FinalZoneCutscene -- announcing it so the partner loads the same world
  peer: travel: hard transition: loading 997_FinalZoneCutscene through GameInstance.LoadMapWithLoadingScreen
  travel: both machines arrived in 997_FinalZoneCutscene; host releasing the pair to resume
  ```

  and the same for the return load to `Ruins_Level_00`. A hard travel costs a `WORLD
  DIVERGENCE` line while the two machines are mid-transition and one more when they agree
  again; that is the transition, not a desync.
- **A repeated hard world load used to kill the game (Session 22 found it, Session 23 fixed
  it - R152).** The peer died on the second hard return to `Ruins_Level_00`, and a fast loop
  showed it was any load after enough of them: six consecutive loads on the unfixed build, dead
  on the sixth, 7 min 51 s into the process. Both dumps fault at one instruction -
  `sub dword ptr [rax+10h],1` at RVA `0x102e790` - inside the engine's own container destroy,
  reached through `FEngineLoop::Tick` -> a UFunction -> `UObject::ProcessEvent`. In the second
  dump the pointer the lookup produced was `0x4100000000000032`, i.e. printable text: the
  container's backing store had been freed and reused. The module's only frames in that stack
  are the `ProcessEvent` forward and the tick detour's trampoline.
  The module was holding world-scoped state across that teardown (enemy records and their
  actors, constructed mirrors, the partner's body, spawned loot drops, `void* g_fxRing[64]`,
  latched `UClass*`, ~50 cached `UFunction*`, and a level table reset only when
  `StreamUnion=1`). `WorldTeardownReset` now drops all of it, before the module asks for a hard
  load and again when the world pointer changes, and `HK_Tick` does nothing between the request
  and the change. **Measured: 6 loads -> peer dead (unfixed); 8 loads -> both alive, 47 PASS /
  0 FAIL (fixed).** The engine-side double destruction itself is not claimed to be understood;
  what is proven is that the module no longer contributes to it. See `LIVE-TEST-LOG.md`
  Session 23.
- **One enemy key can name two bodies (Session 22's "standing HP divergence", explained and
  closed as a measurement in Session 23 - R151).** `530F7FD0` read 140 on one machine and 30 on
  the other for three segments. The host's own dump held BOTH under the key: a constructed copy
  at full HP 140 (`mirrored=1`) and the real enemy at 30. Two bodies at the same HP *fraction*
  cannot converge, so the difference was permanent. A real enemy now takes its key back from a
  copy (`DropMirrorForKey`), a key this machine already runs is never copied
  (`HoldsOwnLiveBody`), and the dump prints `mx=`, `peers=` and the whole 128-bit key (`id=`) so
  a duplicate is visible rather than reported as a desync. Measured after the fix in
  `780_SZ_Depths`: `peers=0` on every ALIVE line, 22 pairs compared, 0 unequal. **Still open:**
  the identity itself can hand one spawner GUID to more than one live enemy
  (`SpawnerManagesActor` falls back to `true` when `CurrentManagedEnemy` is unreadable), so a
  cross-machine pair can still be two different bodies; it is now counted as `mx-differ`, not as
  unequal HP.
- **A second engine crash, found by the Session 23 soak and named in Session 24 (R155, the
  module's half CLOSED).** After two hard round trips and 1605 s of body the peer died in
  ordinary play: `NULL_CLASS_PTR_READ` at RVA `0xE420D5`, `mov rax,[rsi+0x130]` with `rsi = 0`,
  inside `FUN_00E41B90` - the implementation of a Blueprint movement/steering function that
  reads `RootComponent` (+0x130) of `this` and of its **third parameter, an actor**, without
  testing that parameter. The call arrives through the generated exec thunk `FUN_00E54570` and
  vtable slot `0x890`, from `UObject::ProcessEvent`, from the module's pass-through forward, from
  game code. So the null target is the game's to pass; the mod's contribution is what made the
  graph run against nothing:
  the peer held **one live enemy key with two records whose class was `WidgetTree` and
  `SaveHelper_C`** (`enemykey AD410C56 ... cls=WidgetTree`, then `cls=SaveHelper_C` 0.2 s
  later). Nothing is an actor there: the object-table **slot had been reused**, and `weak::Ref`
  cannot see a reuse that happens before a capture. The scan then read a "transform" out of the
  WidgetTree, got `(0, -9.223372e18, 0)`, and `dev teleport near 'enemy'` **wrote it into the
  player pawn** - six seconds before the fault.
  Three fixes: a record now carries the class it was created for and `EnemyActor()` refuses and
  retires a record whose slot no longer holds it (sweep purge, resolver, scan, guid teleport,
  dump, `peers=`); `motion::SanePos()` makes `ReadActorXform` reject, and
  `SetActorXform`/`SetActorXformSweep`/`SweepPawnTo` refuse to write, anything that is not a
  finite position inside `|1e7|` cm; and `HK_ProcessEvent` refuses a dispatch on a null object
  and names the function. **Measured: two full `loot` phases on the fixed build, 0 FAULT lines,
  0 `ERR` lines, no dump, and the class guard never misfired.** The game's own missing null
  check is untouched - any graph that passes a null actor will still fault there - so this is
  closed as "the module no longer manufactures the state", not as "that instruction is safe".
  See `LIVE-TEST-LOG.md` Session 24.

## 4. Not synced, lower stakes

- **Moving world objects** are synced owner-authoritatively (`MoverSync`). The engine seam
  is `ReadActorXform` / `K2_SetActorLocationAndRotation`; the unproven part is the
  class-name filter that decides which actors are movers, so a live run must show the
  `movers: tracking …` lines for a real elevator.
- **Loot pickups** share ONE drop set, and a drop's identity is now the DEATH that caused
  it (`LootSync`). A take is a permanent tombstone and the local copy is hidden, not
  destroyed. **R129's root cause is fixed and measured** (Session 22): the key used to be
  `{item class, quantized spawn point, coincidence ordinal}`, which two machines cannot
  compute the same way -- they die at different centimetres and spend the coincidence
  ordinal a different number of times (`loot: drop 9FB8B0A8#1` on the peer for a drop the
  host called `#0`) -- and one machine often rolls NO drop at all for a death it only
  applied, because the HP-write fallback does not run the Blueprint death branch
  (`key 765952E9#0: host=absent peer=HELD`, four keys in one run). The key is now
  `{mixed hash of the enemy GUID, ordinal within that death}`: both machines run the same
  death for the same GUID, so both compute the same key with nothing negotiated. The machine
  that rolled the drop owns it and describes it on the lane (item class, point, facing), and
  a machine that holds no copy CONSTRUCTS the same class at the same point under the same
  key. Measured in every run since:
  `loot: drop 54F6FD50#0 = Herb_Item_BP_C (the death this machine ran)` on the peer,
  `loot: constructed the partner's drop 54F6FD50#0 = Herb_Item_BP_C at -27846,-15403,-15339`
  on the host, and `key 46FB326A#0: BOTH machines hold it` from the rig's own resolver --
  the precondition Session 21 could never reach. The lane's key stays addressable:
  `qd.tp near:drop:<8hex>:<index>`.
  **Session 23 measured two more ways to make the game consume a drop, and neither worked
  either** - but both removed a harness fault that had been masking the attempt. `qd.near`
  treats a non-positive offset as "not set" and substitutes 140, so every "stand on it" leg the
  harness had ever asked for actually stood 140 cm away (`dev teleport drop ... standing
  offset=140 move=teleport`, nine times in one take); the spec suffix `:on` now forces offset 0
  and a **sweep** (`SetActorXformSweep`, `bSweep=true, bTeleport=false`, in 100 cm steps),
  because the engine dispatches `BeginOverlap` from a MOVE and every earlier attempt was a
  position write. And `ReceiveActorBeginOverlap` - declared by `AActor`, and the event a
  Blueprint child *overrides* (an override creates no UFunction, which is why the child reports
  `0 UFUNCTIONs`) - is now the first drop verb. Measured in `780_SZ_Depths`, both directions,
  three drops rolled and `key D1DB5E8F#0: BOTH machines hold it`: the verb RAN
  (`drop verbs tried; ran=ReceiveActorBeginOverlap`), the gate said `IsInteractible=1`, the
  player's `InpActEvt_Interact` event ran, and the drop was still standing afterwards. **So the
  take half is still NOT-PROVEN**, and it is now blocked on something other than the four
  mechanisms Session 22 listed. The next probe is the content itself: read the drop class'
  collision/overlap component and whether its pickup body hangs off `ReceiveActorBeginOverlap`
  at all, and drive REAL movement (the input-injection rig) rather than any position write.
  **Session 24 found two more, and the second is the one no earlier session could see.** The
  pickup answers the Interact action *itself* - `ItemPickup_Base_C` declares
  `InpActEvt_Interact_K2Node_InputActionEvent`, with `PickupItemAnim__AllowInput__EventFunc` and
  the `DoItemPickupPause`/`EndItemPickupPause` pair - so the act lever had been pressing the
  *player's* compiled event, which is a different Blueprint. And the rig's "step onto the drop"
  leg never moved the pawn at all: `SweepPawnTo` from a point the capsule was just teleported to
  returns no movement when the start already penetrates geometry, so every `:on` leg since R153
  stood 3 m away (`take: pawn now 299 cm from the drop`, five stages running) and then reported
  that nothing consumed the drop. The new `qd.take` lever teleports onto the drop, stands at 0
  cm, and presses, in stages, `Activate`, the pickup's own `InpActEvt_Interact`, `Interact`,
  `ReceiveActorBeginOverlap` and `Pickup` - never a payout. **The tilt is unchanged: all five
  run and the drop stays**, and the reason is now a value rather than a mystery:
  the pickup's own **`Acquired`** bool is `1` before any press, on a rolled drop and on one the
  mod constructed (`take: gate before stage requireActivation=0 activated=0 acquired=1 ...`).
  Nothing in the module writes that property. Next: read it on the class default object, read
  the `PickedUp` delegate, and ask `GetInteractInfo` for every out-parameter.
  **Session 25 closed the take half: the order was the whole of it.** The lever presses
  `Interact` FIRST and the pickup's own dismiss events after it, and the first gate of a real
  take now reads `acquired=0`; `Interact` sets it to 1, the loot lane tombstones 13 ms later
  (`loot: local pickup DD452DFD#0 (Herb_Item_BP_C) -- tombstoned for the partner`), and the item
  is granted 70 ms after that (`inventory host set 2 entries (live 2) rev=214` on the host,
  `item intent queued ...HealingHerb x2` on the peer). The partner hides its copy under the
  lane's own key (`loot: hiding local drop DD452DFD#0 -- the partner took it`, 49 ms after the
  take) and the tombstone makes it untouchable -- `LootHide` now also disables its collision and
  sets `Acquired`, and a second attempt is refused by name (`rig: drop DD452DFD#0 is already
  gone on this machine`) with **no inventory change on either machine**. Measured in both
  directions in `780_SZ_Depths` (host takes `DD452DFD#0`, peer takes `54F6FD50#0`), 39 PASS /
  0 FAIL, kill sets diff 0, shared HP 26 compared / 0 unequal. Three earlier sessions of "the
  verb runs and consumes nothing" were the dismiss-before-Interact order, not a content gate.
  **Session 27 closed the standing actor.** A take whose showcase is never dismissed left the
  pickup actor on the taker's screen. Fifteen seconds after a local take, the loot lane runs the
  pickup's own `PutAway` (the dismiss path) once. If the actor still stands, the lane hides it
  and disables its collision (`loot: taken drop 54F6FD50#0 still stood 15 s after the take;
  PutAway ran -- hidden instead`). Measured in both directions in `780_SZ_Depths` (run 3).
  `PutAway` alone does not finish the hide outside a showcase, so the hide is what removes it.
- **Hit reactions to the partner's hits (closed, Session 27).** The owner applies the partner's
  hit through the class's own `TryApplyDamage`, the game's whole on-hit pipeline. The call goes
  first with the empty hit that has always worked: sent first, a built `FHitResult` stopped drop
  rolls (24 kills, 0 drops). If the class refuses, the call is retried with a built hit (the
  enemy, its mesh, a point on its body). If that is refused too, the clamped HP write runs and
  the class's own `DoCustomDamagedFX` plays. Measured: an ordinary enemy took the hit at
  220 -> 176 on both machines, and the green dragon took it inside its fight at 3500 -> 3300.
  Outside its fight the dragon refuses every payload: its 761-byte override calls the parent
  `TryApplyDamage` and then `CheckForSplit`.
- **Flags that only an actor's load path reads (Session 27, `FlagRefreshWorld`).** Some placed
  actors keep their state only as a NamedSaveData flag and apply it only on load. When such a
  flag is applied from the partner (peer flag apply, host intent apply), the actors listed for
  it run their own load path. First entry: `ShopDoorOpened` makes `Shop_Door_00_BP_C` run
  `Activate(ActivateByLoad=1)` and then `SetToOpenState`. The shop door (R105) was a one-sided
  door opened by `OpenDoor`, not a lane failure. It now opens on both machines: the host opens
  it, and the peer's copy opens when the flag arrives.
- **The world follows every partner flag (closed, Session 28).** The game never writes a story
  flag bare: `SaveFunctions.SaveNamedData(Name, Data, RefreshQuestStates)` calls
  `GameInstance.GI_UpdateQuestState(NSD)` and `GI_NotifyWorldStateChanged()`, which every
  `WorldEventReceiver` listens to (the 14 `ActorQuestStateManager_C`, teleporters, checkpoints,
  the shop, spawners' `RequireQuestStateToSpawn`). The flag lane wrote the bank directly, so the
  receiving world never heard it. Every applied flag (peer apply, host intent apply) is now
  queued and, at most every 250 ms, the same two calls run on the live GameInstance
  (`QuestRefreshPump`). Measured run 1: the host wrote `LibrarianQuestDone` through the game's
  own `SaveNamedData`; 0.2 s after it arrived, the peer's `LibrarianQuestStateManager` went
  `InternalState 0 -> 1`.
- **Bosses start on both machines (Session 28).** Three mechanisms, all the game's own calls:
  - *Trigger follow-through.* A dispatched overlap does not start a boss: the trigger's Blueprint
    re-checks the player (grounded, still overlapping) and a placed or pulled-in player is
    falling through the volume. After a boss entry (rig or replay) the trigger's own action runs:
    `ActivationTrigger.DoTrigger()`, `Activate(false, player)` on a boss spawner target without a
    managed enemy, or `EnemySpawner.ActivateEnemy()` for a spawner trap; repeated when a boss
    appears after the trap (the Whale King spawns by proximity). Measured: ROS dragon now on both
    machines, one key, one bar, 9999 -> 9799 on both, dead on both (runs 2, 3, 5, 6).
  - *Partner boss start.* A partner's row for a live boss spawner key within 300 m that has no
    copy here starts the local spawner through its class recipe (default `Activate`,
    `Try Spawning Enemy`; the Bridge Lord's `BridgeCenterTrigger` overlap event). The peer also
    asks the host (`kBossAsk`) about bosses the host sent no row for, so a peer-only boss starts
    on the host. A dead key is never restarted.
  - *Boss identity.* `GiantFish_Spawner_BP_C` is a boss though its data says `Boss = 0`.
- **Death flags (Session 28).** `DemonBossesKilled` needs the spider AND the hell-bat (now in both
  banks, run 1); `GiantSwampFish_IsDead` is in both banks (run 4). The others are written at the
  end of the boss's own death sequence, not on the HP drop: the Bridge Lord and the Necronomicon
  through a death dialogue (`DummyNPC` -> `DialogueEnded` -> `Kill` -> `SaveNamedData`), the ROS
  dragon and the Whale King in a death montage's `OnCompleted`. **Open:** the ROS dragon dies on
  both machines and plays `Dragon_Ground_Die_Montage`, and neither machine reaches the callback
  (runs 2-6); a solo control run is needed to tell the mod from the rig's kill.
- **Shop, journal/bestiary, XP, weapon levels, grimoire ink.** Per-player by nature.
- **Peer vitals (closed, Session 27).** Each machine reads its own player's `Health` and
  `GetMaxHP` and sends them on the STATUS tail (WIRE.md 3.15, vitals tail: `hp`, `hp_max`,
  `flags` valid/dead). The receiver draws `PARTNER  hp / max`, or `PARTNER  DOWN`, as a bar
  at the top centre of its HUD. It draws through the HUD's own `DrawRect`/`DrawText`, right
  after the engine's `FS_HUD_C.ReceiveDrawHUD` dispatch crosses the ProcessEvent hook.
  Measured: the peer set its HP to 37, the host logged `vitals: partner hp 37/100` 53 ms later,
  and the bar showed 37 / 100 in the host's screenshot. **There is no stamina to show**: this
  build's player has no stamina stat (the player Blueprint declares none, and `PlayerBalance`
  keeps only a leftover `BaseJumpStaminaCost`). The relay validates every body, so it had to
  be rebuilt to accept the tail (an old relay drops the longer STATUS as malformed).
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
12. **Loot (`LootSync=1`).** `loot: drop <hash>#<n> = <path> (the death this machine ran)`
    on the roller, and `loot: constructed the partner's drop <hash>#<n> = <path> at <x,y,z>`
    on the machine that held no copy — the same key and the same point on both.  When one
    player picks it up, `loot: local pickup … -- tombstoned` and
    `loot: hiding local drop … -- the partner took it`.  **Pass:** the drop exists on both
    screens under one key (MEASURED, Session 22), the drop disappears on both, and only one
    copy enters an inventory.  The last two have never been observed: the rig cannot make
    the game consume a drop, so the take half is still a human-only observation.
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

### 5.3 The enemy-parity round — what to grep for

Added with the identity / montage / projectile / boss / bandwidth work. Each line names
the log text the code emits, so a run either shows it or does not.

1. **Identity.** On the owner, one line per enemy the first time it is identified:
   `enemy id: placed <path>` for a placed enemy, or `enemy id: summon at x,y,z dup=N
   key=…` for a summon — and **no** such line for a spawner-owned enemy (that path keeps
   the spawner GUID and logs only at DEBUG). A runtime spawn with no recorded summoner
   logs `enemy id: no summoner for <class>; keyed by class and spawn point`. On the
   follower, the row's `flags` bits 4-5 (`0x10` placed, `0x20` summon) appear in
   `enemy detail sent …` and the copy must follow. If the follower minted a different key,
   one line says so: `enemy id: adopted the owner's key … (local … src=A->B)`. An
   ambiguous match prints `enemy id: N local position-keyed enemies near an unknown key;
   not adopting (ambiguous)` instead of guessing.
2. **Montage.** The follower must print `enemy anim: Montage_Play verified by read-back`
    at least once. `enemy anim: Montage_Play ran but read-back says NONE -- this AnimBP
    probably plays sequences, not montages` is the measured answer that there is nothing
    to mirror — a finding, not a bug in the call. A resolve failure is
    `enemy anim: Montage_Play did not resolve`, once; a fault is
    `enemy anim: Montage_Play faulted`, at most three times per enemy.
3. **Projectiles.** With the two players apart, only the machine running the shooter
    prints `effect: local #<id> <class> v=…` for an enemy's shot; the other prints only
    `effect: <class> shown`. Two `local` lines for one shot is the failure this gated.
4. **Boss.** On the owner: `boss: guid=… is a boss`, then `boss: ActivateBossHPBar
    called` and `boss: sent guid=… phase=… bar on`; on the follower, when it takes over:
    `boss: follower raised its own bar for guid=… hp=… phase=…`. A
    `boss: no local copy for guid=…` means the follower has no body for that boss and the
    bar cannot be raised there — the pre-existing "no spawn on the follower" limit, now
    named. `boss: PlayerTeleportedOutOfBossArena did not resolve` means this build's
    spawner does not carry the event.
5. **Bandwidth.** Under a big fight, one DEBUG line per second:
    `enemy detail: candidates=N sent=M budgetDrops=… farSkips=… overflows=…`. `sent` must
    never exceed 10 in any rolling second, and `budgetDrops` should be 0 with two players
    together in an ordinary fight.

### 5.4 The exact G3 grant / G4 damage-effect lines now required

Session 6's two failures have both been root-caused and fixed (API-GAPS §5A), so a run
with the same scenario must now produce these instead of the failure lines. Grep them
literally; the `->` is `count -> count` and `hp -> hp`.

**G3 — the grant lands.** The success line is the one the native counter oracle prints;
it only appears when the count rose by exactly the requested amount.

```
write api: AddItemToInventory /Game/Items/KeyItems/AquaDoorKey/AquaDoorKey_2_Data.AquaDoorKey_2_Data +1 read back 0 -> 1
inventory grant applied: <path> x1 (peer total <N> rev=<R>)
```

and the peer settles:

```
item intent confirmed: <path> host=1
```

The **failure** forms that must be gone, and what each now means if it appears:

| If this appears | Then |
|---|---|
| `count did not rise by the requested amount: 0 -> 0` | the soft record still does not resolve: check the `AddItemToInventory` size line says `size=40`, and that `ResolveItemObject` found an `FS_ItemData_C` |
| `cannot resolve the item object for read-back` | the item asset is not loaded on the host — a load-order fact, not an offset bug |
| `the item soft reference has neither a live object nor a sub-path` | the path had no `.Object` segment, so nothing could be written |
| `SoftObjectProperty is smaller than an FSoftObjectPtr` | the build changed the soft form; re-measure before anything else |

**G4 — the player really takes the damage.** Two lines are accepted, in this priority;
the first is the game's own path, the second is the proven degradation, and each says
which one ran.

```
damage: player hp via TryApplyDamage 20.0 (TryApplyDamage returned 0.00) hp 400.0 -> 380.0
damage: player hp via Health (TryApplyDamage had no effect) 20.0 (TryApplyDamage returned 0.00) hp 400.0 -> 380.0
```

and, when the amount is enough to empty the bar:

```
damage: the player reached 0 hp; Kill called
```

The K_DAMAGE receiver's summary line must show the movement too:

```
damage from enemy <guid> melee: 20.0 applied hp 400.0 -> 380.0
```

The failure forms:

| If this appears | Then |
|---|---|
| `player damage had NO effect: TryApplyDamage ran asked 20.0 returned 0.00 hp 400.0 -> 400.0 (both the message and the Health write failed)` | the message still does not apply **and** `Health` could not be written — the pawn had no readable `Health`, which is the thing to investigate |
| `damage: the player reached 0 hp; Kill called` missing while hp reached 0 | `Kill` did not resolve on the pawn class; `LogFunctionsOf` on `FS_PlayerChar_C` says whether the verb is there |
| `hp 400.0 -> 380.0` absent because the **rig floor** refilled it | the floor is `qd.god` (400); the G4 step must not run with the floor armed if the HP movement itself is what is being measured. The `damage:` line is printed from the value read back inside the call, so it is still the proof the write happened |

**Why the second form is accepted rather than a failure.** Session 6 measured the player's
own `TryApplyDamage` running and returning `AppliedDamage = 0` while the identical payload
took an enemy from `34.9` to `-165.0`. Its script gates the HP write behind
`IncomingDamageMult`, `GetSimpleSumStat`, `TakenDamageMultiplier`, `IsDead`,
`HasPitInvincibility`/`KnockedDown`, and a `bCanBeDamaged` branch that skips the write
(API-GAPS §5A.2). Writing `Health` is the same scalar the game's `Set Health Points` and
the rig's own god floor already write, so it is the class's own state, not a fake.

### 5.5 The placed-object round — `ObjectSync=1`

The lane `OBJECTS` (0x2A, WIRE.md §3.20) is for interactables that are **not**
`SavableActorStates` entries. R67 measured the case: `SG_Chest_BP2 [SG_Chest_BP_C]
savable=no guid=- index=-1 ... opened=0`, so `ACTOR_STATES` cannot carry it. Run with
`ObjectSync=1`, `FlagSync=1`, `LogLevel=2`, and `ActorStateSync=0` — the two object lanes
must be proven separately, because the new lane skips any actor that has a `GUIDs` entry.

**1. The two machines must name the same objects with the same keys.** One line per object
the first time it is classified, at INFO. The key is the same 8 hex digits on both, and the
shape is the one the cooked-package survey predicts:

```
[1] INFO object sync: tracking chest SG_Chest_BP2 [SG_Chest_BP_C] key=1A2B3C4D state=0 prop=Opened visual=SetToFinalState
[2] INFO object sync: tracking chest SG_Chest_BP2 [SG_Chest_BP_C] key=1A2B3C4D state=0 prop=Opened visual=SetToFinalState
```

A door that arrives as `tracking door Door_Base_7 [Door_Base_C] ... prop=Opened
visual=SetToOpenState` and a placed pickup as `tracking pickup Herb_Item_BP
[ItemPickup_Base_C] ... prop=PickedUp visual=none` are the same proof for the other shapes.
These counted lines are the **failure** forms, and each says which object was not carried:

| If this appears | Then |
|---|---|
| `object sync: <class> carries no readable state property; not published (<path>)` | the class gained (or lost) a property name between builds: re-run the survey, do not guess |
| `object sync: N object(s) could not be represented (table full)` | more than 256 placed interactables in the loaded world; the bound is named, nothing is silently dropped |
| a `dev state:` line for the same actor says `guids=` > 0 | the actor is a savable and belongs to `ActorStateSync`, not here — expected, not a bug |

**2. An opened chest reaches the partner as an intent, then an absolute echo.** The rig
opens the chest on the **host** (`near:closedchest`, then the interact lever) and the
sequence on the two machines is:

```
[1] INFO object local change: queued=1 coalesced=0 echoes=0 overflow=0
[1] INFO object intent sent key=1A2B3C4D chest state=1 rev=3 try 1/5
[2] INFO object intent applied: SG_Chest_BP2 key=1A2B3C4D state=1 visual=SetToFinalState prop=Opened 0->1
[2] INFO objects applied=1 held=0 confirmed=1 failed=0 unknown=0 close-ignored=0 rev=4 n=6
[1] INFO object intent confirmed: key=1A2B3C4D state=1 host=1
```

(The mirror image — the peer opens it — prints `object intent sent` on the **peer** and
`object apply: chest SG_Chest_BP2 key=… state=1 rev=N visual=SetToFinalState prop=Opened
0->1` on the host, followed by `objects applied=1 … confirmed=1` on the peer when the echo
arrives. R73's gate is what makes the `rev=0` case work: `based_on_rev == 0` means "no
baseline yet", not stale.)

**3. The state converged and the visuals followed.** The partner's own `dev state:` probe
must show the gate moved, which is the same line R67 used to prove the chest was closed:

```
[2] INFO dev state: SG_Chest_BP2 [SG_Chest_BP_C] savable=no guid=- index=-1 value=-1 isset=? opened=1 isOpen=n/a mySaveData=1 guids=0
```

and the partner's chest must **refuse the interaction** (the `IsInteractible` gate reads
`Opened`). On screen: the lid is open on both machines.

**4. Nothing paid out twice.** This is the half a screenshot cannot show, so it is a grep:

- the partner logs **no** `write api: AddItemToInventory` for the chest's contents, and
  **no** `object apply failed … refused: interaction/payout verb` line (a refusal means a
  ladder member named a verb it must not; the ladder is in `qdobjectstate.h`);
- `TotalChestsOpened` on the partner arrives through the **flags** lane, as an absolute
  write, and is `2` — not `3`. The accepted lines are the existing ones:
  `flag CHANGE TotalChestsOpened : 1 -> 2` on the machine that opened it, and on the
  partner either nothing (it was already 2) or a single absolute write of `2`;
- the chest's own loot appears **once**, on the machine the player was standing at.

**5. The lane is bounded and repairs itself.** `objects sent rev=… objects=N` must appear
once per change and once per 5 s of quiet (the repair pass), never at 1 Hz — this lane is
reliable, so a 1 Hz body would be a retransmission budget spent on nothing. A newly joined
peer shows `objects sent rev=…` within the first 5 s of the session without any action.
`object intent given up key=… after 5 attempts, the host never echoed it` is the failure
line: it means the host never had that key, which is a key disagreement to investigate,
not a lost datagram.



## 6. Suggested order from here

1. **Run §5 and §5.3.** Every correction so far came from measurement rather than from
   play, and the systems added in both rounds have never all touched the game together.
2. **B7's live answer** (`PlayerBody=1`, §5.1), the player-class spawn. It is now the
   single blocker for enemy targeting, for spawner activation, and for the puppet's
   appearance — and the code path exists, so the run is a decision rather than a piece of
   work.
3. **G2's live follow-up.** If §5.6 shows `INCOMPLETE`, inspect that subclass' own
   `Restore State From Save Data` override before changing the container path; if the
   live pass shows the in-level actor does not follow the container at all, start from
   the visual refresh the second round added. §5.5 is the other half: an actor with no
   GUID at all is `ObjectSync`'s, and the two lanes are proven separately.
4. **Boss bar, arena exit, and phases.** The bar and the arena exit are two reflective
   calls, both measured and both driven. Boss phases remain unexpressible unless a boss
   turns out to expose a phase to a sync layer after all — the `BOSS` and `ENEMY_DETAIL`
   fields are already there to carry one.
5. **Hard map transitions** are built against the game's own
   `GameInstance.LoadMapWithLoadingScreen` and need a measured entry point into another
   `UWorld`; the live run has to confirm the call lands in the new `UWorld` (§5 item 9).
   If it does not, the refusal is named and the session survives, which is the correct
   fallback until the signature is proven.
