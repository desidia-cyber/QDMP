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
  ladder (`objects::VisualFor`), so a tracked pot would print `visual=none` while the
  apply line names `Break`. Cosmetic; the `object apply:` line is the authoritative one.

---

# Round 2 live proof — 2026-11-15

Rig: `mod/tools/session_wi2.ps1` (new). Two off-screen instances on one machine, both
seeded from `analysis/save-backup-before-live`, booting into `Ruins_Level_00` (the Green
Ruins camp). Switch line measured on the host:

```
18:40:27.108 INFO switches: flags=1 inventory=0 actors=1 enemy=0 puppet=0 playerBody=0
             dialogue=0 dialogueView=0 stream=0 travel=0 cutscene=0 movers=0 loot=0
             suspend=0 pause=0 objects=1 interact=1 trigger=0 pullIn=1
```

`objects=1 actors=1 interact=1` — the two transport lanes and this coverage layer are on.

## What the boot world actually places (measured, not assumed)

`qd.obj` after the incremental object walk has produced rows (the walk needs ~2 s; a dump
taken the instant gameplay starts reports `tracked 0`):

```
PEER  savable rows 65 (in world 49), object rows 54
   savable SecretWall_C x12, Chest_C x9, Barrel_C x4, Barrel_Mossy_C x3, Crate_C x5,
           Crate_Mossy_C x1, AquaGate_00_BP_C x2, AquaGate_01_BP_C x2, SZ_Door_02_BP_C x2,
           SecretZoneDoor_Blue_BP_C x1, Lib_Door_01_BP_C x3, Lib_FrontDoor_BP_C x1,
           EggDoor_BP_C x2, M_DoorSmall_00_BP_C x2, AquaLever_00_C x3, MausoLever_00_C x4,
           Gargoyle_Switch_C x3, AqrStatuePuzzleDoor_C x1, PTSeal_SipehaRune_BP_C x3, ...
   object  or_pot_tall_bp_C x28, Pot_Small_Base_C x5, Pot_Fat_C x6, Pot_Tall_Base_C x6,
           Pot_Tall_2_C x4, Pot_Fat_2_C x3, Pot_Small_2_C x2   (each prop=Broken, state=0)
```

## PASSED

**A door opened on the peer opens on the host, with the class' own animation.**
`SwordShrineGate_BP`, `qd.act near:guid:709d84e1!Activate` on the peer:

```
[2] 18:44:16.761 INFO dev interact: forced verb Activate on SwordShrineGate_BP [SwordShrineGate_BP_C] ran=yes state 0 -> 1
[2] 18:44:16.788 INFO actor-state local change: queued=1 coalesced=0 echoes=0 overflow=0
[2] 18:44:16.789 INFO actor-state intent sent guid=709D84E1-48E692EE-C877CB90-7919E660 value=1 rev=172 try 1/5
[1] 18:44:16.838 INFO actor-state intent applied: guid=709D84E1-48E692EE-C877CB90-7919E660 value=1
[1] 18:44:16.838 INFO actor-state refresh: RestoreFromSave resolved at 25A5D12F4A0 frame=0 (MySaveData cleared, cache rebuilt, state re-applied)
[1] 18:44:16.838 INFO actor-state refresh ok SwordShrineGate_BP [SwordShrineGate_BP_C] index=0 value=1 mySaveData=1->1 guids=1 cached=ok opened=1
[1] 18:44:16.838 INFO interact: door anim SetToOpenState (intent) key=709D84E1 state=1
```

That is the whole claim: the state crossed, the host's own copy reads `opened=1`, and it
played the door's own `SetToOpenState` with the wanted direction.

**A barrel broken on the peer is broken on the host.** `Barrel` (`Barrel_C`),
`qd.act near:Barrel!Break`:

```
[2] 18:44:37.465 INFO actor-state intent sent guid=9C90EB3F-40441C95-3CB42E9F-908C3B73 value=1 rev=193 try 1/5
[1] 18:44:37.516 INFO actor-state: wrote guid=9c90eb3f-40441c95-3cb42e9f-908c3b73 value=1 via SavableActor.Save Data Index to World Save index=0 read back container=1 cached=ok
[1] 18:44:37.516 INFO actor-state intent applied: guid=9C90EB3F-40441C95-3CB42E9F-908C3B73 value=1
[1] 18:44:37.517 INFO actor-state refresh ok Barrel [Barrel_C] index=0 value=1 mySaveData=1->1 guids=1 cached=ok opened=n/a
```

`opened=n/a` is the measurement that `Barrel_C`/`Crate_C` carry no bool of their own
(`dev props: Barrel Broken=absent AlreadyBroke=absent BrokeBarricade=absent`): a
`Permabreakable_base_C` breakable's state IS the `SavableActorStates` entry, which is why
the GUID lane owns it and the object lane must not.

**The class survey is right about what these classes expose.** `Opened` is present on the
gate families and absent where the survey says it is not:

```
[2] dev props: AquaGate_01_SM3 Opened=1 DoorClosed=absent PreviewOpen=absent
[2] dev props: SwordShrineGate_BP Opened=0 DoorClosed=absent PreviewOpen=absent
```

**The real interaction press is NOT the door path on this content** (consistent with R91),
which is why the rig drives the class' own verb:

```
[2] 18:44:10.012 INFO dev interact: component target set (read back ok) -- the field the Blueprint open gate reads
[2] 18:44:10.030 INFO dev interact: the player's Interact input event InpActEvt_Interact_K2Node_InputActionEvent ran -- a key press runs exactly this
[2] 18:44:10.030 INFO dev interact: the input event ran and the object did not change (state 0); driving the target's own verb
```

## BUGS FOUND AND FIXED (both blocked the peer -> host leg)

**1. The object lane flooded the reliable channel and its pass never completed.**
`objects::Publisher::Due()` returns true for the whole of a HALF-SENT pass, so a set larger
than one datagram (66 rows of 18 bytes at `wire::kMaxBody` = 1200) was re-attempted on every
tick, while the incremental `ObjectsScanStep` kept growing the set and restarting the pass.
Measured: the host published `objects sent rev=12 … rev=41` at ~1 Hz while the peer's own
`peer-rev` stayed at **3**, so every peer -> host object intent was refused:

```
[2] 18:41:56.339 INFO object intent sent key=BFE8687B barricade state=1 rev=3 try 1/5
[1] 18:41:56.349 INFO object intent refused (stale): based_on=3 last_sent=11
[2] 18:42:00.335 WARN object intent given up key=BFE8687B state=1 after 5 attempts, the host never echoed it
```

Fix: `HostPushObjects` finishes the pass inside one tick (bounded to 4 datagrams), so a
pass costs one tick instead of a permanent restart.

**2. A stale basis was refused outright.** A revision is only a proxy for "is this request
still meaningful", and the host can measure that directly because it holds the actor: the
`interact::ApplyAllowed` test already applies only when the host's own state differs and the
direction is legal. The refusal therefore bought nothing and cost the entire peer -> host
leg. Fix: the stale basis is reported (`object intent: a stale basis (based_on=… ) is
accepted here; this machine decides on its own state`) and no longer refused.

Build after both fixes: `winmm.dll`, 1 459 712 bytes, `build.bat` exit 0; the whole native
suite passes (`all test executables passed`).

The rigs this round added (`mod/tools/`): `session_wi2.ps1` (the world-interact phases),
`session_gap.ps1` (the three steps a previous run could not reach), and
`session_final.ps1` (kept because it measured two facts cited below). `analysis/scratch/wi_maps.py`
and `wi_scan_doors.py` are the offline enumeration used to choose which content to point the
rig at (placed actors per map, by class).

## NOT PROVEN, AND WHY (measured, not guessed)

**A `SecretWall_C`'s own `Activate` does not write the container.** The wall's runtime bool
moves and nothing crosses, because the transport here is `SavableActorStates`:

```
[2] 18:42:50.004 INFO dev interact: forced verb Activate on SecretWall25 [SecretWall_C] ran=yes state 0 -> 1
   (no `actor-state local change` follows, on either machine)
```

The wall's save entry is `isset=0` before and after, so there is nothing for the lane to
carry. Whether the game's REAL secret-wall path writes it (a lever that writes both its own
entry and the wall's) is not established by this run; the mechanism family is proven on a
lever in the round below instead. `Opened` on a `SecretWall_C` is a runtime latch, not
persisted state, until something writes it.

## Runs 3 and 4 — the same claims re-measured after the fixes, and what is still open

`mod/tools/session_wi2.ps1 -Phase r3` (22 PASS / 2 FAIL) and `mod/tools/session_gap.ps1`
(17 PASS / 11 FAIL, the progression steps of that run included). What they add:

**The POT now crosses, and the drop trap is answered (the open risk in this document).**

```
[2] 20:05:25.495 INFO object intent sent key=9C58C769 barricade state=1 rev=1 try 3/5
[1] 20:05:23.530 INFO object intent applied: or_pot_tall_bp87 key=9C58C769 state=1 visual=Break prop=Broken 1->1
[B] distinct drop actors before=10 after=10
```

So `Break` on the receiver does NOT roll the pot's loot: the receiver runs the break visual
and the `Broken` bool and spawns nothing. **The pot path therefore stays `Break` — the
visual-only fallback this document said might be needed is NOT needed**, and the statement
"The loot lane, not this lane, owns drops" holds measured. (The drop window is a real one:
the player stands ~1.5 m from the pot, well inside the probe's 30 m, and the probe printed
the same ten Item/Drop actors before and after.)

**A SECRET MECHANISM, on a lever.** `AquaLever_00_C`, `!Activate` on the peer:

```
[2] 20:05:57.885 INFO actor-state intent sent guid=751D89E0-4CA5E855-2E6ACBAB-8CEB1636 value=1 rev=54 try 1/5
[1] 20:05:57.908 INFO actor-state: wrote guid=751d89e0-... value=1 via SavableActor.Save Data Index to World Save index=0 read back container=1 cached=ok
[1] 20:05:57.908 INFO actor-state intent applied: guid=751D89E0-4CA5E855-2E6ACBAB-8CEB1636 value=1
[1] 20:05:57.908 INFO actor-state refresh ok AquaLever [AquaLever_00_C] index=0 value=1 ... opened=n/a
```

**THREE breakable families, from the peer to the host**, each a different class:

```
[1] 20:07:32.497 actor-state intent applied: guid=9C90EB3F-... value=1   refresh ok Barrel [Barrel_C]
[1] 20:07:50.138 actor-state intent applied: guid=385C890D-... value=1   refresh ok Crate [Crate_C]
[1] 20:08:04.223 actor-state intent applied: guid=7FE5676F-... value=1   refresh ok Crate9 [Crate_Mossy_C]
```

**The door, twice more** (`SwordShrineGate_BP`, runs 3 and 4):
`interact: door anim SetToOpenState (intent) key=709D84E1 state=1` on the host.

**The door's CLOSE reached the peer's container but its animation was not observed.** The
host's own close:

```
[1] 20:20:50.113 INFO dev interact: forced verb Deactivate on SwordShrineGate_BP [SwordShrineGate_BP_C] ran=yes state 1 -> 0
[1] 20:20:50.271 INFO actor-state host set 10 entries (live 10) rev=37
[2] 20:20:50.454 INFO actor-state: wrote guid=709d84e1-... value=0 via SavableActor.Save Data Index to World Save index=0 read back container=0 cached=ok
[2] 20:20:50.454 INFO actor states applied=1 held=0 confirmed=0 failed=0 rev=37 n=10
```

The state crossed and the peer's own container reads 0; the peer did not log
`interact: door anim ... state=0`. That is the one item of this feature still NOT proven:
whether the peer's explicit close visual runs is unmeasured, not disproven (the peer's
`Opened` was already 1 before the restore, which is the condition
`NeedsExplicitAnim(sh, had, want)` is meant to see — the next run should read
`dev props: ... Opened=` on the peer immediately before the close).

**Risks this round found and did NOT fix**

* The ACTOR-STATE lane refuses a stale basis too, and it was seen rejecting one on a busy
  machine: `[1] 20:20:38.916 INFO actor-state intent refused (stale): based_on=10
  last_sent=24` (the intent was retried and then applied, so nothing was lost). Its sets are
  small enough that a pass always completes, but the same deadlock the object lane suffered
  is reachable here if an area places enough savables. That lane is not this feature's to
  change; it is reported.
* `world=1` in one machine's `savkey` dump is not a fact about the other machine's world: a
  GUID that is in the peer's world can be absent from the host's (`dev interact: no live
  actor matches 'guid:cf39fbf3'`). A scenario must pick a handle the CONSUMING machine can
  resolve, which for these actors is the name.
* `KillAllEnemies`, and any kill that writes no enemy save state, never reaches the death
  funnel — relevant to the progression lane, recorded there.


---

# Round 3 live proof - 2026-11-15

Rig: `mod/tools/session_r3.ps1` (new). Two off-screen instances, fresh pair per run, boot
level `Ruins_Level_00`. Switches: `FlagSync/ObjectSync/ActorStateSync/InteractSync/
InventorySync/QuestItemSync/KillXPSync/PartnerSaveSync/EnemySync=1`, `LogLevel=4`.
Final run: **36 PASS / 3 FAIL**, and all three remaining FAILs are the RIG's own
attribution (see "Rig attribution, not the lane"), not the feature.

## 0. THE FINDING THAT WAS BLOCKING THREE ITEMS AT ONCE: the relay

The deployed `qd-relay.exe` was stale. It predated Kinds `0x40` (KILL_XP) / `0x41` (SAVE)
and its `OBJECTS`/`ENEMY_ROWS` decoders demanded `n` rows from EVERY chunk, where `n` is
the WHOLE SET SIZE (WIRE.md §3.20) and a datagram legitimately carries a prefix. So it
DROPPED those datagrams before either peer saw them:

```
[qd-relay] malformed datagram from 127.0.0.1:55581 (kind 0x2a): unexpected end of data: needed 128 bits, 0 available   x624
[qd-relay] malformed datagram from 127.0.0.1:54889 (kind 0x14): ...                                                   x301
[qd-relay] malformed datagram from 127.0.0.1:55581 (kind 0x40): unknown packet kind 0x40                              x6
```

`ACTOR_STATES` and `INVENTORY` already had the "stop when the datagram ends" guard;
`OBJECTS` and `ENEMY_ROWS` did not. **Every host -> peer object row was destroyed**, which is
why round 2's object-lane claims only ever worked peer -> host (an intent is one datagram),
and why KILL_XP and SAVE could never arrive at all.

Fixed in `qd-core/src/protocol.rs` (both decoders) and the relay rebuilt. The rig now
asserts, before anything else:

```
  PASS [1] the relay forwarded every datagram (no malformed kinds)          relay malformed total=0
  PASS [1] the relay knows KILL_XP (0x40) and SAVE (0x41)
```

New regression test: `a_chunked_objects_body_is_forwarded_not_dropped` (relay suite now
18 tests).

## 1. SECRET WALLS ? PROVEN (both the wall and its own animation)

MEASURED in round 2, reconfirmed: `SecretWall_C`'s own `Activate` flips `Opened` and writes
NOTHING to `SavableActorStates` (`isset=0` before and after), so the GUID lane has nothing
to publish. The state that moves is the wall's own bool, which is the OBJECT lane's model.

`interact::DirectBoolClass` (new) names the family -- `SecretWall*`, `ScienceSecret*`,
`HiddenWall*`, `Illusory*`, `Illusion*`, `FakeWall*`, `SecretPassage*` -- and
`ObjectsScanStep` no longer skips such an actor as "savable-owned". It is published on
`K_OBJECTS` (cooked-path key, shape `door`) and applied through the class' own
`SetToFinalState` / `Activate`, never `Interact` and never a key.

```
  PASS [2] a secret wall is now published on the object lane -- SecretWall25 [SecretWall_C]
  PASS [2] A's secret wall change reached the OBJECT lane -- 23:52:03.444 object intent sent key=DDD5E8D1 door state=1 rev=10 try 1/5
  PASS [1] B applied A's secret wall change -- 23:52:04.275 object intent applied: SecretWall25 key=DDD5E8D1 state=1 visual=SetToFinalState prop=Opened 1->1
  PASS [1] B ran the wall's OWN open visual -- 23:52:04.275 (same line: visual=SetToFinalState)
```

Run B measured the same on a DIFFERENT class and map (`ScienceRoomDoor2
[ScienceSecretWall_C]`, `visual=SetToFinalState prop=Opened 1->1`), so the coverage is the
family, not one actor. `ScienceSecretWall_C` exposes no close verb (its own functions are
`Interact`/`TryUseKey`/`DoUnlockAnim`), so the reverse direction on that class produces no
change and the rig reports that as such rather than as a failure.

**A REAL RACE WAS FOUND AND FIXED WHILE PROVING THIS.** Run D measured the host's
in-flight body CLOSING a wall the peer had just opened (`object apply: door SecretWall14
key=B142AA4A state=0 rev=10 visual=Deactivate prop=Opened 1->0`, right after the peer's
`forced verb Activate ... 0 -> 1`). `IntentQueue::OnHostEntry` cannot hold that body: the
peer's detector (250 ms) had not queued the intent yet, so the queue had nothing to hold.
Fix: `PeerApplyObjects` now refuses a body on a key whose LOCAL value differs from what the
mirror last saw the host publish (`local != acked`) -- the mirror is the only thing that
knows "this machine has a change the host has not answered". Reported as `held`.

## 2. DOOR CLOSE ANIMATION ? PROVEN (round 2's open item)

Two independent reasons `had != want` skipped it, both now fixed by
`interact::WantsExplicitAnim`: a door is ALWAYS animated on the receiver.

* a door whose own bool already reads the wanted value (or has no readable bool at all)
  reads `had == want` and was never animated -- the one family whose only state IS its
  visual was the family the rule skipped;
* the peer's `PeerApplyObjects`/`PeerApplyActorStates` early-`continue` on `had == want`
  also skipped it.

**And a third cause, measured in run B**: the body-level owner table was built before the
actor streamed in, so `actor` was null when the animation block ran -- the peer wrote
`value=0` (the log proves it had the actor) and still animated nothing, because `sh` had
been computed while `actor` was null. The block now asks `FindActorStateOwner` for the
actor after the write and re-derives the shape.

```
  PASS [1] the peer OPENED the door and the host played its open visual -- 23:52:46.990 interact: door anim SetToOpenState (intent) key=709D84E1 state=1
  PASS [1] the host played its own CLOSE visual (its own verb) -- 23:53:04.327 forced verb Deactivate on SwordShrineGate_BP [SwordShrineGate_BP_C] ran=yes state 1 -> 0
  PASS [2] THE PARTNER PLAYED THE CLOSE VISUAL (round 2's open gap) -- 23:53:05.592 interact: door anim Deactivate key=709D84E1 state=0
```

`interact: door anim Deactivate ... state=0` on the partner is the whole claim: the close
crossed and the peer's door played its own close. (The host's own close is the game's own
verb, so it prints `forced verb ... state 1 -> 0` rather than the receiver's `door anim`
line; both are the same `SetToOpenState(false)`.)

## 4. ACTOR-STATE STALE BASIS ? PROVEN

The object lane's round-2 fix, applied to the same handler for the actor-state lane: the
refusal is replaced by a report, and the host decides from its own state (it holds the
actor, so `WrSetActorState`'s read-back plus `interact::DirectionAllowed` is a direct
measurement where a revision is only a proxy).

```
  PASS [1] NO actor-state intent was refused for a stale basis           accepted-with-report=0, refused=0
  PASS [2] a live intent went out on the actor-state lane -- 23:52:... actor-state intent sent guid=9C90EB3F-... value=1 rev=122 try 1/5
  PASS [1] and the host applied it -- actor-state intent applied: guid=9C90EB3F-... value=1
```

A new `interact::DirectionAllowed` gate also stops a one-way shape being closed or
un-broken by the partner through this lane (it had no such test before), and the body
apply reports `close-ignored=N`.

## Doors / breakables / levers not re-measured here

Round 2 already proved the pot (object lane, `visual=Break`, no drop on the receiver), the
lever/mechanism, and three breakable families peer -> host. Nothing in this round regressed
them; `test_interact` (222 assertions) and `test_objectstate` still pass and the wire
vectors still match 48/48.

## Rig attribution, not the lane ? the three remaining FAILs

* `kill attempt by inst1: inst1's XP moved` ? run E's first kill was answered by the PEER's
  game (`[2] xp: this death paid 132 here guid=12308049#1` five times) and the host paid the
  shortfall from the report. With `EnemySync=1` the enemy is RUN by one machine, so the
  machine the rig aimed from is not always the machine whose game pays. Both counters rose,
  no guid was paid twice ? which is the requirement. Fixed in the rig to assert per-machine.
* `B either mirrored a save or wrote its own` / `did not bounce back` ? the two game timers
  fired in the opposite order to the one the step assumed; the mirror ran (see
  `progression.md` Round 3) and the rig attributed it to the fixed direction. Fixed.

## Files this round (world-interact half)

* `mod/native/src/qdinteract.h` -- `DirectBoolClass`, `WantsExplicitAnim`, `DirectionAllowed`.
* `mod/native/src/proxy.cpp` -- savable-skip exemption for secret walls; always-animate-door
  in both actor-state apply sites plus the post-write owner re-resolve; the
  `local != acked` hold in `PeerApplyObjects`; stale-basis accept; `close-ignored` reporting.
* `mod/rust/qd-core/src/protocol.rs` -- OBJECTS/ENEMY chunk guards.
* `mod/rust/qd-relay/tests/relay.rs` -- `a_chunked_objects_body_is_forwarded_not_dropped`.
* `mod/native/test/test_interact.cpp` -- +18 assertions (direction policy, secret-wall
  class list, always-animate).
* `mod/tools/session_r3.ps1` -- the Round 3 rig.
