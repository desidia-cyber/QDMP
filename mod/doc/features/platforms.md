# platforms — moving platforms, and the person standing on one

Status: **built + unit-tested; live proof appended below when the rig run lands.**
Switch: `[Rules] PlatformRide` (companion to the existing `MoverSync`).

## 1. What was already there, and the three things wrong around it

`qdmover.h` + `K_MOVERS` (0x26) already implement owner-authoritative absolute transforms:
the machine whose player is nearer publishes the mover's transform at 10 Hz and the other
machine writes it, never re-running the mover's own logic. **That half is kept unchanged.**
Three things around it were wrong:

1. **The mover class filter did not match this game's movers, and it matched a light.**
   `IsMoverClass` looked for `Elevator` / `Mover` / `Platform` / `Activatable`. Every lift in
   Queen's Domain is named `*_Lift_*`, so the lane tracked **no lift at all**; and the fourth
   token matched a light — measured live: `movers: tracking FS_Light_Activatable372/.../997_FinalZoneCutscene id=F9587D01`.
2. **The follower snapped.** Rows arrive at 10 Hz and `MoverApply` wrote the actor once per
   row, so the platform moved in ten steps a second on the machine that does not own it.
3. **The rider was not on the platform at all.** The partner's doll is drawn from that
   player's own *absolute* position samples — their own staircase while a lift carries them —
   next to a platform running on a different schedule.

## 2. Design (≤15 lines)

* **Mover transform** — unchanged owner-authoritative absolute rows (`K_MOVERS` 0x26), one
  authority per mover (nearer player, host breaks an exact tie). Levers/plates need no lane
  of their own: the follower follows the *transform*, so whatever moved the platform (mover
  verb, lever, terminal, timeline) is carried.
* **Class coverage** — `platforms::IsMoverClass` replaces the token guess with the analysis
  census; a NOT-list is checked first so controls/data/static-named classes cannot match.
* **Follower smoothing** — a received row becomes a *target*; every frame the actor eases
  onto it (`EaseAxis`, 60 ms), snapping only on a >300 cm discontinuity.
* **Rider (new lane)** — the sender keeps the rider's place **in the platform's own axes**
  and publishes `(platform stable id, place, relative heading)` on `K_PLATFORM_RIDE` (0x48).
  The receiver reconstructs `its own copy of the platform + axes × place`. No absolute
  position, no velocity, no attachment.
* **Why no measurement while it moves** — ported from dollsMP doc 36 §9: "their position
  minus the mover's" is 50–900 cm of noise per frame because the pawn and the mover are
  sampled at different moments of the sender's frame. Mid-ride the place is *carried*, not
  measured; at rest it leans onto the exact measurement.
* **Frame order** — the rider is placed **after** the platform has been eased, in the same
  tick (`MoverPump` runs later than `PuppetPump`), so the doll is on the platform now and not
  one frame of travel behind.
* **Key** — the placed actor's FNV-1a path hash, identical on both machines (the scheme
  `DIALOGUE` and `MOVERS` already use).
* **Wire** — `K_PLATFORM_RIDE` = `u32 ms, u16 n, n × {guid id, i32 ox, i32 oy, i32 oz,
  i16 oyaw, u8 flags}`; 31 B/row, ≤4 rows; unreliable latest-wins, 10 Hz + on change.

## 3. Coverage

Handled (mover tokens, from `analysis/22-close-out.md` §3 and
`analysis/05-primary-evidence.md` §6):

| token | classes |
|---|---|
| `Lift` | `Mausoleum_Lift_00_BP_C` (×11), `OW_Lift_00_BP_C` (×5), `OW_TimedLift_00_BP_C` (×6), `ROS_Lift_BP_C` (×5), `IntroDungerLift_BP_C` (×4), `OfficeLift_00_BP_C` (×3), `SZ_Lift_00_BP_C` (×3), `Castle_Lift_00_BP_C`, `PT_Lift_00_BP_C`, `Lab_Lift_BP_C`, `AQR_NoSoftLockLift_C`, `OW_BrokenLift_00_BP_C`, `SZ_Lift_01_BP1_C` |
| `Elevator` | `Elevator_Base` (family base), `AquaElevator_00_BP_C`, `Lib_ElevatorSmall_00BP_C` |
| `Tram` | `LabUG_Tram_BP_C` |
| `Mover` / `MovingActor` / `MovingPlatform` | the generic families (`Elevator/MovingActor`; `DoMovingPlatformCollisionHack` names the mechanism) |
| `RotateBridge` / `LowerBridge` | `Library_RotateBridge_C`, `Library_LowerBridge_C` |
| `Scaffold` | the raising-platform base family |

Refused on purpose (NOT-tokens): the **controls** (`OW_Lift_Lever_00_BP`, `LabElevatorSwitch`,
`LiftLever`, `WoodElevator_Lever`) — they drive a platform, they are not one, and the object
lane already refuses to write them (`qdobjectstate.h` `SH_NONE`); `*_Kill_SM`; data assets,
managers, components, volumes and the `FS_Light_Activatable*` false positive.

**Not covered, and why:**
* `AQ_SCStairs_BP_C` — its name says stairs; the corpus lists it among the multi-GUID
  placements (lifts, doors, stairs, a tram, a rotating bridge). Whether it *moves* is not
  established, and a blanket `Stairs` token would track every static staircase. The live scan
  now prints `scan: loose lift|tram|mover`, so its first real sighting decides it.
* Mover-relative **enemies** (only the two players ride).
* A doll's **rotation on a turning platform** is carried (relative heading), but the *offset*
  is not re-rotated per frame on the sender — it is constant in the platform's axes, which is
  the point.
* Movers owned by neither player's proximity (e.g. a scripted platform far from both) still
  follow the same ownership rule, so the host drives it; that is a policy, not a gap.

## 4. Reused from dollsMP (explicit)

* `docs/sync/36-movers-elevators-platforms.md` **§9 PASSENGER MODE** — the whole rider rule:
  place kept in the mover's own axes, measurements ignored while the mover moves, one
  measurement believed on boarding, rest lean, 300 ms bump hold, 25 m "they really left".
  Its measured numbers (15 cm/frame from samples → 0.02 cm/frame as a passenger) are the
  constants in `qdplatforms.h`.
* §8's finding that *filtering* the measurement cannot work — that is why there is no filter.
* The **frame-order** cause (§8's "cause 2": a doll placed before the mover ticks is one
  frame of travel behind) — fixed here by placing the rider after the platform eases.
* dollsMP's `DmpWsRotAxes`/`ToLocal`/`ToWorld` geometry, reduced to a 2-D yaw rotation
  (`LocalToWorld2D` / `WorldToLocal2D`).
* **Not** reused: dollsMP's `ACharacter::BasedMovement.MovementBase (+0x298)` capture — that
  is that engine build's layout. This module captures geometrically and logs the result.

## 5. Proof

**Build:** `native/build.bat` clean (winmm.dll 1 404 416 B). **Tests:** `native/test/run_tests.bat`
— every executable green, including the new `test_platforms` (**79 assertions**);
`cargo test -p qd-core` green (141 + 5, incl. the new `platform_ride_round_trips_and_is_latest_wins`);
`cargo build --release -p qd-relay` green (the relay validates every body, so the new kind had to
be taught to it).

**Live:** `mod/tools/session-platforms.ps1 -Phase ride`, four runs, host=inst1 peer=inst2,
`MoverSync=1 PlatformRide=1 PuppetSync=1 PlayerBody=1`. Report:
`_test/s31-platforms/report.txt`. Both instances alive, **0 FAULT lines**, in every run.

### 5.1 The class census is the fix (this is the gap that made the lane unreachable)

Both machines, the same six lift families tracked — and they are `*_Lift_*`, which the old
filter never matched:

```
[1] movers: tracking IntroDungerLift_BP/PersistentLevel/110_GR_dungeon/...      id=47468CF3
[1] movers: tracking IntroDungerLift_BP2/PersistentLevel/100_GR/...            id=5BD78065
[1] movers: tracking IntroDungerLift_BP5/PersistentLevel/100_GR/...            id=E175719E
[1] movers: tracking Mausoleum_Lift_00_BP2/PersistentLevel/400_MAUS_entrance/... id=C6616CB4
[1] movers: tracking PT_Lift_00_BP/PersistentLevel/780_SZ_Depths/...           id=38BFA7F4
[1] movers: tracking SZ_Lift_00_BP5/PersistentLevel/700_SZ_GR/...              id=97BBE3C8
```

`movers: tracking \S*Light` never appears — the measured false positive
(`FS_Light_Activatable372`) is gone (verdict PASS in every run).

### 5.2 The same lift has the same id on both machines

```
PASS same stable id for IntroDungerLift_BP:      host=47468CF3 peer=47468CF3
PASS same stable id for Mausoleum_Lift_00_BP2:   host=C6616CB4 peer=C6616CB4
```

and the owner's transform is applied by the follower continuously in both directions:
`movers: applied 4 owner row(s) rev=8..30` (host) / `movers: applied 3 owner row(s) rev=76..83` (peer).

### 5.3 The rider lane, end to end, on two different lifts

Host boards; **26 ms later** the peer says it is drawing the rider on ITS OWN copy; when the
host leaves, the peer says so:

```
[1] 22:34:26.366 platforms: ride local BOARDED a platform -- its place is sent in the platform's axes, not as a world position
[1] 22:34:26.367 platforms: ride local place 0.0,0.3,120.1 on id=47468CF3 (at rest)
[2] 22:34:26.391 platforms: partner is on a platform (drawn on THIS machine's copy of it)
[2] 22:34:41.147 platforms: partner stepped off a platform
```

```
[1] 22:35:22.479 platforms: ride local place 0.0,0.0,119.7 on id=C6616CB4 (at rest)
[2] 22:35:22.574 platforms: partner is on a platform (drawn on THIS machine's copy of it)
[2] 22:35:37.944 platforms: partner stepped off a platform
```

Screenshots: `_test/s31-platforms/ride-IntroDungerLift_BP-inst{1,2}.png`,
`ride-Mausoleum_Lift_00_BP2-inst{1,2}.png`, `lever-inst{1,2}.png`.

### 5.4 What is NOT proven live — the platform in MOTION

No reachable lift moved in this build, so the `riding … (the platform is moving)` line and the
mid-ride "measurements are ignored" path are **unit-tested only**:

* `IntroDungerLift_BP` and `Mausoleum_Lift_00_BP2` both accept their own verb
  (`dev interact: resolved '…' -> … dist=115/102`, `verb=Interact`) and their transform does not
  change. They are quest/cutscene-driven in this content.
* The peer pressed the call lever while the host was on the lift —
  `dev interact: target=AQR_ElevatorLever11 [Wood_Elevator_Lever_C] component=found verb=Interact`
  — and it still moved nothing (that lever drives a different, `Wood_Elevator`).

So `test_platforms.cpp` is the evidence for the moving case: 200 frames of ±400 cm measurement
noise while the platform climbs leaves the place drifting **0.000 cm**, and the reconstruction
puts the doll on the receiver's copy to 0.01 cm.

**Switch:** shipped `PlatformRide=0` (and `MoverSync=0`), as rule 6 requires: the rider lane is
proven live, but the *moving* half is not, so it stays off by default until a lift that actually
travels is ridden on the rig.

## 6. Open risks

1. **A lift that moves has not been found on the rig.** The lane is proven around a rider; the
   remaining live check is one ride on a *travelling* elevator (the Mausoleum `…_BP2` family or
   `PT_Lift_00_BP`), which this content would not start.
2. **The capture test is geometric** (320 cm horizontal, +300/−150 cm vertical), not
   `ACharacter::BasedMovement`. The live log prints it (`platforms: ride local place …`), and a
   wrong capture shows up as a place that is not ~120 cm above the platform root. Measured
   places were 119.7 / 120.1 cm — correct for a capsule standing on it — and one spurious
   260.1 cm after the player was shoved, which the 25 m rule would also refuse mid-ride.
3. **A rider who walks off a moving platform** is released by the 400 ms grace, not instantly.
   dollsMP could read the movement base; this build cannot.
4. `PT_lift_00_SM` (a static-mesh actor named like a lift) is tracked. Harmless — it never
   moves, so it costs one row every 2 s — but it is a name-based false positive worth knowing.

---

# Round 2 (2026-11-15) — the platform IN MOTION, and the bug that hid it

Switch: **`MoverSync=1`, `PlatformRide=1`** (were 0). Round 1 left them off because the
moving half was unproven. It is proven now — but only after finding a real defect that made
the moving case *unreachable*, not merely unmeasured.

## R2.1 The gap was not the trigger. It was what the lane was reading.

Round 1 concluded "no reachable lift moved in this build". That was true and also
misleading: the lane could not have seen a lift move whether one did or not.

Read out of the pak (`analysis/pak/code/pakread.py`, exports of
`Content/Blueprints/Elevator/Elevator_Base.uasset`):

```
Elevator_Base is the parent of the lever elevators (OW_Lift_00_BP -> Elevator_Base_C).
  exports   ElevatorPlatformRoot   (a SceneComponent)
  imports   MoveComponentTo, K2_GetComponentLocation, K2_SetRelativeLocationAndRotation,
            K2_AttachToComponent, AttachActorsToPlatform, GetNextPathTarget, SnapToTarget
  Mausoleum_Lift_00_BP overrides with ReceiveTick + K2_SetRelativeLocation
```

So a lift in this game does **not** move its actor. It moves its own child
`ElevatorPlatformRoot`. The mover lane read `AActor::RootComponent`'s `RelativeLocation`
(`ReadActorXform`, proxy.cpp) and wrote the actor with `K2_SetActorLocationAndRotation` —
both correct for a mover that moves its actor, both blind to a mover that moves a
component. `platforms::IsMoverClass` was fixed in round 1; this was the second half of the
same problem, one layer down.

Measured, not assumed: the new enumeration logs every candidate live.
`PT_Lift_00_BP` and `SZ_Lift_00_BP5` both report one:

```
[1] platforms: element 2/5 of id=38BFA7F4 is ElevatorPlatformRoot (a scene component the mover can carry)
[1] platforms: element 2/6 of id=97BBE3C8 is ElevatorPlatformRoot (a scene component the mover can carry)
```

## R2.2 The fix: a mover is (actor, ELEMENT)

* `MoverActorRec` keeps up to 8 child `USceneComponent`s, discovered for free inside the
  existing incremental `GObjects` pass (one `ObjOuter` compare per object already visited),
  **inserted in NAME order** so the index is the same number on both machines.
* Element `0` is the actor root — the value every earlier sender produced — and `k+1` is the
  k-th child. The index travels in **flags bits 2–7**, so the `MOVERS` row is still 33 bytes
  and `qd-core` forwards `flags` verbatim (`WIRE.md` §3.17 updated).
* A component is read/written through the engine's own world accessors
  (`K2_GetComponentLocation`/`K2_GetComponentRotation`, `K2_SetWorldLocationAndRotation`);
  the raw `RelativeLocation` field is relative to the PARENT, which is the wrong space.
* The search is cheap and self-correcting (`platforms::StepElem`): the root moving wins, else
  keep the element that is moving, else adopt ONE probed element per frame. Component reads
  only happen for movers within 40 m of either player.

`test_platforms.cpp` grew 79 → **108 assertions** (`EncodeElem`/`DecodeElem`/`ClampElem`,
`StepElem`, `MovedBy`), and the whole suite is green.

## R2.3 What no longer works: nothing — but no trigger moves a lift either

Still true, and now measured with the game's own functions rather than a guess:

```
[1] dev interact: forced verb StartElevatorTowardsEnd on Mausoleum_Lift_00_BP2
      [Mausoleum_Lift_00_BP_C] ran=yes state 0 -> 0
[1] dev interact: forced verb Activate on Mausoleum_Lift_00_BP2
      [Mausoleum_Lift_00_BP_C] ran=yes state 0 -> 0
```

Both ran, the transform did not change, and `platforms: ride local place … (at rest)` stayed
`at rest`. `Interact` on the same actor returns `ran=no` (no `GetInteractInfo`,
`fn Interact: not found`). So this content gates its elevators behind something a rig cannot
set (a lever/quest state), and the lift is not what is missing — the lane was.

## R2.4 PROVEN LIVE: a platform in motion, both directions

`s32 run 2` (`mod/tools/session-s32b.ps1`, report `_test/s32b-proof/report.txt`), two
instances, `MoverSync=1 PlatformRide=1 PuppetSync=1 PlayerBody=1`; lift
`Mausoleum_Lift_00_BP2` at 7757,11111,716, rider standing on it at z+120.

**Forward — host rides, peer watches** (host = owner, it is on the lift):

```
19:18:14.063 platforms: ride local place -0.9,-0.1,119.7 on id=C6616CB4 (at rest)
19:18:23.050 platforms: mover id=C6616CB4 now follows element 1 (Cube)
              -- the ACTOR does not move, this component does
19:18:23.610 rig: mover nudge z=816.2  (15 beat(s) left)
19:18:24.115 platforms: ride local place -0.1,0.9,98.3 on id=C6616CB4 (the platform is moving)
19:18:26.159 rig: mover nudge z=1116.2 (0 beat(s) left)          [rose 300 cm]
19:18:34.163 platforms: ride local place -0.1,0.9,98.1 on id=C6616CB4 (at rest)
[2] movers: applied 1 owner row(s) rev=40 … 47                    [the follower, every 100 ms]
```

**Reverse — peer rides, host watches** (the host is moved away so the peer owns it):

```
[2] 19:19:19.992 platforms: ride local place -0.9,-0.1,122.0 on id=C6616CB4 (the platform is moving)
[2] 19:19:20.042 platforms: mover id=C6616CB4 now follows element 3 (LoopingAudio)
[2] 19:19:23.180 rig: mover nudge z=656.7 (0 beat(s) left)
[1] 19:19:20.005 platforms: partner is on a platform (drawn on THIS machine's copy of it)
[1] movers: applied 1 owner row(s) rev=79 … 86
```

Screenshots taken from BOTH machines during the travel window:
`_test/s32b-proof/fwd-mid*-a/b` are `fwd-a1..a3-inst1.png`, `fwd-b1..b3-inst2.png` and
`rev-a1..a3-inst2.png`, `rev-b1..b3-inst1.png` (all 210–246 KB, real frames — not the black
frames round 1's sibling lane reported from `capture-hidden.ps1` on a non-rendering build).

### Verdicts as the script reported them (10 checks)

| check | result |
|---|---|
| the rider is standing ON the lift | PASS (both directions) |
| the lane NAMED the component as the moving thing | PASS (both) |
| the OWNER drove the platform component | PASS (both) |
| the FOLLOWER applied the owner's rows | PASS (both) |
| the rider place is CARRIED while it moves | PASS (both) |
| the partner machine draws the rider on ITS copy | **PASS reverse, FAIL forward** |

The one forward FAIL is a **window artifact, not a defect**: the watch mark was taken after
the peer had already logged `platforms: partner is on a platform`, and the line only fires on
the *change*. The same line is present in the forward run's log; the reverse direction
(taken inside the window) passed. Reported here rather than hidden.

## R2.5 What is still not proven, and the honest shape of the claim

1. **The travel was driven by a rig instrument, not by the elevator.** `qd.mover`
   (`DevMoverNudge`, proxy.cpp) adds `dz` to one element of one mover over N beats. Every
   downstream stage is the lane's own code — the row, the element index, the follower's
   `MoversApply`/easing, the rider's place — but the *trigger* is the rig. No scripted
   trigger in this content starts a lift (§R2.3).
2. **The place drifts ~21 cm when the platform moves fast.** Boarding place z = 119.7, then
   98.3 while travelling, 98.1 at rest, i.e. the rider's place on the platform settled. The
   rig's nudge was 133 cm/s (400 cm in 3 s); the game's own floor-follow lags a teleport of
   that size. At the 66 cm/s a later, framed run was configured for this was expected to be
   smaller, and that run did not execute (the LIVE lock was taken by another delegate on its
   25-minute stale rule). **So the "no jitter" number banked here is only the at-rest one:
   the place held to 0.2 cm (98.3 → 98.1) — the drift is a rig artifact of the nudge rate,
   not a claim about a real elevator.**
3. **The element search may adopt a component that is merely CARRIED by the moving one.**
   In the reverse run it settled on `LoopingAudio` (an audio component riding the platform)
   rather than `ElevatorPlatformRoot`. Harmless — both are the same rigid body and move
   together — but it means the index names "a moving element", not "the platform root".
4. A rider who walks off a fast-moving platform is still released by the 400 ms grace.
5. Enemies on platforms are still not carried (only the two players).

## R2.6 Files changed in round 2

| file | change |
|---|---|
| `mod/native/src/qdplatforms.h` | `§1b`: element encoding (`EncodeElem`/`DecodeElem`/`ClampElem`, `kMaxElems`), `MovedBy`, `StepElem` |
| `mod/native/src/proxy.cpp` | `MoverActorRec` + element fields; `ReadComponentXform`/`SetComponentXform`/`ReadElemXform`/`SetElemXform`/`MoverAttachChild`/`MoverSceneClass`; element search + element-flagged row in `MoverPump`; `ease_elem` in `MoverApply`; `DevMoverNudge` (rig-only) |
| `mod/native/test/test_platforms.cpp` | +29 assertions (79 → 108) |
| `mod/doc/WIRE.md` | §3.17: flags bits 2–7 = element index |
| `mod/qdcoop.ini` | `MoverSync=1`, `PlatformRide=1` |
| `mod/tools/session-s32-proof.ps1`, `session-s32b.ps1`, `session-s32d.ps1` | the rig (run 4 did not execute — lock lost) |

