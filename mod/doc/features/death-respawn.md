# death-respawn  Ea player's death is per-player, not a world event

Owner: delegate `death-respawn`. Module: `mod/native/src/qdrespawn.h` (rules) +
`mod/native/src/qdrespawn_engine.h` (engine half). Wire: `RESPAWN_DOWN/UP/REST`
(`0x54/0x55/0x56`).

## The gap, measured

In single player a death is the game's own business: `FS_PlayerChar_C:Kill` runs the death
pose and opens the Game Over screen; "Continue" reloads the world from the save. In co-op
that costs the SURVIVOR everything:

* the Game Over screen **pauses** this machine's world clock (SYNC-DESIGN §2.9;
  `20-residual-verification.md`: `TimeSeconds` 440.50644 ↁE440.50650 over 2 s while
  `RealTimeSeconds` advanced), which the partner sees as a hang;
* the reload tears the world down **under the partner too**  Ethe fight restarts, the enemy
  records are re-read, the level list is rebuilt  Eand Session 22's peer died *inside*
  exactly such a teardown (the R140/R138 family: a cached object meeting a destroyed world).

SYNC-DESIGN §2.9 states the rule this implements in one line: **a respawn is a position; a
rest is a world event.** Until now the mod had neither half for a player's death (the enemy
death lane, `qdenemy*`, is a different subject).

## Design

* **Authority.** The machine that OWNS a player is the only authority on that player's
  health, death and rest point. Nothing is negotiated and there is no intent: an intent is a
  REQUEST to the host, and a player's own health is not the host's to grant. (The reserved
  intent 16 is therefore unused  Estated here rather than spent on an empty message.)
* **The death.** Observed twice, in the same tick boundary: the game's own
  `FS_PlayerChar_C:Kill` dispatch on the local pawn (ProcessEvent), and a poll of
  `Health <= 0` / `bDead` / `IsDead`. `DeathState::NoteDeath` is idempotent per death, so the
  two reports cost one stack, not two.
* **The world-reloading flow is REFUSED, not avoided.** `ShowGameOverScreen` on the local
  player is vetoed at the ProcessEvent boundary (the same mechanism `qdsuppresspause` uses
  for `SetGamePaused`). Everything else the death does still runs  Ethe pose, the montage,
  the game's own bookkeeping. "BLOCK" is logged, so a refused dispatch is never silent.
* **The pose.** `respawn::kPoseMs` = 3500 ms. The game's own down montage is ~2 s
  (`A_DYING` ↁE`A_DEAD` in `qdavatar.h`); shorter would read as a teleport out of a standing
  pose, longer costs the pair a fight.
* **The respawn is a position write, never a load.** After the pose: `SetActorLocation` to
  the player's own last rest point, `Health` to the penalised maximum, `bDead`/`KnockedDown`
  off, `SetMovementMode(Walking)`, `SetIgnoreMoveInput/LookInput(false)`,
  `EnableAllPlayerInput`, and the view target put back on the player. No rest call, no
  `SavableActorState` write, no enemy record cleared, no `OpenLevel`/`LoadMapWithLoadingScreen`.
* **Which rest point  Eper player.** The point is recorded when THIS machine's player
  presses a `Checkpoint_C` (`Interact`/`InteractHeld`, with `HealToFull` as the game's own
  "the rest completed" signal). It is stored as **the place the player stood** at that
  moment (a place the capsule already occupied, so no falling-through) plus the checkpoint's
  `<actor>/<level>` key for the log and the world check. Fallback, as the task asks: the
  save's checkpoint  Ethe pawn's own `LastTouchedCheckpoint` / `LastCheckpoint` object, which
  is exactly what a load restores.
* **A cross-map rest point is refused.** Respawning into another map would need the load
  this lane exists to avoid, so a point whose level is not the current map falls back to the
  save's checkpoint in this map; if that is not here either, the player stays where the body
  fell (a place it already occupied).
* **The partner** is told three things and nothing else: DOWN (with where the body fell and
  how many deaths it now carries), UP (the rest point, the restored HP and the penalised
  maximum), REST (the stacks cleared). On UP the partner **teleports** the puppet: the
  interpolation buffer is reset, one sample is pushed, and `PlaceBody` is called in the same
  frame, so the body does not slide across the map. While DOWN the puppet plays `A_DEAD`.
  The partner's enemies, progress, area and world are not touched by any of it.

## The penalty

Each death costs **10 % of the BASE maximum**, stacking, to a **floor of 10 %**:
100 / 90 / 80 / … / 10 / 10 / … A rest restores 100 % and clears the stacks.

It is applied as a **multiplier over the game's computed maximum**, not as a one-off number:

* The game recomputes its own maximum in `CalculateMaxHP` (out pin `HP`). That function is not
  in the player's UFunction chain on this build (measured: `CalculateMaxHP=NO` on
  `FS_PlayerChar_C`); it is on the player's **stats component**, and the lane finds it there,
  observes the dispatch, and scales **the value the game computed** after the call returns
  (`-> x0.9`). So a recomputation on a level-up, an equipment change, a load or a rest cannot
  erase the penalty, and two callers cannot apply it twice  Ethe value scaled is always the
  game's, never ours. Live: `respawn: the game computed max HP 100.0 -> 80.0 (80%, 2 death(s))`.
* A computed maximum **below 75 % of the health this machine has already read** is refused: the
  same pawn class answers 100 for a player standing in a menu map and 400 for the real one, and
  taking the smaller number would shrink a 400 hp player to 90 (measured, run 3).
* Because the maximum itself is not writable here, the penalty is also held as a **cap on
  `Health`**: every 200 ms, while the player carries stacks and is not down, a value above
  `ScaledMax(base, stacks)` is written back down and logged. Whatever the game computes and
  heals to, the penalised maximum is what the player can reach. Live: `Health 400.0 is above the
  penalised maximum 320.0 (base 400.0 x 0.80) -- held at the cap`.
* Our own call to the maximum function is excluded from the hook by a flag, because scaling it
  would double the penalty.
* Base-maximum sources, best first: what the game itself computed (a trustworthy one), our own
  call, and  Elast resort  Ethe highest `Health` this session has read, which is a LOWER bound
  (healing caps at the maximum) and therefore errs toward a smaller penalty rather than above
  the cap.
* At respawn the restored HP is `ScaledMax(base, stacks)`; after a rest the same call returns the
  full maximum and that value is written, so a rest that did not recompute the max in the game
  still leaves the player at 100 %.

## Persistence

* **Level streaming / a world change**: the stacks survive (a fact about the player); the
  cached rest-point ACTOR is dropped (`RespawnWorldReset`), the stored position and key are
  kept and re-checked against the current map.
* **Save/quit**: the STACKS survive, deliberately, in a mod-owned **sidecar**
  `qdcoop-<tag>-respawn.ini` beside this instance's own log (so two players on one machine never
  share a penalty). The game's save cannot carry it: writing a mod field into `NamedSaveData`
  would make the penalty a story flag the partner's bank could overwrite, and `qdsave.py`'s
  monotonic guard exists to stop a mod from lowering the game's data. A penalty that forgets
  itself on a relaunch rewards quitting. The **location** is not persisted (measured, run 2: a
  position remembered from another session was used as "this player's last rest" before the
  player had rested at all); a relaunch falls back to the save's checkpoint, which is what a load
  restores anyway.
* The sidecar stores integers only (positions in the wire's own 1/8 cm units): no float
  formatting, no locale, and an unreadable line is skipped, never applied.

## Wire

| kind | name | body |
|---|---|---|
| `0x54` | `RESPAWN_DOWN` | `deaths u8, pct u8, seq u8, x/y/z i32 (1/8 cm)` |
| `0x55` | `RESPAWN_UP` | `deaths u8, pct u8, seq u8, x/y/z i32, yaw i16, hp u16, hp_max u16, name str` |
| `0x56` | `RESPAWN_REST` | `deaths u8 (=0), pct u8 (=100), seq u8, x/y/z i32, name str` |

All three are **reliable** (a death/respawn is an event; a lost UP leaves the partner's puppet
where the body fell until the next 30 Hz row). `seq` is a per-lane counter; `SeqGate` applies
the newest and drops a duplicate  Ea second UP would teleport the puppet back to a rest point
the player has left, and the comparison is a **signed** byte so 255 ↁE0 is newer, not older.
Bodies refuse what would become a wrong state: a stack and a percentage that disagree, a
non-finite position, `hp > hp_max`, a REST that is not "0 deaths, 100 %".

## Files

| file | what |
|---|---|
| `mod/native/src/qdrespawn.h` | the RULES: the penalty ladder, the per-player state machine, the rest-point record, the three bodies, the sequence gate. Pure (no UObject, no clock), unit-tested. |
| `mod/native/src/qdrespawn_engine.h` | the ENGINE half: resolution, the ProcessEvent observations, the respawn, the sidecar. Included late in `proxy.cpp` (see its header for why). |
| `mod/native/test/test_respawn.cpp` | 95 assertions: the ladder, one death counted once, the settle window, restored stacks, rest point rules (NaN, range, menu map, save-vs-watched), the three bodies' round trip and refusals, the sequence gate including 255 ↁE0. |
| `proxy.cpp` | `#include "qdrespawn.h"`; a declaration block (`namespace resp`); `#include "qdrespawn_engine.h"` immediately before `HK_ProcessEvent`; `resp::RespawnVeto` before the forward and `resp::RespawnAfter` + `resp::RespawnObserve` after it; `resp::RespawnPump(now)` after `StatusPump`; the three `K_RESPAWN_*` cases in `DispatchInbound`; `resp::RespawnWorldReset` in `WorldTeardownReset`; the `A_DEAD` pose in `PuppetPump`. Every line is marked `[death-respawn]`. |
| `mod/native/src/qdwire.h` | `K_RESPAWN_DOWN/UP/REST` (0x54/0x55/0x56) and their place in `IsReliableKind`. |
| `mod/native/src/qdconfig.h`, `mod/qdcoop.ini` | `DeathRespawn` (default **0**). |
| `mod/rust/qd-core/src/protocol.rs` | the same three kinds, `RespawnDown/Up/Rest` bodies, `check_respawn_stack`, `respawn_penalty`, one new test. The relay forwards bodies opaquely, so only `Kind` was needed there  Ebut the crate's `decode_body` is an exhaustive match, so the bodies had to exist for it to compile. |
| `mod/tools/session30-death.ps1` | the live phase (`-Phase death`): a rest, two deaths, a rest, with the verdicts below. A copy of `session26.ps1`'s harness (one boot pair per phase) plus the death phase; it boots **without** the rig's health floor (`qd.god`), which is latched at the first tick and would otherwise refill the penalised health every second. |
| `mod/doc/WIRE.md` | §2 rows + §3.24. |

## Coverage

* **The death entry**: `FS_PlayerChar_C:Kill` (the game's own), plus a `Health <= 0` poll as the
  fallback. Any death reaches one of them: an enemy, a fall, a pit, a scripted kill.
* **The death screen**: observed as a dispatch of `FS_PlayerChar_C.ShowGameOverScreen` (refused)
  **and** disposed of afterwards by name (`DieUI_C`), because on this build the screen is not
  reached through that one function  Emeasured, and the reason both halves exist.
* **The rest point**: any `Checkpoint_C` the local player presses (`Interact`), with
  `HealToFull` logged as the game's own completion. Generic over the 34 placed `Checkpoint_C`
  across 18 maps.
* **The rest point's position**: where the player stood (a place the capsule occupied). A point
  in a menu map (`/UI/`, `/Menus/`, `VideoConfigMap`) is refused; so is one from another map.
* **Not covered**: a player killed by a scripted cutscene death (`FS_PlayerChar.FellInDeathPit`
  and the pit reset keep their own behaviour  Ethe lane still respawns the player at the rest
  point after the pose, which is consistent but not separately proven); the game's own
  `RestoreFromSave` path (never called here).

## Proof

Build: `mod/native/build.bat` clean (`Built ... winmm.dll`, 1 430 016 bytes). Tests:
`mod/native/test/run_tests.bat`  E**`test_respawn: 95 passed, 0 failed`**, and the only failing
suite in the tree is `test_interact.cpp` (`state property`, another lane's file, untouched by
this work). Rust: `cargo test -p qd-core` **142 passed**, `-p qd-relay` **17 passed**, release
relay rebuilt.

Live: `mod/tools/session30-death.ps1 -Phase death`, one boot pair per run, both machines alive,
0 FAULT lines, 0 crash dumps.

**Run 7 (23:16 E3:19, the clean one  Eno health floor):**

| what | line |
|---|---|
| the rest point, recorded from the press | `respawn: rest at //Game/RuinsLevel/Maps/700_SZ_GR/700_SZ_GR/PersistentLevel/Chec (Interact): 0 stack(s) cleared; max back to 100% at 11297,-2494,1120` |
| death 1 | `respawn: DEATH #1 (the game ran its own death entry (FS_PlayerChar_C.Kill)) -- max 90%; Health 100.0, the respawn target is 90.0` |
| respawn 1 at the rest point, penalised | `respawn: respawned at .../700_SZ_GR/... -- source=this player's last rest; Health -> 90.0 (base 100.0 x 0.90, 90%); moved=1 ...` |
| the partner sees it | `[1] respawn: partner went DOWN (#1, max 90%) at 11297,-2494,1041` then `[1] respawn: partner respawned at .../Chec (11297,-2494,1120) hp 90/90 (#1, max 90%) -- puppet teleported to the point, no lerp; this machine's world and enemies are untouched` |
| no world reload, either side | no `world: dropping every world-scoped handle` on either machine; both stayed `Ruins_Level_00`; earlier runs' end checks: kill sets diff 0, shared HP equal, world divergence 0 |
| the death screen | `respawn: a death screen was still up (DieUI_C [DieUI_C]) -- removed from the viewport` |
| the penalty holds while alive (20 s later) | `peer health now: 90` |
| death 2 | `respawn: DEATH #2 ... -- max 80%; Health 90.0, the respawn target is 80.0` |
| respawn 2 | `respawn: respawned at .../Chec -- source=this player's last rest; Health -> 80.0 (base 100.0 x 0.80, 80%)` |
| the rest restores 100 % | `respawn: rest at ...: 2 stack(s) cleared; max back to 100%` then `respawn: max HP back to 100 % -- Health 100.0 of 100.0 (written and read back)` |

Screenshots (`G:\Freespace\Queens_Domain\_test\s26-death\shots\`):
`s25-death-death1-back-inst1.png` (the host, world drawn, mean 76.6), `s25-death-death1-cap-inst2.png`
and `s25-death-death2-back-inst2.png` (the dying machine after the death screen is gone),
`s25-death-death-rest-inst2.png`.

**Run 1 (with the rig's health floor on, base 400)** is the second datapoint for the same rule:
`Health -> 360.0 (base 400.0 x 0.90)` then `Health -> 320.0 (base 400.0 x 0.80)`, the puppet
placed at `11297,-2494,1120` on the partner, and `2 stack(s) cleared` ↁE`Health 400.0 of 400.0`.
The penalty is a percentage of the base, so the number differs per area and the rule does not.

**Run 3** measured the game recomputing its own maximum and being scaled:
`respawn: the game computed max HP 100.0 -> 80.0 (80%, 2 death(s)) -- the multiplier is applied
to the game's own number`.

**Not clean in the table above**: run 7's harness table reports 2 FAILs for
`the restored HP is the penalised max (360 of 400)`  Ethe checks carried run 1's absolute
numbers (400 hp base) while run 7's area gives the player 100 hp, so 90 and 80 are the correct
penalised values. The checks now parse the base and the percentage out of the respawn line and
compare `hp` against `base x pct`, so a rerun of the same phase reports those two as PASS; the
raw log lines above are the measurement either way.

## Open risks

1. **The reload is only behind "Continue".** The Game Over *screen* is created by a path this
   lane does not dispatch (`DieUI_C`, measured), so the refusal of `ShowGameOverScreen` does not
   fire; the screen is removed instead (within ~250 ms of the death, and again after the
   respawn). The world reload happens when that screen's "Continue" is pressed, so the 3.5 s pose
   is the only window in which a player could start it. Removing the screen in that window is
   what closes it; a slower machine could still show it for a few frames.
2. **The game's own maximum is not writable on this build.** `CalculateMaxHP` is not in the
   player's function chain (measured: `CalculateMaxHP=NO`); it was found on the player's STATS
   COMPONENT, and scaling it works (`100.0 -> 80.0` above)  Ebut the number the component
   returns for a menu-map player is 100 while the real player holds 400, so the lane now refuses
   a computed maximum below 75 % of the health the player has been seen to hold. The penalty is
   therefore enforced twice: as a multiplier on the game's computation when it is trustworthy,
   and as a **cap on `Health`** every 200 ms for the rest.
3. **The rig cannot always reach a gameplay checkpoint** (runs 2, 3, 5, 6 landed in
   `VideoConfigMap`); the lane refuses such a rest point and falls back to the save's
   checkpoint, which is the behaviour the task asks for  Ebut it means the rest-point half of
   the proof needs a good placement, and only runs 1 and 7 had one.
4. **`input=0`** in every respawn line: `EnableAllPlayerInput`/`SetMovementMode` do not resolve
   by name on this class. The player is alive, placed and can act (the harness kept driving it),
   so the game's own death path evidently does not leave input locked on this build  Ebut the
   restore is a no-op and is reported as one.
5. **A fall after the respawn** (run 1: the body was 61 m from the point at the second death; the
   rig's own `FELL` guard is the known harness behaviour). The lane places the player at a point
   the capsule already occupied; if the level under it is not resident the game drops the pawn,
   which is the streaming union's problem, not this lane's.
6. **The sidecar persists only the stacks**  Ea relaunch falls back to the save's checkpoint for
   the location. That is deliberate (a remembered position from another session was used as
   "this player's last rest" in run 2, which is worse than no memory).

## What the coordinator must decide

* `DeathRespawn` ships **0**. The lane is proven in the hub with the documented PLAY set; turn
  it on for the shipped configuration only with (1) above accepted (a player can start the game's
  own reload in the first 3.5 s if they press Continue on a screen that is being removed).
* The reserved intent `16` is **unused** (see "Authority"): nothing on this lane needs the host's
  arbitration. Say if you want it spent on something (e.g. a third machine's view) rather than
  left reserved.

---

# Round 2 (2026-11-15) -- the reload behind Continue is closed; input restore is real

Round 1 left two gaps. Both are closed, and the switch is **1**.

## Gap 1: a player could start the world reload during the 3.5 s pose

**What was wrong.** The lane refused `FS_PlayerChar_C.ShowGameOverScreen` and then removed any
`DieUI_C` widget within ~250 ms. That is a race: the reload starts when the screen's **Continue**
is pressed, and the pose is 3.5 s long. Measured in Round 2 (first run): the `ShowGameOverScreen`
refusal **never fires on this build** -- the line is `respawn: a death screen was still up
(DieUI_C [DieUI_C]) -- removed from the viewport`, i.e. the screen exists without that dispatch --
so Round 1's only defence was the removal window.

**What I did instead, and why it is the robust half.** I dumped the cooked `DieUI` package rather
than guessing: its exports include `ContinueButton` (an `FS_Button_C`), and the widget's **only**
import of a world-load entry is `OpenLevel` -- via the bound handler
`BndEvt__DieUI_ContinueButton_K2Node_ComponentBoundEvent_4_CommonButtonBaseClicked__...`.
`proxy.cpp`'s own hard-travel note agrees independently: *"`OpenLevel` is imported only by menus,
death and cheats"*. So the rail is the **function**, not the widget:

* while this machine's player is **down** (`DeathState::Down()`), a dispatch of `OpenLevel` (or
  `RestoreFromSave` / `DeathLoad` / `LoadCheckpoint` / `ReturnToSavePoint` / `RestoreToSavePoint`)
  is **refused at the ProcessEvent boundary** and logged (`respawning is a position, and a load
  here would restart BOTH players' world`). Bypassing it does not need a widget: it does not
  matter whether the screen was dismissed, re-created, or never removed, and it does not matter
  whether the press came from the button, a key bound to the same handler, or the game's own
  follow-up.
* the widget-removal path is kept as the cosmetic half (a dead-looking screen over a live player),
  but it is no longer load-bearing.
* a **belt-and-braces** half is also in place: `WidgetBlueprintLibrary::Create` is refused when the
  requested `WidgetType` is a death screen while down. Measured in run 1 this did **not** fire
  (the log now names every widget created while down, so a build that does use the factory is
  visible) -- the reload rail is what actually holds.

**Proof (two presses DURING the pose, on the dying machine):**

```
18:59:50.469 INFO respawn: DEATH #1 (the game ran its own death entry (FS_PlayerChar_C.Kill)) -- max 90%; Health 100.0, the respawn target is 90.0; respawning in 3 s at this player's own last rest point
18:59:51.670 INFO respawn: BLOCK the world reload -- OpenLevel refused (receiver GameplayStatics) while this machine's player is down (1187 ms into the death): respawning is a position, and a load here would restart BOTH players' world
18:59:52.768 INFO respawn: BLOCK the world reload -- OpenLevel refused (receiver GameplayStatics) while this machine's player is down (2297 ms into the death): respawning is a position, and a load here would restart BOTH players' world
```

Both presses landed inside the pose (1187 ms and 2297 ms of a 3500 ms window) and both were
refused. The rig that presses it is `qd.dlg = death-continue#<nonce>`, which fires the death
screen's own Continue handler **and** the `OpenLevel` call it makes, so the run distinguishes
"the veto held" from "nothing ever asked": the rig logs
`screen=1 handler=1 reloadDispatch=1 reloadRan=1`. Neither machine logged a load or a teardown:
`the press did NOT reload the world (no load/teardown line)` and `the PARTNER's world was not
reloaded by the press` both PASS, and both machines stayed in `Ruins_Level_00`.

## Gap 2: `input=0` -- the restore resolved nothing

**What was wrong (measured from the cooked packages, not guessed).**
`EnableAllPlayerInput` / `DisableAllPlayerInput` are UFUNCTIONs of **`PlayerStatsAndUtils_C`**, a
`BlueprintFunctionLibrary` -- *not* of `FS_PlayerChar_C`. Round 1 resolved them with
`FindFunctionInClass(ObjClass(pawn), ...)`, which walks only the pawn's own class chain and never a
library. `SetIgnoreMoveInput`/`SetIgnoreLookInput` are `APlayerController` entries, and
`SetMovementMode` belongs to `UCharacterMovementComponent` (reached through the pawn's
`CharacterMovement` component). All four were resolved against the pawn, so all four missed, and
`input=0` was an honest report of a call that never happened.

**The fix** (`qdrespawn_engine.h`): each lever is resolved **where it is declared** and dispatched
on the object that owns it --
`Fns.libCdo` = `Default__PlayerStatsAndUtils_C` (the same CDO the boss-bar lane already uses on
this build), the controller for the ignore pair, the movement component for `SetMovementMode`. The
resolved surface is logged once:

```
18:59:34.342 INFO respawn: player class FS_PlayerChar_C: Kill=yes ShowGameOverScreen=yes CalculateMaxHP=yes EnableAllPlayerInput=yes (on PlayerStatsAndUtils_C) SetMovementMode=yes SetIgnoreMoveInput=yes; ... reload entries refused while down=2
```

and every respawn line now says which levers ran:

```
18:59:54.081 INFO respawn: respawned at //Game/RuinsLevel/Maps/700_SZ_GR/700_SZ_GR/PersistentLevel/Chec -- source=this player's last rest; Health -> 90.0 (base 100.0 x 0.90, 90%); moved=1 input=1 walking=1 ignoreInputCleared=1 hpWrite=1 KnockedDown->0
```

`input=1 walking=1 ignoreInputCleared=1` -- the restore is performed, not reported.

## Two false positives removed (measured, run 1)

`DismissDeathScreens` matched by class name, so it "removed" `Default__DieUI_C` -- the **class
default object**, which is never on a viewport. It now refuses any `Default__` object and requires
the engine's own `IsInViewport` to say the widget is on screen; a CDO it sees is reported at DEBUG
(`a CDO is never on a viewport`) instead of being acted on.

## Full re-proof (the clean run, `session30-death.ps1 -Phase death`)

**42 PASS / 0 FAIL.** One boot pair, both machines alive at the end, 0 FAULT lines, 0 crash
dumps, kill sets diff 0, shared HP equal, world divergence 0.

| what | line |
|---|---|
| the rest point | `22:26:31.930 respawn: rest at //Game/RuinsLevel/Maps/700_SZ_GR/.../Chec (Interact): 0 stack(s) cleared; max back to 100% at 11297,-2494,1120` |
| death 1 | `22:26:33.083 respawn: DEATH #1 ... -- max 90%; Health 100.0, the respawn target is 90.0` |
| **Continue during the pose, refused** | `22:26:35.757 respawn: BLOCK the world reload -- OpenLevel refused (receiver GameplayStatics) while this machine's player is down (2672 ms into the death)` |
| respawn 1, penalised, input restored | `22:26:36.819 respawn: respawned at .../Chec -- source=this player's last rest; Health -> 90.0 (base 100.0 x 0.90, 90%); moved=1 **input=1 walking=1 ignoreInputCleared=1** hpWrite=1 KnockedDown->0` |
| the partner sees it | `22:26:33.793 [1] respawn: partner went DOWN (#1, max 90%) at 11297,-2494,1120` then `22:26:36.884 [1] respawn: partner respawned at .../Chec (11297,-2494,1120) hp 90/90 (#1, max 90%) -- puppet teleported to the point, no lerp` |
| no reload, either side | no teardown line on either machine; both stayed `Ruins_Level_00` |
| death 2 | `22:27:28.276 respawn: DEATH #2 ... -- max 80%; Health 90.0, the respawn target is 80.0` |
| respawn 2 | `Health -> 80.0 (base 100.0 x 0.80, 80%)`, `input=1 walking=1` |
| the rest restores 100 % | `22:27:47.368 respawn: rest at .../740_SZ_Prison/... 2 stack(s) cleared; max back to 100%` then `22:27:47.924 respawn: max HP back to 100 % -- Health 100.0 of 100.0 (written and read back)` |

Screenshots: `G:\Freespace\Queens_Domain\_test\s26-death\shots\` (`s25-death-death1-down-inst1.png`,
`s25-death-death1-cap-inst2.png`, `s25-death-death2-back-inst2.png`,
`s25-death-death-rest-inst2.png`).

## Two harness bugs found and fixed while getting this proof (they mattered)

Neither was in the lane, and both made earlier runs measure nothing:

1. **The press landed outside the pose.** The step slept between writing the lever and pressing,
   so presses arrived ~22 s after the death -- past the 3.5 s window, correctly unrefused, and the
   `OpenLevel` it drove dropped that instance to the title screen. The step now writes the lever
   immediately after the DEATH line, presses twice 1.2 s apart, and then waits for the respawn line.
2. **A stale `qd.dlg` fired at boot.** The death phase did not include `qd.dlg` in the control
   files it clears, so a value left by an earlier run executed on the FIRST tick of the next boot --
   before any death existed, so the veto rightly did not apply -- and reloaded that instance. It is
   now cleared with the other levers (`session30-death.ps1`, the `foreach ($leaf ...)` list).
3. **The lever pressed QUIT instead of Continue.** A bare `BndEvt__` prefix match picked whichever
   bound handler the class chain yields first, and `DieUI` also binds `QuitTomenuButton` and
   `QuitToDesktopButton`. It now matches `BndEvt__DieUI_ContinueButton` in full (`proxy.cpp`,
   `PressDeathContinue`), and the fallback list deliberately excludes `QuitGame`.

A fourth, in the lane itself, is in "Two false positives" above: the dismissal path acted on
`Default__DieUI_C`, the class default object.

## Switch

`DeathRespawn=1` in `mod/qdcoop.ini` (built-in default stays 0, so a missing ini cannot arm it).
The Round 1 blocker ("a player can start the game's own reload in the first 3.5 s") is **closed by
measurement**: a Continue press during the pose is refused at the function that button calls.
