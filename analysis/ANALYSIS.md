# Queen's Domain Co-op — master analysis

**Goal.** A 2-player online co-op mod that lets two people play *Queen's Domain* from
start to finish together, built the same way as the existing `G:\Freespace\dollsMP`
co-op mod for *Dolls Nest*.

**What was analysed.** The shipped game
(`G:\Freespace\Queens_Domain\QueensDomain\`, UE 4.27, 1.88 GB pak, 77.6 MB shipping exe),
its save files on this machine, its complete asset listing, its binary name surface, and
the entire `dollsMP` repository (156 k lines: 24 k Rust, 74 k native C++, 18 k Lua,
39 k tooling Python/PowerShell).

**Supporting documents**

| File | Contents |
|---|---|
| `05-primary-evidence.md` | Raw measurements: engine identity, pak format, full save schema, PE facts |
| `03-dollsmp-reuse.md` | Component-by-component reuse matrix, lessons learned, porting plan |
| `06-sync-design.md` | The sync surface and per-system design |
| `01-binary-forensics.md` | PE structure, exports/imports, class surface, UE4SS verdict, hook targets |
| `02-pak-inventory.md` | Asset inventory, world structure, system evidence by asset name |
| `04-sync-surface.md` | Independent sync-surface pass and a self-testing save reader |
| `07-verification.md` | **The two pre-code checks, with method and results** (GUID stability, Steam) |
| `pak-files.txt` | All 47,716 pak paths |

---

## Executive summary

| Question | Answer |
|---|---|
| Engine? | **Unreal Engine 4.27** (4.27.2), packaged `WindowsNoEditor`. **Same engine line as Dolls Nest.** |
| Is it a Blueprint game or is there C++? | **Real C++ gameplay layer** (`/Script/QueensDomain`, `AFS_*`/`UFS_*` classes). Better hook surface than Dolls Nest. |
| Does the player have an actual model? | **Yes — a complete full-body character already ships** (`Player_FullChar_2026`, with skeleton, physics asset, face, hair, armour, blood masks), separate from the first-person arms rig. **No new art needed for the co-op puppet.** |
| Anti-cheat / DRM? | **None** — no EAC, BattlEye, Denuvo or EOS. Steamworks v1.63 is shipped but the DLL sits under `Engine\Binaries\ThirdParty\`, not next to the exe, so Steam may not load at runtime. |
| Is there already multiplayer? | **No.** Engine networking classes and `OnlineSubsystem*` plugins are compiled in, but no game class implements sessions or RPCs. Custom replication is required — exactly what DollsMP already does. |
| Can progression be synced? | **Yes, and cheaply.** It lives in three small containers + four lists in a plaintext, uncompressed `GVAS` save that is fully parsed today. |
| Is there a quest system? | **Yes, but as Blueprint actors** — `ActorQuestState`, `ActorQuestStateManager`, `QuestStateInterface`, `QuestForceUpdateVolume`, plus 13 `Challenges/quest_*` blueprints. There is **no C++ `UFS_Quest*` class**; persistence goes through `NamedSaveData`. Still far more structure than Dolls Nest, which had none at all. |
| Do enemies have stable identity? | **Yes — verified, not assumed.** All 421 persisted IDs from a live save (7 + 414) were found **cooked into the pak**, 421/421. They are authored `FGuid`s in level packages, so both machines resolve the same ID with no extra scheme. See `07-verification.md`. |
| How much of DollsMP is reusable? | **~25–30 % of the code** — but that is almost exactly the hard part (transport, reliability, session, smoothing, PE tooling, packaging). **~80 % of the knowledge.** |

**Verdict: this is a significantly better co-op target than Dolls Nest was.** The engine
matches, the player model exists, the save format is readable, entities have GUIDs, and
the game is unprotected. The realistic risk is *scope* (one persistent world with ~40
streamed areas, 400+ persisted enemies, 4 endings), not *feasibility*.

---

## 1. Engine and modding feasibility

### 1.1 Identity

```
++UE4+Release-4.27-CL-0            (shipping exe)
EngineVersion 4.27.2               (GVAS header of every save)
PackageFileVersion 522
Project module: /Script/QueensDomain
```

Dolls Nest is 4.27.2. **Everything structural transfers**: `FUObjectArray` layout,
`ProcessEvent` vtable slot, `FName` handling, actor spawn sequences, the `-UserDir=` /
`saveddirsuffix` multi-instance levers. The only thing that must be redone is *finding
this image's addresses* — and `tools/builddiff/` exists specifically to do that.

### 1.2 Injection

The proxy-DLL technique from DollsMP applies: a DLL placed next to the shipping exe is
loaded by the game itself — no injector, no remote thread. The real system DLL ships
beside the proxy as `<name>_orig.dll` and every forwarded export is verified to resolve
inside System32.

**Use `version.dll`, not `dxgi.dll`.** DollsMP's shipped proxy is `version.dll` +
`version_orig.dll`, and its launcher deliberately *deletes* `dxgi.dll` because a dxgi
proxy breaks the Steam overlay. That matters here because this game imports
`dxgi.dll` and ships Steamworks (`steam_api64.dll`, v1.63).

> ⚠️ The install also contains `vkd3d-proton.cache`, so this copy is or has been run
> through a D3D compatibility layer. Confirm which DXGI the game resolves before
> considering a dxgi proxy as a fallback at all.

### 1.3 Hook targets that must be re-found for this image

`UObject::ProcessEvent`, `GUObjectArray`, `GMalloc`, `FName::FName` / `FName::ToString`,
`StaticConstructObject_Internal`, `UGameplayStatics::GetPlayerPawn`,
`GetWorldDeltaSeconds`, `GetPlayerController`, `ApplyDamage`, `K2_SetActorLocation`,
`Possess`, `BeginDeferredActorSpawnFromClass`, `FinishSpawningActor`, `OpenLevel`.

Everything else is discovered incrementally. DollsMP's `targets.h` discipline is the rule:
**every target carries an image hash + 16 expected bytes, and a mismatch refuses to run**
rather than half-working.

### 1.4 What is genuinely new work versus Dolls Nest

| Area | Dolls Nest | Queen's Domain |
|---|---|---|
| Substrate (GObjects/ProcessEvent/object scan) | built from scratch | **port the arithmetic**, re-measure RVAs |
| Puppet art | did not exist — had to be improvised | **ships in the game** |
| Entity identity | inferred (spawn lineage, actor labels) | **authored GUIDs** |
| Progression model | no quest system; reverse-engineered from save | **named containers + a quest manager** |
| Change notification | none; polled and diffed | **delegates exist** (`FS_NamedSaveDataChange`, `FS_InventoryChangeEvent`) |
| Transport/session/smoothing | built and tested | **copy** |

---

## 2. The player character — the answer, with evidence

This was the gating question and the answer is unambiguous.

**There is a real, rigged, physics-enabled full-body player character in the game.**

```
QueensDomain/Content/Player/Assets/FPModel/Player2026/
    Player_FullChar_2026                     skeletal mesh
    Player_FullChar_2026_Skeleton            skeleton
    Player_FullChar_2026_PhysicsAsset        physics/ragdoll
    Player_FullChar_2026_IntroAnimation      full-body animation
    Player_FullChar_2026_coolPose(_Animated) authored pose
    Player_Armor_BloodMask / PlayerArm_BloodMask / PlayerFace_26_BloodMask
    PlayerFace_26_tx / playerhair26_tx / Hair_26_MI / Head_26_MI
    Prince_Eat_MA                            full-body animation
```

and a **separate first-person rig**:

```
Player/Assets/FPModel/FPArm_00, Player_FPArms_Arms
Player/Assets/Anims/Player_FPArms_Arms_Skeleton / _ABP / _PhysicsAsset
~200 Player_FPArms_* animations (idle, walk, jump, death, knockdown, every attack)
```

Top-level character blueprints: `FS_PlayerChar`, `FS_Full_PlayerChar`,
`FS_PlayerController_BP`, `CameraManagerAttachedComp`, `FaceFollowComp`.
The binary contains `bForceFirstPerson`, `FirstPerson`, `ThirdPerson`, `bOnlyOwnerSee`,
`bOwnerNoSee`, `SetOnlyOwnerSee`, `SetOwnerNoSee`.

**Implications for co-op**

1. The remote player is rendered as `Player_FullChar_2026`. **Zero new art.**
2. It has a skeleton and a physics asset → animation retargeting works, and ragdoll on
   death is available.
3. Blood masks exist → damage state is already visually expressible on the body.
4. `bForceFirstPerson` implies a third-person path is already implemented somewhere
   (intro/cutscenes), so the camera/body-hiding machinery is not something we invent.
5. The "hide my own body in first person" problem is a per-component
   `bOwnerNoSee`/`bOnlyOwnerSee` flag, not a rendering problem.

For contrast: DollsMP's README still lists *"in-game puppet rendering — not finished"* as
its outstanding milestone. Here the asset that milestone needed is in the box.

---

## 3. What the game contains that must be synced

### 3.1 Content scale

| Folder | Assets |
|---|---|
| `Enemies/` | 12,221 |
| `Items/` | 8,624 (2,506 Weapons, 503 KeyItems, 400 Treasure, 310 Consumables, 281 Accessories, 123 Upgrades) |
| `RuinsLevel/` | 5,492 (51 maps) |
| `NPCs/` | 1,588 |
| `OtherWorld/` | 1,566 |
| `UI/` | 1,457 |
| `Audio/` | 1,366 |
| `VFX/` | 1,239 |
| `Player/` | 1,076 |
| `Blueprints/` | 900 |
| `Cutscenes/` | 223 |
| `Artifacts/` | 94 |

### 3.2 World structure — one persistent world with streamed area sublevels

This is **not** 51 separate levels. `Ruins_Level_00` is the persistent world (and it is
literally what `SaveLevel` contains in the save), with ~40 streamed sublevels:

```
000_Shared_Base_Lighting, 050_Ocean, 060_Ocean_Shadow, 070_UpperShell   global layers
100_GR  110_GR_dungeon  120_GR_goldenroom  125_GR_CubeHall  130_GR_outerisles  140_GR_ShopIsleOnly
200_AQ  210_AQ_underground
300_LIB  310_LIB_exterior  320_LIB_basement
400_MAUS_entrance  420_MAUS_mid  450_MAUS_darkmaze
500_AUT  510_AUT_droptunnel
600_Castle  610_Castle_Ext
700_SZ_GR  710_SZ_AQ  720_SZ_LIB  730_HOD  740_SZ_Prison  750_SZ_MAUS2
760_Dekanima  760_SZ_AUT  770_SZ_Swamp  775_JigekiTemple  780_SZ_Depths  790_SG
800_FinalZone  900_SipehaWorld  990_B33sment
```

plus separate maps for `Intro_Level_00/01`, `998_QueenCutscenes`, `999_SipehaCutscenes`,
`997_FinalZoneCutscene`, `title_screen`, and a few UI preview maps.

**Streaming is trigger-driven.** The binary contains `AFS_LevelManager`,
`AFS_LevelManagerVolume`, `LevelListStack`, `AddLevelListToStack`,
`ClearTriggerLevelLists`, `bLevelListIsFromTriggers`, `FS_LevelLoadSuspendState`, and the
delegates `LevelManagerUpdate` / `LevelManagerDoneLoading`. So walking into a volume
pushes a level list onto a stack, and the manager loads it.

**This is a co-op hazard with a known fix.** With two players, "what is loaded" must be
the **union of what is around both players**, and a level must not unload while the other
player is standing in it. DollsMP solved exactly this in `dmp_world_stream.h`
("load what is around both players") — port the rule, not the Dolls Nest code.

Progression is Metroidvania-shaped: gates are key items (`Items/KeyItems/`, 503 assets)
and abilities come from artifacts (`Artifacts/StrangeMushroom` → double jump,
`Artifacts/Wind_Crystal`, fossils). That is exactly the state that must never diverge.

### 3.3 Completion criteria (the "start to finish" checklist)

From `Blueprints/Challenges/`, and confirmed live in the `Challenges` save
(`cha_Item_GotAllWeps`, `cha_Challenge_AllTeleporters`):

- **10 area** challenges: `area_AQ, area_AW, area_castle, area_forest, area_lib, area_maus, area_prison, area_ros, area_sg, area_swamp`
- **8 boss** challenges: `boss_bigfish, boss_dragon, boss_hellduo, boss_jl, boss_necro, boss_redlords, boss_ros, boss_whaleking`
- **13 quest** challenges: `book, chef, ducks, egg, grimoire, lib, mushroom, necro, raider, sci, skullls, spend, trident`
- **4 endings**: `ending1..4`
- item/collection: `item_acc, item_cells, item_endu, item_eweap, item_flygems, item_masklens, item_melee, item_secretsword, item_spectralsword, item_throw, item_treasure, item_ultra, item_upgrade`
- special: `challenge_chests, challenge_sdoor, challenge_tpsp, plat, quat_vault`

This is a useful acceptance-test matrix: **if both players' challenge sets match at the
end, the playthrough was co-op-correct.**

---

## 4. The sync surface, precisely

The complete list is in `06-sync-design.md`. The short version:

### 4.1 Persistent state — three containers plus four lists

From `World0.sav` (`FS_SaveSlot_C`), measured, plaintext:

| Container | Type | Sample size | Nature |
|---|---|---|---|
| `NamedSaveData` | `Map<Name,int32>` | 11 | **the flag bank** — story facts and counters |
| `SavableActorStates` | `Map<FGuid,int32>` | 7 | interactable / activatable state |
| `EnemySaveStates` | `Map<FGuid,{bValid,RespawnTime,RemainingHP}>` | **414** | enemy persistence |
| `SavePointsVisited` | `Array<Object>` | 61 | fast travel |
| `CollectedNotes` | `Array<Object>` | 46 | lore |
| `NPCsInJournal` | `Set<Object>` | — | journal / bestiary |
| `InventoryContents`, `Equipment`, `EquipmentSlots`, `CurrentlySelectedWeapon`, `WeaponCharges`, `DaggerCharges` | maps | 1–106 | items and loadout |

The operator's own 11-minute save holds exactly these progression facts:

```
Arjaot_State=1  RustySwordKnightState=1  RustySwordKnightGaveSword=1
BrokeBarricade=1  CheckpointUnlockCount=1  TotalAchievementWeapons=1
```

**That is the entire progression payload for that slice: eleven integers.** Syncing quest
progression is therefore a solved-shaped problem: keep one small integer map equal,
host-authoritative, monotonic.

### 4.2 Live state — what streams

Player transform/aim/action, player vitals, enemy transform/animation/HP/death,
interactable state, dialogue lock + line, level transitions. Budget from DollsMP's
measurements plus this game's enemy density: **≈10–20 KB/s**, comfortably LAN and Steam
P2P friendly.

### 4.3 The seams the game already gives us

| Seam | Why it matters |
|---|---|
| `FS_NamedSaveDataChange` | tells us a flag changed — no polling |
| `FS_InventoryChangeEvent`, `ItemChangedEvent` / `OnInventoryChanged` | inventory changed |
| `FS_GameSaveEvent` | a save happened |
| `FS_AreaChangeEvent` / `OnAreaChanged` | area/level changed |
| **`EnemyDiedEvent` / `OnEnemyDied`** | **the exact hook for host-authoritative death + per-player loot roll** |
| **`LevelManagerDoneLoading`** | **the "world is ready" event — replaces DollsMP's 5-second timer guess** |
| `ActivatableGroup` + `ActivatableNamedSaveDataSetter` + `NamedSaveDataSetterVolume` + `WorldEventReceiver` | a ready-made generic one-shot-event bus |
| `UFS_SpawnerInterface` + `Enemies/EnemySpawner` | enemies are spawned by identified spawners |
| `FS_EnemySaveState` GUID | stable per-enemy identity |
| `AFS_PlayerCharacter`, `AFS_EnemyCharacterBase`, `AFS_AIController` | the actors to drive |
| `UFS_SaveGame` / `UFS_SaveGameUtils` / `UQD_Journal_Data` | the game's own save path and journal data asset |

Two class prefixes exist in the game module — `FS_*` (the bulk) and a small `QD_*`
family (`UQD_EnemyData_base`, `UQD_Journal_Data`). Search both.

### 4.4 The five systems, and how each is answered

| System | Answer |
|---|---|
| **Quest progression** | Mirror `NamedSaveData` + `SavableActorStates`. Host-wins, monotonic, never lowered. Peer actions are *intents*. |
| **Quest / key items** | They are inventory entries → shared. Pickup idempotence via `SavableActorStates` GUID. |
| **Loot** | Placed pickups shared (GUID-keyed); enemy drops rolled per-player. |
| **Dialogue** | One speaker per NPC, published as state with a sequence number; first answer decides both; conditional dialogue (`EFS_DialogueConditionType`) must be evaluated from the *shared* flags, not local ones. |
| **Enemy state & movement** | Host simulates AI; peer copies driven with AI off; grouped by authored GUID; `RemainingHP`/`bValid`/`RespawnTime` keep both machines agreeing on persistence. |

### 4.5 A co-op hazard worth checking early

The content contains an environment-control subsystem: `Blueprints/ENVControl/` with
`ENVController`, `ASideTrigger` / `BSideTrigger`, `ENVControlData`,
`ScaryDarkness_ENVData`, plus the whole `FogSystem/` family (`FogProcessor`,
`FogLightActor`, `FogControlInterface`). Separately, `OtherWorld/` (1,566 assets:
`Castle_Ext`, `OW_Props`, `OW_MK`, `Lab`, `Lift_00`) is a distinct place, not a mirror
of the main world.

**Read this as `[I]`, not fact.** The most likely reading is *atmosphere/fog state with
two trigger sides*, not a light/dark world swap. But **if it is a per-player or
per-zone environment state, it must be shared**, or one player will be in darkness while
the other is lit, and any trigger volume that gates on it will disagree. Confirm with one
probe (walk two instances through an `ENVController` volume and compare) and, if it is
shared state, add it to the replicated set from day one — retrofitting it later means
revisiting every interactable.

---

## 5. Reuse from DollsMP

Full matrix in `03-dollsmp-reuse.md`. The headline:

> **The README is stale.** It advertises a UE4SS + Lua + file-bridge mod and says puppet
> rendering is unfinished. The shipped mod is a **native `dxgi.dll` proxy with no UE4SS
> and no Lua**, linked against Rust, and the puppet, world sync, quests, bosses, movers
> and dialogue were all built. Read `HANDOFF.md` and `mod/native/`, not `README.md`.

### 5.1 By the numbers

| Bucket | Lines | Reuse |
|---|---|---|
| Rust transport/protocol/session | ~17 k | **~85 %** (edits, not rewrites) |
| `dollsmp-game` (game domain) | ~1.7 k | ~15 % (smoothing only) |
| Native proxy C++ | 74.2 k | **~5 % of code, ~70 % of design** |
| Lua client | 18.2 k | 0 % code, 100 % lessons |
| Tooling | 39.5 k | **~60 %** |

**~25–30 % of code, ~80 % of knowledge.** The reusable code is precisely the part that is
miserable to get right twice.

### 5.2 Copy almost verbatim

`core::{bitstream, reliability, rng, error}` · `server::relay` (including `NetImpair`) ·
`server::Session` (host = first to join) · sidecar `BlobLane`/`Reassembler` ·
`game::smoothing` (retuned) · `tools/builddiff/*` (PE/RVA/FName tooling) ·
`tools/re/*` · `tools/typecheck/` · `tools/steam_mock/` (this game ships Steamworks too) ·
`backup.ps1` policy · `make-test-instance.ps1` technique.

### 5.3 Retarget

`quantize` defaults (this is a smaller, indoor, human-scale world — fewer bits needed) ·
protocol magic and packet kinds · `ffi::bridge` record body · `launcher` (exe names, save
path `%LOCALAPPDATA%\QueensDomain`) · `builddiff/targets_*` for the new image.

### 5.4 Rewrite

The entire native proxy (every RVA and offset is Dolls Nest), `dollsmp-game`, the save
editor vocabulary, every test scenario.

### 5.5 Do not port

`mod/lua`, `mod/src`, `mod/cpp` — superseded generations. Keeping the old Lua in an
instance actively causes *two writers on one file and two dolls for one peer*.

### 5.6 Eleven defects to fix before reusing

The audit found concrete bugs in the Rust that would otherwise be inherited. The five
most important:

1. `RpcPayload for bool` cannot be encoded through `RpcTable` (writes one bit, then
   `write_bytes` rejects a non-byte-aligned position).
2. `Connection::tick` evicts acked entries from the tracker but not from
   `Connection.retransmit` → a reliable packet is neither resent nor abandoned.
3. `make_header` conflates "peer acked us" with "which inbound sequence to ack".
4. `DesyncDetector::compare_tick` counts a strike per call, not per tick.
5. `Packet::encode` casts `payload.len() as u16` without re-validating `MAX_PAYLOAD`.

The full list of eleven is in `03-dollsmp-reuse.md` §B.5. Fix them **while the code is
still small**, and fix or delete the 8 known-failing `dollsmp-server` tests rather than
carrying them as "known failures".

### 5.7 The one file to read before designing the new sync

`crates/dollsmp-game/src/smoothing.rs` — the best-reasoned file in the repository:
**100 ms render delay, 250 ms extrapolation cap, 32-sample buffer, 2000 cm snap
threshold, 100 ppm clock drift correction.** It encodes the interpolation decisions
that took the previous project the longest to get right. Keep it whole and retune the
constants to this game's scale.

---

## 6. Lessons that will save the most time

These are documented DollsMP failures. Re-deriving them costs game runs.

**Architecture**
- Only UObject access goes on the game thread. File I/O, decoding and maths stay off it.
- A single `std::fs::rename` on the game thread froze the game. One writer thread, handed
  records by seqlock.
- `sleep(1 ms)` is ~15.6 ms unless `timeBeginPeriod(1)` is called *in that process*.
- Gate on game events, not timers. A 5-second timer after a level-name change lost a race.
- **Validate every handle.** Reflection can return a valid-looking object of the wrong
  type; driving it is an access violation that `pcall`/`__try` cannot catch.
- Never trust a cached handle across a map change.

**Reflection and hooking**
- A stride read wrong returns plausible garbage rather than faulting → **measure it, then
  pin it with a round-trip test** (0x14 gave 0/64, 0x18 gave 64/64).
- Byte-verify every RVA at load, with an image hash.
- Liveness is not "a plausible header" — it is *the object array slot at that index
  pointing back at the object*.
- `called=yes` is not evidence the call ran. **Read back the object that was supposed to
  change.**

**Movement**
- Interpolation needs a sample on either side of render time → draw the peer slightly in
  the past. Extrapolating from "now" turns every late packet into a visible correction.
- Adaptive jitter buffer (grow fast, shrink with a time constant), not a fixed one.
- Snap on teleport/level change; never interpolate across a discontinuity.
- 60 Hz costs <10 KB/s and halves sample age — latency, not bandwidth, was the constraint.

**Sync correctness**
- **State, not events.** Repeat absolute state; a lost or duplicated datagram changes
  nothing.
- **One-way rule.** The peer's world follows the host's; a peer action is an intent.
- **An event value must never be lowered** — refuse a write that would regress progression.
- Idempotent application beats delivery guarantees. A test that should pass: *delete the
  ledger, make every key miss, and every one-shot still takes effect exactly once.*
- Dialogue needs a **lock**, not just replication.

**Save safety**
- Give co-op its own save directory and a save guard that refuses to run if the save path
  is missing, relative, duplicated, or at/above the real profile. DollsMP exits before
  opening a save rather than risk the player's data.
- Two instances must never share one save file — copy per instance.
- A save editor once ignored its `--root` and edited the operator's real save. Make the
  root mandatory, echoed, and refuse to write outside it.

**Testing discipline**
- A check that cannot fail is not a check.
- One switch per subsystem, so one broken part does not block testing the rest.
- Take a minidump while hung, before stopping anything.
- Hangs look exactly like "the other player is invisible" — check process responsiveness
  before diagnosing sync.

---

## 7. Risks

| Risk | Severity | Mitigation |
|---|---|---|
| **Scope** — 51 maps, 400+ persisted enemies, 4 endings | **High** | Ship co-op for the *systems*, not per-quest content. The flag-bank design is content-agnostic, so new areas work without new code. Accept that a full 4-ending playthrough needs a long test campaign. |
| **Steam does not load in this install** — `steam_api64.dll` is a **delay-load** import and the DLL is unreachable (`Engine\Binaries\ThirdParty\Steamworks\Steamv163\Win64\` only) | **Verified** | Ship the UDP relay as the primary transport. `OnlineSubsystemNull` + `IpNetDriver` are configured as fallback and are a *later experiment*, not a plan. |
| The proxy collides with a D3D compatibility layer (`vkd3d-proton.cache` present) | Medium | Use `version.dll`, not `dxgi.dll`. |
| `SavableActorStates`/`EnemySaveStates` GUIDs are *runtime* rather than editor-authored | **High if true** | Probe first: log a known interactable's GUID across a reload. If unstable, fall back to spawner-path + index identity. |
| Dialogue pauses the game | Medium | Detect `SetPause`/time dilation; if it pauses, the host must not pause, or the peer must hold. |
| Conditional dialogue evaluates local flags | Medium | Seed the peer's dialogue conditions from the shared flag bank. |
| Both machines write the same save | High | One authority + per-instance save dirs + save guard. |
| **Two instances on one PC is not viable on this hardware** — measured **19.5 GB working set / 20 GB private** at 640×360 with every scalability group at 0, on a 22 GB machine | **Verified** | Build the test rig for **two machines** from the start. DollsMP's side-by-side single-PC rig does not carry over. |
| The game writes **suspend saves silently** (`World0_Suspend_N.sav`, ~2 min apart, observed) | Medium | Treat the suspend path as a save writer the mod must understand — the same "module must not save mid-play" hazard DollsMP documented. |
| Game updates break the mod | Medium | Image hash + byte-verified targets; refuse to run rather than half-work. Plan for a build-detection layer. |
| No fragmentation on DollsMP's live path | Low here | No bulk transfers are needed; state and flag banks are small. |

---

## 8. Plan

Phases only — size is relative, not a schedule.

### Phase 0 — Prove the substrate (nothing game-specific)
1. Copy `tools/builddiff/`, `tools/re/`, `tools/typecheck/`, `backup.ps1`; re-point at
   `QueensDomain-Win64-Shipping.exe` (77.6 MB).
2. Generate and **byte-verify** `targets.h` for the 15 core functions/globals.
3. Measure the object-array stride; pin it with a round-trip test.
4. Build a hello-world `dxgi.dll` that loads, forwards its exports and logs `GObjects`.
   **Do not write game logic before injection is proven.**
5. Stand up two isolated instances (`-UserDir=`, junctioned `Content`, hard-linked exe).

### Phase 1 — Lift the transport
6. New workspace: `qd-core`, `qd-ecs`, `qd-net`, `qd-server`, `qd-sidecar`, `qd-ffi`,
   `qd-sim`, `qd-cli`, `qd-launcher`, `qd-launcher-gui`.
7. **Fix the five defects first**, while the code is small.
8. Retune `quantize` defaults to this game's scale; keep `smoothing.rs`.
9. Prove a record crosses both ways between two instances through the relay.
   *Foundation proven.*

### Phase 2 — See each other
10. Port `dmp_puppet.h` as a pure state machine with its test (it encodes every "absence"
    bug DollsMP paid for).
11. Spawn `Player_FullChar_2026` for the remote player, hide it correctly on the owner.
12. Port `dmp_motion.h` interpolation verbatim; retune constants.
13. Transmit appearance as class names / asset paths, never pointers.
    *This is the milestone DollsMP never finished; here the art already exists.*

### Phase 3 — World facts
14. Write the save codec (`dnsav.py` pattern) **with the root guard built in**.
15. Enumerate `NamedSaveData` keys across a playthrough; define the monotone fact model.
16. Implement the one-way rule: peer intents, host applies, host broadcasts.
17. Wire the game's own "wake" calls so a synced fact actually opens the door / starts
    the scene on the guest.
18. Inventory/equipment/key items; per-player drops; a reward queue so a held player is
    still paid.
19. Dialogue lock + first-answer-decides + cosmetic subtitle lane.

### Phase 4 — Enemies
20. Host owns AI; peer copies driven with `AFS_AIController` disabled.
21. Group by authored GUID; sync `RemainingHP` / `bValid` / `RespawnTime`.
22. Port `dmp_director.h` (which player an enemy fights).
23. Load what is around both players.

### Phase 5 — Packaging
24. Launcher + GUI, save guard, build/session handshake, ini with `[Rules]`/`[Local]`,
    save backups, log collector.
25. Test rig: instances, seeded saves, `bot/dmpbot.py` driver, new scenarios.
26. `steam_mock/` early, so the tunnel is testable with no second account.

---

## 9. Pre-code checks — all three answered

All three gating questions are answered. Detail and method in `07-verification.md`,
`16-live-pass.md` and `13b-pause-surface.md`.

1. ✅ **GUID stability — CONFIRMED AUTHORED.** All 421 persisted IDs from the live save
   (7 `SavableActorStates` + all 414 `EnemySaveStates`) were found **in the pak**,
   421/421, inside the level packages that place the actors. Entity identity is free: key
   on the GUID. The highest-severity design risk is retired.
   **Respawn is measured (`18-d4-respawn.md`):** two kills 121 s apart wrote the same
   authored GUID from the same spawner, and a separate process run matched it. The
   spawner-GUID + index fallback is not needed.
2. ✅ **Steam — CONFIRMED NON-FUNCTIONAL IN THIS INSTALL.** `steam_api64.dll` is a
   *delay-load* import and the DLL is not reachable; the game runs because delay-load
   failures are deferred, not fatal. `DefaultEngine.ini` (readable in the pak) still sets
   `DefaultPlatformService=Steam` and `SteamNetDriver` with `IpNetDriver` fallback.
   → **UDP relay is the primary transport.** Steam is an optional later upgrade.
3. ✅ **Dialogue does NOT pause the world.** The native dialogue layer is 73 nodes with
   zero pause/dilation/tick sinks, and `DialogueManager`/`NPC_Base` reference no pause
   entry point; the game locks input and freezes enemies individually instead. **The
   menus DO pause** — live: Status & Inventory, Pause, Photo mode and Checkpoint/Save all
   freeze `UWorld::TimeSeconds` (`16-live-pass.md` §1, `13b-pause-surface.md`). The co-op
   consequence is the reverse of the original fear: the mod must suppress the *menu*
   pause, not the dialogue.

Also settled as a side effect: **pak payloads are plaintext.** `DefaultEngine.ini` read
straight out of the pak, so no decryption is needed anywhere. The bit-packed entry table
was subsequently decoded (`pak/PAK-DECODE.md`), so payload extraction works too.

---

## 10. Bottom line

The game is a **better co-op target than Dolls Nest in five independent ways**: same
engine, an existing full-body player model, authored entity GUIDs, a readable and small
progression save, and a real C++ layer with change delegates. The existing mod supplies
the hard, expensive, already-tested half — transport, reliability, session authority,
smoothing, PE tooling, packaging — and roughly 80 % of the knowledge about how this kind
of mod fails.

The work that remains is genuinely new but well-bounded: **re-find this image's
addresses, put a body in the world for the other player, and mirror three small save
containers.** Everything else is porting.
