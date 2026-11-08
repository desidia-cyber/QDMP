# Live test log

Running record of what the live game says about this mod. Each entry is a measured
result with the evidence that produced it, because the corpus rule is that the
document is the work's durable memory and a closing summary is only a *claim*.

Environment: game at `QueensDomain\QueensDomain\Binaries\Win64\`, image
`79CCDC6C…D1E6`, instances launched off-screen via `tools/run-hidden.ps1` with
`-UserDir=_test\inst1 -saveddirsuffix=qd1 -SaveToUserDir`. The operator's real save
under `%LOCALAPPDATA%` was never touched.

---

## Session 1 — 2026-11-07

### R1. The mod loads. `winmm.dll` is the right proxy.

`qdcoop.log` appears on the first launch; the bootstrap thread runs and reports the
image base. Confirms the import-table finding: the exe imports `winmm.dll`, so the
proxy is mapped before the entry point.

```
18:47:14.691 INFO qdcMp bootstrap: pid=12204 base=7FF755470000 image=79CCDC6…
```

### R2. BUG (fixed) — a relocated pointer can never match its file bytes

```
targets: 34 verified, 6 runtime-validated, 1 mismatched
ERR  REFUSING TO RUN: build mismatch in ProcessEvent vtable slot @ 0376D258 byte 2
```

The guard fired on a **correct** build. `0x0376D258` is a `.rdata` slot holding the
`ProcessEvent` VA; the loader rewrites it at every base relocation, so in-memory bytes
differ from the file by design.

**Fix.** `gen_targets.py` now classifies a slot whose first 8 bytes are an in-image VA
as `check=2` and records the *target RVA*; `VerifyTargets()` compares
`live == ImageBase() + ptr_rva`. Regenerated: 33 byte-verified, 1 relocated-pointer,
6 runtime-validated.

```
18:51:52.647 INFO targets: 33 byte-verified, 1 relocated-pointer, 6 runtime-validated, 0 mismatched
```

> A guard that fires on a good build gets ignored. This one now checks the right thing.

### R3. BUG (fixed) — the log had no newlines

Every line ran into the next:
`…D1E618:47:14.691 INFO …`. `LogBuild` NUL-terminates but never emitted `'\n'`.

### R4. BUG (fixed) — the probe never retried

`SaveInit` ran once at bootstrap and the code logged "will retry on the next load"
with nothing to do the retrying. A fresh launch sits at the main menu with **no save
object at all**, so the probe would never have run.

**Fix.** `TryInitSaveLayer()` runs from the frame pump: retries every 2 s until the save
layer is up, runs the probe once, and re-initialises (resetting the mirror) when the
save object dies or is replaced by a level load.

### R5. BUG (fixed) — `FName` block table read one indirection too deep

`FNameEntryAllocator::Blocks` is an **array of pointers at `pool+0x10`**, not a pointer
to one. The code did `*(uint8_t**)(pool+0x10)`, which returns `Blocks[0]` — a block —
and then indexed that block's bytes as if they were block pointers. For a low index
(`idx >> 16 == 0`) it lands back on `Blocks[0]` and looks correct; the first index in a
higher block dereferences heap garbage.

Evidence: the process reached `step: RuntimeValidate` and died there with exit code 3,
before any further log line.

**Fix.** `FNameBlocks()` returns the array address and indexes it directly, with a
bound (`blk < 8192`) and a null check.

### R6. BUG (fixed) — hand-typed length in the FName acceptance test

After R5 the decoder resolved the name correctly and the check still failed:

```
substrate: globals=1 fname=0(GameEngine) gobjects=1 objects=62630
ERR  REFUSING TO RUN: the FName decoder did not reproduce 'GameEngine'
```

The assertion required `n == 9`; `"GameEngine"` is ten characters. The decoder was
right and the constant was wrong — the corpus's "right number, wrong place" trap.
The string comparison is now the assertion; the length only has to be positive.

```
18:56:59.395 INFO substrate: globals=1 fname=1(GameEngine) gobjects=1 objects=62130
```

**Substrate proven live:** `GWorld`/`GEngine` non-null, FName decoding correct,
GObjects liveness check passes on a 62 130-object graph.

### R7. BUG (fixed) — `recvfrom` called with a zero `fromLen`

```
WARN recvfrom err=10014   × ~500 per second
```

`10014` is `WSAEFAULT`: `fromLen` is IN/OUT and was never reset. The tight retry also
logged every failure, producing **825 KB in under a second**.

**Fix.** `fromLen_ = sizeof(from_)` before every call; errors log the first occurrence
and then at most 1/s; a real error backs off 50 ms, a would-block 2 ms.

### R8. OPEN — the process exits ~60 s after launch

Both instrumented runs ended ~60 s in, with the log stopping mid-stream and no crash
dump in `Saved/Crashes`. In R7's run the last activity was the `recvfrom` spin, so the
log flood is the leading suspect; that is now fixed but unverified. Not yet
distinguished from an unrelated game-side exit.

**Next probe:** re-run with R7's fix in place. If it still exits, launch with the mod
installed but `Enabled=0` (loads, verifies, does nothing) as the control — that
isolates mod-vs-game in one run.

---

## State at pause

- Game directory: **clean** (mod uninstalled, `qdcoop.log` removed). The game is
  unmodified and runnable.
- Offline suite: 209 native assertions + 146 Rust tests still pass after these fixes.
- `mod/tools/run-hidden.ps1` added: launches windowed, parks the window off-screen with
  `SetWindowPos`, and never takes the desktop. **All live runs must use it** — earlier
  runs came up in the foreground on the operator's screen.
- `_test/` holds the seeded instance and is gitignored.

## Resume checklist

1. `cd mod && python tools/gen_targets.py && native\build.bat` (already regenerated).
2. `.\tools\install.ps1` — reinstalls `winmm.dll` + `qdcoop.ini`; set `LogLevel=3`.
3. `.\tools\run-hidden.ps1 -UserDir G:\Freespace\Queens_Domain\_test\inst1 -Suffix qd1`
4. Watch for `======== API probe ========` in `qdcoop.log`. That block is the whole
   point of the next run: it answers the four write-side APIs (property layout, map
   stride, writer round-trip) that the analysis could not pin statically.

---

## Session 2 — 2026-11-07 (continued)

### R9. BUG (fixed) — the hook installer never installed anything

`gen_targets.py` emitted target names padded to 46 columns (`"%-46s"`). Every lookup was
an exact match, so `"FEngineLoop::Tick"` never matched `"FEngineLoop::Tick" + 32 spaces`,
and **no hook was ever installed** — while `InstallHook` logged nothing, so the mod
looked like it was running hooks and simply doing nothing. This is the corpus's "a probe
that runs fine and writes nothing has usually failed silently" in its purest form.

Fixed: names are emitted unpadded, the installer logs each hook, and a missing target is
`LOG_ERROR` rather than silence.

```
INFO hooked FEngineLoop::Tick @ 007753A0
INFO hooked UGameplayStatics::SetGamePaused @ 028F3180
INFO hooked StoreEnemySaveState @ 00E3BF10
INFO hooks installed: 3 of 3 (ProcessEvent is opt-in)
```

### R10. BUG (fixed) — `0x011A0050` is `FindFunctionChecked`, and it is fatal

Calling it with a name the class does not have aborts the game with a dialog:

```
LowLevelFatalError [File:Unknown] [Line: 1334]
Failed to find function GetNamedSaveData in FS_SaveSlot_C
```

Two conclusions, and the second is the important one:

1. It is an assertion, not a query. The mod no longer calls it.
2. **`GetNamedSaveData` is not a function of `FS_SaveSlot_C` on this build.** The analysis
   inferred that it was (SaveFunctions lists it among its exports, and the native
   `0x00E2F6D0` path was assumed to run on the save object). The live engine disagrees,
   and the class reports **0 UFUNCTIONs**:
   `FS_SaveSlot_C has 0 UFUNCTIONs`.

### R11. BUG (fixed) — UObject work on a worker thread blocked

The bootstrap thread ran the substrate check, the save-layer init and the probe. It
**blocked inside `SaveInit`** — the run that reached it stopped logging while the game
kept running. That is the rule this project wrote down and then broke: *only UObject
access goes on the game thread*. All of it now runs from the `Tick` hook.

### R12. MEASURED — the object array is sparse and holds `UObject*`, not `FUObjectItem*`

First eight slots, printed by the mod: `slot[0]` and `slot[1]` are live heap pointers,
`slot[2]`..`slot[7]` are **zero**, and the deltas are not a constant stride. So the array
holds object pointers and contains nulls.

Consequence: the `gobjects_ok` liveness check in `RuntimeValidate` **passed by
coincidence** — for a null-ish slot the index read back as 0 and the slot compared equal
to itself. It is not evidence and must not be reported as such.

### R13. BUG (fixed) — walking GObjects to find a UFunction terminates the game

Cleanly: no Application Error record, no crash dump, no dialog — the process is simply
gone. Wrapping each slot probe in `__try/__except` did not help, so this is not a memory
violation a guard can contain.

The scan is now **opt-in and off by default** (`[Rules] ObjectScan=0`). Nothing in the
analysis needs it: reads go through the native getter at `0x00E2F6D0`, which takes a
world context and a key and needs no `UFunction` at all.

### R14. CORRECTION — "the game exits after 30-60 s" was an artefact

Every earlier run appeared to die 30-60 s after launch. It does not. The game stayed
alive for a **70-second poll inside a single invocation** and was gone immediately after
that invocation ended: the process tree is torn down when the tool call finishes.

So the exit was environmental, not a game bug — and it sent one round of debugging at the
wrong target. The `FEngineLoop::Tick` return-type fix made in response is still correct
in its own right (a detour that returns `void` for a `bool` function hands the engine a
garbage result and must not be relied on), but it was **not** what was killing the game.

**Consequence for the rig:** a session must be one long-running process that launches,
drives and samples. `tools/live-session.ps1` does exactly that, and `tools/run-hidden.ps1`
keeps the window off the desktop.

### R15. First real read from the live game

With the pump running and the scan disabled, the mirror reads the save through the native
getter:

```
INFO flag CompletedChallenges = 0
INFO flag probe: 1/13 known keys present
```

One key present is correct at that point: the instance is at the main menu, where the
save object exists but holds no player progression. The eleven keys the analysis measured
appear once a save is actually loaded.

### R16. Infra fixed by running it

- `install.ps1` used `$PSScriptRoot` in a parameter default, where it is empty:
  "Cannot bind argument to parameter 'Path' because it is an empty string".
- `shot.ps1` captures the SCREEN, which cannot work for an off-screen window, and defaults
  to stealing focus. Replaced by `tools/capture-hidden.ps1` (`PrintWindow` with
  `PW_RENDERFULLCONTENT`), which renders the window wherever it sits.
- The process tree teardown above, which is why `tools/live-session.ps1` exists.

### R17. The menu is reachable by a posted click

`tools/send-input.ps1` posts `WM_MOUSEMOVE` then `WM_LBUTTONDOWN/UP` at a **fractional**
client coordinate. Measured: clicking at (0.5, 0.744) of the client leaves the main menu
and the window goes black (the level-load transition) — captured in
`_test/shot-after-click.png`. Absolute pixels are not usable: the same launch arguments
produced a 1920x1080 client once and 1280x720 the next time, so a fixed (960,803) missed
the button entirely on the second run.

### R18. The save object must be re-resolved, not cached

The main menu already hands back a transient `FS_SaveSlot_C`; the save a level load uses
is a different object. Caching the first one made the mirror report the main-menu state
(`1/13 known keys present`) for two minutes after the world had loaded. It now re-resolves
every 2 s, and the re-init is visible in the log (`TryInitSaveLayer: calling SaveInit`
recurring).

### R19. OPEN — gameplay was not reached within the session

Clicking Play reliably leaves the menu, but no run got far enough for the loaded save to
answer with the eleven keys the analysis measured: the mirror still reported
`1/13` at the end of every attempt, and one launch exited before the probe at all.

Two candidates, not yet distinguished:

1. **Play opens a save-select screen.** The screenshot after the click is a black
   loading frame, which is consistent with a load, but a second menu may follow.
2. **The instance's save is not being loaded.** `_test\inst1` was seeded by copying
   `World0.sav` etc.; if the game's slot bookkeeping lives in `System0.sav` and the
   instance was seeded inconsistently, "Play" would start a new game.

**Next probe:** capture the screen 20 s after the click and look at it, then click the
obvious next control. That is one run, and it needs a screenshot rather than a guess.

### Where this leaves the mod

Working and measured live: build verification, the substrate, all three hooks, the frame
pump on the game thread, save-object resolution, and **reads through the native getter**
returning real values from the running game.

Not working yet: the progression mirror at full fidelity (needs either map enumeration or
a UFunction, both blocked — see R11/R13), the write path (same blocker), and everything
downstream of a live session (player rows, puppet, enemy sync).

`ObjectScan` is off by default and the write paths refuse rather than guess, so the mod
currently does nothing to the game's state. The game directory is left clean.

---

## Session 3 — 2026-11-08

### R20. FIXED — both clients stayed roleless
The relay never sent `WELCOME`. It now answers every `HELLO` with the assigned role
and session id (`qd-relay/src/lib.rs`, test `every_hello_is_answered_with_the_assigned_role`).

### R21. FIXED — the save layer read garbage / created saves
GObjects is a chunked array (65536 per chunk); flat indexing read past chunk 0.
`GetActiveBaseSave` CREATES a save when none is active; the active save is now read
side-effect-free via GWorld+0x180 → GameInstance+0x210. Measured: map enumeration
11/11 keys agree with the native getter; writer no-op round-trip PASS.

### R22. Gameplay reached without the keyboard (dev only)
Keyboard navigation of File Select was unreliable and once started a NEW GAME on an
empty slot. `QDCOOP_AUTOLOAD=<slot>` (environment, test rig only) calls the title
widget's own `Load Slot(Slot, Backup, BackupSlot)` after 10 s, then `ConfirmLoadSuspend`
if the resume modal opens. It refuses unless `-UserDir` is given AND
`<UserDir>\Saved_<suffix>\SaveGames\World<slot>.sav` exists — it cannot touch a real
save or load an empty slot. Measured: title → in game in ~10 s, both instances.

### R23. FIXED — the remote body was "two blue fragments"
`FS_Full_PlayerChar_C` is the first-person character: it renders arms only. The body
is now `qdavatar.h`: an engine `SkeletalMeshActor` carrying the game's own full-body
model `Player_FullChar_2026` (cutscene asset, loaded with `LoadAsset_Blocking`) on a
`PoseableMeshComponent`. Measured rest pose: Y forward, X = model's left, feet z=0,
170 cm, A-pose; Auto-Rig Pro skeleton (110 bones, `Weapon_R` parented to Root,
`Weapon_L` to `Hand_L`).

### R24. Walk animation is procedural
The skeleton has no locomotion animations (only cutscene shots), so hips/knees/feet,
arms, spine and a footfall bob are posed by component-space FK from the measured
ground speed (`avatar::Animate`). Read-back: `Hand_L` target == actual on every sample.
Verified on screenshots: idle, stride, run.

### R25. Held weapon sync (new wire kind `EQUIPMENT` 0x15, WIRE.md §3.13)
Held items are visible StaticMeshComponents of `WeaponVisuals_C` actors attached to the
pawn (measured: `RustySword_WeaponVisuals_C`, mesh `/Game/Items/Weapons/RustedSword/
RustySword.RustySword`). Sent on change + 1 Hz; receiver attaches the mesh to the body's
`Weapon_R` bone, which the FK carries with `Hand_R`. Measured: peer's sword shown in hand.
**Not yet measured: a weapon CHANGE** (test save owns one weapon; slot 2 is empty).

### R26. FIXED — the body was destroyed and respawned every 1–5 s
`now - LastRxMs()` was unsigned; the net thread stamps receives after the tick reads
`now`, so the difference wrapped to ~4e9 and read as "peer silent". Signed now.
Measured: one spawn per side, stable, `err=0.0cm`.

### R27. Heading is the camera's
The pawn root does not turn with the view. Heading sent = `PlayerCameraManager::
GetCameraRotation` yaw.

### R28. The runner leaned BACKWARD (review: "awkward and unnatural")
A +X rotation carries the spine's up axis toward -Y, i.e. backward; the legs use +X for
"forward" only because they hang down. The torso lean is now negative (2-9 deg forward
with speed). Verified on the mirror body: upright at idle.

### R29. FIXED -- every reflected lookup on the player pawn silently missed
`FindPropertyOffsetIn` collected all properties first and FAILED when a class had more
than 160; `FS_Full_PlayerChar_C` has more. It now streams the field chain. This also hid
the pawn's property dump.

### R30. Weapon change -- measured live, two instances
Test weapons are granted through the game's own cheat object (`QDCOOP_GIVE`,
`FS_Cheats_C.GiveItem` + `FS_PlayerChar_C.AutoEquipItem`). Instance 2 swapped sword ->
axe with key 1; instance 1 logged `peer equipment rev 2 ... Axe_00_SM` in the same
millisecond and the body holds the axe (`_test/s-inst1-e2.png`).

### R31. Attacks -- PLAYER.action now carries kind + counter (WIRE.md §3.5)
Discovered with a ProcessEvent trace (`QDCOOP_PE_TRACE`) and the arms' active montage.
The body plays a procedural swing (wind-up, strike, recover; 0.9 s, strike at
0.2-0.52 s to match the measured hit window), alternating right-to-left and
high-right-to-low-left as the game does. Two instances: `peer action kind 1 #5`,
`#6`...; frames from instance 1 show wind-up, strike across the chest, recovery
(`_test/remote-attack.png`).

### R32. Jump -- PLAYER.flags bit0 from MovementMode
Airborne pose (asymmetric knee tuck, arms up) driven by the sender's falling state.
Two instances: body leaves the ground with legs tucked (`_test/remote-jump.png`).

### R33. Magic / thrown weapons -- new kinds EFFECT 0x16 / EFFECT_MOVE 0x17 (§3.14)
This save cannot throw yet (no throw slot), so `QDCOOP_TEST_FIRE` throws a Fireball
through the player's own `FireDagger(Class)` every 7 s. Measured: local projectile seen,
copy shown on the other instance with its fire VFX; with moves, the copy is within 5 cm
horizontally of the real one at +1.2 s (mirror read-back).

### R34. FIXED -- reliable messages were never acked
Nothing sent `ACK`, so reliable bodies were resent and applied repeatedly (one Fireball
-> six copies). The receiver now acks and deduplicates (WIRE.md §3.14 end). After: one
spawn per throw.

### R35. Rig: two instances for anything shown ON a player
Mirror-mode tests applied the cheats to the viewing player, so its own first-person
overlays (screen tint, flames) mixed into the picture (review). Status, arc and strafe
were then re-tested with instance 2 acting and instance 1 only watching. Also: Git Bash
rewrites `/Game/...` environment values into Windows paths; launch with
`MSYS2_ENV_CONV_EXCL=QDCOOP_`.

### R36. Status effects -- new kind STATUS 0x18 (WIRE.md §3.15)
`FS_Cheats_C.MagicFireMe(Amount)` / `MagicPoisonMe(Amount)` and
`StatusEffectComponent_C.Apply()` drive them (`QDCOOP_TEST_STATUS`, `QDCOOP_TEST_APPLY`).
Two instances: instance 2 burning -> instance 1 `peer status mask 00000001` and the
body burns in blue on arms, torso and legs, out when the fire ends (`_test/n-fire.png`);
poison -> purple wisps (`_test/n-poison.png`); disease + weaken arrive (mask 0x18) but
show only the faint enemy effect at the feet (`_test/n-disease.png`). The enemy CharFX
cannot sample the poseable body (flames at the feet only, also with a master-pose
skeletal copy), so looks were picked from mesh-free systems on screenshots
(`_test/v-gallery*.png`). Magic poison is lethal within ~7 s even at 25, and fire 150
killed the test player in 1.5 s: the test save went through game over twice.

### R37. Melee swing arc -- EFFECT kind 4 (§3.14)
The arc is `MeleeArc_Default`, a Niagara child of the first-person `Arms`, spawned
~0.2 s into a swing, with user parameters Color1, Color2, Lifetime, Power, Charge.
Two instances: instance 2 `arc: local ... User.Power=350 ...`, instance 1 `arc: shown
... params set 5` 8 ms later; a white arc cuts across the body's chest at the strike
(`_test/n-arc.png`). The watch window follows the montage: on a throttled instance one
swing took 2.6 s of wall time and a fixed 0.9 s window missed it.

### R38. Strafe and backpedal
The legs turn toward the travel direction (up to 60 deg) while the chest keeps facing
the view; moving backward steps backward. Two instances, instance 2 holding D:
sidestep with turned hips (`_test/n-strafe.png`).

### R39. Skeletal-mesh weapons
Sender scans `SkeletalMeshComponent`s of weapon visuals (mesh_kind 2); receiver has a
skeletal slot per hand. This save cannot equip one (`EquipItem(...,2001)`,
`WeaponSystem.SetSelectedWeaponByItem` and `EquipSpecifiedWeapon` all leave the held
weapon unchanged; `AllWeapons` is empty), so only the receiving side is verified:
`QDCOOP_TEST_HOLD=<mesh>#2` puts `B33Sword_SK` in the mirror body's hand
(`_test/v-arc2.png`).

### R40. FIXED -- death read as "use item"
`Player_FPArms_Death_Montage` matched "eat" -> A_USE. Death is now A_DYING and the body
falls onto its back and stays down until the sender respawns (not yet seen on screen:
in this rig a death means game over on the test save).

### Open
- Throwing from real input (no throw slot on this save) and the sending side of a
  skeletal weapon (cannot be equipped here) remain unverified live.
- Disease / weaken / heavy / binding show only the enemy effect at the feet; buffs have
  no world visual in the game.
- Charged attack, thrust, use-item and cast montages have not been triggered live; the
  death pose has not been seen on screen.
