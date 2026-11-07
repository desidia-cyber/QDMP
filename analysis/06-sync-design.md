# 06 — The sync surface: what must be replicated, and how

Everything here is derived from evidence in `05-primary-evidence.md` plus the pak and
binary name surfaces. Where a claim is inference rather than measurement it is marked
`[I]`.

---

## 0. The three facts that set the whole design

1. **Progression is a small, named, flat set of containers, not opaque game logic.**
   `World0.sav` (`FS_SaveSlot_C`) holds `SavableActorStates`, `EnemySaveStates`,
   `NamedSaveData`, plus inventory/equipment/notes/checkpoints. All of them are
   readable and parseable offline today.
2. **Entities have stable authored identity.** `EnemySaveStates` keys are 16-byte GUIDs,
   414 of them in a save that is 11 minutes into the game. `SavableActorStates` is keyed
   the same way. So "which enemy is this" is answered by the game, not by us.
   *This is the single biggest difference from Dolls Nest*, where entity identity had to
   be inferred from spawn lineage and actor labels.
3. **The game emits change notifications.** `FS_NamedSaveDataChange`,
   `FS_InventoryChangeEvent`, `FS_GameSaveEvent`, `FS_AreaChangeEvent` are delegate
   types, and `ItemChangedEvent` is a live `MulticastInlineDelegateProperty` on the save
   object (observed in the parsed save). The mod can be event-driven for the discrete
   half instead of polling and diffing.

Together these mean the mod does **not** need to reverse-engineer a quest system. It
needs to (a) keep three containers equal, (b) drive live actors, and (c) relay dialogue.

---

## 1. Authority model

Copy DollsMP's proven rule verbatim; it is the reason that mod converges:

> **Every connected player's world follows the HOST's world. Never the other way round.
> A peer's action is a REQUEST the host applies to its own world; the peer then receives
> the result back like everybody else.**

Concretely:

| Layer | Owner | Mechanism |
|---|---|---|
| Session facts (who is host, session id, join order) | relay | DollsMP `Session` (first to join = host) |
| World facts (`NamedSaveData`, `SavableActorStates`, `EnemySaveStates`, inventory, checkpoints) | **host** | host repeats absolute state; peer sends intents |
| Player transform / vitals / aim | **each player owns itself** | each peer publishes its own; nobody writes another's pawn |
| Enemy transform / animation / HP | **host** | host simulates; peer's copies are driven with AI disabled |
| Dialogue | **host arbitrates, one speaker at a time** | lock published as state |
| Loot rolls | **per-player by default** | see §4.4 |

---

## 2. The persistent half — exactly three containers plus four lists

`World0.sav` properties, split by who they belong to:

### 2.1 World-shared, must be identical on both machines

| Save property | Type | Entries in the sample save | Sync treatment |
|---|---|---|---|
| `NamedSaveData` | `Map<Name,int32>` | 11 | **flag bank** — monotonic, host-wins, never lowered |
| `SavableActorStates` | `Map<FGuid,int32>` | 7 | one-shot interactable state — set semantics |
| `EnemySaveStates` | `Map<FGuid,{bValid,RespawnTime,RemainingHP}>` | **414** | enemy persistence — host-wins |
| `SavePointsVisited` | `Array<Object>` | 61 | fast-travel unlocks — set union |
| `LastTouchedCheckpoint` / `SavePointID` | Object | 1 | current anchor |
| `CollectLevel`/`CollectedNotes` | `Array<Object>` | 46 | lore notes — set union |
| `NPCsInJournal` | `Set<Object>` | — | bestiary/journal — set union |
| `SaveLevel`, `SavedArea` | Name/Object | 1 | **which world we are in — the join handshake key** |
| `CurrentPlayerHealth` | float | — | saved vitals (see §2.3 caveat) |
| `ActiveSuspendSlot` | int | 1 | suspend bookkeeping |

### 2.2 World-shared, but richer than an int

| Save property | Type | Entries | Note |
|---|---|---|---|
| `InventoryContents` | `Map<Object,int32>` | 1 | item object path → count |
| `Equipment` | `Map<Object,int32>` | 1 | |
| `EquipmentSlots` | `Map<int32,Object>` | 0 | |
| `CurrentlySelectedWeapon` | Object | — | |
| `WeaponCharges` | `Map<Object,float>` | 106 | per-weapon charge state |
| `DaggerCharges` | `Map<Object,float>` | 26 | |
| `PlayingMusicTrack` | Object | — | cosmetic; do **not** fight over it |
| `PlayTime*`, `LastSaveTime`, `HasEverSaved`, `SaveVersion` | scalars | — | local; never sync |

### 2.3 Per-player, must NOT be forced equal

- **`CurrentPlayerHealth`** is in the shared world save, which is an artifact of
  single-player design. In co-op each player has their own vitals; the save field is
  "the player" — so the host must not overwrite the peer's live health from it, and the
  two must not both write it at save time. Treat live HP as per-player; treat the saved
  field as host-of-record at save.
- **`SaveLocation` / `SavePointID`** — "where a player respawns" is per-player in DollsMP's
  corrected model (a respawn is a position; a rest is a world event). Keep the concept.

### 2.4 The sample save proves the model works

`NamedSaveData` in the operator's own 11-minute save:

```
Arjaot_State=1  RustySwordKnightState=1  RustySwordKnightGaveSword=1
BrokeBarricade=1  CheckpointUnlockCount=1  TotalAchievementWeapons=1
TotalAchievementTreasures=0  CompletedChallenges=0  IntroMusicState=0
FullEnergyCellsCount=0  TiniScytheHammerAP=0
```

Every one of these is a *story fact* or a *counter*. That is the entire progression
payload for that slice of the game, and it is 11 integers. The flag bank is cheap.

**Design consequence: model the whole thing as ONE delta entity of `Name|u32` pairs,
not one entity per flag.** DollsMP's delta layer costs ~17 B for a one-bit change, so
sharding flags is what makes it expensive; a single flag bank re-sent on change is
correct and tiny.

---

## 3. The live half — what streams every tick

| State | Rate | Channel | Size estimate |
|---|---|---|---|
| Local player transform (pos, rot, velocity) | 30–60 Hz | R1 state | ~40 B incl. header |
| Local player action/animation state (attack id, montage, movement mode) | on change + 10 Hz | R2 event + R1 | ~12 B |
| Local player vitals (hp, stamina, charges) | on change | R1 | ~10 B |
| Enemy transforms (N nearest) | 10–20 Hz | R1 | ~16 B/entity |
| Enemy animation state | on change | R2 | small |
| Enemy HP / death | on change | R2 (idempotent) | small |
| Interactable state deltas | on change | R3 world | small |
| Dialogue lock + line | on change | R3 | ~200 B |
| Level transition | event | R0 control | small |

**Bandwidth.** DollsMP measured 60 Hz at <10 KB/s for two players. Queens Domain has
more enemies visible at once (414 persisted in a single save), so the enemy stream is
the new cost. Budget: 20 Hz × 24 nearest entities × 16 B ≈ 7.7 KB/s, plus ~3 KB/s
player — comfortably inside a 10–20 KB/s budget on a LAN, and fine over Steam P2P.

---

## 4. Per-system design

### 4.1 Player movement and the visible body

- **Local player**: first-person, unchanged. Publish transform + aim + action state.
- **Remote player**: spawn `Player_FullChar_2026` (it already ships, with skeleton,
  physics asset, face/hair/armour materials and blood masks), drive it with snapshot
  interpolation.
- The game already has `bForceFirstPerson` and `bOnlyOwnerSee`/`bOwnerNoSee`, so the
  hide-your-own-body machinery exists; the remote body must be told to *not* be hidden.
  `[I]` — confirm the exact component flags on a live pawn.
- This is the single largest *schedule* win over DollsMP: its unfinished milestone was
  puppet rendering; here the art is already in the game.

### 4.2 Quest progression

The game has `ActorQuestState`, `ActorQuestStateManager`, `QuestStateInterface`,
`QuestForceUpdateVolume`, and 13 `Challenges/quest_*` blueprints. `[I]` on how the
manager persists its stage — but the save shows no obvious "quest stage" array, which
suggests the quest state is expressed *through* `NamedSaveData` + `SavableActorStates`
(the `*_State` keys like `Arjaot_State`, `RustySwordKnightState` are exactly this shape).

**Design:** do **not** invent a quest model. Mirror the containers:
1. Enumerate every `NamedSaveData` key the game ever writes (cheap: run the game, diff
   saves across a playthrough; the mod can also log `FS_NamedSaveDataChange`).
2. Treat each as a monotonic fact: host-wins, and **an event value must never be
   lowered** (DollsMP's `event_never_lower` rule — the host refuses a peer write that
   would regress progression).
3. When the peer's action should advance a quest, the peer sends an **intent**
   (`"I talked to X"`, `"I picked up Y"`); the host applies it through the game's own
   path and the resulting fact replicates back.

### 4.3 Quest items and key items

`Items/KeyItems/` has 503 assets: `AquaDoorKey_1..4`, `ArchivesKey`, `BookKey_*`
(Alchemy, BoneMachines, CookBook, DamsAndWeirs, …), `AcidVial`, and ability artifacts
(`Artifacts/StrangeMushroom` → DoubleJump, `Artifacts/Wind_Crystal`, fossils).

These are **progression gate keys**, so they are world-shared, not per-player: if one
player picks up `AquaDoorKey_2`, the door must open for both.

**Design:** key items are inventory entries → they ride `InventoryContents`, which is
already world-shared in the save. The only work is (a) replicate the inventory map, and
(b) make sure the *pickup actor* is destroyed/hidden on both machines exactly once
(`SavableActorStates` gives you the idempotent "already taken" record).

### 4.4 Loot

Two distinct things share the word "loot" in this game:

| Kind | Evidence | Treatment |
|---|---|---|
| Placed pickups (`ItemPickup_Base`, `CoreItemPickupActor`, chests/`ShopTreasureActor`) | placed in level, saved in `SavableActorStates` / inventory | **shared** — one player takes it, both lose it |
| Enemy drops (`ItemSystem/ItemDrop`, `SpawnItemDrop`, `FS_DropCacheKey`) | rolled at death | **per-player roll** (DollsMP's proven answer) unless the drop is a quest item |

DollsMP's hard-won rule: *"a host kill the peer never saw alive gives the peer nothing
(ghosted); shared loot not done"* — it chose per-player rolls. Queens Domain can do
better for placed loot because `SavableActorStates` gives a stable key; for enemy drops,
per-player is still the right default and avoids double-grant.

### 4.5 Dialogue

The game has `FS_DialogueSequence`, `UFS_DialogueData`, `UFS_DialogueAction`,
`EFS_DialogueConditionType`/`Behaviour` (dialogue is *conditional*), and per-character
`*_Dialogue` assets, plus `NPCs/QuestRewardEntry`, `NPC_GiveNextReward`.

The failure DollsMP hit is the one to design against: **both machines run the same talk
Blueprint and a race decides the choice.** Its fix is the reusable part:

1. **One player per NPC, published as *state* with a sequence number** — a lost blob
   costs a beat, never the session.
2. **First answer decides for both**, and the chosen branch id replicates.
3. Subtitles/voice ride a **one-way cosmetic lane** (fire-and-forget).
4. On death, **end the dialogue with the game's own end-talk call** — DollsMP found a
   dialogue left open at death produces an invisible unanswered choice.

**Answered:** dialogue does **not** pause or time-dilate the world — the game gates
input and freezes enemies individually. The *menus* do pause (live: Status/Inventory,
Pause, Photo, Checkpoint all freeze `UWorld::TimeSeconds`), so the mod's time-sync rule
is "suppress the menu pause", not "hold the peer during dialogue". See
`13b-pause-surface.md`, `13c-pause-surface.md`, `16-live-pass.md` §1.

### 4.6 Enemies

This is where Queens Domain differs from DollsMP most favourably.

- Enemies are placed via **`Enemies/EnemySpawner`** actors and per-enemy
  `<X>_Spawner` blueprints (`Boner_Standard_Fighter_Spawner`, `Boss`/`Miniboss`
  variants, `BloodPool_EnemySpawner`, …). So identity = *spawner GUID + spawn index*.
- The game persists per-enemy state keyed by **GUID**: `FS_EnemySaveState
  { bValid, RespawnTime, RemainingHP }` — 414 records in an 11-minute save.
- `FS_AwarenessCone`, `AggroRadiusFunction`, `FS_EnemyAttackConfig`,
  `EFS_AttackAggroNoise` describe perception/aggro; `dmp_director.h`'s "which player does
  this enemy fight" problem exists here and has a proven solution to port.

**Design:** host simulates AI; the peer's copies are driven (transform, animation,
HP, death) with their `AFS_AIController` disabled. On death, the host writes
`RemainingHP`/`bValid` so both machines agree, and the drop roll is taken per-player.
Grouping is by GUID, so a lost spawn message self-heals on the next absolute state send.

### 4.7 Interactables, world flags, one-shots

The game gives us a ready-made generic mechanism:

```
ActivatableGroup            - group of things that activate together
NotifyActivatables          - activation fan-out
ActivatableNamedSaveDataSetter  - writes a NamedSaveData key when activated
NamedSaveDataSetterVolume   - volume that writes a key on entry
WorldEventReceiver          - receives world events
QuestForceUpdateVolume      - forces a quest update
SuspendSaveBlockingVolume   - blocks saving in an area
```

So a door that opens once = a `NamedSaveData` key, or a `SavableActorStates` int on the
actor's GUID. **One-shot correctness is a set-insert on an id**, and the host commits it.

The 34-case failure analysis in DollsMP's `docs/sync/09` applies unchanged; do not
re-derive it — port the table and the three mitigations
(state-not-events, monotonic keys, idempotent payloads).

### 4.8 Level streaming and the join handshake

The world is **one persistent level** (`Ruins_Level_00` — which is exactly what
`SaveLevel` contains) with ~40 streamed area sublevels, driven by trigger volumes:
`AFS_LevelManager`, `AFS_LevelManagerVolume`, `LevelListStack`, `AddLevelListToStack`,
`ClearTriggerLevelLists`, `bLevelListIsFromTriggers`, `FS_LevelLoadSuspendState`, and the
delegates `LevelManagerUpdate` / `LevelManagerDoneLoading`.

Design rules:

1. **Stream the union of both players' surroundings.** A level list is assembled from
   whichever player entered a trigger volume; it must not be cleared because the *other*
   player walked out.
2. **Do not unload a level the other player is standing in.** This is the streaming
   analogue of "don't despawn what your partner can see".
3. **`LevelManagerDoneLoading` is the readiness event.** Use it instead of a timer — this
   is precisely the mistake DollsMP documented ("gate on events, not timers").
4. **Compare `SaveLevel` (and `SavedArea`) before allowing co-op**, exactly as DollsMP
   compares the `_P` level path.
5. **A hard transition** (intro, cutscene map, `SipehaWorld`, `B33sment`) is a
   host-driven, acknowledged event: host announces, both arrive, both confirm, then play
   resumes.

### 4.9 Checkpoints, death, respawn

- Save points are per-area actors (`Areas/*/<X>_Save`), ≈30 of them, and
  `CheckpointUnlockCount` is a `NamedSaveData` counter.
- **A respawn is a position; a rest is a world event.** Do not route a respawn through
  the checkpoint rest path or the survivor's world resets mid-fight. (DollsMP paid for
  this exact mistake; its rev-12 correction is the reference.)
- Enemy respawn is driven by `RespawnTime` in `FS_EnemySaveState` — so "resting revives
  enemies" is a data-driven event both machines can apply from the same values.

### 4.10 Suspend / quit

`ActiveSuspendSlot` + `SuspendSaveBlockingVolume` mean the game has a suspend-resume
concept. In co-op, a suspend must **detach** the peer rather than silently freeze the
shared world; treat it as a session event, not a local convenience.

---

## 5. Hard problems, with a stated approach

| # | Problem | Approach |
|---|---|---|
| 1 | Both machines write their own save → double-applied progression | **One authority.** Only the host's world is canonical; the peer's save is a mirror. Additionally give each instance its own save directory (`-UserDir=`, `-saveddirsuffix=`) with DollsMP's save guard so one player's progress never overwrites the other's. |
| 2 | Enemy AI runs twice → double damage, double loot | Host-simulates; peer copies are **driven, AI disabled**. Damage is a running total, not an event. |
| 3 | Loot double-grant | Per-player rolls for drops; placed pickups keyed by `SavableActorStates` GUID so a second take is a no-op. |
| 4 | Dialogue race over a choice | Published lock with a sequence number; first answer decides both. |
| 5 | Quest advanced twice (peer + host both trigger) | Fact is a set/counter with monotonic host write; peer actions are intents. |
| 6 | Both players must be in the same level | Join handshake on `SaveLevel`; transitions are acknowledged events. |
| 7 | A player dies during a boss | Held/dead state, not a lose — the survivor must not have the boss reset. DollsMP `dmp_progress_engine.h` has the model (hold the loss while the partner lives; pay the reward to both). |
| 8 | Random rolls diverge | Shared seeded RNG (`dollsmp-core::rng`, PCG-XSH-RR with a golden vector) for anything that must agree; per-player rolls where divergence is desired. |
| 9 | Interpolation across a teleport/level change | Snap, don't interpolate (`TELEPORT_THRESHOLD`, DollsMP `dmp_motion.h`). |
| 10 | Two instances on one PC for testing | `-UserDir=` and `saveddirsuffix` exist in this binary (verified); junction `Content`, hard-link the exe. |

---

## 6. What we still do not know (and the probe that answers it)

| # | Question | Status |
|---|---|---|
| Q1 | Does `ActorQuestStateManager` persist stage anywhere beyond `NamedSaveData`? | open — play 10 minutes, diff `World0.sav`, and log `FS_NamedSaveDataChange` fires |
| Q2 | Are `SavableActorStates` GUIDs editor-authored and stable? | ✅ **ANSWERED — yes.** All 7 found cooked into level packages. |
| Q3 | Is the enemy's GUID stable across sessions / respawns? | ✅ **stable across sessions** — all 414 found in the pak. *Respawn* reuse still open. |
| Q4 | Does a dialogue pause the game? | **open** — live observation; check for `SetPause`/time dilation |
| Q5 | Is the game single-instance (mutex)? | open — `CreateMutexW` is imported; test two launches |
| Q6 | Which component hides the player's own body? | open — dump the pawn's components and `bOwnerNoSee`/`bOnlyOwnerSee` at runtime |
| Q7 | Does `AFS_PlayerCharacter` replicate anything already? | open — inspect the class CDO's `bReplicates` |
| Q8 | Can the peer body be spawned by the game's own path? | open — look for a `UFS_StationarySkeletalMeshActor` / `AFS_Actor` spawn helper |

Method and evidence for the two answered questions: `07-verification.md`.

### 6.1 What the answered questions change

- **Q2/Q3 answered** means the entity key is simply the authored `FGuid`. No spawn-lineage
  inference, no actor-label matching, no hashing. A lost spawn message self-heals on the
  next absolute state send, because both sides agree on the ID without negotiating.
- **`EnemySaveStates` is a registry with a 3-scalar record** (`bValid`, `RespawnTime`,
  `RemainingHP`), fixed 157-byte stride, all at defaults in an early save. Enemy
  *persistence* sync is therefore tiny; the expensive part is live transform/animation,
  not persistence.
- **Steam is out** as the transport (`07-verification.md` Check 2). The UDP relay is the
  design, not a fallback.
