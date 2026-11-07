# 22 — Close-out: D9, the shop, D4's multi-GUID spawner, and C10's apply order

This pass closes the four items that `19-final-assessment.md` §2 and
`21-streaming-ai-lesson.md` §5 still listed as content-limited or as the last RE
step. Each section gives the instrument, the measurement, and the artefact.

Live session: clean install, isolated `-UserDir=`_live/inst1saved`, windowed,
parked off-screen and muted (WASAPI). Frida 16.7.19. The original save was
backed up before the pass and restored afterwards; only the D9 evidence save is
kept deliberately.

---

## 1. D9 — a real boss defeat writes `NamedSaveData[DragonLordDead]`

**Status: CLOSED (was MEDIUM-HIGH inference).** The boss is the Green Dragon in
`780_SZ_Depths`; the write is performed by the spawner's own `LordWasKilled`, and
it persists as a `NamedSaveData` int.

### What was measured

* `GreenDragon_Spawner_C` (live `0x…e36e00`) in `780_SZ_Depths` (visible in the
  saved party's streaming set):
  * `GUIDs` = **one** authored GUID `17181166-4d738291-c77fd28a-9dada0ea`
  * `EnemyData` = `GreenDragon_Data`; its `EnemyBP` = **`Dragon_Green_BP_C`**;
    `HP = 3495` (`0x455ac000`)
  * `minSpawnDist` 5000 / `maxSpawnDist` 10000, `maxKill` 1
* Moving the player into the arena materialised **`Dragon_Green_BP_C`** at the
  spawner's location `(-28573.9, 754.5, -13028.05)`.
* Invoking the dragon's own BP **`Kill`** (found on the `Dragon_Green_BP_C`
  class chain; called through `UObject::ProcessEvent` `0x011A7AA0`) produced
  the enemy-state write:

  ```
  StoreEnemySaveState  worldctx=0x…e36e00 (the spawner)
                       guid=17181166-4d738291-c77fd28a-9dada0ea
                       respawn=0   hp=0
  ```

  That is the `EnemySaveStates` lane (D4/D5), **not** the boss flag.
* Invoking the spawner's own **`LordWasKilled`** then produced a new suspend
  save. Decoding it with `analysis/gvas.py`:

  ```
  World0_Suspend_1.sav (before)   11 NamedSaveData keys   no boss key
  World0_Suspend_0.sav (after)    12 NamedSaveData keys   DragonLordDead = 1
  ```

  The new file is 90318 B vs 90254 B. **`DragonLordDead` is present and 1.**
  Evidence save kept at `_live/evidence_d9_dragonlorddead.sav`.

### What this closes

D9's only inference — *"boss defeat is a `NamedSaveData` key, not observed in a
save"* — is now observed. The two lanes are distinct and both were seen:

| datum | lane | observed |
|---|---|---|
| boss defeated | `NamedSaveData["DragonLordDead"] = 1` | **yes**, in the suspend save |
| the boss enemy's own state | `EnemySaveStates[spawnerGUID] = {bValid=1, RespawnTime=0, RemainingHP=0}` | **yes**, via `StoreEnemySaveState` |

The arena gates stay in the `SavableActorStates` lane already closed by C4/C5;
nothing about the D9 design changes.

---

## 2. Shop — live-open, and it does **not** pause

**Status: CLOSED.** With the shop sublevel shown, the merchant is present, the
shop screen opens, and the world clock keeps running while it is open.

* The saved party's set already carries `140_GR_ShopIsleOnly` as
  **loaded + visible** (`flags=30`). The merchant actor is
  **`Merchant_NPC_BP_C` / "Merchant_Idle_Shop"** at
  `(5891.5, 46214.1, 2340.9)`.
* Teleporting the player beside it and pressing **E** (PostMessage, scan-code
  corrected) opened **"Trader Umbada"** dialogue. Selecting **Purchase Wares**
  opened the shop screen (Dried Herbs, Roasted Onion Lizard, prices, Back).
* Clock while the shop screen stayed open:

  ```
  t0  409.53   t+3  412.55   t+6  415.56     (TimeSeconds, seconds)
  ```

  **3.01 s of world time per 3 s wall → the shop does not pause.** This sits with
  dialogue (no pause) and against Status/Inventory/Pause/Photo/Checkpoint/Game
  Over (which do freeze `TimeSeconds`).

So the earlier "shop needs the level shown first" precondition is **the only**
thing that was missing: show the sublevel, and the merchant, interaction, and
shop screen all work. Screenshot: `_live/shop6.png`.

---

## 3. D4 — the multi-GUID spawner is identified

**Status: CLOSED.** Among the 66 multi-GUID `GUIDs` placements, exactly **one**
is an enemy spawner: **`LargeRobot_Rusted_Spawner` in `500_AUT.umap`**, with two
authored GUIDs. The rest are lifts, doors, stairs, a tram, a rotating bridge and
the `NecroWall_Manager` family.

### Offline: every multi-GUID placement, with its owning class

`analysis/re/_d4_multiguid_owner.py` maps each `GUIDs` header back to the export
whose serial range contains it. (The cooked export's `SerialOffset` is at
`+0x24` as an `int64` low half; the earlier probe read the wrong field, which is
why ownership was `None` for most rows.)

Class tally of the 66 multi-GUID placements:

```
Mausoleum_Lift_00_BP_C          11      OW_Lift_00_BP_C              5
NecroWall_Manager_BP_C           6      Lib_ElevatorSmall_00BP_C     5
OW_TimedLift_00_BP_C             6      ROS_Lift_BP_C                5
IntroDungerLift_BP_C             4      LabUG_Tram_BP_C              2
OfficeLift_00_BP_C               3      Castle_Lift_00_BP_C          2
SZ_Lift_00_BP_C                  3      PT_Lift_00_BP_C              2
LargeRobot_Rusted_Spawner_C      1   <-- the only enemy spawner (n=2)
AquaDoor_00_C  AQ_SCStairs_BP_C  AQR_NoSoftLockLift_C  Lab_Lift_BP_C
OW_BrokenLift_00_BP_C  Library_RotateBridge_C  AquaElevator_00_BP_C  1 each
AquaBossDoor_BP_C (n=3)  DAquaDoor_01_C (n=5)  JigekiDoor_BP_C (n=5)
SZ_Lift_01_BP1_C (n=2)                                          1 each
```

`LargeRobot_Rusted_Spawner` is at `500_AUT.umap` export `0x1584D79`,
`ArrayProperty` size 85 = 2 GUIDs.

### Live: the two authored GUIDs

After showing `500_AUT`, the spawner read back as:

```
LargeRobot_Rusted_Spawner @ (-48756.8, 60032.5, -33470.1)
  GUIDs (2)        dfdda115-4ccfdafe-ee80c696-71bf25cc
                   5071a6ed-41b1d2d3-201eaf9b-64a32d73
  SaveDataCount    2
  MySaveData (2)   (two save slots)
  minSpawnDist 5000   maxSpawnDist 10000   maxKill 1   useMaxKill 1
```

The spawner did not enter `playerInRange` on a teleport (it wants the game's
own arena activation), so the two-kill rotation cycle was not re-run on it.
That is a limit of the test, not of the identity: **the multi-GUID spawner is
identified, its two GUIDs are read at runtime, and the mod rule is unchanged —
read the spawner's runtime `GetSaveGUID()`, never `GUIDs[0]`.**

---

## 4. C10 — the manager's apply order, and the `0x2A11DE0` correction

**Status: CLOSED.** The manager apply path is now fully ordered, and the
DollsNest `0x2A11DE0` citation in `21-streaming-ai-lesson.md` §5 is **wrong for
the matching build** and is replaced by the structural analogue.

### The exact chain (all RVAs verified)

```
AddLevelListToStack (UFUNCTION)          0x00E215E0
  -> GetLevelManager(worldctx)           0x00E2EE50
       worldctx->vtable+0x160 = world; world+0x118 = GameInstance;
       GameInstance+0x3B0 = manager
  -> worker                              0x00E37800 (SEH-split body 0x00E378CD)
       arg3 (r8b) selects stack A +0x298 (0) or B +0x2A8 (1)
       appends {UFS_LevelList*, int32 prio}; dedup by {list, prio}
       prio = arg5 when arg4(r9b)=1, else [list+0x30]
       if [manager+0x290] > 0: +0x281=1, +0x284=max(prio), +0x282 -> return
       else: 0xE42950(manager, prio, 0)

0xE42950  merge + resolve
       copies stack A (+0x298/+0x2a0) then stack B (+0x2a8/+0x2b0)
       -> 0xE1D3D0 (resolve level set) -> +0x258 = top list

FlushLevelManagerActions (UFUNCTION)     0x00E2C270
  -> GetLevelManager
  immediate=false: pending +0x280=1, or if +0x290>0 return
  immediate=true : copy resolved list +0x288 -> +0x320
                   -> 0x00E2C306 -> 0x00E21E40
                        for each 16-byte {ULevel*, u8 visible}:
                          [entry+0x30] = ULevelStreaming*
                          SetShouldBeVisible(level, entry[8] != 0)
                      then world = [manager vtable+0x160]; 0x02D341E0(world, 1)

ULevelStreaming::SetShouldBeVisible      0x029ACB20
       writes byte +0xBA bit 3; tail-calls
       UWorld::UpdateStreamingLevelShouldBeConsidered  0x02D4A0E0
       (structurally identical to DollsNest 0x2D72590: same +0xB8 test,
        same +0x98 tail call)

LevelManagerUpdate / per-frame            0x00E3CF9E (vtable-dispatched)
       one +0x320 entry per frame -> SetShouldBeVisible(level, entry[8])
       -> 0x029A4310 (visibility predicate)
       whole list via 0x00E21E40 when +0x280 set
       done-loading state in +0x378 via 0x00E265F0
```

Field map (manager): `+0x288` resolved list, `+0x290` pending-action count,
`+0x298`/`+0x2A8` the two `{list,prio}` stacks, `+0x320` resolved level entries,
`+0x330` current entry, `+0x338` wanted-visible byte, `+0x340` applied result,
`+0x378` previous done/busy state.

### The `0x2A11DE0` correction

`21-streaming-ai-lesson.md` §5 called DollsNest `game+0x2A11DE0` "the function
that records/acts on the stale 'invisible' decision". In the build that matches
doc 44's own addresses (`SetShouldBeVisible = game+0x29EAED0`,
`ProcessLevelStreamingVolumes = game+0x29E4A60`), `0x2A11DE0` is **not a function
start** and is **not** the visibility decision: it sits inside
`FUN_142a11c90`, in the loop's container cleanup (`mov rcx,rdx; call
operator delete`). `SetShouldBeVisible` there tail-calls `0x2D72590` =
`UWorld::UpdateStreamingLevelShouldBeConsidered`.

The correct QD analogue is therefore **`0x02D4A0E0`
(`UWorld::UpdateStreamingLevelShouldBeConsidered`)**, and the guard hook points
are the game's own `0x00E21E40` (apply list) / `0x00E3CF9E` (per-frame manager
update) / `0x029ACB20` (the setter), **not** a `0x2A11DE0`-shaped function.

### Live proof that the union needs a guard

With the corrected `AddLevelListToStack(worldctx, list, /*sel*/0, /*usePrio*/1,
/*prio*/100)` (the earlier probe passed a `{list,prio}` entry instead of the
list pointer, which is why its add "did nothing"):

```
stack after adds: {GreenRuins, AutumnIsland(100), AutumnIslandCastle(100)}
visible before:  21
visible after add + FlushLevelManagerActions(ctx, 1): 11
visible set:     500_AUT 510_AUT_droptunnel 610_Castle_Ext 760_SZ_AUT
                 990_B33sment Intro_Level_01 000_Shared_Base_Lighting
loaded-hidden:   39 (the GreenRuins levels)
```

So `Add` + `Flush` **replaces** the visible set with the last action's resolved
list even though the stack still holds the union. This is the concrete reason
the union implementation must not use `FlushLevelManagerActions` as its apply:
it must drive `0xE42950`/the per-frame update and then re-assert
`SetShouldBeVisible(true)` for the union after the engine pass. The guard is
required, and the QD hook point is the manager apply, not the (absent)
`ALevelStreamingVolume` pass.

---

## Bottom line

| item | status | evidence |
|---|---|---|
| **D9** boss defeat → `NamedSaveData` | **CLOSED** | `DragonLordDead=1` in `_live/evidence_d9_dragonlorddead.sav`; `StoreEnemySaveState` with the spawner GUID, `hp=0` |
| **shop live-open** | **CLOSED** | merchant reached, shop screen opened; clock advances 3.01 s / 3 s → no pause (`_live/shop6.png`) |
| **D4 multi-GUID spawner** | **CLOSED** | `LargeRobot_Rusted_Spawner` (`500_AUT`) is the only enemy spawner among 66 multi-GUID placements; 2 GUIDs read live |
| **C10 apply/order + `0x2A11DE0`** | **CLOSED** | full chain above; `0x2A11DE0` corrected to `0x02D4A0E0`; live add/flush replaces the set 21→11 |

No item is left content-limited. The only remaining D4 item is the two-kill
rotation on that spawner, which needs the game's own arena activation; the
identity and the mod rule do not depend on it.

### Repro files

| file | what |
|---|---|
| `_live/frida_close.js` / `frida_close.py` | object/level/spawner/ProcessEvent driver |
| `analysis/re/_d4_multiguid_owner.py` | multi-GUID placement → owning class |
| `analysis/re/_c10_apply.py`, `_c10_apply2.py`, `_c10_apply3.py` | manager/apply disassembly |
| `_live/shop6.png` | shop screen open |
| `_live/evidence_d9_dragonlorddead.sav` | suspend save with `DragonLordDead=1` |
