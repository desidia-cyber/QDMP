# Task 3 — the four questions, with evidence

Every claim points at the artefact that backs it. "Not present" claims name the search that was run
and the corpus it was run over (44,092 decoded paths, or the full 1.97 GB of payload bytes).

---

## Q1 — Does quest state live in a DataTable, in the save, or in Blueprint variables?

**Entirely in Blueprint variables, persisted through the save. There is no DataTable in the game at
all, and quest state is not a row, a struct field, or a data asset — it is an integer on a
`SavableActor` blueprint, mirrored into `FS_SaveSlot.NamedSaveData`.**

Evidence, in order of strength:

1. **Zero DataTable assets.** Path filter `/DataTable`, `DataTables/`, `DT_` over 44,092 paths → 0.
   Whole-pak byte search `RowHandle` → 0, `StructRowHandle` → 0. Full detail and the other searches
   in `datatables.md`.
2. **`ActorQuestState` is an `Actor` subclass whose job is to show/hide world actors**, not to store
   state:
   ```
   ActorQuestState_C   parent: Actor
   UFUNCTIONs: SetActive, SetInactive, SetActorsState, GetActors
   properties: ActorsToShow, ChildNodes, CurActor, Disable, FadeIn, FadeOut, WorldStateChanged
   imports:    QuestStateInterface_C, NPC_Ghost_Base_C, EnemySpawner_C
   ```
   It *reads* a state and reacts; nothing here is a definition table.
3. **`ActorQuestStateManager` is where the state actually lives, and it is a `SavableActor`:**
   ```
   ActorQuestStateManager_C   parent: SavableActor_C
   UFUNCTIONs: ForceSyncUpdate, ForceRefresh, RefreshStates, UpdateDesiredState,
               ReceiveBeginPlay, "Restore State From Save Data"
   properties: States, DesiredState, InternalState, StateSaveDataname, "Quest State NSD",
               LastTouchedCheckpoint, NoRefreshAreas, GetCurrentlyActiveSave, GetNamedSaveData,
               FS_SaveSlot_C, SaveHelper_C, SaveFunctions_C, SavableActorState
   ```
   `States` / `DesiredState` / `InternalState` are the quest state; `StateSaveDataname` is the
   `FName` under which it is written into the save; `"Quest State NSD"` is an
   `ActivatableNamedSaveDataSetter` handle.
4. **The save slot declares the persistence containers** — `FS_SaveSlot_C`, parent `FS_SaveGame`,
   89 property names, including:
   ```
   NamedSaveData            MapProperty  (Name -> Int)
   SavableActorStates       MapProperty  (Struct -> SavableActorState)
   SavePointsVisited, UnlockedTeleporters, LastTouchedCheckpoint, SavedArea, SaveLevel,
   SaveLocation, SavePointID, CompletedChallenges, Inventory, EquippedItems, WeaponLevels,
   WeaponUpgrades, WeaponXP, MaxProgressionLevel, ShopPurchases, ShopTeleporterDestinations, ...
   ```
5. **The save file itself proves the shape.** `analysis/save-backup-before-live/World0.sav` begins
   `GVAS` / `++UE4+Release-4.27` and its first object is
   `/Game/Blueprints/SaveSystem/FS_SaveSlot.FS_SaveSlot_C`, then `SavableActorStates` as a
   `MapProperty` of `StructProperty`, then `NamedSaveData` as a `MapProperty` with `NameProperty`
   keys and `IntProperty` values. So: state → `SavableActor` variable → `NamedSaveData[Name] = int`.

**Mod consequence:** there is no table to patch and no row to add. Quest state is per-actor Blueprint
memory; a co-op mod has to synchronise the *values* (`States`, `DesiredState`, `InternalState` and
the `NamedSaveData` entries), not the definitions.

---

## Q2 — The exact key strings used by `NamedSaveData` and `ChallengeStats`

### Where the keys come from

`NamedSaveData` is a `MapProperty` on `FS_SaveSlot_C` (parent native class `FS_SaveGame`). Its keys
are `FName`s authored by hand in content, in three places:

**a) `NamedSaveDataSetter_Config`** — a `UserDefinedStruct` (21 names) that describes one key write:

```
NamedSaveDataSetter_Config          (the struct)
  Data_4_10DEF34A498700139CB3D7B3A90C55AD                 BoolProperty   <- the value
  Mode_5_5FE0660A4ED4C24C701F20859BDB7255                 NamedSaveDataMode (enum)
  ImmediatelyRefreshQuestState_9_001CAB7C43B144457811AAA6F6A151DE   BoolProperty
NamedSaveDataMode  (UserDefinedEnum)
  NamedSaveDataMode::NewEnumerator0 ... NamedSaveDataMode::NamedSaveDataMode_MAX
```

**b) The volumes that apply it** — `NamedSaveDataSetterVolume` (301 names) and
`ActivatableNamedSaveDataSetter` (91 names), both holding an array `NSDs` of
`NamedSaveDataSetter_Config` and calling `SaveNamedData_Advanced` / `SaveNamedData`. Their
writing functions are `Activate` / `Deactivate` / `ActivateByLoad` / `DeactivateByLoad`.

**c) The owning blueprint's own literal** — the `FName` is a plain string in the blueprint that
triggers the write. This is why a whole-pak byte search finds them:

| key string | file it is authored in | hit |
|---|---|---|
| `Arjaot_State` | `QueensDomain/Content/NPCs/Knight/Knight_Arjaot/Knight_Arjaot_Dialogue_00.uasset` | 1 |
| `BrokeBarricade` | `QueensDomain/Content/Blueprints/BreakableBarricade/BreakableBarricade.uasset`; also `NPCs/Knight/Knight_Ekonta/Knight_Ekonta_Dialogue_00.uasset` | 1 + 1 |
| `CheckpointUnlockCount` | `Blueprints/Challenges/FS_ChallengeData.uasset`; also `LD45/Blueprints/Checkpoint/Checkpoint.uasset` | 1 + 1 |
| `TotalAchievementWeapons` | `Blueprints/Challenges/FS_ChallengeData.uasset`; also `Player/FS_PlayerChar.uasset` | 1 + 1 |
| `RustyBrokeBarricade` (as a single literal) | **0 hits** — see the composition rule below | 0 |

### The composition rule (why `RustyBrokeBarricade` is not a literal)

Keys are built as **`<instance or actor name>` + `<property/state name>`**. The save file shows the
two shapes side by side:

```
global stats : TotalAchievementTreasures  CompletedChallenges  IntroMusicState
               TotalAchievementWeapons    FullEnergyCellsCount  CheckpointUnlockCount
               PlayingMusicTrack
per-actor    : Arjaot_State               RustySwordKnightState
               RustySwordKnightGaveSword  BrokeBarricade
               TiniScytheHammerAP
```

`TiniScytheHammerAP` is `Tini` (an actor) + `ScytheHammerAP` (a property), and
`RustySwordKnightState` is `RustySwordKnight` + `State`. So `RustyBrokeBarricade` decomposes as
`Rusty` + `BrokeBarricade`, which is exactly consistent with the two literals the pak does contain:
`BrokeBarricade` lives in `BreakableBarricade.uasset`, and the prefix `Rusty` is the barricade
instance's name in the level. `RustyBrokeBarricade` is not in the supplied `World0.sav`; that save
has `BrokeBarricade` unprefixed and a different barricade instance.

### ChallengeStats — the exact field names

The "challenge stats" are not a separate table; they are **property names on
`Blueprints/Challenges/FS_ChallengeData.uasset`** (121 names) and on
`Player/FS_PlayerChar.uasset`. The complete achievement/stat key surface, verbatim:

```
ChallengeDefinitions   CompletedChallenges   CompleteConditionMode   EFS_ChallengeMode
EFS_ChallengeMode::Manual            EFS_ChallengeMode::Stat_AutoInventory
EFS_ChallengeMode::Stat_AutoItemTag  EFS_ChallengeMode::Stat_AutoNSD
ID  ImportedName  ImportedDescription  ImportedSort  LockedIcon  bActivityTask
bForceConditionIsNotStatTotal  NSDCondition  InventoryCondition  ItemTagCondition

cha_All_Complete
cha_Area_Aqr  cha_Area_Aut  cha_Area_Castle  cha_Area_Forest  cha_Area_Lib
cha_Area_Maus  cha_Area_Prison  cha_Area_ROS  cha_Area_SG  cha_Area_Swamp
cha_Bosses_BridgeLord  cha_Bosses_Demons  cha_Bosses_DragonLord  cha_Bosses_GiantFish
cha_Bosses_Necro  cha_Bosses_RedLords  cha_Bosses_RoSDragon  cha_Bosses_Whale
cha_Challenge_AllChests  cha_Challenge_AllSecretDoors  cha_Challenge_AllTeleporters
cha_Endings
cha_Item_AllDashGems  cha_Item_EnergyWep  cha_Item_FlyingSword  cha_Item_GotAllAcc
cha_Item_GotAllCells  cha_Item_GotAllLenses  cha_Item_GotAllThrowables  cha_Item_GotAllTreasures
cha_Item_GotAllWeps  cha_Item_SecretSword  cha_Item_SpecSword  cha_Item_UltraGem
cha_Item_UpgradeAllWeps
cha_Quest_Books  cha_Quest_Chef  cha_Quest_Fish  cha_Quest_Grimoire  cha_Quest_Lib
cha_Quest_Meat  cha_Quest_Mushroom  cha_Quest_PG_Puzzle  cha_Quest_Raider
cha_Quest_Science  cha_Quest_Shop_Spend  cha_Quest_Shop_Unlocks  cha_Quest_Skulls
cha_Quest_Vaults

stat fields:
CheckpointUnlockCount  DemonBossesKilled  DragonLordDead  RedLordsDead  ROSDragonDead
GiantSwampFish_IsDead  NecronimiconDefeated  NecroWall00Destroyed .. NecroWall04Destroyed
BridgeLordDead  EndingSeen  FishWifeTotalEggs  FullEnergyCellsCount  HasEverInscribedGrimoire
LibrarianBestiaryCompletionPercent  LibBookPuzzleComplete  ROSBookPuzzleComplete
MealTicketsCollected  RaiderNeckacesTurnedIn  ScientistDataGiven  ShopAmountSpent
ShopkeepUnlock1  ShopkeepUnlock2

item tags:
ItemTags.AchievementAccessory  ItemTags.AchievementThrowables  ItemTags.EnergyWeapon
```

Note `EFS_ChallengeMode::Stat_AutoNSD` — an achievement can be auto-earned from a
**NamedSaveData** key. Achievements and NamedSaveData are the same mechanism, which is why
`CheckpointUnlockCount` appears both as a `NamedSaveData` key in the save and as a property on
`FS_ChallengeData`/`Checkpoint`.

---

## Q3 — How many teleport destinations / save points, and their object paths

Counted from the **export tables of all 51 `.umap` files** (`code/maps.py`; 51/51 maps parsed
successfully). Each placed actor is one export, so these are real placements, not asset names.

### Teleporters — 43 placed actors, 4 classes

| class | all maps | shipping world only |
|---|---|---|
| `Teleporter_C` (main fast-travel node) | 21 | **10** |
| `TeleporterFragment_Pickup_BP_C` (fragments that unlock one) | 16 | 15 |
| `MiniTeleporter_C` (incl. `ShopTeleportDestination`) | 4 | 3 |
| `SecretTeleporterDestination_C` | 2 | 1 |
| **total** | **43** | **29** |

"Shipping world only" excludes `LD45/Maps/BirdsTestZone` (developer test map, 12 teleporters),
`UI/Menus/OptionsMenu/VideoConfigMap` (UI preview map, 1) and the unplaced/live-edit actor
`TempTeleporter` in the persistent `Ruins_Level_00` (1).

**So there are 10 real fast-travel destinations**, which lines up exactly with the 10 area
teleporter blueprint classes (`Ruins_`, `Aquatic_`, `Library_`, `Mausoleum_`, `Autumn_`, `Castle_`,
`Forest_`, `StoneGarden_`, `Swamp_`, `ROS_Teleporter`), and with the 10 areas in the
`cha_Area_*` achievement set. The other 19 are the collectible fragments that unlock them
(15), the shop mini-teleport pads (3) and one secret destination.

Cross-check: the achievement `cha_Challenge_AllTeleporters` exists, and `FS_SaveSlot` has
`UnlockedTeleporters` and `ShopTeleporterDestinations` — so the game tracks unlocked destinations by
name, and the destination data type is `Blueprints/Teleporter/TeleporterData` (1 type asset; the
per-destination data is instanced inline on each placed actor, not separate assets).

### Save points — 34 placed `Checkpoint_C` actors (31 outside dev/UI maps)

`28` of them also have a dedicated blueprint asset `Blueprints/Areas/<Area>/<Name>_Save.uasset`
(plus 4 lowercase `_save`: `GRSZ_save`, `Swamp_1_save`, `Swamp_2_save`, `Swamp_3_save`).
`FS_SaveSlot` tracks them with `SavePointsVisited`, `LastTouchedCheckpoint`, `SavePointID`.

Note the naming collision that matters: the `_Save` **assets** (28) are not the same set as the
`Checkpoint_C` **placements** (34). The placements are the runtime objects; the `_Save` assets are
the per-area save-point definitions.

---

## Q4 — Is there any per-player or host/client concept anywhere in the content?

**No. There is no per-player, host, client, ownership or replication concept in any game asset. The
shipped content is single-player-only; the networking that exists is engine/plugin plumbing that the
game never uses.**

Searches run with `code/scan_strings.py` over the **whole 1.97 GB pak** (every payload byte plus
every name table):

| term | hits | where | verdict |
|---|---|---|---|
| `PlayerIndex` | **0** | — | absent |
| `NetOwner` | **0** | — | absent |
| `NetMulticast` | **0** | — | absent |
| `HasAuthority` | **0** | — | absent |
| `GameStateBase` | **0** | — | absent |
| `ClientTravel` | **0** | — | absent |
| `Replicate` | 33 | **only** `.umap` files (one hit each) + `AssetRegistry.bin`; context is `bReplicates` / `NumReplicatedProperties` in the Engine actor/component name tables | engine default flags, not game logic |
| `LocalPlayer` | 19 | `Blueprints/FS_GameInstance`, `UI/Menus/Journal/Guides_Menu`, `UI/Menus/OptionsMenu`, `UI/UI_Style/FS_Button`, `UI/Widgets/Core/InputViewIcon`, `Config/DefaultEngine.ini` | UI input routing (`GetLocalPlayer` for widgets) — no gameplay role |
| `PlayerState` | 3 | `Engine/Content/Localization/Engine/*/Engine.locres` only | engine text |
| `ServerTravel` | 3 | `Engine/Config/BaseEngine.ini`, `BaseInput.ini` | engine config |
| `NetConnection` | 9 | `Engine/Config/BaseEngine.ini` (7) + `QueensDomain/Config/DefaultEngine.ini` (2) | config only |

Positive evidence that the architecture is single-player:

* **One save slot object with no player identity.** `FS_SaveSlot_C` has `SaveVersion`,
  `SaveLevel`, `SaveLocation`, `SavedArea`, `LastSaveTime`, `PlayTime` — and nothing like
  `PlayerId`, `PlayerName`, `SlotIndex`, or `bIsHost`.
* **One player character blueprint**, `Player/FS_PlayerChar` (with `FS_PlayerChar_C` referenced
  from the save-time stat block `TotalAchievementWeapons`, `TotalDamage`, `TotalHitMultiplier`), and
  no second player class.
* **Global singletons, not per-player state**: `FS_GameInstance` holds `NamedSaveData`,
  `GetNamedSaveData`, `MusicController`; `ActorQuestStateManager` is a single actor in the world with
  `ForceSyncUpdate` / `RefreshStates`.
* **No RPC-shaped functions.** No `Server_*`, `Client_*` or `*_Implementation` function exists in
  any blueprint export list (`code/build_reports.py` enumerates every `Function`-class export in the
  surveyed assets; none matches).
* **The `OnlineSubsystem*` + `SteamBridge` plugins ship** (`Engine/Plugins/Online/...`) and
  `DefaultEngine.ini` contains `[OnlineSubsystemSteam] bEnabled=true` and a `NetConnection` block —
  but that is the *engine's* default online-session layer being present, not the game using it.
  `OnlineSubsystemSteam`'s `.locres` files are engine text; no game asset references a session.

**Mod consequence:** there is no existing host/client split to piggyback on. Co-op must introduce
the concept from scratch — a session layer, a second player puppet (the body art exists —
`Player/Assets/FPModel/Player2026/Player_FullChar_2026`), and a decision on which of the two
players' `FS_SaveSlot` is authoritative. The good news is that the *state* to synchronise is already
narrow and identified: `NamedSaveData` (Name→Int), `SavableActorStates`, `States`/`DesiredState`/
`InternalState` on `ActorQuestStateManager`, plus `Inventory`/`EquippedItems`/`WeaponLevels`.

---

## Appendix — teleporter object paths (43)

```
CLASS                          ACTOR NAME                     OBJECT PATH
MiniTeleporter_C               ShopTeleportDestination        /Game/LD45/Maps/BirdsTestZone.BirdsTestZone:PersistentLevel.ShopTeleportDestination
SecretTeleporterDestination_C  SecretTeleporterDestination    /Game/LD45/Maps/BirdsTestZone.BirdsTestZone:PersistentLevel.SecretTeleporterDestination
Teleporter_C                   Teleporter10                   /Game/LD45/Maps/BirdsTestZone.BirdsTestZone:PersistentLevel.Teleporter10
Teleporter_C                   Teleporter11                   /Game/LD45/Maps/BirdsTestZone.BirdsTestZone:PersistentLevel.Teleporter11
Teleporter_C                   Teleporter2                    /Game/LD45/Maps/BirdsTestZone.BirdsTestZone:PersistentLevel.Teleporter2
Teleporter_C                   Teleporter3                    /Game/LD45/Maps/BirdsTestZone.BirdsTestZone:PersistentLevel.Teleporter3
Teleporter_C                   Teleporter4                    /Game/LD45/Maps/BirdsTestZone.BirdsTestZone:PersistentLevel.Teleporter4
Teleporter_C                   Teleporter5                    /Game/LD45/Maps/BirdsTestZone.BirdsTestZone:PersistentLevel.Teleporter5
Teleporter_C                   Teleporter6                    /Game/LD45/Maps/BirdsTestZone.BirdsTestZone:PersistentLevel.Teleporter6
Teleporter_C                   Teleporter7                    /Game/LD45/Maps/BirdsTestZone.BirdsTestZone:PersistentLevel.Teleporter7
Teleporter_C                   Teleporter8                    /Game/LD45/Maps/BirdsTestZone.BirdsTestZone:PersistentLevel.Teleporter8
Teleporter_C                   Teleporter9                    /Game/LD45/Maps/BirdsTestZone.BirdsTestZone:PersistentLevel.Teleporter9
Teleporter_C                   Teleporter_GR                  /Game/RuinsLevel/Maps/100_GR.100_GR:PersistentLevel.Teleporter_GR
MiniTeleporter_C               ShopTeleportDestination        /Game/RuinsLevel/Maps/140_GR_ShopIsleOnly.140_GR_ShopIsleOnly:PersistentLevel.ShopTeleportDestination
Teleporter_C                   Teleporter_Aqr                 /Game/RuinsLevel/Maps/200_AQ.200_AQ:PersistentLevel.Teleporter_Aqr
TeleporterFragment_Pickup_BP_C TeleporterFragment_Pickup_BP2  /Game/RuinsLevel/Maps/210_AQ_underground.210_AQ_underground:PersistentLevel.TeleporterFragment_Pickup_BP2
TeleporterFragment_Pickup_BP_C TeleporterFragment_Pickup_BP   /Game/RuinsLevel/Maps/300_LIB.300_LIB:PersistentLevel.TeleporterFragment_Pickup_BP
Teleporter_C                   Teleporter_Lib                 /Game/RuinsLevel/Maps/310_LIB_exterior.310_LIB_exterior:PersistentLevel.Teleporter_Lib
TeleporterFragment_Pickup_BP_C TeleporterFragment_Pickup_BP3  /Game/RuinsLevel/Maps/320_LIB_basement.320_LIB_basement:PersistentLevel.TeleporterFragment_Pickup_BP3
Teleporter_C                   Teleporter_Maus                /Game/RuinsLevel/Maps/420_MAUS_mid.420_MAUS_mid:PersistentLevel.Teleporter_Maus
Teleporter_C                   Teleporter_AI                  /Game/RuinsLevel/Maps/500_AUT.500_AUT:PersistentLevel.Teleporter_AI
TeleporterFragment_Pickup_BP_C TeleporterFragment_Pickup_BP2  /Game/RuinsLevel/Maps/500_AUT.500_AUT:PersistentLevel.TeleporterFragment_Pickup_BP2
TeleporterFragment_Pickup_BP_C TeleporterFragment_Pickup_BP3  /Game/RuinsLevel/Maps/500_AUT.500_AUT:PersistentLevel.TeleporterFragment_Pickup_BP3
Teleporter_C                   Teleporter                     /Game/RuinsLevel/Maps/600_Castle.600_Castle:PersistentLevel.Teleporter
TeleporterFragment_Pickup_BP_C TeleporterFragment_Pickup_BP2  /Game/RuinsLevel/Maps/600_Castle.600_Castle:PersistentLevel.TeleporterFragment_Pickup_BP2
TeleporterFragment_Pickup_BP_C TeleporterFragment_Pickup_BP   /Game/RuinsLevel/Maps/600_Castle.600_Castle:PersistentLevel.TeleporterFragment_Pickup_BP
TeleporterFragment_Pickup_BP_C TeleporterFragment_Pickup_BP2  /Game/RuinsLevel/Maps/710_SZ_AQ.710_SZ_AQ:PersistentLevel.TeleporterFragment_Pickup_BP2
TeleporterFragment_Pickup_BP_C TeleporterFragment_Pickup_BP   /Game/RuinsLevel/Maps/740_SZ_Prison.740_SZ_Prison:PersistentLevel.TeleporterFragment_Pickup_BP
Teleporter_C                   Teleporter_ROS                 /Game/RuinsLevel/Maps/760_SZ_AUT.760_SZ_AUT:PersistentLevel.Teleporter_ROS
TeleporterFragment_Pickup_BP_C TeleporterFragment_Pickup_BP7  /Game/RuinsLevel/Maps/760_SZ_AUT.760_SZ_AUT:PersistentLevel.TeleporterFragment_Pickup_BP7
TeleporterFragment_Pickup_BP_C TeleporterFragment_Pickup_BP   /Game/RuinsLevel/Maps/760_SZ_AUT.760_SZ_AUT:PersistentLevel.TeleporterFragment_Pickup_BP
Teleporter_C                   Teleporter_Swamp               /Game/RuinsLevel/Maps/770_SZ_Swamp.770_SZ_Swamp:PersistentLevel.Teleporter_Swamp
TeleporterFragment_Pickup_BP_C TeleporterFragment_Pickup_BP6  /Game/RuinsLevel/Maps/770_SZ_Swamp.770_SZ_Swamp:PersistentLevel.TeleporterFragment_Pickup_BP6
Teleporter_C                   Teleporter_Forest              /Game/RuinsLevel/Maps/780_SZ_Depths.780_SZ_Depths:PersistentLevel.Teleporter_Forest
TeleporterFragment_Pickup_BP_C TeleporterFragment_Pickup_BP3  /Game/RuinsLevel/Maps/780_SZ_Depths.780_SZ_Depths:PersistentLevel.TeleporterFragment_Pickup_BP3
TeleporterFragment_Pickup_BP_C TeleporterFragment_Pickup_BP4  /Game/RuinsLevel/Maps/780_SZ_Depths.780_SZ_Depths:PersistentLevel.TeleporterFragment_Pickup_BP4
TeleporterFragment_Pickup_BP_C TeleporterFragment_Pickup_BP5  /Game/RuinsLevel/Maps/780_SZ_Depths.780_SZ_Depths:PersistentLevel.TeleporterFragment_Pickup_BP5
MiniTeleporter_C               MiniTeleporter2                /Game/RuinsLevel/Maps/790_SG.790_SG:PersistentLevel.MiniTeleporter2
SecretTeleporterDestination_C  SecretTeleporterDestination2   /Game/RuinsLevel/Maps/790_SG.790_SG:PersistentLevel.SecretTeleporterDestination2
Teleporter_C                   Teleporter_SG                  /Game/RuinsLevel/Maps/790_SG.790_SG:PersistentLevel.Teleporter_SG
MiniTeleporter_C               MiniTeleporter                 /Game/RuinsLevel/Maps/800_FinalZone.800_FinalZone:PersistentLevel.MiniTeleporter
Teleporter_C                   TempTeleporter                 /Game/RuinsLevel/Maps/Ruins_Level_00.Ruins_Level_00:PersistentLevel.TempTeleporter
TeleporterFragment_Pickup_BP_C TeleporterFragment_Pickup_BP4  /Game/UI/Menus/OptionsMenu/VideoConfigMap.VideoConfigMap:PersistentLevel.TeleporterFragment_Pickup_BP4
```

## Appendix — save-point object paths (34)

```
CLASS                          ACTOR NAME                     OBJECT PATH
Checkpoint_C                   Checkpoint2                    /Game/LD45/Maps/BirdsTestZone.BirdsTestZone:PersistentLevel.Checkpoint2
Checkpoint_C                   Checkpoint                     /Game/LD45/Maps/BirdsTestZone.BirdsTestZone:PersistentLevel.Checkpoint
Checkpoint_C                   Checkpoint17                   /Game/RuinsLevel/Maps/100_GR.100_GR:PersistentLevel.Checkpoint17
Checkpoint_C                   Checkpoint7                    /Game/RuinsLevel/Maps/100_GR.100_GR:PersistentLevel.Checkpoint7
Checkpoint_C                   Checkpoint3                    /Game/RuinsLevel/Maps/200_AQ.200_AQ:PersistentLevel.Checkpoint3
Checkpoint_C                   Checkpoint4                    /Game/RuinsLevel/Maps/210_AQ_underground.210_AQ_underground:PersistentLevel.Checkpoint4
Checkpoint_C                   Checkpoint5                    /Game/RuinsLevel/Maps/210_AQ_underground.210_AQ_underground:PersistentLevel.Checkpoint5
Checkpoint_C                   Checkpoint8                    /Game/RuinsLevel/Maps/210_AQ_underground.210_AQ_underground:PersistentLevel.Checkpoint8
Checkpoint_C                   Checkpoint9                    /Game/RuinsLevel/Maps/210_AQ_underground.210_AQ_underground:PersistentLevel.Checkpoint9
Checkpoint_C                   Checkpoint13                   /Game/RuinsLevel/Maps/300_LIB.300_LIB:PersistentLevel.Checkpoint13
Checkpoint_C                   Checkpoint2                    /Game/RuinsLevel/Maps/300_LIB.300_LIB:PersistentLevel.Checkpoint2
Checkpoint_C                   Checkpoint6                    /Game/RuinsLevel/Maps/300_LIB.300_LIB:PersistentLevel.Checkpoint6
Checkpoint_C                   Checkpoint10                   /Game/RuinsLevel/Maps/400_MAUS_entrance.400_MAUS_entrance:PersistentLevel.Checkpoint10
Checkpoint_C                   Checkpoint11                   /Game/RuinsLevel/Maps/420_MAUS_mid.420_MAUS_mid:PersistentLevel.Checkpoint11
Checkpoint_C                   Checkpoint2                    /Game/RuinsLevel/Maps/450_MAUS_darkmaze.450_MAUS_darkmaze:PersistentLevel.Checkpoint2
Checkpoint_C                   Checkpoint12                   /Game/RuinsLevel/Maps/500_AUT.500_AUT:PersistentLevel.Checkpoint12
Checkpoint_C                   Checkpoint3                    /Game/RuinsLevel/Maps/500_AUT.500_AUT:PersistentLevel.Checkpoint3
Checkpoint_C                   Checkpoint4                    /Game/RuinsLevel/Maps/500_AUT.500_AUT:PersistentLevel.Checkpoint4
Checkpoint_C                   Checkpoint15                   /Game/RuinsLevel/Maps/510_AUT_droptunnel.510_AUT_droptunnel:PersistentLevel.Checkpoint15
Checkpoint_C                   Checkpoint14                   /Game/RuinsLevel/Maps/600_Castle.600_Castle:PersistentLevel.Checkpoint14
Checkpoint_C                   Checkpoint2                    /Game/RuinsLevel/Maps/600_Castle.600_Castle:PersistentLevel.Checkpoint2
Checkpoint_C                   Checkpoint                     /Game/RuinsLevel/Maps/600_Castle.600_Castle:PersistentLevel.Checkpoint
Checkpoint_C                   Checkpoint                     /Game/RuinsLevel/Maps/700_SZ_GR.700_SZ_GR:PersistentLevel.Checkpoint
Checkpoint_C                   Checkpoint2                    /Game/RuinsLevel/Maps/740_SZ_Prison.740_SZ_Prison:PersistentLevel.Checkpoint2
Checkpoint_C                   Checkpoint                     /Game/RuinsLevel/Maps/740_SZ_Prison.740_SZ_Prison:PersistentLevel.Checkpoint
Checkpoint_C                   Checkpoint12                   /Game/RuinsLevel/Maps/760_SZ_AUT.760_SZ_AUT:PersistentLevel.Checkpoint12
Checkpoint_C                   Checkpoint23                   /Game/RuinsLevel/Maps/760_SZ_AUT.760_SZ_AUT:PersistentLevel.Checkpoint23
Checkpoint_C                   Checkpoint18                   /Game/RuinsLevel/Maps/770_SZ_Swamp.770_SZ_Swamp:PersistentLevel.Checkpoint18
Checkpoint_C                   Checkpoint21                   /Game/RuinsLevel/Maps/770_SZ_Swamp.770_SZ_Swamp:PersistentLevel.Checkpoint21
Checkpoint_C                   Checkpoint22                   /Game/RuinsLevel/Maps/770_SZ_Swamp.770_SZ_Swamp:PersistentLevel.Checkpoint22
Checkpoint_C                   Checkpoint19                   /Game/RuinsLevel/Maps/780_SZ_Depths.780_SZ_Depths:PersistentLevel.Checkpoint19
Checkpoint_C                   Checkpoint20                   /Game/RuinsLevel/Maps/780_SZ_Depths.780_SZ_Depths:PersistentLevel.Checkpoint20
Checkpoint_C                   Checkpoint16                   /Game/RuinsLevel/Maps/790_SG.790_SG:PersistentLevel.Checkpoint16
Checkpoint_C                   Checkpoint                     /Game/UI/Menus/OptionsMenu/VideoConfigMap.VideoConfigMap:PersistentLevel.Checkpoint
```
