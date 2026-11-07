# 23 — Quest, progression, trigger and quest-item sync map

**Short answer to "are there any other gaps left?":** the analysis register is still
**50 / 50 CLOSED**, and the four residual content items were closed in `22-close-out.md`.
There is **no unmeasured substrate** left that blocks a co-op build.

What did *not* exist is the thing this file now provides: **one consolidated map of the
state that must be synced** (quests, progression flags, triggers, quest items) and their
lanes. The remaining true gaps are *content-mapping* gaps, not analysis gaps, and they are
listed in §9 so they are not mistaken for solved work.

---

## 1. The whole progression model in one picture

QD has **no C++ quest class and no DataTable**. Progression is six small containers on one
plaintext `GVAS` save object plus per-actor Blueprint memory:

```
                        ┌─────────────────────────────────────────────┐
                        │ FS_SaveSlot_C  (the only campaign state)    │
                        │                                             │
  NamedSaveData ───────►│ Map<FName,int32>   flags, counters, stages  │
  SavableActorStates ──►│ Map<FGuid,SavableActorState> one-shot bits  │
  InventoryContents ───►│ array of {soft item path, int count}        │
  Equipment/Slots ─────►│ arrays of item refs / ints                  │
  CompletedChallenges ─►│ set of challenge ids                        │
  EnemySaveStates ─────►│ Map<FGuid,{bValid,RespawnTime,RemainingHP}> │
                        └─────────────────────────────────────────────┘
                                   ▲
                                   │ per-actor Blueprint memory
                 ActorQuestStateManager_C (SavableActor_C, singleton)
                   States / DesiredState / InternalState / StateSaveDataname
                   "Quest State NSD" ── ActivatableNamedSaveDataSetter
```

Everything quest-shaped is a *value* in one of those containers. The game itself drives
quests from those values; the mod should mirror the values and never re-implement a quest
(`04-sync-surface.md` §6.3). One generic hook covers a quest the mod has never heard of.

---

## 2. The quest system — what is placed where

`ActorQuestStateManager_C` is **not placed in any map**; the game creates it once per world
(its class lives at `Blueprints/ActorQuestStateManager`, 180 names, parent `SavableActor_C`).
The quest *presentation/gating* is placed per area:

| map | `ActorQuestState` | `QuestForceUpdateVolume` | `ActivatableNamedSaveDataSetter` | `NamedSaveDataSetterVolume` |
|---|---|---|---|---|
| `100_GR` | 6 | — | 1 | 1 |
| `200_AQ` | 6 | 1 | — | — |
| `300_LIB` | 6 | 1 | — | — |
| `420_MAUS_mid` | 3 | — | 2 | — |
| `450_MAUS_darkmaze` | **12** | — | 1 | — |
| `500_AUT` | 6 | — | — | — |
| `800_FinalZone` | 3 | — | — | — |
| `110_GR_dungeon` | — | — | 1 | — |
| `210_AQ_underground` | — | — | 1 | — |
| `320_LIB_basement` | — | — | 1 | — |
| `700_SZ_GR` | — | — | 1 | — |
| `770_SZ_Swamp` | — | — | 1 | 2 |
| `780_SZ_Depths` | — | — | 1 | — |
| **total** | **42** | **2** | **10** | **3** |

`ActorQuestState_C` (parent `Actor`) does **not store state** — it shows/hides actors
(`ActorsToShow`, `ChildNodes`, `CurActor`, `FadeIn/FadeOut`, `Disable`) in response to a
state read through `QuestStateInterface_C`:
`SetQuestStateActive` / `SetQuestStateInactive` / `SetActorsState` / `GetActors`.

`ActorQuestStateManager_C` is the state machine:
`ForceSyncUpdate`, `ForceRefresh`, `RefreshStates`, `UpdateDesiredState`,
`Restore State From Save Data`, and the change signal
`OnQuestStateUpdated` (via `WorldEventReceiver`). Its state vocabulary is *not* a literal
asset: `States` / `DesiredState` / `InternalState` are populated per world, and the manager
writes `StateSaveDataname` into `NamedSaveData` (`"Quest State NSD"`).

---

## 3. The 13 quests and the challenge layer

The 13 `Blueprints/Challenges/quest_*` assets are **achievement icons (`Texture2D`), not
quest definitions** — the same correction D9 made for `boss_*`. The completion layer is
`Blueprint/Challenges/FS_ChallengeData` (export class `FS_ChallengeData`), whose
`ChallengeDefinitions` property is a **`MapProperty` (Name → struct)** of 33,988 bytes.

There are **14 `cha_Quest_*` ids** and **13 quest icon assets**, and they are *not* a 1:1
pairing (icons include `ducks/egg/necro/trident`, ids include `Fish/Meat/Skulls/Vaults`).
The `ChallengeDefinitions` map **is now decoded** (`analysis/re/_challenge_map.py`): 63
entries, each with a `NamedSaveData` condition map of `{key: threshold}`.

### The 14 quests — exact completion conditions

| challenge id | completion condition (NSD key → threshold) |
|---|---|
| `cha_Quest_Raider` | `RaiderNeckacesTurnedIn` ≥ 4 |
| `cha_Quest_Fish` | `FishWifeTotalEggs` ≥ 50 |
| `cha_Quest_Lib` | `LibrarianBestiaryCompletionPercent` = 100 |
| `cha_Quest_Meat` | `NecroWall00Destroyed` … `NecroWall04Destroyed` = 1 (all five) |
| `cha_Quest_Books` | `LibBookPuzzleComplete` = 1 **and** `ROSBookPuzzleComplete` = 1 |
| `cha_Quest_Vaults` | `SR_GMS_Acquired`, `SR_GreenKey_Acquired`, `SR_RustedLeafBlade_Acquired`, `SR_Key_Blue_Acquired`, `SR_Rusted_Greatsword_Acquired` = 1 each |
| `cha_Quest_Chef` | `MealTicketsCollected` ≥ 60 |
| `cha_Quest_Science` | `ScientistDataGiven` ≥ 10 |
| `cha_Quest_Shop_Spend` | `ShopAmountSpent` ≥ 1000 |
| `cha_Quest_PG_Puzzle` | `StoneGardenPuzzleFinished` = 1 |
| `cha_Quest_Shop_Unlocks` | `ShopUnlockFinished` = 1 and `ShopkeepUnlock1..4` = 1 each |
| `cha_Quest_Grimoire` | `HasEverInscribedGrimoire` = 1 |
| `cha_Quest_Mushroom` | **inventory** `StrangeMushroom` = 1 (decoded in `24` §1b) |
| `cha_Quest_Skulls` | **no condition entry** — manual/scripted; `SecretSkullDoor_TurnInAction` grants `SkullKeyRelic_Data` (`24` §2) |

### The 8 bosses, the meta-challenge and the counters

| challenge id | condition |
|---|---|
| `cha_Bosses_Whale` | `WhaleKingIsDead` = 1 |
| `cha_Bosses_Necro` | `NecronimiconDefeated` = 1 |
| `cha_Bosses_Demons` | `DemonBossesKilled` = 1 |
| `cha_Bosses_BridgeLord` | `BridgeLordDead` = 1 |
| `cha_Bosses_RoSDragon` | `ROSDragonDead` = 1 |
| `cha_Bosses_DragonLord` | `DragonLordDead` = 1 |
| `cha_Bosses_GiantFish` | `GiantSwampFish_IsDead` = 1 |
| `cha_Bosses_RedLords` | `RedLordsDead` = 1 |
| `cha_All_Complete` | `CompletedChallenges` ≥ 52 |
| `cha_Challenge_AllTeleporters` | `TeleporterUnlockCount` = 10 **and** `CheckpointUnlockCount` = 32 |
| `cha_Challenge_AllChests` | `TotalChestsOpened` = 108 |
| `cha_Challenge_AllSecretDoors` | `TotalSecretDoorsOpened` = 89 |
| `cha_Item_UpgradeAllWeps` | `TotalWeaponsUpgraded` = 63 |
| `cha_Item_GotAllWeps` | `TotalAchievementWeapons` = 77 |
| `cha_Item_GotAllThrowables` | `TotalAchievementThrowables` = 22 (item tag) |
| `cha_Item_GotAllAcc` | `TotalAchievementAccessories` = 33 (item tag) |
| `cha_Item_GotAllTreasures` | `TotalAchievementTreasures` = 93 |
| `cha_Item_GotAllCells` | `FullEnergyCellsCount` = 4 |
| `cha_Item_*` (FlyingSword, SecretSword, SpecSword, UltraGem, AllDashGems, GotAllLenses, EnergyWep) | inventory/item-tag conditions — **now decoded** in `24` §1b |
| `cha_Area_*` (10) | no condition entry (manual/area script) |
| `cha_Endings` (4 entries) | no condition entry; the ending is driven by `EndingSeen` + `ending1..4` |

> `cha_Quest_Meat` is the Necro-wall quest (all five `NecroWall0*Destroyed`), and
> `cha_Quest_Books` requires **both** book puzzles (`Lib` + `ROS`). This is the kind of
> cross-quest dependency the generic flag mirror handles without knowing about it.

Two condition columns are *not* fully decoded: `InventoryCondition` and `ItemTagCondition`
are per-entry maps, but their tag headers differ from `NSDCondition` (the raw decode yields
offset-shifted keys for entries 11, 22, 25–29, 32–33, 36–37). The NSD column is exact; the
inventory/tag column is the remaining decode (§9 item 1b).

The rest of the challenge set is closed by the same mechanisms:
10 `cha_Area_*`, 8 `cha_Bosses_*` (D9), `cha_Challenge_All{Chests,SecretDoors,Teleporters}`,
`cha_Endings`, and 14 `cha_Item_*`.

---

## 4. Quest items — the `Items/KeyItems/` set

**503 key-item assets in 26 families.** They are ordinary inventory entries, so their sync
lane is the **shared inventory**, not a separate quest lane:

| family | assets | family | assets |
|---|---:|---|---:|
| `ScientistUpgrades` | 53 | `PrisonKeys` | 12 |
| `Memento_Content` | 52 | `Knights_Key` | 11 |
| `TridentKeys` | 32 | `MerchantKeys` | 11 |
| `ShopProgressionKey` | 29 | `Ros_Orb` | 9 |
| `CastleDoorKeys` | 24 | `Mealticket` | 9 |
| `AquaDoorKey` | 19 | `KnifeBelt` | 7 |
| `FishMaidenTiaras` | 18 | `SwordShrineKey` | 6 |
| `AcidVial` | 16 | `ArchivesKey` | 6 |
| `ShopPurchasedKey` | 16 | `SecretRelicKey` | 6 |
| `Egg` | 16 | `JosamiKey_Red` | 6 |
| `ForbiddenDoorKey` | 16 | `RaidersKey` | 6 |
| `JigekiKeys` | 15 | `KingSword_Remnant` | 3 |
| | | `RaiderNecklaceKey` | 3 |
| | | `BookKeys` | 2 |

26 folders, 503 `.uasset` files (the counts are the `.uasset` entries in `entries.tsv`).

Rules already established: a key granted twice is harmless, but **a key the peer lacks locks
the *peer* out of a region alone** (`04-sync-surface.md` #14). So key items are shared and
granted by the host; pickup idempotence is the `SavableActorStates` GUID of the pickup.

Shop progression is a separate pair of key families (`ShopProgressionKey` 29,
`ShopPurchasedKey` 16) and the `ShopAmountSpent` / `ShopkeepUnlock1/2` NSD fields — these
gate stock, so they are host-owned even though the *purchase* is a player action.

---

## 5. Triggers and world events — the write surface

Placed actor census over all 51 maps (export tables; one export per placed actor):

| class | placed | maps | writes / role |
|---|---:|---:|---|
| `WorldEventReceiver` | **223** | 27 | the change bus (`OnWorldRefresh`, `OnQuestStateUpdated`, inventory/NSD events) |
| `ActivationTrigger` | 130 | 21 | overlap activation of a targeted actor/group |
| `Chest` | 114 | 24 | one-shot container open → `SavableActorStates` + inventory |
| `ActivatableGroup` | 95 | 18 | group on/off → `NamedSaveData` via a setter |
| `LevelManagerTrigger` | 49 | 2 | streaming A/B side (C10) |
| `Teleporter` (all classes) | 43 | 20 | fast travel unlock (`UnlockedTeleporters`, `cha_Challenge_AllTeleporters`) |
| `ActorQuestState` | 42 | 7 | show/hide quest actors (reads state) |
| `Checkpoint` | 34 | 18 | save point + `CheckpointUnlockCount` |
| `ActivatableNamedSaveDataSetter` | 10 | 9 | writes `NSDs` on activate/deactivate |
| `NamedSaveDataSetterVolume` | 3 | 2 | writes `NSDs` on volume enter/exit |
| `QuestForceUpdateVolume` | 2 | 2 | forces a quest re-evaluation |
| `BreakableBarricade` | 1 | 1 | `BrokeBarricade` one-shot (`SavableActorStates`) |

`NotifyActivatables` and `Door_Base` are **not placed actors**: the former is a helper
function on `ActivatableGroup`, the latter is a class with subclasses (`Lib_BossDoor`,
`DesBoss_Gate`, `GraveyardBoss_Gate`, …).

### The write path, in order

```
player overlaps ActivationTrigger / volume
  -> ActivatableGroup.Activate / NamedSaveDataSetterVolume.Activate
     -> ActivatableNamedSaveDataSetter.SaveNamedData_Advanced(NSDs[])
        -> SaveFunctions_C.SetNamedSaveData(worldctx, FName, int32)
           -> FS_SaveSlot.NamedSaveData[key] = value
              -> FS_NamedSaveDataChange delegate
                 -> WorldEventReceiver.OnQuestStateUpdated
                    -> ActorQuestStateManager.RefreshStates
                       -> ActorQuestState.SetQuestStateActive/Inactive
```

This is why the mod can ignore individual quests: hook the two containers
(`NamedSaveData`, `SavableActorStates`) and the change delegates, and the game's own quest
manager follows.

---

## 6. What must be synced, with authority and rate

| # | datum | container | authority | direction | rate |
|---|---|---|---|---|---|
| 1 | quest/progression flags | `NamedSaveData Map<FName,int32>` | **host only** | host→peer, absolute | on change (≤0.05 KB/s) |
| 2 | one-shot world state | `SavableActorStates Map<FGuid,int>` | **host only**, first-wins | host→peer | on change |
| 3 | quest/key items | `InventoryContents` | **host**, shared | host→peer; peer pickup is an intent | on change |
| 4 | equipment / weapons / upgrades | `Equipment`, `EquipmentSlots`, `WeaponLevels`, `WeaponXP`, `MaxProgressionLevel` | host | host→peer | on change |
| 5 | challenges/achievements | `CompletedChallenges` | **per-save** (design choice; see §9 item 4) | host→peer | on completion |
| 6 | quest state machine | `ActorQuestStateManager` `States`/`DesiredState`/`InternalState` | host | host→peer, or let the peer recompute from #1 | on change |
| 7 | enemy persistence | `EnemySaveStates` | host | host→peer | on death/respawn (D4/D5) |
| 8 | shop stock/unlocks | `ShopPurchases`, `ShopTeleporterDestinations`, `ShopAmountSpent` | host | host→peer | on purchase |
| 9 | fast travel / save points | `UnlockedTeleporters`, `SavePointsVisited`, `LastTouchedCheckpoint` | host | host→peer; the peer may *touch* a checkpoint as an intent | on change |
| 10 | journal / bestiary | `NPCsInJournal`, `CollectedNotes`, `KilledEnemyCounts` | host | host→peer | on change |
| 11 | endings | `EndingSeen` + `ending1..4` challenge ids | host | host→peer | once |

The two hard rules from the existing design still hold:
**one writer (`NamedSaveData`) is the host**, and **a value never lowers** (a peer write that
would regress progression is refused). Peer actions are intents the host validates and echoes.

---

## 7. The `NamedSaveData` key universe (the thing to keep equal)

The complete key surface is already enumerated in `answers.md` Q2 and `answers-2.md` C6/D9.
For the quest map, the progression-relevant subset is:

```
global counters : CheckpointUnlockCount  FullEnergyCellsCount  TotalAchievementTreasures
                  TotalAchievementWeapons  TotalAchievementThrowables
                  TotalAchievementAccessories  TotalWeaponsUpgraded
                  CompletedChallenges  EndingSeen  TeleporterUnlockCount
                  TotalChestsOpened  TotalSecretDoorsOpened
boss flags      : BridgeLordDead  DragonLordDead  RedLordsDead  ROSDragonDead
                  GiantSwampFish_IsDead  NecronimiconDefeated  DemonBossesKilled
                  WhaleKingIsDead  NecroWall00Destroyed .. NecroWall04Destroyed
quest facts     : FishWifeTotalEggs  HasEverInscribedGrimoire  LibBookPuzzleComplete
                  ROSBookPuzzleComplete  LibrarianBestiaryCompletionPercent
                  MealTicketsCollected  RaiderNeckacesTurnedIn  ScientistDataGiven
                  ShopAmountSpent  ShopUnlockFinished  ShopkeepUnlock1..4
                  StoneGardenPuzzleFinished
                  SR_GMS_Acquired  SR_GreenKey_Acquired  SR_RustedLeafBlade_Acquired
                  SR_Key_Blue_Acquired  SR_Rusted_Greatsword_Acquired
                  FlyingSwordPickedUp  Knight_00_CampState  AQR_OpenedDarkCave
                  GotKnightKey  PlayerReachedKitchen
                  Ending1Complete  Ending2Complete  Ending3Complete  Ending4Complete
                  EndingWakeOnBeach
per-actor       : <instance><Property>  (Arjaot_State, RustySwordKnightState,
                  RustySwordKnightGaveSword, BrokeBarricade, TiniScytheHammerAP, …)
```

Keys are **composed**, not literals: `<instance or actor name>` + `<property/state name>`.
The mod must treat the map as an opaque `FName→int32` bank and mirror it wholesale; that is
what makes it content-agnostic across all 13 quests, the 4 endings and future content.

---

## 8. How this changes (or does not change) the design

* **No new lane is needed.** The existing design already mirrors the six containers; the
  quest map confirms there is no hidden seventh store.
* **The trigger graph is not something the mod must replicate.** The game's own
  `ActivationTrigger → ActivatableGroup → NamedSaveDataSetter → NamedSaveData` chain runs on
  each machine locally; the mod only has to make the *result* agree.
* **The one real risk is double-application on travel.** A level's `BeginPlay` re-runs and
  can re-trigger a volume; the host's absolute value must be applied after arrival, and the
  peer must not re-request a write it already got (`04-sync-surface.md` §6.3, §7).
* **Quest items are shared by construction.** 503 key assets, 26 families; the lane is the
  inventory, with `SavableActorStates` for pickup idempotence.
* **Shop progression keys are host-owned** even though buying is a player action: the
  `ShopProgressionKey`/`ShopPurchasedKey` families gate stock, and `ShopAmountSpent` is a
  single save-level counter.

---

## 9. Content-mapping status — CLOSED in `24-quest-closeout.md`

All five items below were closed in the follow-up pass; the text is kept as the work list
with the answer appended. See `24-quest-closeout.md` for the tables and repro scripts.

1. **`ChallengeDefinitions` entry pairing — CLOSED.** All 63 entries decoded (NSD map,
   inventory map, item tags, mode). Exact quest conditions in `24` §1.
2. **Per-quest trigger graph — CLOSED.** 10 setter actors with their `NSDs` keys; the 61
   dialogue assets' written keys and action refs; the 14 placed managers and 28 state
   actors. `24` §2.
3. **Quest-state vocabulary — CLOSED.** The 28 `ActorQuestState` names grouped per manager;
   the manager's `States` count equals its state actors (`24` §3).
4. **Ending gating + NPC reward/journal — CLOSED.** Four outcome flags
   (`Ending1/2` Queen, `Ending3/4` Sipeha) + `EndingSeen`/`EndingWakeOnBeach`; the reward
   action→item table and the 10 tutorial notes (`24` §4).
5. **Design decision — RECORDED.** Both players may talk to NPCs (per-player dialogue
   view/lock); progression stays host-authoritative (`24` §5).

The original work list follows for context.

1. **`ChallengeDefinitions` entry pairing — DONE for NSD.**
   `FS_ChallengeData.ChallengeDefinitions` is a `MapProperty` (Name → struct), 33,988 bytes.
   Each entry is a per-property-tagged stream (24-byte tag + 1 property-GUID byte; a
   `NameProperty` value follows at +25; an `NSDCondition` is a `Map<Name,int32>` with the
   count at +45 and `{FName, int32}` entries at 12-byte stride). `_challenge_map.py` decodes
   all **63** entries; the quest/boss/condition table in §3 is the result.
   **1b — remaining:** the `InventoryCondition` and `ItemTagCondition` columns use a
   different tag header; the raw decode is offset-shifted for ~10 entries
   (`quiz_Quest_Mushroom`, the `cha_Item_*` sword/gem/lens entries, `cha_Item_EnergyWep`).
   Method: reuse the same walker with the type-specific header for those two properties.
2. **Per-quest trigger graph.** Which of the 42 `ActorQuestState`, 10
   `ActivatableNamedSaveDataSetter`, 3 `NamedSaveDataSetterVolume`, 2
   `QuestForceUpdateVolume` and 95 `ActivatableGroup` actors belong to which of the 13
   quests. Method: read each placed actor's `States`/`NSDs` defaults from its map export
   (the same per-export property walk used for C10), and join on the NSD key.
3. **Quest-state vocabulary.** The manager's `States`/`DesiredState`/`InternalState` values
   are per-world, not literals in the asset. Method: read the placed `ActorQuestState`
   `States` arrays (42 actors, 7 maps) and the runtime manager (`ForceRefresh`, then dump).
4. **Per-player vs per-save policy decisions** (not unknowns): challenges/achievements,
   shop stock, loot rolls, and the four endings. These are design choices the owner must
   make; the existing recommendation is host-wins / shared, with per-player loot rolls only.
5. **Ending gating.** `EndingSeen` + `ending1..4` are known; the exact quest-completion set
   that unlocks each ending is not enumerated.
6. **NPC/journal mapping.** `NPCsInJournal`, `CollectedNotes`, the 61 `FS_DialogueData`
   assets (D2) and `NPC_GiveNextReward` / `QuestRewardEntry` are modelled, but the
   per-NPC reward table is not enumerated.

**Nothing in this list is an unknown substrate.** Each is a bounded content read over the
pak or the live object graph, with the same tools already used for C5/C6/C10/C11/D2/D9.

---

## 10. Bottom line

* **Analysis gaps: none.** 50/50 register items closed; the four residual content items
  closed in `22-close-out.md`.
* **Sync map: now exists** (this file) — six containers, **63 decoded challenge definitions**
  (14 quest ids / 13 icons), 503 key items in 26 families, **746 placed**
  quest/trigger/setter/chest/teleport/checkpoint actors, and the authority/rate for each
  datum.
* **Complete co-op still needs** the six content mines in §9 plus the implementation work
  (transport, session, puppet, save codec, packaging). Those are build tasks, not research
  blockers.

### Repro

| file | what |
|---|---|
| `analysis/re/_quest_map.py` | challenge-data tag inspection + placed trigger census |
| `analysis/re/_d4_multiguid_owner.py` | the per-export property/tag walker to reuse for §9.1–2 |
| `analysis/pak/answers.md` Q1/Q2 | quest state model + full NSD key surface |
| `analysis/pak/blueprint-surface.md` | `ActorQuestState`/`Manager`/`QuestStateInterface` names |
| `analysis/04-sync-surface.md` §6.3 | the generic flag-mirror design |
