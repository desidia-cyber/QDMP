# 04 — Sync surface: the complete game state a 2-player co-op mod must synchronise

**Game:** *Queens Domain* — UE **4.27.2**, shipping build
**Binary:** `QueensDomain\QueensDomain\Binaries\Win64\QueensDomain-Win64-Shipping.exe` (81,374,720 B)
**Content:** `QueensDomain-Win64-WindowsNoEditor.pak` (1,976,294,370 B, pak **v11**, 44,092 entries)
**Save root:** `%LOCALAPPDATA%\QueensDomain\Saved\SaveGames\`
**Analysis date:** 2026-11-06

Everything below is either **CONFIRMED** (measured from a real artefact in this session, with the
artefact named) or **CANDIDATE** (inferred). No name in this document is invented; every quoted
identifier came out of the shipping exe's string pool, the pak directory index, or a save file.

Tooling written for this report lives beside it:

| file | purpose |
|---|---|
| `gvas.py` | full UE4 SaveGame reader: header, custom versions, tagged property tree. |
| `test_gvas.py` | self-test — parses all **five** real files (`System0`, `World0`, `World0_backup`, `Challenges`, `Challenges_Backup`) to an exact byte fit (`stream_end == size - 4`). Currently passing on all five. |
| `gvas_probe.py` | header/entropy/zlib reconnaissance |
| `dump_save.py`, `d_save_full.py`, `d_enemies.py` | value dumps; `d_enemies.py` decodes all 414 enemy records exactly |
| `stringsx.py` | ASCII + UTF-16LE string extractor for the shipping exe |
| `classify.py`, `extract_surface.py` | keyword grouping of the 69,091 extracted identifiers |
| `pakindex.py`, `paknames.py` | pak footer/index interrogation + name taxonomy extraction |

---

## 1. Summary

1. **The game has been run and the save format is fully cracked.** Three save files exist, written
   today (2026-11-06 15:18–15:30). They are stock UE4 `GVAS` SaveGames with a **non-stock header**
   (an `FEngineVersion` block plus 51 custom-version GUIDs) and a length convention where **string
   lengths include the NUL**. `gvas.py` parses all three to an exact byte fit.
2. **The single most valuable artefact is `World0.sav`, and it is small: 79,823 bytes.** It contains
   the *entire* persistent world state as 30 named properties, including a **generic named-flag store**
   (`NamedSaveData`, `Map<FName,int>`), a **per-actor one-shot store** (`SavableActorStates`,
   `Map<Guid,int>`), the **full inventory/equipment/weapon-charge state**, and **414 enemy records**
   (`EnemySaveStates`, `Map<Guid,{bValid,RespawnTime,RemainingHP}>`). This is the quest-progression
   hook, and it is one map.
3. **Quests are not a subsystem.** The word "Quest" does not appear in the game's own class names.
   Progression is `NamedSaveData` flags + `UQD_Journal_Data` + per-actor state. That is *good news*:
   one generic hook covers the majority of progression sync, and the escape hatch is a data file.
4. **Enemy identity is already stable and already persisted.** Enemies carry a `Guid` key that the
   save uses, and the game has `SpawnerGUID`, `RespawnTime`, `RemainingHP` and `bValid` per record.
   Two machines can therefore agree on *which* enemy is which without inventing an ID scheme.
5. **The game is single-player with no replication of its own.** The only occurrence of
   `GetLifetimeReplicatedProps` in 81 MB is inside an engine console-help sentence, no game class
   carries RPCs, the only networking strings are stock engine boilerplate, and `steam_api64.dll` —
   listed in the manifest — is **not installed**. The mod must implement state replication itself.
6. **Enemy AI cannot be made deterministic.** The game uses a custom action system
   (`UFS_EnemyAction`, `EFS_EnemyActionResponse`, `EFS_EnemyActionCancelReason`) driven by randomised
   attack configuration and frame time, not a fixed-step simulation. The recommendation is
   **host-authoritative AI**, with peer-side enemies as animation-driven puppets.
7. **Dialogue and level transitions are data-driven and instrumented.** The engine-side hooks the mod
   needs already exist as named delegates: `LevelManagerUpdate`, `LevelManagerDoneLoading`,
   `EnemyDiedEvent`, `EnemyStaggeredEvent`, `DialogueActionEvent`, `DialogueJumpEvent`,
   `FS_NamedSaveDataChange`, `FS_GameSaveEvent`, `FS_InventoryChangeEvent`, `FS_AreaChangeEvent`.
   This is a hookable game, not a black box.

---

## 2. Save-data findings

### 2.1 Location — CONFIRMED

```
C:\Users\<user>\AppData\Local\QueensDomain\Saved\SaveGames\
    System0.sav            1,197 B   2026-11-06 15:18:46
    World0.sav            79,823 B   2026-11-06 15:30:14
    World0_backup.sav     79,823 B   2026-11-06 15:30:14
    Challenges             1,282 B   2026-11-06 15:30:13
    Challenges_Backup      1,282 B   2026-11-06 15:30:13
```

There is **no** `Saved\SaveGames` under `%LOCALAPPDATA%` for any variant name (`Queens Domain`,
`Queens_Domain`) and nothing alongside the game directory. `%LOCALAPPDATA%\QueensDomain\Saved\Config\WindowsNoEditor\`
also exists (16 ini files; `Engine.ini` contains only `[Core.System] Paths=`, `GameUserSettings.ini`
is stock 1920×1080 windowed). `Saved\Logs\` is **empty** — this build does not write logs by default.

**The game HAS been run.** Evidence, not inference: `HasEverSaved = true`, `PlayTime_Seconds = 658`,
`GamePlayTime_Seconds = 479`, `SaveLevel = "Ruins_Level_00"`, `SaveVersion = 6`, and an inventory
containing exactly one item. This is an early-game character, not a test stub.

Note the two **backup** files and the extension-less `Challenges` pair: the save system writes a
primary and a `.bak`-style copy on every save. Any mod that writes saves must respect this or it will
desynchronise the backup from the primary.

### 2.2 Container format — CONFIRMED, and it is NOT stock UE4

Measured layout, identical in all three files:

| off | field |
|---|---|
| 0x00 | `char[4]` `"GVAS"` |
| 0x04 | `int32` SaveGameFileVersion = **2** |
| 0x08 | `int32` PackageFileUE4Version = **522** |
| 0x0C | `uint16` EngineVersion.Major = 4 |
| 0x0E | `uint16` EngineVersion.Minor = 27 |
| 0x10 | `uint16` EngineVersion.Patch = 2 |
| 0x12 | `int32` Changelist = 0 |
| 0x16 | `int32` n, then `n` bytes — branch string **including the NUL** → `"++UE4+Release-4.27"` |
| — | `int32` CustomVersionFormat = **3** (Optimized) |
| — | `int32` CustomVersionCount = **51** |
| — | 51 × (`FGuid` 16 B, `int32` version) |
| — | `FString` SaveGame class path |
| — | tagged property stream, terminated by a `None` tag |
| EOF-4 | `uint32` 0 |

Two deviations from stock `UGameplayStatics::SaveGameToSlot`, both load-bearing:

* **An `FEngineVersion` block is written after `PackageFileUE4Version`.** Stock UE4 writes only the
  package version and then goes straight to custom versions. This block explains the `00 00 00 00`
  that makes a naive parser read `CustomVersionFormat` as `0x001B0004`.
* **String lengths include the NUL.** `"++UE4+Release-4.27"` is 18 characters and is written with
  `n = 19`. Every `FString` in the file follows this. This differs from stock UE4, where `n` is the
  character count and the NUL is implied. A parser written to stock rules reads every name one byte
  short and then walks off the end — that is precisely the failure this analysis hit first.

Consequence for the mod: **do not reuse a generic UE4 save parser unmodified.** `dnsav.py` is not
directly reusable (see §2.6), but its *approach* — locate the stream by structural self-validation
rather than a hardcoded offset — transfers exactly and is what `gvas.py` does.

### 2.3 The three save classes — CONFIRMED

| file | `SaveGameClassPath` | top-level properties |
|---|---|---|
| `System0.sav` | `/Game/Blueprints/SaveSystem/FS_SystemSave.FS_SystemSave_C` | **1**: `LanguageCode = "en"` |
| `World0.sav` | `/Game/Blueprints/SaveSystem/FS_SaveSlot.FS_SaveSlot_C` | **30** (below) |
| `Challenges` | `/Script/QueensDomain.FS_ChallengeSaveGame` | **1**: `ChallengeStats`, `Map<Name,int>` |

`FS_SystemSave` and `FS_SaveSlot` are Blueprint classes under `Content/Blueprints/SaveSystem/`.
`FS_ChallengeSaveGame` is a **native C++ class** declared in the game module `QueensDomain` — the
`/Script/QueensDomain.` prefix proves it. The exe also contains `UFS_SaveGame` and `UFS_SaveGameUtils`
(CANDIDATE: `UFS_SaveGame` is the C++ base of the Blueprint `FS_SaveSlot_C`; the property names below
are then C++ `UPROPERTY`s and stable across content builds — which would make them safe to hook).

### 2.4 `World0.sav` — the complete persistent world state (CONFIRMED)

Decoded exactly; `stream_end == file_size - 4`. 30 properties:

| # | Property | Type | Observed value in this save |
|---|---|---|---|
| 0 | `SavableActorStates` | `Map<Struct(Guid 16B), Int>` | 7 entries, values 0/1 (2 true) |
| 1 | `SaveLocation` | `Struct(Transform)` | tagged stream: `Rotation` (raw 16 B), `Translation` x=4039.071 y=-1748.335 z=146.915 |
| 2 | `SaveLevel` | `Name` | `Ruins_Level_00` |
| 3 | **`NamedSaveData`** | **`Map<Name, Int>`** | **11 flags** (below) |
| 4 | `PlayingMusicTrack` | `Object` | `/Game/Music/1-Stage/1-1_GreenRuin-…_MusicCue` |
| 5 | `SavePointID` | `Object` | `/Game/Blueprints/Areas/1GreenRuins/GR_Camp_Save.GR_Camp_Save` |
| 6 | `SaveVersion` | `Int` | 6 |
| 7 | `LastSaveTime` | `Struct(DateTime)` | ticks 639295758149420000 |
| 8 | `HasEverSaved` | `Bool` | true |
| 9 | `SavedArea` | `Object` | `GR_Camp_Save` |
| 10 | `PlayTime` | `Float` | 0.860657 |
| 11 | `PlayTime_Seconds` | `Int` | 658 |
| 12 | `LastSavePlaytime` | `Float` | 658.860657 |
| 13 | **`CurrentPlayerHealth`** | **`Float`** | **90.0** |
| 14 | `CollectedNotes` | `Array<Object>` | 5 (4 tutorial notes + one `None` slot) |
| 15 | `NPCsInJournal` | `Set<Object>` | 2 (Arjaot, Ekonta knight data assets) |
| 16 | `CurrentlySelectedWeapon` | `Object` | `RustySword_Item` |
| 17 | **`WeaponCharges`** | **`Map<Object, Float>`** | **106 entries**, 28 non-zero |
| 18 | **`DaggerCharges`** | **`Map<Object, Float>`** | **26 entries**, all non-zero |
| 19 | `LastTouchedCheckpoint` | `Object` | `GR_Camp_Save` |
| 20 | `SavePointsVisited` | `Array<Object>` | 1 |
| 21 | `ActiveSuspendSlot` | `Int` | 1 |
| 22 | `LastRealSavePlaytime` | `Float` | 658.860657 |
| 23 | `GamePlayTime` | `Float` | 0.268555 |
| 24 | `GamePlayTime_Seconds` | `Int` | 479 |
| 25 | **`InventoryContents`** | **`Map<Object, Int>`** | `{RustySword_Item: 1}` |
| 26 | **`Equipment`** | **`Map<Object, Int>`** | `{RustySword_Item: 2000}` |
| 27 | **`EquipmentSlots`** | **`Map<Int, Object>`** | `{2000: RustySword_Item}` |
| 28 | `ItemChangedEvent` | `MulticastInlineDelegate` | 141 B opaque (a bound delegate, serialised) |
| 29 | **`EnemySaveStates`** | **`Map<Struct, Struct>`** | **414 records** (below) |

#### `NamedSaveData` — the generic progress store (all 11 entries, CONFIRMED)

```
Arjaot_State                      1     CompletedChallenges               0
BrokeBarricade                    1     FullEnergyCellsCount              0
CheckpointUnlockCount             1     IntroMusicState                   0
RustySwordKnightGaveSword         1     TiniScytheHammerAP                0
RustySwordKnightState             1     TotalAchievementTreasures         0
                                       TotalAchievementWeapons           1
```

Read that list as a specification. It is a **`Map<FName,int>` covering quest steps, NPC states, world
events, counters and achievements in one place** — `Arjaot_State` (an NPC's state machine value),
`RustySwordKnightState` + `RustySwordKnightGaveSword` (two-step NPC interaction), `BrokeBarricade`
(a one-shot world event), `CheckpointUnlockCount` / `TotalAchievementWeapons` (monotone counters),
`IntroMusicState` / `FullEnergyCellsCount` (misc). **This one map is the quest-state sync target.**

It also shows the *shape* of the trap: three kinds of value share one type.
`BrokeBarricade` is a boolean (set, never incremented), `Arjaot_State` is an enum ordinal,
`CheckpointUnlockCount` is a counter that **must be replicated as an absolute value, never as a
delta**, or a duplicated message double-counts. The mod cannot tell them apart from the type alone;
it needs a per-flag manifest (§6.3).

#### `SavableActorStates` — per-actor one-shot bits (CONFIRMED)

`Map<Guid,int>`, 7 entries, `1,1,0,0,0,0,1`. GUIDs are 16 raw bytes; each is a *placement* GUID for a
savable actor. These are almost certainly chests/one-shot interactables. The five zeros are actors
that exist in the level but have not been used.

**The GUIDs are not resolvable from the save alone.** They are stored in the level package next to
the actor, and the pak is compressed. To map GUID → actor name you need a runtime actor dump
(UE4SS `FindAllOf` on the relevant class, printing `ActorGuid`) or a decompressed level package.
This is listed as an open question, not guessed at.

#### `EnemySaveStates` — 414 records, fully decoded (CONFIRMED)

```
Map<Struct(16-byte Guid), Struct(bValid:Bool, RespawnTime:Float, RemainingHP:Float)>
414 / 414 decoded, 65,006 / 65,006 bytes consumed
```

Every record in this save is at its default: `bValid = true`, `RespawnTime = -1.0`,
`RemainingHP = -1.0`. **`-1.0` is a sentinel meaning "never damaged / use the class default", not
"dead"** — the game has not been in combat, which matches a 479-second playthrough.

This is a gift for co-op: **the game already persists enemy state keyed by a GUID it generates, and
already has a concept of "this enemy is invalid/dead and respawns at time T with HP H".** The mod
does not need to invent entity identity or a persistence model for enemies. It needs to (a) make both
machines agree on live enemy state and (b) make this map converge.

### 2.5 `Challenges` — CONFIRMED

`ChallengeStats: Map<Name,int>` with `{cha_Item_GotAllWeps: 1, cha_Challenge_AllTeleporters: 1}`.
The game has a challenge/achievement system with an id namespace `cha_*`. There is a
`UFS_ChallengeSystemComponent`, `FS_ChallengeData`, `FS_ChallengeDefinition`, `EFS_ChallengeMode`,
and a `ChallengeCompletedEvent` delegate. Challenges are **per-player progression** in a co-op sense
— decide per-save or per-player (open question).

### 2.6 What `dnsav.py` gives us, and what it does not

`dnsav.py` targets EasyMultiSave, a 48-byte-block, zlib-chunked container. Queens Domain uses the
engine's own `GVAS` format, so **none of the container code is reusable** — there is no block loop, no
zlib, no preamble scan. What transfers:

* the tagged-property grammar (`Name/Type/Size/ArrayIndex` + type extras + optional property GUID) —
  identical, because it is engine code;
* the container-value conventions (`Array` = count + items; `Set` = numToRemove + count; `Map` =
  numToRemove + count + pairs) — identical;
* the *method*: locate the stream by structural self-validation (parse to `None`, require the
  remainder to be exactly the 4-byte trailer) rather than trusting an offset. This is what made the
  non-stock header survivable — `gvas.py` never hardcodes `stream_offset`, it derives it, and the
  exact-fit check is what proves the derivation.

Three game-specific quirks `dnsav.py` could not anticipate and that a co-op writer will hit:
**(1)** lengths include the NUL; **(2)** the game declares its own `USTRUCT` named `Transform` whose
body is a *tagged stream* (`Rotation`, `Translation`) rather than the native 40-byte struct — a parser
that trusts the struct name produces a 40-byte read for a 174-byte property and dies; **(3)** map keys
can be **bare 16-byte binary structs** with the value as a tagged stream (`EnemySaveStates`) or a
scalar (`SavableActorStates`), so map key width must sometimes be derived by arithmetic from the
declared property size rather than parsed.

---

## 3. Extracted class / property surface

Extraction: 81,374,720 bytes → 968,485 ASCII runs + 40,494 UTF-16LE runs → **194,474 unique**,
**69,091 identifier-shaped**, **664 asset paths**. The game module is `/Script/QueensDomain`.
Grouping is in `exe_surface_grouped.json`; raw lists are `exe_names.txt` / `exe_paths.txt`.

Note on method: this is a *string-pool* extraction. It proves a name **exists in the build**; it does
not prove a name is a `UPROPERTY` rather than a local variable or a data-asset field. Everything in
§3.1 is a class or delegate name, which is much stronger evidence. Everything in §3.2 is a
property/function-shaped name and is **CANDIDATE** as to its exact class and replicated status.

### 3.1 CONFIRMED — the game's own types and delegates

**Save / persistence**
```
UFS_SaveGame              UFS_SaveGameUtils          UFS_ChallengeSaveGame
FS_NamedSaveDataChange    EFS_NamedSaveDataChangeMode    NamedSaveDataValueChanges
GetNamedSaveData          ReadNamedSaveData          FS_GameSaveEvent
GenerateSuspendData       RestoreFromSuspendData     RestoreSuspendState
GetSafeLocationSuspendState   CacheSafeLocationData  CacheSafeLocationState
RestoreSafeLocationData   RestoreSafeLocationState   SafeLocationDoorCounters
OneShotEntityTree         CurrentlyActiveSave        CurrentlyActiveSystemSave
GetActiveBaseSave         SetActiveSave              GetCurrentSaveFileSlot
ForceReloadSave           DeleteCurrentSaveFile      CurrentlyWritingSave   RunningSaveOps
```

**Enemies / AI**
```
FS_Enemy                  FS_EnemyAttackConfig       FS_EnemySaveState      EnemySaveStates(+_Key)
UFS_EnemyAction           UFS_EnemyUtils             UFS_EnemyAnimInstanceBase
UFS_PathFollowingComponent   UFS_SpawnerInterface    EFS_SpawnerUpdateMode
EFS_EnemyActionResponse   EFS_EnemyActionCancelReason   EFS_AttackAggroNoise
SpawnerGUID               CheckSpawnerMaxDist        CheckSpawnerVis
PlayerDistanceDespawnCheck   SpawnerDistanceDespawnCheck
EnemyDiedEvent            EnemyStaggeredEvent        EnemyPauseEvent
EnemyActionFinishedCallback   EnemySetPausedTDL       EnemyThatDied   EnemyThatStaggered
RespawnTime               bValid                     RemainingHP
```

**Items / inventory / equipment**
```
UFS_Item     UFS_ItemStats     UFS_ItemDropSystem    UFS_StatsContainer
UFS_WeaponStats   UFS_DaggerStats   FS_WeaponStats   FS_AccessoryStats
FS_ThrowingWeaponStats   FS_Stats   S_StatContainerValues
FS_DropCacheEntry   FS_DropCacheKey   DropTable   DropTrigger   DropIndex   UPickupUtils
EFS_PickupLocation   EFS_PickupDifficulty   ItemsDestroyed   ItemsGenerated
InventoryContents   Equipment   EquipmentSlots   ItemChangedEvent   ItemAcquired
WeaponCharges   DaggerCharges   WeaponChargeDuration   ItemQuantity   ItemId   ItemDefs
FS_ShopConfigData   FS_ShopStockUnlock   Restocks   bSetNamedSaveDataWhenPurchased
```

**Dialogue / journal**
```
UFS_DialogueData   UFS_DialogueAction   FS_DialogueSequence
S_DialogueSequenceEntry   S_DialogueOption
EFS_DialogueConditionType   EFS_DialogueConditionBehaviour
DialogueToJumpTo   JumpToDialogue   DialogueOptions   OptionalDialogueToRunAfter
DialogueActionEvent   DialogueJumpEvent   DialogueNotes
bSubtitlesEnabled   bSuppressSubtitles   DialogueFilenameFormat
UQD_Journal_Data   JournalMode   Journal_ViewConfig
```

**Levels / areas / music**
```
UFS_LevelManagerEntry   UFS_LevelList   FS_LevelListStackEntry   FS_LevelManagerAction
FS_LevelLoadSuspendState   EFS_WaitForLevelManagementMode   FS_WaitForLevelManagement
LevelManagerUpdate   LevelManagerDoneLoading
EFS_LocationType   FS_LocationData   UFS_LocationData   FS_AreaChangeEvent
AreaID   AreaName   Areas   AreaUpdated
UFS_MusicAreaData   UFS_MusicAreaInstance   FS_MusicArea   MusicAreaUpdated
```

**Player / stats / input / damage**
```
FB_DamageAttributeSet   UFB_AttributeComponent   UFB_DamageableInterface   UFB_DamageableTarget
RegisterDamageableActor   UnregisterDamageableActor   bCanBeDamaged
MaxHealth   CurrentHealth   HealthRegen   MaxStamina   StaminaRegen   GetDamageMultiplierStat
UFS_InputSubsystem   UFS_GameInstanceBase   UFS_GameViewport
UFS_CheatManagerBase   UFS_CheatManagerExtension
FS_ButtonSignature   FS_AnalogueSignature   FS_MouseButtonSignature   FS_MouseMoveSignature
```

**Enums that encode authority-relevant state**
```
EFS_EnemyActionResponse   EFS_EnemyActionCancelReason   EFS_SpawnerUpdateMode
EFS_NamedSaveDataChangeMode   EFS_DialogueConditionType   EFS_DialogueConditionBehaviour
EFS_PickupLocation   EFS_PickupDifficulty   EFS_LocationType   EFS_ChallengeMode
EFS_WaitForLevelManagementMode   EFS_WeaponDamageTypeDisplay   EFS_WeaponRange
```

`UFS_CheatManagerBase` / `UFS_CheatManagerExtension` are worth calling out separately: the game ships
a **native cheat manager**, which is the cheapest possible test rig (teleport, god mode, grant item)
for building the co-op harness.

### 3.2 CANDIDATE — property/function names, grouped

Grouped by domain; provenance is the exe string pool, class attribution unverified. Useful as a
search key when the mod hooks a class, not as a specification.

* **Quest/journal:** `Progress`, `ProgressType`, `ProgressPercent`, `ProgressionLevel`, `UnlockItem`,
  `UnlockedDescription`, `AchievementID`, `AchievementName`, `ChallengesCompletedDuringFullRefresh`,
  `TrialGenerations`, `TrialRandomness`, `UnlockAILogic`.
* **Dialogue:** `DialogueContext`, `DialogueWave`, `PlayDialogueAtLocation`, `SpawnDialogueAttached`,
  `SubtitlePriority`, `SubtitleOverride`.
* **Enemy:** `AggroCones`, `AttackConfiguration`, `EnemySecondaryTickFunction`, `Spawner`,
  `SpawnerGUID`, `bIsStaggered`.
* **Interactable:** `bDoorCounter`, `ResetDoorCountersOnAllInstances`, `bLinearBreakable`,
  `bAngularBreakable`, `Vendor`, `Restocks`, `Gate`, `Hasp`, `Portal`.
* **Player:** `PlayerGuid`, `PlayerIndex`, `PlayerName`, `PlayerPushbackHorizontalStrength`,
  `PlayerPushbackVerticalStrength`, `PlayerKnockdownStyle`.
* **Chest/gating:** `Chest`, `LockedDoor`, `KeyItem` were **not** found as identifiers; the game
  expresses gating through item paths under `Items/KeyItems/` (§4). Absence is reported, not
  inferred into a class.

### 3.3 CONFIRMED NEGATIVE — there is no multiplayer code

Measured, in this build:

| probe | result |
|---|---|
| `GetLifetimeReplicatedProps` | occurs **once** in 81 MB, and only inside an engine console-help sentence: `"Warns if replicated properties were not registered in GetLifetimeReplicatedProps."` — i.e. it is engine doc text, **not** a function name belonging to any game class |
| game-module `Server*`/`Client*` RPC pairs with `_Implementation`/`_Validate` | none attributable to `FS_`/`UFS_` types |
| `bReplicates`, `ReplicatedMovement`, `NetUpdateFrequency`, `ReplicatedBasedMovement`, `NetConnection`, `NetDriver` | present once each — stock engine `FName` pool, `Engine/NetDriver.h` boilerplate text also present (**`D:\\.jenkins\workspace\QueensDomain\FB-UE\Engine\Source\Runtime\Engine\Classes\Engine/NetDriver.h`** — the build path, which also confirms a Jenkins build of a custom engine fork) |
| `UIpNetDriver`, `IpNetDriver` | present — the engine's UDP driver is compiled in, but nothing in the game uses it |
| `CreateSession` / `FindSessions` / `OnlineSession` / Steam session code | engine plugin only; `Steam*` hits are the Steamworks plugin's API surface |
| `steam_api64.dll` on disk | **absent**. `Binaries\Win64` contains only the exe plus `vkd3d-proton.cache*` (a Proton/D3D-translation artefact, i.e. someone has run this under Wine/Proton) |
| Net driver configuration | only `Engine.ini` with `[Core.System] Paths=`; **no** `NetDriverDefinitions`, no `Game.ini` content |
| `bUseSeamlessTravel`, `ServerTravel`, `ClientTravel` | engine names only |

**Conclusion (CONFIRMED):** the game is a single-player UE4 build with the engine's net stack present
but unused. There is no server, no session, no replicated property, and no RPC in the game module.
The co-op mod must supply transport, authority and state replication from outside the game's own
systems. This matches the task's premise and does not need to be re-litigated.

---

## 4. Interactables and one-shot world events

### 4.1 Level set (CONFIRMED, 51 `.umap`)

```
Intro_Level_00/01   Ruins_Level_00   100_GR   110_GR_dungeon   120_GR_goldenroom
125_GR_CubeHall     130_GR_outerisles  140_GR_ShopIsleOnly   200_AQ   210_AQ_underground
300_LIB   310_LIB_exterior   320_LIB_basement   400_MAUS_entrance   420_MAUS_mid
450_MAUS_darkmaze   500_AUT   510_AUT_droptunnel   600_Castle   610_Castle_Ext
700_SZ_GR   710_SZ_AQ   720_SZ_LIB   730_HOD   740_SZ_Prison   750_SZ_MAUS2
760_Dekanima   760_SZ_AUT   770_SZ_Swamp   775_JigekiTemple   780_SZ_Depths
790_SG   800_FinalZone   900_SipehaWorld   990_B33sment
997_FinalZoneCutscene   998_QueenCutscenes   999_SipehaCutscenes
050_Ocean   060_Ocean_Shadow   070_UpperShell   Entry   title_screen
Journal3DViewMap   VideoConfigMap   VideoConfig_2   BirdsTestZone
SublevelRefTest   Ruins_Level_00_ShaderCompile   000_Shared_Base_Lighting
```

Two structural facts matter for travel sync: **`Journal3DViewMap` is a separate map** (the journal has
a 3D viewer — opening it is a travel-shaped event), and the game uses the `NNN_XX_YYY` level-prefix
convention with paired `_Cutscene` maps, i.e. **cutscene playback is a level transition**, not an
in-place sequence. That makes cutscenes a travel-sync problem, not a dialogue-sync problem.

### 4.2 Area / Blueprint taxonomy (CONFIRMED, from the pak directory index)

```
Blueprints/BreakableBarricade   Blueprints/Elevator          Blueprints/Teleporter
Blueprints/SecretSwordAltar     Blueprints/SpectralSwordAltar Blueprints/SuperCubeAltar
Blueprints/UpgradeMachine       Blueprints/Shop              Blueprints/SaveSystem
Blueprints/Challenges           Blueprints/Cutscenes         Blueprints/LevelScripts
Blueprints/ChefCrowEvent        Blueprints/ENVControl        Blueprints/FogSystem
Blueprints/MusicControl         Blueprints/ItemSystem        Blueprints/WallHint
Blueprints/Areas/{1GreenRuins, 2AquaRuins, 3Library, 4Mausoleum, 5AutumnIsland, 6Castle, 7SZ, ROS, Internal, Other}
```

Read as an interactable inventory, that folder list is the answer to "what are the interactables":
**breakable barricades, elevators, teleporters, three kinds of altar, an upgrade machine, shops,
save points, challenge boards, cutscene triggers, music/fog area control, wall hints, and a
scripted event (Chef Crow).** Each `Areas/<N><Name>/` folder holds the region's `*_Save` actors
(`GR_Camp_Save` is a confirmed example from the save) — i.e. **save points are ordinary actors placed
in the level, and `SavePointsVisited` / `LastTouchedCheckpoint` / `SavePointID` are object paths.**

### 4.3 Key items are the door-gating model (CONFIRMED, `Items/KeyItems/`)

```
AquaDoorKey (+ Dark/)   CastleDoorKeys   ForbiddenDoorKey   ArchivesKey
JigekiKeys              JosamiKey_Red    ShopProgressionKey   ShopPurchasedKey
BookKeys/{Alchemy, BoneMachines, CookBook, DamsAndWeirs, Dragons, Eldritch,
          FairyTales, Jigeki, Puwema, QueerRomance, RattyBugs, Sipeha, WarHistory, Wodaso}
KingSword_Remnant       KnifeBelt        FishMaidenTiaras     Egg
```

So doors are **key-item gated**, and the key items are ordinary inventory items. That means **door
unlock state does not need its own replication channel** — it falls out of inventory sync plus the
door's own one-shot flag. The `BookKeys/` set is a large, enumerable collectible family with 14
categories, which is exactly the kind of thing an achievement counter (`TotalAchievementTreasures`)
tracks.

### 4.4 NPCs (CONFIRMED)

`NPCsInJournal` is a `Set<Object>` of NPC data assets. The pak gives:
`Knight/{Arjaot, Budaen, Daon, Ekonta, Imba, Ippibi, Nu, Ujji}` (+ `Dead/`), `Librarian`, `Merchant`,
`Chef`, `Crow`, `King`, `FishWife`, `SageIkku`, `Raider`, `Rat`, `Crab`, `Lizard`, `Bug`, `Fish`,
`BMMC`. The save contains `Arjaot` and `Ekonta` — so **journal membership is per-NPC state**, and
`NamedSaveData` holds `Arjaot_State` per NPC. NPC dialogue state is therefore two-part: journal
membership (a set) and a per-NPC state int (a flag).

### 4.5 Proposed authoritative model for world events

Classify every interactive object into exactly one of four classes at mod-load time, by the actor's
own type plus a shipped manifest:

| class | examples | authority | rule |
|---|---|---|---|
| **Server one-shot** | chests, altars, breakable barricade, key-item doors, elevator/teleporter activation, shop stock unlock, challenge board claim | **host** | First request wins; host applies, records `actorGuid → state`, broadcasts. Peer applies only on receipt. **Never** peer-initiated. |
| **Server-owned state machine** | NPC dialogue availability, quest flag writes, `NamedSaveData` | **host** | Host is the only writer of the flag map. Peer requests a write; host validates and echoes. |
| **Owner-local, shared effect** | player's own weapon charge, dagger charge, stamina, status effects | **owning client** | Client owns its vitals; host mirrors for HUD/aggro. Host may correct. |
| **Client-local, never replicated** | UI widgets, subtitle rendering, camera, pause menu, tutorial note popups | **each client** | Do not replicate. Replicating these is the classic co-op bug (dollsMP F-001: `bHidden` is not replicated; UI is per-client by construction). |

Rationale for "host owns one-shots": the save already records one-shots as `actorGuid → int` and
`FName → int`. Those are **idempotent set operations** — a lost or duplicated message converges on
the correct value as long as the *value* is sent, not a delta. This is the cheapest possible
correctness argument, and it is why the model should be "resend state, never merge".

---

## 5. The Sync Matrix

Legend — **Rate**: `C` continuous streaming, `D` discrete reliable event, `S` snapshot/resync.
**Auth**: `H` host, `O` owning client, `L` local-only. **Risk**: desync severity if unsynchronised.

| # | Subsystem | State that exists | Rate | Auth | Kind | Size est. | Desync risk |
|---|---|---|---|---|---|---|---|
| 1 | **Player transform** | world `Translation`, `Rotation` (pitch/yaw/roll), velocity, capsule half-height, movement mode | 20–30 Hz | O | C | 24–32 B | **Low.** Interpolated with a 100–150 ms buffer; snap beyond 2 m. View angles are the sensitive part (aiming) — send pitch/yaw at full rate, position at 20 Hz. |
| 2 | **Player representation** (the *other* player) | skeletal mesh + pose, weapon actor (`CurrentlySelectedWeapon`), dagger mesh, accessory meshes, skin, locomotion + montage state | 10 Hz + events | O | C + D | ~40 B + 8 B/event | **High if absent.** This game's player is a ThirdPerson-style body (dollsMP needed a full appearance lane). Montage/attack playback must be an event with a start timestamp, not polled. |
| 3 | **Player vitals** | `CurrentHealth`, `MaxHealth`, `MaxStamina`, energy cells (`FullEnergyCellsCount`), status: poison / bind / weaken / sticky / heavy / disease | 10 Hz + event | O (H may correct) | C + D | 20–28 B | **Medium.** `CurrentPlayerHealth` is *in the save*, so a wrong value persists across loads. Death is the dangerous edge (§6.6). |
| 4 | **Player stats / progression** | `FS_Stats` / `S_StatContainerValues` (attack, defense, weapon XP), accessory effects, upgrade-machine purchases | D | O | D | 8–24 B | **Low-Med.** Derived stats recompute locally from equipment; only the inputs must agree. |
| 5 | **Inventory / equipment** | `InventoryContents Map<Object,int>`, `Equipment Map<Object,int>`, `EquipmentSlots Map<int,Object>`, item quantities, key items, consumables, currency if any | D | per-player, H arbitrates world pickups | D | 16–24 B/op | **High.** Object-path keys are long (~60–90 B raw) — hash them to u32 ids from a shared table. `ItemChangedEvent` is a multicast delegate that will fire locally on the peer and churn UI. |
| 6 | **Weapon charges** | `WeaponCharges Map<Object,float>` (106), `DaggerCharges Map<Object,float>` (26), `WeaponChargeDuration` | 5 Hz or on change | O | C (dirty-flag) | 4 B/entry | **Low.** Float counters drift if integrated locally. Send absolute values on a dirty mask. |
| 7 | **Enemy lifecycle** | spawn / despawn / `bValid`, `SpawnerGUID`, `SpawnerDistanceDespawnCheck`, `EFS_SpawnerUpdateMode` | D | H | D | 16–20 B | **High.** Peer must *not* run its own spawn/despawn logic or enemies pop in and out independently. |
| 8 | **Enemy transform + animation** | location, rotation, velocity, movement mode, montage / action id, `bIsStaggered` | 10 Hz (tier A) / 2 Hz (tier B) | H | C | 19 B/row | **High** if local AI runs; **Low** with a puppet. Tier by distance + engagement. |
| 9 | **Enemy health / damage** | `RemainingHP`, `RespawnTime`, death | D | H only | D | 12 B | **Critical.** Double-damage and double-loot both live here. See §6.2. |
| 10 | **Enemy aggro / target** | target actor, `EFS_AttackAggroNoise`, awareness cone | 2 Hz + event | H | C + D | 6 B | **Medium.** Peer's enemy must *face and chase what the host says*, not what it perceives. |
| 11 | **Enemy actions / attacks** | `UFS_EnemyAction` id, `EFS_EnemyActionResponse`, `EFS_EnemyActionCancelReason`, hit timing, projectile spawn | D | H | D | 8–16 B | **High.** Attacks that damage the peer must be triggered by the host so the peer can dodge against them. |
| 12 | **Quest flags** | `NamedSaveData Map<FName,int>` (quest steps, NPC states, world events, counters, achievements) | D on change; S on join | H | D | 8–12 B/flag | **Critical.** Three value kinds share one type (§2.4). Counters must be absolute. |
| 13 | **Journal / notes** | `CollectedNotes Array<Object>`, `NPCsInJournal Set<Object>`, `UQD_Journal_Data` unlocks | D | H | D | 8 B/id | **Low.** Monotone set adds — trivially idempotent. |
| 14 | **Quest items** | key items in inventory, `Items/KeyItems/*` grants, door opens | D | H | D | 12 B | **Medium.** A key granted twice is harmless; a key the peer lacks *locks the peer out of a region alone*. |
| 15 | **Loot spawns** | `DropTable`, `DropTrigger`, `FS_DropCacheEntry`, `ItemsGenerated`, `EFS_PickupLocation`, `EFS_PickupDifficulty` | D | H | D | 24 B | **Critical.** Randomised drops generated on both machines = different items in the same chest. |
| 16 | **Pickups / containers** | pickup claimed, container opened, `SavableActorStates` bit, `ItemsDestroyed` | D | H | D | 16–20 B | **High.** Classic race: both players grab the same item. Host arbitrates by `actorGuid`. |
| 17 | **Dialogue** | sequence asset, entry index, subtitle state, `DialogueOptions` choice, `DialogueToJumpTo`, `OptionalDialogueToRunAfter` | D + 2 Hz progress | L (local dialogues) / H (global) | D | 8 B + 4 B | **High.** Blocking + possible world pause. See §6.4. |
| 18 | **Interactables** | doors, levers, elevators, altars, upgrade machine, shops, breakable barricade, `bDoorCounter`, `SafeLocationDoorCounters` | D | H | D | 20 B | **Medium.** One-shots are idempotent; *elevators and moving platforms* are continuous and need transform sync, not a flag. |
| 19 | **Checkpoints / save points** | `SavePointsVisited`, `LastTouchedCheckpoint`, `SavePointID`, `CheckpointUnlockCount`, safe-location state | D | H | D | 24 B | **High.** Determines respawn point for *both* players. |
| 20 | **Level / area transitions** | `SaveLevel`, `SavedArea`, `UFS_LevelList`, `LevelManagerUpdate`, `LevelManagerDoneLoading`, `EFS_WaitForLevelManagementMode`, entry transform | D | H initiates | D | 64–96 B + level name | **Critical.** Both must arrive together. See §6.5. |
| 21 | **Level streaming** (sublevels) | streamed block load/unload, `LevelVisibilities`, `LevelStreamingStatus` | 0.5 Hz + D | H | D | 4 B/mask | **Medium.** If the peer streams a different block set, the peer falls through unloaded floor. |
| 22 | **Death / respawn** | `bDead`, death location, respawn target, `RespawnTime`, invuln window | D | O + H arbitration | D | 12 B | **Critical.** Does death reload the level? Unknown (§9). |
| 23 | **Fast travel / teleporter** | `Blueprints/Teleporter`, `TeleporterCrystal`, destination | D | H | D | 32 B | **High.** A teleport is a travel-shaped event for one player — does it move the other? |
| 24 | **Music / ambience** | `PlayingMusicTrack`, `IntroMusicState`, `UFS_MusicAreaInstance`, `MusicAreaUpdated` | D | H | D | 8 B | **Low** (cosmetic) but **high-visibility**: mismatched boss music reads as a broken mod. |
| 25 | **Save / checkpoint coordination** | write of `World0.sav`, `World0_backup.sav`, `System0.sav`, `Challenges` | D | H (authoritative) | D | — | **Critical.** See §6.7. |
| 26 | **Challenges / achievements** | `ChallengeStats Map<Name,int>`, `ChallengeCompletedEvent`, `EFS_ChallengeMode` | D | per-player or H | D | 8 B | **Low.** Decide per-save vs per-player. |
| 27 | **Cutscenes** | playback of `997/998/999_*Cutscene` maps, `Blueprints/Cutscenes` | D | H | D | 16 B | **High.** A cutscene is a **level load** (§4.1), so it is a travel, not a dialogue. |
| 28 | **Shop state** | `FS_ShopConfigData`, `FS_ShopStockUnlock`, `Restocks`, `ShopProgressionKey`, `ShopPurchasedKey` | D | H | D | 12 B | **Medium.** Stock is per-save; per-player purchases must not deplete the other's stock unless that is the design. |

### 5.1 Why this decomposition and not a per-actor blanket

A generic "replicate every actor" design fails here for a concrete reason: **the game already spawns,
despawns and persists enemies with its own distance and visibility rules**
(`CheckSpawnerMaxDist`, `CheckSpawnerVis`, `PlayerDistanceDespawnCheck`), and those rules are
*per-player*. On two machines they will disagree constantly, producing enemies that exist on one
screen and not the other. The matrix above therefore separates **lifecycle** (#7, host-only) from
**representation** (#8, host-sampled, peer-puppeted) from **effects** (#9/#11, host-emitted events).
Collapsing those three into one lane is the design error that produces both double-damage and
invisible enemies.

---

## 6. Hard problems and proposed solutions

### 6.1 The game has no networking — so what carries the packets?

Confirmed in §3.3: nothing in the game replicates. The mod must own transport. Three viable carriers,
in order of preference:

1. **UE4SS (Lua/C++) mod + an injected UDP socket**, with a same-machine loopback path for
   development. This is what the dollsMP project converged on (external binary + UE4SS Lua + a
   `dxgi.dll` proxy for the same-machine test rig).
2. **Steam networking** (`ISteamNetworkingSockets`) — `steam_api64.dll` is *listed in the manifest but
   absent from the install*, and the engine's Steam plugin is cooked in. If the game is distributed
   with Steam DRM this is the cleanest remote path; it requires the DLL to be present, which is a
   packaging question, not a code one. Treat as conditional.
3. **Raw UDP with a rendezvous server / port-forward.** Works, no dependency, but needs NAT handling.

The mod needs a **game-thread callback budget**, not a bandwidth budget, as the binding constraint —
dollsMP measured `~39–43 game-thread callbacks/s` as the real wall and had to coalesce every
high-rate lane into one batch per tick. The same applies here: the game is a UE4 title with a single
game thread and heavy per-actor tick work (`EnemySecondaryTickFunction` exists precisely because
someone already moved enemy secondary ticks off the main path).

### 6.2 Enemy AI — deterministic or authoritative? **Authoritative, and the reasons are specific.**

Arguing determinism first, and rejecting it on evidence:

* Enemy behaviour is a **custom action system**, not a behaviour tree:
  `UFS_EnemyAction`, `EFS_EnemyActionResponse`, `EFS_EnemyActionCancelReason`,
  `EnemyActionFinishedCallback`. Actions are selected from `FS_EnemyAttackConfig`
  (`AttackConfiguration`) with randomised parameters — the exe also carries `TrialRandomness`,
  `TrialGenerations`, `Randomness`-shaped names in the same domain.
* The simulation is **frame-time driven** (`EnemySecondaryTickFunction`, `DeltaTime`-scaled), not
  fixed-step. Two machines at different frame rates integrate differently even with identical inputs.
* Physics is **Chaos** (`ChaosBreakingEventRequestSettings`, `ChaosCollisionEvent…`) — floating-point
  divergence is guaranteed, not merely likely.
* There is **no shared random seed** because there is no networking layer to seed it from.

So: **host-authoritative AI.** Concretely:

* **Host**: enemies run their real AI, real timers, real attacks. Host is the only machine that
  applies damage to enemies and the only machine that decides a death.
* **Peer**: the same enemy actors exist (they are level-placed, so both machines have them at the
  same transforms), but their **AI controller and movement are disabled**; they are driven from host
  snapshots (transform, action id, montage position). Animation runs locally from the replicated
  action id, so it looks like a live enemy with a ~100 ms lag.
* **Peer's attacks against enemies** are *intents*, not effects: the peer sends
  `{weaponId, targetGuid, hitBone, damageType, timestamp}` and the host applies the damage. This is
  what removes double-damage at the root — there is exactly one damage applier.
* **Loot**: only the host rolls `DropTable` / `FS_DropCacheEntry`. The peer never generates a drop.
  On pickup, the peer sends `claim(actorGuid)`; host applies first-wins and broadcasts. This removes
  double-loot at the root, and it matches the save's own model (`SavableActorStates` is a
  `Guid → int` set operation, `ItemsGenerated`/`ItemsDestroyed` are recorded).

**Cost of this choice:** enemy attacks aimed at the peer must be initiated by the host, so the peer's
dodge timing is against a ~1 RTT-old attack. That is the standard co-op compromise and is preferable
to two independent worlds. **Escape hatch:** if latency proves unacceptable, promote enemy
*animation-only* to peer-local prediction (play the attack wind-up immediately on the peer when the
host's intent arrives, then reconcile) — this keeps damage single-applier while restoring feel.

**The one thing that must NOT be done:** letting both machines run AI and reconciling afterwards.
With randomised attack configs and Chaos physics, reconciliation is not convergent, and the failure
mode is the worst kind — enemies that are alive on one screen and dead on the other, which then
double-drop loot.

### 6.3 Quest advancement — the generic hook, and the escape hatch

**Generic approach (recommended).** Do not hook quests; **hook the flag store**.

`NamedSaveData` is a single `Map<FName,int>` that already contains quest steps, NPC states, world
events and counters. The exe confirms the accessors and the notification path:

```
GetNamedSaveData   ReadNamedSaveData
FS_NamedSaveDataChange        (delegate)
NamedSaveDataValueChanges     (+ _Key)
EFS_NamedSaveDataChangeMode   (enum — a change *mode*, i.e. set/add/clear)
```

So the mod's quest sync is:

1. On load and on join, read the whole map → authoritative snapshot.
2. Subscribe to the change notification; every write produces `(FName, value, mode)`.
3. Host is the only writer. Host broadcasts the resulting **absolute value** of the changed key.
4. Peer applies by calling the game's own setter, so all downstream Blueprint logic runs exactly as
   it would have — the mod never reimplements a quest.
5. Resync is "re-send the whole map", never "merge".

This is generic: it works for a quest the mod has never heard of, because the game itself drives its
own logic off the map.

Two caveats that must be handled or the generic hook silently corrupts state:

* **`EFS_NamedSaveDataChangeMode` means some writes are relative.** A key written in "add" mode is a
  counter (`CheckpointUnlockCount`, `TotalAchievementWeapons`, `TotalAchievementTreasures`). The mod
  must broadcast **the post-write absolute value**, never the delta, and the peer must **set**, never
  add. This is the single most likely way to ship a double-counting bug.
* **Set-side effects.** Some flags exist to trigger an action once (`BrokeBarricade` opens a door,
  `RustySwordKnightGaveSword` removes an item). Applying the flag on the peer must not re-run the
  action; the peer should apply flags *quietly* (set the value, suppress the Blueprint reaction) and
  let the corresponding world event arrive on its own channel. Otherwise the peer double-grants.

**Escape hatch.** Ship a small manifest (`flags.json`) that classifies each `FName`:

```
{ "BrokeBarricade":            {"kind":"bool"},
  "Arjaot_State":              {"kind":"enum", "values":["0","1","2","3"]},
  "CheckpointUnlockCount":     {"kind":"counter","absolute":true},
  "RustySwordKnightGaveSword": {"kind":"oneshot","quiet":true} }
```

Unknown flags default to `{"kind":"bool","quiet":true}` — safe, because a set is idempotent. The
manifest is the escape hatch for the handful of flags whose semantics the type cannot express (an
objective that requires "talk to X **then** Y" is really two flags and a gate; classify it in the
manifest rather than adding code).

**Third hook, for the cases the flag map misses:** `FS_GameSaveEvent` fires around saves, and
`FS_InventoryChangeEvent` fires on item changes. Between `NamedSaveData` + `ItemChangedEvent` +
`FS_AreaChangeEvent`, the mod sees essentially every progression-relevant write without knowing what
a quest is.

### 6.4 Dialogue — the coherence problem

Dialogue here is **data-driven** (`UFS_DialogueData`, `FS_DialogueSequence`,
`S_DialogueSequenceEntry`, `S_DialogueOption`, `EFS_DialogueConditionType`,
`EFS_DialogueConditionBehaviour`, `OptionalDialogueToRunAfter`, `DialogueToJumpTo`). That means the
mod can enumerate and classify sequences instead of patching each one.

Proposed classification, decided **per sequence** at load:

| class | signature | behaviour |
|---|---|---|
| **Local conversation** | short, has `DialogueOptions` (a choice), no camera takeover, no `OptionalDialogueToRunAfter` chain into a cutscene | **Per-player and independent.** Host does not pause the world for the peer's conversation. The peer's own game may pause *itself*; the mod must ensure that pause is **client-local**, not a replicated world pause. |
| **Global / cutscene** | long, chained (`OptionalDialogueToRunAfter`), or lives in `997/998/999_*Cutscene` maps | **Host-initiated, both view.** Host triggers, sends the sequence id + entry index; both play; "skip" is host-authoritative and propagates. |

The pause question is the real hazard, and it must be handled explicitly:

* The mod must intercept the game's world-pause path (`SetPause` / `UGameplayStatics::SetGamePaused`)
  and **refuse to pause for a local conversation.** If the peer pauses, the host keeps simulating;
  if the host pauses for a menu, that is a host-side *local* pause and must not freeze the peer.
* If a **global** sequence pauses, both pause — but the pause must be driven by the *sequence id*,
  not by the delegate, so a late joiner or a lost packet cannot leave one client frozen forever. Add
  a 10 s watchdog: if the peer is paused with no matching sequence id, unpause locally and resync.
* **Choices**: only the host's selection is authoritative for a global sequence. For a local
  conversation, only the owning player's selection matters. Sending both over one channel is how
  you get the "both players' choices merged" bug — keep them in separate streams keyed by player.

The dollsMP corpus spent several documents on exactly this (`76-dialogue-lock-event-rules-consumed-keys.md`,
`71-death-with-dialogue-open.md`); the transferable lesson is that **dialogue must be modelled as a
lock with an owner**, not as a stream of events.

### 6.5 Level transitions — how two clients arrive together

The game obliges here: it has an explicit level manager with an ordered list and completion delegates.

```
UFS_LevelManagerEntry    UFS_LevelList    FS_LevelListStackEntry
FS_LevelManagerAction    EFS_WaitForLevelManagementMode   FS_WaitForLevelManagement
LevelManagerUpdate       LevelManagerDoneLoading
FS_LevelLoadSuspendState (the save system's record of a load in flight)
```

**Protocol:**

1. **Host is the only travel initiator.** `FS_AreaChangeEvent` / `LevelManagerUpdate` on the host is
   the trigger; on the peer it is ignored as a trigger and used only as an acknowledgement.
2. Host sends `TRAVEL{level, sublevelMask, entryTransform, generation}`. `generation` is a monotone
   counter — it is the fencing token that makes a stale travel message harmless.
3. Peer enters a **blackout gate**: input suppressed locally, no interaction, no pickup claims. The
   peer then loads the same level.
4. Peer sends `ARRIVED{generation, level}` when `LevelManagerDoneLoading` fires. Host waits for it
   before un-gating. Timeout (30 s) → host un-gates alone and logs; **never** strand a client in
   blackout.
5. **Snapshot after arrival, not before.** Quest flags, inventory, enemy states and one-shots are
   re-sent *after* both report arrival. Applying them before the level exists is how a load
   double-applies.
6. **Quest state must not double-apply** on travel because the level's own `BeginPlay` re-runs
   Blueprint logic that reads `NamedSaveData`. If the peer's map already matches the host's, that
   logic is a no-op. If the mod instead *replayed* the write log, every counter would double. So:
   **travel always ends in a full snapshot, never a replay.**

The `FS_LevelLoadSuspendState` name says the game already has a notion of "a load is in flight and the
world must not be interacted with" — the mod should reuse that state rather than inventing a second
one, or a save triggered mid-travel will capture a half-loaded world.

### 6.6 Death and respawn

Unknown and important: **does death reload the level?** The save exposes `RespawnTime` and the
`EnemySaveStates` sentinel, but nothing about the player's death path. The dollsMP project spent four
documents (and refuted its own conclusion twice) on this exact question — see
`docs/sync/20-coop-authority-model.md` F-012/F-013: the symptom that distinguished cause categories
was "ESC does not open the menu", not a rendering observation.

For Queens Domain the *first* experiment must therefore be: **die once with the mod loaded and
observe whether the world reloads, whether `LastTouchedCheckpoint` governs the spawn, and whether the
menu still opens afterwards.** Everything else about death sync is downstream of that answer.

Proposed default until measured: **host-authoritative death, owner-local death screen.** The dying
player's screen is theirs alone (never replicate UI). The host is told `DIED{playerId}` and decides
whether the partner's world is affected at all. If death *does* reload the level, that reload must be
suppressed for the survivor and turned into a respawn — the survivor must not be dragged into the
dead player's reload.

### 6.7 Save coordination

The save is a **single-player save** with three files and two backups. In co-op:

* **Host's `World0.sav` is the authoritative campaign state.** Quest flags, one-shots, checkpoints,
  enemy states and the world's item containers live there.
* **Per-player state must not be written into the shared save** — `InventoryContents`, `Equipment`,
  `EquipmentSlots`, `WeaponCharges`, `DaggerCharges`, `CurrentPlayerHealth` are *one* player's in this
  format. Writing the peer's inventory over them destroys the host's character. The peer's
  player-state therefore needs its **own** container (its own `WorldN.sav` slot, or a sidecar file
  owned by the mod).
* **Save points must be host-triggered.** A peer saving mid-combat would snapshot a world the host
  does not agree with. Peer save requests become a host action.
* The `_backup` twins mean a partial write produces a *recoverable* but *inconsistent* pair — the mod
  must write primary and backup together or leave both alone.

---

## 7. Authority model (one page)

```
                    ┌──────────────────────────────┐
   authoritative    │  HOST                        │
   for:             │  · enemy AI, damage, death   │
                    │  · loot rolls, item claims   │
                    │  · NamedSaveData writes      │
                    │  · one-shot world events     │
                    │  · level travel + snapshots  │
                    │  · checkpoints / saves       │
                    └───────────┬──────────────────┘
                                │  snapshots + events (reliable)
                                │  entity rows (unreliable, tiered)
                    ┌───────────▼──────────────────┐
   authoritative    │  EACH CLIENT (its own pawn)  │
   for:             │  · transform, view angles    │
                    │  · vitals, stamina, charges  │
                    │  · its own inventory         │
                    │  · its own UI, subtitles,    │
                    │    death screen, camera      │
                    │  · its own local dialogue    │
                    └──────────────────────────────┘
```

**Invariants that must hold:**

1. **One damage applier** (the host). No exceptions; not even "just for the peer's own target".
2. **One loot roller** (the host). The peer never calls the drop system.
3. **One writer of `NamedSaveData`** (the host).
4. **Absolute values on the wire; idempotent apply.** Never deltas, never "apply twice".
5. **Owner-authoritative locomotion**, host-corrected only on gross violation (>2 m discrepancy or
   through-geometry).
6. **UI is never replicated.** A replicated widget is a bug in every co-op mod that has ever shipped.
7. **Every discrete message carries a monotone `generation`** so a stale travel/resync is a no-op.

**Add a fencing token (the dollsMP §8.14 lesson).** A monotone `progression_revision` incremented on
every host-side flag write, plus a `flagmap_hash` (FNV-1a over canonically sorted keys) sent
periodically. If the peer's hash disagrees for two consecutive beats, the host re-sends the full map.
This is cheap (20 B) and it is the difference between "drift is corrected in 2 s" and "drift is
discovered an hour later".

---

## 8. Bandwidth estimates

Reference points borrowed from the dollsMP corpus (same genre, same engine generation, measured on a
real rig) rather than invented: a player record is **141 B at 60 Hz ≈ 8.5 KB/s**, an enemy row is
**~19 B at 10 Hz**, world interaction is **< 150 B/s event-driven**, progression is **≤ 0.26 KB/s**,
and the whole boss-fight total was **≈13.5 KB/s both directions**. Those numbers are the sanity
envelope; Queens Domain has a comparable or smaller actor count (414 enemy *records* across the whole
game, not 414 live actors).

| lane | rate | bytes/s | note |
|---|---|---|---|
| Player transform + view (own pawn) | 25 Hz × 32 B | 0.8 KB/s | ×2 players, host relays |
| Player representation (peer body) | 10 Hz × 40 B + events | 0.4 KB/s | montage events ~8 B |
| Player vitals | 10 Hz × 24 B | 0.24 KB/s | dirty-flag; idle players ≈ 0 |
| Inventory / equipment / charges | event | < 0.1 KB/s | burst on pickup |
| Enemy transform + anim (12 live @ 10 Hz) | 12 × 19 B × 10 | 2.3 KB/s | tier B reduces to 2 Hz beyond 30 m |
| Enemy lifecycle + damage + actions | event | < 0.3 KB/s | burst in a fight |
| Quest flags | event | < 0.05 KB/s | ~1 write per 30 s of play |
| Loot / pickups / containers | event | < 0.1 KB/s | |
| Interactables / doors / checkpoints | event | < 0.05 KB/s | |
| Dialogue | event + 2 Hz × 8 B | < 0.05 KB/s | |
| Level travel | event + snapshot | burst 2–4 KB | once per transition |
| **Steady-state total, both directions** | | **≈ 4–5 KB/s** | **≈ 40 kbit/s** |
| **Combat worst case (20 live enemies)** | | **≈ 9 KB/s** | **≈ 72 kbit/s** |

**The binding constraint is not bandwidth; it is the game-thread callback budget.** dollsMP measured
`~39–43 callbacks/s` as the wall and had to coalesce every high-rate lane into a single per-tick
batch. The design consequence is the same here: **one drain per tick, all lanes coalesced into one
datagram**, and the enemy lane **tiered** (10 Hz engaged → 2 Hz peripheral → 0 when off-screen) rather
than flat. A flat "send every enemy every tick" model over-states this lane by roughly 7×, which is
exactly the error the dollsMP corpus found and corrected in itself.

Two caveats on these numbers, stated as caveats: (a) they assume the peer runs enemies as puppets, so
no AI state is transmitted — if AI state had to be replicated the enemy lane would be several times
larger; (b) the 414 `EnemySaveStates` records are *persistent* state, not live actors — they belong
in the join/travel snapshot (≈ 414 × 20 B ≈ 8 KB raw, ≈ 2–3 KB deflated), not in the per-tick budget.

---

## 9. Open questions

> **Superseded by `10-gaps-register.md` (50 items, all CLOSED).** This section is the
> original independent pass, kept for provenance. Status against it: **Q5** dialogue does
> **not** pause (closed; `13b`/`16`). **Q6** enemies are spawner-driven with authored GUIDs
> (closed; D4/`18`). **Q8** Steam is non-functional in this install (closed; E3). **Q2/Q3/Q4**
> are settled by C1/C5/C6. **Q9** is moot for the puppet design: the remote player is an
> actor copy, not a second `PlayerController`. **Q7** (peer inventory) is a design decision,
> not a measurement. **Q1** (does death reload the level) was only observed, not
> instrumented: Game Over → Continue respawned at the checkpoint; that remains the one
> verification item this list still owns.

1. **Does player death reload the level?** *Blocks:* the entire death/respawn model and whether the
   survivor is dragged along. *Test:* die once with the mod's reload hook installed; check
   `SaveLevel`, `LastTouchedCheckpoint`, and whether the menu opens after respawn (dollsMP F-012
   showed the failing-to-open menu was the decisive symptom). *Changes:* if yes, the mod must
   suppress the reload and promote the game's own respawn; if no, death sync is a 12-byte event.
2. **What are the 7 `SavableActorStates` GUIDs?** *Blocks:* knowing whether that map is "chests" or
   "all savable actors", which sets the size of the one-shot channel. *Test:* runtime actor dump
   printing `ActorGuid` for every actor in `Ruins_Level_00`, joined against the save. *Changes:* if
   the map is generic, one-shot sync is 20 B/actor with no manifest; if it is chest-specific, chests
   get their own lane.
3. **Is `UFS_SaveGame` really the C++ base of `FS_SaveSlot_C`?** *Blocks:* whether the 30 property
   names are stable `UPROPERTY`s (safe to hook by name) or Blueprint-added variables (which can be
   renamed by a content patch). *Test:* UE4SS reflection dump of `FS_SaveSlot_C`'s class hierarchy
   and property list. *Changes:* if Blueprint, the mod must bind by index/offset with a version check.
4. **Which `NamedSaveData` keys are counters, and which are enums?** *Blocks:* correctness of
   replication mode for every flag. *Test:* play through one gate with the change notification
   logged; observe `EFS_NamedSaveDataChangeMode` per write. *Changes:* shrinks `flags.json` to only
   the genuinely ambiguous keys.
5. **Does the game pause the world during dialogue, and can that pause be suppressed without breaking
   the sequence?** *Blocks:* the local/global dialogue split. *Test:* open a conversation and read
   `UGameplayStatics::IsGamePaused` on the *other* process while the first is talking.
6. **Are enemies level-placed or spawner-driven in the open world?** The exe has both
   `UFS_SpawnerInterface` + `EFS_SpawnerUpdateMode` *and* `PlayerDistanceDespawnCheck`. *Blocks:*
   whether enemy identity is stable enough to use the game's GUID as the wire ID in every area.
   *Test:* actor dump in two different areas, compare GUID stability across a reload.
7. **Does the peer's inventory need to be real, or can the mod keep it?** The save format has one
   inventory. *Blocks:* the shape of the player-state container. *Test:* determine whether
   `InventoryContents` is read from a component that could be duplicated per pawn.
8. **Is `steam_api64.dll` intended to ship?** The manifest lists it; the install does not have it.
   *Blocks:* whether Steam networking is a viable transport. *Test:* check the distribution the user
   actually owns for the DLL.
9. **How does the game handle a second pawn?** There is no evidence of multi-player support at all
   (`PlayerController` count, `PlayerArray`). *Blocks:* everything. *Test:* spawn a second
   `APlayerController`/pawn via UE4SS and see whether `UFS_GameInstanceBase` tolerates it. This is
   the single highest-risk unknown in the whole design and should be probed before any sync work.

---

## 10. What was verified vs. what was inferred

To keep the record honest:

**Verified by parsing a real file:** the save location, the three save classes, the full `World0`
property tree (30 properties, exact byte fit), all 11 `NamedSaveData` values, all 7
`SavableActorStates` values, all 414 `EnemySaveStates` records, both `Challenges` entries, the
`System0` content, the header layout including the `FEngineVersion` block and the length-includes-NUL
convention, the 51 custom-version GUIDs.

**Verified by reading the shipping binary's string pool:** every `FS_`/`UFS_`/`EFS_`/`S_`/`QD_` type
name quoted, every delegate name, every enum name, the absence of game replication.

**Verified by reading the pak directory index:** the 51 level names, the Blueprint folder taxonomy,
the `Items/KeyItems` families, the NPC roster, the 44,092-entry count and pak version 11.

**Inferred (labelled CANDIDATE above):** the parent/child relationship of `UFS_SaveGame` and
`FS_SaveSlot_C`; the class attribution of §3.2 property names; the assignment of `SavableActorStates`
to chests; the counter-vs-enum classification of individual flags; all bandwidth figures (modelled
from the dollsMP measured envelope, not measured on this game).

**Not attempted / not possible here:** a runtime reflection dump (no UE4SS install on this game), a
decompressed level package, or a second-machine test. Those are the three things that would convert
the largest remaining CANDIDATE block into CONFIRMED, and they are named as open questions rather
than papered over.
