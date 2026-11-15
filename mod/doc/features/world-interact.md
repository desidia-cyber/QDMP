# world-interact — every door, breakable and secret mechanism

Owner: `world-interact` (worktree `sync-fix`). Module: `mod/native/src/qdinteract.h`.
Reserved wire IDs 0x44-0x47 / intents 13-14 were **not used**: see "Why no new Kind" below.

## What this closes

`qdobjectstate.h` gave the placed-object lane a correct shape (cooked-path key, the object's
own state bool, a visual entry that is never the interaction verb) but its class survey was
deliberately narrow — chest, door, pickup, barricade, activatable — and the world is built
from far more than that. A pass over the shipped pak finds **52 door families, 33 breakable
families and 32 secret-mechanism classes**, plus the fact that most doors are `SavableActor_C`
subclasses and therefore belong to `ACTOR_STATES`, not `OBJECTS`. This module supplies the
missing classification and the missing direction, and makes the savable path animate the
partner's door through the class's own visual.

## Coverage (derived from the pak, not assumed)

`python analysis/pak/code/probes/interact_classes.py` prints the table and its rejects.
Result: **117 BlueprintGeneratedClass actors classify** — 52 door, 33 breakable, 32
secret/mechanism. `interact_classes.txt` in that directory is the captured run.

* **Doors (52)** — `Door_Base_C` and every subclass: `Shop_Door_00_BP_C` (one-sided, opens
  through `ShopDoorOpened`/`Activate`), `SecretZoneDoor_BP_C` (+ Blue/Green/Red),
  `SZ_Door_02_BP_C`, `SZ_BoringDoor_00_BP_C`, `SZ_JailDoor_BP_C`, `Lib_FrontDoor_BP_C`,
  `Lib_IntDoor_00_BP_C`, `Lib_MidDoor_BP1_C`, `Lib_TrapDoors_00_BP_C`, `Lib_Door_01_BP_C`,
  `LibGate_00_BP_C` (+ Circle), `Lib_BossDoor_BP_C`, `Lib_TreasureGate_BP_C`,
  `KitchenDoor_BP_C`, `M_DoorSmall_00_BP_C`, `M_WodasGate_00_BP_C`, `EggDoor_BP_C`,
  `Queen_Door_Bp_C`, `CastleFrontDoor_BP_C`, `Castle_Door_Int_BP_C`, `OW_BridgeDoor_C`,
  `OW_TP_Interior_Door_C`, `OW_TP_Shutter_BP_C`, `OW_Tower_RobotShutter_BP_C`,
  `AquaDoor_00_C`, `AquaGate_00_BP_C`, `AquaGate_01_BP_C`, `AquaBossDoor_BP_C`,
  `AQR_BossExitDoor_C`, `AqrStatuePuzzleDoor_C`, `GraveyardBoss_Gate_C`, `DesBoss_Gate_C`,
  `Necro_Boss_Door_C`, `JosamiPrisonDoor_C`, `PT_Door_C`, `Maus_QG_Door_C`,
  `SG_Door_Beepee_C`, `SwordShrineGate_BP_C`, `ForbiddenDoor_BaseClass_C` and its
  Forbidden/Office/Tower/BloodRitual subclasses, `JigekiDoor_BP_C`, `ROS_Door_BP_C`,
  `Ros_Cage_Gate_BP_C`, `DAquaDoor_01_C`.
* **Breakables (33)** — `Permabreakable_base_C` (the base of `Barrel_C`, `Crate_C`,
  `ScifiCrate_BP_C` and their `_Hard`/`_Mossy`/`_Rusty` variants), `BreakableBarricade_C`,
  `Wellcover_Breakable_C`, `Pot_Base_C` and the `Pot_Fat`/`Pot_Small`/`Pot_Tall`/`Po*_Sci`
  families, `M_Kitchen_Pot_*_BP_C`, `OW_BigCrate_BP_C`, `OW_MachineBarrel_BP_C`,
  `or_pot_tall_bp_C`. Note the split: `Permabreakable_base_C`/`BreakableBarricade_C` are
  `SavableActor_C` (the GUID lane owns their state); **`Pot_Base_C` is a plain `Actor`**, so
  it is the object lane's job and its state bool is `Broken` (its name table has `Broken`,
  `Restored`, `RestorePot`, `Break`, `SetActorEnableCollision`).
* **Secret mechanisms (32)** — `SecretWall_C` and `ScienceSecretWall_C` (both are
  DOOR-shaped: they animate out of the wall), `Lever_C`, `OW_Lever_00_BP_C`,
  `MausoLever_00_C`, `AquaLever_00_C`, `Lib_Switchbox_BP_C`, `RuneButton_C`,
  `DaggerButton_*`, `gate_switch_00`, `Gargoyle_Switch_C`, `Wodaso_Rune_BP_C`.

**Rejected on purpose** (each contains a category word but is not the category): `DoorLamp_*`
(lamp), `*DoorKey`/`SwordShrine_Gate_Key` (key), `SecretSkullDoor_*` (dialogue NPC),
`UIButton*`/`WishlistButton` (widgets), `DS_Plate_BP` (dining plate), `DoorOpenShake`
(camera shake), `SZFrame_NoDoor_BP` (frame), `QueenDoortempBlocker` (collision blocker),
`*_Broken`/`*_Remnant` (mesh assets), and — the crash family — `Lib_LiftSwitch_BP_C`,
`OW_Lift_Lever_00_BP_C`, `LabElevatorSwitch_C`, `OW_Lever_00_lift_BP8` (Session 26 fault).

## Design (≤15 lines)

* Two lanes, disjoint by ownership: **`ACTOR_STATES`** (GUID) owns every `SavableActor_C`
  subclass (doors, breakables, secret walls, savable levers/barricades); **`OBJECTS`**
  (placed-path hash) owns the non-savable ones (shutters such as
  `OW_Tower_RobotShutter_BP_C`, non-savable chests/levers). The object lane still skips an
  actor with a `GUIDs` entry, so the two never write the same object.
* `qdinteract.h` is the classification + policy: class name → wire shape (the existing
  0x2A row byte), state property per family (with `DoorClosed` inversion), direction
  policy, and an ordered **animation plan** of the class's own functions.
* Apply is the class's own visual, **with the wanted direction**: `SetToOpenState(want)`
  for a door (`OpenDoor` and `CloseDoor` both call it), `Activate`/`Deactivate` for a
  lever/shutter, `SetToFinalState` for a secret wall, `Break` for a breakable.
* The savable path writes the container and runs `RestoreFromSave` as before; for a DOOR
  whose state actually changed it then calls the same `SetToOpenState(want)`, because the
  door's restore only loads `Opened` and the timeline is driven by the visual.
* A door can be closed again (`Bidirectional`); a broken barrel cannot come back.
* `Forbidden` refuses `Interact`, `OpenDoor`, `CloseDoor`, `InsertKey`, `TryUseKey`,
  `CheckRequiredItems`, `Grant*`, `AddItemToInventory`, `TryApplyDamage` — the receiver
  never presses, never consumes a key and never rolls a drop. Loot drops stay per-player
  (the loot lane owns them).
* New switch: `InteractSync` (default 0). It enables the extended classification, the
  animation plan and the savable-door animation; it rides on top of `ObjectSync` /
  `ActorStateSync`, which are the transport switches.

### Why no new Kind

0x44-0x47/intents 13-14 were reserved but not used. The existing `K_OBJECTS` row already
carries exactly the two bytes this feature needs (shape + state), reliably, with the echo,
retry, chunk and repair machinery; a second absolute-state lane would either duplicate
rows or race the first. The reservation is left unused rather than spent on a duplicate.

## Files

* `mod/native/src/qdinteract.h` (new) — classification, state-property and call-plan policy.
* `mod/native/src/proxy.cpp` — `#include`; `ObjectsScanStep`/`DevObjectDump` use
  `interact::ShapeOfClass`; `ObjectStateOf` uses `interact::StateProp/Alt/Alt2`;
  `ObjectApplyToActor` uses `interact::Plan`; the object intent/apply use
  `interact::ApplyAllowed`; `PeerApplyActorStates`/the intent handler re-apply a
  streamed-in actor whose local state disagrees (doors streamed in later) and call the
  door visual.
* `mod/native/src/qdconfig.h`, `mod/qdcoop.ini` — `InteractSync`.
* `mod/native/test/test_interact.cpp` (new) — classification, polarity, direction, plan,
  forbidden names.
* `analysis/pak/code/probes/interact_classes.py` (new) — the enumeration above.

## Proof

**Build + unit tests: PASS (offline).**

* `mod/native/build.bat` → `Built ...winmm.dll`, size 1,404,416 bytes, exit 0
  (`analysis/scratch/build_wi.log`; a first retry at 22:23 failed in another delegate's
  in-flight `qdrespawn_engine.h` — duplicate `isCp` — and passed after they fixed it, as
  the lock rules require).
* `mod/native/test/run_tests.bat` → **all test executables passed**, exit 0
  (`analysis/scratch/tests_wi3.log`, run after the final header change). The new
  `test_interact` is **205 assertions, 0 failed**; the wire-vector cross-check (native vs
  qd-core) still matches 48/48, and the existing `test_objectstate` (180) and
  `test_actorstate` suites are unchanged and pass. No Rust change was needed (no new
  Kind; `cargo` suites therefore unaffected).

**Live: NOT live-proven.** The LIVE lock was held by another delegate for the whole
window, and the current rig has no door/breakable/secret phase with `InteractSync=1`
(`session26.ps1` lists the switches at line 503 and does not include it). The switch is
therefore left at `0`. The recipe and expected lines:

1. Set `ObjectSync=1` (non-savable) and/or `ActorStateSync=1` (savable) **and**
   `InteractSync=1`, `LogLevel=2`; launch the two instances from a fresh pair.
2. On the peer, open a `Door_Base_C`/`Shop_Door_00_BP_C`/`SecretZoneDoor_BP_C` instance:
   * savable door → host: `actor-state intent applied ... value=1` then
     `actor-state refresh ok ... opened=1` then
     `interact: door anim SetToOpenState key=… state=1`;
   * the peer then logs `actor states applied=1 …` and its own
     `interact: door anim …` if the door streamed in late.
3. Close the same door from the other side; `state=0` must reach both and
   `interact: door anim SetToOpenState … state=0` must appear (bidirectional).
4. Non-savable shutter (`OW_Tower_RobotShutter_BP_C`): receiver
   `object apply: door … visual=Activate` / `Deactivate`.
5. Break a `Barrel_C`/`Crate_C`: the other machine must show
   `actor-state refresh ok …` for the barrel key and the mesh must be broken; a second
   interaction must not re-break it. Do **not** expect a synced drop (drops are per-player).
6. Secret wall (`SecretWall_C`): the partner's wall must slide out
   (`actor-state refresh ok … opened=1` + `interact: door anim SetToFinalState …`).

Prior live evidence this builds on (same game functions, different switch):
`SZ_BoringDoor_00_BP9 opened=1` on the other machine (LIVE-TEST-LOG Session 17, R123) and
the shop door's `SetToOpenState ran` on the partner (Session 27). The new code makes the
door visual explicit and closes the "streamed in later" gap; those two lines are not yet
seen with `InteractSync=1`.

## Open risks

* `Break` is assumed to be the class's visual/state path (the loaded save's
  `Restore State From Save Data` calls it) and NOT the loot roll. `Pot_Base_C`'s name table
  also carries `TrySpawningItem`/`ItemDrop`, so if a live run shows the receiver's pot
  spawning a drop when `Break` is called, the pot path must fall back to a visual-only
  composition (`SetActorEnableCollision(false)` + hide the unbroken mesh) and this statement
  must change. The loot lane, not this lane, owns drops.
* A non-savable actor with no readable bool is not published by the object lane: the
  `OW_Tower_RobotShutter_BP_C` shutter (`FS_StaticMeshActor_C`, only `bDead`) is the known
  case. It is a spawner-driven shutter rather than a player-completable door; the
  `Door_Base_C` shutters (`OW_TP_Shutter_BP_C`) are savable and covered.
* The savable-door animation calls `SetToOpenState(true/false)` after the restore. If a
  door subclass' restore already plays the timeline, this would animate twice; the door
  whose restore was traced (`Door_Base_C`) does not reference `SetToOpenState`, so the
  explicit call is the measured choice. Verify on the live rig.
* `PreviewBroken`/`PreviewOpen` are last-resort state probes; they are editor-named and
  only used when every real bool is absent.
* The object lane's `object sync: tracking … visual=` line still prints the OLD narrow
  ladder (`objects::VisualFnFor`), so a tracked pot would print `visual=none` while the
  apply line names `Break`. Cosmetic; the `object apply:` line is the authoritative one.
