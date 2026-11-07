# 05 — Primary evidence gathered directly (coordinator)

This file records measurements I made myself, with the exact commands/tools used.
Everything here is `L1` (measured on the shipped game on this machine) unless marked.

Machine: `G:\Freespace\Queens_Domain` — game never modified by me; all reads only.

---

## 1. Engine identity — UE4.27, C++ project

| Fact | Value | How |
|---|---|---|
| Engine | **Unreal Engine 4.27** | `++UE4+Release-4.27-CL-0` and `++UE4+Release-4.27` in `QueensDomain-Win64-Shipping.exe` |
| Save `EngineVersion` | **4.27.2** | GVAS header of every `.sav` |
| PackageFileVersion | 522 (`0x20A`) | GVAS header |
| Project module | **`/Script/QueensDomain`** | `Challenges` save: class `/Script/QueensDomain.FS_ChallengeSaveGame` |
| Config flavour | `WindowsNoEditor` | path + config dirs |
| Pak | v11, **unencrypted index, no compression methods** | footer parse (below) |

**This is the same engine line as Dolls Nest (4.27.2).** That is the single biggest
schedule win in this whole analysis: the protocol, the smoothing, the pacing and the
UE4.27 structural knowledge all transfer.

### The project is C++, not Blueprint-only

Unlike Dolls Nest, this game has a real native gameplay layer. Names recovered from
the shipping exe's FName pool:

```
AFS_PlayerCharacter   AFS_PlayerController   AFS_PlayerCameraManager
AFS_EnemyCharacterBase   AFS_AIController   AFS_GameModeCore
AFS_LevelManager   AFS_LevelManagerVolume   AFS_HUD_Base
AFS_MusicSystemBase   AFS_CrowBase   AFS_Actor

UFS_SaveGame   UFS_SaveGameUtils   UFS_ChallengeSaveGame
UFS_ChallengeSystemComponent   UFS_ChallengeSystem   UFS_ChallengeUtils
UFS_DialogueData   UFS_DialogueAction
UFS_Item   UFS_ItemStats   UFS_ItemDropSystem
UFS_StatsContainer   UFS_WeaponStats   UFS_DaggerStats   UFS_AccessoryStats
UFS_LevelList   UFS_LevelManagerEntry   UFS_LevelListStackEntry
UFS_SpawnerInterface   UFS_Destination   UFS_PathFollowingComponent
UFS_JiggleComponent   UFS_GameInstanceBase   UFS_InputSubsystem

FS_Enemy   FS_EnemyAttackConfig   FS_EnemySaveState   FS_AwarenessCone
FS_DialogueSequence   FS_DropCacheEntry   FS_DropCacheKey
FS_GameSaveEvent          <- delegate
FS_NamedSaveDataChange    <- delegate
FS_InventoryChangeEvent   <- delegate
FS_AreaChangeEvent        <- delegate
FS_LevelManagerAction   FS_LevelLoadSuspendState   FS_WaitForLevelManagement

EFS_DialogueConditionType   EFS_DialogueConditionBehaviour
EFS_NamedSaveDataChangeMode   EFS_PickupDifficulty   EFS_PickupLocation
EFS_WeaponDamageTypeDisplay   EFS_WeaponRange   EFS_WeaponWidth
EFS_SpawnerUpdateMode   EFS_AttackAggroNoise
```

**Why this matters more than anything else:** the four `FS_*Event`/`FS_*Change`
delegates are *engine-level notifications for exactly the state a co-op mod must
replicate*. `FS_GameSaveEvent`, `FS_NamedSaveDataChange` and `FS_InventoryChangeEvent`
mean the game will tell us when progression, flags and inventory changed — we do not
have to diff the save file every frame (which is what Dolls Nest forced).

---

## 2. Player model — **YES, a full body exists**

This was the headline question and the answer is unambiguous.

`QueensDomain/Content/Player/Assets/FPModel/Player2026/`:

```
Player_FullChar_2026                    <- full-body skeletal mesh
Player_FullChar_2026_Skeleton           <- its skeleton
Player_FullChar_2026_PhysicsAsset       <- ragdoll/physics
Player_FullChar_2026_coolPose           <- authored pose
Player_FullChar_2026_coolPose_Animated  <- animated pose
Player_FullChar_2026_IntroAnimation     <- intro/full-body animation
Player_Armor_BloodMask / PlayerArm_BloodMask / PlayerFace_26_BloodMask
PlayerFace_26_tx   playerhair26_tx   Hair_26_MI   Head_26_MI
Prince_Eat_MA                           <- "eat" full-body animation
```

and a **separate first-person rig**:

```
Assets/FPModel/FPArm_00                <- first-person arm mesh
Assets/FPModel/Player_FPArms_Arms      <- FP arms mesh
Assets/Anims/Player_FPArms_Arms_Skeleton / _ABP / _PhysicsAsset
~200 x Player_FPArms_* animations (idle, walk, jump, death, knockdown, every attack)
```

Top-level player BPs: `FS_PlayerChar`, `FS_Full_PlayerChar`,
`FS_PlayerController_BP`, `CameraManagerAttachedComp`, `FaceFollowComp`.

The binary also contains `bForceFirstPerson`, `FirstPerson`, `ThirdPerson`,
`bOnlyOwnerSee`, `bOwnerNoSee`, `bHiddenInGame`, `SetOwnerNoSee`, `SetOnlyOwnerSee`.

**Conclusion.** The game is first-person with a *separate* FP arms rig, and it owns a
complete, rigged, physics-enabled full-body character that is already used somewhere
(intro animation, blood masks, "eat" animation, cool pose). A `bForceFirstPerson`
flag implies a third-person path already exists in code.

For co-op this is the best possible outcome:
- the remote player can be rendered as `Player_FullChar_2026` — **no new art needed**;
- it has a skeleton + physics asset, so animation retargeting and ragdoll at death work;
- blood masks exist, so damage state is already visually representable on the body;
- the "hidden in first person" problem is a per-component `bOwnerNoSee`/`bOnlyOwnerSee`
  decision, not an art problem.

Compare Dolls Nest, where the README's open item was literally "in-game puppet
rendering ... not finished". Here the asset we need already ships.

---

## 3. Pak — v11, **index unencrypted, no compression**

Footer at end of `QueensDomain-WindowsNoEditor.pak` (1,976,294,370 B):

```
magic             E1 12 6F 5A
version           11
index_offset      1,973,612,650
index_size        529,218
index_hash        d7e07fccb0a097aba197b98ec23cb277002ba1bb
bEncryptedIndex   0
EncryptionKeyGuid 00000000-0000-0000-0000-000000000000
CompressionMethods  (all zero - no zlib/oodle)
```

This is materially easier than Dolls Nest, whose index was AES-256-ECB and needed a
community key. **Here the index is plaintext**, so no key hunt is needed.

Layout discovered:

```
index_offset            primary index:
                          FString MountPoint "../../../"
                          int32  NumEntries   = 44156
                          u64    PathHashSeed
                          u8     bEncodedPakEntries
                          ... encoded entry blob ...
index_offset+index_size PathHashIndex: int32 count = 44092, then count x (u64 hash, i32 offset)
then                    File-table: int32 numDirs = 287,
                          repeat { FString dir; int32 n; repeat { FString name; i32 loc } }
```

The last structure is the one that carries the names. Parsing all of it produced
**47,716 file paths** (`analysis/pak-files.txt`) with 19,156 `.uasset`, 51 `.umap`,
1,730 `.ubulk`. The declared 44,156 entries and the 47,716 reclaimed names are
consistent once `.uexp`/`.ubulk` siblings are counted.

> Note: the naive "FString-validated scan" over the tail region finds 47,716 names;
> the strict TMap walk stops at 287 directories. The scan is the one to trust for the
> *name list*; a strict walk is still needed for *entry offsets* (see the open item).

### Content shape

| Folder | assets | what |
|---|---|---|
| `Enemies/` | 12,221 | ~40+ enemy families, all bosses |
| `Items/` | 8,624 | 2,506 Weapons, 503 KeyItems, 400 Treasure, 310 Consumables, 281 Accessories, 123 Upgrades |
| `RuinsLevel/` | 5,492 | all maps |
| `NPCs/` | 1,588 | Knight, Chef, Raider, Scientist, Crow, Librarian, FishWife, King, Merchant ... |
| `OtherWorld/` | 1,566 | secondary world |
| `UI/` | 1,457 | |
| `Audio/` | 1,366 | |
| `VFX/` | 1,239 | |
| `Player/` | 1,076 | |
| `Blueprints/` | 900 | gameplay logic |
| `Cutscenes/` | 223 | |
| `Tnf_Secrets/` | 241 | secrets |
| `Artifacts/` | 94 | ability-granting collectibles |

### Map / world structure

Numbered, gated, hub-and-spoke — 51 maps:

```
030?  Intro_Level_00 / Intro_Level_01
      Ruins_Level_00  (+ Ruins_Level_00_ShaderCompile)
100_GR, 110_GR_dungeon, 120_GR_goldenroom, 125_GR_CubeHall, 130_GR_outerisles, 140_GR_ShopIsleOnly
200_AQ, 210_AQ_underground
300_LIB, 310_LIB_exterior, 320_LIB_basement
400_MAUS_entrance, 420_MAUS_mid, 450_MAUS_darkmaze
500_AUT, 510_AUT_droptunnel
600_Castle, 610_Castle_Ext
700_SZ_GR, 710_SZ_AQ, 720_SZ_LIB, 730_HOD, 740_SZ_Prison, 750_SZ_MAUS2,
760_Dekanima, 760_SZ_AUT, 770_SZ_Swamp, 775_JigekiTemple, 780_SZ_Depths, 790_SG
800_FinalZone, 900_SipehaWorld, 990_B33sment
997_FinalZoneCutscene, 998_QueenCutscenes, 999_SipehaCutscenes
title_screen, BirdsTestZone
```

Districts: **GR** green ruins, **AQ** aqueduct, **LIB** library, **MAUS** mausoleum,
**AUT** autumn island, **Castle**, **SZ/HOD/SG** the late areas, **ROS**, **OtherWorld**.

This is a Metroidvania-with-keys, not a linear corridor: progression gates are key
items and granted abilities, which is exactly the state that must never diverge.

---

## 4. Save format — **fully solved**

`%LOCALAPPDATA%\QueensDomain\Saved\SaveGames\` (game has been played on this machine):

| File | Class | Size |
|---|---|---|
| `World0.sav` | `/Game/Blueprints/SaveSystem/FS_SaveSlot.FS_SaveSlot_C` | 79,823 B |
| `World0_backup.sav` | same | 79,823 B |
| `System0.sav` | `/Game/Blueprints/SaveSystem/FS_SystemSave.FS_SystemSave_C` | 1,197 B |
| `Challenges` | `/Script/QueensDomain.FS_ChallengeSaveGame` | 1,282 B |
| `Challenges_Backup` | same | |

**Standard UE4 `GVAS` container, plaintext, uncompressed.** Header:
`"GVAS"`, SaveGameFileVersion=2, PackageFileVersion=522, Engine 4.27.2, CL 0,
branch `++UE4+Release-4.27`, CustomVersionFormat=3, 51 custom versions.

Property stream (tagged properties: `FString name`, `FString type`, `i32 size`,
`i32 arrayIndex`, type-specific fields, `u8 hasGuid`, then value).

> **Layout trap found the hard way:** there is one extra byte (a `hasGuid` flag)
> between the tag fields and the value, and `Array`/`Map` values themselves begin
> with **two** int32s (a header int + the element count). Getting either wrong
> desynchronises the stream silently. Working parsers:
> `analysis/probe_save_tree.py`, `analysis/probe_save_maps.py`.

### `World0.sav` — complete schema (measured)

| Property | Type | Meaning for co-op |
|---|---|---|
| `SavableActorStates` | Map<Struct16, int32> | **generic per-actor persistent state** — the exactly-once seam |
| `EnemySaveStates` | Map<Struct, Struct> (65,006 B) | **enemy persistence** — dead/damaged enemies are saved |
| `NamedSaveData` | Map<Name, int32> | **the game's own named flag store** |
| `SaveLevel` | Name | current persistent level (`Ruins_Level_00`) |
| `SaveLocation` | Transform | player position at save |
| `SavePointID` / `LastTouchedCheckpoint` | Object | which save point |
| `SavePointsVisited` | Array<Object> (n=61) | fast-travel unlocks |
| `SavedArea` | Object | current area object |
| `PlayingMusicTrack` | Object | music state |
| `CollectLevel`/`CollectedNotes` | Array<Object> (n=46) | lore notes picked up |
| `NPCsInJournal` | Set<Object> | bestiary/NPC journal |
| `CurrentPlayerHealth` | Float | saved vitals |
| `InventoryContents` | Map<Object, int32> | **the inventory** |
| `Equipment` | Map<Object, int32> | **equipment** |
| `EquipmentSlots` | Map<int32, Object> | slot bindings |
| `CurrentlySelectedWeapon` | Object | |
| `WeaponCharges` | Map<Object, float> (106) | per-weapon charge |
| `DaggerCharges` | Map<Object, float> (26) | |
| `PlayTime` / `_Seconds` / `GamePlayTime` | Float/Int | |
| `SaveVersion` | Int (=6) | |
| `LastSaveTime` | DateTime | |
| `HasEverSaved` | Bool | |
| `ActiveSuspendSlot` | Int | suspend/quick-resume slot |
| `ItemChangedEvent` | Multicast delegate | transient, not persisted |

`NamedSaveData` keys actually present in this save:

```
TotalAchievementTreasures=0  CompletedChallenges=0  IntroMusicState=0
Arjaot_State=1  RustySwordKnightState=1  RustySwordKnightGaveSword=1
TotalAchievementWeapons=1  FullEnergyCellsCount=0  BrokeBarricade=1
CheckpointUnlockCount=1  TiniScytheHammerAP=0
```

`Challenges` save: `ChallengeStats` Map<Name,int32> — e.g. `cha_Item_GotAllWeps`,
`cha_Challenge_AllTeleporters`. 13 more challenge keys exist as assets
(`Challenges/quest_*`, `boss_*`, `area_*`, `item_*`, `ending1..4`).

**Progression is therefore NOT opaque.** It is a small, named, flat set of ints plus
a GUID-keyed actor-state map plus an enemy-state map. That is a far better starting
position than Dolls Nest, where progression had to be reverse-engineered out of a
save that was never designed to be read.

---

## 5. Quest system — this game HAS one

`QueensDomain/Content/Blueprints/`:

```
ActorQuestState                QuestStateInterface
ActorQuestStateManager         QuestForceUpdateVolume
Challenges/quest_book, quest_chef, quest_ducks, quest_egg, quest_grimoire,
           quest_lib, quest_mushroom, quest_necro, quest_raider, quest_sci,
           quest_skullls, quest_spend, quest_trident
```

Plus `FS_DialogueSequence`, `UFS_DialogueData`, `UFS_DialogueAction`,
`EFS_DialogueConditionType`, `EFS_DialogueConditionBehaviour`, and per-character
dialogue assets (`Queen_GreetDialogue`, `Sipeha_Dialogue`,
`WhaleKing_Dialogue`, `JosamiLord_DeathDialogue`, `ShrineBoss_Dialogue`, …).

So: **there is a queryable/observable quest state machine and a conditional dialogue
system**, unlike Dolls Nest which had none. The mod can hook the quest manager
directly instead of inferring quest progress from save deltas.

---

## 6. Interactables and one-shot world events (asset evidence)

From `Blueprints/`:

| Family | Assets |
|---|---|
| Save points | `Areas/<Area>/<X>_Save` + `_Icon_SP` (≈30 across all areas) |
| Teleporters | `Areas/<Area>/<X>_Teleporter`, `Teleporter/Teleporter`, `MiniTeleporter`, `SecretTeleporterDestination` |
| Doors | `Door_Base`, `QueenDoortempBlocker`, `SecretWall`, `ScienceSecretWall` |
| Elevators/movers | `Elevator/Elevator_Base`, `ElevatorLever`, `MovingActor`, `Library_RotateBridge`, `Library_LowerBridge` |
| Loot | `ItemPickup_Base`, `ItemSystem/ItemDrop`, `SpawnItemDrop`, `Shop/ShopTreasureActor`, `CoreItemPickupActor` |
| Breakables | `BreakableBarricade/*`, `Wellcover_Breakable` |
| Altars | `SecretSwordAltar/*`, `SpectralSwordAltar/*`, `SuperCubeAltar/*`, `MementoRecepticle` |
| Shops | `Shop/Shop`, `Shopkeeper`, `ShopMenu`, `ShopFunctions` |
| Upgrades | `UpgradeMachine/WeaponUpgradeMachine` |
| Gating | `ActivatableGroup`, `NotifyActivatables`, `NamedSaveDataSetterVolume`, `SuspendSaveBlockingVolume`, `QuestForceUpdateVolume`, `ActivationTrigger` |
| World events | `WorldEventReceiver`, `ActivatableNamedSaveDataSetter`, `LevelManagerTrigger` |

**`ActivatableGroup` + `NamedSaveDataSetterVolume` + `WorldEventReceiver` is a ready-made
generic one-shot-event bus.** A label/group is written into `NamedSaveData` when it
fires, and every client can read that back. We do not need to invent a flag store —
we need to keep the two copies of this one store equal.

---

## 7. PE facts (measured)

```
machine  x86-64      ImageBase 0x140000000      sections 8
export   rva 0x46E8BB0  size 8,600   -> 269 names, ALL third-party:
                                       AMD AGS (ags*), libpng (png_*), NvOptimusEnablement,
                                       AmdPowerXpressRequestHighPerformance
import   rva 0x46EAD48  size 880
delayimp rva 0x46E57A4  size 576
debug    rva 0x4431BA8  size 84      -> RSDS present (PDB path/GUID recorded, file absent)
```

**No Unreal exports.** So there is no shortcut to `GObjects`/`ProcessEvent` through the
export table — UE4SS-style pattern scanning is required, exactly as DollsMP did.
Imports include `dxgi.dll`, `d3d11/12/9`, `d3dcompiler_43`, `steam_api64.dll`, `winmm.dll`,
`dbghelp.dll`, `xinput1_3.dll`, `vcruntime140_1.dll`.

**Proxy DLL choice:** DollsMP ships `version.dll` (+ `version_orig.dll`) and its launcher
deliberately deletes `dxgi.dll` because a dxgi proxy breaks the Steam overlay. Use
`version.dll` here.

**Steam caveat:** `steam_api64.dll` exists only under
`Engine\Binaries\ThirdParty\Steamworks\Steamv163\Win64\` — **not next to the shipping
exe** — so whether Steamworks actually loads at runtime is unverified. Do not make Steam
P2P the only transport.

**No anti-cheat, no DRM wrapper** (no EasyAntiCheat, BattlEye, Denuvo, EOS strings).

---

## 8. Open items I could not close offline — all four since closed

1. Decode `EncodedPakEntries` → **CLOSED** (`pak/PAK-DECODE.md`; payload extraction works).
2. Decode the `EnemySaveStates` value struct → **CLOSED** (C4: `{bValid, RespawnTime,
   RemainingHP}`, 414 records, fixed 157-byte stride).
3. Does `AFS_PlayerCharacter` set replication? → **CLOSED** (B6: `bReplicates` and
   `bReplicateMovement` are both **1**, set by `APawn::APawn`; the game still has no
   networking layer).
4. Save class backing / GUID stability → **CLOSED** (C1: 30 properties of `FS_SaveSlot_C`
   parsed; C3: **421/421** persisted GUIDs are authored and cooked into the level packages).
