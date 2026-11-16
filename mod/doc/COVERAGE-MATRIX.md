# COVERAGE-MATRIX — every stateful gameplay system, and how the mod handles it

Research pass, 2026-11-16. This file answers one question per system: **is it synced, by
what, and is that proven live?** It is deliberately exhaustive — it enumerates every
persistent store, every Blueprint class family that carries player-visible or persistent
state, and every system the pack analysis found — and it names the ones nothing covers.

Companion documents:

* [`CAMPAIGN-ROUTE.md`](CAMPAIGN-ROUTE.md) — the same content as a start-to-finish route.
* [`SYNC-GAPS.md`](SYNC-GAPS.md) — the authoritative narrative of what is fixed and what
  still breaks.
* [`WIRE.md`](WIRE.md) — the message formats.
* `analysis/23-quest-progression-sync-map.md`, `analysis/24-quest-closeout.md`,
  `analysis/pak/answers.md`, `analysis/pak/answers-2.md` (§C7, C10, D2, D9) — the measurements
  this matrix is built from.

---

## 0. How to read a row

**Verdicts.**

| verdict | meaning |
|---|---|
| **LIVE** | seen working on two machines with log lines (and a screenshot where visual). The citing document carries the lines. |
| **BUILT** | code exists, unit-tested off-target, **not** proven on two machines. |
| **LOCAL-BY-DESIGN** | deliberately *not* shared. Owner's rule: loot and inventory are per-player except quest/key items; a few systems are per-player by nature (see §1). |
| **GAP** | nothing covers it, and it costs something in a co-op playthrough. Ranked in §3; the number in brackets is the rank. |
| **NOT-MAPPED** | no measurement exists to decide; §4 lists these. |
| **N/A** | the system the brief asked about does not exist in this build (e.g. time-of-day). |

**Lane columns.** `switch` is the `[Rules]` key in `mod/qdcoop.ini` (`qdconfig.h`). The ini
shipped in `mod/` has **every** switch at 1 except the ones named in §3; the compiled
defaults are 0 for every write path, on purpose (one crash must be attributable).

---

## 1. The lanes, and the stores they carry

Kinds are from `WIRE.md` §2; module names are `mod/native/src/*.h`.

| Kind | Lane | Module | Switch | What it carries |
|---|---|---|---|---|
| `0x10` `PLAYER` | each to other | `qdpuppet.h`, `qdmotion.h` | `PuppetSync` | transform, aim, action, 30/60 Hz |
| `0x11` `WORLD_FLAGS` | host to peer | `qdflags.h`, `qdpending.h` | `FlagSync` | absolute `NamedSaveData` set |
| `0x12` `ACTOR_STATES` | host to peer (+ intent) | `qdactorstate.h` | `ActorStateSync` | `SavableActorStates` `Map<FGuid,int32>` |
| `0x13` `INVENTORY` | host to peer (+ intent) | `qdinventory.h`, `qdinvintent.h` | `InventorySync`, `QuestItemSync` | `InventoryContents` |
| `0x14` `ENEMY` | both | `qdenemy.h`, `qdenemyid.h`, `qdenemybw.h` | `EnemySync`, `MirrorSpawn` | per-enemy rows: key, transform, HP, flags |
| `0x15` `EQUIPMENT` | each to other | `qdavatar.h` | `PuppetSync` | hand/body item + slot |
| `0x16/0x17` `EFFECT`/`EFFECT_MOVE` | each to other | `qdcombatvfx.h` | `CombatVfx` | projectiles, impacts, casts |
| `0x18` `STATUS` | each to other | proxy | — | status mask + vitals tail |
| `0x19` `ENEMY_DETAIL` | both | `qdenemybw.h` | `EnemySync` | montage, status, boss ordinal |
| `0x20` `INTENT` | peer to host | `qdpending.h` | — | kinds 1,2,3,6,7,8,9 |
| `0x21` `DIALOGUE` | each to other | `qddialogue.h` | `DialogueSync` | who is talking to which NPC |
| `0x22` `LEVEL_UNION` | both | `qdstream.h` | `StreamUnion` | what each game wants resident |
| `0x23` `TRAVEL` | both | `qdtravel.h` | `TravelSync` | map, destination actor, hard handshake |
| `0x24` `DAMAGE` | host to peer | `qdenemydmg.h`, `qdplayerbody.h` | `PlayerBody` | a host-run enemy's hit |
| `0x25` `CUTSCENE` | both | `qdsequence.h` | `CutsceneSync` | which manager each machine runs |
| `0x26` `MOVERS` | owner to follower | `qdmover.h` | `MoverSync` | absolute mover element transform |
| `0x27` `LOOT` | both | `qdloot.h` | `LootSync` | one shared drop set + tombstones |
| `0x28` `SUSPEND` | both | `qdsuspend.h` | `SuspendGuard` | peer's suspend hold, load repair |
| `0x29` `BOSS` | both | `qdboss.h` | `EnemySync` | bar, observed phase, arena exit, begin/conclusion |
| `0x2A` `OBJECTS` | host to peer (+ intent) | `qdobjectstate.h`, `qdinteract.h` | `ObjectSync`, `InteractSync` | non-savable placed objects |
| `0x2B` `ENEMY_SPAWN` | owner to follower | `qdenemy.h` | `MirrorSpawn` | construct a copy |
| `0x2C` `TRIGGER` | each to other | `qdtrigger.h` | `TriggerSync` | a placed volume overlap |
| `0x40` `KILL_XP` | each to other | `qdprogression.h` | `KillXPSync` | a measured kill award |
| `0x41` `SAVE` | each to other | `qdprogression.h` | `PartnerSaveSync` | save request + route |
| `0x48` `PLATFORM_RIDE` | each to other | `qdplatforms.h` | `PlatformRide` | a rider's place in the platform's axes |
| `0x50` `DIALOGUE_VIEW` | each to other | `qddialogue2.h` | `DialogueView` | the dialogue cursor, for display only |
| `0x54-0x56` `RESPAWN_*` | each to other | `qdrespawn.h`, `qdrespawn_engine.h` | `DeathRespawn` | death, respawn point, rest |

**The six persistent stores** (`FS_SaveSlot_C`, `World0.sav`, plaintext GVAS — measured
30-34 properties, `analysis/04-sync-surface.md`):

| store | type | who writes | lane |
|---|---|---|---|
| `NamedSaveData` | `Map<FName,int32>` | host only, monotone | `WORLD_FLAGS` + intent 1 |
| `SavableActorStates` | `Map<FGuid,int32>` | host only, first-wins | `ACTOR_STATES` + intent 6 |
| `InventoryContents` | `Map<Object,int32>` (count) | host; peer pickups are intents | `INVENTORY` + intents 2/3 |
| `Equipment` / `EquipmentSlots` / `CurrentlySelectedWeapon` / `WeaponCharges` / `DaggerCharges` | maps + scalars | **each machine its own** | `EQUIPMENT` (visual only) |
| `EnemySaveStates` | `Map<FGuid,{bValid,RespawnTime,RemainingHP}>` | the machine that ran the death; the owner's spawner owns persistence | `ENEMY` |
| `CompletedChallenges` | set of ids | each machine, evaluated locally from the mirrored NSD/inventory | (implicit) |

There is **no seventh store**. `analysis/23-quest-progression-sync-map.md` §8 confirms it, and
the save reader in `analysis/04-sync-surface.md` enumerates every serialised property.

---

## 2. The matrix

### A. Progression: flags, quests, quest state machines

| # | system (classes / keys) | store | lane (module · switch · kind) | verdict | evidence |
|---|---|---|---|---|---|
| A1 | Quest/progression flags, all of them — opaque `FName`-to-`int32` bank, `<instance><Property>` composed keys (`Arjaot_State`, `BrokeBarricade`, `RustySwordKnightGaveSword`, `CheckpointUnlockCount`, `TotalChestsOpened`, boss/ending keys) | `NamedSaveData` | `qdflags.h` · `FlagSync` · `0x11` + `INTENT 1` | **LIVE** | `SYNC-GAPS` G1; `LIVE-TEST-LOG` Session 28 (peer bank follows 0.2 s after the host writes) |
| A2 | A flag that only an actor's **load path** reads (`ShopDoorOpened` then `Shop_Door_00_BP_C`) | `NamedSaveData` | `FlagRefreshWorld` (proxy) · `FlagSync` | **LIVE** | R105 closed, Session 27: `flag refresh: ShopDoorOpened=1 -> Shop_Door_00_SM [Shop_Door_00_BP_C].SetToOpenState ran` |
| A3 | The **world reaction** to a flag: `GameInstance.GI_UpdateQuestState` + `GI_NotifyWorldStateChanged` | — | `QuestRefreshPump` (proxy, 250 ms coalesce) · `FlagSync` | **LIVE** | Session 28 run 1: peer `LibrarianQuestStateManager … InternalState 0 -> 1` 0.2 s after the flag arrived |
| A4 | Quest state machines — `ActorQuestStateManager_C` **x14**, `ActorQuestState_C` **x28**, `QuestStateInterface_C` | none of their own; `States`/`DesiredState`/`InternalState` recomputed | rides A3 (no lane of its own) | **LIVE** | `analysis/24` §2c/§3 (placement + `States` count per manager); Session 28 run 1 |
| A5 | The **write** path: `ActivatableGroup_C` (95), `ActivatableNamedSaveDataSetter_C` (10), `NamedSaveDataSetterVolume_C` (3), `QuestForceUpdateVolume_C` (2), `SaveFunctions_C.SaveNamedData`, `WorldEventReceiver_C` (204) | `NamedSaveData` | local on each machine; the **result** is mirrored (A1) — the chain is never replicated | **LIVE** | `analysis/23` §5; the ten setter actors and their keys in `analysis/24` §2a |
| A6 | `NSD_ActorHider_C` — **304** placed actors whose visibility is an NSD condition | `NamedSaveData` | A3 (it is a `WorldEventReceiver`) | **BUILT** (rides A3; never separately measured) | census (this pass), `analysis/24` §4 |
| A7 | The 14 quests and their completion conditions (`cha_Quest_*`) | NSD keys + 1 inventory entry | A1/A3 + `QuestItemSync` for `cha_Quest_Mushroom` | **LIVE** (the mechanism) | `analysis/24` §1 table; the challenge board evaluates locally from mirrored state |
| A8 | The 63 `FS_ChallengeData` definitions / `CompletedChallenges` | NSD + inventory | evaluated per machine from mirrored state | **LIVE** (by construction) / per-save | `analysis/24` §1; `analysis/pak/answers.md` Q2 |
| A9 | Shop progression counters — `ShopAmountSpent`, `ShopUnlockFinished`, `ShopkeepUnlock1..4`, `ShopProgressionKey/*`, `ShopPurchasedKey/*` | NSD + inventory | A1 + `QuestItemSync` | **LIVE** (flags) / keys proven | `progression.md` Round 2/3 (14 KeyItems grants) |
| A10 | `SavePointsVisited` (`Array<Object>`), `LastTouchedCheckpoint` (`Object`), `SavePointID` | object arrays | **nothing** — only the `CheckpointUnlockCount` counter is mirrored | **GAP (3)** | `analysis/04` save table; `answers.md` Q3 |
| A11 | `UnlockedTeleporters`, `ShopTeleporterDestinations` (`Array<Object>`) | object arrays | **nothing** — only `TeleporterUnlockCount` is mirrored | **GAP (3)** | same |
| A12 | Endings — `Ending1..4Complete`, `EndingSeen`, `EndingWakeOnBeach`, `cha_Endings` | NSD | A1 + `CutsceneSync` | **LIVE** | Session 29 runs 10, 12-14 |
| A13 | `LevelListStack` / `TriggerLevelListStack` / `SafeLocLevelListStack` (save-side) | `UFS_LevelList` stack | `StreamUnion` (`0x22`) — rebuilt as a union, not copied | **LIVE** | `analysis/pak/answers-2.md` §C10.6; Sessions 22/24 area follow |

### B. Interactables: doors, chests, breakables, levers, secret walls, keys

| # | system (classes) | store | lane | verdict | evidence |
|---|---|---|---|---|---|
| B1 | `SavableActor_C` one-shot state, keyed by the **authored `FGuid`** | `SavableActorStates` | `qdactorstate.h` · `ActorStateSync` · `0x12` + `INTENT 6` | **LIVE** | `world-interact.md` Round 2/3: `actor-state intent applied` then `actor-state refresh ok … opened=1` on `SwordShrineGate_BP_C` |
| B2 | `Chest_C` (**61** placed) | `SavableActorStates` (GUID) | B1 | **LIVE** | Session 17: `actor-state refresh ok Chest5 [Chest_C] … opened=1` |
| B3 | Chests with **no GUID** at all — `SG_Chest_BP_C`, `Castle_Chest_00_BP_C`, `Sci_Chest_00_C`, `Josami_Des_Chest_BP_C` (its own `Opened` bool) | actor bool | `qdobjectstate.h` · `ObjectSync` · `0x2A` | **LIVE** | R67 measured `savable=no guid=-`; `SYNC-GAPS` §5.5 |
| B4 | Doors — **52 families** incl. `Door_Base_C`, `Shop_Door_00_BP_C`, `SecretZoneDoor_BP_C` (+Blue/Green/Red), `SZ_Door_02_BP_C`, `SZ_JailDoor_BP_C`, `Lib_FrontDoor_BP_C`, `Lib_IntDoor_00_BP_C`, `Lib_MidDoor_BP1_C`, `Lib_TrapDoors_00_BP_C`, `Lib_Door_01_BP_C`, `LibGate_00_BP_C` (+Circle), `Lib_BossDoor_BP_C`, `Lib_TreasureGate_BP_C`, `KitchenDoor_BP_C`, `M_DoorSmall_00_BP_C`, `M_WodasGate_00_BP_C`, `EggDoor_BP_C`, `Queen_Door_Bp_C`, `CastleFrontDoor_BP_C`, `Castle_Door_Int_BP_C`, `OW_BridgeDoor_C`, `OW_TP_Interior_Door_C`, `OW_TP_Shutter_BP_C`, `OW_Tower_RobotShutter_BP_C`, `AquaDoor_00_C`, `AquaGate_00_BP_C`, `AquaGate_01_BP_C`, `AquaBossDoor_BP_C`, `AQR_BossExitDoor_C`, `AqrStatuePuzzleDoor_C`, `GraveyardBoss_Gate_C`, `DesBoss_Gate_C`, `Necro_Boss_Door_C`, `JosamiPrisonDoor_C`, `PT_Door_C`, `Maus_QG_Door_C`, `SG_Door_Beepee_C`, `SwordShrineGate_BP_C`, `ForbiddenDoor_BaseClass_C` (+Forbidden/Office/Tower/BloodRitual), `JigekiDoor_BP_C`, `ROS_Door_BP_C`, `Ros_Cage_Gate_BP_C`, `DAquaDoor_01_C` | GUID or actor bool | `qdinteract.h` on both lanes · `ActorStateSync` / `ObjectSync` · `InteractSync` | **LIVE** for open and **close** (`SetToOpenState(want)`), 3 sessions | `world-interact.md` Round 3: `interact: door anim Deactivate key=709D84E1 state=0` on the partner |
| B5 | Breakables — **33 families**: `Permabreakable_base_C` (`Barrel_C`, `Crate_C`, `ScifiCrate_BP_C` + `_Hard`/`_Mossy`/`_Rusty`), `BreakableBarricade_C`, `Wellcover_Breakable_C`, `Pot_Base_C` and `Pot_Fat`/`Pot_Small`/`Pot_Tall`/`Po*_Sci`, `M_Kitchen_Pot_*_BP_C`, `OW_BigCrate_BP_C`, `OW_MachineBarrel_BP_C`, `or_pot_tall_bp_C` | GUID (`Permabreakable`) / `Broken` bool (`Pot_Base` is a plain `Actor`) | B1 for savables, `ObjectSync` for pots (`visual=Break`) | **LIVE** | `Barrel_C`, `Crate_C`, `Crate_Mossy_C` peer-to-host; a pot with **no drop on the receiver** |
| B6 | Secret mechanisms — **32 classes**: `SecretWall_C`, `ScienceSecretWall_C` (door-shaped, `DirectBoolClass`), `Lever_C`, `OW_Lever_00_BP_C`, `MausoLever_00_C`, `AquaLever_00_C`, `Lib_Switchbox_BP_C`, `RuneButton_C`, `DaggerButton_*`, `gate_switch_00`, `Gargoyle_Switch_C`, `Wodaso_Rune_BP_C` | actor bool `Opened` (writes no container) | `ObjectSync` + `InteractSync` (`SetToFinalState`/`Activate`) | **LIVE** | `object intent applied: SecretWall25 … visual=SetToFinalState`; `ScienceRoomDoor2 [ScienceSecretWall_C]` |
| B7 | Lift/elevator **controls** — `Lib_LiftSwitch_BP_C`, `OW_Lift_Lever_00_BP_C`, `LabElevatorSwitch_C`, `OW_Lever_00_lift_BP8` | — | **refused on purpose** (they drive a mover the mover lane carries) | **LIVE (as a refusal)** | Session 26 fault: setting `Activated` on them crashed the game; runs 17/18 clean |
| B8 | Key-locked doors — `Door_Base_C.CheckRequiredItems`, `Chest_C.TryUseKey`, `AQR_KeyRecepticle_BP_C`, `SwordShrineGate_BP_C` | `InventoryContents` | `qdinventory.h` · `QuestItemSync` (key granted to both) | **LIVE** | `SYNC-GAPS` G3 ("Keys and doors use both lanes"); 14 KeyItems grants measured |
| B9 | Key a player consumed (count to zero) | `InventoryContents` | `QuestItemSync` tombstone (`count 0`) + `RemovalNeeded` | **LIVE** | `progression.md` Round 3: `quest item removed here: … -1 (host holds 0, this machine held 1)` |
| B10 | `BreakableBarricade_C` (1 placed) — the tutorial barricade | `SavableActorStates` (it *is* a `SavableActor_C`) | B1 | **LIVE** (the class family; that one actor not separately measured) | its flag sibling `BrokeBarricade` is in the sample save |
| B11 | Puzzle actors with intermediate state — `Lib_Book_00..04_BP_C` (287 placed), `Lib_Book_Open_BP_C`, `Library_RotateBridge_C`, `Library_LowerBridge_C`, `AqrStatuePuzzleDoor_C`, `SG_Door_Beepee_C`, `Jigeki_Rune_BP_C`, `RuneButton_C`, `SuperCubePuzzleDoor` | NSD completion flag only (`LibBookPuzzleComplete`, `ROSBookPuzzleComplete`, `StoneGardenPuzzleFinished`) | the **completion** is mirrored (A1); the puzzle's *per-object* position is not | **GAP (6)** | `analysis/24` §2 (the flags); no lane reads a book's rotation |
| B12 | Pressure plates `DS_Plate_BP_C` (**24**), `DS_Plate_F_BP_C`, `DS_Plate_Small_*_BP_C` | actor bool | none — a plate is an event source | **LOCAL-BY-DESIGN** (each machine fires its own volume) — *but* see GAP 5 | census (this pass) |

### C. Enemies, combat, hazards

| # | system (classes) | store | lane | verdict | evidence |
|---|---|---|---|---|---|
| C1 | Enemy identity — spawner `GUIDs`, placed `<world>/<level>/<name>` hash, summon `(summoner, point, index)` | `EnemySaveStates` | `qdenemyid.h` · `EnemySync` · `0x14`/`0x2B` | **LIVE** | `SYNC-GAPS` §2 (identity); 421/421 authored GUIDs found in the pak |
| C2 | Enemy HP, transform, death, montage, status | `EnemySaveStates.RemainingHP` | `qdenemy.h`, `qdenemybw.h` · `0x14`/`0x19` | **LIVE** | 71 shared-HP pairs, 0 unequal, 0.1 HP max delta (Session 21); `61 of 61 equal` (Session 29 run 46) |
| C3 | Ownership (who simulates an enemy) | — | `qdenemyown.h` (nearer player, host breaks a tie, 300 cm grab margin) | **LIVE** | R45 closed; Sessions 29 runs 38/43 |
| C4 | A copy on the machine that never spawned it | — | `MirrorSpawn` · `0x2B` | **LIVE** | Session 16: item 15/15, boss 25/25 |
| C5 | Enemy damage to the partner's own pawn | — | `qdplayerbody.h` + `qdenemydmg.h` · `PlayerBody` · `0x24` | **LIVE** | Sessions 27/28: `player body hit … -> sent`; `damage from enemy … hp 400 -> 380` |
| C6 | Player damage to an enemy | `EnemySaveStates` | `INTENT 7` then the owner applies `TryApplyDamage` | **LIVE** | Session 27: `220.0 -> 176.0` on both; dragon in-fight `3500 -> 3300` |
| C7 | Enemy projectiles | — | `qdcombatvfx.h` · `CombatVfx` · `0x16`/`0x17` | **LIVE** | one shot / one `local` line, gated on ownership |
| C8 | Player projectiles, casts, impacts, weapon-in-hand | — | `qdcombatvfx.h`, `qdavatar.h` · `CombatVfx` · `0x16` | **LIVE** | s32 run 2: axe/hammer/spear/greatsword/gun/claw/scythe/dagger; crossbow bolt round 3 |
| C9 | Player status effects (poison, fire, weaken, disease, sticky, bind, MP) | — | `STATUS` `0x18` mask | **LIVE** | s32; `test_status` |
| C10 | Spawners and respawn — `EnemySpawner_C` (a `SavableActor_C`, the sole writer of `EnemySaveStates`), respawn timer | `EnemySaveStates.RespawnTime` | `ENEMY` + the owner's spawner | **LIVE** | `analysis/18-d4-respawn.md`: two kills 121 s apart, same GUID |
| C11 | Traps and hazards — `DamagingCollision_C` (**489**), `Boner_*_TrapDecoy_C` (784), `Robo_Mechaknight_TrapDecoy*`, `FlyingRobot_CA_01_TrapDecoy_C`, magma puddles (`DemonBoss_FightController_C`) | — | each machine's own volume damages its own pawn | **LOCAL-BY-DESIGN** for static hazards; **GAP (5)** for a *spawned* hazard (the peer sees no lava) | `analysis/pak/answers-2.md` §D9.4/§D9.7 |
| C12 | Enemy **phases** (`Phase`, `bBoss`, `OnSubStateChanged`) | — | `ENEMY_DETAIL.boss` carries an observed ordinal only | **GAP (1)** | the strings exist in the exe and in **zero** cooked packages; each machine runs its own copy's phase script |
| C13 | Boss grab/orb attacks against the partner's body | — | `0x24` + `qdplayerbody.h` | **LIVE** | Session 29 runs 44/46: the Queen's own copy fired `Queen_FastProjectile_BP_C` at the peer |

### D. Bosses, arenas, gates

| # | system (class / flag) | store | lane | verdict | evidence |
|---|---|---|---|---|---|
| D1 | Boss **bar** (one bar per game, negotiated `ASK`/`ACK`) | — | `qdboss.h` · `EnemySync` · `0x29` | **LIVE** | `780_SZ_Depths`: same key `17181166`, HP 65535 to 5616 identical, bars down 28 ms apart |
| D2 | Boss **fight start** from one player's overlap (`ActivationTrigger_C`) | — | `qdtrigger.h` · `TriggerSync` · `0x2C` | **LIVE** | Sessions 26/28: `trigger: replayed the partner's entered …`; `DoTrigger` follow-through |
| D3 | Boss **begin** announced (the Queen's greeting end, the Necronomicon lectern) | — | `kBossBegin` on `0x29` | **LIVE** | Session 29 runs 13/14/43 |
| D4 | Boss **death / conclusion** — a downed boss waits for a dialogue; `RewardDialogueEnded` / `EndedDeathDialogue` / `Ended` | NSD (`<Boss>Dead`) | `BossConclusionNote/Pump` + A1 | **LIVE** | Session 29: `ROSDragonDead`, `WhaleKingIsDead`, `BridgeLordDead`, `NecronimiconDefeated` in both banks |
| D5 | Boss flags — all 8: `WhaleKingIsDead`, `NecronimiconDefeated`, `DemonBossesKilled`, `BridgeLordDead`, `ROSDragonDead`, `DragonLordDead`, `GiantSwampFish_IsDead`, `RedLordsDead` | NSD | A1 | **LIVE** | Sessions 26/28/29 tables |
| D6 | The **Shrine Boss** (`100_GR`) | — | D1/D2/D4 | **LIVE except the flag** | Session 26: key `03FDBC21`, 260 to 60 both, dead both, **no flag in the content** |
| D7 | Arena **gates** — `GraveyardBoss_Gate_C`, `Lib_BossDoor_BP_C` (x3), `Necro_Boss_Door_C`, `DesBoss_Gate_C`, `AquaBossDoor_BP_C`, `AquaBossExitDoor_C`, `WebBossWall_BP_C`, `JosamiPrisonDoor_C`, `Lib_TreasureGate_BP_C` | `SavableActorStates` / door base | B1 / B4 | **LIVE** (lane); gates named in D9 §D9.3 | `analysis/pak/answers-2.md` §D9.3 parent-class table |
| D8 | A far partner is pulled into the arena | — | `BossPullIn` (`boss_pull_in`, default on) | **LIVE** | Session 26 fault 2 (the pull-in is a 170 m drop) |
| D9 | One body per boss (no double run, sticky ownership, 1024-record table) | — | `qdenemyown.h` | **LIVE** | Session 29 runs 37, 43, 46 |
| D10 | The Queen's **second phase / orb cutscene / `Queen_Nude_Data` form / Sipeha branch** | — | none | **GAP (2)** | Session 29 run 15: phase 1 at HP 0 waits at minus 200 HP for 45 s |
| D11 | Ending **wake** (`EndingWakeOnBeach` second half) | NSD | `PostEndingWakeOnBeach` + `StopCameraFade` fallback at 15/25 s | **GAP (10)** (the fallback works; the game's own wake never plays under the rig) | Session 29 runs 43/46 |
| D12 | Arena post-process (`Queen_BossArena_PPC`, `ROS_BossArenaPPC`, `Boss_Env_PPC`) and boss music (`Music/3-Boss/*`) | — | none | **GAP (9)** | `analysis/pak/answers-2.md` §D9.4; "mismatched boss music reads as broken" |

### E. The players themselves

| # | system | store | lane | verdict | evidence |
|---|---|---|---|---|---|
| E1 | Partner body, transform, animation, action | — | `qdpuppet.h`, `qdmotion.h` · `PuppetSync` · `0x10` | **LIVE** | `puppet at x,y,z … err=0.0cm` |
| E2 | The body as the game's own `FS_PlayerChar_C` (so Blueprints can target and damage it) | — | `qdplayerbody.h` · `PlayerBody` | **LIVE** | s32; `player body: class FS_PlayerChar_C resolved` |
| E3 | Vitals (HP / max on screen) | — | `STATUS` vitals tail, HUD `DrawRect`/`DrawText` | **LIVE** | Session 27: `vitals: partner hp 37/100`, bar in the screenshot |
| E4 | Stamina | — | — | **N/A** | the player Blueprint declares none (`BaseJumpStaminaCost` is a leftover) |
| E5 | Death, respawn, the minus-10-percent-per-death penalty, rest points | mod sidecar | `qdrespawn.h` + `qdrespawn_engine.h` · `DeathRespawn` · `0x54-0x56` | **LIVE** | `death-respawn.md` Round 2: 42 PASS / 0 FAIL, `input=1 walking=1`, two Continue presses refused |
| E6 | The Game Over screen and its world reload | — | refused at `OpenLevel`/`RestoreFromSave`/`DeathLoad` | **LIVE** | `respawning is a position…` |
| E7 | Kill XP to both players, once each | `XPAmount` (save slot) | `qdprogression.h` · `KillXPSync` · `0x40` | **LIVE** | `progression.md` Round 3: `xp: paid 140 … (412 -> 552)` + `xp: this death paid 140 here` |
| E8 | Level-up / `MaxProgressionLevel` / `WeaponXP` / `WeaponLevels` / blacksmith upgrades | save-slot scalars | none | **LOCAL-BY-DESIGN** | `SYNC-GAPS` §4 ("weapon levels … per-player by nature"); `analysis/pak/blueprint-surface.md` `FS_SaveSlot` |
| E9 | Currency / coins (`Coin_Item_BP_C` 111, `2Coins_Item_BP_C` 73, `3Coins_Item_Bp_C` 80, `CoinBag_ItemBP_C` 62) | `InventoryContents` | `QuestItemSync` **excludes** it | **LOCAL-BY-DESIGN** | owner rule; `progression.md` coverage table |
| E10 | Equipment / hand weapon / off-hand | `Equipment`, `EquipmentSlots` | `qdavatar.h` · `EQUIPMENT` `0x15` | **LIVE** (visual) / per-player (state) | s32 run 2 (8 weapon families in the partner's hand) |
| E11 | Weapon charge / dagger charge | `WeaponCharges`, `DaggerCharges` | none (the visual lane carries the effect) | **LOCAL-BY-DESIGN** | `analysis/pak/answers-2.md` §C7.5 |

### F. Items and loot

| # | system | store | lane | verdict | evidence |
|---|---|---|---|---|---|
| F1 | Inventory mirror (host to peer grants) | `InventoryContents` | `qdinventory.h` · `InventorySync` · `0x13` | **LIVE** | `write api: AddItemToInventory … read back 0 -> 1`; `inventory grant applied:` |
| F2 | Peer pickup as a cumulative intent; peer use as a removal | same | `qdinvintent.h` · `INTENT 2`/`3` | **LIVE** | `SYNC-GAPS` G3 |
| F3 | Quest/key items only (503 assets, 26 families) | same | `progression::IsQuestItem` · `QuestItemSync` | **LIVE** x3 sessions | `65 entries (live 69)`, 14 KeyItems grants, `GiveAllConsumables` gave 0 grants |
| F4 | Enemy drops — one shared set keyed by `{enemy GUID, ordinal in that death}` | — | `qdloot.h` · `LootSync` · `0x27` | **LIVE** | Sessions 25/27: `loot: local pickup DD452DFD#0 … tombstoned`, the partner hides its copy, `PutAway`/hide 15 s later |
| F5 | Placed pickups — `ItemPickup_Base_C` and family (`Herb_Item_BP_C`, `HealingPotion_Item_BP_C`, `CrystalFragment_Pickup_BP_C`, `TeleporterFragment_Pickup_BP_C` x16, `RadiantFruit_Pickup_BP_C`, every `*Cure_Pickup_Bp_C`, `WeaponXPPotion_Pickup_Bp_C`, `FishWife_Egg_Item_Bp_C`, `SkullKeyRelic_BP_C`, `King_Hammer_Pickup_C`) | actor `Acquired`/`PickedUp` | `ObjectSync` (+ `QuestItemSync` for a key one) | **LIVE** for the class family (`ItemPickup_Base_C` itself, `Acquired` gate measured) | Sessions 24/25: the pickup answers its own `InpActEvt_Interact`; `Acquired=1` before any press was the blocker |
| F6 | Chest contents | — | paid **only** by the opener; counters via A1 | **LIVE** | `SYNC-GAPS` §5.5 step 4 ("Nothing paid out twice") |
| F7 | Shop purchase / sell screen (`Merchant-OpenShopScreen`, `Merchant-OpenSellScreen`) | `InventoryContents` + `ShopPurchasedKey` | per-player UI; the keys cross | **PARTIAL, GAP (7)** (the peer's own shop menu is its own; stock keys are host-owned and mirrored, but the *purchase* is local) | `analysis/24` §2b; `SYNC-GAPS` §4 |

### G. NPCs, dialogue, quest givers

| # | system | store | lane | verdict | evidence |
|---|---|---|---|---|---|
| G1 | Who is talking to which NPC; the NPC is blocked for the **other** player only | `NPC_Base_C.BlockInteract`, `RunningDialogue`, `PlayerIsTalkingTo` | `qddialogue.h` · `DialogueSync` · `0x21` | **LIVE** | `dialogue: BLOCK Fishwife_Ghost_BP … BlockInteract=1` (read back) |
| G2 | A concurrent start on the same NPC | — | reservation + `DialogueManager.End`/`DialogueEnded` | **LIVE** | `dialogue: PREVENTED a concurrent start …` |
| G3 | The partner **sees** the conversation (cursor: asset, sequenceIndex, entry, options, chosen) | — | `qddialogue2.h` · `DialogueView` · `0x50` | **LIVE** | `dialogue.md` Round 2, 28 PASS / 0 FAIL: widget, camera, input lock, advance followed |
| G4 | Dialogue **conditions** (`EFS_DialogueConditionType`/`Behaviour`/`Comparator`) | NSD + inventory | evaluated locally from mirrored state — nothing is sent | **LIVE** (by construction) | `analysis/pak/answers-2.md` §D2.3 |
| G5 | Dialogue **outcome** — `NamedSaveDataValueChanges` (`ENSDCM_Add`/`Set`), `ActionToRun`, `ActionToRunAtEnd` | NSD + inventory | A1 + F1 (choices are *not* sent as choices) | **LIVE** | `SYNC-GAPS` §3: "Choices are not sent as choices. Progress flows through flags" |
| G6 | NPC reward table — `NPC_GiveNextReward`, `QuestRewardEntry`, `Scientist_GiveOfficeKey_Action`, `Chef_TurnInAction`, `Raider_TurnInNecklace`, `SecretSkullDoor_TurnInAction`, `Merchant_CheckForDucks`/`BoughtALot` | inventory + NSD | F3 + A1 | **LIVE** (mechanism) | `analysis/24` §4b (action to item table) |
| G7 | NPC roster — `Knight_Arjaot_NPC_BP_C`, `Knight_Ekonta_NPC_BP_C`, `Knight_Ippibi_NPC_BP_C`, `Merchant_NPC_BP_C`, `Fishwife_NPC_BP_C`, `Fishwife_Ghost_BP_C`, `ChefCrowEvent_NPC_C`, the 9 `Knight_*_NPC_Ghost_BP_C`, `RaiderSailor_2/4_Ghost_NPC_BP_C`, `SecretSkullDoor_NPC_BP_C`, `DummyNPC` (Bridge Lord / Necronomicon) | per-actor | G1-G5 | **LIVE** | census (this pass) + `analysis/24` §2b |
| G8 | Journal / bestiary — `NPCsInJournal`, `CollectedNotes` (46), `UQD_Journal_Data`, the 10 `Notes/Tutorials/*` | object sets | none | **LOCAL-BY-DESIGN** | `SYNC-GAPS` §4; `analysis/24` §4b |
| G9 | Bestiary completion counter `LibrarianBestiaryCompletionPercent` | NSD | A1 | **LIVE** | it is a flag, so it crosses; `cha_Quest_Lib` completes on both |
| G10 | `KilledEnemyCounts` | save map | none | **LOCAL-BY-DESIGN** | per-player bestiary |

### H. World: streaming, travel, cutscenes, movers, saves

| # | system | store | lane | verdict | evidence |
|---|---|---|---|---|---|
| H1 | Level streaming — the **union** of what is around both players; never hide a level the partner stands in | save-side level-list stack | `qdstream.h` · `StreamUnion` · `0x22`, veto at `SetShouldBeLoaded`/`SetShouldBeVisible` | **LIVE** | Sessions 22/24: no veto and no forcing within 60 m; the jam it caused (black world) is measured and closed |
| H2 | `LevelManagerTrigger_C` (49), `FS_LevelManagerVolume` (6), `LevelStreamingDynamic` (43) | — | H1 | **LIVE** | `analysis/pak/answers-2.md` §C10 (6/6 volumes, 49/49 triggers, 78 refs) |
| H3 | Entering an **area** (a sublevel, not a `UWorld`) | — | `TravelSync` phase 4 (`TP_AREA`), `ForceLevel` pin, `ClearAreaPin` | **LIVE** | R150a closed Session 22: `travel: entered the partner's area 770_SZ_Swamp by streaming it` |
| H4 | In-world teleport (`Teleporter_C` x21, `MiniTeleporter_C`, `SecretTeleporterDestination_C`) | — | `qdtravel.h` · `TravelSync` · `0x23` (`FS_PlayerChar.TeleportToLocation`) | **LIVE** | `travel: local destination …` then `travel: followed …` |
| H5 | A **hard** map switch (a different `UWorld`: `900_SipehaWorld`, `990_B33sment`, `997/998/999`) | — | `GameInstance.LoadMapWithLoadingScreen` + `HARD_BEGIN`/`ARRIVED`/`RESUME` | **LIVE** | Sessions 22/24: `travel: both machines arrived in 997_FinalZoneCutscene`; R152 (repeated-load crash) closed by `WorldTeardownReset` |
| H6 | `Checkpoint_C` (**34** placed) — the rest, `CheckpointUnlockCount`, the respawn point | NSD + mod sidecar | A1 + E5 | **LIVE** | `flag CHANGE CheckpointUnlockCount : 1 -> 2`; `respawn: rest at …/Checkpoint` |
| H7 | Cutscenes / `LevelSequenceActor`, `FB_SequenceCamera_C`, `Ending1CutsceneManager_C`, `998_QueenCutscenes`, `999_SipehaCutscenes`, `997_FinalZoneCutscene` | — | `qdsequence.h` · `CutsceneSync` · `0x25` | **LIVE** (play, or hold + resume together) | Session 29 runs 10, 12-14 |
| H8 | Movers — `Mausoleum_Lift_00_BP_C` (x11), `OW_Lift_00_BP_C` (x5), `OW_TimedLift_00_BP_C` (x6), `ROS_Lift_BP_C` (x5), `IntroDungerLift_BP_C` (x4), `OfficeLift_00_BP_C` (x3), `SZ_Lift_00_BP_C` (x3), `Castle_Lift_00_BP_C`, `PT_Lift_00_BP_C`, `Lab_Lift_BP_C`, `AQR_NoSoftLockLift_C`, `OW_BrokenLift_00_BP_C`, `SZ_Lift_01_BP1_C`, `Elevator_Base`, `AquaElevator_00_BP_C`, `Lib_ElevatorSmall_00BP_C`, `LabUG_Tram_BP_C`, `DoMovingPlatformCollisionHack` families | — | `qdmover.h` · `MoverSync` · `0x26` (the moving **element**, e.g. `ElevatorPlatformRoot`) | **LIVE** | s32 run 2: `platforms: mover id=C6616CB4 now follows element 1 (Cube)` |
| H9 | A rider standing on a moving platform | — | `qdplatforms.h` · `PlatformRide` · `0x48` | **LIVE** | s32 runs 1-2 both directions, including in motion |
| H10 | A world that reloads on a machine (death Continue, menu load) | — | `qdrespawn_engine.h` + `SuspendGuard` | **LIVE** | Continue refused; a suspend load repaired |
| H11 | Timed suspend saves | `World0_Suspend_N.sav` | `qdsuspend.h` · `SuspendGuard` · `0x28` | **LIVE** | `suspend: peer holds BlockSuspendSaves=1`; the host re-applies state |
| H12 | A save on one machine means the other saves its own slot | — | `qdprogression.h` · `PartnerSaveSync` · `0x41` (+ `HK_SaveGameToSlot`) | **LIVE** | `progression.md` Round 3, both directions, file mtime moved |
| H13 | Menu pause (`SetGamePaused` from Status/Inventory/Pause/Photo/Checkpoint) | — | `SuppressMenuPause` (veto at the dispatch) | **LIVE** | `analysis/13b-pause-surface.md`; `SYNC-GAPS` §1 |
| H14 | `SuspendSaveBlockingVolume_C` (22 placed) | — | H11 (the game's own volume still applies locally) | **LOCAL-BY-DESIGN** | census (this pass) |

### I. Presentation the pair will notice

| # | system | verdict | note |
|---|---|---|---|
| I1 | Boss HP bar | **LIVE** | D1 |
| I2 | Partner HP on the HUD | **LIVE** | E3 |
| I3 | Music and ambience — `MusicController`, `MusicControlTrigger_C` (**142** placed), `ActivatableSound_C` (28), `AudioBlocker_C` (508), `SipehaStatueAmbience_C` | **GAP (9)** | each machine runs its own triggers; one player walking into a zone changes only their music. Cosmetic, but boss music disagreeing is the visible case (`answers-2` §D9.7) |
| I4 | Fog / atmosphere — `FogLightActor_C` (243), `FogLightComponent_C` (309), `Cloud*_FogCard_C`, `SwampFog_*`, `EnemyFogControlTrigger_C` (**73**), `FS_Light_Activatable_C` (939), `BP_LightBeam_C` (456) | **GAP (11)** | all local, all visual. `EnemyFogControlTrigger_C` is enemy-driven so it usually agrees |
| I5 | `ENVControl/` family (`ENVController`, `ASideTrigger`/`BSideTrigger`, `ENVControlData`, `ScaryDarkness_ENVData`) — flagged in `ANALYSIS.md` §4.5 as a **possible** co-op hazard | **N/A — measured 0 placed instances** across the 46 gameplay maps (census, this pass). The assets exist; the world does not place them, so the hazard ANALYSIS.md asked to check is retired |
| I6 | Time-of-day / weather | **N/A** | no such subsystem in the content or the binary surface |

---

## 3. The GAPs, ranked by damage to a co-op completion

Ranked by: does it stop the pair finishing, then does it make one player's screen wrong.

| rank | gap | why it hurts | fix shape |
|---|---|---|---|
| **1** | **BOSS PHASES are not a synced value** (C12). Each machine runs its own copy's phase script on shared HP. | A phase boss (the Queen, the dragons, the Necronomicon, the Red Lords' revive) can behave differently on the two screens: one enters phase 2 while the other does not, or two `Kill` paths fire. The Queen's *second phase at all* is unproven (D10), and a second-phase boss **is** the end of the game. | The strings (`Phase`, `bBoss`, `OnSubStateChanged`) exist only in the exe and in **zero** cooked packages, so there is nothing to read on the content side. The honest route is to sync the state index the boss itself exposes — `Queen_BP!?CurrentStateIndex` is already in the rig's `Phase` probe list — and treat it as an opaque ordinal the follower force-sets from the owner. |
| **2** | **The Queen's second phase / orb cutscene / `Queen_Nude_Data` form / the Sipeha branch** (D10). | Run 15: phase 1 at HP 0 satisfies then `DoStateTransition`, and she waits at minus 200 HP for 45 s. The rig never reached phase 2, so **no one has seen the final fight work in co-op at all**; ending 1 was only reached by killing her in the state that ends the game. Without this, "finish the game together" is not measured. | Rig work first (`session26.ps1 -Only Queen` with a scripted phase advance; `Queen_BP!OrbProjectileHitPlayer` is already in the probe list), then either (a) the same opaque phase ordinal as gap 1, or (b) announce the transition montage/cutscene (`kBossBegin` already does this for the intro). |
| **3** | **Fast-travel and rest-point sets are not mirrored** (A10, A11): `UnlockedTeleporters`, `SavePointsVisited`, `LastTouchedCheckpoint`, `ShopTeleporterDestinations`. Only the counters (`TeleporterUnlockCount`, `CheckpointUnlockCount`) cross. | The peer's own teleport menu will not list a destination the host unlocked, and its `LastTouchedCheckpoint` (what a load restores) can be a different one. A player who has unlocked nothing cannot fast-travel with the partner, so the pair must walk everywhere together or split and get lost. `cha_Challenge_AllTeleporters` needs 10 + 32 on **both** banks, so it is also an achievement-divergence bug. | A 7th mirrored set: `Array<Object>` of soft paths, host-authoritative, monotone add, on change + 1 Hz, applied through the game's own unlock call — the same shape the inventory lane already uses for key items. Cheap: 10 + 32 entries. |
| **4** | **A rest does not obviously respawn enemies on both machines** (unmeasured; §4 item 1). If a checkpoint rest clears or respawns enemies the way a load does, the two machines hold different kill sets from that moment, and a **boss could come back on one machine only**. | Compounds with the death lane: death and respawn are per-player now, so a player rests mid-fight and the world may reset for one of them. | One measurement (rest, then compare the per-GUID enemy dump) decides it. If it does reset, a rest is a world event and belongs on `0x28`/`0x2C` (a `WORLD_FLAGS` + enemy-set re-apply). |
| **5** | **Spawned hazards are host-only** (C11): `DemonBoss_FightController_C` spawns despawnable magma puddles and applies lava damage; the peer sees no lava. Plates and trap decoys a peer triggers are also local. | A fight where the floor is lethal on one screen and safe on the other. Not fatal to completion, but it makes the Demon Bosses fight unfair and indescribable. | D9's rule is "do not replicate the controller". The minimum that makes it fair is the *damage*: the host can already send `0x24` for a `TryApplyDamage` it applies to the partner's body, and the controller's `TryApplyDamage`/`DamageAmount`/`DamageTags` are its entry. |
| **6** | **Puzzle intermediate state** (B11): 287 `Lib_Book_*` actors, `AqrStatuePuzzleDoor_C`, `SG_Door_Beepee_C`, `Jigeki_Rune_BP_C`, `Library_RotateBridge_C`. Only the completion flags cross. | Two players solving a rotate-the-books puzzle each see their own book positions; the completion flag appears to "just happen". If a puzzle needs a book moved in both machines' state to open the door, one player's clicks do nothing visible for the other. | The flags already fire (A1); the per-object state is another `ACTOR_STATES`/`OBJECTS` shape if the class exposes one — one survey (`dev props` on a book) decides whether it does. |
| **7** | **A player's own shop transaction is local** (F7) while the stock keys are host-owned. | The peer buys from its own menu with its own money, so its `ShopAmountSpent` only moves if the shop writes the flag on *its* machine — which then arrives as an intent. Probably fine, but unmeasured; a dupe (the peer buys the last `ShopPurchasedKey`) would be a real divergence. | Measure once: peer buys, compare `ShopAmountSpent` and the `ShopPurchasedKey` sets. |
| **8** | **`TotalChestsOpened` may count differently per machine** — `ObjectSync` applies a chest without payout *by design*, and `ActorStateSync`'s `RestoreFromSave` is the game's own load path. | `cha_Challenge_AllChests` = 108 is a completion target; a counter that differs by a few makes "all chests" unachievable together. The lane's doc says the partner's counter must arrive only through the flag lane (so it should be right) — this is a verify, not a known break. | Grep the two banks after a chest is opened by the peer; `SYNC-GAPS` §5.5 step 4 is exactly that check. |
| **9** | **Arena post-process and boss music** (D12, I3). | Cosmetic. D9 names it as "reads as broken" rather than a blocker. | The manager is per-machine Blueprint state, and the cutscene lane already publishes "which manager is running", so music could ride the same shape. Lowest value; do last. |
| **10** | **The ending's own wake-up never plays** (D11); the mod's fallback (fade stop at 25 s) is what restores the picture. | Post-ending the screen is drawn but at mean brightness 4-6 (a night beach), so the harness cannot tell "drawn" from "black". A player would see a dark scene and a walkable body. | The game's wake montage is the missing call; the rig's main-menu skip is the standing suspect. One solo control run decides mod vs. rig. |
| **11** | **Fog / ambient lighting state** (I4) — 73 `EnemyFogControlTrigger_C` and 243 `FogLightActor_C` are all local. | Visual only, and enemy-driven fog usually agrees. Lowest-value gap. | None planned. |
| **12** | **Breakable loot** — a barrel's or pot's drop is per-player by design. | Deliberate: `QuestItemSync` narrows the loot lane to key items, so a normal drop one player takes stays on the floor for the other. | none — by design |
| **13** | **The `DAMAGE` arrow points one way** — host to peer. A peer-run enemy that hits the *host's* body applies the hit to the peer's own dummy and nothing is forwarded. | Kept rare by ownership (an enemy with the host nearest is run by the host), so it is not a regression — but the host can be silently hurt-free against an enemy the partner runs. | The mirror of the existing lane: the peer sends `0x24` for a hit the game resolved on the host's body. The body already exists on both machines (`PlayerBody=1`). |
| **14** | **Enemy attack animation mirroring is unproven** — `enemy anim: Montage_Play verified by read-back` has never been seen; an AnimBP that plays *sequences* has nothing to mirror (`SYNC-GAPS` §3). | The enemy's swing may not be drawn on the follower; HP and death still agree, so it is visual — but a boss's telegraph is exactly what a co-op partner needs to see. | One live run looking for the read-back line; if the AnimBP uses sequences, mirror the sequence asset the same reflective way `EFFECT` mirrors projectiles. |
| **15** | **The dialogue *chosen option* is never carried** — `chosen` reads 65535 for ever and the reachable hub NPC offers `options=1` (`dialogue.md` Round 2, "NOT proven"). | The spectator sees A's line but not A's choice; on a real branching quest NPC the partner cannot tell which branch was taken. | The manager exposes no chosen-index property on this build, so it needs a different source (the `DialogueOptionChosen` delegate, the same event `DialogueScreen_C` fires). |

Ranks 11-15 are close enough that the order among them is a judgement call; 1-10 are not.

**Verified non-gaps**, recorded so they are not re-opened: the tutorial barricade (B10), the
shop door (A2), key-to-door gating (B8), `SecretWall_C` (B6), the pot break with no drop on
the receiver (B5), the dialogue lock *and* the spectator (G1-G3), a rider on a moving platform
(H9), and the `ENVControl` hazard (I5 — not placed in any map).

---

## 4. Not mapped, and what would map it

These are honest holes in the *research*, not known bugs.

1. **The rest to enemy-respawn question.** No document measures what a `Checkpoint_C` rest does
   to `EnemySaveStates` or to live enemies. It matters because the death lane made a rest a
   per-player act. *Probe:* rest on one machine, dump `qd.enemies` on both, compare the kill
   sets (`session30-death.ps1` already rests).
2. **The Queen's phase-2 state machine** (D10). *Probe:* the rig's `Phase` list already reads
   `CurrentStateIndex`, `DoingPhaseAnim`, `DeathIsSuspended`, `DoinAOrb`,
   `SpecialSwordTriggerHP`; nobody has driven the transition.
3. **Door to key mapping.** The key *families* are enumerated (26, 503 assets) and the door
   families are enumerated (52), but **which key opens which door is not decoded** — no
   document joins `CastleDoorKeys/*` to a specific `Castle_Door_Int_BP_C`.
   `Door_Base_C.CheckRequiredItems` reads the inventory at runtime, so the mod does not need
   it; a route does.
4. **Music/ambience trigger state** (I3). Nothing reads `MusicController`'s current cue.
5. **`LevelSequenceActor` placement** — a `scan: loose sequence none in this world` line was
   the only attempt (R91 era); no census of placed sequencer actors exists.
6. **`SaveHelper_C` (4,245 placed)** and **`FS_Light_C` (6,578 placed)** are the two largest
   placed classes and neither has a documented role. `SaveHelper` is a plausible per-actor
   save/restore helper; if it holds per-actor state, that is unexamined.
7. **What `730_HOD` / `760_Dekanima` / `775_JigekiTemple` / `990_B33sment` / `800_FinalZone`
   are for.** The classes are known (`SkullKeyRelic_BP_C` in HOD and Dekanima,
   `JigekiDoor_BP_C` + `FishWifeEvil_Spawner_C` in Jigeki, the Skull Door manager set in
   `800_FinalZone`, `Lib_LibrarianChair_BP_C` in `990_B33sment`), but no document says what
   the player is meant to do there. `CAMPAIGN-ROUTE.md` marks them optional/uncertain.
8. **The Shrine Boss's completion.** The content writes no flag at all (measured), so nothing
   can prove it beaten except its absence. There is no `cha_Bosses_*` for it either; it is
   presumably folded into `cha_Area_Aqr`.

---

## 5. Where each claim comes from

| claim class | source |
|---|---|
| what the game contains, by class and placement | `analysis/pak/answers.md`, `analysis/pak/answers-2.md` (§C7 inventory, C10 streaming, D2 dialogue, D9 bosses), `analysis/pak/blueprint-surface.md`, `analysis/pak/code/probes/interact_classes.txt` |
| the save schema and every store | `analysis/04-sync-surface.md`, `analysis/05-primary-evidence.md`, `analysis/gvas.py` |
| quests, flags, quest-state managers, endings | `analysis/23-quest-progression-sync-map.md`, `analysis/24-quest-closeout.md` |
| the challenge/achievement set and its order | `analysis/re/_challenge_full.txt`, `analysis/data/extracted/QueensDomain__Config__DefaultEngine.ini` (`Achievement_N_Id`) |
| what the mod does | `mod/native/src/*.h` (38 headers beside `proxy.cpp` and `fwd_symbols.c`), `mod/doc/WIRE.md`, `mod/qdcoop.ini` |
| what is proven | `mod/doc/SYNC-GAPS.md` §1-§5, `mod/doc/LIVE-TEST-LOG.md` Sessions 16-29, `mod/doc/features/*.md` |
| the placed-actor census in this file | a read-only pass over all 51 `.umap` export tables (`pakread.py` + `blueprints.py`), excluding `BirdsTestZone`, `VideoConfigMap`, `VideoConfig_2`, `SublevelRefTest`, `Ruins_Level_00_ShaderCompile` — 46 gameplay maps |
