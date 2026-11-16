# CAMPAIGN-ROUTE — Queen's Domain, start to finish, as an ordered checklist

Research pass, 2026-11-16. This is the campaign as a sequence of **steps a co-op pair must
complete**, each with the *save-format fact that proves it happened* and *how a test rig can
reach it*. It exists so a two-machine run can be driven from the first cutscene to an ending
without guessing which map comes next.

Companion: [`COVERAGE-MATRIX.md`](COVERAGE-MATRIX.md) — for each system, whether the mod
actually syncs it.

**Counts.** **49 map steps** (R1-R49; **44 on the critical path**, 5 optional) +
**14 quest chains** (Q1-Q14) = **63 route steps**; **10 boss encounters** with a rig row
(B1-B10) plus Sipeha (+BS) = 11 named encounters; **4 endings** (E1-E4).

---

## 0. The world model — read this before the route

The game is **one persistent `UWorld` (`Ruins_Level_00`) plus 42 streamed sublevels**, not 51
levels. Measured in `analysis/pak/answers-2.md` §C10 (42 `LevelStreamingDynamic` exports, 42
distinct `/Game/RuinsLevel/Maps/<Level>` package paths, one-for-one).

**The six other `UWorld`s** (a "hard" travel, `TravelSync`'s `HARD_BEGIN`/`ARRIVED`/`RESUME`):

```
Entry                    (boot)
Intro_Level_00/01        the prologue, a separate world
900_SipehaWorld          Sipeha's world (ending 3/4)
990_B33sment             a small hidden basement world
997_FinalZoneCutscene    the ending cutscene world
998_QueenCutscenes / 999_SipehaCutscenes
title_screen
```
plus non-gameplay maps: `Journal3DViewMap`, `VideoConfigMap`, `VideoConfig_2` (UI preview —
**but the shop door lives in `VideoConfigMap`**, a streamed sublevel despite its name),
`BirdsTestZone` + `SublevelRefTest` (dev), `Ruins_Level_00_ShaderCompile`.

**Global layers** (always streamed, no gameplay): `000_Shared_Base_Lighting`, `050_Ocean`,
`060_Ocean_Shadow`, `070_UpperShell`.

**The area topology, from the 22 `FS_LevelList` assets** (`answers-2.md` §C10.5). This is the
game's own notion of "where you are", and it is what bounds a split party (union ≤ 18 of 42
sublevels):

| level list | levels | the maps it covers |
|---|---:|---|
| `GreenRuins` | 15 | 100_GR and its siblings |
| `GreenRuins_OuterIsles` / `_ExtraPri` | 16 each | 130_GR_outerisles |
| `GreenRuins_NearLibrary` | 15 | 130 → 310 transition |
| `AquaRuins` | 15 | 200_AQ, 210_AQ_underground |
| `Library` | 16 | 300_LIB, 310_LIB_exterior, 320_LIB_basement |
| `Library_SZ` | 10 | 720_SZ_LIB |
| `Mausoleum` | 16 | 400/420/450 |
| `Mausoleum2_Deep` / `Mausoleum_SZ` | 12 / 9 | 750_SZ_MAUS2 |
| `AutumnIsland` / `AutumnIslandCastle` | 5 / 5 | 500_AUT, 510, 600 |
| `DropTunnel` | 6 | 510_AUT_droptunnel |
| `SZ_Depths` | 11 | 780_SZ_Depths (the hub of the second half) |
| `Prison_SZ` | 9 | 740_SZ_Prison |
| `ROS` | 6 | 760_SZ_AUT |
| `StoneGarden` | 8 | 790_SG |
| `SipehaArena` | 2 | the Sipeha fight |
| `IntroCutscene` | 2 | the prologue |

**Area order the content itself declares** — the `Blueprints/Areas/` folder numbering
(`1GreenRuins`, `2AquaRuins`, `3Library`, `4Mausoleum`, `5AutumnIsland`, `6Castle`, `7SZ`,
`ROS`), and the achievement id order in `DefaultEngine.ini`
(`cha_Area_Aqr, _Lib, _Maus, _Aut, _Castle, _Prison, _Forest, _Swamp, _ROS, _SG`). The route
below follows both.

**Two ways to read "where am I":** the `UWorld` name (only changes on a hard travel) and the
**area** (a sublevel, which is what `TravelSync`'s phase-4 `TP_AREA` announcement carries,
because entering an area changes no `UWorld` name — `SYNC-GAPS` §3).

---

## 1. The route

### Part A — map steps

Each row: **R#** · map · what the pair must do · **the fact that proves it** (save key or actor
state, from the save format) · **rig reach**.

| # | map | step | proves it | rig reach |
|---|---|---|---|---|
| **R1** | `title_screen` → `Entry` | new game; the game boots `Intro_Level_00` | — (no save yet) | `QDCOOP_AUTOLOAD=<slot>` boots straight into gameplay past the menu, which is why the ending's wake-up is suspect (Session 29 "still open") |
| **R2** | `Intro_Level_00` | the prologue tutorial | — | `qd.hard Intro_Level_00` (hard world; measured for `997_FinalZoneCutscene`, not for the intro) |
| **R3** | `Intro_Level_01` | the wake on the beach / first walk (`FS_SipehaStatue_BP` ×2, `SipehaRune_BP`) | — | `qd.hard Intro_Level_01` |
| **R4** | `Ruins_Level_00` (hub) | the Green Ruins camp: talk to `Knight_Arjaot_NPC_BP_C` | `Arjaot_State = 1` (a dialogue write, `analysis/24` §2b) | boot level of every rig; `qd.act near:Knight_Arjaot_NPC_BP!Interact`, `qd.dlg advance` |
| **R5** | `100_GR` | `Knight_Ekonta_NPC_BP_C`: the rusty sword | `RustySwordKnightState = 1`, `RustySwordKnightGaveSword = 1` | `qd.flag set:RustySwordKnightGaveSword=1`, or `qd.act near:Knight_Ekonta_NPC_BP!Interact` |
| **R6** | `100_GR` | break the tutorial barricade (`BreakableBarricade_C`) | `BrokeBarricade = 1` **and** its `SavableActorStates` GUID | `qd.act near:Barrel!Break` (the family is proven); `qd.act near:<barricade>!Break` |
| **R7** | `100_GR` | first checkpoint (`Checkpoint7` / `Checkpoint17`) | `CheckpointUnlockCount ≥ 1`; `SavePointsVisited` gains the path | `qd.act near:Checkpoint!Interact`, or `qd.flag set:CheckpointUnlockCount=2` |
| **R8** | `100_GR` | unlock the Green Ruins fast-travel node (`Teleporter_GR`, class `Ruins_Teleporter`) | `TeleporterUnlockCount ≥ 1`; `UnlockedTeleporters` gains a path (**not mirrored — GAP 3**) | `qd.travel near:Teleporter_GR`; `qd.flag set:TeleporterUnlockCount=1` |
| **R9** | `110_GR_dungeon` | the Sword Shrine: take `SwordShrineKey`, open `SwordShrineGate_BP_C` | `Knight_00_CampState = 1` (setter `SwordShrineKeyNamedSaveDatasetter`); gate's `SavableActorStates` GUID | `qd.act near:SwordShrineGate_BP!Activate` — **the door lane's live proof** (`world-interact.md`: `key=709D84E1`) |
| **R10** | `110_GR_dungeon` | ride the dunger lift (`IntroDungerLift_BP*`, ×4) into the dungeon | — (position) | `qd.mover = IntroDungerLift_BP,,<beats>` — the mover lane's live proof (s32 run 2) |
| **R11** | `120_GR_goldenroom` | **optional** treasure room | — | `qd.level = 120_GR_goldenroom` |
| **R12** | `125_GR_CubeHall` | **optional** puzzle hall (`ActivationTrigger_C`, `M_DoorSmall`) | — | `qd.level = 125_GR_CubeHall` |
| **R13** | `130_GR_outerisles` | the isles; `TeleporterFragment_Pickup_BP_C` ×? (fragments unlock a node) | `TeleporterUnlockCount` | `qd.level = 130_GR_outerisles` |
| **R14** | `140_GR_ShopIsleOnly` | the merchant: `Merchant_NPC_BP_C`, the shop door, buying | `ShopDoorOpened = 1`; `ShopAmountSpent`; `ShopkeepUnlock1..4`; `ShopPurchasedKey/*` | `qd.act near:Merchant_NPC_BP!Interact`; `qd.flag set:ShopDoorOpened=1` (R105's path); `qd.cheat GiveAllKeys` for stock |
| **R15** | `100_GR` | **Shrine Boss** — `ShrinebossSpawner` (`Boner_ShrineBoss_Spawner_C`), trigger `ShrinebossActivationTrigger`, gates `GraveyardBoss_Gate_C` ×2 | **no save flag exists** (B1 below) | `qd.trigger ShrinebossActivationTrigger@100_GR` then `qd.hit`; Session 26 key `03FDBC21` |
| **R16** | `200_AQ` | the Raider camp (`RaiderCampAliveState`/`DeadState`) and `Raider_TurnInNecklace` | `RaiderNeckacesTurnedIn ≥ 4` | `qd.level = 200_AQ`; `qd.act near:Raider!Interact` |
| **R17** | `210_AQ_underground` | `Fishwife_NPC_BP_C` / `Fishwife_Ghost_BP_C`: the egg quest | `FishWifeTotalEggs ≥ 50` | `qd.act near:Fishwife_Ghost_BP!Interact` — **the dialogue lane's live proof** (`Fishwife_Ghost_Dialogue`) |
| **R18** | `210_AQ_underground` | open the dark cave | `AQR_OpenedDarkCave = 1` (setter `ActivatableNamedSaveDataSetter`) | `qd.flag set:AQR_OpenedDarkCave=1` |
| **R19** | `210_AQ_underground` | the aqua boss arena (`AquaBossDoor_BP_C`, `AquaBossExitDoor_C`, `Boss_Env_PPC`, `ChickenBossNecklace_Pickup_C`, `Fishman_Boss_Crown_Pickup_C`) and the **Whale King** (`WhaleKingSpawner`) | `WhaleKingIsDead = 1` | `qd.trigger WhaleKingSpawner@210_AQ_underground`; Session 29 run 5 key `6AEFB87A` |
| **R20** | `300_LIB` | `LibrarianQuestStateManager`: the bestiary | `LibrarianBestiaryCompletionPercent = 100` | `qd.flag set:LibrarianBestiaryCompletionPercent=100` |
| **R21** | `300_LIB` | `Knight_Budaen_QuestStateManager` (+`Knight_Budaen_GiveKey_Action`) | the manager's `InternalState`; a key item | `qd.act near:Knight_Budaen_NPC_Ghost_BP!Interact` |
| **R22** | `300_LIB` | the Library book puzzle (`Lib_Book_00..04_BP_C`, `Lib_Checkpoint_Gate_SM`) | `LibBookPuzzleComplete = 1` | no puzzle lever exists — `qd.flag set:LibBookPuzzleComplete=1` (a **skip**, not a solve) |
| **R23** | `300_LIB` | the **Necronomicon**: `Necronomicon_Boss_Spawner`, gates `Necro_Boss_Door`, `Lib_BossDoor_BP` ×3 | `NecronimiconDefeated = 1` | `qd.trigger Necronomicon_Boss_Spawner@300_LIB`; conclude by `qd.act near:dialogue!End` (Session 29 runs 9, 14) |
| **R24** | `310_LIB_exterior` | the Library's outside; `Teleporter_Lib` | `TeleporterUnlockCount` | `qd.level = 310_LIB_exterior` |
| **R25** | `320_LIB_basement` | vault relic #1: `SR_Rusted_Greatsword_Acquired` (setter `ActivatableNamedSaveDataSetter6`) | `SR_Rusted_Greatsword_Acquired = 1` | `qd.flag set:SR_Rusted_Greatsword_Acquired=1` |
| **R26** | `400_MAUS_entrance` | the Mausoleum: `Mausoleum_Lift_00_BP2` (mover), `MausoLever_00_C`, `M_DoorSmall_00_BP_C`, `Checkpoint10` | — | `qd.level = 400_MAUS_entrance`; `qd.mover` |
| **R27** | `420_MAUS_mid` | vault relic #2 `SR_RustedLeafBlade_Acquired` (setter 4) and `GotKnightKey` (`ReanimatedKnightNSDSetter`); `Knight_Nu_QSM` | both keys = 1 | `qd.flag set:SR_RustedLeafBlade_Acquired=1` / `GotKnightKey=1` |
| **R28** | `450_MAUS_darkmaze` | `ChefAliveState`/`ChefDeadState` (the mealticket quest), `King_QuestStateManager`, `BMMC`, `PreUndeadState`/`UndeadState`, `PlayerReachedKitchen` | `MealTicketsCollected ≥ 60`; `PlayerReachedKitchen = 1` | `qd.act near:Chef!Interact`; `qd.flag set:PlayerReachedKitchen=1` |
| **R29** | `450_MAUS_darkmaze` | the **Demon Bosses** — `DemonSpider_Spawner` + `Boss_Deco`, `DemonBosses_FightController`, gate `WebBossWall_BP`; `SuperCubePuzzleDoor` | `DemonBossesKilled = 1` (needs the spider **and** the hell-bat) | `qd.trigger ActivationTrigger4@450_MAUS_darkmaze`; Session 28 run 1 |
| **R30** | `500_AUT` | `ScientistQuestStateManager` / `DeadScientistState`: the data quest | `ScientistDataGiven ≥ 10` | `qd.act near:Scientist!Interact`; `qd.flag set:ScientistDataGiven=10` |
| **R31** | `500_AUT` | the **Bridge Lord** — `JosamiLord_Spawner` (+`JosamiLord_ArenaOrientationActor`) | `BridgeLordDead = 1` | `qd.trigger JosamiLord_Spawner@500_AUT`; Session 29 run 8 key `2543387C` |
| **R32** | `510_AUT_droptunnel` | the drop tunnel down; `Checkpoint15` | — | `qd.level = 510_AUT_droptunnel` |
| **R33** | `600_Castle` | the **Queen** — `Queen_Spawner_BP`, `Queen_Throne_BP`, `Queen_Door_Bp` ×2, 5 miniboss spawners | `Ending1Complete` / `Ending2Complete` + `EndingSeen` (+`EndingWakeOnBeach`) | `qd.trigger Queen_Spawner_BP@600_Castle`; then `qd.act near:Queen_BP!Interact` + `qd.dlg advance` + `qd.act near:dialogue!End`; Session 29 runs 10-15, 33-46. **Phase 2 not reached** |
| **R34** | `610_Castle_Ext` | the castle exterior / bridge (`OW_BridgeDoor_C`) | — | `qd.level = 610_Castle_Ext` |
| **R35** | `700_SZ_GR` | vault relic #3 `SR_GMS_Acquired` (setter 2); `SecretZoneDoor_Blue_BP_C`, `SZ_Door_02_BP_C`, `SipehaRune_GR_SZ` | `SR_GMS_Acquired = 1` | `qd.level = 700_SZ_GR`; `qd.flag set:SR_GMS_Acquired=1` |
| **R36** | `710_SZ_AQ` · `720_SZ_LIB` | the two sub-areas between; `TeleporterFragment_Pickup_BP2`, `SecretZoneDoor_BP2` | — | `qd.level = 710_SZ_AQ` / `720_SZ_LIB` |
| **R37** | `730_HOD` | **optional/uncertain** — a `SkullKeyRelic_BP` + `FS_SipehaStatue_BP2` room | the `SkullKeyRelic_Data` item | `qd.level = 730_HOD` |
| **R38** | `740_SZ_Prison` | the **Red Lords** — `JosamiBloodLord_Sword_Spawner` + `_Gun_Spawner`; `JosamiPrisonDoor_C`, 9× `SZ_JailDoor_BP` | `RedLordsDead = 1` (both sisters must die within a second — one revives the other) | `qd.trigger ActivationTrigger4@740_SZ_Prison`; Session 26 runs 14/16 keys `DBF4FFD3`/`17E153F4` |
| **R39** | `750_SZ_MAUS2` | the deep mausoleum | — | `qd.level = 750_SZ_MAUS2` |
| **R40** | `760_Dekanima` | **optional** whole world (`Stalker`/`Manta`/`SnailBull` spawners, `SkullKeyRelic_BP` ×3) | the skull relic items | `qd.level = 760_Dekanima` |
| **R41** | `760_SZ_AUT` | the **ROS Dragon** — `ROSDragon_Spawner`; the ROS book puzzle (`ROSBookPuzzleComplete`); `Teleporter_ROS`, `ROS_Door_BP_C`, `Ros_Cage_Gate_BP_C` | `ROSDragonDead = 1`; `ROSBookPuzzleComplete = 1` | `qd.trigger ActivationTrigger11@760_SZ_AUT`; conclude with `qd.act near:dialogue!End`; Session 29 run 4 key `8120191E` |
| **R42** | `770_SZ_Swamp` | the **Giant Fish** — `GiantFish_Spawner_BP`; vault relic #4 `SR_GreenKey_Acquired` (setter 3); `SipehaRune_Swamp` | `GiantSwampFish_IsDead = 1`; `SR_GreenKey_Acquired = 1` | `qd.trigger GiantFish_Spawner_BP@770_SZ_Swamp`; Session 29 run 6/9 key `51BC2B8B` |
| **R43** | `775_JigekiTemple` | `JigekiDoor_BP_C`, `FishWifeEvil_Spawner_C` (JigekiKeys family) | a `JigekiKeys/*` item | `qd.level = 775_JigekiTemple` |
| **R44** | `780_SZ_Depths` | the **Dragon Lord** — `GreenDragon_Spawner` **+** `JosamiLord_Green_Spawner` (rider on the dragon); gate `DesBoss_Gate_C`; vault relic #5 `SR_Key_Blue_Acquired` (setter 5); `Teleporter_Forest`, `OnyxSipehaStatue_Pickup` | `DragonLordDead = 1` (needs both bodies) | `qd.trigger ActivationTrigger9@780_SZ_Depths`; Session 26 key `17181166`. **The most-used rig area (Sessions 11-25)** |
| **R45** | `790_SG` | the Stone Garden puzzle → `StoneGardenPuzzleFinished = 1`; `WhaleKing_SZ_Spawner` + `WhaleKing_Env_PPC` (a second whale placement); `SipehaRune_BP`, `MiniTeleporter2`, `SecretTeleporterDestination2`; `Checkpoint16` | `StoneGardenPuzzleFinished = 1` | `qd.level = 790_SG`; `qd.flag set:StoneGardenPuzzleFinished=1` |
| **R46** | `800_FinalZone` | the **Secret Skull Door** — `SkullDoorQuestStateManager` + `SkullDoorQuestState0/1`, `SecretSkullDoor_NPC_BP_C`, `SecretSkullRitual_BP`, `SecretWall4`; `MiniTeleporter` | `cha_Quest_Skulls` (no NSD condition — script-side); `SkullKeyRelic_Data` granted by `SecretSkullDoor_TurnInAction` | `qd.level = 800_FinalZone`; `qd.act near:SecretSkullDoor_NPC_BP!Interact` |
| **R47** | `900_SipehaWorld` | **Sipeha** (`Sipeha_Spawner_C`, `Sipeha_NPC`, `Sipeha_BanishDialogueAction`, `Sipeha_EatDialogueAction`) | `Ending3Complete` / `Ending4Complete` + `EndingSeen` | `qd.hard 900_SipehaWorld` (the rig streams it as a sublevel: `rig: hard lever -> 900_SipehaWorld streamed=1`) |
| **R48** | `990_B33sment` | **optional/secret** — 98 exports, `Lib_LibrarianChair_BP_C` | — | `qd.hard 990_B33sment` (measured `streamed=1` in Session 21) |
| **R49** | `997/998/999` | the ending cutscene worlds (`Ending1CutsceneManager` lives in `998_QueenCutscenes`) | `EndingSeen = 1` | `qd.hard 997_FinalZoneCutscene` — **the measured hard-travel proof** (Session 22) |

> Steps R11, R12, R37, R40, R48 are optional; everything else is on the critical path. There
> are **49 map rows**, so the **critical-path count is 44**.

### Part B — the 14 quest chains

Each is a save-flag condition, not a quest object (`analysis/24` §1a/§1b). "Rig" is the
shortest honest way to reach the state.

| # | quest (`cha_Quest_*`) | condition (exact) | where the pieces are | rig |
|---|---|---|---|---|
| **Q1** | `Raider` | `RaiderNeckacesTurnedIn ≥ 4` | `200_AQ` `Raider_TurnInNecklace`; `RaidersKey`, `RaiderNecklaceKey` | `qd.flag set:RaiderNeckacesTurnedIn=4` |
| **Q2** | `Fish` | `FishWifeTotalEggs ≥ 50` | `200_AQ` Fishwife; `Egg/*` (16), `FishMaidenTiaras` (18) | `qd.flag set:FishWifeTotalEggs=50` |
| **Q3** | `Lib` | `LibrarianBestiaryCompletionPercent = 100` | `300_LIB` Librarian; needs the bestiary (kills) | `qd.flag set:` (a skip — the real route needs kills per species) |
| **Q4** | `Meat` | `NecroWall00..04Destroyed = 1` (all five) | the LIB maps; writer named `NecroWall_Manager` in `analysis/24` §2 — **not a placed actor** in the export census | `qd.flag set:NecroWall0{0..4}Destroyed=1` |
| **Q5** | `Books` | `LibBookPuzzleComplete = 1` **and** `ROSBookPuzzleComplete = 1` | `300_LIB`, `760_SZ_AUT` | two `qd.flag set:` values |
| **Q6** | `Vaults` | `SR_GMS_Acquired`, `SR_GreenKey_Acquired`, `SR_RustedLeafBlade_Acquired`, `SR_Key_Blue_Acquired`, `SR_Rusted_Greatsword_Acquired` = 1 each | one relic per map: `700_SZ_GR`, `770_SZ_Swamp`, `780_SZ_Depths`, `420_MAUS_mid`, `320_LIB_basement` (the ten setters in `analysis/24` §2a) | walk R25/R27/R35/R42/R44, or five `qd.flag set:` |
| **Q7** | `Chef` | `MealTicketsCollected ≥ 60` | `450_MAUS_darkmaze` Chef; `Mealticket/*` (9) | `qd.flag set:MealTicketsCollected=60` |
| **Q8** | `Science` | `ScientistDataGiven ≥ 10` | `500_AUT` Scientist; `ScientistUpgrades/*` (53) | `qd.flag set:ScientistDataGiven=10` |
| **Q9** | `Shop_Spend` | `ShopAmountSpent ≥ 1000` | `140_GR_ShopIsleOnly` | `qd.flag set:ShopAmountSpent=1000`; a real run needs money |
| **Q10** | `PG_Puzzle` | `StoneGardenPuzzleFinished = 1` | `790_SG` | `qd.flag set:StoneGardenPuzzleFinished=1` |
| **Q11** | `Shop_Unlocks` | `ShopUnlockFinished = 1` + `ShopkeepUnlock1..4 = 1` | `140_GR`; `ShopProgressionKey/*` (29), `ShopPurchasedKey/*` (16) | `qd.cheat GiveAllKeys` + five `qd.flag set:` |
| **Q12** | `Grimoire` | `HasEverInscribedGrimoire = 1` | any grimoire (the inscription UI) | `qd.flag set:HasEverInscribedGrimoire=1` |
| **Q13** | `Mushroom` | **inventory** `StrangeMushroom ≥ 1` | `Artifacts/StrangeMushroom` | `QDCOOP_GIVE="<path>"` or `qd.cheat GiveAllKeys`-style grant |
| **Q14** | `Skulls` | **no condition entry** — scripted; `SecretSkullDoor_TurnInAction` grants `SkullKeyRelic_Data` | `800_FinalZone` (`SkullDoorQuestStateManager`) | R46; the challenge is completed manually by the content |

The **item/collection challenges** (`cha_Item_*`, 14 of them) are inventory/tag conditions and
are listed in `analysis/24` §1b; they are not route steps, they are completion checks
(`TotalAchievementWeapons = 77`, `TotalChestsOpened = 108`,
`TotalSecretDoorsOpened = 89`, `FullEnergyCellsCount = 4`, etc.).

---

## 2. Bosses

The rig's own table (`mod/tools/session26.ps1`, `$script:Bosses`) is the authority for
"name / area / start actor / class to look for / flag", and Session 26-29 is the evidence.
Placements from the map export census (this pass).

| # | boss | class(es) | map | arena / how the fight starts | flag that marks it beaten | live |
|---|---|---|---|---|---|---|
| **B1** | **Shrine Boss** | `Boner_ShrineBoss_Spawner_C` → `Shrineboss*` | `100_GR` | `ShrinebossActivationTrigger`; gates `GraveyardBoss_Gate_C` ×2 | **none — the content writes no flag** | proven except the flag (Session 26, key `03FDBC21`, 260→60 both) |
| **B2** | **Whale King** | `WhaleKingSpawner` → `Whaleking_Boss` | `210_AQ_underground` | proximity trap (`WhaleKingSpawner`, its own `TrapActivateTrigger`); `AquaBossDoor_BP_C` / `AquaBossExitDoor_C`; `Boss_Env_PPC` | `WhaleKingIsDead = 1` | **proven** (Session 29 run 5, key `6AEFB87A`, 1500→1300 both) |
| **B3** | **Necronomicon** | `Necronomicon_Boss_Spawner_C` → `Necronomicon_BP` | `300_LIB` | starts from the **lectern** (`left its rest spot` → `CustomEvent` → `DoIntroAnimation`); gates `Necro_Boss_Door_C`, `Lib_BossDoor_BP_C` ×3 | `NecronimiconDefeated = 1` | **proven** (Session 29 runs 9, 14; 3000→2600 both) |
| **B4** | **Demon Bosses** (spider + hell-bat) | `DemonSpider_Spawner_C` (+`Hellbat`) → `DemonSpider_BP`; `DemonBoss_FightController_C` | `450_MAUS_darkmaze` | `ActivationTrigger4`; gate `WebBossWall_BP_C` (+`BossWebWallFX`) | `DemonBossesKilled = 1` (needs **both**) | **proven** (Session 26 run 15, 2300→1900; flag Session 28 run 1) |
| **B5** | **Bridge Lord** | `JosamiLord_Spawner_C` → `JosamiLord_BP` | `500_AUT` | `~BndEvt__JosamiLord_Spawner_BridgeCenterTrigger`; `JosamiLord_ArenaOrientationActor_C` | `BridgeLordDead = 1` | **proven** (Session 29 run 8, key `2543387C`, 4000→3599.9 both) |
| **B6** | **Red Lords** (2 sisters) | `JosamiBloodLord_Sword_Spawner_C` + `JosamiBloodLord_Gun_Spawner_C` | `740_SZ_Prison` | `ActivationTrigger4`; `JosamiPrisonDoor_C` | `RedLordsDead = 1` | **proven** (Session 26 runs 14/16) |
| **B7** | **Giant Fish** | `GiantFish_Spawner_BP_C` → `GiantFish_Enemy_BP` (`GiantFish_Data.Boss = 0` but it is a boss) | `770_SZ_Swamp` | proximity; `GiantSwampFish_EmergeSpot` | `GiantSwampFish_IsDead = 1` | **proven** (Session 29 runs 6, 9, key `51BC2B8B`, 1800→1600 both) |
| **B8** | **ROS Dragon** | `ROSDragon_Spawner_C` → `Dragon_Ros_BP` | `760_SZ_AUT` | `ActivationTrigger11`; `ROS_BossArenaPPC`; `Ros_Cage_Gate_BP_C` | `ROSDragonDead = 1` | **proven** (Session 29 run 4, key `8120191E`, 9999→9799 both) |
| **B9** | **Dragon Lord** (green dragon + Josami rider) | `GreenDragon_Spawner_C` + `JosamiLord_Green_Spawner_C` | `780_SZ_Depths` | `ActivationTrigger9`; `DesBoss_Gate_C` | `DragonLordDead = 1` (**needs both bodies**: the rider answers the dragon's HP) | **proven** (Session 26 runs 10/13/14, key `17181166`, 3500→3300) |
| **B10** | **The Queen** | `Queen_Spawner_BP_C` → `Queen_BP` | `600_Castle` | `Queen_Spawner_BP`, `Queen_Throne_BP`, `Queen_Door_Bp` ×2; the fight starts from her **greeting dialogue** (`Queen_P1_Intro` montage) | `Ending1Complete`/`Ending2Complete` + `EndingSeen` | **ending proven** (Session 29 runs 10, 12-14). **Phase 2 / orb / second form NOT driven** (run 15) |
| **+BS** | **Sipeha** | `Sipeha_Spawner_C`, `Sipeha_NPC` | `900_SipehaWorld` | a conversation (`Sipeha_BanishDialogueAction` / `Sipeha_EatDialogueAction`) | `Ending3Complete`/`Ending4Complete` + `EndingSeen` | **not driven at all** |

**Eight of the ten rig bosses write a `cha_Bosses_*` achievement** (`Whale`, `Necro`, `Demons`,
`BridgeLord`, `RoSDragon`, `DragonLord`, `GiantFish`, `RedLords`). The Shrine Boss has none;
the Queen and Sipeha write endings, not boss achievements.

---

## 3. The endings

| # | ending | written by | flags | route |
|---|---|---|---|---|
| **E1** | Queen outcome 1 | `Enemies/Queen/Queen_BP` | `Ending1Complete = 1`, `EndingSeen = 1`, `EndingWakeOnBeach`, `cha_Endings` | R33 → the `Ending1CutsceneManager` (in `998_QueenCutscenes`) |
| **E2** | Queen outcome 2 | `Queen_BP` | `Ending2Complete = 1`, same + | R33 with the other branch |
| **E3** | Sipeha outcome 1 | `Enemies/Sipeha/Sipeha_NPC` | `Ending3Complete = 1`, `EndingSeen = 1`, `EndingWakeOnBeach` | R47 |
| **E4** | Sipeha outcome 2 | `Sipeha_NPC` | `Ending4Complete = 1`, same | R47 |

Each machine writes its **own** `EndingNComplete`/`EndingSeen` (measured, Session 29 runs
12-14), and the cutscene lane plays or holds so the pair resumes together. **The choice that
picks 1 vs 2 (and 3 vs 4) is not decoded** — the flags are four independent writes, and no
document names the dialogue option that selects them.

**After the ending:** `EndingWakeOnBeach` holds a camera fade whose second half never runs on
the rig; the mod stops the held fade at 25 s (`PostEndingWakeOnBeach` at 15 s,
`StopCameraFade` at 25 s). Also open: the game's `ClearAllEnemySaveStates` lets the Queen's
spawner make a new Queen (Session 29).

---

## 4. Optional and uncertain content

| what | where | why it is listed as optional/uncertain |
|---|---|---|
| `120_GR_goldenroom`, `125_GR_CubeHall` | Green Ruins | tiny maps (178 / 390 exports) with no quest manager and no setter; a treasure/puzzle room |
| `130_GR_outerisles` | Green Ruins | has teleporter fragments and gates but no quest flag of its own |
| `730_HOD` | SZ | 775 exports, one `SkullKeyRelic_BP` + a Sipeha statue — a relic room with no documented purpose |
| `760_Dekanima` | SZ | a whole 5,894-export world with its own spawner families (`Dekanima` manta/stalker/snail) and 3 skull relics; reachable as a sublevel, never referenced by a quest flag |
| `775_JigekiTemple` | SZ | 615 exports, `JigekiDoor_BP_C` + `FishWifeEvil_Spawner_C` |
| `990_B33sment` | hard world | 98 exports, one `Lib_LibrarianChair_BP_C`; a joke/secret room |
| `790_SG`'s second Whale King (`WhaleKing_SZ_Spawner`) | SZ | the rig fights the `210_AQ` one; whether this one is a second encounter or a leftover is unmeasured |
| `140_GR_ShopIsleOnly` | Green Ruins | optional but **required** for `cha_Quest_Shop_Spend` / `_Shop_Unlocks` |

---

## 5. The rig recipe — every lever this route needs

All levers are files (`qd.<name>`) beside the instance's `-UserDir`, or environment
variables, read by `proxy.cpp`; `#<nonce>` re-arms an unchanged instruction. Full list: 58
`QDCOOP_*` names (`mod/native/src/proxy.cpp`).

| lever | what it does | which route steps need it |
|---|---|---|
| `qd.level = <area>` / `= -` | stream an **area** on this machine; `-` leaves it | every R step in the persistent world |
| `qd.hard = <map>` / `qd.hard auto` | a **hard world** load through `GameInstance.LoadMapWithLoadingScreen` | R2, R3, R47, R48, R49 |
| `qd.travel = near:<sub>` \| `<actor>` | in-world teleport through `FS_PlayerChar.TeleportToLocation` | R8, R24, R35, R44 |
| `qd.tp = near:<kind>` / `qd.near = <cm>` | put the pawn next to a target | every interact step |
| `qd.act = near:<name>!<Verb>[@side]` | perform a named Blueprint verb on a placed actor (the interaction path) | R4-R9, R15-R23, R26-R33, R38-R46 |
| `qd.trigger = <Name>@<Area>` | dispatch a placed volume's overlap event (the boss-start path) | R15, R19, R23, R29, R31, R33, R38, R41, R42, R44 |
| `qd.flag = <name>` \| `all` \| `set:<Name>=<v>` | read the NSD bank / write one flag | every "proves it" column; also the **skip** for Q1-Q12 |
| `qd.cheat = <FS_Cheats fn>` (+ `qd.cheatarg`) | call the game's own cheat Blueprint (`GiveAllKeys`, `GiveAllConsumables`, `KillAllEnemies`, `SaveHere`, `MagicFireMe`, `MagicPoisonMe`) | Q11, Q13, stock |
| `qd.dlg = advance`/`opt<N>`/`leave`/`cursor` | drive or read a dialogue | R4, R17, R21, R28, R30, R33, R46 |
| `qd.sequence = <name>` / `stop:<name>` | start/stop a placed `LevelSequenceActor` | cutscene steps |
| `qd.enemies`, `qd.hit`, `qd.hitguid <8hex>[:amt]`, `qd.enemykill` | enemy dump / damage / kill | every boss |
| `qd.take`, `qd.drops`, `qd.use` | take a drop, dump drops, consume an item | F-lane proof |
| `qd.god`, `qd.selfhp`, `qd.kill` | health floor, set HP, kill the local player | boss fights, death lane |
| `qd.obj`, `qd.gate`, `qd.list` | object inventory, `GetInteractInfo` on one actor, push a level list | diagnosis |
| `qd.mover = <sub>[,dz[,beats]]` | nudge a mover's element (rig-only) | R10, R26 |
| `qd.attack`, `qd.equip`, `qd.wsys`, `qd.proj` | drive the game's own attack input / equip / weapons / a projectile class | combat proof |
| `qd.menu`, `qd.dlg` | drive the menu-pause path | H13 |
| `QDCOOP_AUTOLOAD=<slot>` | boot straight into gameplay (skips the main menu) | all |
| `QDCOOP_GIVE="<item path>;…"` | grant items | Q13 |

**Which existing rig already reached what** (`mod/tools/`):

| rig | reached |
|---|---|
| `session26.ps1` (and `session30-death.ps1`, its copy) | the **whole boss tour** — `100_GR, 210_AQ, 300_LIB, 450_MAUS, 500_AUT, 600_Castle, 740_SZ_Prison, 760_SZ_AUT, 770_SZ_Swamp, 780_SZ_Depths` — plus `-Phase story`, `-Phase gaps`, `-Phase bosses`, `-Only <boss>` |
| `session21.ps1` | `760_SZ_AUT`, `770_SZ_Swamp`, `780_SZ_Depths`, `790_SG` — the area-follow and hard-travel rounds |
| `session22/23/24/25.ps1` | `770_SZ_Swamp`, `780_SZ_Depths` — area follow, the repeated-load crash, loot |
| `session11/13/14/15/16/18/20.ps1` | `780_SZ_Depths` (+ `210_AQ`, `760_SZ_AUT`) — streaming, enemies, the bar |
| `session17.ps1` | `100_GR`, `760_SZ_AUT`, `780_SZ_Depths` — doors, the hit pipeline |
| `session-platforms.ps1`, `session-s32*.ps1`, `session-grip.ps1`, `session-gun.ps1` | `450_MAUS`, `600_Castle`, `780_SZ_Depths` — movers, grip, weapons |
| `session_dialogue.ps1`, `session_prog.ps1`, `session_gap.ps1`, `session_r3.ps1`, `session_final.ps1` | the hub — dialogue, quest items, save mirror, world-interact |
| `session_wi2.ps1` | the hub — doors/breakables/levers/secret walls |
| **never reached** | `Intro_Level_00/01`, `200_AQ` (the Raider camp proper), `310_LIB_exterior`, `400_MAUS_entrance`, `420_MAUS_mid`, `510_AUT_droptunnel`, `610_Castle_Ext`, `700/710/720_SZ_*`, `730_HOD`, `750_SZ_MAUS2`, `760_Dekanima`, `775_JigekiTemple`, `800_FinalZone`, `900_SipehaWorld`, `990_B33sment` |

That last row is the honest state of the campaign: **10 of 42 worlds/areas have had a live
two-machine session**, and they are the ones the bosses and the streaming proof needed.

---

## 6. What I could not map

1. **The actual quest *order*.** No walkthrough exists in the corpus and the game has no quest
   object — the order below is inferred from three independent orderings that agree
   (map numbering, the `Areas/` folder index, the achievement id order) plus the level-list
   adjacency. It is a **defensible** order, not a measured one. A step can be out of order
   without any flag objecting, because a flag is just an integer.
2. **Which key opens which door.** 503 key items in 26 families and 52 door families are both
   enumerated, but **no document joins them**. `Door_Base_C.CheckRequiredItems` reads the
   player's inventory at runtime, so the mod does not care — a *route* does.
3. **The ending choice.** `Ending1..4Complete` are four independent writes; the dialogue
   option (or boss state) that selects Queen-1 vs Queen-2 and Sipeha-3 vs Sipeha-4 is not
   decoded.
4. **The Queen's phase 2 and the Sipeha branch** — the only combat content in the game never
   driven on the rig at all (B10, +BS).
5. **`NecroWall_Manager`** — `analysis/24` §2 names it as the writer of the five
   `NecroWall0*Destroyed` keys, but it is **not a placed actor** in the export census of any
   of the 46 gameplay maps, so the five walls' physical location is unknown.
6. **What `730_HOD`, `760_Dekanima`, `775_JigekiTemple`, `990_B33sment` are for.** Their
   classes are known; their purpose is not stated by any flag, dialogue key or challenge.
7. **`LevelSequenceActor` placements** — no census exists (the only attempt was
   `scan: loose sequence none in this world`, R91-era), so the cutscene steps cannot be listed
   per map.
8. **Whether a rest respawns enemies** (see `COVERAGE-MATRIX.md` §4 item 1) — this decides
   whether R7/R26/R32/R37/R45 are "safe rest points" or world-reset events.
9. **The prologue's own progression** (R1-R3): `Intro_Level_00/01` hold 327 / 721 exports and
   no save flag of their own; whether the intro writes anything at all is unmeasured.
