# 02 — Pak inventory and system evidence

*Written by the coordinator. The delegated pak agent produced the raw listings
(`pak_assets.txt`, `pak_umaps.txt`, `pak_plugins.txt`, `pak-files.json`) and then stalled;
this report interprets them. Everything below was extracted from the shipped pak.*

---

## 1. Container

`QueensDomain\QueensDomain\Content\Paks\QueensDomain-WindowsNoEditor.pak`, 1,976,294,370 B.

```
footer magic             E1 12 6F 5A
version                  11              (the UE 4.27 layout)
index_offset             1,973,612,650
index_size               529,218
index_hash               d7e07fccb0a097aba197b98ec23cb277002ba1bb
bEncryptedIndex          0               <-- plaintext
EncryptionKeyGuid        0000...0000
CompressionMethods       all zero        <-- no zlib, no Oodle
```

**This is easier than Dolls Nest in every dimension.** Dolls Nest's index was AES-256-ECB
and needed a community key; here the index is plaintext and nothing is compressed, so no
key hunt and no decompressor are needed. `OodleData`/`OodleNetwork` ship as *plugins* but
are not used for pak compression.

### Layout

```
index_offset              primary index
                            FString mount "../../../"
                            int32   NumEntries = 44156
                            u64     PathHashSeed
                            u8      bEncodedPakEntries
                            ... encoded entry blob ...
index_offset+index_size   PathHashIndex: i32 count = 44092, then count x (u64 hash, i32 off)
then                      file table: i32 numDirs, repeat { FString dir; i32 n;
                                           repeat { FString name; i32 loc } }
```

The **names** are recoverable in full (47,716 paths, `pak-files.txt`). The per-entry
offsets/sizes were subsequently decoded from the bit-packed `EncodedPakEntries` blob
(`pak/PAK-DECODE.md`, validated), so payload extraction works. This was the only open
item in this file and it is closed.

### What is inside

| Extension | Count |
|---|---|
| `.uexp` | 19,219 |
| `.uasset` | 19,156 |
| `.res` | 6,876 (ICU/localisation resources) |
| `.ubulk` | 1,730 |
| `.png` | 283 |
| `.uplugin` | 144 |
| `.locres` | 82 |
| `.ini` | 62 |
| `.umap` | **51** |
| `.brk`, `.ufont`, `.ttf`, `.dict`, `.locmeta`, `.icu`, `.nrm`, `.psd` | ~60 |
| `.uproject` | 1 |
| **total** | **47,716** |

## 2. Plugins shipped

72 plugins, and the list is informative:

```
OnlineSubsystem, OnlineSubsystemNull, OnlineSubsystemSteam, OnlineSubsystemUtils   <-- networking compiled in
SteamBridge, SteamShared                                                          <-- Steam integration
KawaiiPhysics                                                                     <-- hair/skirt physics (anime character rig)
OodleData, OodleNetwork
GameplayCameras, TemplateSequence, LevelSequenceEditor, MediaCompositing
CommonUI, AsyncLoadingScreen, MotoSynth, Synthesis, SoundFields, Spatialization
ACLPlugin, GeometryCache, ProceduralMeshComponent, EditableMesh, CustomMeshComponent
CharacterAI, AISupport, EnvironmentQueryEditor
SQLiteCore, AssetManagerEditor, AssetTags, AssetSearch, BulkRenamer
AnimationBudgetAllocator, ComponentTimeline, MeshPainting, ScreenshotTools
FB_AnimText, FB_RenderWorlds, FB_Utils, FBAttributes, Switcheroo, CommandsPlugin
```

Two things stand out:

- **`KawaiiPhysics`** — a hair/skirt simulation plugin. It only makes sense if the game
  renders a *character with hair and cloth*, which corroborates the full-body player model
  finding in §3.
- **`OnlineSubsystem*` + `SteamBridge`** — the online session layer is compiled in. It is an
  enabler, not a plan: no game class implements sessions or RPCs (see `01`).

## 3. The player character

`QueensDomain/Content/Player/Assets/FPModel/`

```
Player2026/Player_FullChar_2026                    full-body skeletal mesh
Player2026/Player_FullChar_2026_Skeleton           skeleton
Player2026/Player_FullChar_2026_PhysicsAsset       physics / ragdoll
Player2026/Player_FullChar_2026_IntroAnimation     full-body intro animation
Player2026/Player_FullChar_2026_coolPose(_Animated)
Player2026/PlayerFace_26_tx, playerhair26_tx, Hair_26_MI, Head_26_MI
Player2026/Player_Armor_BloodMask, PlayerArm_BloodMask, PlayerFace_26_BloodMask
Player2026/Prince_Eat_MA                           full-body animation
FPArm_00, FPArm_00_MI, Player_FPArms_Arms          <-- the separate first-person rig
Anims/Player_FPArms_Arms_Skeleton / _ABP / _PhysicsAsset
~200 x Player_FPArms_* animations
```

Top-level character blueprints: `FS_PlayerChar`, `FS_Full_PlayerChar`,
`FS_PlayerController_BP`, `CameraManagerAttachedComp`, `FaceFollowComp`,
`TitleScreen_Character`, `Cutscenes/FS_SequenceCharacter`.

**Verdict: the player has a real, rigged, physics-enabled, texture-complete body, plus a
separate first-person arms rig.** The co-op puppet needs no new art. Blood masks mean
damage is already visually expressible on the body.

## 4. Content shape

| Folder | Assets | Contents |
|---|---|---|
| `Enemies/` | 12,221 | ~40+ families incl. every boss |
| `Items/` | 8,624 | Weapons 2,506 · KeyItems 503 · Treasure 400 · Consumables 310 · Accessories 281 · Upgrades 123 |
| `RuinsLevel/` | 5,492 | all maps |
| `NPCs/` | 1,588 | Knight, Chef, Raider, Scientist, Crow, Librarian, FishWife, King, Merchant … |
| `OtherWorld/` | 1,566 | `Castle_Ext`, `OW_Props`, `OW_MK`, `Lab`, `Lift_00`, `OW_TP` |
| `UI/` | 1,457 | |
| `Audio/` | 1,366 | |
| `VFX/` | 1,239 | |
| `Props/` | 1,082 | |
| `Player/` | 1,076 | |
| `Blueprints/` | 900 | gameplay logic |
| `BaseMaterials/` | 507 | |
| `Music/` | 418 | |
| `Cutscenes/` | 223 | |
| `Tnf_Secrets/` | 241 | |
| `Artifacts/` | 94 | ability-granting collectibles |

## 5. World

**One persistent level with streamed area sublevels**, not 51 separate worlds.

```
Ruins_Level_00                      the persistent world (and SaveLevel in the save)
 000_Shared_Base_Lighting, 050_Ocean, 060_Ocean_Shadow, 070_UpperShell     global layers
 100_GR 110_GR_dungeon 120_GR_goldenroom 125_GR_CubeHall 130_GR_outerisles 140_GR_ShopIsleOnly
 200_AQ 210_AQ_underground
 300_LIB 310_LIB_exterior 320_LIB_basement
 400_MAUS_entrance 420_MAUS_mid 450_MAUS_darkmaze
 500_AUT 510_AUT_droptunnel
 600_Castle 610_Castle_Ext
 700_SZ_GR 710_SZ_AQ 720_SZ_LIB 730_HOD 740_SZ_Prison 750_SZ_MAUS2
 760_Dekanima 760_SZ_AUT 770_SZ_Swamp 775_JigekiTemple 780_SZ_Depths 790_SG
 800_FinalZone 900_SipehaWorld 990_B33sment
```

Separate maps: `Intro_Level_00/01`, `997_FinalZoneCutscene`, `998_QueenCutscenes`,
`999_SipehaCutscenes`, `LD45/Maps/title_screen`, plus UI preview maps.

## 6. System evidence, by asset name

### Quest / progression
```
Blueprints/ActorQuestState, ActorQuestStateManager, QuestStateInterface, QuestForceUpdateVolume
Blueprints/Challenges/quest_book, quest_chef, quest_ducks, quest_egg, quest_grimoire,
                       quest_lib, quest_mushroom, quest_necro, quest_raider, quest_sci,
                       quest_skullls, quest_spend, quest_trident
Blueprints/SaveSystem/FS_SaveSlot, FS_SystemSave, FS_CoinsSave, SavableActor,
                       SavableActorState, SaveFunctions, SaveHelper, ItemDropSave
Blueprints/ActivatableGroup, ActivatableNamedSaveDataSetter, NamedSaveDataSetterVolume,
           NamedSaveDataSetter_Config, WorldEventReceiver, NotifyActivatables
```

### Dialogue
```
Blueprints/ChefCrowEvent/{Chef,Crow}_Event_Dialogue
Blueprints/SecretSwordAltar/SecretSwordAltar_{MissingParts,Unevod}Dialogue
Enemies/**/{Queen_Greet,Queen_SpecialShieldBreakGreet,Sipeha,Sipeha_Eat,Sipeha_Banish,
            WhaleKing,WhaleKing_SG,Dragon_ROS,Necronomicon,JosamiLord_Death,
            Des_JosamiKnight,ShrineBoss,Shrineboss_Demo,Fishwife_Evil,
            LargeRobot_Rusty,Boner_Witch}_Dialogue
NPCs/**/Ghost_*_Dialogue  (Arjaot, Budaen, Daon, Ekonta, Imba, Ippibi, Nu, Ujji,
                           Raider_1..4, Raider_Captain, Chef, Scientist, King, FishWife, BMMC)
Blueprints/ReadableText_DataAsset, InspectText
```

Note the `Ghost_` prefix: a large part of the cast are ghosts.

### Items / loot
```
Blueprints/ItemPickup_Base, CoreItemPickupActor, ItemSystem/{FS_ItemData, ItemCategory,
            ItemDrop, SpawnItemDrop, Item3DConfig}, ItemDestabilizingActivatableVolume
Blueprints/Shop/{Shop, Shopkeeper, ShopMenu, ShopFunctions, ShopTreasureActor, ShopEntry}
Blueprints/UpgradeMachine/WeaponUpgradeMachine
Items/KeyItems/{AquaDoorKey(1..4), ArchivesKey, BookKeys/{Alchemy,BoneMachines,CookBook,
                 DamsAndWeirs,...}, AcidVial, ScientistUpgrades}
Artifacts/{StrangeMushroom(DoubleJump), Wind_Crystal, Anemone_Fossil, Egg_Fossil,
           Fish_Fossil, Pigeon_Fossil}
```

### Enemies and AI
```
Enemies/EnemySpawner                        <-- the generic spawner
Enemies/<Family>/<Variant>_Spawner          <-- per-enemy spawner blueprints
Enemies/BloodPoolDuo/BloodPool_EnemySpawner, Miniboss_Stalker_Spawner,
        SnailBull_Miniboss_Spawner, DrillRobot_Miniboss_Spawner, LargeRobot_Miniboss_Spawner
Blueprints/EnemyBehaviourMode, AggroRadiusFunction, Enemy_Spline_PPC, PPC_EnemyOnly
```

Families in the pak: `Boner_00` (skeletons: Black/Gray/Mossy/Stone/ShrineBoss), `JosamiKnight_00`,
`JosamiLord`, `Necronomicon`, `WhaleKing`, `Queen`, `Dragon`, `MechaKnight`, `Golem`,
`DemonBosses`, `Sipeha`, `FishWifeEvil`, `LargeRobot`, `DrillRobot`, `Manticore`,
`Spider`, `Crawler`, `Bat_00`, `Slime_00`, `HellSlime`, `Mimic`, `Waspdeer`, `Ribber_00`,
`Orbiter_00`, `Looker_00`, `RattyBugger_00`, `Wheeler_00`, `Gargler_00`, `Lizer_00`,
`Fisher_00`, `FishManer_00`, `BookBug`, `Chickilus`, `Ballooner`, `FlyingManta`,
`PoisonFishMan`, `Barnacle`, `Alien1`, `Cuttledog`, `Spiker`, `Bat`, and more.

### Interactables and world objects
```
Door_Base, QueenDoortempBlocker, SecretWall, ScienceSecretWall
Elevator/{Elevator_Base, ElevatorLever, ElevatorPathKeyframes, WoodElevator_Lever}
MovingActor, Library_RotateBridge, Library_LowerBridge
Teleporter/{Teleporter, MiniTeleporter, SecretTeleporterDestination, TeleporterData}
Areas/<Area>/<Name>_Save  + _Icon_SP      (~30 save points across all areas)
Areas/<Area>/<Name>_Teleporter            (per-area fast travel)
Altars: SecretSwordAltar/*, SpectralSwordAltar/*, SuperCubeAltar/*
BreakableBarricade/*, Wellcover_Breakable
SuspendSaveBlockingVolume, LevelManagerTrigger, JumpAcrossHelper, NavUpdaterBox
ENVControl/{ENVController, ASideTrigger, BSideTrigger, ENVControlData, ScaryDarkness_ENVData}
FogSystem/{FogProcessor, FogLightActor, FogControlInterface, FogLightDataMPC, ...}
```

### UI / cutscenes
```
UI/Menus/Journal (Journal3DViewMap), Shop, Options, UpgradeMachine screens
Cutscenes/{CutsceneFadePP, FB_SequenceCamera, FS_SequenceCharacter, SequencePreviewRT}
```

## 7. Completion criteria (the "start to finish" checklist)

From `Blueprints/Challenges/`, and confirmed live in the `Challenges` save, whose keys are
`cha_` + asset name (`cha_Item_GotAllWeps`, `cha_Challenge_AllTeleporters`):

- **10 areas**: `area_AQ, area_AW, area_castle, area_forest, area_lib, area_maus, area_prison, area_ros, area_sg, area_swamp`
- **8 bosses**: `boss_bigfish, boss_dragon, boss_hellduo, boss_jl, boss_necro, boss_redlords, boss_ros, boss_whaleking`
- **13 quests**: `book, chef, ducks, egg, grimoire, lib, mushroom, necro, raider, sci, skullls, spend, trident`
- **4 endings**: `ending1..4`
- **13 collection**: `item_acc, item_cells, item_endu, item_eweap, item_flygems, item_masklens, item_melee, item_secretsword, item_spectralsword, item_throw, item_treasure, item_ultra, item_upgrade`
- **5 special**: `challenge_chests, challenge_sdoor, challenge_tpsp, plat, quat_vault`

This is a ready-made acceptance test: **if both players' challenge sets match at the end,
the playthrough was co-op-correct.**

## 8. What this pak does *not* give us

- Asset **payloads**. Row values in DataTables (item stats, quest definitions), Blueprint
  bytecode, and material parameters all require decoding `EncodedPakEntries` and extracting
  `.uasset`/`.uexp`. The names are enough to plan; the payloads are needed to *balance* and
  to read quest definitions.
- Any evidence about runtime behaviour. Names prove a system exists, never how it behaves.

## 9. Moddability implication

The game loads content from a monolithic pak and (per the manifest) a set of loose files.
DollsMP's rule applies: **ship loose files, never repack the 1.88 GB pak.** A mod that
needs to *add* an asset (which this one does not — the player body already exists) would
use `_P` patch paks, which this build supports (v11).
