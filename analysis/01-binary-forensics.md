# 01 — Binary forensics and modding feasibility

*Written by the coordinator. The delegated binary-forensics agent stalled after
confirming the `.pdata` function count; its working artefacts (`exe_names.txt`,
`exe_paths.txt`, `surface_groups.txt`, `exe_surface_grouped.json`) are in this folder and
were used here. Claims below are measured unless marked `[I]`.*

---

## 1. Image identity

| Field | Value |
|---|---|
| File | `QueensDomain\QueensDomain\Binaries\Win64\QueensDomain-Win64-Shipping.exe` |
| Size | 81,374,720 B (77.6 MB) |
| Machine | x86-64 (`0x8664`) |
| ImageBase | `0x140000000` (ASLR-capable, relocations present) |
| Sections | 8 |
| LINK timestamp | `1793847905` |
| Engine string | `++UE4+Release-4.27-CL-0`, `++UE4+Release-4.27` |
| Engine build confirmed elsewhere | **4.27.2** (GVAS header of every save) |
| Debug directory | present (84 B) — **RSDS record exists**, PDB is **not** on disk |
| Delay-load | present (576 B) |
| TLS | present |
| Load config | present |
| Reliability | **242,000+ functions in `.pdata`** → the function-boundary table is intact, so RVA→function enumeration and pattern scanning both work |

The `QueensDomain.exe` at the install root (265 KB) is only the stock UE4 launcher stub:
strings are the generic `"This program is used for packaged games…"`, `CreateProcess()`
and the VC++ redist check. It spawns `Binaries\Win64\QueensDomain-Win64-Shipping.exe`.
**Target the Shipping exe.**

## 2. Exports — there are none that matter

The image has an export directory (RVA `0x46E8BB0`, 8,600 B, **269 names**), and every one
is a statically linked third-party symbol:

```
ags* (AMD AGS), png_* (libpng), NvOptimusEnablement,
AmdPowerXpressRequestHighPerformance, ...
```

**No `GObjects`, no `ProcessEvent`, no `GNatives`, no `GMalloc`, no `FName::` exports.**
(An ASCII scan returns 0 for each; so does the export table.) This is normal for a UE
shipping build and it means there is **no shortcut**: UE4SS-style signature scanning is
required, exactly as DollsMP concluded for Dolls Nest.

## 3. Imports

```
dxgi.dll            d3d11.dll        d3d12.dll        d3d9.dll
d3dcompiler_43.dll  steam_api64.dll  winmm.dll        dbghelp.dll
xinput1_3.dll       vcruntime140_1.dll  kernel32/user32/advapi32 ...
```

Notes:

- `dxgi.dll` is a static import → a `dxgi.dll` proxy *would* load, but see §5.
- `steam_api64.dll` is imported, **yet the DLL is not next to the exe** — it exists only
  under `Engine\Binaries\ThirdParty\Steamworks\Steamv163\Win64\`. Whether it resolves at
  runtime is unverified. Do not build a transport that depends on Steam.
- `d3d12` + `vkd3d-proton.cache` in the same folder ⇒ this copy has been run through a
  D3D translation layer at some point.

## 4. Anti-cheat, DRM, and networking

Measured string presence in the 81 MB image:

| Token | Hits |
|---|---|
| `EasyAntiCheat`, `BattlEye`, `Denuvo`, `EOSSDK`, `XIGNCODE` | **0** |
| `SteamAPI_Init` / `steam_api` / `Steamworks` | 1 / 2 / 1 |
| `bReplicates`, `bReplicateMovement`, `RemoteRole` | 1 / 1 / 2 (engine boilerplate) |
| `GetLifetimeReplicatedProps` | 1 (inside an engine console-help sentence) |
| `ReplicatedUsing`, `DOREPLIFETIME`, `Server RPC` | **0** |
| `CreateSession` / `FindSessions` / `JoinSession` | 1 / 2 / 1 (engine OnlineSubsystem) |
| `NetMulticast` | 10 (engine) |

**Verdict: no anti-cheat, no DRM wrapper.** And while UE4's networking machinery exists in
the build, **no game class opts into it** — there is no session UI, no RPCs on game
classes, and no replication hookup. The game is single-player and the mod must implement
replication itself.

> ⚠️ But the build **does** ship `OnlineSubsystemSteam`, `OnlineSubsystemNull`,
> `OnlineSubsystemUtils`, `SteamBridge` and `SteamShared` as plugins (see `02`). That is an
> *enabler* — a LAN/Null session layer is compiled in — not evidence that the game can be
> played co-op through it. Treat it as a possible later experiment, not a plan.

## 5. Proxy DLL: use `version.dll`

DollsMP's shipped proxy is `version.dll` + `version_orig.dll`, and its launcher
deliberately **deletes** `dxgi.dll` because a dxgi proxy breaks the Steam overlay. The
same reasoning applies here and is stronger, because this title has a compatibility-layer
cache in the binaries folder.

## 6. The game's class surface

Recovered from the exe's name pool (69,091 identifiers in `exe_names.txt`). Two game
prefixes exist: `FS_*` (the bulk) and a small `QD_*` family.

### Actors

```
AFS_PlayerCharacter     AFS_PlayerController     AFS_PlayerCameraManager
AFS_EnemyCharacterBase  AFS_AIController         AFS_GameModeCore
AFS_LevelManager        AFS_LevelManagerVolume   AFS_HUD_Base
AFS_MusicSystemBase     AFS_CrowBase             AFS_Actor
AFS_FogLightActorBase   AFS_FogProcessor         AFS_NavLinkProxy
AFS_NiagaraEnvironmentFX  AFS_StationarySkeletalMeshActor  AFS_Music*Volume
```

### Objects / components / subsystems

```
UFS_SaveGame  UFS_SaveGameUtils  UFS_ChallengeSaveGame
UFS_ChallengeSystem  UFS_ChallengeSystemComponent  UFS_ChallengeUtils  UFS_ChallengeData
UFS_DialogueData  UFS_DialogueAction
UFS_Item  UFS_ItemStats  UFS_ItemDropSystem  UFS_StatsContainer
UFS_WeaponStats  UFS_DaggerStats  UFS_AccessoryStats
UFS_LevelList  UFS_LevelManagerEntry  UFS_SpawnerInterface  UFS_Destination
UFS_PathFollowingComponent  UFS_GameInstanceBase  UFS_InputSubsystem
UFS_ConfigurableStaticMeshComponent  UFS_NoEdTickSkeletalMeshComponent
UFS_JiggleComponent  UFS_PropertyHelpers  UFS_GameScriptingUtils  UFS_WeaponPoseUtil
UFS_AnimInstance  UFS_EnemyAnimInstanceBase  UFS_EnemyUtils  UFS_EnemyAction
UFS_AssetToolsSubsystem  UFS_CheatManagerBase  UFS_CheatManagerExtension
UQD_EnemyData_base  UQD_Journal_Data
```

### Structs

```
FS_Enemy  FS_EnemySaveState  FS_EnemyAttackConfig  FS_AwarenessCone
FS_DialogueSequence  FS_DropCacheEntry  FS_DropCacheKey
FS_Item  FS_Stats  FS_AccessoryStats  FS_ThrowingWeaponStats  FS_WeaponStats
FS_ChallengeData  FS_ChallengeDefinition  FS_ChallengeSystem
FS_LevelManagerAction  FS_LevelLoadSuspendState  FS_LevelListStackEntry
FS_LocationData  FS_MusicArea  FS_MusicAreaInstance_SuspendData  FS_NamedSaveDataChange
FS_ShopConfigData  FS_ShopStockUnlock  FS_WeaponEvolutionConfig  FS_WeaponViewConfig
FS_WeaponChargeVisuals  FS_FogData  FS_MeleeFXOverride  FS_UpdateBudgetPool
FS_LineTrace*/FS_SphereTrace*  FS_WaitForLevelManagement  FS_GameSaveEvent
FS_InventoryChangeEvent  FS_AreaChangeEvent
```

### Enums

```
EFS_DialogueConditionType  EFS_DialogueConditionBehaviour  EFS_NamedSaveDataChangeMode
EFS_PickupDifficulty  EFS_PickupLocation  EFS_SpawnerUpdateMode
EFS_WeaponDamageTypeDisplay  EFS_WeaponRange  EFS_WeaponWidth
EFS_EnemyActionResponse  EFS_EnemyActionCancelReason  EFS_AttackAggroNoise
EFS_ChallengeMode  EFS_LocationType  EFS_Comparator  EFS_TraceType
EFS_WaitForLevelManagementMode
```

### The delegates that matter

```
FS_NamedSaveDataChange          progression flag changed
FS_InventoryChangeEvent / OnInventoryChanged   inventory changed
FS_GameSaveEvent                a save happened
FS_AreaChangeEvent / OnAreaChanged             area changed
EnemyDiedEvent / OnEnemyDied                   enemy died
EnemyStaggeredEvent                            enemy staggered
DialogueActionEvent / DialogueJumpEvent        dialogue advanced
LevelManagerUpdate / LevelManagerDoneLoading   streaming state
```

**This is the single most important difference from Dolls Nest.** That game had to be
*polled and diffed*. This one **tells you** when progression, inventory, death and
streaming change. Most of the discrete sync work becomes event-driven instead of
inference.

## 7. UE4SS verdict

Do **not** start with UE4SS. DollsMP's own post-mortem is the argument:

- UE4SS Lua has no socket library at all.
- `ForEachUObject` with a Lua callback over ~260k objects took **525 s of CPU**.
- `FindAllOf("StaticMeshActor")` returned 60,046 objects and crashed the game.
- `StaticFindObject` by path **crashes** rather than returning nil on this engine line.
- A missing property read returns a **non-nil ghost with a NULL class pointer**; touching
  it is an access violation `pcall` cannot catch (`push_nameproperty+0x2c1`, `mov rcx,[rax+70h]`).
- At least four distinct failure shapes (deadlocks, crashes, hangs, null derefs) were
  traced to the runtime itself.

The project ripped it out and shipped a native proxy. **Start native on this game too.**

What the native proxy needs on this image (the DollsMP "substrate" set):

```
UObject::ProcessEvent      GUObjectArray      GMalloc
FName::FName / FName::ToString      StaticConstructObject_Internal
UGameplayStatics::GetPlayerPawn / GetWorldDeltaSeconds / GetPlayerController
ApplyDamage                K2_SetActorLocation / SetActorRotation
Possess                    BeginDeferredActorSpawnFromClass  FinishSpawningActor
OpenLevel
```

Each must be located by pattern, then **byte-verified**: DollsMP's `targets.h` pins an
image SHA-256 plus the first 16 bytes of every target, and refuses to run on a mismatch.

## 8. Multi-instance (test rig) facts

Verified present in this binary:

| Lever | Status |
|---|---|
| `-UserDir=` | present (string `UserDir=` found) |
| `saveddirsuffix` | present |
| `SaveToUserDir` | present |
| `-nullrhi` | present |
| `-log`, `-fullscreen`, `-windowed`, `-ResX` | present |
| Single-instance mutex | `CreateMutexW` imported — **test whether it actually blocks a second instance** |

DollsMP's measured finding still applies: `-UserDir` is the real lever, and junctioning
`Saved` does nothing because `FPaths::ShouldSaveToUserDir()` is true.

## 9. Risks specific to the binary

1. **A game update invalidates everything.** Every hook is an RVA into one image. Ship the
   hash check and refuse rather than half-work.
2. **No PDB.** The RSDS record names a PDB that is not on disk. Reverse-engineering must be
   pattern-based. DollsMP's `tools/builddiff/` + `tools/re/` exist for exactly this.
3. **The Steam import may not resolve**, which means Steam P2P is not a dependable
   transport.
4. **No crash handler survives a MinHook trampoline fault** — the process just dies. Keep
   `%LOCALAPPDATA%\CrashDumps` analysis in the loop from day one.
