# SYNC-GAPS — what is still not synced, and what it costs

Written 2026-11-08 after enemy, dialogue and level-streaming sync were built
(branch `sync-world`). Everything here comes from reading the module as it stands —
which wire kinds it actually sends and handles — and from the cooked Blueprint name tables
(`analysis/pak/code/blueprints.py` on `EnemySpawner`, `EnemyBase`, `NPC_Base`,
`DialogueManager`). It is not taken from what the design documents intend. "Built" still
means **not proven live**: the three new systems are behind `EnemySync`, `DialogueSync`
and `StreamUnion`, all still `0`.

## 0. What the module sends today

| Kind | Sent | Handled | Note |
|---|---|---|---|
| `PLAYER` | yes | yes | proven live |
| `WORLD_FLAGS` | host only | peer | host → peer only; see G1 |
| `ACTOR_STATES` | **no** | **no** | G2 |
| `INVENTORY` | **no** | **no** | G3 |
| `ENEMY` | host | peer | new, not proven live |
| `EQUIPMENT` / `EFFECT` / `EFFECT_MOVE` / `STATUS` | yes | yes | proven live |
| `INTENT` | peer: kinds 7, 8 only | host: 1, 6 (log only), 4, 7, 8 | **no flag/item/checkpoint intent is ever sent** (G1) |
| `DIALOGUE` | yes | yes | new, not proven live |
| `LEVEL_UNION` | yes | yes | new, not proven live |

## 1. Gaps that break the co-op promise (fix first)

**G1. The peer's progression never reaches the host.** `HostApplyIntent` accepts
`I_SET_FLAG`, but nothing sends one. When the peer opens a barricade, picks up the flying
sword or finishes a quest step, its `NamedSaveData` changes **locally only**. The host's
`WORLD_FLAGS` cannot lower the peer's value (monotone rule), so the two saves now differ
for good, and the host's world never shows what the peer did.
*Needed:* the mirror already sees every local change (`MirrorObserve`); on the peer, turn
each raised value into `INTENT kind 1` (absolute, `based_on_rev` = the last host rev). The
write side is already proven (`NsdWrite` no-op round trip, LIVE-TEST-LOG R21). This is the
cheapest gap and the biggest one.

**G2. One-shot world objects (`SavableActorStates`) are not synced.** Chests, doors,
breakable barricades and `ActivatableGroup` setters each live on one machine only. Failure:
both players loot the same chest, and a door opened by one player is shut for the other.
*Blocked by:* the container offset (`save+0x168` is a LOW/MEDIUM lead, API-GAPS §2) and the
lack of any native writer. Getting a read-back oracle is the first step.

**G3. Inventory and key items are not synced.** A key picked up by one player does not
exist for the other, so that player is locked out of the region the key opens
(SYNC-DESIGN §2.4 calls this out as the reason inventory is host-owned).
*Blocked by:* the inventory **write** (`SaveFunctions_C.AddItemToInventory` has no pair-table
entry and has to be reached reflectively, API-GAPS §2). The read side (`save+0x28`,
stride 0x18) is measured.

**G4. Enemies only fight the player on their own machine.** Measured in the cooked
names: aggro, chase and attack go through `GetPlayerCharacter` / `TargetPlayer` /
`GetTargetPlayer` in `EnemyBase_C` — player 0 of that machine. A host-simulated enemy
therefore never targets, chases or damages the peer, and the peer is invulnerable
wherever the host runs the enemies. The peer's driven copies do not attack either,
because their AI is off.
*Needed:* a way to make the host's enemy pick the remote body as a target. The puppet is
a `SkeletalMeshActor`, not an `FS_PlayerChar_C`, so a cast in the Blueprint fails. The
options are (a) a hidden proxy pawn of the player class standing at the puppet's position
that the Blueprint can target, or (b) overriding the target in the Blueprint's
`GetTargetPlayer` through a `ProcessEvent` post-hook. (b) only works if that call is a
real `ProcessEvent` dispatch and not a VM-internal call, and that is unmeasured. Damage
taken by the peer would then be an event from host to peer, so a new intent kind would
be needed in that direction.

**G5. Spawners activate only for their own machine's player.** `PlayerIsInActivationRange`,
`CheckSpawnerMaxDist` and `PlayerDistanceDespawnCheck` are Blueprint and read the local
player. The enemy model built here works around this: when the host has no live copy,
the peer runs the enemy itself, and kills come back as `INTENT kind 8`. The cost is that
when the host walks up later, its spawner spawns a **fresh** enemy (full HP, new
position), and the peer's copy switches to following it. Making a spawner count the
remote player is the same problem as G4.

**G6. Level transitions are not coordinated.** Hard transitions (intro, cutscene maps
`997/998/999_*`, `SipehaWorld`, `B33sment`) and travel are not handled. When one player
travels, the two are in different worlds. The union, the enemy table and the dialogue
board all reset on a world change, and nothing brings the other player along. SYNC-DESIGN
§3 specifies host-announced, acknowledged travel; the travel API itself is still an open
item (API-GAPS §4). **Announcement plus refusal is implemented** (`K_TRAVEL`, sent by
both machines, tracked by `stream::MapBoard`): each machine announces its short map name
on change and every ~2 s; a difference that persists 3 s is logged once, and cross-world
state is dropped — the partner's `LEVEL_UNION` names are not installed and
`K_ENEMY`/`K_DIALOGUE` are not applied — until the names match again. Hard travel itself
stays refused in co-op v1; the session is not torn down.

## 2. Gaps inside the three systems just built

**Enemies**
- **Attacks and animation.** A driven copy moves and turns. With its AI and tick off it
  plays no attack montage, so the peer sees an enemy that slides into range and does
  nothing. Its locomotion animation depends on the movement component's velocity, which
  is not written. Sync the montage the way `PLAYER.action` does: the active montage's
  asset as an index, plus a counter.
- **Enemy projectiles and hit effects** exist on the host only (fireballs and spit from
  enemies). They could ride the existing `EFFECT` lane.
- **Status effects on enemies** (fire, poison, stagger, `Poisoned`, `bIsStaggered`) are
  not synced. Only HP converges.
- **Bosses.** A boss is synced as an enemy row (position + HP) and nothing more: phases,
  the boss health bar (`ActivateBossHPBar`), arena teleports
  (`PlayerTeleportedOutOfBossArena`) and `PairedBossSpawners` run per machine. The
  multi-GUID spawner (`LargeRobot_Rusted_Spawner`, two GUIDs) is keyed by whatever
  `GetSaveGUID` returns, which is unverified for that spawner.
- **Enemies without a spawner** (placed directly, or summoned by a boss) have no key and
  are not synced at all. They run on both machines.
- **The damage path is unproven.** HP convergence and peer hits use
  `UGameplayStatics::ApplyDamage`. Analysis D5 says damage goes ApplyDamage → TakeDamage,
  but the game also uses `ApplyDamageToHitboxTarget`. Every call is read back, and the
  log says "enemy damage had NO effect" if that path is wrong.
- **Hit attribution.** A peer hit is detected as an HP drop on the copy that the mod did
  not cause. Damage over time on a driven copy, such as poison applied by the peer,
  arrives as a stream of small hits. That is correct, but noisy.

**Dialogue**
- **Choices are not synced, by design.** Progression is supposed to flow through flags
  (G1). Until G1 is fixed, a quest choice the peer makes in dialogue reaches nobody.
- `entry` (the cursor within a sequence) is not read and is sent as 0. Nothing uses it
  yet.
- **Concurrent start.** If both players start the same NPC within one round trip, both
  conversations run. This is logged, not prevented. Turn-in NPCs can then pay out twice
  until G1/G3 put the host in charge of the reward.
- **Death.** The design says a death must end the conversation. The module only
  *observes* `RunningDialogue`. If the game does not end the talk on death, the partner's
  NPC stays blocked until the talk ends or goes stale (5 s after the sender stops).
- `BlockInteract`'s signature is logged on first use, but it is unverified. If it takes
  arguments, they are passed as zero.

**Level streaming**
- **Both setters are guarded.** `SetShouldBeVisible` (`0x029ACB20`) and the virtual
  `SetShouldBeLoaded` (body `0x029ACAC0`, reached through vtable `+0x270`) are vetoed
  for a level the partner needs. The measured manager path only calls the visibility
  setter, so the unload hook is inert unless the game calls it.
- **Loading cost.** A partner's levels are shown at most two per 500 ms, so a hitch is
  possible when the players separate. Forced levels stay *loaded* after release (only
  visibility is released), which costs memory; that is deliberate because it avoids a
  re-load and a re-spawn when the partner crosses the boundary again.
- **`bShouldBeVisible` at +0xBA bit 3 and `bShouldBeLoaded` at +0xBA bit 4** are
  verified by the setters' own bytes (and `ShouldBeLoaded` at vtable `+0x278` reads bit
  4). The `WorldAsset` soft path at +0x10 used for level names is still unverified; the
  resolver now logs which source produced each name once and refuses empty/implausible
  names (preferring `PackageNameToLoad` when the two disagree) rather than publishing
  them into the union.

## 3. Not synced, lower stakes

- **Checkpoints** (`INTENT kind 5`, `SavePointsVisited`, `CheckpointUnlockCount`): never sent,
  so each player unlocks fast-travel alone.
- **Suspend saves** (SYNC-DESIGN §2.11): nothing holds `BlockSuspendSaves` or re-applies
  host state after a suspend load. The `-UserDir` isolation in the test rig protects the
  saves, but a player's normal launch shares `%LOCALAPPDATA%`.
- **Moving world objects** (elevators, platforms, `Activatable` movers): each machine
  runs its own, so they can be at different positions. A player riding one is then shown
  floating on the other machine.
- **Loot pickups.** Each machine rolls its own drops (SYNC-DESIGN §2.5). A drop picked
  up on one machine stays on the floor on the other, and quest items can be doubled.
- **Shop, journal/bestiary, XP, weapon levels, grimoire ink.** These are per-player by
  nature. They only become a problem if the owner wants shared progress.
- **Peer vitals.** The partner's HP and death are not shown (no HUD element). A death
  shows only as the body's fall.
- **Cutscenes / sequencer.** They play only on the machine that triggers them.

## 4. Suggested order

1. **G1** (peer flag intents). The write path is proven and the change is small.
2. **Live-prove the three new systems.** Use two instances in `100_GR` and run the
   SYNC-DESIGN §2.6/§2.7/§2.8 acceptance tests, with the read-back log lines named in
   §2 above.
3. **G4/G5** (enemies targeting the remote player). Without this, co-op combat is one
   player fighting while the other watches.
4. **Enemy attack montages and projectiles** (§2). They are cheap once G4 decides who
   is attacked.
5. **G3 and G2.** They are blocked on write APIs and need oracle work first.
6. **G6** (coordinated travel).
