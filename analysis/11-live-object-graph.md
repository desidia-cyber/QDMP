# 11 — The live object graph (gameplay)

Measured with Frida against the shipping exe in gameplay, using the substrate in
`09-live-substrate.md`. This is the evidence that closes the player/puppet chain.

State when captured: `Ruins_Level_00`, at the Knight's Encampment save point,
**485,823 live UObjects**.

---

## 1. The player character — class chain

| Object | Class | Note |
|---|---|---|
| live pawn | **`FS_Full_PlayerChar_C`** | the actual possessed pawn |
| — | `FS_PlayerChar_C` | a sibling Blueprint (CDO present) |
| — | `FS_PlayerCharacter` | the C++ base class |
| live controller | **`FS_PlayerController_BP_C`** | |
| — | `FS_PlayerController` | C++ base |

Full class list recovered from the object array (all game classes, live):

```
FS_3DWidget_C  FS_AIController  FS_AIController_C  FS_Actor  FS_AnimInstance
FS_AnimNotifyState_TimedNiagaraEffect(Advanced)  FS_AnimNotify_PlayMeleeSound
FS_AnimNotify_PlayNiagaraEffect  FS_AssetToolsSubsystem  FS_Branch_00_C  FS_Bush_00_C
FS_Bush_01_C  FS_Button_C  FS_ChallengeData  FS_ChallengeSaveGame
FS_ChallengeSystemComponent  FS_ChallengeUtils  FS_CheatManagerBase
FS_CheatManagerExtension  FS_Cheats_C  FS_CoinsSave_C  FS_CommonGM_C
FS_ConfigurableStaticMeshComponent  FS_CrowBase  FS_DaggerStats  FS_DescriptionText_C
FS_Destination  FS_DialogueAction  FS_DialogueData  FS_EnemyAction
FS_EnemyAnimInstanceBase  FS_EnemyCharacterBase  FS_EnemyUtils  FS_Fern_00_C
FS_FogData_Base  FS_FogFunctions  FS_FogLightActorBase  FS_FogLightComponent
FS_FogProcessor  FS_Full_PlayerChar_C  FS_GameInstanceBase  FS_GameInstance_C
FS_GameModeCore  FS_GameScriptingUtils  FS_GameViewport  FS_Grass_00_C  FS_Grass_00B_C
FS_HUD_Base  FS_HUD_C  FS_InAir_CameraFX_C  FS_InAirMove_Effects_C  FS_InputSubsystem
FS_InspectItem_C  FS_Item  FS_ItemData_C  FS_ItemDropSystem  FS_ItemStats
FS_JiggleComponent  FS_LevelList  FS_LevelManager  FS_LevelManagerEntry
FS_LevelManagerVolume  FS_Light_Activatable_C  FS_Light_C  FS_LocationData  FS_Modal_C
FS_MusicAreaData  FS_MusicAreaInstance  FS_MusicHoldVolume  FS_MusicSystemBase
FS_MusicTrackAssetData  FS_MusicVolume  FS_NavLinkProxy  FS_NiagaraEnvironmentFX
FS_NoEdTickSkeletalMeshComponent  FS_PathFollowingComponent  FS_PlayerCameraManager
FS_PlayerChar_C  FS_PlayerCharacter  FS_PlayerController  FS_PlayerController_BP_C
FS_PlayerHUD_C  FS_PropertyHelpers  FS_RichTextImagesDecorator_C  FS_SaveGame
FS_SaveGameUtils  FS_SaveSlot_C  FS_ScrollStyle_C  FS_ScrollStyle_ClipFix_C
FS_ScrollStyle_Fast_C  FS_SpawnerInterface  FS_StaticMeshActor_C
FS_StationarySkeletalMeshActor  FS_StatsContainer  FS_SystemSave_C  FS_Textbox_C
FS_WeaponPoseUtil  FS_WeaponStats  QD_EnemyData_base  QD_Journal_Data
```

## 2. ★ The pawn carries BOTH meshes — this is the puppet answer

The live `FS_Full_PlayerChar_C` owns these `SkeletalMeshComponent`s:

| Component | Owner | Meaning |
|---|---|---|
| **`Arms`** | `FS_Full_PlayerChar_C` | **the first-person rig** (what you see in-game) |
| **`CharacterMesh0`** | `FS_Full_PlayerChar_C` | **the full body** — present on the live pawn, not drawn in FP |

(The CDOs `Default__FS_PlayerCharacter`, `Default__FS_PlayerChar_C` and
`Default__FS_Full_PlayerChar_C` each also carry a `CharacterMesh0`, i.e. the body mesh is
inherited from the C++ base `AFS_PlayerCharacter`.)

**Consequence for co-op:** the remote player can be the *same* class the game already
uses. There is no need to invent a puppet class or attach meshes by hand — spawn
`FS_Full_PlayerChar_C` and the body is already on it. The only question is which flag
suppresses `CharacterMesh0` for the owning player, and that flag must be left at its
default on the puppet (or inverted, since a puppet has no owning player).

This is the single most valuable finding in the whole analysis: DollsMP's unfinished
milestone ("in-game puppet rendering") is a *configuration* problem here, not an art or
attachment problem.

## 3. Full component inventory of the live pawn

Recovered by walking every object whose `Outer` is the pawn (57 components):

**Movement / collision / input**
`CharMoveComp` (CharacterMovementComponent) · `CollisionCylinder` (CapsuleComponent) ·
`PawnInputComponent0` (InputComponent) · `TickGroup` (TickGroupComponent)

**Hitboxes** (the game's own damage volumes — useful for hit replication)
`DefaultHitbox`, `NormalHitbox`, `ParryHitbox`, `WindHitbox` (capsule),
`WingDashHitbox`, `UltraHitBoxCollision`

**Weapons and interaction**
`WeaponSystem_C` ("WeaponSelection") · `WeaponAttachPoint` · `WeaponAttachPoint_L` ·
`PlayerInteractionComponent_C` · `DaggerDummy_C` · `NullDaggerChargeLogic_A/B/C_C` ·
`Null_WeaponScript_C` · `FlyingSword_WeaponScript_C` ("Offhand_FlyingSword_WeaponScript")

**Status effects (one component per effect — a ready-made replicated state set)**
`MagicRotResistStatusEffect`, `b33smentstatus`, `CaputRingStatus`, `B33PunchStatusEffect`,
`BindingStatusEffect`, `DiseaseStatusEffect`, `WeakenStatusEffect`, `HeavyStatusEffect`,
`PoisonStatusEffect`, `RadiantFruitStatusEffect`, `IronskinFruitStatusEffect`,
`IronskinPotionStatusEffect`, `BerserkFruitStatusEffect`, `BerserkPotionStatusEffect`

**Camera / presentation**
`CameraAttachedComp`, `CameraHelper`, `CameraOffset`, `CameraManagerAttachedComp_C`,
`FaceFollowComp_C`, `PhotoModePostProcess`, `LowHealthPostProcess`, `TPSkySphere`

**Timelines** (13 of them): `HitFlash`, `LowHealthBlend`, `TPSecretFadeOut/In`,
`QuickFadeTL`, `introcrosshairfade`, `QuickTPFakeLightFade`, …

**Everything else**
`Grimoire_Logic_C`, `WoundLogic_C` (holds anim refs incl. `Player_FPArms_*`), `ItemMergeAnim_Comp_C`,
`ItemMergeAnim_Comp_RV_C`, `MementoMirror_EffectScript_C`, `StrangeMushroomLogic_C`,
`Player_HolyAmuletLight_C`, `Player_EquipmentLight_C` ×2, `MagicPoisonDebuffLogic_C`,
`MagicPoisonStatusEffectVis_C`

**Why this matters:** `PlayerInteractionComponent` is the seam for interactions; the
status-effect components are the seam for combat state; the weapon scripts are the seam
for what the other player should see in the puppet's hands.

## 4. Class census (top of the array)

```
MaterialInstanceDynamic 20523 · Function 14180 · Package 6109 · Class 2600
StaticMeshComponent 1870 · ScriptStruct 1829 · NiagaraScript 1545
SoundNodeWavePlayer 1479 · SCS_Node 1187 · NiagaraDataInterfaceCurve 1157
Texture2D 1153 · Enum 1108 · CanvasPanelSlot 1105 · CommonTextBlock 1011
SkeletalBodySetup 844 · HorizontalBoxSlot 810 · SceneComponent 788 · SoundWave 736
BlueprintGeneratedClass 713 · VerticalBoxSlot 658 · MaterialInstanceConstant 634
PhysicsConstraintTemplate 616 · NiagaraEmitter 576 · Image 519 · SimpleConstructionScript 506
```

Sanity note: these are exactly what a loaded UE4 level should look like, and the
`CanvasPanelSlot` / `CommonTextBlock` / `HorizontalBoxSlot` / `VerticalBoxSlot` entries
confirm **UMG/CommonUI is live** — relevant for adding a partner HUD.

## 5. What this closes

| Gap | Now |
|---|---|
| A5 — FName blocks | **CLOSED.** The decoder is `Blocks[idx>>16] + 2*(idx & 0xFFFF)`, header `>> 6`, `& 1` = wide. **100 % resolve rate (46,819/46,819)** on a 60k-object sample |
| B3 — pawn class | **CLOSED.** `FS_Full_PlayerChar_C`, controller `FS_PlayerController_BP_C` |
| B5 — components | **CLOSED.** 57 components enumerated above |
| B4 — which mesh is the body / which is FP | **CLOSED.** `Arms` = FP rig, `CharacterMesh0` = body. The suppression flag is **`bOwnerNoSee` = bit 5 (0x20) of `+0x212`**; `bOnlyOwnerSee` = bit 6 (`12-b4-owner-nosee.md`) |
| B6 — does `AFS_PlayerCharacter` replicate | **CLOSED.** `bReplicates` = bit 1 of `AActor+0x5B` and `bReplicateMovement` = bit 4 of `AActor+0x58`, both **1**, set by `APawn::APawn` `0x02AD9010` (`12-puppet-answers.md`) |
| B8 — is the body drawn anywhere | **CLOSED.** Cutscenes only (`FS_SequenceCharacter_C` in `Intro_Level_00`, `CutsceneSkeletalMeshActor_C` in the ending); the gameplay `CharacterMesh0` is **unassigned**, not hidden (`12-puppet-answers.md`) |

## 6. The decoder, for the mod

```
GuObjectArray      = base + 0x4A64250
  Objects          = *(base + 0x4A64250 + 0x10)        // FUObjectItem**
  NumElements      =  (base + 0x4A64250 + 0x24)        // i32
  item(i)          = *(Objects + (i / 65536) * 8) + (i % 65536) * 0x18

FNamePool          = base + 0x4A27F00
  Blocks           = base + 0x4A27F00 + 0x10           // u8*[8192]
  entry(idx)       = Blocks[idx >> 16] + 2 * (idx & 0xFFFF)
  header           = u16 at entry
  len              = header >> 6
  wide             = header & 1
  chars            = entry + 2, len bytes (ANSI) or len*2 (UTF-16)

UObject
  +0x00 vtable   +0x10 Class   +0x18 Name(FName)   +0x20 Outer
```

Both the 2× scaling and the `>> 6` length were derived from `FName::ToString`'s
disassembly and then confirmed empirically at 100 % coverage — not guessed.
