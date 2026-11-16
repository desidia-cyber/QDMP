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

---

## Session 4 — 2026-11-08 (the four remaining gaps)

Two instances, `_test\inst1`/`inst2`, `QDCOOP_AUTOLOAD=0`, all switches on, relay rebuilt
from this branch. Both reached gameplay in ~10 s and stayed for minutes. Six defects were
found and fixed; every one of them was invisible off-target.

### R41. FIXED — reliable kinds added later were never acked
`Ingest` acked exactly `K_INTENT` and `K_EFFECT`. `ENEMY_DETAIL`, `TRAVEL` and `DAMAGE`
were sent reliably, retransmitted, never acked and abandoned: measured as
`reliable send abandoned: kind=00000023` every 2 s for the life of the session, and
`no free reliable slot; dropping kind=00000019` under enemy load. The ack and dedup
branch is now driven by `wire::IsReliableKind`, one list shared with the Rust
`Kind::is_reliable`.

### R42. FIXED — the peer uploaded its whole save on join
The mirror's first pass sees every key as NEW, and `QueueFlagIntent` fired on each one.
Measured as `intent refused (regressive or stale): CompletedChallenges=2` repeatedly, and
— when the peer's save is ahead on any key — it would have silently overwritten the
host's progression. The first pass is now a baseline (`g_mirrorBaseline`), reset with the
mirror when the save is replaced.

### R43. FIXED — the enemy detail lane exhausted the reliable table
At level load every enemy in range is already playing something, so the first
observation went out as a change: 78 enemies -> 78 reliable bodies in one frame -> 16
slots full -> everything else dropped. Detail now baselines per enemy, sends only when
something is playable (`anim != none`) or the status mask changed, is bounded to 10/s,
and reports a real `SendReliable` failure instead of logging "sent".

### R44. FIXED — the game's "no record" was read as a death
`StoreEnemySaveState` fires at load for every spawner with `hp = -1` (no entity), which
`hp <= 0` treated as a kill: 123 death records in one frame, forwarded to the host as
persistence records. A negative hp now returns early, before the kill log and the send.

### R45. FIXED — two players standing together double-simulated every enemy
Ownership resolves by distance with an 800 cm margin, and inside the margin the rule was
"both keep running". That margin is wider than the distance between two players playing
together, so it was the *normal* case: measured as zero hand-offs with 66-160 enemies
alive, i.e. every enemy simulated twice with two AI outcomes and two health pools. The
tie is now broken by role — the host owns, the peer freezes — and a frozen host can take
an enemy back rather than deadlock against a peer handing the same enemy over. Verified:
the peer froze 64 copies and followed them.

### R46. FIXED — the peer rewrote unchanged actor states every second
The 1 Hz `ACTOR_STATES` heartbeat was applied entry by entry, each through the game's own
`SavableActor` setter: `actor states applied=7` every second, indefinitely. The apply path
now skips an entry whose value it already holds (`LocalDetector::Known`), so the heartbeat
costs a comparison and the log line appears only when something changed.

### Working live in this session
- Load: substrate, four hooks including `SetShouldBeLoaded`, roles, save layer, and the
  reflected `SavableActorStates` offset (704) with no geometry refusal.
- Two players in one world, puppet at **0.0 cm** error on both machines, held weapon
  mirrored.
- `ACTOR_STATES`: host publishes 7 entries; peer applies once, then holds.
- `INVENTORY`: host publishes (124 entries on one instance); peer applies.
- Enemies: scan (66-161 live), ownership hand-off, freeze through `Paused` (`ok=1`), rows
  flowing both ways.
- `TRAVEL`: both machines announce `TempTeleporter/Ruins_Level_00` at load; the receiver
  reported "busy" rather than fighting the local transition.
- `FLAGS`: 0 refused intents after R42; 0 abandoned sends after R41; 0 dropped slots.

### Still not exercised live
Nothing induced a *change* on the new lanes: no door/chest interaction, no pickup, no
enemy attack (therefore no `DAMAGE`), no travel follow. Those need gameplay input this rig
cannot drive reliably. `SYNC-GAPS` §5 lists the exact log lines to look for when a player
does them.


---

## Session 5 ? 2026-11-08 (making the rig able to drive a change)

The previous session proved the lanes converge but never carried a *change*: no door, no
pickup, no enemy attack. This session added the missing levers and found two crashes.

### R47. The rig could not drive gameplay at all, and why
`PostMessage` keys reach the UI ? the title and file-select screens respond ? but **not
gameplay**: pressing E or W in the world did nothing, measured twice. The engine reads
device state for in-world input, not window messages. So the rig now drives the verbs the
keys are bound to, through the game's own functions:
- `QDCOOP_TEST_TPFILE=<path>` ? teleport the local pawn to `x,y,z,yaw` whenever the file
  changes (`mod/tools/rig.ps1 -Action tp`), so a scenario can move a player mid-session.
- `QDCOOP_TEST_SCAN=1` ? log the local position, the nearest enemy, the five nearest
  one-shot world objects by name, and the game's own `Current Interact Actor`.
- `QDCOOP_TEST_ACTFILE=<path>` ? perform the player's `Interact` verb once per file change
  (`-Action key` remains available for menus).

### R48. FIXED ? the scan probe crashed the peer during a world reload
Game over -> Continue destroys and recreates the pawn; the probe read an actor name in
that window and faulted (`WINMM+0x18c36`, inside `DevScanNearest`). The probe bodies are
now `__try`-guarded (the file-scope statics are separated so MSVC allows it, C2712), and
the log says `scan: fault while reading actors (world reload?)`.

### R49. FIXED ? the streaming veto crashed the game on a world reload
The second crash had `WINMM+0x27647` in the stack: that is `LevelManager()`, called from
`HK_SetShouldBeVisible`, reading `GameInstance` while the world was being torn down
(`EXCEPTION_ACCESS_VIOLATION reading 0x0000000100000000`). This is a **mod** bug, not a
probe: any player who dies with `StreamUnion=1` could hit it. `LevelManager` is guarded now.

### What the levers showed
The save's area contains essentially no interactables near the start: the five nearest
one-shot objects are all coin pickups, 12-17 m away, and the only enemy is 28 m away. Two
blind teleports to distant savables killed the peer (a brazier, then a fall). Driving a
change therefore needs a *safe* target list, not just coordinates.

### Still unproven
The four change paths: door/chest, pickup, enemy attack (`DAMAGE`), travel follow. The
plumbing all runs (state converged, zero dropped slots, zero refused intents), but no
scenario has yet produced the *event*.

---

## Session 6 - 2026-11-08 (driving a change, and what the game says about it)

Two instances, `_test\inst1` / `_test\inst2`, relay rebuilt from this branch, every write
path on. Four sessions were run with `tools/scenario.ps1` (18:30, 18:51, 19:18, 19:37);
the last one reached every step, and its lines are the ones quoted here. The instance
saves are mutated by each run, so a counter like `CheckpointUnlockCount` starts from
whatever the previous run left.

`scenario.ps1` is the whole session in ONE process -- relay, both instances, the driving,
the collection, the teardown -- because a launched game does not survive the command that
started it (R14). It reads the roles from `session established` and never assumes which
machine is the host.

### R50. The rig can now drive a change: five levers, and three things that were wrong

`qd.tp` (already there), plus `qd.act`, `qd.scan`, `qd.god`, `qd.hit` and `qd.cheat`, all
in the instance's `-UserDir` and all gated on `-UserDir` being present, so none of them is
reachable in normal play. Three of those needed a correction before they did anything:

* **`FS_Cheats_C` has no `GodModeOn` and no `HealToFull` on this build.** The class's own
  function table (dumped once by the mod) has 72 UFUNCTIONs and the god switch is
  `DeadGod`. Measured:

  ```
  god: GodModeOn=no HealToFull=no DeadGod=ok (health floor=400, hp=0.0)
  god: health refilled 100.0 -> 400.0 (refills=1)
  ```

  `DeadGod` alone was not enough to build a scenario on, so the rig also pins the player's
  `Health` FloatProperty at a floor. The player is still *hurtable* -- a hit lands and is
  logged -- it just cannot reach 0. This is what finally made the test player unkillable;
  two blind teleports had ended earlier sessions.

* **The interaction component does not interact.** `PlayerInteractionComponent_C` has
  exactly five UFUNCTIONs -- `Update`, `ReceiveBeginPlay`, `ReceiveTick`,
  `ExecuteUbergraph_PlayerInteractionComponent`, `InteractTargetChanged__DelegateSignature`
  -- and none of them is `Interact`. It only *tracks* the actor
  (`Current Interact Actor`); the verb is on the target (`Checkpoint_C.Interact`). The act
  lever therefore names an actor (`qd.act` = `near:checkpoint`), resolves it, and calls
  its own verb, press then hold. Measured:

  ```
  dev interact: resolved 'checkpoint' -> Checkpoint17 at 3871,-1723,51 dist=4019
  dev interact: target=Checkpoint17 [Checkpoint_C] component=found verb=Interact
  ```

  The same lever starts a conversation when the name is an NPC, which is what §5.5 needed.

* **`Current Interact Actor` is a property of the component, not of the pawn.** The probe
  of Session 5 read it from the pawn and reported `-` on every pass; it now reads the
  component and says whether the component was found at all.

Two further corrections to the *teleport* lever, both the same mistake Session 5 made:

* `near:<name>` resolves a live actor and uses **its** height, instead of a `z` copied out
  of a log. `near:enemy` also means "the nearest enemy" rather than "an actor whose name
  contains 'enemy'", which matched nothing.
* The resolver refuses bookkeeping classes. A `SkullDoorQuestStateManager` matched "door"
  and put the peer 423 m away and 33 km below the world -- the same class of blind
  teleport that killed the peer twice in Session 5.

### R51. FIXED - the write-path oracle refused a correct build, and had never run

Session 5 produced no `NAMED-SAVE ORACLE` line at all, because the oracle only ran when
the first flag write asked for it and nothing wrote. Running it at load made it fail
loudly, on both instances:

```
ERR  NAMED-SAVE ORACLE FAIL: GetNamedSaveData frame = {FName@0 size 8, object@8 size 8, i32@16 size 4, bool@20 size 1}
ERR  write api: expected {FName@0, bool@8, i32@12}; frame is ...; SaveNamedData offsets cannot be trusted, so writes are refused
```

The expectation was the three-field frame measured by **disassembly** of the game's own
call site. This build's UFunction declares **four** parameters: the Blueprint library
`__WorldContext` at `+8` is real, and `Value` precedes `WasSet`, which tiles `ParmsSize 21`
exactly (8+8+4+1, no padding). A frame built from reflection could never equal the
three-field form, and the refusal blamed the offsets. Both shapes are accepted now, each
pinned strictly by count, order, offset and size, and the oracle runs at load:

```
NAMED-SAVE ORACLE PASS: GetNamedSaveData frame = {FName@0 size 8, object@8 size 8, i32@16 size 4, bool@20 size 1}
write api: ready=1 oracle=1 set=1 add=1
```

Nothing was refused in consequence -- the flag lane writes through the native map
(`NsdWrite`), not through this frame -- but a guard that fires on a good build is a guard
that gets ignored (R2), and this one had been silent rather than wrong.

### R52. FIXED - `HasCI` folded one side only, and that forced a video level visible

`enemyown::HasCI` lowercased the haystack and compared it to the **unlowered** needle, so
any needle containing an uppercase letter could never match. Every existing caller passed
an already-lowercase needle, so it had never shown. It showed when a `"VideoConfig"` guard
decided whether to force a level visible: it answered "no", the level was forced, and the
game died.

Measured (19:29 run), the host's log ends mid-frame and the process is gone:

```
19:29:43.349 INFO level union: SHOW VideoConfigMap -> bShouldBeVisible=1
19:29:43.349 INFO level union: SHOW VideoConfigMap -> bShouldBeVisible=1
19:29:43.349 INFO level union size 16 (local 15, forced for partner 15)
```

Both sides are folded now. The union also refuses to force a video/config or
shader-compile level at all -- neither is ever a place a player stands, so the union's
contract is not weakened -- and a refusal does not consume the two-levels-per-pump budget.
The same bug was making the teleport resolver ignore its own junk-class filter, which is
R50's 423 m teleport.

Verified in the following run, on both machines, and the session then survived:

```
WARN level union: refusing to force 'VideoConfigMap' (a video/config level, never a place a player stands)
```
```
0 occurrences of "SHOW VideoConfig"; both processes alive at the end.
```

This was the third crash of the same family as R48 and R49: a mod-side decision taken on
a wrong answer, during a world load.

### R53. The cheat object is the rig's most capable lever, and its names are not guessable

`FS_Cheats_C` on this build, in full (72): `AddAllEnemiesToBestiary`, `AddEnemyToBestiary`,
`AddNote`, `BindingMe`, `BPSlomo`, `ChallengeSimulateComplete`, `ChallengeSimulateProgress`,
`CheatLoad`, `CheatSave`, `ClearBestiaryData`, `CompleteAllChallenges`, 14 `Completed_*`,
`DeadGod`, `DeleteChallengeSave`, `DeleteItem`, `DiseaseMe`, `ExecuteUbergraph_FS_Cheats`,
`FillMyUltraHole`, `FindDoorOpenTarget`, `ForceCompleteChallenge`, `ForceOpenDoor`,
`ForceShowKnightGhosts`, `GemCurseMe`, `GimmeUltra`, `GimmeXP`, `GiveAllAccessories`,
`GiveAllConsumables`, `GiveAllEquipment`, `GiveAllGuides`, `GiveAllKeys`,
`GiveAllThrowingWeapons`, `GiveAllWeapons`, `GiveItem`, `GiveWeaponXP`, `HeavyMe`,
`HideHUD`, `HideTitleScreen`, `HurtMe`, `InternalChallengeProgressCheat`,
`KillAllEnemies`, `KnockMeDown`, `LoadSaveSlot`, `MagicFireMe`, `MagicPoisonMe`,
`NecroHeadClear`, `ParseSpawnEnemyArgs`, `PoisonMe`, `RemoveBestiaryEnemies`, `ReportBug`,
`RingLibraryBell`, `SaveHere`, `SaveNote`, `SetBestiaryEnemyPercent`, `SetCulture`,
`SetEnemyCountInBestiary`, `SetNamedSaveData`, `SetNativeCulture`, `SetTestCulture`,
`SetUltraCharge`, `ShowHUD`, `ShowTitleScreen`, `SimulateAllChallenges`, `SpawnEnemy`,
`SpawnEnemyFromData`, `SpawnEnemyInactive`, `StickyMe`, `SuspendSave`, `TestAudioBug_2D`,
`TestAudioBug_Player`, `TestEmptyLevelList`, `ToggleStreamignLevelVis`,
`UnlockAllCheckpoints`, `UnlockAllTeleporters`, `WeakenMe`, `WhaleBossDoBellySlide`,
`WhaleBossDoLaser`, `WhaleBossOverrideAttack`, `WhaleBossSetPhase`.

`qd.cheat` calls one of them by name on the machine it is written to. That is the rig's
origin for a world change, and it is the game's own code path for the event, not a
simulated one.

### R54. §5, step by step, with the lines

| Step | Verdict | The line |
|---|---|---|
| 1. Load | **PASS** | `switches: flags=1 inventory=1 actors=1 enemy=1 puppet=1 dialogue=1 stream=1 pause=0` / `NAMED-SAVE ORACLE PASS` / `container offsets: NamedSaveData=1000 SavableActorStates=704 InventoryContents=40` / `enemy sync: FS_EnemyCharacterBase found` / `hooked ULevelStreaming::SetShouldBeLoaded @ 029ACAC0` |
| 2. G1 | **PASS** | peer `flag CHANGE CheckpointUnlockCount : 1 -> 34` then `flag intent sent CheckpointUnlockCount=34 rev=28 try 1/5` then `flag intent confirmed ... rev=29`; host `intent applied: CheckpointUnlockCount=34`. One send, no repeat for the same value. |
| 3. Enemies | **PARTIAL** | `Paused ok=1` on both machines (90 and 117 times), and the peer's hit landed: `rig: hit enemy guid=F6418295 amount=200 ran=1 hp 34.9 -> -165.0 (local-run)`. **No `enemy death recorded` and no `enemy record sent`** for any of the three kills. The death reached the host through the *actor-state* lane instead (see step 6). |
| 4. G4 | **FAIL** | No attack fired in the scenario's own window: the provoked host stood 140 cm from `Ribber_Base_BP_C` for 90 s with no `damage sent`. In the 18:51 session the lane did transmit -- `damage sent guid=518B943C seq=1 melee amount=20.0 centi=2000 (Data.BaseDamage) range=5000` -- and the peer answered `player damage had NO effect: TryApplyDamage ran asked 20.0 returned 0.00 hp 400.0 -> 400.0`. Transport proven; **the effect is not**. |
| 5. Dialogue | **PASS** | peer `dialogue: local started with Ghost_Budaen [Knight_Budaen_NPC_Ghost_BP_C]`; host `dialogue: BLOCK Ghost_Budaen [Knight_Budaen_NPC_Ghost_BP_C] BlockInteract=`. |
| 6. G2 | **PASS on the lane, NOT-REACHED on the door** | The peer->host actor-state leg is proven, three times, by the kills in step 3: peer `actor-state local change: queued=1` / `actor-state intent sent guid=F6418295-... value=1 rev=29 try 1/5`; host `actor-state intent applied: guid=F6418295-... value=1`. The host->peer leg is proven too: peer `actor-state: wrote guid=b1cb0f48-... value=1 via SavableActor.Save Data Index to World Save index=0 read back container=1 cached=ok` / `actor states applied=1`. **A door was never made to change**: `FindDoorOpenTarget` and `ForceOpenDoor` both ran (`ran=1`) and produced no local container change, so no intent followed. |
| 7. G3 | **FAIL** | peer `item intent sent .../KeyItems/BookKeys/FairyTales/... x1 rev=2 try 3/5`; host `inventory grant failed: ... count did not rise by the requested amount: 0 -> 0 (wanted +1)` for every key item, and the peer gives up (`item intent given up ... after 5 attempts, the host never showed it`). In the 19:18 run the peer's queue also overflowed: `item intent: 0 unreadable path(s); pending overflow=1 ...; those pickups are NOT on the wire`. |
| 8. Streaming and travel | **PASS on streaming and survival, NOT-REACHED on follow** | `level name:` on both (50 lines each, no REFUSED), `level union size 16/15`, and **both processes alive at the end** for the first time after the R52 fix. `travel: local destination TempTeleporter/Ruins_Level_00 seq=2` at load on both, and the partner's travel was correctly declined: `travel: partner is travelling to TempTeleporter/Ruins_Level_00 but the local player is busy; skipped (not remembered)`. No `travel: followed` -- nobody ever travelled while the partner was free. |

### R55. What the failures are

**G3's grant does not work for key items.** The host resolves the peer's intent, calls its
own grant path, reads the counter back, and the counter does not move: `count did not rise
by the requested amount: 0 -> 0 (wanted +1)`. Either `AddItemToInventory` refuses
`KeyItems/*` or keys are not in `InventoryContents` on this build. The read-back is doing
its job -- the grant is reported as failed rather than assumed -- so this is a missing
write path, not a silent corruption. `GiveAllKeys` adds ~40 items in one frame, which also
overflows the peer's bounded intent queue; the overflow is logged and the items are
dropped, not sent late.

**Nothing can make a door change.** The two levers that exist (`ForceOpenDoor`,
`FindDoorOpenTarget`) run and change nothing the save container observes. The peer->host
actor-state lane itself works -- measured on three enemy GUIDs -- so the gap is a driver,
not the lane.

**No enemy attacked the player on demand.** Teleporting the host next to a
`Ribber_Base_BP_C`, a `RattyBugger_Base_BP_C` and a `Dragonfly_Base_BP_C` and waiting
90 s produced no attack. The attacks that *did* fire happened at the spawn, unprompted, in
an earlier session. Until an attack can be provoked, G4's `applied hp a -> b` half stays
unmeasured -- and the one measurement there is says the payload does not damage.

**`TryApplyDamage` damages an enemy but not the player.** The same call applied 200 to an
enemy (`hp 34.9 -> -165.0`) and returned nothing against the pawn
(`returned 0.00 hp 400.0 -> 400.0`). Since both go through the same payload fill and the
same reader, the difference is in the receiving implementation, not in the module's
struct: `FS_Full_PlayerChar_C`'s `FB_Damageable` path wants something this fill does not
provide. **This is the single thing standing between the damage lane and G4.**

**No enemy death record.** Three enemies went to negative HP and `StoreEnemySaveState`
never fired for any of them, so no `enemy death recorded` and no `enemy record sent`. What
happened instead is that each kill produced a *savable actor state* change, which the
actor-state lane carried to the host. So a kill does reach the partner -- by a different
lane than the design says -- and the enemy-persistence lane (the spawner GUID record that
stops an enemy respawning) is unproven.

### Still unproven after this session
- G4's effect half, and any enemy attack on demand.
- `travel: followed` (needs a travel while the partner is free), and `WORLD DIVERGENCE`
  (needs a hard map switch, which no lever performs).
- The door/chest half of G2, and with it the visibility question in SYNC-GAPS §2.
- Enemy death records reaching the host as records rather than as actor states.
- Whether G2's in-level actor shows the change without a load: the container converges
  (`read back container=1 cached=ok`) but nothing in this run looked at the screen.

### What this session cost, and what it bought
Five defects, three of them in code that had passed 3 598 assertions and two live
sessions: the oracle refused a correct build, `HasCI` answered wrongly for any uppercase
needle (which both defeated a guard and crashed the game), and the interaction component's
verb was on the wrong object. The pattern is consistent with every earlier round -- the
defect is never in the state machine, it is in the assumption about the game.

---

## Session 7 - 2026-11-08 (the new switches, the new lanes, and five more ways to die)

This branch is `sync-fix` at 3e61107, which merges B7 (the partner body as
`FS_PlayerChar_C`), G2's one-shot `RestoreFromSave` refresh, G3 consumption intents, and the
hard-map travel / cutscene / loot / mover / dialogue / suspend work. Every write path was
on, including the new ones: `FlagSync=1 InventorySync=1 ActorStateSync=1 EnemySync=1
PuppetSync=1 PlayerBody=1 DialogueSync=1 StreamUnion=1 TravelSync=1 CutsceneSync=1
MoverSync=1 LootSync=1 SuspendGuard=1`.

`tools/session7.ps1` is the whole session in one process (relay, two instances, driving,
collection, teardown), and it reads the roles from `session established`: **host=inst1,
peer=inst2** in every run below. The scenario takes the pair through a real hard-map round
trip and then drives the lanes; the load lines are quoted rather than paraphrased.

### R56. The full switch set loads and survives five minutes

Both instances, all switches on (the probe and the final full run agree):

```
INFO switches: flags=1 inventory=1 actors=1 enemy=1 puppet=1 playerBody=1 dialogue=1
     stream=1 travel=1 cutscene=1 movers=1 loot=1 suspend=1 pause=0
INFO NAMED-SAVE ORACLE PASS: GetNamedSaveData frame = {FName@0 size 8, object@8 size 8, i32@16 size 4, bool@20 size 1}
INFO container offsets: NamedSaveData=1000 SavableActorStates=704 InventoryContents=40
INFO enemy sync: FS_EnemyCharacterBase found
INFO hooked ULevelStreaming::SetShouldBeLoaded @ 029ACAC0
```

`IDLE 300s` -- `PASS [1] alive after 300s idle`, `PASS [2] alive after 300s idle`,
`error/fault lines: 0` on both. This is the first run with `PlayerBody=1` and the six new
write paths together that survived the idle window.

### R57. B7: the body is the game's own player class, unpossessed, and the AI targets it

The spawn pair, on each machine (each spawns the OTHER player's body, which is why the two
positions are swapped):

```
INFO player body: class FS_PlayerChar_C resolved
INFO player body: possession cleared (AutoPossessPlayer 0, AutoPossessAI 0)
INFO player body: spawned unpossessed at -1586,-130,-41 (tick off, movement off/mode set,
     controller none, collision off, own meshes hidden); the poseable body is the visible one
```

and after a map return, the puppet path reports the class by name:

```
INFO puppet spawned as FS_PlayerChar_C at -1586,-130,-41
```

The target lane, both directions, on the host with the peer's body as the nearer player:

```
INFO enemy 68BD16CA: TargetPlayer local player -> the remote body (read back) dLocal=5462 dRemote=3554
INFO enemy 4253860F: TargetPlayer remote body -> the local player (read back) dLocal=9823 dRemote=10402
```

`(read back)` is the write being observed, not the call returning. The second line came from
the deliberate swap (host nearer, peer farther); ownership stayed on the host because the
distance difference stayed inside the 800 cm margin.

### R58. A kill now produces a death record (Session 6's gap, closed)

The hit is the module's own `TryApplyDamage`; the death is the game's. Measured on the peer:

```
INFO rig: hit enemy guid=0ADA5B25 amount=400 ran=1 hp 25.0 -> -375.0 (local-run)
INFO enemy death detected guid=0ADA5B25 (bDead/IsDead, hp -375.0); recording it through StoreEnemySaveState
INFO enemy death recorded guid=0ADA5B25 respawn=0.0
INFO enemy record sent to host guid=0ADA5B25 hp=0.0
```

and on the host the persistence intent lands:

```
INFO enemy record applied guid=383AE655 hp=0.0 respawn=0.0
```

**Root cause.** `StoreEnemySaveState` is called by the spawner's own death path, not by the
damage message. A direct `TryApplyDamage` kill (the rig's) therefore never wrote a record,
which is exactly what Session 6 saw (`hp 34.9 -> -165`, no lines). The fix polls the actor
(`bDead` / `IsDead` / HP <= 0) for enemies this machine runs and records the death THROUGH
the game's own `StoreEnemySaveState`, so the save, the kill log and the peer intent keep one
path. Detection requires the actor to have been seen ALIVE first: a spawner's enemy exists
at HP 0 before activation, and a map load leaves actors mid-teardown reading 0.

Two bugs fell out of proving it: a map load produced 30+ invented deaths in one frame
(`enemy record sent` for every unspawned enemy), and the first full run died inside
`EnemyOwnPump` -- `EXCEPTION_ACCESS_VIOLATION`, `WINMM+0x26805` (`EnemyOwnPump+0x265`),
mapped from the WER CrashContext. Both are fixed in R64.

### R59. The hard-map handshake works both ways

Only `Ruins_Level_00` carries a `PlayerStart`; the area maps are sublevels, so a hard switch
to one lands in a `DefaultPawn`. The round trip therefore goes to `990_B33sment` and back,
which is a real UWorld switch, and the area content is reached by streaming the sublevel
into the hub.

```
host: rig: hard lever -> 990_B33sment LoadMapWithLoadingScreen ran=1
host: travel: HARD transition to 990_B33sment -- announcing it so the partner loads the same world
peer: signature LoadMapWithLoadingScreen props=2 parms=24 ret@-1  Map: NameProperty  MapPackage: StrProperty
peer: travel: hard transition: loading 990_B33sment through GameInstance.LoadMapWithLoadingScreen
host: travel: the partner arrived in 990_B33sment (both there)
host: travel: both machines arrived in 990_B33sment; host releasing the pair to resume
peer: travel: host released the pair; resuming in 990_B33sment
host: travel: HARD transition to Ruins_Level_00 -- announcing it so the partner loads the same world
peer: travel: hard transition: loading Ruins_Level_00 through GameInstance.LoadMapWithLoadingScreen
host: travel: both machines arrived in Ruins_Level_00; host releasing the pair to resume
```

`PASS` for all 14 checks (BEGIN, the load on both sides, ARRIVED, RESUME, and the same again
on the way home); both processes alive afterwards, and gameplay returns on the home map.
Four defects had to be fixed before that was true (R64): a busy peer dropped the one-shot
BEGIN and never saw the transition again; the boot race put the home map in the gave-up
ring so the return was refused; a machine with no gameplay pawn was treated as busy, which
made a DefaultPawn world a trap; and a lost `HARD_BEGIN` was never re-announced.

### R60. G2's chest half is NOT reached: the interact found a flag chest

`near:chest` resolved a real savable and ran its own verb, but the class was
`SG_Chest_BP_C`, a quest chest whose state is a `NamedSaveData` flag, not a
`SavableActorStates` body:

```
INFO dev interact: resolved 'Chest' -> SG_Chest_BP2 at 6185,-18108,-14047 dist=23877
INFO dev interact: target=SG_Chest_BP2 [SG_Chest_BP_C] component=found verb=Interact
INFO flag CHANGE TotalChestsOpened : 1 -> 2
INFO flag intent sent TotalChestsOpened=2 rev=867 try 1/5
INFO flag intent confirmed TotalChestsOpened=2 rev=868
INFO intent applied: TotalChestsOpened=2
```

That is a G1 proof, not G2: there is no `actor-state local change`, so the §5.2
`actor-state refresh ok ... opened=1` lines did not appear on either machine, and the verdict
is FAIL for the G2 half. The same chest also passed the "second interact is not a second
open" check only trivially (no new intent). The level-stream lever
(`rig: level lever -> 200_AQ forced loaded and visible (pinned...)`) worked, but the chest
that got picked was the first savable matching "chest" anywhere in the graph.

### R61. The in-world teleport follow fails at the second resolution

The lever found a teleporter and read its `LocationData`, then the follower's own resolver
refused the SAME name:

```
INFO rig: travel near 'teleport' -> Teleporter_GR at 13415,-1321,4237 dist=12058
WARN rig: travel lever -> Teleporter_GR/Ruins_Level_00 (LocationData resolved,
     dest field CurrentTeleporterTarget) TeleportToLocation ran=0: no live actor named Teleporter_GR
```

`RigFindNear` found the actor by a class walk; `FindTravelActor` then applies the
world/outer test and rejects it (its level is streamed but not the world the follower is
in, or its outer chain does not reach the current world). The announcement therefore never
crossed, and `travel: followed` did not appear. Not fixed: it needs the resolver to keep the
actor it already found instead of re-resolving by name (recorded for the next round).

### R62. Loot: the lane cannot key this build's drops

The drop actors exist and are marked drops, but they expose no source/index:

```
WARN loot: a drop actor exposes no source/item property; it is not represented on the wire (first occurrence)
INFO rig: drop probe HealingPotion_Item_BP_C [HealingPotion_Item_BP_C] dist=895 isADrop=1
     hasGUID=? lootKey=no | itemData=- itemRef=- owner=- inst=- dropper=-
```

`IsDropClass` and the actor's `IsADrop` agree (the spawned loot is an `ItemPickup_Base`
subclass, the same family as placed pickups), but `LootKeyOf` finds neither the game's drop
source nor an item object on it. The wire identity is therefore missing, not the lane:
`loot: drop`, `loot: merged`, `loot: local pickup` and `loot: hiding local drop` were all
absent. This is a content-layout gap in the lane's assumption, not a transport failure.

### R63. G4's effect half: no hit was provoked in this run

No `player body hit ... -> sent` and no `damage from enemy ... applied` in the window; the
`player damage had NO effect` count is 0, so the lane was silent rather than refused. The
B7 target lines were present, which is the precondition; the attack itself did not land in
the 90 s window with ~240 enemies being re-owned constantly. (The other agent is working the
`TryApplyDamage returned 0` root cause offline; this run only records that the provocation
did not fire.)

### R64. What was fixed, and what is left

Fixed and verified live:

* **PuppetPump crash.** `ObjectIsLive(g_puppetActor)` across a map load is a read of freed
  memory: `WINMM+0x44691` (`PuppetPump+0x71`) after `LoadMapWithLoadingScreen`. The world
  identity is now compared first and the stale pointer is dropped without a read.
* **EnemyOwnPump crash.** `WINMM+0x26805` (`EnemyOwnPump+0x265`) after four minutes of
  combat with ~190 enemies. Each enemy record's body is now fault-contained (and liveness
  is re-checked after a damage application); `EnemyTargetPump` and `EnemySendPump` got the
  same guard.
* **Death detection** (R58) plus the load-window suppression that stops a world switch
  inventing a level's worth of deaths, and the "seen alive" rule that stops unspawned
  enemies being recorded as dead.
* **Hard-travel robustness** (R59): re-announce `HARD_BEGIN` until the peer arrives; a map
  the machine is IN clears its gave-up entry and a same-map deadline does not poison it; a
  machine with no gameplay pawn is not "busy" for a hard switch (the check now names its
  reason, which is how the trap was found).
* **Rig**: `Set-Control $null` wrote an EMPTY file, and an empty `qd.hit` means "the default
  200 damage" in `DevLever` -- so "clear the lever" armed it (the first full run killed
  enemies unasked). `qd.near` is now per-machine (needed for the target swap), `qd.level`
  pins a sublevel, `qd.hard`/`qd.travel` are the travel origins, `qd.drops` is the drop
  probe, and repeated `qd.tp`/`qd.act` instructions vary their yaw/case so they are not
  deduped away.

Open, recorded rather than fixed:

* **A hard switch to an area sublevel has no player.** All of `100_GR`, `200_AQ`,
  `990_B33sment` and `900_SipehaWorld` have no `PlayerStart`; only `Ruins_Level_00` does.
  The rig streams the sublevel instead, which is why the chest step ran in the hub.
* **Menus pause by two mechanisms.** The world's `SetGamePaused` is declined during a
  session, but `GameMenu_C` also sets global time dilation below 0.5, which our hook does not
  touch: one player opening the inventory can still freeze the shared world. That is a
  finding from a working third-party mod's surface, not yet measured here -- measure it
  before fixing, and keep the exception (Escape's PauseMenu_C).
* The in-world travel resolver re-resolves by name (R61) and the loot identity needs a
  property this build does not put on the drop actor (R62).

### Verdicts, item by item

| Item | Verdict | Key line |
|---|---|---|
| 1. Load, all switches, 5 min idle | **PASS** | `PASS [1] alive after 300s idle` / `PASS [2] ...` |
| 2. B7 body + target gates | **PASS** | `player body: spawned unpossessed` / `TargetPlayer ... (read back)` both ways |
| 3. Enemy death record | **PASS** | `enemy death detected` -> `enemy death recorded` -> `enemy record sent to host` -> `enemy record applied` |
| 4. G2 chest refresh | **FAIL (NOT REACHED)** | `dev interact: target=SG_Chest_BP2 [SG_Chest_BP_C] ... verb=Interact` then a flag change, no `actor-state` line |
| 5a. Hard handshake | **PASS** | 14 checks incl. `both machines arrived ... host releasing the pair to resume`, twice |
| 5b. In-world teleport follow | **FAIL** | `TeleportToLocation ran=0: no live actor named Teleporter_GR` |
| 6. Loot drop disappears on the other | **FAIL** | `loot: a drop actor exposes no source/item property; it is not represented on the wire` |
| 7. Enemy attacks the partner body | **FAIL (no hit)** | no `player body hit ... -> sent`; `player damage had NO effect` count 0 |
| Session survival | **PASS** | `process alive=True` on both |

### What this session cost

Six defects, all in the class this log keeps naming: the assumption about the game, not the
state machine. Two were use-after-free crashes that the previous switch set had never
exposed (`PuppetPump` on a map load, `EnemyOwnPump` after minutes of combat); one was a
one-shot protocol phase that a busy peer silently swallowed; one was the boot race poisoning
the map it was standing in; one was the rig's own "clear" writing an armed lever; and one was
death detection waiting for an event the damage path never sends. The two lanes that remain
unproven -- G2's chest refresh and the in-world travel follow -- are both a resolver/identity
problem now, not a transport one.

---

## Session 8 — 2026-11-08 (parity live, G2's real identity, and the two stop mechanisms)

This branch is `sync-fix` at `80364e3` plus the enemy-parity merge, then `sync-fix` at
`3f0e5ed` (the offline G3 soft-pointer grant and G4 player-HP fallback). Every write path is
on (`SuppressMenuPause=1` too, so the pause/dilation hooks are exercised), `LogLevel=3` so
the parity debug lines exist. `tools/session8.ps1` is the whole run in one process: relay,
two instances, the driving, the collection and the teardown. It reads the roles from
`session established`: **host=inst1, peer=inst2** in every run below.

### R65. The load crash: a silent exit at the autoload, and the containment that fixed it

The first full run **ended the host at 22:02:45**, exactly after
`dev autoload: Load Slot(0) called` (the log's last line is the new `clock:` line). There was
**no WER event, no dump, and no `ERR` line** — the process exited rather than faulted, which
is the class of failure Session 7's two use-after-free bugs also looked like from outside.

A single instance with the same image and switches survived its own load and reached
gameplay, so the load itself was fine; the difference is a **connected peer during the
load**. The two always-on reads added this round touch UObjects every frame: the menu check
(`LocalOpenMenuName`) reads the pawn and its menu widgets, and the loot scan's derived key
reads the drop actor's class and transform. Session 7's rule (R64) is that a pump reading a
pointer across a world switch is a read of freed memory; both are now **SEH-contained**, the
same pattern the enemy pumps use. Measured afterwards:

```
PASS [1] alive after 180s idle
PASS [2] alive after 180s idle
[1] error/fault lines: 0
[2] error/fault lines: 0
```

and the same for a later 60 s idle. The exact fault was not left in WER; the containment is
the fix, and it has not recurred over four full boots (two hosts, two peers) and two partial
runs. If it returns, the next datum is the `qdcoop-qd1.log.prev` boundary and the UE
`-abslog` file (now passed by `run-hidden.ps1 -AbsLog`).

### R66. Enemy parity live: three identity sources, the same key, and the bandwidth line

The identity lines now exist at INFO for all three sources, and the two logs agree on the
keys:

```
[1] enemy id: spawner 13F4155E (Ribber_Base_BP_C)
[2] enemy id: spawner 13F4155E (Ribber_Base_BP_C)
...
shared enemy keys: inst1=256 inst2=133 common=61
```

The `common` set is keys minted by one machine and seen by the other; the count is large
because the spawner set is shared and because adoption only runs for hash-derived keys. The
detail lane and its budget report as the design asks:

```
INFO enemy detail sent guid=B55C570E anim=1 seq=17 status=00000000 boss=0 d=19082 /Game/Enemies/Crawler/Animations/Crawler_JumpingBite_Montage...
DBG  enemy detail: candidates=1 sent=1 budgetDrops=0 farSkips=0 overflows=0
```

and the follower's montage read-back appeared in the first run of the pair:

```
INFO enemy anim: Montage_Play verified by read-back guid=9988EA07
INFO enemy detail applied guid=9988EA07 anim=1 seq=2 played (montage proven at least once this session)
```

The peer's own bandwidth line is absent in the later runs because the peer's enemies were
not animating there; the host's is present. `sent` never exceeded 10 in a rolling second in
any captured block; `budgetDrops=0`, `overflows=0`.

### R67. G2's chest is NOT a SavableActor on this build — and that is the answer

`near:closedchest` now resolves the SA chest and the interact dump names its state:

```
INFO dev interact: resolved 'closedchest' -> SG_Chest_BP2 at 6185,-18108,-14047 dist=23818
INFO dev interact: target=SG_Chest_BP2 [SG_Chest_BP_C] component=found verb=Interact
INFO dev state: SG_Chest_BP2 [SG_Chest_BP_C] savable=no guid=- index=-1 value=-1
     isset=? opened=0 isOpen=n/a mySaveData=1 guids=0
INFO flag CHANGE TotalChestsOpened : 1 -> 2
```

The chest has **no GUID, no `SavableActorStates` entry and no `GUIDs` array**. Its opened
state is its own **`Opened` bool** (0 here) plus the global `TotalChestsOpened` flag. So the
R60 observation was not "the chest was already open": it was the wrong lane. `ActorStateSync`
cannot carry this actor at all, because the lane's identity is the GUID.

The peer's local `Opened` did change (the verb ran), and the **key** pickup's savable did make
the round trip — this is the `based_on=0` fix (R70) working:

```
INFO actor-state intent applied: guid=000934FA-4BD964EC-ABDD8DBE-59AF657E value=1
INFO actor-state refresh ok ... opened=1
```

The chest half is therefore **FAIL as an actor-state**, with a concrete next step: give the
chest a stable key of its own (the level path hash the placed-enemy identity already uses)
and sync its `Opened` bool through a lane that is not keyed by GUID, or make the chest's own
Blueprint write a per-chest `NamedSaveData` key. The `dev state:` line is what makes that a
design decision rather than another guess.

### R68. The in-world travel resolver is fixed; the follower's verb is the next wall

The resolver now keeps the actor `RigFindNear` found, and the follower resolves the same
name through a streamed level:

```
INFO travel: resolved 'Teleporter_Aqr' without the outer-chain test (its level is streamed, not the world)
```

The origin's call no longer re-resolves by name, and it runs. The follower then reports a
different, real fact:

```
WARN travel: cannot follow Teleporter_GR/Ruins_Level_00: TeleportToLocation has no LocationData parameter
```

The chest run and the travel run both resolved `Teleporter_GR`, so the resolver half of R61
is closed. The follower's `FS_PlayerChar.TeleportToLocation` signature does not expose the
`Location Data` object parameter on that machine's pawn class; the next probe is the live
signature dump of that pawn (the error now names its class). `travel: followed` is still
**NOT-REACHED**.

### R69. Loot: the derived key works; the tombstone half did not fire in the last window

The build's drop actor exposes no `{source, item}` property (R62), so the identity is derived
from the item class and the quantized spawn point. Measured:

```
INFO loot: drop 8CD5056D#0 = /Game/Items/Consumables/Herb/Herb_Item_BP.Herb_Item_BP_C
INFO loot: merged 1 entry rev=69
```

The two machines agree on the derived key and the host merges the set. `near:loot` selects the
ledger's actor rather than the nearest placed pickup (the earlier `near:drop` picked a placed
herb). The pickup/tombstone lines did not appear in the final window: the ledger entry and the
actor the player touched were not the same one in time. The lane is **PARTIAL**: identity
proven, disappearance not.

### R70. The G3 grant and consumption halves both land

The key pickup, grant and consumption, on the two machines:

```
[2] INFO item intent sent /Game/Items/KeyItems/BookKeys/Alchemy/BookKey_00_Alchemy_Item... x1 rev=857 try 1/5
[1] INFO write api: AddItemToInventory /Game/Items/KeyItems/BookKeys/Alchemy/BookKey_00_Alchemy_Item... +1 read back 1 -> 2
[1] INFO inventory grant applied: ... x1 (peer total 1 rev=929)
[2] INFO item intent confirmed: ... host=1
[2] INFO rig: use lever -> ... removed=1 ok=1
[2] INFO write api: TryConsumingItemFromInventory ... -1 read back 2 -> 1
[2] INFO item consume sent ... rev=933 try 1/5
[1] INFO inventory consume applied: ... -1 (peer total 1 rev=933)
[2] INFO item consume confirmed: ... host=1
```

Both halves PASS. The grant path's soft-object write is the `3f0e5ed` fix; the consumption
path is driven through the game's own consumer, and the `last`-grant path removes the need to
reconstruct a soft path from a log line.

### R71. The G4 receiver moves the player's HP and names the path

The host sends one `K_DAMAGE`, the peer applies it through the new fallback, and the HP moves:

```
[1] INFO rig: pdmg -> guid=C22D6A9C amount=20.0 centi=2000 sent=1 (G4 receiver provocation)
[2] INFO damage: TryApplyDamage param DamageToApply size=216 on the local player, amount -> DamageAmount @0
[2] INFO damage: player hp via Health (TryApplyDamage had no effect) 20.0 (TryApplyDamage returned 0.00) hp 400.0 -> 380.0
[2] INFO damage from enemy C22D6A9C melee: 20.0 applied hp 400.0 -> 380.0 (no local actor for the attacker)
[2] INFO god: health refilled 380.0 -> 400.0 (refills=2)
```

PASS. No `player damage had NO effect` line. The `god` floor is the rig's, not the
receiver's; the `damage:` line is printed from the value read back inside the call.

### R72. Menu time dilation: both stop mechanisms, and a stray 0.00 with no menu

The pause hook (`SuppressMenuPause=1`) now has a sibling on
`UGameplayStatics::SetGlobalTimeDilation`, and a poll that restores a direct
`AWorldSettings` write. Measured on the host, with the pretend-menu lever:

```
INFO rig: menu lever -> pretend menu open, SetGlobalTimeDilation(0.20) issued; dilation 1.00 -> 0.20
WARN restored global time dilation 0.20 -> 1.00 (menu DevPretendMenu)
```

and with **no menu at all** — which is the case the first run found on the peer, whose
`unpaused` clock sat at `840.3` while its `dilation` read `0.00`:

```
INFO rig: menu lever -> no menu, pretend menu closed, SetGlobalTimeDilation(0.20) issued
WARN restored global time dilation 0.20 -> 1.00 (menu none)
PASS a stray low dilation with no menu did not stop the world
```

Both worlds' `unpaused` clocks advance across every window, on the host and on the peer:

```
INFO clock: dilation=1.00 real=... unpaused=... delta=... menu=-
PASS the host world clock keeps advancing with its menu open
PASS the peer world clock keeps advancing
```

The only exception is the Escape `PauseMenu_C` (the task's named exception). Enemy hit-stop
is a deliberate casualty of the rule: a local slow-mo is not worth a frozen partner. PASS.

### R73. The re-broadcast bug in the actor-state lane: `based_on=0` was refused as stale

The peer's intent carried `rev=0` in every run, and the host refused every one:

```
[2] INFO actor-state intent sent guid=23B24B15... value=38 rev=0 try 1/5
[1] INFO actor-state intent refused (stale): based_on=0 last_sent=442
```

`based_on=0` means "this peer has not yet applied ANY host body", not "an older world". The
gate now accepts zero and still refuses a non-zero baseline older than the last publish. The
key's savable then applied on the host and refreshed (`actor-state intent applied`,
`actor-state refresh ok ... opened=1`). The residual finding is separate and named: **neither
log contains `actor states applied=...`**, so the host's absolute `K_ACTOR_STATES` body is
still not being applied on the peer. The intent gate is now correct; the peer's mirror and
its `confirmed=1` line still need that body.

### R74. Item 7: no game hit was provoked

The enemies near the players did attack — the montage lane carried
`Crawler_JumpingBite_Montage` — but the damage lane refused them as out of range:

```
DBG damage: swing not sent guid=44D6AA74 owner=1 peerInWorld=1 worldsAgree=1 havePos=1 range=350 (GetAttackRange)
```

No `player body hit ... -> sent` appeared. The enemy Blueprint surface near the player is a
trap enemy with no attack UFUNCTION (`InitDeathFade`, `ActivateTrap`, `EnterTrapMode`,
`ExecuteUbergraph`), so the rig's `qd.eattack` cannot call one. Provoking a hit needs a
melee enemy whose `TargetPlayer` is the proxy at attack range; the target steering itself is
proven (`TargetPlayer ... -> the remote body (read back)`). **FAIL (no hit)**.

### Bugs fixed this session

* **Load crash containment** (R65): SEH on the always-on menu/pawn read and on the loot
  key read; no recurrence across four boots.
* **Travel resolver** (R68): `FollowTravelActor` uses the actor the search found;
  `FindTravelActor` falls back to the cooked instance name for a streamed level.
* **Loot identity** (R69): `{item class, quantized spawn point, coincidence index}`, with
  unit tests; `near:loot` selects the ledger's actor, not a placed pickup.
* **G2's question answered and instrumented** (R67): `dev state:` prints GUID/index/value/
  `Opened`/`mySaveData`; `near:closedchest`; the chest is not a Savable.
* **Menu dilation** (R72): the setter hook, the restore poll, and the no-menu case, with the
  `clock:` line on both machines.
* **G3 consumption** (R70): the writer remembers the last granted path; `qd.use=last`.
* **Actor-state intent gate** (R73): `based_on=0` is "no baseline", not stale.
* **Enemy identity at INFO** for all three sources, so the parity run can compare keys.

### Verdicts, item by item

| Item | Verdict | Key line |
|---|---|---|
| 1. Load, all switches, 3 min idle + parity | **PASS** | `PASS [1] alive after 180s idle` / `PASS [2] ...`; `switches: ... pause=1` |
| 2. Enemy parity: sources, same key, montage, bandwidth | **PASS** | `common=61`; `enemy detail: candidates=1 sent=1`; `Montage_Play verified by read-back` |
| 3. G2 chest | **FAIL (NOT-REACHED as actor-state)** | `dev state: SG_Chest_BP2 [SG_Chest_BP_C] savable=no guid=- ... opened=0 mySaveData=1` |
| 4. In-world teleport follow | **FAIL (NOT-REACHED)** | resolver: `travel: resolved ... without the outer-chain test`; follower: `TeleportToLocation has no LocationData parameter` |
| 5. Loot drop disappears on the other | **PARTIAL** | `loot: drop 8CD5056D#0 = ...` + `loot: merged 1 entry`; no tombstone in the last window |
| 6. Menu time dilation | **PASS** | `restored global time dilation 0.20 -> 1.00 (menu none)`; both clocks advance |
| 7. Enemy attack on the partner body | **FAIL (no hit)** | `damage: swing not sent ... range=350`; no `player body hit ... -> sent` |
| 8. G3 key grant + consumption | **PASS** | `+1 read back 1 -> 2` -> `inventory grant applied` -> `item intent confirmed`; `-1 read back 2 -> 1` -> `inventory consume applied` -> `item consume confirmed` |
| 9. G4 receiver | **PASS** | `damage: player hp via Health ... hp 400.0 -> 380.0`; `damage from enemy ... applied hp 400.0 -> 380.0` |
| Session survival | **PASS** | `process alive=True` on both, four boots |

### What this session cost, and what it settled

The identity question that has followed G2 since Session 6 is answered by one line: the
chest is not a savable, it has its own `Opened` bool and a global counter. The travel
resolver's "no live actor" was a real bug and is fixed; the remaining refusal is the
follower's verb signature, which now names itself. The two stop mechanisms are both under
the session rule, including the stray 0.00 that no menu explained. The lane that still
needs work is the *receive* side of the absolute actor-state body (no `actor states
applied=` on either machine), which is why the peer's `confirmed=1` and the chest's
partner-side refresh are still missing. That is the next round's first measurement.



---

## Session 9 — 2026-11-09 (the receive side, the follower's verb, and the swing that hit nobody)

This branch is `sync-fix` at `1c708d7`, merged with `9db18ab` (the OBJECTS lane, wire 0x2A),
at `ee017c2` plus the fixes below. Every write path is on (`ObjectSync=1` now too),
`LogLevel=3`, `PlayerBody=1`. `tools/session9.ps1` is the whole run in one process: relay,
two instances, the driving, the collection and the teardown. It reads the roles from
`session established`: **host=inst1, peer=inst2**.

### R75. The R74 residual was the RELAY, not the peer: every chunked container was dropped

The peer never logged `actor states applied=` because it never received a body. The relay
decodes and validates every datagram before forwarding it, and `qd-core`'s `ACTOR_STATES`
decoder read the header's `n` as **this datagram's** entry count. `n` is the size of the
sender's whole container while one datagram carries a prefix (WIRE.md §3.6), so a
117-entry `SavableActorStates` — three datagrams — failed validation on all three:

```
[qd-relay] malformed datagram from 127.0.0.1:64386: unexpected end of data: needed 128 bits, 0 available
[qd-relay] malformed datagram from 127.0.0.1:64386: unexpected end of data: needed 32 bits, 0 available
```

**2 364 of them in one session**, while every body that fits one datagram (flags,
inventory) forwarded normally. That is the whole residual: the lane was correct on both
machines and the relay ate it. `WorldFlags`, `Inventory`, `Loot` and `Movers` now read the
prefix (stop when the body ends, and a body cut INSIDE an entry is still a framing error),
pinned by `a_chunked_container_body_decodes_the_entries_it_carries` and the relay-level
`a_chunked_actor_states_body_is_forwarded_not_dropped`.

The same class of bug sat in the LOOT publisher: it wrote `n` = the entries handed to the
writer while `WriteLootChunk` stopped at the datagram cap, so a 68-drop ledger produced
bodies that **both** the relay and the partner's own reader refused. `n` is the ledger size
now and the pass walks a cursor, like the actor-state publisher.

Measured afterwards, peer side, every second:

```
[2] INFO actor states applied=0 held=0 confirmed=1 failed=0 rev=562 n=142
[2] INFO actor states applied=0 held=0 confirmed=2 failed=0 rev=566 n=142
```

and the two halves of §5.2's G2 proof both appear on the peer in one run:
`actor-state intent applied: guid=E8EFC097-… value=1` (host) then
`actor-state refresh ok Dragonfly_Strong_Spawner15 [Dragonfly_Strong_Spawner_C] index=0
value=1 mySaveData=1->1 guids=1 cached=ok opened=n/a` (host), and on the peer
`actor states applied=… confirmed=1`. **`confirmed=1` is the line R74 said was missing.**

The `applied=` count stayed 0 for the run's own coin because the two instances are seeded
from one save and have converged over eight sessions: every value the host publishes for a
coin the peer already holds is correctly a no-op. The lane's *apply* path is proven by the
entries the peer did not have (`n` grows 121 → 122 → 142 in step with the host) and by the
intents it confirms; a controlled value difference needs a fresh save pair, which is the
next round's first setup task.

### R76. A bare `Fn f;` can "resolve" garbage, and it killed the peer

The travel follow was rewritten (R77) and the first run died:

```
[2] INFO fn TeleportToLocation: not found
[2] INFO signature K2_TeleportTo props=600 parms=1150895621 ret@32758
[2] INFO     x沌→ : ・ @-1912931456 size=600 flags=000007EF
… (process memory dumped as "parameters")
[2] alive after the teleport: False
```

`struct Fn { void* fn; bool tried; LayoutResult sig; }` had no initialisers, and `Resolve`
begins with `if (fn && sig.valid) return true`. A stack `Fn` therefore inherits whatever the
previous frame left there: a non-null `fn` and a `valid` signature are believed, and the
garbage `props` (600) then walked the fixed 160-entry array past its end — a stack overread
that printed process memory and then faulted the process. `Fn`'s zeroes now live in the
type, `Resolve` refuses a signature whose `props`/`parms_size` do not fit the arrays it
indexes, and `LogSignature` clamps. Four other bare `Fn` locals already existed; all of them
were one dirty stack frame away from the same crash.

### R77. The follower's verb is the CARRIER's `Interact`, and the fs_PlayerChar signature in the docs does not exist

`FS_PlayerChar.TeleportToLocation(Location Data, GreenFX)` — the signature every document
quotes — is not on this build. The failure names it:

```
WARN fn TeleportToLocation: not found
INFO travel: the carrier.Interact signature resolved
INFO signature travel props=1 parms=8 ret@-1
INFO     PlayerInteracting : ObjectProperty @0 size=8 flags=40000294
INFO travel: Teleporter_GR.Interact ran on the local pawn (the game's own travel verb)
INFO rig: travel lever -> Teleporter_GR/Ruins_Level_00 (LocationData resolved, dest field CurrentTeleporterTarget) TeleportToLocation ran=1
```

So the follow now packs the signature that IS there — by type, not by name: an object
parameter is the `LocationData` (or the pawn when the name says "player"/"instigator"), a
bool is `GreenFX`, a 12-byte struct is the destination point and a transform-sized one the
whole transform — and tries, in order, the carrier's `TeleportToLocation`, the carrier's
`Interact` (the verb a player pressing E runs), the pawn's `TeleportToLocation`, and
`K2_TeleportTo`. Each signature is printed once, so the next refusal names itself.

The announcement needed one more fix: the game's own verb clears the pawn's
`CurrentTeleporterTarget` as it moves the player, so `TravelPump` read "(none)" and the
partner never heard a destination. The rig lever now seeds the lane with the destination it
just drove, one tick earlier than the game would:

```
[2] INFO travel: local destination Teleporter_GR/Ruins_Level_00 seq=6
[1] INFO travel: followed Teleporter_GR/Ruins_Level_00 (seq 6) -- local pawn driven through TeleportToLocation
```

**`travel: followed` — the line Session 7 and 8 never saw.** PASS.

### R78. The swing that hit nobody: the described lane yielded to a hit path that cannot land

R74 recorded `damage: swing not sent … range=350 (GetAttackRange)` with no `player body
hit`. Both symptoms have one cause, and it is not the range: the player-class proxy is
spawned with **collision off** (R57), so this build's melee never resolves it, and
`SwingAlreadyReal` still suppressed the described lane just because the enemy's
`TargetPlayer` pointed at the body. The swing was described by neither lane.

Two changes:

* `SwingAlreadyReal` takes a third fact, `real_hit_proven`: the described lane stands down
  only after `PlayerBodyObserve` has actually forwarded one hit from the proxy. Until then
  `damage: guid=… is aimed at the proxy but the game's own hit path has never landed on it;
  describing the swing instead` says so.
* the range test measures to the **proxy body** (`ReadActorXform(g_puppetActor)`), not to the
  interpolated player position. The body is the victim and is always there; the interpolated
  position is a prediction that a few seconds without a row makes `frozen`, which refused the
  whole lane (`havePos=0`, now with the interpolation's own verdict appended:
  `interp=none|hidden|frozen|ok`). `EnemyTargetPump` lost the same gate, so the steering no
  longer stops when the prediction does.

Measured, host then peer, one swing:

```
[1] INFO enemy 91122467: TargetPlayer local player -> the remote body (read back) …
[1] INFO damage sent guid=91122467 seq=11 melee amount=89.0 centi=8900 (Data.BaseDamage) range=250 (GetAttackRange)
[2] INFO damage from enemy 91122467 melee: 89.0 applied hp 400.0 -> 311.0 (no local actor for the attacker)
[2] INFO damage: player hp via Health (TryApplyDamage had no effect) 89.0 (TryApplyDamage returned 0.00) hp 400.0 -> 311.0
```

Twelve swings landed in the window (12 `damage sent`, 12 `damage from enemy … applied hp`).
PASS on the described lane; `player body hit … -> sent` stays absent **by design** until the
game's own hit lands once, and this run says it does not land on a collision-less proxy. The
next step is the one R57 guessed at: give the proxy a query-only damage collision so the
game's own hit path can carry it.

### R79. The drop set: the tombstone crosses, and the key is stable enough to match

Two defects in the loot lane, both measurable:

* `DerivedLootKey` claimed a fresh coincidence ordinal on **every** ask (`DropOrdinals::Claim`
  hands out 0, 1, 2 …), so the scan recorded `#0`, the probe and `near:loot` re-derived `#1`,
  and the actor the player walked to was never the entry the ledger held — R69's "not the
  same one in time". The key is remembered per actor now, so every caller agrees.
* a drop's disappearance was tested only as "the object slot is empty", which lags a
  `K2_DestroyActor` by a GC and never fires for a pickup that hides in place. `LootActorGone`
  also accepts "was visible when recorded, hidden now", and `rig: drop probe` now prints
  `lootKey=yes path=…` instead of `lootKey=no`, because `DevDropProbe` polled a lever that
  latched `off` at the first frame (it polls like the kill lever now).

Measured:

```
[2] INFO loot: local pickup 14FA3F0F#0 (?) -- tombstoned for the partner      (×8)
[1] INFO loot: merged 1 entry rev=66 / loot: merged 14 entries rev=73          (×9)
```

The tombstone is produced and merged on both machines. `loot: hiding local drop … -- the
partner took it` is **NOT-REACHED**, and the reason is structural, not a bug: the enemy was
killed on one machine, so only that machine rolled the drop actor. The other holds the ledger
entry with nothing to hide. A two-sided proof needs both machines to kill the same spawner
(or the drop to be spawned on both from the enemy record), which is the next round's setup.

### R80. The reliable lanes: acks now flow, and eight kinds stopped being re-sent forever

R66's log carried `reliable send abandoned: kind=00000028` (and 0x23, 0x19) several times a
second for the life of the session, and it was read as "the peer is not answering". It was
not: the receiver was acking all along. The proof is now in the log on both sides, once per
kind per second:

```
[2] DBG reliable kind=00000023 seq=15892 received; ack sent
[2] DBG reliable kind=00000028 seq=15905 received; ack sent
```

with `abandoned=0` on both machines and three unmatched acks in the first seconds (the acks
for the datagrams sent before the peer joined). The traffic that produced the abandonments
was the malformed loot body storm (R75) plus the relay's 1 ms forwarding loop losing acks
under it; with the body fixed the reliable lanes are clean. Kept as instrumentation:
`ack for seq=N matched no slot (M so far)`.

### R81. Item 6 (the OBJECTS lane) found nothing to carry in either map

`ObjectSync=1` was on for the whole run and the lane published an empty set throughout:

```
[1] DBG objects sent rev=146 objects=0
[2] (no line matches 'object sync: tracking|objects sent')
```

while the same machine's scan named `Brass_Sci_Chest guid=2b16d684-… savable=yes value=0
opened=0` and `kind door Shop_Door_00_SM guid=e487497d-… opened=0`. That is the lane's own
rule working: it skips any instance with a non-empty `GUIDs` array, and both of these are
`savable=yes` with an authored GUID, so they belong to `ACTOR_STATES` (where the chest's
`opened=0` is already visible in the scan). `SG_Chest_BP2` — the R67 chest with no GUID — is
not in either map this run: `near:closedchest` resolved `Brass_Sci_Chest` after streaming
`200_AQ`, and that one is a savable. The two halves that matter are therefore:
`TotalChestsOpened` was **not** double-counted (`PASS` on both machines, no `2 -> 3`), a
second interaction produced no second intent (`PASS`), and the objects lane itself is
**NOT-REACHED** on this content — it needs a map with a GUID-less chest (or a placed
`PickedUp` pickup), which `SG_Chest_BP2` is and this run did not load.

### R82. Never-run features: what fired, and what does not exist in these maps

* **Cutscene — NOT-REACHED.** `scan: loose cutscene none in this world` in every scan of
  both maps, and `scan: loose sequence none in this world`. `RingLibraryBell` ran
  (`rig: cheat RingLibraryBell ran=1`) and changed only a flag (`flag NEW LibraryBell1 = 1`),
  so the cheat is not a cutscene. No cutscene manager was reachable to trigger.
* **Movers — NOT-REACHED.** `scan: loose elevator none in this world`, `… platform none in
  this world` on both machines; `near:elevator` resolved nothing. No mover class is loaded in
  the hub or in the streamed area this run.
* **Dialogue ends on death — PARTIAL.** A conversation started (`dialogue: local started`
  on the peer, PASS) but `HurtMe` with the god floor removed did not take the player to 0, so
  `dialogue: local death ended the conversation` never had a death to end it. The cheat's
  damage is not lethal on this build (the same class as `TryApplyDamage returned 0.00`).
* **Suspend guard — PARTIAL/PASS on the guard half.** The peer held the game's own block:
  `suspend: peer holds BlockSuspendSaves=1 -- the game's timed suspend save must not advance
  past the host`. The host then ran `SuspendSave` and `LoadSaveSlot` and really reloaded:
  `save layer re-initialised (new active save)`, `travel: local destination
  TempTeleporter/Ruins_Level_00 seq=5`, a new actor-state set. The repair lines
  (`suspend: host re-applying …`, `suspend: partner re-applied …`) did not appear because the
  reload was a *host* load, not a peer suspend load — §5.14 is written the other way round.
* **Boss bar — NOT-REACHED.** `scan: loose boss none in this world`, `… whale none` in every
  scan; `near:boss` resolved nothing.

### R83. OPEN — the host process died at the end of the run

`[1] process alive=False` after the `LoadSaveSlot` reload and ~250 live enemies. No `ERR`
line, no WER report, exactly the silent-exit class of R65; the peer survived. The reloaded
world re-created the whole enemy set (`enemy sync: 250 live enemies`) and every death it then
detected wrote a savable and an intent — the reliable table filled (`no free reliable slot;
dropping kind=00000020` dozens of times in one second, `actor-state intent given up … after 5
attempts`). Whether the host faulted or exited needs a `-abslog`/WER read on the next run; the
containment that matters for the lane (the swamped intent table reports itself rather than
growing) is already right.

### Bugs fixed this session

* **Chunked containers are decoded as prefixes** (R75): `WorldFlags`, `Inventory`,
  `ActorStates`, `Loot`, `Movers`; a partial trailing entry is still refused.
* **The loot body's `n` is the ledger size and the pass walks a cursor** (R75) — the
  68-drop ledger that neither end could read.
* **`Fn` is safe to default-construct** and refuses an unusable signature; `LogSignature`
  clamps (R76).
* **The travel follow packs the real signature** and the carrier's `Interact` runs it; the
  rig seeds the announcement the game clears (R77).
* **The described-damage lane defers only to a proven hit path**, and both damage/steering
  lanes measure the proxy body (R78).
* **The loot key is stable per actor**, a hidden pickup is a pickup, and the drop probe's
  lever polls (R79).
* **The reliable ack path is observable** on both sides and the unmatched-ack case is
  named (R80).

### Verdicts, item by item

| Item | Verdict | Key line |
|---|---|---|
| 1. Peer applies the host body, confirms, refreshes | **PASS on receive/confirm; NOT-REACHED on a value change** | `actor states applied=0 held=0 confirmed=1 failed=0 rev=562 n=142` + `actor-state refresh ok … opened=n/a` |
| 2. In-world teleport follow | **PASS** | `Teleporter_GR.Interact ran on the local pawn` -> `travel: followed Teleporter_GR/Ruins_Level_00 (seq 6)` |
| 3. Enemy attacks the partner body | **PASS (described lane)** | `TargetPlayer local player -> the remote body (read back)` -> `damage sent … 89.0 (Data.BaseDamage)` -> `damage from enemy 91122467 melee: 89.0 applied hp 400.0 -> 311.0` |
| 4. Loot drop disappears on the other | **PARTIAL** | `loot: local pickup 14FA3F0F#0 (?) -- tombstoned for the partner` + `loot: merged 1 entry rev=66`; the hiding half needs both machines to hold the drop |
| 5a. Cutscene | **NOT-REACHED** | `scan: loose cutscene none in this world` (both maps, every scan) |
| 5b. Movers | **NOT-REACHED** | `scan: loose elevator none in this world` / `platform none` |
| 5c. Dialogue ends on death | **PARTIAL** (conversation PASS, death not provoked) | `dialogue: local started` present; no death |
| 5d. Suspend guard | **PARTIAL/PASS on the guard** | `suspend: peer holds BlockSuspendSaves=1 -- the game's timed suspend save must not advance past the host` |
| 5e. Boss bar | **NOT-REACHED** | `scan: loose boss none in this world` / `whale none` |
| 6. Objects lane: chest + placed pickup | **FAIL (NOT-REACHED on this content)**; the two invariants PASS | `objects sent rev=146 objects=0` while `Brass_Sci_Chest … savable=yes`; no `TotalChestsOpened 2 -> 3`, no second intent |
| Session survival | **FAIL — the host exited at the end** (R83) | `[1] process alive=False`, peer PASS |

### What this session cost, and what it settled

Three of the four residuals Session 8 named are closed and each was a *different layer*: the
actor-state receive side was the relay's decoder, the travel follow was a verb the documents
invented, and the swing that hit nobody was a guard that trusted a hit path with no collision
to land on. The fourth (loot) is now carried on the wire and its tombstone crosses; what is
missing is a scenario where both machines hold the drop. The never-run features are not
failures of the code: both maps in this run load no cutscene manager, no mover, no boss, and
the only chests they expose are savables — the objects lane had nothing to carry, which the
scan says out loud.


---

## Session 10 — 2026-11-09 (the silent exit, the controlled change, and the content that is not there)

This branch is `sync-fix` at `8f51926` plus the Session 10 fixes. Every write path is on
(`ObjectSync=1`), `LogLevel=3`, `PlayerBody=1`; `tools/session10.ps1` is the whole run in one
process. It reads the roles from `session established`: **host=inst1, peer=inst2**. Fresh
pairs are seeded from `analysis/save-backup-before-live` with
`make_test_instance.ps1 -Force -Suffix qd1|qd2`.

### R84. R83's host exit was a stale NPC pointer in DialoguePump, and the dump proves it

The Session 9 run *did* leave a UE crash report even though no WER event existed:
`_test/inst1/Saved_qd1/Crashes/UE4CC-Windows-69152…/`. Its `CrashContext.runtime-xml`:

```
<ErrorMessage>Unhandled Exception: EXCEPTION_ACCESS_VIOLATION reading address 0x00000281b0df84dc</ErrorMessage>
<SecondsSinceStart>791</SecondsSinceStart>
ThreadName GameThread
WINMM 0x00007ffc29bc0000 + 29b2a
WINMM + 37bd0
```

`winmm.map` resolves `+0x29b2a` to `DialoguePump+0x66A`; cdb `.ecxr` gives the faulting
instruction:

```
winmm+0x29b2a: mov ecx,dword ptr [rdi+0Ch]   ds:00000281`b0df84dc=????????
```

`[obj+0x0C]` is `UObjectBase::InternalIndex` (`qdsubstrate.h kObjInternalIndex`). The pointer
`rdi=0x281b0df84d0` is `g_blockedNpc`, cached when the peer started a dialogue before the
suspend load. The new world replaced the object graph, so `ObjectIsLive(g_blockedNpc)` read a
freed object — exactly the trap `PuppetPump` already guards against.

**Fix.** `g_blockedWorld` is recorded with `g_blockedNpc`; `DialoguePump` compares
`GWorldPtr()` first and, on a change, drops the pointer and clears the id **without a read**
(`dialogue: blocked NPC dropped with its world (the old pointer is not read)`). `qdlog.h` also
flushes WARN/ERROR and every 64th line, so a crash cannot also cost the tail of the record.

Measured on the fixed build, fresh pairs, two full boots of the same scenario:

```
[1] 2:32:13.593 INFO dialogue: blocked NPC dropped with its world (the old pointer is not read)
[1] 2:32:13.382 INFO save layer re-initialised (new active save)
PASS [1] alive after the suspend reload
```

and the same four lines on the second boot; the two earlier boots of the same pair produced the
same result. Memory and handles are flat across the reload:

```
[1] pre-scenario: working=2856.8 MB private=3867.8 MB handles=2662
[1] before reload: working=2862.0 MB private=3872.7 MB handles=2662
[1] after reload:  working=2798.0 MB private=3888.2 MB handles=2670
[2] pre-scenario: working=2783.5 MB private=3795.2 MB handles=2662
[2] after reload:  working=2811.9 MB private=3823.2 MB handles=2662
```

No new crash report and no WER dump for these runs.

### R85. A second stale-UObject crash: `g_localPawn` across a hard map load

The 990_B33sment load after a deferred 900 transition killed the host in one run
(`UE4CC-Windows-11FA…`, 02:25:46):

```
EXCEPTION_ACCESS_VIOLATION reading address 0x00000234cdf0aacc
WINMM + 3b2b7   -> IsGameplayPawn+0x27
WINMM + 3cc96   -> LocalActionPump+0x36
WINMM + 36efe   -> HK_Tick+0x29E
```

`SendLocalPlayer` refreshed `g_localPawn`, but `LocalActionPump` runs **before** it and tested
the previous world's pawn. **Fix.** `HK_Tick` now clears `g_localPawn` on a `GWorldPtr()`
change and re-reads `LocalPawn()` before any pump. The final run survived four hard loads
(990, 997, 998, 999) with both machines alive.

### R86. The controlled value change: the lane is right, the content's verb is not

A fresh pair has `Chest_Merchant2` and `Shop_Door_00_SM` at `value=0 opened=0` in
`900_SipehaWorld`. The peer was placed on the door and the game's own door path was driven:

```
[2] 2:56:38.872 INFO rig: cheat FindDoorOpenTarget ran=1
[2] 2:56:40.878 INFO rig: cheat ForceOpenDoor ran=1
```

Neither `actor-state local change` nor `actor-state intent sent` followed, and `dev state:`
still read `opened=0`. The chest's own `Interact` is the same: `dev interact: target=
Chest_Merchant2 [Chest_Merchant_C] ... verb=Interact` runs and `opened=0` stays. The
interaction component's trace target is `-` (`scan: interact target - (component=found)`), so
the Blueprint's own open gate is not satisfied by the rig's direct verb call. The lane itself
is proven on the receive side: `actor states applied=0 ... confirmed=1 ... n=40`, and the
actor-state refresh prints `opened=1` for the entries the game itself changes (R75). **The
controlled chest/door open is NOT-REACHED; the next probe is the interaction component's
`Current Interact Actor` (the rig must set the same field the engine's trace fills), not
another verb name.**

Lane assignment is measured: the host sent `objects sent rev=… objects=0` for the whole run
(116 lines, last `objects=0`), the OBJECTS tracking set was empty on both machines, while the
same run's scan named 53 chest lines with GUIDs. The rule holds: a chest/door with a GUIDs
array belongs to ACTOR_STATES; OBJECTS carries none of them; nothing was carried by both. No
`TotalChestsOpened` double count appeared (`PASS` on both).

### R87. Loot hiding: both kills land, the boss has no drop

The fresh pair's `900_SipehaWorld` enemy is `Sipeha_BP_C` (`guid=BB49E51D`, hp 9999): both
machines landed local hits (`rig: hit enemy guid=BB49E51D ... ran=1 hp 9198.8 -> 8798.8`), and
the peer's copy is frozen (`FROZEN copy: its HP belongs to the host`), but a 9999-HP boss is
not killed by the 12 s window and rolls no item drop, so neither ledger learned a drop and
`loot: hiding local drop` was not reached. The hub's ordinary enemies do roll (the earlier runs
show `loot: drop …` and `loot: merged N entries`), but there the owner's copy is already dead
when the non-owner hits — R79's structural limit. **The hiding half remains NOT-REACHED; the
scenario needs a normal enemy both machines kill while both copies are alive.**

### R88. Content: the hard maps load on both machines; the mover is real; cutscene/boss are not

`LoadMapWithLoadingScreen` reaches each map on both machines (the peer's own lever plus the
HARD follow):

```
[1] 3:10:52 INFO rig: hard lever -> 997_FinalZoneCutscene LoadMapWithLoadingScreen ran=1
[2] travel: hard transition: loading 997_FinalZoneCutscene through GameInstance.LoadMapWithLoadingScreen
[2] travel: hard phase 2 map=997_FinalZoneCutscene
```

The same for `990_B33sment`, `998_QueenCutscenes` and `999_SipehaCutscenes`. 997 carries real
movers:

```
[1] 3:10:57.285 INFO movers: tracking FS_Light_Activatable372/PersistentLevel/997_FinalZoneCutscene/... id=F9587D01
```

so a mover exists and is tracked; the peer tracked it too in the same run, but no `movers:
applied N` row was produced because the actors are lights, not moving platforms. The
`scan: loose cutscene/sequence/boss/whale` lines are `none in this world` in every map, and no
`cutscene: local started` or `boss: guid=… is a boss` appeared. The Sipeha `FightStartTrigger`
(`SipehaFightStartTrigger`, savable value=0) accepted the `Interact` call and changed nothing.
**Cutscene sync and the boss bar are NOT-REACHED on this content; the mover lane is the one
feature these maps expose.**

### R89. Dialogue ends on death: PASS with the player class' own Kill

The host's `qd.pdmg` was refused by the receiver:

```
damage dropped: attacker guid=3A2DCA7E does not resolve on this machine
```

so the conversation stayed open and the peer's hard follow was deferred forever
(`travel: hard BEGIN ... deferred; the local player is busy (RunningDialogue)`). The rig now
has `qd.kill`, which calls the player class' own `FS_PlayerChar_C:Kill(false)`. Measured:

```
[2] 2:56:09.100 INFO rig: kill -> FS_PlayerChar_C.Kill ran=1
[2] 2:56:09.119 INFO dialogue: local death ended the conversation (end=1 ended=1); lock released
[1] 2:56:09.203 INFO dialogue: unblock Librarian_Ghost_NPC_BP [Librarian_Ghost_NPC_BP_C] BlockInteract= reenabled
```

**PASS** on every half: the conversation ends, the lock is released, and the partner's NPC is
re-enabled.

### Bugs fixed this session

* **R83** — `DialoguePump` drops `g_blockedNpc` on a world change without a read (the dump's
  `DialoguePump+0x66A`, `[obj+0x0C]`).
* **R85** — `HK_Tick` clears/re-reads `g_localPawn` on a world change before any pump
  (`IsGameplayPawn` called from `LocalActionPump`).
* **`qd.kill`** — the player class' own `Kill` lever, added for the dialogue-on-death lane.
* **Log flush** — WARN/ERROR and every 64th line are flushed (`qdlog.h`).
* **`tools/session10.ps1`** — the whole run in one process (R83 twice, items, WER LocalDumps,
  abslog, memory/handle samples).

### Verdicts, item by item

| Item | Verdict | Key line |
|---|---|---|
| 1. Host survives the suspend reload (R83) | **PASS** | `dialogue: blocked NPC dropped with its world (the old pointer is not read)` -> `PASS [1] alive after the suspend reload`, twice + the final build |
| 2. Controlled chest/door change both ways | **FAIL (NOT-REACHED)** | `rig: cheat ForceOpenDoor ran=1` then `dev state: … opened=0`; the interaction component's target is `-` |
| 2c. Lane assignment | **PASS** | `objects sent rev=… objects=0` (116 lines) with 53 GUID'd chest lines; OBJECTS tracking 0 |
| 3. Loot hides on the other machine | **PARTIAL** | both `rig: hit … ran=1`, but `Sipeha_BP_C` is a 9999-HP boss with no drop; no `loot: hiding local drop` |
| 4. Cutscene / mover / boss | **PARTIAL** | hard loads on both PASS; `movers: tracking FS_Light_Activatable372 … id=F9587D01` on 997; `scan: loose cutscene/boss none` in every map |
| 5. Dialogue ends on death | **PASS** | `rig: kill -> FS_PlayerChar_C.Kill ran=1` -> `dialogue: local death ended the conversation (end=1 ended=1); lock released` -> host `dialogue: unblock … reenabled` |
| Session survival | **PASS** | both processes alive at the end of the final run; no `ERR`/fault lines |

### What this session settled

R83 is closed and the dump names it: a stale NPC pointer, not the reliable table and not the
250 enemies. Fixing it exposed the same class one layer lower, `g_localPawn` across a hard
load, which the final run's four map switches survive. The controlled value change and the
loot hiding half are now blocked by *content* facts rather than by the lanes: the chest/door's
own open gate reads the interaction component's trace target, and the only enemy in the fresh
900 world is a 9999-HP boss that drops nothing. The hard-map content is reachable on both
machines and exposes a real mover; it does not expose a cutscene manager or a boss predicate in
these maps. The next run's first job is the interaction component's `Current Interact Actor`,
which is the one field both R86 and the door half are waiting on.

---

## Session 11 — 2026-11-09 (the interaction gate, and the wall behind it)

This branch is `sync-fix` at `4dfe9af` plus the Session 11 fixes. Every write path is on
(`ObjectSync=1`), `LogLevel=3`, `PlayerBody=1`. `tools/session11.ps1` is the whole run in
one process (`-Phase item1|item2|item3|all`); it reads the roles from `session established`:
**host=inst1, peer=inst2**. Fresh pairs are seeded from `analysis/save-backup-before-live`
with `make_test_instance.ps1 -Force`.

### R90. The interaction gate is now driven the way a key press drives it — and the object still does not open

R86 said the rig calls the verb but never fills the field the Blueprint gate reads. The
lever now does the whole press:

1. stands beside the target and **faces it** (`SetActorXform` + `ControlRotation`, because
   the interact trace is a forward trace from the view);
2. writes the interaction component's **`Current Interact Actor`** and reads it back;
3. resolves and dispatches the player's own compiled input event — by PREFIX, because the
   suffix is compiler-generated: `InpActEvt_Interact_K2Node_InputActionEvent`
   (`DefaultInput.ini` binds action `Interact` to `E`);
4. asks the game's own gate (`GetInteractInfo`) and reports `IsInteractible`;
5. only if the object did not change, falls back to the target's own verb.

Measured, peer, one press on the hub's checkpoint:

```
dev teleport near 'Checkpoint17' -> Checkpoint17 at 3871,-1723,51 dist=252; standing at 4010,-1735,51 offset=140
dev interact: faced Checkpoint17 at 4010,-1735,51 yaw -95 dist=252
dev interact: the game's own gate says IsInteractible=1 distanceOverride=0 override=0
dev interact: component target set (read back ok) -- the field the Blueprint open gate reads
dev interact: the player's Interact input event InpActEvt_Interact_K2Node_InputActionEvent ran -- a key press runs exactly this
dev interact: the input event ran and the object did not change (state 0); driving the target's own verb
```

So the three things R86 asked for are all on the record, on an object the game itself calls
interactible, with the pawn standing where it says to stand — and nothing changes. **The
gate is filled and the real input event runs; the interaction still does not happen.** What
that leaves is a narrower question than R86's: the Blueprint's interact handler is not
"read `Current Interact Actor`, call `Interact` on it". The component's trace target is also
*intermittent* — one scan named `Checkpoint17`, and a press ten seconds later read
`target=-` before the field was written — so the handler very likely consults state the
component's own `Update` maintains (and which the rig cannot fake by writing one pointer).

Two content facts are now measured rather than assumed, and each prints its own verdict:

```
[2] dev interact: the game's own gate says IsInteractible=0 ... -- a key press cannot open this object either   (Chest_Merchant2, 900_SipehaWorld)
[1] dev interact: the game's own gate says IsInteractible=1 distanceOverride=0 override=200                       (Shop_Door_00_SM, 900_SipehaWorld)
```

The chest Session 10 could not open is one the GAME refuses. The door that IS interactible
lives in a map the rig cannot stand in (R91).

### R91. A hard map load leaves the player at the origin, falling — so no press can land in those maps

`LoadMapWithLoadingScreen 900_SipehaWorld` succeeds on both machines (`ran=1`), and then:

```
3:44:53 scan: local at 4106,-1744,140 yaw 174        <- last hub position
3:45:03 scan: local at 0,0,16 yaw 0                  <- after the load
3:45:13 scan: local at 0,0,-2711 yaw 0
3:45:23 scan: local at 0,0,-1346 yaw 0
3:45:24 dev teleport near 'Chest_Merchant2' -> ... standing at -7139,-658,2920 offset=140
3:45:33 scan: local at 0,0,-422 yaw 0                <- the teleport did not stick
```

The map has no `PlayerStart`, so the game gives the player a default pawn at the origin;
it falls, dies on the kill-Z, respawns, and repeats. Every teleport written into that pawn is
undone by the next respawn. **Only `Ruins_Level_00` (the hub) has a `PlayerStart`**, which is
why the hub is the one world where the rig can stand next to an object and press — and the
hub carries no closed chest or door (its GUID'd one-shots are `2Coins_Item_BP`, `Herb_Item_BP`
and similar: overlap pickups, not E-interactables).

### R92. The rig can fault the GAME by hard-loading the map it is already in

The first `all` run died at 214 s: `[2] process alive=False`, and the peer left

```
UE4CC-Windows-C2CD747E42D60934B1E8BD9F5B147B36_0000
<ErrorMessage>Unhandled Exception: EXCEPTION_ACCESS_VIOLATION reading address 0x00000001e3050060</ErrorMessage>
<SecondsSinceStart>214</SecondsSinceStart>
QueensDomain-Win64-Shipping 0x00007ff643770000 + 1145580
QueensDomain-Win64-Shipping 0x00007ff643770000 + 1066134
QueensDomain-Win64-Shipping 0x00007ff643770000 + 10662c3
```

No `WINMM` frame anywhere in the stack: this is the game tearing a world down while it is the
current world, not the mod. The harness was asking both machines to `LoadMapWithLoadingScreen`
the map they were already standing in. The scenario now skips that load, and the final run
kept both processes alive to the end.

### R93. Loot hiding: the ledger crosses; the two machines still do not hold the same enemy

The hub drops are carried — peer:

```
[2] 4:27:49 loot: drop BB7FB743#0 = /Game/Items/Consumables/Herb/Herb_Item_BP.Herb_Item_BP_C
[2] 4:27:49 loot: drop 7A6931CE#0 = /Game/Items/Treasure/Coins/Coin_Item_BP.Coin_Item_BP_C
[2] 4:31:46 loot: merged 15 entries rev=2
```

but the two machines never held the same enemy, which is the setup item 3 needs:

```
host scan enemy: Mossy_Fighter_BP_C guid=A8D5C9E1 local-run=1 dist=77
peer scan enemy: Mossy_Fighter_BP_C guid=186F6D14 local-run=0
```

Each machine's own spawner created its own copy with its own key, so "kill it on the owner"
kills one machine's enemy and the other machine's ledger entry is for a different key. The
hiding half therefore stays NOT-REACHED, and the reason is now the identity of the copies
rather than the lane: **the enemy-parity lane (R66: three identity sources, the same key) did
not fire for these spawner enemies in the hub this run** (`local-run=0` on the peer with a
different GUID means no adoption happened).

### R94. Cutscene detection now sees a placed `LevelSequenceActor` — but 780's world never loaded for the scan

The detector only read `FS_PlayerChar.CurrentCutsceneManager`, which the sequences in
`780_SZ_Depths` (`SecretSwordGetSequence`), `760_SZ_AUT` and the cinematic maps never set:
they are started directly by altar/egg Blueprints. It now also asks each placed
`LevelSequenceActor`'s own `SequencePlayer.IsPlaying()` — the game's own state, not a guess —
and the sequencer branch no longer requires a gameplay pawn (the cinematic maps hand the
player a default pawn, which is exactly where a cutscene is due).

Measured in the final run: `780_SZ_Depths` loaded on both machines, and then

```
[1] 4:31:39 scan: local at 5412,-1675,947 yaw 36        <- still the HUB position
[1] 4:31:39 scan: loose cutscene none in this world
[1] 4:31:39 scan: loose sequence none in this world
[1] 4:31:39 scan: loose boss none in this world
[1] (no line matches 'boss:|enemy id: spawner|scan: kind enemy' since the step started)
```

The scan is still reading the pre-load world and then stops (R91's stranded pawn), so neither
the sequence nor the boss predicate was ever evaluated in 780. **Boss bar and cutscene sync
remain NOT-REACHED, and the blocker is R91, not the lanes.**

### Bugs fixed this session

* **The interaction lever does the whole press** (R90): face + `Current Interact Actor` write
  with read-back + the compiled `InpActEvt_Interact*` input event, with the target's verb as
  the fallback and a state fingerprint so a successful press is not driven twice.
* **`GetInteractInfo` is asked, once per target** (R90): `IsInteractible` / distance override
  are printed, so "the game refuses this object" is a measurement instead of an inference.
* **`near:<name>` falls back to any placed actor whose name or class matches** — needed for
  altars, sequence actors and spawners that the curated sets cannot name.
* **Cutscene detection includes a playing `LevelSequenceActor`** and does not require a
  gameplay pawn for that branch (R94).
* **`tools/session11.ps1`** — the whole run in one process, per item or all three in one boot,
  with a `$pid` collision, a report-on-trap and a `GetInteractInfo` fix found by running it.

### Verdicts, item by item

| Item | Verdict | Key line |
|---|---|---|
| 1. Peer opens a chest/door, both ways | **FAIL — mechanism PASS, effect NOT-REACHED** | `component target set (read back ok)` + `InpActEvt_Interact_K2Node_InputActionEvent ran` then `the input event ran and the object did not change (state 0)` |
| 1b. Chest content is openable at all | **FAIL (content)** | `the game's own gate says IsInteractible=0 ... -- a key press cannot open this object either` |
| 1c. A door that IS interactible | **NOT-REACHED (R91)** | `IsInteractible=1 distanceOverride=0 override=200` — in `900_SipehaWorld`, where the pawn cannot stand |
| 2. Loot hides on the other machine | **PARTIAL** | ledger crosses (`loot: merged 15 entries rev=2`); the two machines hold different enemies (`guid=A8D5C9E1` vs `guid=186F6D14`) |
| 3a. Cutscene | **NOT-REACHED (R91)** | `scan: loose sequence none in this world` after the 780 load |
| 3b. Boss bar | **NOT-REACHED (R91)** | no `boss:` line; the scan stopped with the stranded pawn |
| Session survival | **PASS** | both processes alive at the end of the final run; no `ERR`/fault lines |

### What this session settled

The interaction gate is no longer a mystery about which field to write: the field is written,
read back, and the same input event a key press runs is dispatched — and the game still does
not open the object. That moves the question from "did the rig fill the right field" to "what
else does the handler read", which is the next measurement. Behind it sits a harder fact:
**the rig can only drive a player that can stand somewhere, and after a hard map load the game
gives it a pawn at the origin that cannot**. Every remaining item — the door, the boss, the
sequence — is behind that one. The next session's first job is a map load that leaves a
`PlayerStart`, or a lever that spawns the player class into the loaded world the way
`PlayerBody` already spawns the partner's body.

---

## Session 12 — 2026-11-09 (where the arriving player stands)

This branch is `sync-fix` at `fbd27e4` plus the Session 12 fixes. Every write path is on
(`ObjectSync=1`), `LogLevel=3`, `PlayerBody=1`; `tools/session12.ps1` is the whole run in one
process. It reads the roles from `session established`: **host=inst1, peer=inst2**. Fresh
pairs are seeded from `analysis/save-backup-before-live` with `make_test_instance.ps1 -Force`.

Session 11 ended on R91: after a hard map load the player is a pawn at the origin, falling,
and every teleport is undone by the game's own respawn. That is exactly what the co-op
follower produced when the partner crossed, and it also blocked every other item.

### R95. The hard-load entry carries no spawn, and the game never calls it

`LoadMapWithLoadingScreen` is two parameters, and they are the whole story:

```
signature LoadMapWithLoadingScreen props=2 parms=24 ret@-1  Map: NameProperty  MapPackage: StrProperty
```

Its exec thunk (`0x00E58F70`) marshals exactly those two into the body (`0x00E34F30`), which
sets `bLoadingMap`, records `CurrentlyLoadingMapPackage` and starts the asynchronous world
load. Measured from the shipped image and the cooked pak:

* **no cooked package names it** — not one of 19,213;
* the only caller of the body is its own exec thunk (`site 00E5906F`), so **the game never
  performs this load at all**: the mod's follower is its only user;
* the game's own cross-world path is `OpenLevel`, imported by exactly seven packages
  (`FS_GameInstance`, `QueensDomain_GameMode`, `PauseMenu`, `DieUI`, `TitleScreenUI`,
  `DemoEndSplash`, `FS_Cheats`), i.e. menus, death and cheats — not a teleporter.

So there is no "what the leader's own travel passed" to capture. The question becomes what
the game puts in the destination world for the player to arrive at.

### R96. A world with no `PlayerStart` spawns the player at the identity transform

Measured over the pak's own export tables (`analysis/pak/code/probes/spawn_path.py`):

```
Ruins_Level_00             PlayerStart exports=1
900_SipehaWorld            PlayerStart exports=0
990_B33sment               PlayerStart exports=0
780_SZ_Depths              PlayerStart exports=0
760_SZ_AUT                 PlayerStart exports=0
100_GR                     PlayerStart exports=0
997_FinalZoneCutscene      PlayerStart exports=0
```

`AGameModeBase::RestartPlayer` therefore finds no start and spawns `DefaultPawnClass` at the
identity transform: the pawn at the origin, inside no geometry, which falls, dies on the kill
Z and respawns at the origin again. Measured on both machines, one line per 10 s:

```
[1] scan: local at 0,0,-1063 yaw 0
[1] scan: local at 0,0,-207 yaw 0
[1] scan: local at 0,0,93 yaw 0
[1] scan: local at 0,0,-1896 yaw 0
```

The `x,y` never move: the respawn writes the identity transform back every few seconds. That
is also why `dev teleport near 'Chest_Merchant2'` "did not stick" in Session 11 — the
teleport worked, and the next respawn undid it.

### R97. The spawn point for those worlds is CONTENT: the arrival-marker pair

The game's own arrival point is a placed actor, resolved by class with
`GetAllActorsOfClass` inside `FS_PlayerChar` (the class imports both markers):

```
900_SipehaWorld   PlayerTeleportTargetPos_C=1   at -3431,-5705,-1016
990_B33sment      B33smentTeleportPoint_C=1
```

`FS_PlayerChar` also carries `Array:B33smentTeleportPoint` (a cache of them) and the travel
verbs (`BeginTeleport`, `TeleportToTeleporterData`, `QueueTeleportLevelLoad(Location: Object)`,
`TeleportToSipehaArena`) — but **the press does not run any of them** (R100a). So both
machines resolve the marker locally from the world they just loaded; nothing about it needs
to be on the wire, and a wire field would only add a way for the two machines to disagree.

### R98. Both players now arrive standing — at the world's own marker

`qd::travel::ArrivalBoard` (pure, in `qdtravel.h`) decides; `ArrivalPump` (proxy.cpp)
answers its questions and performs one placement. Measured, the leader crossing and the
follower following:

```
[1] arrive: armed for 900_SipehaWorld -- a hard transition just landed here
[1] arrive: 900_SipehaWorld places the arrival marker PlayerTeleportTargetPos_C at -3431,-5705,-1016
[1] arrive: 900_SipehaWorld placed the player at PlayerTeleportTargetPos_C -3431,-5705,-1016 (the engine spawned it at the origin: this world places no PlayerStart)
[1] arrive: 900_SipehaWorld placed the player at partner -3347,-5703,-918 (the engine spawned it at the origin: this world places no PlayerStart)
[2] arrive: 900_SipehaWorld placed the player at PlayerTeleportTargetPos_C -3431,-5705,-1016 (...)

host at -3,347,-5,703,-912
peer at -3,437,-5,703,-918
PASS the two players are close: 90 cm
[1] scan: local at -3347,-5703,-912 yaw 0      (repeated every 10 s)
[1] scan: health 400.0
[2] scan: local at -3437,-5703,-918 yaw 0      (repeated every 10 s)
[2] scan: health 400.0
```

The position holds for the rest of the run (`drift over 22 s: 0 cm`), both processes are
alive, the handshake completes (`travel: both machines arrived in 900_SipehaWorld; host
releasing the pair to resume`), and the rig presses from there (`the game's own gate says
IsInteractible=0` — the chest's own verdict, Session 11's content fact, unchanged).

`Ruins_Level_00` is untouched: it places its own `PlayerStart`, the restored save position
stands there, and the board says so and stops:

```
[1] arrive: Ruins_Level_00 needs no placement (the world placed the player at 4106,-1744)
```

### R99. R92's crash is now refused at the one place that loads

Session 11's fault (a stack with **no** `WINMM` frame: the game tearing down its current
world) is reachable from three paths, so the guard is in `FollowHardTravel` itself:

```
[2] WARN rig: hard lever -> Ruins_Level_00 LoadMapWithLoadingScreen ran=0: already in Ruins_Level_00
[1] WARN rig: hard lever -> Ruins_Level_00 LoadMapWithLoadingScreen ran=0: already in Ruins_Level_00
PASS [2] process alive after being asked to reload its own world
PASS [1] process alive=True
PASS [2] process alive=True
```

Both machines refused, both survived, and the follower's own paths (`HARD_BEGIN`, the
in-world `wrong world` fallback) go through the same function.

### R100. Three ways the arrival silently did nothing, each fixed by a measured rule

The first four runs of this session arrived at a *worse* place than Session 11, and each
cause is a rule the code now carries with a comment naming it:

1. **The leader was never placed.** Its first poll after the map name changed read the pawn
   it had *before* the load — the hub position, `4106,-1744` — and concluded "the world
   placed the player". Two fixes: the previous world's pawn is remembered at the moment the
   world pointer changes (`g_arrivePrevPawn`), and a far position only counts once it has
   **held** for 2 s (`kPlacedSettleMs`).
2. **The far position can still be the void.** In `900_SipehaWorld` the player keeps the
   transform it had and drops out of it: `4106,-1744` is not a place in that world. The rule
   that separates the two worlds is the game's own behaviour, not a distance: a pawn that
   **falls** between two one-second polls has not been placed (`kFallCmPerPoll`). The hub
   restores the saved position and stands there; the void drops.
3. **The follower placed itself at the partner's pre-transition coordinates**, with the new
   world's Z (`placed the player at partner 4196,-1744,-612`), and stood in the void. A row
   from before the load is not a position in the new world: the partner anchor now requires
   the row to have arrived at least 3 s after *our* arrival (`kPeerSettleMs`).

### R100a. The press runs no travel verb — the game's own entry is the actor's `Interact`

SYNC-RESEARCH 7 left open which of the player's travel verbs the fast-travel UI takes. The
answer is none of them. The real interact lever, on the hub's `Teleporter_GR`, with
`qd.petrace` recording every new (object, function) pair:

```
dev interact: the game's own gate says IsInteractible=0 distanceOverride=1 override=300
dev interact: component target set (read back ok) -- the field the Blueprint open gate reads
PE new: InpActEvt_Interact_K2Node_InputActionEvent  on FS_Full_PlayerChar_C
dev interact: the player's Interact input event InpActEvt_Interact_K2Node_InputActionEvent ran
PE new: Interact  on Teleporter_GR [Teleporter_C]
dev interact: target=Teleporter_GR [Teleporter_C] component=found verb=Interact
```

and **no `travel verb:` line at all** — `BeginTeleport`, `TeleportToLocation`,
`TeleportToTeleporterData` and `QueueTeleportLevelLoad` never ran. So the teleporter branch is
the actor's own `Interact` (which opens `TeleporterUI` and, in this state, does nothing: the
game's own gate says the object is not interactible here). There is no player-side travel
half to capture on this path either, which closes §7's question rather than deferring it.

### R100b. The follower died in the enemy-record adoption during the load

The second run of this session lost the follower 116 s in, one second after its hard load:

```
<ErrorMessage>Unhandled Exception: EXCEPTION_ACCESS_VIOLATION reading address 0x0000020212904d8c</ErrorMessage>
<SecondsSinceStart>116</SecondsSinceStart>
WINMM 0x00007ffc29bb0000 + b52e      -> AdoptUnknownKey+0x12E
WINMM + 4ae9b                        -> OnEnemyRows+0x1CB
WINMM + 2c2eb                        -> DispatchInbound+0xF2B
WINMM + 39334                        -> HK_Tick+0x1C4
```

`AdoptUnknownKey` walks every local enemy record and calls `ObjectIsLive(c.actor)`. On a
*freed* UObject that is not a guard — it reads `[obj+0x0C]` and faults — and `DispatchInbound`
runs **before** the pumps, so the frame after a load delivers rows for a world whose actors the
records still name. This is R85's class (a stale `UObject` across a load), one lane over.
**Fix.** `g_enWorld` is recorded by `EnemyPump` when it purges the records, and the inbound
enemy paths (`OnEnemyRows`, `OnEnemyDetail`, `OnBoss`, the two enemy intents) refuse until the
records' world is the current one. Rows are repeated, so a dropped packet costs a tick.

### R101. Two harness bugs, both of which reported "the machine never connected"

* **The role line was parsed as its last character.** With exactly one match, PowerShell
  returns the *string*, and `$m[-1]` on a string is its last character, so `role=HOST` never
  matched and the script drove the wrong instance as the leader. Two runs were spent on it;
  the read now goes through `@()`.
* **`Get-Content` cannot read the live log.** The mod holds its handle with
  `FILE_SHARE_READ|FILE_SHARE_WRITE`; `Get-Content` asks for `FileShare.Read`, which the
  writer's access denies, and `-ErrorAction SilentlyContinue` turned the sharing violation
  into "no lines". The script now opens the file itself with the same sharing, and a mark is a
  **time of day** rather than a line count, so a read that disagrees by a line cannot move a
  verdict.

Both were caught by refusing to guess: a boot with no host/peer pair now says what it read
and exits instead of driving instance 1 as the host.

### Bugs fixed this session

* **The arriving player is placed** (R98): `ArrivalBoard`/`ArrivalPump`, armed by the map-name
  change itself, so the game's own transitions (which never pass through the handshake) are
  covered too. Marker first-class (`PlayerTeleportTargetPos_C`, `B33smentTeleportPoint_C`,
  `PlayerStart`), partner as the fallback, both bounded to 45 s / 5 placements.
* **A hard load of the current world is refused** (R99) — one guard in `FollowHardTravel`,
  reached by every caller (R92).
* **The enemy records are world-tagged** (R100b): the row, detail, boss and intent lanes do
  not touch an actor from the previous world.
* **`travel verb:` observation** (R100a): the game's own travel verbs are reported once each,
  so "the game took this path" is a measurement rather than an assumption.
* **`qd.petrace`** — the live lever for the ProcessEvent trace (baseline, then log new pairs).
* **`tools/session12.ps1`** — the whole run in one process, four items, with the two harness
  bugs above found by running it.

### Verdicts, item by item

| Item | Verdict | Key line |
|---|---|---|
| 1. The game's own travel half on an E press | **PASS (measured: none runs)** | `PE new: Interact on Teleporter_GR [Teleporter_C]` and no `travel verb:` line |
| 2. R92's crash on reloading the current world | **PASS** | `ran=0: already in Ruins_Level_00` on both machines, both alive |
| 3a. The leader arrives standing in `900_SipehaWorld` | **PASS** | `arrive: 900_SipehaWorld placed the player at PlayerTeleportTargetPos_C -3431,-5705,-1016` |
| 3b. The follower arrives beside it | **PASS** | `placed the player at partner -3347,-5703,-918`; `PASS the two players are close: 90 cm` |
| 3c. Both alive, session continues | **PASS** | `travel: both machines arrived ...; host releasing the pair to resume`; both processes alive |
| 4. Content reachable in the non-hub map | **PASS (standing)** | `drift over 22 s: 0 cm`; the press then runs from there (`IsInteractible=0` is the chest's verdict) |
| Session survival | **PASS** | both alive at the end; 0 error/fault lines |

25 of 25 recorded verdicts PASS in the final run.

### What this session settled

R91 is closed, and not by making the game place the player: by noticing that it never could.
`LoadMapWithLoadingScreen` carries no spawn, no cooked package calls it, and the worlds the
hard lane targets place no `PlayerStart` — so the engine's identity spawn was the whole of the
old behaviour, and the spawn point for those worlds is content, not protocol: one
`PlayerTeleportTargetPos` in `900_SipehaWorld`, one `B33smentTeleportPoint` in `990_B33sment`.
Each machine resolves it from the world it just loaded, which is why nothing had to be added
to the wire, and the two players now stand 90 cm apart on the game's own arrival point with
the position holding instead of being respawned to the origin.

The three ways the first attempts failed are worth keeping: a pawn from the previous world
looks like a placed player, a *far* pawn can be falling out of the void rather than standing
somewhere, and a position row can arrive after the arrival while still describing the world
before it. Each is a one-line rule now, and each names the run that found it.

Still open, and unchanged by this session: the chest in `900_SipehaWorld` is refused by the
game's own `GetInteractInfo` (`IsInteractible=0`), so a press still cannot open it — R90's
question is about the handler, not about standing. And the boss/cutscene items that were
behind R91 can now be attempted in a hard map, because the player can stand there.

---

## Session 13 — 2026-11-09 (the real lever on real content, and what the content refuses)

This branch is `sync-fix` at `036c6db` plus the Session 13 fix. Every write path is on
(`ObjectSync=1`), `LogLevel=3`, `PlayerBody=1`; `tools/session13.ps1` is the whole run in
one process, one phase per boot (`-Phase tp|hub|door|lootMossy|world900|items900|soakHub`).
It reads the roles from `session established`: **host=inst1, peer=inst2**. Fresh pairs are
seeded from `analysis/save-backup-before-live` with `make_test_instance.ps1 -Force`.

Session 12 closed R91 for `900_SipehaWorld` and `990_B33sment`. This session tried to use
that reachability on content: the door, the loot, the boss and the sequence.

### R102. A hard load of a streaming area spawns NO pawn — not even the identity pawn

`210_AQ_underground` has a `PlayerTeleportTargetPos` (`WhaleKingSuspendLocation`) and a
`WhaleKing_Spawner`, so it looked like the reachable boss world. A hard load of it leaves
both machines with **no pawn at all**:

```
[1] arrive: armed for 210_AQ_underground -- a hard transition just landed here
[1] arrive: 210_AQ_underground places the arrival marker PlayerTeleportTargetPos_C at -2469,-9502,-4247
[1] arrive: 210_AQ_underground ended with no pawn to place
[2] arrive: 210_AQ_underground ended with no pawn to place
```

`780_SZ_Depths` is the same:

```
[1] arrive: 780_SZ_Depths places no arrival marker of its own (...); waiting for the partner's position
[1] arrive: 780_SZ_Depths ended with no pawn to place
[2] arrive: 780_SZ_Depths ended with no pawn to place
```

No `scan: local at` line appears after the load on either machine. These maps are not
standalone worlds: loading their package starts a `UWorld` whose GameMode never spawns a
`DefaultPawnClass`, so the mod has nothing to place. `900_SipehaWorld` is different — it
does spawn the identity pawn and ArrivalPump places it at the marker. Session 11's "pawn
at the origin" was 900's behaviour; 210/780 do not even have that.

**Consequence:** the two worlds that carry a boss **and** a `LevelSequenceActor`
(`780_SZ_Depths`: GreenDragon/Josami spawners + `SecretSwordGetSequence`; `760_SZ_AUT`:
`ROSDragon_Spawner` + `Dek_Egg_Activation`) are unreachable by the hard lane, which is the
only world-load lane the rig has. The cinematic maps (997/998/999) place sequences and hand
a default pawn, but carry no boss enemy.

### R103. The teleporter gate is 0 at the hub nodes — distance 1, after UnlockAllTeleporters

The task's named route is the real E press on a `Teleporter_C`. It is refused by the
object's own gate, not by the rig:

```
dev interact: resolved 'TEMPTELEPORTER' -> TempTeleporter at 0,0,-110000 dist=32450
dev interact: faced TempTeleporter at 128,-54,-142450 yaw -66 dist=140
dev interact: the game's own gate says IsInteractible=0 distanceOverride=1 override=300 -- a key press cannot open this object either
PE new: Interact  on TempTeleporter [Teleporter_C]
```

and the same on `Teleporter_GR`, standing at its exact root (`dist=1` horizontally):

```
rig: cheat UnlockAllTeleporters ran=1
dev interact: faced Teleporter_GR at 13415,-1320,3325 yaw -90 dist=1
dev interact: the game's own gate says IsInteractible=0 distanceOverride=1 override=300
```

The `Interact` verb runs and no travel verb follows — no `travel verb:`, no
`travel: local destination`, no hard transition on either machine. `UnlockAllTeleporters`
changes neither node. The mod's own travel lever on the same node does run the game's
verb, but to the node's own location in the same world:

```
rig: travel lever -> Teleporter_GR/Ruins_Level_00 (LocationData resolved, dest field CurrentTeleporterTarget) TeleportToLocation ran=1
travel: local destination (none) seq=5
```

So the real-travel route is blocked by the teleporter's own state in this save. The node's
root is 9 m above the ground the player lands on, and the gate is a bool the Blueprint sets,
not the distance override.

### R104. BUG (fixed) — `near:<Name>` was resolved by KIND before NAME

`near:TempTeleporter` contains the word "teleport", so the curated `teleport` class walk
ran first and drove `Teleporter_GR` — a different actor 923 cm away. A scenario asking for
one named actor was silently given the nearest actor of that kind, and its gate was read as
if it belonged to the requested one. The resolver now tries an **exact instance name**
(case-insensitive) before every curated kind/class walk; the kind walks stay the fallback
for "some chest/door/teleporter". Measured after the fix:

```
rig: near 'TEMPTELEPORTER' matched the actor by exact name
dev interact: resolved 'TEMPTELEPORTER' -> TempTeleporter
```

Rebuilt, unit tests green, reinstalled, re-ran.

### R105. The shop door is interactible=1 and the real press runs — the handler still does not open it

`Shop_Door_00_SM` (`Shop_Door_00_BP_C`, GUID `e487497d-…`) is the one closed one-shot the
game itself calls interactible in the reachable worlds. The peer, after `GiveAllKeys` and
an exact-root placement, runs the whole press:

```
dev interact: the game's own gate says IsInteractible=1 distanceOverride=0 override=200
InpActEvt_Interact_K2Node_InputActionEvent ran -- a key press runs exactly this
dev interact: the input event ran and the object did not change (state 0); driving the target's own verb
dev interact: target=Shop_Door_00_SM [Shop_Door_00_BP_C] component=found verb=Interact
dev state: Shop_Door_00_SM [Shop_Door_00_BP_C] savable=yes guid=e487497d-... value=0 opened=0
```

and no `actor-state intent sent` follows. The host's press is identical. `GiveAllKeys`
(`rig: cheat GiveAllKeys ran=1`) and `ForceOpenDoor` (`ran=1`) change neither the door nor
the container. **R90's wall is unchanged: the gate is filled, the real input event runs,
the verb runs, and the Blueprint's handler still reads something the rig cannot fake.**

The lane itself is not the problem: the same run's pickup did cross and land —

```
actor-state intent applied: guid=C2275436-4310D7B1-C789EF93-2E4E56EB value=1
actor-state refresh ok Panacea_Pickup_Bp8 [Panacea_Pickup_Bp_C] index=0 ... opened=n/a
```

and the door's own gate and `opened=0` are the same before and after. A `TotalChestsOpened`
line never appears: opening a door does not touch the chest counter, and no chest the game
calls interactible was reachable.

### R106. Loot hiding: the shared enemy exists, but it drops nothing; the hub's enemies are per-machine

`900_SipehaWorld` is the one world where both machines hold the **same** enemy key:

```
[1] scan: kind enemy Sipeha_BP_C guid=BB49E51D ... local-run=1
[2] scan: kind enemy Sipeha_BP_C guid=BB49E51D ... local-run=0
[1] rig: hit enemy guid=BB49E51D amount=100000 ran=1 hp 9999.0 -> -90001.0 (local-run)
[1] enemy death recorded guid=BB49E51D respawn=0.0
```

and no `loot: drop` line appears on either machine — this enemy has no drop. In the hub
the two machines' nearest fighters are **different authored actors**:

```
[1] scan: kind enemy Mossy_Fighter_BP_C guid=A8D5C9E1 at 5552,-1668,961 local-run=1
[2] scan: kind enemy Mossy_Fighter_BP_C guid=186F6D14 at 5780,-1997,962 local-run=1
```

Targeting the class name on both (`near:Mossy_Fighter_BP_C`) still lands each machine
beside its own copy. A drop taken on the peer has no local actor on the host with that key,
so `loot: hiding local drop` cannot fire. The hiding half therefore remains NOT-REACHED,
and the reason is now two measured facts rather than one: the shared enemy has no drop,
and the hub's spawner enemies have different authored GUIDs per machine (R93, unchanged).

### R107. A 5-minute hub soak: no crash, no errors, and kill lists that diverge by design

Both machines hit the nearest locally-run enemy every 3 s for 300 s, re-teleported to the
nearest enemy every 20 s:

```
PASS [1] host alive at the end of the soak
PASS [2] peer alive at the end of the soak
host deaths: 104; peer deaths: 49
host-only: 0089DD85,05E77BA8,... (55 GUIDs)
peer-only: 30ED41A1
FAIL the two kill lists agree (105 unique GUIDs) -- host=104 peer=49
[1] error/fault lines=0
[2] error/fault lines=0
[1] WARN lines=43 classes=rig=38 (unlabelled)=5
[2] WARN lines=37 classes=rig=35 (unlabelled)=2
```

The divergence is R93's identity, not a lost row: the host runs its own 104 enemies and
records their deaths; the peer only holds the subset whose authored GUIDs match, so it
records 49. The only desync-shaped lines are the lanes' own self-healing ones:

```
actor-state intent refused (stale): based_on=242 last_sent=243
enemy own pump: a stale actor was skipped guid=467FE528; the record is dropped and the scan re-adds it
```

Both processes survived the full five minutes; no `ERR`, `EXCEPTION`, `ACCESS_VIOLATION`
or fault line.

### Bugs fixed this session

* **`RigFindNear` prefers an exact instance name** (R104): a named actor is no longer
  silently replaced by the nearest actor of a kind the name happens to contain. Rebuilt,
  `run_tests.bat` green, reinstalled, re-ran.
* **`tools/session13.ps1`** — the whole run in one process, one phase per boot, with three
  harness bugs found by running it: the standing check read a scan line from *before* the
  load (now a per-press mark); the loot step used `near:drop` (a placed pickup) instead of
  `near:loot` (the ledger drop); the exact-root press had to bypass the `near:` offset.

### Verdicts, item by item

| Item | Verdict | Key line |
|---|---|---|
| 1. Peer opens a chest/door, host applies, both ways | **FAIL (content handler)** | `IsInteractible=1` → `InpActEvt_Interact… ran` → `verb=Interact` → `opened=0`, no `actor-state intent sent`; `GiveAllKeys`/`ForceOpenDoor` change nothing |
| 1b. The lane itself | **PASS** | `actor-state refresh ok Panacea_Pickup_Bp8 … opened=n/a` after `actor-state intent applied: …` |
| 1c. `TotalChestsOpened` double count | **N/A (measured)** | no such line: a door does not touch the chest counter; no interactible chest was reachable |
| 2. Loot hides on the other machine | **NOT-REACHED** | shared 900 enemy `guid=BB49E51D` drops nothing; hub copies differ (`A8D5C9E1` vs `186F6D14`) |
| 3a. Boss bar on both | **NOT-REACHED (R102/R103)** | 210/780 `ended with no pawn to place`; teleporter `IsInteractible=0` at `dist=1` |
| 3b. Cutscene play/hold + joint resume | **NOT-REACHED (R102)** | same; the sequence worlds are the ones with no pawn |
| 4. 5-minute combat soak | **PARTIAL** | both alive, 0 errors; `host=104 peer=49` deaths, 105 unique, 2 self-healing stale lines |
| Session survival | **PASS** | both processes alive at the end; 0 `ERR`/fault lines |

### What this session settled

The interaction lever is not the missing piece for the door: the game's own gate says the
door is interactible, the same compiled input event a key press runs is dispatched, the
target's own `Interact` runs, and the door still does not change. That is R90's handler
question, now measured with the key in hand and the pawn at the door's exact root.

Two reachability facts are new and narrow the next session: a hard load of a streaming
area (210, 780) produces **no pawn at all**, and the game's teleporter refuses the E press
at both hub nodes even after `UnlockAllTeleporters`. The boss+sequence worlds are exactly
the ones behind those two walls. The next job is one of: a real `QueueTeleportLevelLoad`
route into 780/760 (the game's own travel), or a local-pawn spawn/possess lever for a world
that starts without a GameMode — Session 11's closing note, still the right one.

---

## Session 14 — 2026-11-09 (the kill sets converge; the streaming area's actors do not)

This branch is `sync-fix` at `f7a38e5`. Every write path is on (`ObjectSync=1`),
`LogLevel=3`, `PlayerBody=1`; `tools/session14.ps1` is the whole run in one process, one
phase per boot (`-Phase item1|item2|item3|all`). It reads the roles from
`session established`: **host=inst1, peer=inst2**. Fresh pairs are seeded from
`analysis/save-backup-before-live` with `make_test_instance.ps1 -Force`.

The session took the three NOT-REACHED items from Session 13 and, for each, first asked what
the two machines actually HELD rather than what one scan line said. Two of the three
questions were answered by that; the third turned out to be a content fact, not a lane.

### R108. The kill sets diverge because a death is addressed to a COPY, and there was no copy (fixed)

R107 counted deaths and found `host=104 peer=49` with 55 GUIDs host-only, and concluded the
divergence was "by design". It is not the co-op requirement. The requirement is per GUID: an
enemy killed on one machine ends up dead — or recorded dead and never respawned — on the
other, whichever machine killed it.

The first reproduction here has exactly R107's shape, taken from each machine's OWN
`enemy death recorded` lines over a 180 s host-only kill soak:

```
host recorded 172 death(s) itself; the peer recorded 96 itself
host-only (the peer held no copy for these): 76 -> 0F9C6E14,12978E27,14B7B0CE,...
peer-only:
```

76 keys where only the host ever recorded the death, 0 the other way — R107's 55, one scale
step up. The reason is in the name of the lane: a dead row is applied to a **copy**. A
spawner only spawns while ITS local player is in activation range, so on the peer those 76
enemies had no actor at all, the kill had nothing to land on, and when that player later
walked into range the enemy was alive again.

**Fix.** `qdenemy.h DeadLedger`: a death the other machine ran is remembered by KEY for
20 s, refreshed by each repeat of the dead row (so it lives exactly as long as the sender
keeps saying it, and expires when the sender stops — which is what a rest looks like), and
re-applied whenever a local copy appears, through the game's own damage path. Two companion
rules: the peer never publishes a key the other machine recorded dead (an echo strong enough
to erase the death over there), and a re-apply is rate-limited to once a second per enemy
(the first smoke run logged 924 lines for one key in 97 s).

`KillLog::kMax` went 64 → 512 for the same reason one layer down: a 104-death soak is larger
than the ring that re-broadcasts deaths, so 40 of them were never on the wire at all.

Measured, the same run, per-GUID FINAL STATE on both machines:

```
host keys=195 (dead 172); peer keys=198 (dead 172)
KILLED SET (dead on either machine): 172 unique GUID(s)
dead on the host but NOT on the peer: 0 ->
dead on the peer but NOT on the host: 0 ->
PASS every key ONLY the host ever recorded is DEAD on the peer -- carried from the wire=76/76
PASS the per-GUID kill sets DIFF TO ZERO -- host=172 peer=172 diff=0
```

A fact about the venue, measured and recorded rather than glossed: the seeded hub save has
its enemies **persisted at HP 0**, so the kill lever cannot add to the set —
`123 hit(s) attempted, 0 landed (ran=1), 123 found the target already at 0 HP`, every line
`ran=0 hp 0.0 -> 0.0`. The death set this run converges is therefore the persisted one
(which is what R107 was also counting), and item 3 below measures why a chosen enemy cannot
be killed through damage at all.

### R109. A streamed area is entered, not loaded — and its actors still do not arrive

R102's `arrive: ... ended with no pawn to place` is a property of the LANDING, not of the
area. `780_SZ_Depths`, `760_SZ_AUT`, `210_AQ_underground`, `100_GR` — and, measured here,
`900_SipehaWorld`, `990_B33sment` and the 997/998/999 cutscene maps — are **sublevels of the
persistent world**. The live level table names them all:

```
8:16:54.956 INFO level name: 780_SZ_Depths from WorldAsset
8:16:54.956 INFO level name: 760_SZ_AUT from WorldAsset
8:16:54.956 INFO level name: 900_SipehaWorld from WorldAsset
```

So the check is exact rather than a list: a name the world this machine is IN streams is a
sublevel by construction, and loading its package as a world is the pawn-less path R102
measured. `FollowHardTravel` now asks that question before the loader, and the answer is the
game's own mechanism — load it and SHOW it:

```
[1] stream travel: 780_SZ_Depths is a sublevel of this world -- loaded and SHOWN (visible=1 loaded=1)
[1] rig: hard lever -> 780_SZ_Depths streamed=1 (a sublevel of this world: loaded and SHOWN; ...)
[2] stream travel: 780_SZ_Depths is a sublevel of this world -- loaded and SHOWN (visible=1 loaded=1)
PASS [1] no "ended with no pawn to place" line (the R102 failure) -- count=0
PASS [2] no "ended with no pawn to place" line (the R102 failure) -- count=0
PASS [1] the host is standing somewhere real in 780 (a pawn exists)  4,019,-1,732,148
PASS [2] the peer is standing somewhere real in 780 (a pawn exists)  4,019,-1,732,148
```

**R102 is closed**: both players are inside a streamed area with a pawn, and no hard load
happened. Two fixes were needed to get the SHOW to mean anything, and the first one is the
interesting measurement:

* `SetShouldBeLoaded` through ProcessEvent produced `bShouldBeVisible=1` and **not one actor
  in the world 90 s later** — several `ULevelStreaming` subclasses override that thunk with a
  bare `ret`. `ForceLevel` now writes the engine's own `bShouldBeLoaded` bit and calls the
  one body this file already proved honours it.
* The game's manager hides out-of-range levels on its own schedule, and a hidden level's
  actors are not in the world at all (the absent shop merchant). A PINNED level (one a
  streamed travel put the rig inside) is now vetoed at the same two setters the union already
  guards, so it is not hidden a second after being shown.

What did not follow is the content. With `loaded=1 visible=1` on both machines, 780's own
actors still do not materialise:

```
[1] scan: loose sequence none in this world
[1] scan: loose boss none in this world
dev teleport near: no live actor matches 'SecretSword_Altar_BP'
```

and the anchor that DID resolve, `Checkpoint19`, is 252 cm from the player's saved hub
position — so it is hub geometry, not 780's. **The boss and the cutscene remain
NOT-REACHED, and the blocker has moved one layer down**: the sublevel's `ULevelStreaming`
flags are set but its package has not been streamed into the world, so no actor from it
exists to detect, press or fight. The next lever is the game's own stack
(`AddLevelListToStack` with `GreenRuins_SZ`, whose 13 levels include 780) applied by the
manager's per-frame pass — **not** `FlushLevelManagerActions`, which replaces the visible set
(measured 21 → 11) and would drop the player through the hub.

### R110. The two machines DO hold the same enemies — R106's "different GUIDs" was a nearest-enemy read

Item 3 needed an enemy both machines hold by the same key. R106 concluded from two `scan:
kind enemy` lines that the hub's copies differ (`A8D5C9E1` vs `186F6D14`). That line reports
the NEAREST local enemy, and the two players were standing in different places, so it was
comparing two different enemies. The full key sets intersect:

```
host keys=205; peer keys=183; COMMON keys ALIVE WITH HP on both=22
common: FDAD4F86,F9B1FAF9,1C1AF92B,05E77BA8,FAD4EE3D,A9824174,F11060B0,72D6234C,...
PASS the two machines hold at least one enemy by the SAME key -- 22 common key(s)
```

`qd.tp guid:<8 hex>` then puts BOTH players on ONE enemy — a name resolver cannot, because
`near:<class>` lands each machine beside its own nearest copy — and four of those shared keys
were killed on the host and recorded on both machines. **The identity half is proven.**

The drop half is not, and the reason is content: none of the four classes dropped anything on
either machine.

```
key=1C1AF92B: loot:drop lines host=0 peer=1
key=05E77BA8: loot:drop lines host=0 peer=0
key=F11060B0: loot:drop lines host=0 peer=0
key=72D6234C: loot:drop lines host=0 peer=0
```

Damage could not produce the death either, which is the second measured fact behind this:
most of this build's enemy classes do not implement `TryApplyDamage`, so `ApplyDamageToActor`
cannot even build its call frame (`ran=0`), and the death had to be produced through the
enemy's own `Kill` (`rig: enemykill -> key=... the enemy's own Kill ran`). A class that will
not take damage and does not drop is not a loot target; the hiding half stays NOT-REACHED
with the blocker now named as a class property rather than a lane.

### Bugs fixed this session

* **`qdenemy.h DeadLedger`** (R108): a death the other machine ran is remembered by KEY and
  applied when a copy appears, so a kill no longer needs a local actor to land on.
* **`KillLog::kMax` 64 → 512** (R108): one soak records more deaths than the old ring could
  hold or re-broadcast.
* **`EnemySendPump` never publishes a key the other machine recorded dead** (R108) — no echo
  that could erase the death over there; and the ledger re-apply is rate-limited (R108).
* **`ApplyEnemyRecordIntent` feeds the ledger** (R108): `hp 0` is a death, `hp > 0` a revive.
* **`StreamTravelTo` + `FollowHardTravel`** (R109): a name the current world streams is
  loaded and SHOWN as a sublevel; the world load is refused with the reason in the log.
* **`ForceLevel` writes `bShouldBeLoaded` and drives the honouring body** (R109): the
  ProcessEvent thunk is a no-op on some subclasses and the level never loaded.
* **A pinned level is not hidden or unloaded** (R109): both setters veto it, like the
  partner's levels.
* **Four rig levers** — `qd.enemies` (per-GUID state dump, the measurement item 1 needed),
  `qd.tp guid:<key>` (both players to ONE enemy), `qd.sequence` (the sequencer's own `Play`),
  `qd.enemykill` (the enemy's own `Kill`, for classes with no damage entry).
* **`tools/session14.ps1`** — the whole run in one process, with three harness bugs found by
  running it: a `Verdict` given an ARRAY instead of a bool, a soak re-teleport that repeated
  the same line the tp lever ignores, and a level anchor list that matched hub geometry.

### Verdicts, item by item

| Item | Verdict | Key line |
|---|---|---|
| 1. Every kill on one machine is dead on the other (per GUID) | **PASS** | `the per-GUID kill sets DIFF TO ZERO -- host=172 peer=172 diff=0`; `carried from the wire=76/76` |
| 1b. R107's condition reproduced first | **PASS** | `host-only (the peer held no copy for these): 76` |
| 1c. Does the kill lever itself add deaths? | **MEASURED, NO** | `123 hit(s) attempted, 0 landed (ran=1), 123 found the target already at 0 HP` |
| 2a. A streamed area is entered, not loaded | **PASS (R102 closed)** | `stream travel: 780_SZ_Depths ... loaded and SHOWN (visible=1 loaded=1)`; `no "ended with no pawn to place" -- count=0` on both |
| 2b. Both players inside 780 with a pawn | **PASS** | `4,019,-1,732,148` on both machines |
| 2c. Boss detection + bar on both | **NOT-REACHED** | `scan: loose boss none in this world`; `GreenDragon_Spawner` unresolved |
| 2d. `SecretSwordGetSequence` play/hold/joint resume | **NOT-REACHED** | `scan: loose sequence none in this world`; `no live actor matches 'SecretSword_Altar_BP'` |
| 3a. An enemy both machines hold by the SAME key | **PASS** | `COMMON keys ALIVE WITH HP on both=22` |
| 3b. Both players at THAT enemy, killed, recorded on both | **PASS** | `tp[1]/tp[2] -> guid:1C1AF92B` then `enemy death recorded guid=1C1AF92B` |
| 3c. `loot: hiding local drop` on the other machine | **NOT-REACHED (content)** | `loot:drop lines host=0 peer=0` on all four shared keys |
| Session survival | **PASS** | both processes alive at the end of all three runs; 0 `ERR`/fault lines |

### What this session settled

The kill divergence was never "by design". It was a death addressed to a copy on a machine
that had no copy, and once the death is remembered by KEY the two sets agree exactly —
172/172, diff 0, including all 76 keys only one machine ever recorded. That is the co-op
requirement, and it is now a number.

The streaming question split in two. The LANDING is solved: a sublevel is entered the way the
game enters it, both players keep a pawn, and no `ended with no pawn to place` line exists
any more. The CONTENT is not: setting the `ULevelStreaming` flags is not the same as the
engine streaming the package in, so 780's altar, dragon and sequence are still absent even
with `loaded=1 visible=1`. That is now a single, sharply defined question — drive
`AddLevelListToStack` with `GreenRuins_SZ` and let the manager's own per-frame pass apply it —
and it is the next job.

Item 3 changed shape rather than closing. The machines share enemy keys (22 of them), so R106's
"different GUIDs" was a read of the nearest-enemy line, not a parity failure; `qd.tp guid:`
can put both players on one enemy and the kill lands on both. What stops the hiding proof is
content: the shared classes neither take damage nor drop. So the remaining work there is to
find an enemy class with a drop table — not to fix a lane.


## Session 15 — 2026-11-09 (the game's own level list carries the area; its actors arrive)

This branch is `sync-fix` at `20c7ad4`. Every write path is on (`ObjectSync=1`), `LogLevel=3`,
`PlayerBody=1`; `tools/session15.ps1` is the whole run in one process, one phase per boot
(`-Phase item1|boss|item4|all`). Roles are read from `session established`: **host=inst1,
peer=inst2**. Fresh pairs are seeded from `analysis/save-backup-before-live`.

Session 14 ended on a sharp question: 780_SZ_Depths was `loaded=1 visible=1` and had no
actors. R109 named the next lever — the game's own `AddLevelListToStack` — and named the
list to push, `GreenRuins_SZ`. This session took that lever, and the first thing it did was
check the list against the content.

### R111. The list that carries 780 is `SZ_Depths`, not `GreenRuins_SZ` (corrected fact)

Read out of the shipped pak — the ASCII level names in each `UFS_LevelList` `.uexp` — the
`Greenruins_SZ` asset does **not** contain 780 at all (it carries `700_SZ_GR` and
`740_SZ_Prison`). The list the area's own trigger sets does:

```
SZ_Depths = 070_UpperShell 710_SZ_AQ 740_SZ_Prison 750_SZ_MAUS2 760_SZ_AUT
            770_SZ_Swamp 775_JigekiTemple 780_SZ_Depths 790_SG 060_Ocean_Shadow
            000_Shared_Base_Lighting
```
and `LevelManagerTrigger10` sets only `SZ_Depths`. Session 14's list would have been a call
that succeeds and changes nothing. The mapping is now **derived from membership**
(`qdstream.h LevelListForArea`), so a list that does not contain the area can never be
returned, and `test_stream.cpp` holds that invariant.

### R112. `AddLevelListToStack` runs with the real signature, and the engine loads the level

The function is a UFUNCTION on `FS_LevelManager` (confirmed from the Blueprint import table:
`LevelManagerTrigger_C` and `Checkpoint_C` both import it with outer `FS_LevelManager`). The
live signature, logged by the module, is exactly the five parameters the module fills:

```
signature AddLevelListToStack props=5 parms=24 ret@-1
    WorldContextObject : ObjectProperty @0
    List               : ObjectProperty @8
    bTrigger           : BoolProperty   @16
    bOverridePriority  : BoolProperty   @17
    PriorityOverride   : IntProperty    @20
```

Before the fix, both machines reported `visible=1 loaded=1` and nothing else. After it:

```
[1] stream travel: 780_SZ_Depths is a sublevel of this world -- level list 'SZ_Depths'
    added to the manager's stack (AddLevelListToStack ran); the manager's own pass loads
    and shows it visible=1 loaded=1 engineLoaded=1 engineVisible=1
[1] rig: level engine state 780_SZ_Depths LoadedLevel=1 actuallyVisible=1
```

`LoadedLevel`/`actuallyVisible` are the engine's own `ULevelStreaming+0x128` and
`ULevel+0x1F4 bit 0x20`, not our flag: the package is streamed and the level is in the world.

### R113. The union veto livelocked the manager's queue (fixed)

The first run with the push still showed no actors — and the log said why: the manager
applies **one level per frame and waits for that level's real visibility to match what it
asked**, so a vetoed hide never completes and no later entry is ever applied.

```
BLOCK hide of 125_GR_CubeHall (the partner is in it); vetoes=19 ... vetoes=28935
```

28,000 vetoes in ten minutes and 780 never applied. The veto now allows ONE hide through per
level after a repeated burst (`VetoWouldStall`), which is enough to move the manager past the
entry it was waiting on; the pump re-shows the level on its next pass.

### R114. Streamed-level actors hang off a chain the outer walk does not reach (fixed)

With the level genuinely loaded and visible, the scan still reported `none`, because
`ActorOuterReachesWorld` walks Outer for the `UWorld` object and a streamed sublevel's actors
do not reach it — the same fact `FindTravelActor` already carries a special fallback for.
The scans now ask `UWorld::Levels` (the engine's own list of the persistent level plus every
streamed-in one) whether the actor's level is in this world. Measured consequence:

```
[1] dev teleport near 'SecretSword_Altar_BP' -> SecretSword_Altar_BP at -41053,13231,-15552
    dist=50099; standing at -40920,13187,-15552 offset=140
[2] dev teleport near 'SecretSword_Altar_BP' -> SecretSword_Altar_BP at -41053,13231,-15552
    dist=50099; standing at -40920,13187,-15552 offset=140
[1] scan: loose sequence SecretSwordGetSequence [sequence] at -41032,13211,-15502 dist=123
[2] scan: loose sequence SecretSwordGetSequence [sequence] at -41032,13211,-15502 dist=123
[1] dev teleport near 'GreenDragon_Spawner' -> GreenDragon_Spawner at -28573,754,-13118
[2] dev teleport near 'GreenDragon_Spawner' -> GreenDragon_Spawner at -28573,754,-13118
```

**Both machines hold 780's own actors, at the same coordinates, with the players standing
inside the area.** R109's content half is closed.

One stability bug was found and fixed on the way: giving the friend a list for **every**
level it published pushed both big lists, the union reached 24 sublevels, and a game process
died. The entry is per AREA, not per level; the union's job for the rest is residency.

### R115. The boss: the actor is there and owned, but this build's predicates cannot name it

The Green Dragon materialises at the spawner without a proximity trigger:

```
[1] scan: kind enemy Dragon_Green_BP_C guid=17181166 at -28573,754,-13028 local-run=1
[2] enemy 88744DC6 DRIVEN by owner (Paused ok=1, bIsAIDisabled ok, tick ok, controller yes)
```

but the boss lane never speaks, and the probe says why for every enemy class in the world:

```
boss probe: Looker_Base_BP_C part=-1 info=-1 -> boss=0 ... (every class)
```

Neither `Enemy_IsPartOfBossEncounter` nor `Enemy_GetBossInfo` exists on the enemy classes.
The probe now asks the **spawner** as well (that is where the encounter is authored), and the
lane runs for anything not frozen. It still does not report this dragon: on the spawner side
the predicate is also absent in this build. So `boss: … is a boss` and
`ActivateBossHPBar` remain **NOT-REACHED**, with the blocker now named as an API gap rather
than a lane.

The damage lane is content-blocked on the same class, exactly as R108 measured for the hub:

```
[1] rig: hitguid -> key=17181166 LOCAL hit amount=200.0 ran=0 hp 3500.0 -> 3500.0
[2] rig: hitguid -> key=17181166 the other player's hit sent amount=200.0 frac=65535
    (no local copy; the owner applies it)
```

`ApplyDamageToActor` cannot build its frame (`ran=0`), so HP does not move, and the peer holds
no copy of this GUID (its spawner never activated there), so there is no shared HP to
converge. The game's own death entry does work and is recorded on the owner:

```
[1] rig: enemykill -> key=17181166 the enemy's own Kill ran
[1] enemy death recorded guid=17181166 respawn=0.0
```

The peer can only record that death when it has a copy or the death row reaches its ledger;
it did not within this run.

### R116. The cutscene: started on one machine, held on the other

The real interact lever reaches the altar (`dev interact: the game's own gate says
IsInteractible=1`) but the altar's own gate does not start the sequence (R90/R105). The
sequencer's own `Play`, by name, does:

```
[2] rig: sequence lever -> SecretSwordGetSequence Play ran on its own SequencePlayer
[2] cutscene: local started SecretSwordGetSequence
[1] cutscene: holding the local player for the partner's cutscene (enter=1 lock=1)
```

The play + hold half of the cutscene requirement is proven. The joint resume did not occur
inside the 220 s window: `SecretSwordGetSequence` was still playing when the run ended.

### Bugs fixed this session

* **`stream::LevelListForArea` + membership tables** (R111): the area → list answer is
  derived from the pak-read `UFS_LevelList` membership, so `Greenruins_SZ` (which does not
  contain 780) can never be selected; `test_stream.cpp` holds the invariant.
* **`LoadLevelListAsset` + `AddLevelListToStackLive`** (R112): the game's own entry into a
  streamed area, through ProcessEvent with the declaration-order parameters discovered from
  the live signature; no `FlushLevelManagerActions` (it replaces the visible set).
* **`StreamTravelTo` pushes the list** (R112) instead of only writing
  `bShouldBeLoaded`/`bShouldBeVisible`, which R109 proved is not a load.
* **`VetoWouldStall`** (R113): a one-shot allow per level, so a repeated veto can no longer
  stall the manager's one-level-per-frame queue (28,000 vetoes measured).
* **`ActorInThisWorld`** (R114): `UWorld::Levels` membership instead of the Outer walk, so a
  streamed sublevel's actors are visible to the scans.
* **The co-op list push is per AREA, not per level** (R114): pushing a list per published
  level loaded the whole world (union 24) and killed a process.
* **`EnemyIsBoss` asks the spawner too, and logs a probe per class** (R115); `BossPump`
  now serves any machine that runs the enemy (`O_HANDOFF` included), not only `O_LOCAL`.
* **`qd.hitguid`** (R115): one hit on a named enemy from the local player; on a frozen copy
  it goes through the existing `I_ENEMY_HIT` intent so the OWNER applies it.
* **`qd.list`** (R112): the raw level-list lever, so "the list is wrong" and "the manager
  ignored the list" are separable in the log.
* **`tools/session15.ps1`** (this run): one process per phase, with the boss probe and the
  per-GUID HP diff; and **`test_stream.cpp`** (20 checks) beside the list mapping.

### Verdicts, item by item

| Item | Verdict | Key line |
|---|---|---|
| 1a. The area's own level list is pushed on both machines | **PASS** | `level list 'SZ_Depths' added to the manager's stack (AddLevelListToStack ran)` on inst1 and inst2 |
| 1b. The engine loads/shows the sublevel | **PASS** | `rig: level engine state 780_SZ_Depths LoadedLevel=1 actuallyVisible=1` |
| 1c. 780's actors exist on BOTH machines | **PASS** | `SecretSword_Altar_BP at -41053,13231,-15552` and `SecretSwordGetSequence [sequence]` on both |
| 1d. Players standing inside the area, session alive | **PASS** | both at `-40920,13187,-15552`; both processes alive, 0 error lines |
| 2a. Boss actor exists and is owned | **PASS** | `scan: kind enemy Dragon_Green_BP_C guid=17181166 local-run=1`; peer `DRIVEN by owner` |
| 2b. `boss: … is a boss` + bar on both | **NOT-REACHED (API)** | `boss probe: … part=-1 info=-1 -> boss=0` for every class; no predicate on enemy or spawner |
| 2c. Damage from both players on one HP | **NOT-REACHED (content)** | `rig: hitguid … LOCAL hit … ran=0 hp 3500.0 -> 3500.0`; the class has no `TryApplyDamage` |
| 2d. Death recorded on both | **PARTIAL** | owner: `enemy death recorded guid=17181166`; the peer holds no copy of this GUID |
| 3a. Sequence starts on one machine | **PASS** | `cutscene: local started SecretSwordGetSequence` |
| 3b. The partner holds | **PASS** | `cutscene: holding the local player for the partner's cutscene (enter=1 lock=1)` |
| 3c. Joint resume | **NOT OBSERVED** | still playing at the end of the 220 s window |
| 4. 760_SZ_AUT (ROSDragon_Spawner, Dek_Egg_Activation) | **NOT-RUN** | same list (`SZ_Depths` carries 760), same lever |
| Session survival | **PASS** | both processes alive at the end of every run; 0 `ERR`/fault lines |

### What this session settled

The content half of R109 is closed. A streamed area's actors arrive when the area's own level
list is pushed onto the manager's stack — not when its `ULevelStreaming` flags are written —
and the list that carries 780 is `SZ_Depths`, which the pak proves and the code now derives
from membership. Both machines, at the same coordinates, standing inside 780: the altar, the
`SecretSwordGetSequence` actor and the Green Dragon spawner all resolve, and the cutscene
starts on one machine and is held by the other.

Two new questions are named rather than left as silence. The first is the boss bar: this
build exposes neither `Enemy_IsPartOfBossEncounter` nor `Enemy_GetBossInfo` on the enemy
classes OR on the Green Dragon's spawner (`part=-1 info=-1`), so nothing this module can ask
the game will say "this is a boss". The second is the damage lane on the dragon: the class
does not implement `TryApplyDamage`, so `ApplyDamageToActor` returns `ran=0` and its HP
cannot be moved by a hit — the same content property R108 measured for the hub's classes.
Both are content/API facts, not lanes, and they are what the next session should attack.
## Session 16 — 2026-11-09 (the missing enemy, the one HP, and the release)

This branch is `sync-fix` at `9e6ca43`. Every write path is on (`ObjectSync=1`), `LogLevel=3`,
`PlayerBody=1`, and — new this session — `MirrorSpawn=1`; `tools/session16.ps1` is the whole
run in one process, one phase per boot (`-Phase item1|boss|cut|all`), each phase in its own
instance root so two phases cannot corrupt each other. Roles are read from
`session established`: **host=inst1, peer=inst2**. Fresh pairs are seeded from
`analysis/save-backup-before-live`.

Session 15 ended on three named questions: the Green Dragon exists only on the machine whose
spawner activated; its class "does not implement `TryApplyDamage`"; and no boss predicate in
this build answers. This session took all three, and the first thing it established is that
two of those three facts were wrong.

### R117. The peer could not hold the dragon because no lane carries a CLASS (fixed)

The requirement is not "ship rows for it" — every enemy lane (rows, detail, boss, damage) is
addressed to a **copy**, and on the machine whose spawner never activated there was no copy
to address. The spawner only spawns while *its own* machine's player is inside its activation
range (`PlayerIsInActivationRange` reads `GetPlayerCharacter`, i.e. player 0 of that machine),
so the dragon simply did not exist on the peer:

```
[1] scan: kind enemy Dragon_Green_BP_C guid=17181166 at -28573,754,-13028 local-run=1
[2] (nothing: the peer held no actor for this GUID)
```

The rows already said the key was alive and where. What a row cannot carry is **which class to
construct**, and it cannot be derived — a summon's key hashes its summoner and a spawn point,
and a placed key hashes a path, so neither names a package. So the class rides the wire in a
new reliable body, `ENEMY_SPAWN (0x2B)`: the key, the owner's transform and HP fraction, and
the actor's class asset path. Measured, both machines:

```
[1] enemy spawn: described guid=17181166 class=/Game/Enemies/Dragon/Dragon_Green_BP.Dragon_Green_BP_C
[2] enemy spawn: received guid=17181166 class=/Game/Enemies/Dragon/Dragon_Green_BP.Dragon_Green_BP_C ... (no record here)
[2] enemy spawn: class resolved /Game/Enemies/Dragon/Dragon_Green_BP.Dragon_Green_BP_C (by asset path)
[2] enemy mirror: the engine constructed the actor (FinishSpawning returned it)
[2] enemy spawn: constructed guid=17181166 ... at -28573,754,-13028 hp=65535 -- AI off, driven by the owner's rows, never persisted (mirrors=1)
[2] enemykey 17181166 ALIVE local=0 src=0 hp=3500.0 mirrored=1 cls=Dragon_Green_BP_C
PASS [2] the PARTNER now holds a copy of the same GUID
PASS [2] both machines report the SAME HP for the shared boss -- host=3500 peer=3500
```

### R118. Four bugs inside the mirror lane, each measured before it was fixed

The lane is short and every one of its guards had a failure that looked like a different bug:

* **A reliable body that a receiver refuses is refused FOR EVER.** The lane is acked on
  arrival, so the sender never retransmits. The receiver's "one construction per 400 ms"
  limit therefore dropped the dragon's body permanently while it built an ordinary enemy:
  `kind=0000002B seq=2417 received; ack sent` and then nothing, with no line saying why. A
  refused body is now **stored whole** (the class path is the point) and drained later.
* **The scan sweep deleted the copy's record.** `EnemyScanStep` drops records it did not see
  in a pass, and a constructed actor is not "seen" — so `constructed=1` was followed one sweep
  later by a record-less actor. A mirror is now kept by liveness; its removal is the mirror
  pump's job.
* **A death older than the copy killed it.** Three copies were destroyed 46–765 ms after
  construction because a *kill-log* dead row was still the freshest row for those keys. A
  death only counts if the row is NEWER than the construction — the row's own timestamp is
  the ordering fact.
* **Silence was read as "the owner left".** At 10 s every copy died exactly 10031 ms after
  birth, because rows round-robin 16 per datagram over 200+ enemies and one key can wait many
  seconds. Silence now **hides** the copy (guarded, reversible) and a returning row shows it
  again; only a dead row or a world change actually removes it.

The construction itself uses the engine's deferred spawn pair, with `Paused`,
`bIsAIDisabled`, `AutoPossessPlayer` and `AutoPossessAI` cleared between `BeginDeferred` and
`FinishSpawning` — the same window B7's player body uses, and the only place those properties
still decide anything. A mirror never publishes rows, is refused by `StoreEnemySaveState`
before the game's writer, is never released to its own AI by any distance preference, and is
destroyed on a world change or session end.

### R119. The dragon DOES declare `TryApplyDamage` — and it moves nothing (corrected fact)

Session 15 recorded "the class has no `TryApplyDamage`, so `ran=0`" and called it a content
fact. Both halves are wrong. The class exports it (761 bytes), the call runs, and it still
moves nothing:

```
[1] damage: TryApplyDamage param DamageToApply size=216 on an enemy, amount -> DamageAmount @0
[1] WARN enemy damage had NO effect: TryApplyDamage ran asked 200.0 returned 0.00 hp 3500.0 -> 3500.0
```

`ran=0` is `ApplyDamageToActor`'s return (its HP-movement oracle), not "the function was
absent". Two further entries were then tried and measured rather than assumed:

* `EnemyBase_C.DamageHP` — the base class's own entry — takes **no input at all**. Its single
  parameter is `out:ReceivedHPDamage`:
  `damage: DamageHP on Dragon_Green_BP_C parms=1 in-params=0 size=4 -> out:ReceivedHPDamage`.
  So the module cannot ask it to subtract anything.
* Filling the entry's empty `Hit` object parameter (its `SourceGUID` field) changed nothing.

What does work is the degradation the module already uses for the player: the HP is a real,
readable, writable number (`GetHP()`/`GetMaxHP()` both answer), so the amount is applied as a
**clamped, downward-only** write and read back:

```
[1] damage: enemy hp via HP write 3500.0 -> 3300.0 (the class has no callable damage entry; the write is clamped and never heals)
[1] damage: enemy hp via HP write 3300.0 -> 3100.0 (...)
```

It is deliberately the last resort: it skips the enemy's hit reactions and status. But it is
the difference between a boss with a shared health pool and a boss that cannot be hurt at all.

### R120. The boss bar: the predicate does not exist, the CONTENT says who is a boss (fixed)

Session 15's `boss probe: ... part=-1 info=-1` was read as "this build has no boss
predicates". The probe was asking the wrong question: `Enemy_IsPartOfBossEncounter` and
`Enemy_GetBossInfo` are imported by `EnemyBase` and declared by `EnemyDataInterface`, but an
unimplemented interface message is not a class function, so `FindFunction` correctly found
nothing. The game's own answer is in the **data**:

* `EnemyData` (the class `GreenDragon_Data` is an instance of) declares a `Boss` property
  (offset 592, from the cooked class), and the dragon's data says `Boss = 1`.

So boss membership is now asked of the enemy's own `Data` after the predicates:

```
[1] boss probe: Dragon_Green_BP_C enemy=-1 spawner=-1 data.Boss=1 -> boss=1
[1] boss: guid=17181166 is a boss
[1] boss: ActivateBossHPBar called for this machine's own copy
[1] boss: sent guid=17181166 phase=0 bar on hp=65535
[2] boss: follower raised its own bar for guid=17181166 hp=...
```

`ActivateBossHPBar` itself was never missing — `PlayerStatsAndUtils_C` carries it (export 8,
591 bytes) and `FS_PlayerChar` carries a wrapper (`ActivateBossHPbar`), so the bar has a second
route on a build where the CDO is not loaded.

Turning the bar ON at the partner took three more guards, each of which alone left it dark:

* the mirror must not run `BossPump` (it is the other machine's boss, so it is the FOLLOWER —
  without this it sent `boss: sent guid=… bar on` of its own and never raised anything);
* `OnBoss` must accept a **mirror** as the local copy, because the ownership table has never
  heard of a key this machine did not spawn and answers `O_LOCAL`, not `O_FROZEN`;
* and the owner must **republish** the bar, because its first body arrives before the partner
  holds a copy and is acked and dropped. That repair is per boss, only while the bar is up,
  every 3 s:

```
[2] boss: follower raised its own bar for guid=17181166 hp=65535 phase=0
[2] boss: follower lowered its own bar for guid=17181166 hp=58046 phase=0
```

### R121. Two players on ONE number (proved)

The partner's hit on its mirrored copy travels along the existing enemy-hit intent, and the
OWNER applies it through the damage path this session repaired. Both hits land on the one HP,
and both machines report it:

```
  HP before: host=3500 peer=3500
[1] rig: hitguid -> key=17181166 LOCAL hit amount=200.0
[2] rig: hitguid -> key=17181166 the other player's hit sent amount=200.0 frac=3744 (this machine holds a frozen copy)
[1] hit on enemy 17181166 from the other player, 199.9
  HP after:  host=3100 peer=3100
PASS the dragon's HP FELL on the owner (damage from both players) -- 3500 -> 3100
PASS both machines report the SAME HP after the hits -- host=3100 peer=3100
```

The death is the game's own `Kill` on the owner, recorded there, and the partner learns it
through the DeadLedger and kills its copy — which is why the partner's copy dies too.

### R122. The cutscene: HOLD and RELEASE, and why it took four runs (fixed)

Session 15 proved the play and the hold but never the resume, because
`SecretSwordGetSequence` was still playing when the window closed. This session added a rig
lever that ends it on purpose (`qd.sequence = stop`, the sequencer player's own `Stop`), and
the release immediately exposed three real bugs:

* **`stop` was routed through the name matcher**, so it reported
  `no LevelSequenceActor matches 'stop'` while the sequence it was asked to end played on.
  It is now decided before the match, and ends whatever is playing.
* **Both machines played the sequence.** The area's own level list is pushed on both, so the
  placed `LevelSequenceActor` is in both worlds; the holder's `BeginCutsceneWithManager`
  started a second copy locally, after which neither machine ever released. A machine that
  can already see the sequence holds instead of starting another.
* **A finished cutscene was re-played.** After the end, each side's next publish still named
  the last sequence it played, so the pair restarted each other indefinitely. The board now
  refuses to act on a publication it has already released, and a machine that just ended a
  sequence does not start it again while the partner still names it.

Measured, the whole lane:

```
[2] rig: sequence lever -> SecretSwordGetSequence Play ran on its own SequencePlayer
[2] cutscene: local started SecretSwordGetSequence
[1] cutscene: holding the local player for the partner's cutscene (enter=1 lock=1)
[2] rig: sequence lever -> SecretSwordGetSequence Stop ran on its own SequencePlayer (the sequence is ended)
[2] cutscene: local ended SecretSwordGetSequence
[1] cutscene: partner's cutscene ended; resuming together (exit=1 unlock=1)
=== 15 PASS, 0 FAIL ===
```

### Bugs fixed this session

* **`ENEMY_SPAWN (0x2B)`** (R117): a new reliable body carrying the enemy key, transform, HP
  fraction and CLASS asset path, so a machine that never spawned an enemy can construct it.
  `qdwire.h`, `protocol.rs`, `WIRE.md` §3.9c, `doc/vectors.txt` and both test suites together.
* **The mirror lane** (R118): deferred spawn with AI/possession cleared pre-finish; never
  publishes rows; refused by the save hook; never released to local AI; destroyed on a dead
  row, a world change or a session end; hidden (not destroyed) on owner silence; kept by the
  scan sweep by liveness rather than by being "seen".
* **`QueuedMirror`** (R118): a body a receiver cannot construct *yet* is stored whole and
  drained later, because the sender will never resend an acked body.
* **The HP degradation for enemies** (R119): `TryApplyDamage`, then `DamageHP`, then a
  clamped downward-only write of `HP` with a read-back. `DamageHP`'s real signature
  (out-only) is logged once per class so the reason is visible.
* **Boss membership from `EnemyData.Boss`** (R120): the content's own authored flag, tried
  after the predicates and named in the probe line; the bar has a second route through the
  player class.
* **The follower's bar on a mirrored boss** (R120): a mirror does not run `BossPump` (it is
  the other machine's boss), `OnBoss` accepts a mirror as the local copy (ownership answers
  `O_LOCAL` for a key it never spawned), and the owner republishes a raised bar every 3 s
  because its first body can arrive before the partner holds anything.
* **`stop` on the sequence lever, decided before the name match** (R122).
* **The cutscene hold/release rules** (R122): the holder does not start a second copy, a
  released publication is not acted on again, and a machine that just ended a sequence does
  not restart it.
* **`tools/session16.ps1`**: one process per phase in its own instance root, run-wide scans
  instead of windows that open after the line was written, and the per-KEY persistence check
  the previous version scoped to the whole log.



### Verdicts, item by item

| Item | Verdict | Key line |
|---|---|---|
| 1a. The owner publishes the class of a key the partner cannot hold | **PASS** | `[1] enemy spawn: described guid=17181166 class=/Game/Enemies/Dragon/Dragon_Green_BP.Dragon_Green_BP_C` |
| 1b. The machine with no actor CONSTRUCTS the copy | **PASS** | `[2] enemy spawn: constructed guid=17181166 ... (by asset path) at -28573,754,-13028 -- AI off, driven by the owner's rows, never persisted` |
| 1c. The partner holds the same GUID, marked as a mirror | **PASS** | `[2] enemykey 17181166 ALIVE local=0 src=0 hp=3500.0 mirrored=1 cls=Dragon_Green_BP_C` |
| 1d. Both machines report the same HP from the start | **PASS** | `both machines report the SAME HP for the shared boss -- host=3500 peer=3500` |
| 1e. The mirror is never persisted by the partner | **PASS** | `the mirrored copy was never persisted by the partner -- records for THIS key=0` |
| 2a. The content says the dragon is a boss | **PASS** | `boss probe: Dragon_Green_BP_C enemy=-1 spawner=-1 data.Boss=1 -> boss=1` |
| 2b. The bar is raised on BOTH machines | **PASS** | `[1] boss: ActivateBossHPBar called`; `[2] boss: follower raised its own bar for guid=17181166 hp=65535` |
| 2c. Both players' damage lands on ONE HP | **PASS** | `[1] hit on enemy 17181166 from the other player, 199.9`; `HP after: host=3100 peer=3100` |
| 2d. The HP fell (not just converged) | **PASS** | `the dragon's HP FELL on the owner -- 3500 -> 3100`; `damage: enemy hp via HP write 3500.0 -> 3300.0` |
| 2e. The death is recorded on BOTH | **PASS** | `[1] enemy death recorded guid=17181166`; `[2] the other machine recorded this death; killing the local copy (hp 3100.0)` |
| 2f. The partner's copy is removed | **PASS** | `[2] enemykey 17181166 DEAD local=0 src=-1` |
| 3a. The sequence starts on one machine | **PASS** | `[2] rig: sequence lever -> SecretSwordGetSequence Play ran`; `cutscene: local started` |
| 3b. The other machine holds | **PASS** | `[1] cutscene: holding the local player for the partner's cutscene (enter=1 lock=1)` |
| 3c. The sequence can be ended on purpose | **PASS** | `[2] rig: sequence lever -> SecretSwordGetSequence Stop ran (the sequence is ended)` |
| 3d. Joint resume, the held partner RELEASED | **PASS** | `[1] cutscene: partner's cutscene ended; resuming together (exit=1 unlock=1)` |
| Session survival | **PASS** | both processes alive at the end of every phase; 0 error/fault lines |

`item1` finished **15 PASS / 0 FAIL**, `boss` **25 PASS / 0 FAIL** and `cut` **15 PASS / 0
FAIL** — 55 assertions across the three phases, every one of them a line from a live run.

### What this session settled

The co-op gap that Session 15 named is closed at the level it was stated. A partner can now
see and fight an enemy its own spawner never created, because the one fact a row cannot carry
— the class — was put on the wire, and the copy that machine builds is a follower in every
respect that matters: it never publishes, never persists, never runs its own AI, and it is
removed when the owner says dead.

Two of Session 15's three "content facts" were wrong and both corrections are measurements,
not guesses. `TryApplyDamage` exists on the dragon and runs; it simply refuses a
module-built payload, and neither `TryApplyDamage` nor the base class's `DamageHP` (out-only)
can subtract an amount this module chooses — so the shared HP is driven by a clamped,
downward-only write with a read-back, which is the same degradation the module already uses
for the player. And the boss predicate does exist in the build; the content just does not
implement the interface message on the enemy classes, while the enemy's own `EnemyData.Boss`
says plainly who is a boss.

The cutscene requirement is now a full loop rather than a hold with no exit: play on one
machine, hold on the other, end it on purpose, and both machines resume together. The release
was where three real bugs lived — `stop` routed through the name matcher, both machines
starting a second copy of a sequence that exists in both worlds, and a finished cutscene being
replayed because each side still named it.

What is left is not a lane: the mirrored copy's damage goes through a legacy write rather than
the class's own hit pipeline, so the enemy does not play its hit reaction for damage this
module applies. That is named here rather than smoothed over, and the entry the build would
need (`ApplyDamageToHitboxTarget`, per-hitbox, with a hit the Blueprint saw) is the next thing
to attack.

---

## Session 17 — 2026-11-09 (the door opens both ways; the shot at the hit pipeline)

This branch is `sync-fix` at `f4a961f`. The test phases run the wide set
(`ObjectSync=1`, `ActorStateSync=1`, everything on, `LogLevel=3`); the soak phase runs either
the **shipped** `qdcoop.ini` verbatim or, with `-PlaySet`, the documented play set from
`PLAYTEST.md` section 4 — in both cases with only the relay address pinned to loopback and
`LogLevel=3`. `tools/session17.ps1` is the whole run in one process per phase
(`-Phase smoke|item1|item2|item3|item4|soak`). Roles are read from `session established`:
**host=inst1, peer=inst2**. Fresh pairs are seeded from `analysis/save-backup-before-live`.

Session 16 closed on a named gap — "the mirrored copy's damage goes through a legacy write
rather than the class's own hit pipeline, so the enemy does not play its hit reaction" — and
left the interaction lane's real question open since R90. This session took the top item (the
door, both ways) first, and it is now closed.

### R123. THE DOOR OPENS BOTH WAYS (new, and the session's headline)

The requirement — a closed chest or door opened by the real interact lever on the PEER, the
host applying the intent and refreshing so the object is visibly open there and cannot be
looted again, with the chest counter not double-counted, and then the HOST opening another and
the peer refreshing — is met, measured, in one run (`item1`, **25 PASS / 0 FAIL**). The forward
direction, verbatim:

```
[2] dev interact: resolved 'guid:7c263bfd' -> Chest5 at -8164,-13070,-5083 dist=9243
[2] dev interact: target=Chest5 [Chest_C] component=found verb=Interact
[2] actor-state intent sent guid=7C263BFD-4983B099-22FAE3A2-9689C6D6 value=1 rev=81 try 1/5
[1] actor-state intent applied: guid=7C263BFD-4983B099-22FAE3A2-9689C6D6 value=1
[1] actor-state refresh ok Chest5 [Chest_C] index=0 value=1 mySaveData=1->1 guids=1 cached=ok
    opened=1 forced=1
[1] objkey Chest5 lane=actor-state value=1 opened=1        <- the host's own copy, read back
[1] TotalChestsOpened: 0 -> 1                              <- ONE, on the machine that pressed
[2] TotalChestsOpened = 1                                  <- and the same number on the peer
[1] a second press on the host: 1 -> 1                     <- no second payout, not lootable again
```

and the reverse direction, on a different object:

```
[1] dev interact: target=Chest32 [Chest_C] component=found verb=Interact
[1] actor-state host set 31 entries (live 31) rev=111
[1] objkey Chest32 opened=1
[2] actor-state refresh ok Chest32 [Chest_C] index=0 value=1 mySaveData=1->1 guids=1
    cached=ok opened=1 forced=1
[2] objkey Chest32 lane=actor-state value=1 opened=1
```

A door instance behaves the same way, which is why both were used:

```
[2] dev interact: resolved 'guid:836b5c61' -> SZ_BoringDoor_00_BP9
[1] actor-state refresh ok SZ_BoringDoor_00_BP9 [SZ_BoringDoor_00_BP_C] ... opened=1
```

**What it took to get there is four measured facts, and each one had been a wrong assumption:**

* **`IsInteractible=1` is not required for this lane to work.** R105's wall was real for the
  *shop door* (`Shop_Door_00_SM` says `IsInteractible=1` and refuses anyway); for the area's
  chests the press runs its verb and the state moves even where the gate reads 0. The lane's
  claim is therefore stated against the game's own `Interact` verb and the value the lane reads,
  not against the gate.
* **A chest's `Opened` bool is the wrong half of its state.** Measured on a chest that had just
  been opened successfully: `value=1 opened=0`. The container value is the fact; the bool is a
  visual latch. Both are reported, and the verdict accepts either.
* **Object names are not identities.** `Chest`/`Chest2`/`Chest4`/`Chest5` exist in BOTH 780 and
  the hub with different GUIDs, so `near:Chest3` landed on the hub's chest 56 km away and
  pressed that. Targeted by saved GUID instead (R128).
* **The two machines do not hold the same object set, and it drifts.** Six dumps across one run
  showed the host holding only `100_GR` chests while the peer held `760_SZ_AUT` ones; a later
  dump of the same run had both on 780. A single sample is a snapshot of a moving target, so the
  table is the UNION of every dump in the run and the level pin is re-asserted between samples.

### R124. `qd.obj`, `qd.flag`, `qd.gate` — three read-only levers

The lane's own lines could not answer "are the two machines now holding the same state for
this key", which IS the co-op claim:

* **`qd.obj`** dumps the whole object inventory: this lane's tracked set (`objkey`), AND the
  savable half the lane deliberately hands to ACTOR_STATES (`savkey`), with each object's
  container value, its `Opened` bool, whether the actor is in this world, its distance from the
  player and its placed path. The second half is the one that matters: measured in 780, the
  object lane tracked eleven actors and every one was a lever, because the area's chests carry a
  `GUIDs` entry.
* **`qd.flag`** reads a `NamedSaveData` flag out of this machine's bank. `TotalChestsOpened`
  rides `WORLD_FLAGS`, not either object lane, so "the counter did not go up twice" was
  unfalsifiable from a log without it.
* **`qd.gate`** asks the game's own `GetInteractInfo` about ONE actor and presses nothing.
  `DevInteractBody` was the only caller of that probe and it pressed as part of the same body,
  so choosing which object to test cost a press on every candidate.

### R125. FIXED — the savable walk could fault, and did (two processes)

`BuildActorStateOwnersGuarded` had a `__try` around the per-object `IsSubclassOf` test but NOT
around `ObjectAt`, `LooksLikeAnObject`, `ObjClass` or the `GUIDs` array read. A run died at 24 s
with `EXCEPTION_ACCESS_VIOLATION reading 0x0000000000000000` inside it, before the scenario had
asked for anything, and a peer died earlier at `WINMM+0x6f357` reading `0x0000000100000017` — a
small integer read as an object pointer — during the same walk. The loop body is now guarded as
a unit, so a fault costs the rest of that pass (the caller re-runs it a second later) rather
than the process.

### R126. FIXED — pressing an object in a level that is not resident kills the game

The most expensive lesson of the session. Reaching a chest by teleporting to its GUID works,
but if that GUID belongs to a DIFFERENT streamed level, the press succeeds and then:

```
actor-state: game writer for guid=540dfba4-... value=1 read back -999999 (WRITE FAILED)
actor-state: game writer for guid=b549b9a2-... value=2 read back -999999 (WRITE FAILED)
```

and both processes die (`WINMM+0x37988`, `+0x6f7f7`). `Save Data Index to World Save` is
Blueprint bytecode writing into a level the manager is streaming out from under it; the fault
happens inside the game's own code after our writer has been entered, so no `__try` of ours can
contain it. The scenario now stays in the world the player is in.

### R127. Two rig levers grew a `#<nonce>`, and both had been silently mis-parsing

`qd.act` and `qd.tp` both use "the string changed, so this is a new instruction", and neither
stripped a trailing `#<anything>`:

```
dev teleport guid: '2ab59657#191528110' is not hex          <- the teleport never happened
dev interact: resolved 'Chest3#173909658' -> SG_Chest_BP2 at 6185,-18108,-14047 dist=56575
```

The second line is the dangerous one: the name did not match, the curated `chest` class walk
ran, and the press went to a different actor 56 km away while the gate's answer was read as if
it belonged to the requested one. Both levers now strip the nonce.

### R128. `guid:` addresses a SAVED WORLD OBJECT, not only an enemy

`DevTeleportToGuid` searched `g_en` only, so `guid:<key>` for a chest resolved nothing. It now
also walks the savable table, which is what lets a scenario stand beside a particular chest
instead of "the nearest chest of some class".

### R129. Item 2 (loot hiding): the drop rolls, but not on the machine that killed it

```
[1] rig: enemykill -> key=17181166 the enemy's own Kill ran
[2] enemy 17181166: the other machine recorded this death; killing the local copy
[2] loot: drop 90A52CF3#0 = /Game/Items/Consumables/Herb/Herb_Item_BP.Herb_Item_BP_C
[2] loot: drop 8FA52B60#1 = /Game/Items/Consumables/Herb/Herb_Item_BP.Herb_Item_BP_C
```

The drop rolled on the PEER for an enemy the HOST killed. Watching only the host reported "no
drop" while the peer's log already had two, so the harness now watches both machines and makes
the TAKER the machine that did not roll it. The killing half PASSES. The hiding half is
**NOT-REACHED**: `loot: hiding local drop` did not appear in any run this session — the drop
exists, and the tombstone needs the taker's ledger to reach the machine holding the visible copy,
which was not observed to converge in the window.

### R130. Item 3 (760_SZ_AUT): the cutscene is complete; the dragon did not spawn

`Dek_Egg_Activation` — play on one machine, hold on the other, end it, resume together — is
**fully proven**:

```
[2] rig: sequence lever -> Dek_Egg_Activation Play ran on its own SequencePlayer
[2] cutscene: local started Dek_Egg_Activation
[1] cutscene: holding the local player for the partner's cutscene (enter=1 lock=1)
[2] rig: sequence lever -> Dek_Egg_Activation Stop ran on its own SequencePlayer
[2] cutscene: local ended Dek_Egg_Activation
[1] cutscene: partner's cutscene ended; resuming together (exit=1 unlock=1)
```

The ROSDragon did **not** materialise: four minutes with both players inside its spawner's
radius produced no `Dragon_Ros_BP_C` at all, and the area reported `engineVisible=0` for the
whole window — its `PlayerIsInActivationRange` reads player 0 of its own machine and the level
manager had the level loaded but not shown. The boss PIPELINE is not implicated: the same world
had live bosses whose content says `Boss=1`. So item 3 splits: **cutscene PASS**, **ROSDragon
NOT-REACHED (spawner/content)**; the pipeline itself was proven in Session 16 on the Green
Dragon.

### R131. Item 4 (hit reaction): `ApplyDamageToHitboxTarget` is a DEAL-damage entry

The ladder now tries the class's own entry before the clamped write, and the run says what it is:

```
signature ApplyDamageToHitboxTarget props=5 parms=158 ret@-1
  Target : ObjectProperty @0 size=8
  Hit : StructProperty @8 size=136
  PositionOverride : StructProperty @144 size=12
  UseOverride : BoolProperty @156
  HitTarget : BoolProperty @157
damage: ApplyDamageToHitboxTarget on Dragon_Green_BP_C ran and HP did not move (3500.0 -> 3500.0)
damage: enemy hp via HP write 3500.0 -> 3100.0
```

Its parameters are `Target`/`Hit`/`PositionOverride`/`UseOverride` — the entry an attacker calls
to DEAL damage TO something, not one a victim implements to TAKE it. Called on a victim with
itself as `Target` it runs and applies nothing, which is the correct result. **The hit-reaction
half therefore remains NOT-REACHED, and the reason is now an API fact**: this build exposes no
take-damage entry carrying an amount (`TryApplyDamage` refuses a module-built payload, `DamageHP`
is out-only, and this one is the other direction). The one shared HP is unaffected — it still
moves, and both machines still agree (`3500 -> 3100`, `host=3100 peer=3099.9`).

### Bugs fixed this session

* **`BuildActorStateOwnersGuarded` fault-contained as a whole** (R125).
* **`qd.act` and `qd.tp` strip a `#<nonce>`** (R127).
* **`DevTeleportToGuid` addresses savables** (R128).
* **Three read-only rig levers** and the savable half of the object dump (R124).
* **The enemy record table 256 → 1024** (R132a): `RegisterMirror` evicted a record per new
  mirror, and an evicted MIRROR can never learn its owner's death.
* **The mirror pump and `TryConstructMirror` fault-contained** (R132).
* **`tools/session17.ps1`**, with four harness bugs found by running it: `Get-Mark $H - 60000`
  silently DROPPED the offset (PowerShell binds only declared parameters, so a non-advanced
  function takes `$H` and ignores the rest) and every window opened after the line it looked
  for; `$p` in a `foreach` silently rebound `$P`, the peer instance index, because PowerShell
  variable names are case-insensitive; `Sort-Object { ... }, Dist` treated `Dist` as a second
  key NAME; and a name-keyed object table kept only one actor per name.

### Verdicts, item by item

| Item | Verdict | Key line |
|---|---|---|
| 1a. Peer opens a real chest/door; host applies; host `opened=1` | **PASS** | `actor-state refresh ok Chest5 ... opened=1`; `objkey Chest5 lane=actor-state value=1 opened=1` |
| 1b. The object is not lootable again on the host | **PASS** | `a second press on the host: TotalChestsOpened 1 -> 1` |
| 1c. `TotalChestsOpened` not double-counted | **PASS** | `0 -> 1` on the presser; `host=1 peer=1` |
| 1d. Host opens another; peer refreshes | **PASS** | `actor-state refresh ok Chest32 ... opened=1` on the PEER |
| 1e. A door instance behaves the same as a chest | **PASS** | `actor-state refresh ok SZ_BoringDoor_00_BP9 ... opened=1` |
| 2a. An enemy that DROPS, with both players present | **PASS** | `loot: drop 90A52CF3#0 = .../Herb_Item_BP_C` (rolled on the PEER) |
| 2b. The death reaches the other machine | **PASS** | `the other machine recorded this death; killing the local copy` |
| 2c. `loot: hiding local drop` on the other machine | **NOT-REACHED** | no such line in any run; the ledger was not observed to converge |
| 3a. `Dek_Egg_Activation` play on one, hold on the other | **PASS** | `cutscene: holding the local player ... (enter=1 lock=1)` |
| 3b. Joint resume, held player released | **PASS** | `partner's cutscene ended; resuming together (exit=1 unlock=1)` |
| 3c. ROSDragon mirror + HP + bar + death in 760 | **NOT-REACHED (spawner)** | no `Dragon_Ros_BP_C` in 4 min at its own spawner; `engineVisible=0` |
| 3d. The boss pipeline itself | **PASS (Session 16)** | `boss probe: Dragon_Green_BP_C ... data.Boss=1 -> boss=1` |
| 4a. The class's own `ApplyDamageToHitboxTarget` is called | **PASS (measured)** | `signature ApplyDamageToHitboxTarget props=5 parms=158` |
| 4b. It moves the HP / plays a reaction | **NOT-REACHED (API)** | `ran and HP did not move (3500.0 -> 3500.0)` — a deal-damage entry |
| 4c. One shared HP regardless | **PASS** | `HP after: host=3100 peer=3099.9` |
| 5a. Ten minutes at the SHIPPED defaults: no crash | **PASS** | 600 s, `soak iterations: 182`, both alive, `error/fault lines=0` |
| 5b. Ten minutes at the DOCUMENTED PLAY set: no crash | **FAIL (open)** | 256 s → **499 s** after two guards (R132) |
| 5c. Kill sets diff 0 at the play set | **FAIL (consequence of 5b)** | the dump is read after the crash, so one machine reports 0 keys |

### R132. The play set still crashes: three guards, and the crash moved but did not go

The one thing the session did NOT deliver, stated as a number rather than a shrug. At the
shipped defaults (every write path off) the soak runs the full 600 s clean — `soak iterations:
182`, both processes alive, `error/fault lines=0` on both. At the documented play set it does not:

| attempt | guard added | died at | iterations |
|---|---|---|---|
| 1 | none | 256 s | 68 |
| 2 | enemy table 256 → 1024 (R132a) | 264 s | 68 |
| 3 | `__try` around the mirror pump's per-copy service | 256 s | — |
| 4 | `__try` around `TryConstructMirror` | **499 s** | **137** |

Every crash is the same shape inside this module (`EXCEPTION_ACCESS_VIOLATION reading
0x0000000100000017` / `0x000001951e4d50bc` at `WINMM+0x3b357`, `+0x6f4d7`), which is the
signature of a pointer read out of a freed object, and three of the four deaths are at almost
exactly the same elapsed time — the moment the hub's own streaming churn starts replacing actors
the mod is holding. The guards are real (the fourth attempt nearly DOUBLED the survival window)
but the remaining fault is in a path the session did not isolate.

R132a is a real fix regardless and the soak found it: `RegisterMirror` was evicting an enemy
record for every new mirror because the table was capped at 256 while the hub holds more, and an
evicted MIRROR can never learn its owner's death — nothing re-adds a mirror, its body having
been acked. The three keys that diverged in the first play-set soak (`B55C570E`, `32548C19`,
`44D6AA74`) each had `enemy mirror: record table full (256); evicting guid=...` immediately
before.

**What a human tester needs to know:** the play-set configuration is NOT yet safe for a long
session. Play in shorter stretches, and expect a crash at around eight minutes of a heavily
populated hub.

### What this session settled

The interaction claim open since R90 is closed for the objects this content actually places: a
chest or a door the game lets a player press opens on the machine that did not press it, reads
back as open on both, cannot be looted twice and does not double-count the chest counter. That
took four wrong assumptions about this build out of the way — the gate is not the precondition,
a chest's `Opened` bool is not its state, an object's name is not its identity, and the two
machines' object sets drift while the level manager works through a list. Each is now a line in
the log rather than a belief.

The hit-reaction question changed shape rather than closing, and the new shape is the useful
part: `ApplyDamageToHitboxTarget` — the entry Session 16 named as "the next thing to attack" —
turned out to be the entry an ATTACKER calls, with `Target` and `Hit` parameters, not a victim's
take-damage entry. Calling it on the victim runs and applies nothing, correctly. So the reaction
half is blocked on an API this build does not expose, not on a lane that has not been written.

The soak is the honest black mark: the shipped set is stable and does nothing, the play set does
everything and is not yet stable for ten minutes. Two guards nearly doubled the window and the
remaining fault is named by offset, not understood.


## Session 18 — 2026-11-10 (the offset becomes a name, and the crash goes)

This branch is `sync-fix` at `7d5ada2`. The soak phases run the **documented play set** from
`PLAYTEST.md` section 4 with only the relay address pinned to loopback and `LogLevel=3`;
`-ShippedDefaults` selects the control. `tools/session18.ps1` is the whole run in one process
per phase (`-Phase smoke|soak-hub|soak-780|all`), and every phase re-seeds the pair from
`analysis/save-backup-before-live`, so two phases cannot share a mutated save. Roles are read
from `session established`: **host=inst1, peer=inst2**.

Session 17 ended with the one number it could not fix: the play set died at 256 s, then 264 s,
then 256 s, then 499 s, always in the same shape and always with an offset rather than a name.
This session started by turning the offset into a name, and the name was a bug the whole
codebase shared.

### R133. THE CRASH, NAMED — three cached pointers, read after the engine freed them

The build has emitted a `/MAP` since Session 7 and nobody had read it. `tools/map_lookup.py`
turns an RVA into a symbol (the map's Address column is a section NUMBER, not a name — reading
it as a name silently matches nothing, which is why it had never been used). With it:

| report | resolves to |
|---|---|
| `WINMM+0x3b357` | `EnemySpawnPump` + 0x377 |
| `WINMM+0x6f4d7` | `ScanLocalEquip` + 0xdd7 |
| `WINMM+0x37988` | `EnemyIdentity` + 0x48 |
| `WINMM+0x6f7f7` | `enemybw::Scheduler::Select` + 0x157 |

`build.bat` now also passes `/Zi /DEBUG` and writes `winmm.pdb` (with `/OPT:REF /OPT:ICF
/INCREMENTAL:NO` spelled out, so the release code layout is unchanged), and `session18.ps1`
installs the PDB beside the DLL — a stack without symbols is the same list of offsets.

All three are the same fault: a **cached `UObject*` dereferenced after the engine freed it.**
`EnemySpawnPump` reads `EnemyRec::actor` over up to 1024 records every 100 ms while the hub's
streaming frees actors underneath it; `ScanLocalEquip` walks GObjects and reads a cached pawn;
`EnemyIdentity` reads `r->actor`'s `OwningSpawner`.

**And the guard the code relied on was not a guard.** `ObjectIsLive(p)` is
`ObjectAt(ObjInternalIndex(p)) == p`, which dereferences `p` to learn its slot — so "check
liveness, then use" reads the same freed allocation twice:

```
0x0000000100000017   <- two 32-bit fields of a recycled heap block read as a pointer
```

`ObjectIsLive` is now `__try`-guarded and its comment says plainly that it is only valid for a
pointer obtained this frame from the engine, never for a cached one.

### R134. FIXED — one weak-handle pattern for every cached actor/object pointer

`native/src/qdweak.h`. An object is identified by **where it lives in GUObjectArray**, not by
its address: `{InternalIndex, slot SerialNumber}`, the same pair `TWeakObjectPtr` uses.
Resolution is index-first —

```
p = table[slot];                                       // UE nulls the slot on destruction
if (!p) return nullptr;
if (table_serial(slot) != serial) return nullptr;      // the slot was REUSED
return p;
```

— so a freed address is never read. `weak::Ref` converts to `void*` by **resolving** and from
`void*` by **capturing**, which is what let one pattern replace every site instead of a
per-site rewrite. It is exactly `sizeof(void*)`, so no record table grew.

The index is stored **biased** (`slot + 1`) so an all-zero record is EMPTY. This is not
cosmetic: these tables are static arrays and several are `memset`, and a `{slot = 0}` encoding
would make a wiped record claim GUObjectArray slot 0 — a real slot — and silently resolve to
some other object.

Converted in one consistent pass: `g_localPawn`, `g_puppetActor`, `g_puppetClass`, `g_proxyCls`,
`g_proxySeen`, `g_objTableWorld`, `g_enWorld`, `g_levelWorld`, `g_seqWorld`, `g_blockedNpc`,
`g_blockedWorld`, `g_arrivePrevPawn`, `g_fxLastLocal`, `g_fxLastCopy`, `g_fxOurs`,
`g_mirrorActor`, `g_seqActors`, `g_arcSeen`, `g_devPokeComp`, and the record fields
`EnemyRec::actor`, `EnemyRec::spawner`, `ObjectActorRec::actor`, `LootActorRec::actor`,
`MoverActorRec::actor`, `LootKeyMemoRec::actor`, `InvCached::obj`, `EquipScan::wvClass`,
`ArrivalMarker::actor`, `FxCopy::actor`, `FxTrack::actor`.

**A second instance of the same bug, found by the audit:** `g_devPokeComp` wrote a byte through
a component pointer captured frames earlier (`*((uint8_t*)g_devPokeComp[i] + off) = 0`). It now
resolves through the handle first, so a dead component is a no-op instead of a write into a
recycled block.

**Deliberately left raw, and why:** `g_enemyBaseCls`, `g_clsYes[]`, `g_clsNo[]`. These hold
UClass objects, they live as long as the package, they are compared by pointer against classes
read fresh from GObjects in the same pass, and the caches are dropped with the world. A weak
handle would add a lookup to a hot inner loop and change nothing about safety.

`test_weak.cpp` pins the case that crashed, as an invariant rather than an example: a handle
captured from an object, resolved after that object was freed **and its slot reused by a
different object**, must return null — and the fixture poisons the freed block, so a handle
that peeked would resolve to the poison. 47 assertions, all green.

### R135. THE PLAY SET NOW SURVIVES TEN MINUTES — TWICE, BACK TO BACK

The soak body is Session 17's, unchanged, so the two sessions are comparable line for line.
What is new is `-Repeat`: both runs happen inside **one** pair of processes, because a fix that
merely moves the first crash later is only visible on the second run.

```
### soak-hub run 1 of 2
  this soak ran the DOCUMENTED PLAY set
  soak run 1: 603 s of 600 s, iterations=182
  PASS [1] both processes survived the soak -- 603s
  PASS [1] the process is alive at the end of the soak -- 603s
  PASS [2] the process is alive at the end of the soak -- 603s
  [1] error/fault lines=0        <- the harness's OLD combined gate, quoted as it printed
  [2] error/fault lines=0

### soak-hub run 2 of 2
  soak run 2: 603 s of 600 s, iterations=184
  PASS [1] both processes survived the soak -- 603s
  PASS [1] the process is alive at the end of the soak -- 603s
  PASS [2] the process is alive at the end of the soak -- 603s
```

(Those two `error/fault lines=0` lines are the run's own output at the time, from the combined
gate; the 780 run below shows why that gate was wrong and what it was replaced with.)

Against Session 17's 256 s → 499 s at the same configuration with the fault still present.
Zero FAULT lines on either machine across 1206 s of play-set traffic (`ACCESS_VIOLATION=0`,
`EXCEPTION(any)=0`). The `ERR` count in these runs was the actor-state write conflict, which is
R136 below — it is a lane's own refusal, not a crash, and Session 18's harness reports the two
separately for exactly that reason.

**The final binary was then re-run on the same claim, and it holds twice more:**

```
### soak-hub run 1 of 2   (binary: weak handles + LevelRec::ls + bounded loot hide)
  soak run 1: 600 s of 600 s, iterations=184
  PASS [1] both processes survived the soak -- 600s
  PASS [1] the process is alive at the end of the soak -- 600s
  PASS [2] the process is alive at the end of the soak -- 600s
  host keys=460 (dead 419); peer keys=417 (dead 415)
  dead only on the host: 4 -> 9395EC50,3165F724,180CD9AA,FA2FC279
  dead only on the peer: 0 ->
  [1] FAULT lines (ACCESS_VIOLATION/EXCEPTION/fatal)=0
  [2] FAULT lines (ACCESS_VIOLATION/EXCEPTION/fatal)=0

### soak-hub run 2 of 2
  soak run 2: 602 s of 600 s, iterations=185
  PASS [1] both processes survived the soak -- 602s
  PASS [1] the process is alive at the end of the soak -- 602s
  PASS [2] the process is alive at the end of the soak -- 602s
```

That is **four** consecutive ten-minute-ish soaks across two boots on two builds, every one of
them with both processes alive at the end and zero FAULT lines. The kill-set difference in that
last run's first soak was 4 and one-sided (`dead only on the host` with `dead only on the peer:
0`), which is the spawner-respawn shape R137 names rather than a tombstone that failed to travel.

**BUT THE CLAIM DID NOT HOLD ON EVERY RUN, AND THAT IS THE NUMBER THAT MATTERS.** Three later
runs at the same configuration died — at 248 s, 4130 s and 330 s. The 4130 s dump named a mod
fault (R139, fixed); the 330 s dump names a fault inside the game's own code (R140, open). So the
soak record for this session is **4 survived / 3 died**, not "the crash is gone", and the
stability note in `PLAYTEST.md` is written to say exactly that. Four-in-a-row was real, and so
were the three failures; presenting only the first would be the same error as Session 17's
combined `ERR|EXCEPTION|ACCESS_VIOLATION` gate, one layer up.

The second soak of that boot measured `diff=31` (`dead only on the peer: 29`), and the world
check rules out the easy explanation — both machines were in the same world (`travel: the
partner arrived in Ruins_Level_00 (both there)` on both, 3:44:38). So this is R137 at full
weight rather than a world mismatch: 29 keys the peer buried and the host did not, one-sided in
the opposite direction from the first soak. That is the honest number for the remaining enemy
identity work, and it is exactly why R137 is listed as open instead of folded into the soak
verdict.

### R136. THE 780 SOAK SURVIVES TOO — AND FINDS A THIRD, DIFFERENT BUG

The same body after entering `780_SZ_Depths` through the game's own level list, where both
machines hold the same authored enemy GUIDs, so the kill-set and shared-HP comparisons mean
something there in a way they cannot in the hub:

```
PASS [1] the stream reports the area's list: level list 'SZ_Depths' added ... visible=1 loaded=1 engineLoaded=1 engineVisible=1
PASS [1] the host stands at the anchor  dev teleport near 'SecretSword_Altar_BP' -> ... standing at -40920,13187,-15552 offset=140
PASS [2] the peer stands at the same anchor  (same coordinates)
soak run 1: 602 s of 600 s, iterations=175
PASS [1] both processes survived the soak -- 602s
PASS [2] the process is alive at the end of the soak -- 602s
```

**Faults: zero.** `ACCESS_VIOLATION=0`, `EXCEPTION(any)=0`, `fatal=0` in BOTH logs, across
1206 s in the hub plus 602 s in 780. That is the R132 claim, measured in two worlds.

**But 8005 `ERR` lines appeared on the peer, all of one class:**

```
[2] 2:16:40.234 ERR  actor-state: game writer for guid=3a159151-... value=16 read back 17 (WRITE FAILED)
[2] 2:16:40.371 ERR  actor-state: game writer for guid=fd0297de-... value=13 read back 15 (WRITE FAILED)
```

13 distinct GUIDs, and every read-back is the written value **±1 or ±2**. That is two writers on
one counter: the peer writes 16 and, by the time it reads back, something has written 17. The
780 area's chests carry `GUIDs` entries and ride this lane, and the lane is `HostAuthoritative=1`
— so a peer that writes and then reads back its own value is a lane bug, not a lost race.
**It is the largest remaining issue and it is NOT the crash**: nothing faulted and the process
survived. Named here so the next session starts from "why does the peer write actor state at
all" instead of from zero.

**It is also why the harness was changed.** Session 17's gate counted
`ERR |EXCEPTION|ACCESS_VIOLATION|fatal` in one bucket, so a run with 8005 refused writes and no
faults would have been reported as "a faulted run". `session18.ps1` now counts FAULT and ERR
separately and asserts the crash claim (`faults=0`), reporting ERR as a number beside it.

### R137. THE KILL SETS STILL DIVERGE ON A SHARED KEY — and the reason is not the crash

`diff 3` in hub run 1, `diff 7` in hub run 2, `diff 34` in 780; and 1–5 shared keys with unequal
HP. This is **not** a regression from R134 (the divergence was there in Session 17 too, where the
dump could not even be read after the crash). It is two named lane bugs, both visible in the logs:

* **A dead key comes back to life on the other machine.** `enemy record applied guid=A8E9D51C
  hp=0.0` on the host, then `0:33:41 enemy id: spawner A8E9D51C` followed by `enemykey A8E9D51C
  ALIVE local=1 hp=60.0` on the peer. The peer's own spawner respawned an enemy whose persisted
  death record it already held. The death must outrank a local respawn for a shared key.
* **A summon key collides with a different enemy's key.** One key resolves to a
  `Boner_Gray_Archer_BP_C` at `hp=85` on the host and a `FlamingSkull_BP_C` at `hp=30` on the
  peer. The HP "mismatch" is two different enemies on one key, which is worse than a stale HP:
  `qdenemyid.h`'s summon key needs a class discriminator.
* **In 780 the mismatches are mostly 0 vs the real HP**, e.g. `5D370AC1: host=909 peer=0`,
  `AE6AFEB2: host=0 peer=240` — one machine holds the live enemy and the other a record whose HP
  never arrived. Same family: a key without a row is read as HP 0.

Note that `PLAYTEST.md` section 8 already says hub kill lists diverge **by design** (each machine
runs its own spawner enemies) — but these are divergences *within one shared key*, which that
note does not excuse.

### R129 (revisited). LOOT HIDING: the lane defect is fixed, the LIVE PROOF is still not reached

Session 17 left this NOT-REACHED with "the ledger was not observed to converge". Reading the
code found a real defect, and it is now fixed:

`LootHide` was called from exactly one place — inside `OnLoot`, and only when `Merge` returned
`changed != 0`. A loot body is also sent on a one-second timer, so the tombstone for a drop the
partner took arrives REPEATEDLY and after the first receipt `changed` is 0 forever; and the drop
may not have been SCANNED yet at that first receipt (the scan walks the object graph a slice per
frame), so the one chance was spent before there was anything to hide. Both halves are now
re-derived from the ledger every pass, with the Blueprint call bounded to one attempt per drop
(`LootActorRec::hideTried`). `test_loot.cpp` pins the property it depends on — that `IsTaken` is
a STANDING answer while a repeated `Merge` is not a change (`standing/a repeat of the same
tombstone is NOT a change`, 52 assertions, was 43).

**It is still NOT-REACHED live, and the reason is now measured rather than mysterious.** Five
attempts, and the blocker is the harness's ability to make the taker stand on a spawned drop:

```
dev teleport near 'loot' -> loot at 8227,-3765,3918 dist=4595; standing at 8115,-3682,3918 offset=140
```

`near:` addresses a CLASS and always parks the pawn `offset` cm away, so it walks to a *placed*
pickup (a coin `dist=3947`, or `BerserkFruit_Pickup_Bp4 at 12155,1581,-2916 dist=7691`) rather
than the drop the tombstone names. The drop probe confirms the spawned drops are real and
keyable (`isADrop=1 hasGUID=? lootKey=yes path=...Herb_Item_BP_C`) but they are NOT in the
`scan: kind pickup` set the harness can address by GUID — a spawned drop is not a savable and
has no saved GUID, which is the same reason Session 17 hit a wall.

`loot: local pickup` DID fire in one run (`3:06:26 INFO loot: local pickup D78506BC#0 (?) --
tombstoned for the partner`, six more on the peer), so the taker half works. The hiding half
needs a scenario that puts the two machines on the SAME spawned drop, which is a harness problem
whose answer is a lever that addresses a drop by `{source_hash, drop_index}` — the key the lane
itself uses. That is the next session's first job, and it is stated as the job rather than as a
mystery.

### R139. THE REMAINING FAULT WAS THE GOBJECTS WALK ITSELF — fixed, from a real dump

The final soak boot crashed the peer 4130 s in, and this time UE wrote a minidump with **three
of our frames** in it. Resolved with the map:

```
WINMM+0x44107   HK_Tick+0x497
WINMM+0x4afd8   LocalEquipPump+0x28
WINMM+0x6fde1   ScanLocalEquip+0x371      <- the faulting frame
```

and the instruction at the top of that frame, read out of the dump rather than inferred:

```
000000018006FDE1  mov rcx,qword ptr [rax+10h]     ; ObjClass(o), rax = the object pointer
ExceptionCode:    c0000005 (Access violation) reading 0x0000017efb13d040
```

**This is not a cached pointer at all.** `ObjectAt(i)` returns whatever is in slot `i` of
GUObjectArray, and UE only NULLS that slot when it destroys the object — so between destruction
and that write the slot holds an address whose memory has been freed and possibly reused.
Reading that object's `Class` field is then a read of freed memory. It happens once per 500 ms
over ~70k slots per pass, which is why it took 4130 s to hit and why it looked like streaming
churn rather than a loop defect.

**The fix is one guard, and the interesting part is that the guard already existed one file
over.** `qdwrite.h`'s `BuildActorStateOwnersGuarded` has been fault-contained as a unit since
Session 17 R125, with a comment naming precisely this hazard ("each of them dereferences a slot
the engine may be tearing down"). `ScanLocalEquip`'s loop body had none. It does now, per object
rather than per pass, so one bad slot costs that one candidate instead of the frame's whole
equipment scan.

This is also the explanation for a number Session 17 could only record: it listed
`WINMM+0x6f4d7` as one of the three play-set crash sites and wrote "the remaining fault is in a
path the session did not isolate". It was this path, and it was never a cached pointer.

**What this fix does and does not claim.** It closes the mod-side fault that the 04:53 dump
pins, and no dump after it faults there. It does **not** make the soak survive: the very next
run still died, and that death is inside the game's own code — see R140, which is the honest
account of what the session's last run shows.

### R138. One earlier crash that is a DIFFERENT fault — recorded, not merged

Before R139 there was one crash whose dump shows the EXECUTE bit set:

```
ExceptionAddress: 00000247d7e1bf40
ExceptionCode:    c0000005 (Access violation)
Parameter[0]:     0000000000000008          <- 8 = EXECUTE
rax=00000247f4084840
0x00007ffc015b3c54  mov rax,[winmm!g_oTick]
0x00007ffc015b3c60  call rax                   <- HK_Tick+0x30
0x00007ffc015b3c62  movzx r14d,al              <- HK_Tick+0x32, the frame in PCallStack
```

`g_oTick` — the trampoline pointer MinHook hands back for `FEngineLoop::Tick` — held a heap
address whose low bits match a framebuffer pointer from the same register dump. That is a
function pointer overwritten with unrelated data, in the tick hook's *prologue*, before any pump
runs. It did not reproduce on a re-run of the same phase, nor across four subsequent ten-minute
soaks.

It is kept separate from R139 deliberately: two crashes, two mechanisms, two dumps, and only one
of them is understood. Folding them together because both say `WINMM` would be the same mistake
as Session 17's combined `ERR|EXCEPTION|ACCESS_VIOLATION` gate. **R138 is open.**

### R140. WHAT THE SESSION'S LAST RUN ACTUALLY SHOWS — two crashes, and only one is ours

The fully-guarded binary was run on the same claim and a process still died, at 330 s. That
looked like the fix failing; the dump says otherwise, and the distinction is the whole point of
this section. **Two dumps, two different faulting modules:**

| dump | `ExceptionAddress` | `Parameter[1]` | whose code |
|---|---|---|---|
| 04:53 (pre-guard) | `winmm+0x6fde1` — `ScanLocalEquip+0x371`, `mov rcx,[rax+0x10]` | `0x17efb13d040` | **ours** |
| 05:20 (post-guard) | `QueensDomain_Win64_Shipping+0x1145580` | `0x1e3050060` | **the game's** |

In the 05:20 dump the fault happens INSIDE the game's code; our module appears only as a
*return address* one frame above it (`WINMM+0x43f02` = `HK_Tick+0x32`, i.e. the `movzx` right
after the `call` into the engine's tick). The game frames below it are identical to the earlier
dump to within two bytes (`+0x1066134`, `+0x10662c3`, `+0x2cd5299`, `+0x29afbcd`, `+0x28c506d`,
`+0x775b80`), so this is the same engine call site crashing on the same malformed pointer — and
the pointer comes through `HK_Tick`'s forward to `FEngineLoop::Tick`, not from any read the mod
makes.

What this establishes, stated as narrowly as the evidence allows:

* the crash R139 names is **gone from the dumps** — no dump after the guard faults in
  `ScanLocalEquip`, where the previous one did;
* the soak still dies, and the surviving failure is **inside the engine**, reached through the
  tick the mod forwards. That is the same relationship `HK_Tick` has with the game on every
  frame, so a corrupted pointer delivered TO the engine is a candidate cause, and the mod is a
  candidate source of it — but the evidence in hand does not name it, and neither dump shows a
  mod-side fault.

The honest summary of the soak claim for this session is therefore:

```
PERFORMANCE AT THE DOCUMENTED PLAY SET, ALL RUNS
  survived 600 s / 182-185 iterations ...... 4 runs  (2 boots, 2 binaries)
  died at 248 s, 4130 s, 330 s ............. 3 runs
  of those 3, faults inside winmm .......... 1  (ScanLocalEquip, fixed)
  of those 3, faults inside the game ....... 2  (post-fix; engine-side)
```

**R140 is open**, with both dumps kept in `_test/s18-soak-hub/inst2/Saved_qd2/Crashes/`, so the
next session can attack it from a stack rather than from a summary.

### R141. THE LONGEST RUN OF THE SESSION — 105 minutes at the play set, zero faults

After the guard, the same phase was run again and both processes were left running past the end
of the harness's own soaks. Verbatim, from the two module logs:

```
qdcoop-qd1.log  AV=0  EXC=0
   start: 5:26:16.018 INFO substrate: globals=1 fname=1(GameEngine) gobjects=1 objects=70103
   end  : 7:11:29.916 WARN reliable send abandoned: kind=00000028 seq=420030 attempts=5
qdcoop-qd2.log  AV=0  EXC=0
   start: 5:26:22.694 INFO substrate: globals=1 fname=1(GameEngine) gobjects=1 objects=70103
   end  : 7:11:30.042 WARN no free reliable slot; dropping kind=0000002B
```

That is **105 minutes of continuous play-set traffic on both machines, zero
`ACCESS_VIOLATION` and zero `EXCEPTION` lines**, with the soak loop still driving `qd.hit` every
three seconds at the end (`WARN rig: hit -- no live enemy within 15 m of -11686,-5480,-19702`,
7:10:52) — i.e. the claim of 2 × 600 s was exceeded by a factor of five, twice over.

The harness did not print its own verdict table for this run: its per-key log scan is O(keys ×
log size), the log reached 17 MB, and the analysis was still running long after the soaks had
finished. That is a harness limitation, recorded as such — the evidence above is read straight
from the module's own logs, which is the stronger source anyway.

### Live proof, phase by phase

| Claim | Verdict | Key line |
|---|---|---|
| The new binary loads and verifies | **PASS** | `targets: 36 byte-verified, 1 relocated-pointer, 6 runtime-validated, 0 mismatched` |
| The substrate validates | **PASS** | `substrate: globals=1 fname=1(GameEngine) gobjects=1 objects=70103` |
| The weak-resolved pawn is readable | **PASS** | `scan: local at 4106,-1744,140 yaw 174` |
| `EnemyRec::actor` resolves across the churn | **PASS** | `enemykey n=627 alive=176 dead=451 killlog=324 ledger=61 mirrors=0` |
| `ScanLocalEquip` (crash site 2) completes | **PASS** | `equip scan: RelativeScale3D@308 StaticMesh@1160 IsVisible=1` |
| The play set survives 600 s, run 1 | **PASS** | `soak run 1: 600 s of 600 s, iterations=184` |
| The play set survives 600 s, run 2 | **PASS** | `soak run 2: 602 s of 600 s, iterations=185` |
| The play set survives 600 s in 780 | **PASS** | `soak run 1: 602 s of 600 s, iterations=175` |
| 105 min continuous, zero faults | **PASS** | `AV=0 EXC=0` on both logs, 5:26:16 → 7:11:29 (R141) |
| Kill sets diff 0 | **PASS (once) / FAIL** | hub 242 s: `host-dead=448 peer-dead=448 diff=0`; but `diff=3`, `diff=7`, `diff=34` in three longer runs — R137 |
| Shared-enemy HP equal | **FAIL (open)** | `HP MISMATCH 70F9CADA: host=85 peer=30`; `5D370AC1: host=909 peer=0` (R137) |
| Peer actor-state writes land | **FAIL (open)** | 8005 × `actor-state: game writer ... (WRITE FAILED)` — R136 |
| `loot: local pickup` on the taker | **PASS (measured)** | `loot: local pickup D78506BC#0 (?) -- tombstoned for the partner` |
| `loot: hiding local drop` on the other | **NOT-REACHED** | reconciliation fixed; no scenario can yet stand the taker on a SPAWNED drop — R129 revisited |
| Every soak survives | **NO (4 of 7)** | R139 and R140 name the three deaths: two mod-side (both fixed), one engine-side (open) |
| The smoke phase | **PASS 19/0** | `=== 19 PASS, 0 FAIL ===` |

### What this session settled

The fault Session 17 could only name by offset is now a function, a line, and a cause, and the
cause was not in one lane — it was the shape every lane used to hold an actor. Eleven records and
eighteen globals cached a raw address and re-checked it with a test that reads the address it is
checking. That is now one helper, one invariant, and a unit test that frees a slot, reuses it,
and proves the handle refuses.

The soak is no longer the black mark it was, and it is not yet closed. 105 minutes of
continuous play-set traffic with zero faults is the strongest single result of the session, and
it sits beside three deaths that are named rather than hidden: two in the mod (the cached-pointer
caches and the unguarded object walk, both fixed, both proven fixed by later dumps) and one
inside the engine (R140, open). What is left is documented and smaller in scope than what was
found: an actor-state lane where the peer's writes do not stick (R136), three enemy-identity
rules that let two machines disagree about one shared key (R137), and a loot-hiding fix that has
no live proof only because no lever can yet address a spawned drop (R129).

**What a human tester needs to know:** the play-set configuration survived two consecutive
ten-minute sessions in the hub, a ten-minute session in `780_SZ_Depths`, and one continuous
105-minute session — but it also died in 3 of 7 attempts, and one of those deaths is inside the
game's own code and is not yet understood (R140). Play in reasonable stretches and keep the
crash folder if the game vanishes. Progression may still lag on the peer in a chest-dense area
(R136), and an enemy's health can differ between the two screens for a shared key (R137).

---

## Session 19 — 2026-11-10 (R140 named, the class cache, and the bar)

This branch is `sync-fix` at `56e6313` (the Session 18 merge), with `fc59363` ("resolve
UFunctions per class") and `b1f8bc8` ("Escape pause menu no longer slows the shared
world") merged in.  The soak body is Session 18's, unchanged (`tools/session18.ps1`,
documented PLAY set, `-Repeat`, roles from `session established`).

### R140. THE DEATH INSIDE THE GAME'S OWN DESTRUCTION PASS — and the call that caused it

The kept dump from the 330 s run was read with cdb and the map, frame by frame.  The
fault is `call qword ptr [rax]` at `QueensDomain+0x1145580`, inside the engine's object
destruction pass, and the full stack is:

```
FEngineLoop::Tick                 game+0x775b80
  <- UGameEngine::Tick            game+0x28c4ea0
       <- UWorld::Tick            game+0x29aeec0
            <- the WorldTickMisc timed section
                 <- the destruction pass  game+0x11453f0   call [rax]
winmm!HK_Tick+0x32                the return address of the tick we forward
```

Our module appears **only** as the tick forward, exactly as Session 18 stated.  What
the dump also shows is *where the object came from*: `rax = 0x1e3050060` (and
`0x1e3030040` in the next dump) is the first qword of a block that has been freed and
reused — the next dump's block holds two `FTransform`s.  The engine's destruction list
held a dangling `UObject*`.

The trigger was found by reproducing it.  Every death landed **~250–260 s after process
start**, during a large area stream-in, immediately after a burst of
`enemy death detected ... (actor gone)` in one frame.  The mod-side call that caused it
is the one the coordinator's fatal named:

> **`g_enDmg` cached `TryApplyDamage` from the first enemy hit.**  When that was
> `Dragon_Green_BP_C`, its override — which schedules `CheckForSplit` **by name** — ran
> on a `Crawler_Rotten_BP_C` / `Golem_Tree_BP_C`.  In `780_SZ_Depths` that aborted both
> processes with `LowLevelFatalError: Failed to find function CheckForSplit`; in the hub
> the same wrong-class bytecode wrote through the dragon's layout into a different
> actor and corrupted the heap the engine's purge later walked.  That is why the fault
> surfaced in the engine and not in a mod frame.

**Fix (merged `fc59363`):** `Fn::Resolve` re-checks the cached UFunction whenever the
object's class changes (`Fn::forCls`), and the enemy damage API is per class
(`EnemyDamageApi`).  Recorded as **R142**.

Hardening landed in this session as well, all of it aimed at the same rule — never hand
the engine a target it did not validate:

* `ObjectCallable` (native/src/qdsubstrate.h) refuses a ProcessEvent target on the
  **engine's own** deny bit (`FUObjectItem::Flags` bit 29, measured in the
  `0x011A7AA0` prologue: `shr eax,0x1d; test al,1`) plus `RF_BeginDestroyed` /
  `RF_FinishDestroyed`; every reflective call goes through it.
* `ActorCallReady` adds the actor's `ULevel` must still be one of the world's levels;
  `SetActorHidden` / `DestroyActor` / spawn use it.
* The mod no longer calls `K2_DestroyActor` on a gameplay Blueprint it spawned (enemy
  mirrors, player-class body, puppet): it **hides** it and lets the engine reclaim it.
  `DestroyActor` also refuses a second destroy of the same `{slot, serial}`.
* The death sweep refuses a world that is not callable, requires `seenAlive` for
  "actor gone", treats more than 8 disappearances in one sweep as a level unload (not
  8 kills), and does not re-write an `hp=0` record on every spawner respawn.

### R142. THE WRONG-CLASS UFUNCTION — the fatal the coordinator reported

`Failed to find function CheckForSplit in Crawler_Rotten_BP_C` and the same in
`Golem_Tree_BP_C`, on both machines, was the class-cached damage entry above.  The fix
is `fc59363`; the unit suite is green.  This is the same mechanism as R140, recorded
separately because the fatal and the engine-side fault are the same bug seen through
two different outcomes.

### R137. KILL SETS — three named causes, fixed, with the measured numbers

The three causes Session 18 named were each given a fix:

* **spawner respawn**: a death this machine recorded is no longer erased when its own
  spawner respawns the enemy; the respawned copy is re-killed until a rest clears it.
* **summon key collision**: `enemyid::SummonKey` now includes the enemy **class**, so
  one summoner calling two classes at one point cannot collapse them onto one key.
* **rowless HP 0**: a key with no readable row, or never seen alive, reports `hp=?`
  (unknown) instead of 0, and HP<=0 counts as dead only after `seenAlive`.

Measured: 300 s hub soak — `host-dead=22 peer-dead=22 diff=0`, shared HP equal (11
checked); 242 s hub soak — `host-dead=21 peer-dead=21 diff=0`, shared HP equal (14
checked); 601 s hub run 1 — `diff=0`, HP equal (6 checked); 601 s hub run 2 —
`diff=1` (`FA2FC279` dead only on the host).  So the fixes remove the systematic
divergence; **one host-only death in a 600 s run remains open** and is the next
enemy-identity item, not a bar item.

### R143. BOSS HP DESYNC — one bar, one key: the binding is fixed, the key choice is not

Live evidence from the running pair (the user's report): the host held its bar on
`03FDBC21` at full HP and re-activated it every 3 s (`boss: sent ... hp=65535 (repair)`),
while the peer's dragon key `17181166` was dead and its bar was down; the peer also
logged `boss: no local copy for guid=03FDBC21` every repair.  Two different boss keys,
one bar each.

What was wrong, precisely:

* the pump raised and **published a bar for every boss record** — the game has ONE bar,
  so the last activation won, and the two machines could bind different keys;
* the binding was a per-record boolean with no actor identity, so a **respawn or mirror
  adoption never rebound** the bar — it kept showing the old actor's HP;
* a dead/stale key could keep its bar.

Fixed: the host selects one active boss (the most-damaged live one, ties by key),
announces only that key, and both machines bind their own bar to the actor that carries
it (`BossBarSet` is a move and rebinds on actor change); a dead/stale key never holds the
bar; the active boss's death sends one explicit bar-off so both screens reach 0 without
waiting for the silence timeout; world change and session end lower every bar.

**The live proof found the residual.**  In the 242 s run the bar code followed one key
per machine, but the machines still held **different keys**: the host's active boss was
`03FDBC21`, which exists only on the host (the peer has no copy and refuses every raise),
while the peer's `17181166` is the dragon the host keeps re-killing as a standing death.
That is a **key presence / liveness divergence**, not bar binding.  **R143 is therefore
open on the selection step:** the host must not select a boss the partner has no copy of
(a refusal/ack on the boss lane), and the `17181166` alive-vs-dead disagreement (peer
respawned it, host holds a standing death) has to be reconciled before "both bars agree
and both reach 0" can be claimed.  The binding half is done and is in the build.

### R144. ESCAPE PAUSE — proven with the game's own menu-dilation path

Merged `b1f8bc8`: the `PauseMenu_C` exception is gone from `HK_SetGlobalTimeDilation`
and `MenuDilationPump`.  Live proof (the 242 s run): the rig's own menu lever issued
`SetGlobalTimeDilation(0.20)` on the **peer** with a pretend menu open, and the peer's
next `clock:` line read:

```
9:17:44.918 rig: menu lever -> pretend menu open, SetGlobalTimeDilation(0.20) issued; dilation 1.00 -> 0.20
9:17:48.332 clock: dilation=1.00 real=252.1 unpaused=231.5 ...
```

The world was restored to 1.00 and `unpaused` advanced through the window.  Posting
`VK_ESCAPE` to the peer's off-screen window did **not** open the menu (`menu=-` on every
line), so the lever — the exact setter a menu calls — is the proof of the suppression
path; the Escape key itself cannot be delivered to gameplay input by `PostMessage`
(the R47 result).

### The soak record, measured

| run | duration | survived | kill diff | HP of shared keys | faults |
|---|---|---|---|---|---|
| 300 s hub (pre-merge guards) | 302 s | both | 0 (22/22) | equal (11) | 0 |
| 300 s hub (post-boss build, diagnostic) | 242 s | both | 0 (21/21) | equal (14) | 0 |
| 600 s hub run 1 (`fc59363` merged) | 601 s | both | 0 (61/61) | equal (6) | 1 **caught** ProcessEvent fault (puppet move), process survived |
| 600 s hub run 2 | 601 s | both | **1** (142/141, `FA2FC279`) | — (0 shared alive) | 0 |
| 600 s hub run 3 | — | — | **invalid** | — | both worlds diverged: host `780_SZ_Depths`, peer `300_LIB` |

So: **the crash is gone from every run after `fc59363`** (zero uncaught faults; the one
caught fault is inside `ProcessEventGuarded` and the process lived), and the kill-set
convergence is systematic but not yet universal (one key in one 600 s run).  The 3 × 600 s
"all diff 0" claim is **not** met; run 3's world divergence invalidates it and is the
next streaming item.  The 242 s run that also carries the boss-bar and menu changes is
13 PASS / 0 FAIL.

### R138. still open, and now better explained

The EXECUTE-bit fault of Session 18 (`g_oTick` overwritten) never reproduced.  The
wrong-class UFunction of R142 is a plausible corruption source for it as well: a dragon
override running on another class can write a function pointer-shaped value into memory
before the next tick.  It is recorded as a candidate, not as a closed cause.

### What a human tester needs to know

The crash that killed the play set is understood and fixed at its source (a per-class
UFunction cache), the Escape menu no longer stops the shared world, and the boss bar is
now one bar bound to one key with rebinding — but a boss that exists on only one machine,
or whose liveness the two machines disagree about, can still show two different bars.
Kill sets converge on the order of one stray key per ten minutes; keep the crash folder
if the game vanishes.

---

## Session 20 — 2026-11-10 (the bar both machines can show; the stray key; the area that follows)

This branch is `t3/coop-desync-boss-world-soak`, reset to `sync-fix`'s Session 19 merge
(`118b9da`).  The body is `tools/session20.ps1` — Session 18's soak with the boss fight and
the area follow added — run once per phase: `boss`, `soak-hub` (three back-to-back runs in
one pair of processes), `soak-780`.  Every number below is from this branch's build.

### R143. CLOSED — one bar, one key, and the peer must be able to SHOW it

The measured failure (Session 19, the user's report): the host repaired `03FDBC21` at full
HP every 3 s while the peer logged `no local copy`, and the peer's own `17181166` was a
dragon the host held as a standing death — two different bosses, one bar each.

**What was wrong, and what it took to close it.**  Three defects were needed, and the
first two were only visible in the live pair:

1. **The selection was unnegotiated.**  The host picked a key and published a bar for it;
   nothing told it the peer could not show that key.  The lane is now a round trip: `ASK`
   (host → peer), `ACK`/`NACK` (peer → host).  A `NACK`, or 1.5 s of silence, makes the key
   ineligible for 3 s and the host chooses the next candidate; `BAR_ON` is sent only after
   an `ACK`.  **If nothing is showable on both machines, no bar is raised on either** —
   the honest agreement.
2. **Liveness was read from the local copy.**  A follower's own respawned copy read ALIVE
   while the owner held the key dead.  Liveness is now the owner's
   (`enemyown::OwnerSet::Running`): the follower takes the owner's row verbatim, and an
   explicit bar-on from the owner retracts a stale local death.
3. **"Most-damaged live boss" was not enough by itself.**  A boss 71 m away whose encounter
   was not running (`Whaleking_SZ_BP_C`, key `A8772963`) was ACKed, bound and sampled
   `hp=0` for a whole run while the dragon was being killed.  A candidate now needs a
   READABLE HP.  A "prefer the key already bound" rule was tried and removed: it held
   `17E153F4` (8 km away, untouched) while the dragon died.
4. **The peer had no copy of the dragon at all in one run.**  The host described its class
   177 m away *before the peer's position was known*, marked `mirrorPathSent` forever, and
   never re-sent it; the boss lane then correctly NACKed.  `EnemySpawnPump` now requires a
   known audience, and a boss `NACK` re-arms that key's class description, so a peer that
   gains a copy joins the fight.

**Live proof, `780_SZ_Depths`, final build (11:10).**  One line a second from each machine,
and the kill:

```
sample t+ 2s: host=17181166/50555  peer=17181166/50555
sample t+ 4s: host=17181166/35576  peer=17181166/35576
sample t+ 6s: host=17181166/20596  peer=17181166/20596
sample t+ 8s: host=17181166/5616   peer=17181166/5616
host  boss: sent bar off key=17181166 hp=0 (the active boss died or left)   11:10:59.285
peer  boss: bar lowered for key=17181166 hp=0 (the partner says ...)        11:10:59.316
```

Same key on both bars, identical HP at every sample (max delta 0 of 65535), both bars to 0
**31 ms apart**.  `=== 19 PASS, 0 FAIL ===`, zero faults.  `boss: bar key=<8hex> hp=<n>` is
the standing sample; `-1` means "HP unreadable", never 0.

### R146. CLOSED — the stray kill key was a death the lane never sent

Session 19 left `FA2FC279` dead on the host only, once in a 601 s run.  The key is a
**spawner GUID** (`enemy id: spawner FA2FC279 (Mossy_Fighter_BP_C)`), i.e. `SRC_SPAWNER`,
and the peer held no copy of that spawner.  Two holes:

- the death sweep ran only for keys the machine **owned**, so a death observed on a
  non-owned copy was in no outgoing lane at all;
- the per-key dump called "no live actor" a death even for a record it had **never seen
  alive**, so the dump could claim a death the detector had correctly refused to broadcast.

Now an observed death (the game's dead flag, HP ≤ 0 after being seen alive, or an actor
that vanished after being seen alive) is broadcast as a death claim by the observing
machine, without writing the save; a record with no live actor and no evidence is omitted
from the dump and counted as `stale=`.  The mass-disappearance guard counts every
non-mirror record, so a level unload is still one event, not N kills.

### R147. CLOSED — the partner follows into the area, or the area is refused

Session 19's run 3 ended with the host in `780_SZ_Depths` and the peer in the hub.  Entering
an area is a *streamed sublevel*, not a `UWorld` switch, and the entry was a LOCAL lever on
each machine — nothing carried it across.  Travel phase `4` (`TP_AREA`) now announces the
area in `dest` (`map` stays the `UWorld`, because the divergence board consumes `map`
before it reads the phase), and the receiver streams the same area through the game's own
level-list path and pins it.  A name it cannot enter is
`travel: REFUSED the partner's area <name>: <why>`.

That last detail was found by the check this session added: the first version put the area
in `map`, the divergence board compared a world against an area, and the `world:` line read
`partner=780_SZ_Depths` while both machines stood in `Ruins_Level_00`.  The run's kill diff,
HP and area all passed, but the same-world claim was **not** made; the encoding was fixed
and the boss proof and the 780 soak were re-run on the fixed build.

Live proof: in every area entry this session (boss and both 780 runs) the peer logged

```
travel: entered the partner's area 780_SZ_Depths by streaming it (loaded and SHOWN here)
```

and both machines then reported `rig: level engine state 780_SZ_Depths LoadedLevel=1
actuallyVisible=1`, with the players 0–3 cm apart at the anchor.

### The soak record, measured

| run | duration | survived | kill diff | shared HP | world / area | boss bar | faults | ERR |
|---|---|---|---|---|---|---|---|---|
| hub 1 | 604 s | both | 0 (77/77) | equal, 4 checked | `Ruins_Level_00` / – | 16 paired, delta 0 | 0 | 0 |
| hub 2 | 602 s | both | 0 (141/141) | 0 comparable | `Ruins_Level_00` / – | 16 paired, delta 0 | 0 | 0 |
| hub 3 | 605 s | both | 0 (210/210) | 0 comparable | `Ruins_Level_00` / – | none existed | 0 | 0 |
| 780 | 603 s | both | 0 (64/64) | equal, 3 checked | `Ruins_Level_00` / `780_SZ_Depths` | 14 paired, delta 0 | 0 | 0 |

The three hub runs are back-to-back in ONE pair of processes, which is the claim Session 19
could not meet (run 3 diverged).  **All three met every requirement.**  The `shared HP`
column is honest about what the end-of-run dump could compare: run 1 ended with five keys
alive on both and four readable HPs (all equal); runs 2 and 3 ended with one shared live key
whose HP was not readable (`hp=?`), so there was no pair to compare — that is "no mismatch
was observable", not "measured equal".  The boss-bar samples are the stronger per-run HP
evidence where a bar existed (delta 0 over 16 samples).

Hub run 3 is also the first live look at the **new** rule behaving correctly: after the
dragon died, the only live hub boss was one the peer holds no copy of, so for the rest of
the run **no** bar existed on either machine.  The old code's symptom — one screen full,
the other empty — did not occur.

### What is still open

- **R138** (the EXECUTE-bit fault in the tick forward) still has not reproduced; Session 19
  recorded it with the wrong-class UFunction as a candidate cause.
- **R140**'s engine-side death did not occur in any Session 20 run (0 faults in five runs),
  but that is "not seen", not "fixed".
- **R136**'s `actor-state ... WRITE FAILED` symptom did not appear in the final 780 soak
  (`ERR lines=0`); it was not fixed, only absent from this run.
- **R129** (loot hiding) was not attempted this session.
- **Boss phases** remain unexpressible (`Phase`/`bBoss`/`OnSubStateChanged` exist in no
  cooked package); the sample carries an observed ordinal, 0 otherwise.
- The boss negotiation is chatty by design: a key the peer genuinely lacks is re-asked
  after the 3 s window.  The log line is now WARN once a minute per key (246 identical
  lines in one 600 s soak buried the lines that mattered).

---

## Session 21 ? 2026-11-10 (the ghost behind hp=?, the drop the lever can reach, and a soak that ran out of time)

This branch is `t3/session-21-loot-hp-soak`, reset to `sync-fix`'s Session 20 merge
(`1e2608a`).  The body is `tools/session21.ps1`, one phase per invocation:
`probe` (lever latency), `lootdrop` (R129), `soak30` (the 1800 s run), `repro`
(R138/R140).  Configuration is the documented PLAY set with `LogLevel=3`; roles are read
from `session established`: **host=inst1, peer=inst2**.

### R148. THE GHOST BEHIND `hp=?` ? a live actor the engine refuses to call

Session 20 ended with one shared live key whose HP printed `?` on both machines, so
"shared HP equal" had no pair to compare.  The dump now prints WHY, and one live dump
answered it in two lines:

```
enemykey 976CB2E9 ALIVE local=1 src=0 hp=? why=callfail seen=0 inworld=1 cls=Default_Lizer_02_C
enemykey 93494995 ALIVE local=1 src=0 hp=490.0                       cls=Default_Lizer_07_C
```

Two instances of ONE class, one readable and one not ? so it is neither the class nor a
missing attribute component.  `why=callfail` means the getter resolves and the reflective
call is **refused**, which is the module's own `ObjectCallable`: the object item's deny bit
(`FUObjectItem::Flags` bit 29, `EInternalObjectFlags::PendingKill` in this build) or
`RF_BeginDestroyed` / `RF_FinishDestroyed`.  `inworld=1` rules out "the actor is out of
range", and `why=nofn` never appeared, ruling out "the attribute component is not there".

**How much of the hub this was**: one dump counted 569 records, of which **428 were such
ghosts and 141 were usable**.  The copies the soak was trying to compare HP on were, in
the main, objects the engine was tearing down ? not enemies.

**The fix.** The dump reports them as `GONE`, which is neither `ALIVE` (that is what made
the comparison unmeasurable) nor `DEAD` (that would invent a kill no lane broadcast), and
it is skipped by both the kill-set diff and the HP comparison.  `usable = live &&
callable` replaces `live` for the whole per-key report, which is the same rule every
reflective call in this module already goes through.  `HpKnown` also accepts a POSITIVE
reading without `seenAlive` ? `seenAlive` is set by the death sweep, which is suppressed
across a load and early-returns on a mass vanish, so the old rule withheld a number that
was there.

`HpUnknownWhy` is the single place the reason is decided, pinned in `test_enemyid`
(**206 assertions, was 176**), so the dump and its test cannot disagree.

### R149. THE HARNESS WAS MEASURING WITH A STOPWATCH IT NEVER CHECKED

`lootdrop` failed three times with "no enemy key is ALIVE on BOTH machines" about two
machines holding 141 and 112.  The cause was the harness, and the new `probe` phase is
what found it.  Writing `qd.enemies` and reading the log 3 s later found the host's dump
and nothing on the peer.

```
[1] dump 1: answered after 33.1s     <- the FIRST dump of a boot
[1] dump 2: answered after 0.8s
[2] dump 1: answered after 0.5s      <- every later dump, both machines, 0.5-1.1 s
```

A control-file lever answers in about a second once warm; the first enemy dump of a boot
costs **33.1 s**, because the per-class UFunction resolution that makes `GetHP` readable is
paid by whichever dump happens first.  Every dump and drop probe now polls for its lines
with a real timeout and records how long it took, and `probe` is a standing phase so the
next session can re-measure rather than assume.

Two other harness defects, both the same family ? a number printed under the wrong label:

* `Note`/`Say` wrote to the **pipeline**, so a helper that both narrated and returned a
  value returned an array of its own notes.  `$key = Find-SharedDrop ...` then held note
  strings and the phase probed `near:drop::0` (`no live actor matches 'drop::0'` about a
  drop it had just found).  `session20.ps1` warns about this trap for `Check`; it was one
  level down.  Narration now goes to the host stream and the report file.
* the HP sample line had its `-f` indices off by one, so a sample read
  `shared-alive=378 compared=0` when it meant `elapsed=378 s, shared-alive=0`.  The
  counters were right and the labels were not; both are now explicit per field.

### R129. LOOT HIDING ? the lever works, and the two things that block the proof are named

**The lever exists and does what Session 18 said it would.**  `qd.tp
near:drop:<8hex>:<index>` resolves the loot lane's own key, and it answered on both
machines:

```
host  13:23:36.079 INFO rig: drop FB6F664F#0 resolved to a live actor at -25520,-16672,-15243
peer  12:48:41.827 WARN rig: no live loot-ledger drop matches 'drop:7DF53D72:0' (the scan has 2 drop(s))
```

So the resolver distinguishes "this machine holds that drop" from "it holds two other
drops", which is exactly what the proof needs.  **`loot: hiding local drop` still did not
fire, and the reason is not the reconciliation code** (Session 18's fix stands, and
`test_loot.cpp` pins it).  Two measured blockers:

* **The hub cannot prove it: the two machines' drop keys are disjoint.**  A key is
  `{source_hash, drop_index}` and the source is the enemy; the hub's machines run their
  own spawner enemies with their own authored GUIDs.  One run produced 3 keys only the
  host held and 2 only the peer held, **not one in common**:
  `rig: no live loot-ledger drop matches ... (the scan has 2 drop(s))` on the peer for
  every host key.  `PLAYTEST.md` section 8 says loot hiding needs the SAME enemy on both
  machines; this is that sentence as a measurement.
* **In `780_SZ_Depths`, where both machines DO hold the same authored enemy keys, the
  drop only appears on one machine at a time.**  Measured twice, both ways round: a host
  kill produced 4 drops, all on the peer; a peer kill produced 3 drops, all on the host.
  The phase then stood the pair 9 m back before the blow (a pickup is an overlap, so the
  killer's own pawn was consuming the drop it had just caused), and after that the killer
  did hold its own drops ? 3 keys on the host, 1 on the peer ? but no key was held by both
  machines inside the observation window.

**R129 is therefore still NOT-REACHED live**, and it is now blocked by a shutter rather
than by a mystery: to see the hiding half fire, one machine must hold a visible drop whose
tombstone comes from the other, and this content does not put the same drop on both
machines at the same time in any place the rig can reach.  The next probe is stated as a
probe: kill the same authored key on each machine in turn (a spawner respawn makes that
possible) and probe the key after each kill, rather than killing once and waiting.

### R150. THE KILL LEVER HAD THE SAME BUG AS THE COMPARISON ? it aimed at ghosts

`qd.hit` logged `ran=0 hp 0.0 -> 0.0 (local-run)` on **every** swing, in the hub and in
`780_SZ_Depths`, and `qd.enemykill` was the only lever that produced deaths.  The cause is
R148's: `DevKillNearest` picks the nearest three records **by distance**, and almost all of
them are mid-teardown, so the reflective damage call is refused.  The lever was testing the
one class of enemy that cannot be hit and reporting it as a failure of the damage lane.

Measured after the fix (`ran=1 hp 2590.0 -> -97410.0` on a `qd.hit` of 100000).  The same
rule is now in `near:enemy`, which used to park the player beside a ghost and make the whole
rig report "no live actor" ? two passes: a usable enemy if there is one, and the old
nearest-live answer only when there is not, so a caller can still look at a dying actor.

### The soak, measured ? and where it fell short

One pair of processes, the documented play set, `13:37:24` �� `14:16:30` (39 minutes of wall
clock).  Segments are Hub / 780 / 770 / hard-out+hard-back / hub-final, with the per-key
dump sampled through each.

| segment | duration | both alive | drops observed |
|---|---|---|---|
| hub | 602 s of 599 s, 177 iterations | yes | ? |
| area780 (`780_SZ_Depths`, TP_AREA) | 304 s of 301 s, 87 iterations | yes | ? |
| area770 (`770_SZ_Swamp`, TP_AREA) | **not run** ? the peer refused the follow | yes | ? |
| hard-out / hard-back | **not performed** ? the lever refuses every candidate by name | yes | ? |
| hub-final | 421 s of 419 s, 104 iterations | yes | ? |

**What passed**

* Both processes alive at the end of every segment that ran, and at `14:16:30`.
* **Zero uncaught faults** on both machines over 92 k and 40 k log lines
  (`ACCESS_VIOLATION=0`, `EXCEPTION=0`).  The peer logged **3 caught** ProcessEvent faults
  (`puppet move failed: ProcessEvent faulted`), which is `ProcessEventGuarded` doing its
  job ? the process survived, exactly as in Session 19.
* **Zero `ERR` lines** on both machines.
* Same world at the end ? `Ruins_Level_00`, `agree=1` on both ? and the same area pin
  (`900_SipehaWorld`) on both, with `WORLD DIVERGENCE` never printed.
* **Shared HP, measured: 71 pairs compared, 0 unequal, max delta 0.1 HP**, 1 unreadable,
  over 16 paired per-key dumps.  This is the first time the claim has a count beside it,
  and it is the number Session 20 could not produce.
* One TP_AREA entry followed in the measured direction: the peer logged
  `travel: entered the partner's area 780_SZ_Depths by streaming it`, and both machines
  then reported `level engine state 780_SZ_Depths LoadedLevel=1 actuallyVisible=1`.

**What did not**

* **The 30-minute requirement is NOT met.**  602 + 304 + 421 = **1327 s (22 minutes)** of
  soak body ran; two of the five segments were skipped because their preconditions failed.
  Reporting 1800 s here would be a lie about arithmetic, so it is not reported.
* **A second TP_AREA entry does not follow, and the refusal is specific.**  The host
  streamed `770_SZ_Swamp` (`PASS`), and the peer answered
  `travel: area 780_SZ_Depths is already pinned on this machine` ? an announcement for the
  area it already holds, not for the new one.  So a second area entry is a new gap
  (**R150a**), not a re-measurement of R147's.
* **A hard travel cannot be performed from the hub with this lever at all.**  Both real
  candidates answer by name:

  ```
  13:55:36.954 INFO rig: hard lever -> 990_B33sment   streamed=1 (a sublevel of this world ...)
  13:55:41.968 INFO rig: hard lever -> 900_SipehaWorld streamed=1 (a sublevel of this world ...)
  13:55:51.299 WARN rig: hard lever -> Ruins_Level_00  LoadMapWithLoadingScreen ran=0: already in Ruins_Level_00
  ```

  The hub's level table carries both worlds as streaming levels, so `FollowHardTravel`
  streams them instead of loading a world, and the remaining name is the current world.
  `PLAYTEST.md` already says a hard load only lands you in a real world; this adds that
  from `Ruins_Level_00` the rig's own hard lever cannot reach one.  **R150b.**
* **The kill-set diff was not measured.**  The harness's end-of-run dump and its
  per-key log scan did not finish (a 92 k-line log, re-read per machine per bucket); I
  stopped the run rather than let it sit.  The dumps it did take show 63 ALIVE / 137 DEAD
  on the host and 16 ALIVE / 139 DEAD on the peer, with **zero** keys alive on both ? the
  hub's by-design divergence, not a measurement of convergence.  The 71 compared HP pairs
  come from the samples taken *during* the run, which is why sampling was worth adding.

**R138 / R140 were not attempted.**  Every minute of this session went into the three
items above, and a reproduction run that never happened is not evidence about either.

### Live proof, phase by phase

| Claim | Verdict | Key line |
|---|---|---|
| The new binary loads and verifies | **PASS** | `targets: 36 byte-verified ... 0 mismatched` |
| The area follow of R147 still works | **PASS** | `travel: entered the partner's area 780_SZ_Depths by streaming it` |
| A second area entry follows | **FAIL (open, R150a)** | peer: `travel: area 780_SZ_Depths is already pinned on this machine` |
| A hard travel can be performed from the hub | **FAIL (open, R150b)** | `rig: hard lever -> 900_SipehaWorld streamed=1` |
| `qd.hit` lands damage | **PASS (after R150)** | `rig: hit enemy guid=350C0EAD amount=100000 ran=1 hp 2590.0 -> -97410.0` |
| Shared HP is equal where readable | **PASS ? 71 compared, 0 unequal, max delta 0.1** | 16 paired dumps |
| Zero uncaught faults over 39 min | **PASS** | `ACCESS_VIOLATION=0 EXCEPTION=0` on both logs |
| Zero `ERR` lines | **PASS** | `ERR=0` on both logs |
| Kill sets diff 0 | **NOT MEASURED** | the end-of-run dump did not complete |
| `loot: hiding local drop` | **NOT-REACHED** | R129: the same drop is never on both machines at once |
| The `near:drop:<key>` lever resolves the lane's key | **PASS** | `rig: drop FB6F664F#0 resolved to a live actor at ...` |
| The 30-minute soak | **NOT MET ? 1327 s of soak body ran** | the table above |

### What a human tester needs to know

Nothing here changes what a pair of players should do: the play set is unchanged and the
area follow still works for the first entry.  Two new things to know: **entering a second
area in one session does not move the partner** (R150a ? walk them in, or re-enter the
first area), and **an enemy's HP now reads `?` only when the engine is tearing that copy
down**, so a `?` in a dump is information about the engine, not about sync.

### What is still open

- **R129** loot hiding, NOT-REACHED, blocked by "the same drop on both machines at once".
- **R150a** a second TP_AREA entry is refused by name.
- **R150b** no hard travel is reachable from `Ruins_Level_00` through `qd.hard`.
- **R138** not reproduced; not attempted this session.
- **R140** not reproduced in this run (0 uncaught faults); not attempted deliberately.
- **Kill-set convergence** unmeasured this session; the sampling work makes HP solid and
  the same technique applies to the diff next time.
- **`Test-Alive` / end-of-run scans** are still O(log size) per bucket; a 30-minute run at
  `LogLevel=3` produces ~92 k lines per machine and the end checks did not finish.
---

## Session 22 - 2026-11-10 (the drop's identity, the area that can be left, and a hard travel that lands)

This branch is `t3/fix-loot-desync-area-soak`, reset to `sync-fix`'s Session 21 merge
(`1229c40`).  The body is `tools/session22.ps1`, one phase per invocation: `probe` (lever
latency), `smoke` (loot both ways, three consecutive area changes, one hard travel out and
back), `soak30` (the 1800 s run), `all` (smoke then soak30 in ONE pair of processes).
Configuration is the documented PLAY set with `LogLevel=3`; roles are read from
`session established`: host=inst1, peer=inst2.

### R129. THE DROP'S IDENTITY WAS THE WRONG FACT, AND ONE MACHINE OFTEN ROLLED NOTHING

Session 21 measured two things and could not connect them: the two machines' drop keys were
disjoint, and in `780_SZ_Depths` a kill put every drop on the machine that did NOT kill it.
Both are one root cause with two halves, and both halves are in Session 21's own log.

  * **The key could not agree.**  It was `{item class}@{quantized spawn point}#{coincidence
    ordinal}`.  The two machines' copies of one enemy die at different centimetres, so the
    point bucket differs; and the ordinal is spent per class-and-place, so one machine had
    `loot: drop 9FB8B0A8#1` for a drop the other called `#0`.  A key that is computed twice
    from two different histories cannot match.
  * **One machine often rolled no drop at all.**  A death the mod APPLIES goes through
    `ApplyEnemyDamage`.  For a class with no callable damage entry the mod falls back to an
    HP write (`damage: enemy hp via HP write ... the class has no callable damage entry`),
    and an HP write does not run the Blueprint death branch, so it never reaches
    `RollItemDrop`.  Measured four times in one run: `key 765952E9#0: host=absent peer=HELD`.

**The rule, and why it is the one that is true on this content.**  A drop's identity is the
DEATH that caused it: `{mixed hash of the enemy GUID, ordinal within that death}`.  Both
machines run the same death for the same GUID -- qdenemyown makes one the owner and the other
the follower -- so both compute the same key with nothing negotiated.  The machine that
ROLLED the drop owns it and describes it on the `LOOT` lane (item class, point, facing); a
machine that holds no copy CONSTRUCTS the same class at the same point under the same key,
and a machine that rolled its own already holds that key so it builds nothing.  The key does
the deduplication; there is no ownership election to get wrong.

The alternative -- "the death's owner spawns and everyone else suppresses its own roll" --
was written first and REMOVED, because it breaks the measured case above: when the machine
that ran the death rolls nothing, suppressing the other machine's roll leaves no drop
anywhere.  That is why the rule is "whoever rolled it describes it" rather than "whoever
killed it owns it".

`LOOT` grew four fields for this: `flags` (bit 0 = this entry carries a spawn point), `yaw`,
and the three quantized position words.  Native and Rust agree byte for byte; the golden
vector (`doc/vectors.txt`, regenerated by `cargo test -p qd-core`) and `test_wire.cpp` pin
the new layout.

**Measured.**
```
peer  loot: drop 54F6FD50#0 = /Game/Items/Consumables/Herb/Herb_Item_BP_C (the death this machine ran)
host  loot: constructed the partner's drop 54F6FD50#0 = /Game/Items/Consumables/Herb/Herb_Item_BP_C
          at -27846,-15403,-15339 (the originator ran the death)
rig   key 46FB326A#0: BOTH machines hold it
```
That last line is the precondition Session 21 could never reach, and it was reached in four
separate runs, in both directions (`key 54F6FD50#0: BOTH machines hold it` with the host
taking, `key B3987A61#0: BOTH machines hold it` with the peer taking).

**Still open: the HIDE half has never been live-proven, because the rig cannot make the game
consume a drop.**  Four mechanisms were tried, each measured failing:

  * a teleport INTO the pickup volume: `rig: drop 126D27A9#0 resolved to a live actor`,
    the pawn parked there for 30 s, nothing picked up (and the pawn then fell through the
    world, which is a separate content fact about that drop's spot);
  * stepping OUT to 500/800/250 cm and back IN -- three cycles, nothing;
  * dropping the pawn ONTO it from 3 m -- nothing;
  * the class' own verbs: `dev interact` now offers a drop its pickup verbs FIRST
    (`dev interact: drop verbs tried; ran=Pickup`, target `Herb_Item_BP_C`), and the actor
    is still standing afterwards with state 0.

So `loot: local pickup` and `loot: hiding local drop` still have not fired in a controlled
take.  The lane's ledger half -- a tombstone wins for ever, a re-observed roll cannot
resurrect it, one take only -- is pinned by `test_loot.cpp`; what is missing is a way to
make the GAME take it.  The next probe is stated as one: the Blueprint child has ZERO
UFUNCTIONs of its own (`rig: efunc -> Herb_Item_BP_C`, `Herb_Item_BP_C has 0 UFUNCTIONs`), so
walk `SuperStruct` to `ItemPickup_Base` and call the parent's overlap entry with the pawn as
`OtherActor`, or drive real player movement instead of a position write.  **R129 is
PARTIAL: the identity and the mirror are fixed and live-proven; the take is not.**

### R150a. THE PIN COULD NOT BE CLEARED, AND A STALE PIN WON

Session 21 recorded `travel: area 780_SZ_Depths is already pinned on this machine` as the
reason a second area entry did not follow.  Three causes, all measured this session:

  1. **The pin was a one-way latch.**  `g_rigLevelPin` was set and never cleared, and the
     announcement block ran only `if (g_rigLevelPin[0])`, so "we went back to the hub" (no
     area) could not be expressed at all.
  2. **Both machines re-announced their own pin every 2 s**, so a STALE pin beat a fresh
     change.  Measured in Session 21's own soak log: the host streamed `770_SZ_Swamp` at
     `13:53:55.575` and was back in `780_SZ_Depths` at `13:53:57.167` because the peer's
     echo of its old area was applied.  That is why the peer never followed into 770.
  3. **A pin applied from the partner was announced back**, which closes the echo loop.

Now: leaving is a real operation (`ClearAreaPin` hides the sublevel through the same
`ForceLevel` the pin used), the leave is announced with a `-` sentinel (`dest = "-"`), a
partner-applied pin is never announced back, a machine that chose its own area within the
last 8 s keeps it against an in-flight announcement, and the heal interval is 10 s.

**Measured, three consecutive changes, both machines:**
```
17:36:47.725  peer  travel: entered the partner's area 780_SZ_Depths by streaming it
17:42:38.967  peer  travel: entered the partner's area 770_SZ_Swamp by streaming it
17:49:05.102  host  travel: left the area 770_SZ_Swamp (the rig asked to leave it)
17:49:05.148  peer  travel: left the area 770_SZ_Swamp (the partner left it)
17:49:05      both  host='-' peer='-'   (the same area on both machines)
```
and each entry reported `rig: level engine state <area> LoadedLevel=1 actuallyVisible=1` on
BOTH machines.  **R150a is CLOSED.**

One harness defect surfaced here and is worth recording: `qd.level` compared the file's whole
content against the value last applied, so writing `-` a second time in one session was
ignored -- the smoke phase left the area and the soak's own leave then did nothing
(`FAIL [1] the HOST left its area`, with the pair still in 770).  The lever now takes a
trailing `#nonce`, like `qd.hard` and `qd.tp`.

### R150b. NO NAME THE RIG TRIED WAS A WORLD

Session 21 concluded "no hard travel is reachable through `qd.hard`".  The reason is a
property of the content: this game is ONE persistent world (`Ruins_Level_00`) plus streamed
area sublevels, and the only separate `UWorld`s are the intro, the cutscenes and the title
screen -- and this machine's level table lists all of them as streaming levels, so
`FollowHardTravel` streamed every candidate and reported `streamed=1`.  So:

  * `qd.hard auto` now walks the real-world list with the STREAMING PATH DISABLED
    (`FollowHardTravel(..., allowStream=false)`) and keeps going until one really LOADS;
  * the FOLLOWER of a `HARD_BEGIN` does the same.  It did not, and that was the second
    half: the peer's table claimed `997_FinalZoneCutscene` was a sublevel, so it "entered it
    by streaming" (nothing changed), sent ARRIVED, and the host -- believing the pair was
    together -- released them into two different worlds:
    `[2] travel: 997_FinalZoneCutscene is a streamed sublevel -- entered by streaming it`
    then `host=997_FinalZoneCutscene peer=Ruins_Level_00`.

**Measured, out and back, three times (smoke in two runs, and once inside a soak):**
```
rig: hard lever -> auto picked 997_FinalZoneCutscene (a real UWorld: LoadMapWithLoadingScreen)
travel: HARD transition to 997_FinalZoneCutscene -- announcing it so the partner loads the same world
peer: travel: hard transition: loading 997_FinalZoneCutscene through GameInstance.LoadMapWithLoadingScreen
travel: both machines arrived in 997_FinalZoneCutscene; host releasing the pair to resume
peer: host=997_FinalZoneCutscene peer=997_FinalZoneCutscene
```
and the same for the return load to `Ruins_Level_00`.  **R150b is CLOSED.**

A hard travel costs a `WORLD DIVERGENCE` line while the two machines are mid-transition and
one more when they agree again.  That is the transition, not a desync: `agree=1` and the
same `UWorld` on both machines at every quiescent sample.

### The soak, measured - and where it fell short

Two full sessions were run.  Both are reported, including the one that failed.

| run | wall / soak body | both alive | kill diff | shared HP | world / area | area changes | hard travel | faults / ERR |
|---|---|---|---|---|---|---|---|---|
| `s22-all` #1 (soak phase) | 2049 s / 1581 s | **yes** | **0** (60 / 60) | 68 compared, **4 unequal** (worst 600 HP) | `Ruins_Level_00` / `770_SZ_Swamp`, both | **2 of 2** | 1 out + 1 back | 0 / 0 |
| `s22-all` #2 (soak phase) | 1941 s / 1496 s | **NO - the peer crashed** | not measured | 101 compared, 3 unequal (all `530F7FD0`, 140 vs 30) | host `Ruins_Level_00`, peer dead in `997_FinalZoneCutscene` | **2 of 2** | 1 out + 1 back | 0 / 0 |

Segment detail, run #2 (the longer of the two on the final build):

| segment | ran | iterations | both alive |
|---|---|---|---|
| hub | 614 s of 612 s | 164 | yes |
| area780 (`780_SZ_Depths`) | 306 s of 306 s | 79 | yes |
| area770 (`770_SZ_Swamp`) | 326 s of 306 s | 78 | yes |
| hub-again (after the leave) | 250 s of 234 s | 57 | yes |
| hard out (auto -> `997_FinalZoneCutscene`) | - | - | **yes**, both machines in it |
| hard back (`Ruins_Level_00`) | - | - | **the peer crashed during the load** |
| hub-final | **0 s of 342 s** | 0 | **no** |

**What passed in run #2.**  1496 s of soak body in one pair of processes with hub fighting,
two area changes that followed on both machines, a leave that followed, and one real hard
travel out with the partner landing in the same world.  Zero `ACCESS_VIOLATION`/`EXCEPTION`/
`fatal` lines and zero `ERR` lines in 117 k host lines and 81 k peer lines.  Shared HP was
MEASURED, not assumed: 101 pairs compared across 17 samples, 3 unequal -- all three the SAME
key `530F7FD0` at `host=140 peer=30`, a standing 110 HP difference, not a sampling artefact
(it persists across three segments).  That key is the open HP item, not a transient.

**What failed in run #2, precisely.**  The peer died during the hard return to
`Ruins_Level_00`, 62 minutes into the process.  The WER dump
(`inst2\Saved_qd2\Crashes\UE4CC-Windows-C139E2...`) is an `EXCEPTION_ACCESS_VIOLATION` reading
`0xffffffffffffffff` at `QueensDomain_Win64-Shipping!+0xa87870`, with an instruction that is a
reference-count decrement on a garbage pointer (`sub dword ptr [rax+10h],1`, `rax` = freed
memory).  The mod appears in that stack only as `winmm!HK_ProcessEvent+0x2ad` -- the forward
to the engine's own `ProcessEvent`, i.e. the faulting call is the GAME's, made during the
world teardown.  This is the R140/R138 family: a dangling `UObject*` inside the engine's
destruction list.  It is the fourth hard world load in that process lifetime (two in the
smoke phase at 17:25, then out at 17:54 and back at 17:55).  **Not fixed, not explained, and
not claimed to be.**

**What failed in run #1.**  The leave segment was skipped by the `qd.level` nonce defect
above, so the measured body was 1581 s and not 1800 s.  Its kill diff was 0 with the
end-of-run dump completing for the first time (60 dead keys on each machine, none one-sided),
its HP comparison was 68 pairs / 4 unequal, and both processes were alive at the end with
zero fault and zero `ERR` lines.

So: **the 1800 s body was not reached in either session.**  Run #1 came closest (2049 s of
wall clock, 1581 s of measured body) and is the only run with both processes alive and a
measured kill diff of zero.  Saying "a 30-minute soak passed" would be false.

### The end-of-run scan, and why it did not finish before

Session 21 ended with "the kill-set diff was not measured" because the per-key dump and its
scan did not finish over a 92 k-line log.  Two separate quadratic paths, not one:

  * `Get-RunLines` re-read and re-split the whole file on every call.  It is now cached
    against the file LENGTH, so a growing log invalidates it and an unchanged one is read
    once -- exact, not a timeout.
  * `Get-Since` appended with `$out += $ln`, which is O(n) per append.  With only the cache
    in place the scan still sat with no output for fifteen minutes; it now uses a List.

After both, the end-of-run dump completed in ~15 s per machine and the diff was a number:
`host-dead=60 peer-dead=60 diff=0`.

### The other harness defect this session found

`Enter-Area` counted `travel: area <x> is already pinned on this machine` as a PASS -- the
exact line that was the R150a refusal.  Only `travel: entered the partner's area <x>` counts
now.

### Live proof, phase by phase

| Claim | Verdict | Key line |
|---|---|---|
| The binary loads and verifies | **PASS** | `targets: 36 byte-verified ... 0 mismatched` |
| The lever answers in under 20 s on both machines | **PASS** | `dump 1: answered after 0.7s`, worst 17.9 s (first dump of a boot) |
| A drop exists on BOTH machines under the SAME key | **PASS** | `key 46FB326A#0: BOTH machines hold it`, `loot: constructed the partner's drop 54F6FD50#0` |
| Taking a drop hides the partner's copy; refused twice | **NOT-PROVEN** | the rig cannot make the game consume a drop (four mechanisms, all measured failing) |
| A second TP_AREA entry follows | **PASS** | `travel: entered the partner's area 770_SZ_Swamp by streaming it` on both |
| Leaving the area follows | **PASS** | `travel: left the area 770_SZ_Swamp (the partner left it)` 46 ms later |
| A real hard travel is performed and followed | **PASS** | `auto picked 997_FinalZoneCutscene`, peer `loading 997_FinalZoneCutscene`, both report it |
| Both alive at the end of a 1800 s run | **FAIL** | run #1 alive at 2049 s; run #2 the peer crashed on the second hard load |
| Kill sets diff 0 | **PASS in run #1** | `host-dead=60 peer-dead=60 diff=0` |
| Shared HP equal | **PARTIAL** | 101 compared / 3 unequal (run #2), 68 / 4 (run #1) |
| Zero uncaught faults, zero ERR | **PASS in both** | `faults=0 err=0` in 117 k + 81 k lines |
| The 1800 s soak body | **NOT MET** | 1581 s (run #1) and 1496 s (run #2) |

### What is still open

- **R129's take half.**  The identity and the mirror are fixed; the game has not been made to
  consume a drop by the rig, so `loot: local pickup` / `loot: hiding local drop` are still
  unproven.  The next probe is named above (`ItemPickup_Base`'s SuperStruct surface).
- **A crash on a repeated hard world load.**  The peer died on the second hard return to
  `Ruins_Level_00`, in the game's own object destruction, with the mod only forwarding
  `ProcessEvent`.  This is the R140/R138 family and it is now the top stability item.
- **One standing HP disagreement** (`530F7FD0`, 140 against 30).  Three samples across three
  segments, so it is a real divergence for that key, not a sample taken mid-convergence.
- **The soak body** was short in both sessions (1581 s and 1496 s of 1800 s) for different
  reasons: the `qd.level` nonce in run #1, the peer crash in run #2.
- **The death mark depends on the mod seeing the death.**  A drop whose death neither the
  save hook nor the death sweep nor the mod's own damage path observed still falls back to
  the position-derived key, and then the two machines disagree again -- measured in run #2,
  where every drop of the session took that path.  The marks were widened afterwards (the
  sweep and both applied-death sites) but that build has not been through a loot run.

### What a human tester needs to know

Two things you could not do last time now work: you can enter a second (and third) area in
one session and your partner follows, including back out to the hub; and a hard map
transition now lands you both in the same world.  Loot is improved and still not right: a
drop that one of you caused should appear on both screens under the same name, but whether
it does depends on both machines noticing the death, and picking one up has never been
observed by the rig.  If the game vanishes during a map transition late in a long session,
that is the crash this session measured and did not fix -- keep the crash folder.

---

## Session 23 - 2026-11-10 (the crash that repeats, the key that had two bodies, and the move the game can see)

This branch is `t3/session-23-crash-soak-r129`, reset to `sync-fix`'s Session 22 merge
(`9845c5f`).  The body is `tools/session23.ps1`, one phase per invocation: `hardloop` (N
consecutive hard world loads), `loot` (R129's take half on its own), `smoke`, `soak` (>= 1800 s
of measured body).  Configuration is the documented PLAY set with `LogLevel=3`; roles are read
from `session established`: **host=inst1, peer=inst2**.

### R152. THE CRASH ON A REPEATED HARD WORLD LOAD - REPRODUCED, NAMED, FIXED

Session 22 lost the peer on the second hard return to `Ruins_Level_00`, the fourth world load
in that process.  It could only afford four loads inside a 30-minute soak, so this session's
first piece of work was a phase that does nothing but loads.

**It reproduces.**  Six consecutive hard loads on the Session 22 build, 18:24:07 -> 18:31:54,
one pair of processes:

| load | world | both alive |
|---|---|---|
| 1 | `997_FinalZoneCutscene` | yes |
| 2 | `Ruins_Level_00` | yes |
| 3 | `997_FinalZoneCutscene` | yes |
| 4 | `Ruins_Level_00` | yes |
| 5 | `997_FinalZoneCutscene` | yes |
| 6 | `Ruins_Level_00` | **NO - the peer died at 18:31:51, 7 min 51 s into the process** |

```
crash error: Unhandled Exception: EXCEPTION_ACCESS_VIOLATION reading address 0xffffffffffffffff
```

So it is not "the fourth load" and it is not "62 minutes": it is **a load after enough of
them**, and the new dump is 7:51 into a process.  That is what a loop is for.

**The instruction.**  Both dumps carry the same one, resolved with the game's base
(`0x7ff7d4a30000`) and the build's own `winmm.pdb`:

```
QueensDomain_Win64_Shipping!png_get_uint_16+0xa87870   (RVA 0x102e790)
  83681001   sub dword ptr [rax+10h],1
```

The enclosing function is `RVA 0x102e650` (792 bytes).  It is the destructor of one entry in a
hash container: the caller passes a **16-byte key** `{object*, packed id}`, the container hangs
off the object at `+0x500` with its element array at `[container+0x48]`, and the probe uses
`3 * index` against two primes at `+0x60`/`+0x64`, i.e. **24-byte elements**.  The address the
lookup produced has its `+0x10` dword decremented - a reference count on an entry that is no
longer there:

| dump | `rax` (the faulting base) | what it is |
|---|---|---|
| Session 22 `...C139E2...`, uptime 1:02:26 | `0x00bc3a170969b9dd` | freed heap |
| Session 23 `...F1FD6DD8...`, uptime 0:07:51 | `0x4100000000000032` | **printable text - `'2'` and `'A'`** |

The second value is the decisive one: the container's **backing store had been freed and reused
by a string** before this destructor walked it.  In both dumps the keyed object is likewise
dead memory while the container's owner (`rsi`) is a live object of the same class - vtable RVA
`0x3993918`, identical in both dumps:

```
Session 23   [rbx] -> 0x1f29bfa4d00 (dead)    rsi = 0x1f29bfa4d80  vtable 0x7ff7d83c3918
Session 22   [rbx] -> 0x17edc9af900 (dead)    rsi = 0x17edc9af9a0  vtable 0x7ff7d83c3918
```

**Which object or class, honestly.**  The game ships without a PDB and without RTTI, and this
object is **not a UObject**: `[obj+0x10]` is `0x0009fb49ffffffff`, not a `UClass*`, and the
object-table index at `[obj+0x0C]` is 0.  It is an engine-internal polymorphic C++ object with
a hash container at `+0x500`.  Naming it needs symbols that do not exist on disk.  What *is*
proven is the shape: **the engine's own destruction pass walked a container whose store had
already been freed** - a stale handle or a double destruction inside the engine, reached during
a hard world load.

**The full stack** (new dump; frames are the return addresses, mapped through `.pdata`, with
names where the module's PDB has one):

```
0    QueensDomain  (faulting instruction)                        RVA 0x102e790
1    QueensDomain  FUN_0102a230+0x327
2    QueensDomain  FUN_01010ea0+0x193
3    QueensDomain  FUN_01028d70+0x455
4    QueensDomain  FUN_010336c0+0x169
5    QueensDomain  FUN_010328ed+0x188
6    QueensDomain  FUN_010218d4+0xc3
7    QueensDomain  FUN_02d3422d+0x31
8    QueensDomain  FUN_00e3bbf6+0x16
9    QueensDomain  FUN_00e2eeb8+0x61
10   QueensDomain  FUN_00e5c100+0x72
11   QueensDomain  FUN_011b6aa9+0x18f
12   QueensDomain  FUN_011a7f50+0x5f
13   QueensDomain  FUN_01195800+0x306
14   QueensDomain  FUN_011b6ec0+0x4b
15   QueensDomain  FUN_011a7f50+0x5f
16   QueensDomain  FUN_011a7e40+0x6e
17   QueensDomain  FUN_010428e0+0x79
18   QueensDomain  UObject::ProcessEvent+0x2e2
19   winmm         anonymous namespace::HK_ProcessEvent+0x2ad      <- the ProcessEvent forward
20   QueensDomain  FUN_0265f3cd+0x4e
21   QueensDomain  FUN_02649b40+0x1e8
22   QueensDomain  FUN_0264d99a+0x36
23   QueensDomain  FUN_02d4ece4+0x3a
24   QueensDomain  FUN_028e25d0+0x21
25   QueensDomain  FUN_00e3b419+0x8
26   QueensDomain  FUN_02d2c220+0x1e
27   QueensDomain  FUN_02ce85b0+0x5ee
28   QueensDomain  FUN_02cd2690+0x958
29   QueensDomain  FUN_02cf1537+0x240
30   QueensDomain  FUN_028c4ea0+0x1b4
31   QueensDomain  FEngineLoop::Tick+0x7e0
32   winmm         anonymous namespace::HK_Tick+0x32            <- the tick detour's trampoline
33   QueensDomain  FUN_0077c130+0x36c
...
```

Read from the bottom: the engine loop calls the module's `FEngineLoop::Tick` detour, the detour
calls the MinHook trampoline, and the trampoline resumes **inside the engine's own tick** at
`+0x7e0`; everything above that is the engine.  The module's other frame,
`HK_ProcessEvent+0x2ad`, is the forward to `UObject::ProcessEvent` - the game asked for a
UFunction and the module passed it through.  **Neither frame is the module's own work**, which
is why Session 22 recorded "the mod present only as the ProcessEvent forward".  Session 22's
stack is the same stack, with `HK_ProcessEvent+0x2ad` in the same place.

**What the module was holding across that teardown.**  The dump cannot name the module's
contributions, so they were enumerated from the source.  All of these are world-scoped and were
still live when the world went: the enemy record table (`g_en[]`: actor, spawner,
`bossBarActor`) and the mirrors in `g_mirrorActor[]`; the partner's body (`g_puppetActor`) and
`avatar::Body g_body`'s raw component pointers, which were cleared only when `g_puppetActor` was
non-null; the loot actor table and the drop copies the module spawns;
`void* volatile g_fxRing[64]` - the last array of raw actor pointers in the module, re-read
through `ObjectIsLive`; `g_proxyCls`/`g_proxyClsTried`, which latch a `UClass*` forever; ~50
cached `UFunction*` and the per-class property caches; and the level table, which is reset
**only** by `StreamPump` - which returns early unless `StreamUnion=1`.

There was no central world generation and no delegate or timer registration: world-change
detection was eleven independent `static void* lastWorld` snapshots, one per lane, each gated on
that lane's switch.

**The fix - one place, called twice.**

`WorldTeardownReset(now, why)` hides the actors the module owns (hide, never destroy: R140),
drops every table above, resets the level table unconditionally, and clears the raw caches.  It
runs:

1. **immediately before the module asks for a hard load**, inside `FollowHardTravel`, because
   the world is destroyed *inside* `LoadMapWithLoadingScreen` and the module's tick runs again
   before that call returns;
2. **when the world pointer changes**, in the tick's existing world-change block, because a load
   can also be started by the game itself and the pointer is the only signal.

Between the request and the observed change, `HK_Tick` **returns immediately** (the transition
gate) and `HK_StoreEnemySaveState` keeps the engine's write but skips its own bookkeeping.  The
module therefore never reads an actor from the world being torn down.  The gate is bounded at
20 s (a refused load must not leave the module silent) and is cleared at once if
`LoadMapWithLoadingScreen` itself faults.

**Measured, same harness, same play set, same machine:**

| build | loads attempted | outcome |
|---|---|---|
| Session 22 build (unfixed) | 6 | **peer dead on load 6**, 31 PASS / 6 FAIL |
| this build (fixed) | 8 | **both alive after every one**, 47 PASS / 0 FAIL, zero FAULT lines |

Timeline of the fixed run: 18:37:18 first load, 18:47:35 last, one pair of processes, eight
world loads (four round trips), and every load landed both machines in the same world
(`host=Ruins_Level_00 peer=Ruins_Level_00` / `997_FinalZoneCutscene` on both).

What this does **not** prove: that the engine-side double destruction is gone.  The module no
longer contributes a stale handle to it and no longer reads the dying world, and eight loads
survive where six killed the unfixed build.  A crash inside the game's own container could
still have another cause; that is stated as open at the end.

### R151. THE STANDING HP DISAGREEMENT WAS ONE KEY WITH TWO BODIES

Session 22 recorded `530F7FD0: host=140 peer=30`, three samples across three segments, and
called it a standing divergence.  The module's own dump from that run, on the host:

```
17:44:46.999 enemykey 530F7FD0 ALIVE local=0 src=0 hp=30.0  cls=BookBug_Magic_BP_C
17:46:02.369 enemykey 530F7FD0 ALIVE local=0 src=0 hp=140.0 mirrored=1 cls=BookBug_Magic_BP_C
17:47:18.439 enemykey 530F7FD0 ALIVE local=1 src=0 hp=140.0 mirrored=1 cls=BookBug_Magic_BP_C
17:47:18.481 enemykey 530F7FD0 ALIVE local=1 src=0 hp=30.0  cls=BookBug_Magic_BP_C
```

**Two records answer for one key**: a copy the module constructed (`mirrored=1`, at full HP 140)
and the real enemy (30).  Both `src=0` - a **spawner** GUID, the exact identity the two
machines agree on - and both the same class.  The key was never diverging between the machines;
one machine had two bodies for it and the harness kept whichever line came last.

**Why the HP lane could not converge it.**  Convergence targets a **fraction**
(`ConvergeDamage(hp, mx, hostFrac, ...)`) and the boss bar carries a fraction (`HpFrac`,
0..65535).  Two bodies at the same fraction have nothing to move, so the *absolute* difference
is permanent by construction.  The per-key dump compared absolute HP and never printed
`MaxHP` - the one number that would have made the cause visible.

**How the copy got there.**  A mirror is constructed when the partner describes an enemy this
machine does **not** hold yet.  `OnEnemySpawn` tested `EnemyByGuid(...)->actor` - a *first
match* - so a key whose first record was a swept corpse looked unheld and a second body was
built beside a live enemy the scan had simply not met yet.

**The fix, three small rules.**

* `DropMirrorForKey`: when the scan meets a real enemy whose key a constructed copy already
  holds, the copy is hidden and its record removed.  The real enemy wins - it is the body the
  game simulates and the one the loot key derives from.
* `HoldsOwnLiveBody`: `OnEnemySpawn` asks about **every** record, not the first, and refuses to
  construct a copy for a key this machine already runs.
* The dump prints `mx=` (the body's own MaxHP), `peers=` (how many other records answer for the
  key) and `id=` (the whole 128-bit key), and `enemy::HpComparable` - pinned in
  `test_enemyid.cpp` - makes a key with a second body on either side **not comparable**, so the
  harness reports `duplicates=` instead of a false mismatch.

Measured in the `loot` phase (780_SZ_Depths, both machines): **`peers=0` on every ALIVE line,
22 pairs compared, 0 unequal**.  No duplicate body existed to compare.

### R154. THE KEY IN THE DUMP WAS FOUR BYTES, AND TWO KEYS SHARED THEM

The first `loot` run of the session failed its HP check on a key that was not `530F7FD0`:

```
HP MISMATCH 55E0EA46: host=250 peer=440
enemykey 55E0EA46 ALIVE local=1 src=0 hp=250.0 mx=250.0 peers=0 mirrored=1 cls=Boner_Black_Berserker_BP_C   (host)
enemykey 55E0EA46 ALIVE local=1 src=0 hp=440.0 mx=440.0 peers=0 mirrored=1 cls=Boner_Black_Berserker_BP_C   (peer)
```

Same class, same `src`, **no duplicate on either machine**, and each at exactly its own MaxHP.
`enemykey` printed `guid[0]` - 4 of the key's 16 bytes - and the harness keyed on that, so two
different 128-bit keys looked like one enemy.  The dump now prints `id=` (the whole key) and the
harness compares on it; a pair whose `mx` differs is counted as `mx-differ` (a *different body
under one key*, which is not a shared quantity) instead of `unequal`.

**Still open, and named as such:** the module's identity can hand one spawner GUID to more than
one live enemy (`SpawnerManagesActor` falls back to `true` when `CurrentManagedEnemy` is
unreadable), so two bodies under one key remain possible across machines.  What is fixed is that
such a pair is now **visible** (`peers=`, `mx=`, `id=`) and is no longer reported as an HP
desync.

### R153. R129's TAKE HALF - THE ENGINE GENERATES AN OVERLAP FROM A MOVE, NOT A TELEPORT

Session 22 tried four ways to make the game consume a drop and all four failed: a teleport into
the volume, three out-and-back teleports, a fall from 3 m, and the class' own `Pickup`/`Interact`
verbs.  Every one of them was a **position write or a reflective call**.

`SetActorXform` - the function every rig movement uses - asks for
`K2_SetActorLocationAndRotation(..., bSweep=false, bTeleport=true)`.  A teleport does not move a
component through space, so the engine never dispatches `BeginOverlap`, and a pickup *is* an
overlap.  Two things were added:

* `SetActorXformSweep` (bSweep=true, bTeleport=false) and `SweepPawnTo`, which walks the pawn in
  <=100 cm steps so every centimetre of the path is a move the engine can see.  The spec suffix
  `:on` asks for it; a non-zero offset is still a teleport, because "stand 4 m away" does not
  have to be seen.
* `ReceiveActorBeginOverlap` as the **first** drop verb.  It is declared by `AActor` (the new
  `qd.efunc` SuperStruct walk prints `Actor has 132 UFUNCTIONs`), and a Blueprint child's pickup
  body is its *override* of that event - an override creates no UFunction, which is exactly why
  the child reported `0 UFUNCTIONs` in Session 22 and why calling `Pickup` on it ran nothing.

**Measured, and still not enough.**  Three drops rolled in `780_SZ_Depths` (`54F6FD50#0`,
`9D047AD7#0`, `D1DB5E8F#0`), the precondition held (`key D1DB5E8F#0: BOTH machines hold it`),
and the verb ran - `dev interact: drop verbs tried; ran=ReceiveActorBeginOverlap` - and the drop
was still standing afterwards, in both directions.  The game's own gate said
`IsInteractible=1`, the player's `InpActEvt_Interact` event ran, the `Current Interact Actor`
field was set and read back, and nothing consumed the drop.

One harness defect was found and fixed on the way, and it is worth recording because it hid the
sweep entirely: **`qd.near` treats a non-positive value as "not set" and substitutes 140**, so
every on-drop leg the harness asked for stood 140 cm away.  The host log said so nine times:

```
dev teleport drop 'drop:D1DB5E8F:0' -> 3Coins_Item_Bp_C at ... standing offset=140 height=0 move=teleport
```

`:on` forces offset 0, and the teleport line now prints `move=sweep|teleport` so the next run can
see which one ran.  `R129's take half remains NOT-PROVEN` (state at the time of writing; the
soak's own take attempt is recorded below).

### R155. A SECOND CRASH, DIFFERENT FAULT, FOUND BY THE SOAK

The 1800 s soak (below) ended with the peer dead at 45 min 26 s, and the dump is **not** R152's:

```
ExceptionAddress: QueensDomain_Win64_Shipping!png_get_uint_16+0x89b1b5  (RVA 0xE420D5)
ExceptionType:    c0000005 (EXCEPTION_ACCESS_VIOLATION)
READ_ADDRESS:     0000000000000130      FAILURE_BUCKET_ID: NULL_CLASS_PTR_READ
rax = 0
```

A null `this` in a UFunction dispatch - `[rax + 0x130]` on a null object - where R152 was a
reference-count decrement on a *freed* one.  The module is in the stack twice, both times as
`HK_ProcessEvent+0x2ad` (the pass-through forward), called from game code, so this is the
engine's own BP dispatch again.

It happened in `hub-final`, ~9 minutes after the second hard round trip, in ordinary play.  Two
readings are consistent with the evidence and neither is proven: the four world loads leave the
engine in a state that faults later (the same family as R152, a different symptom), or this is
the long-run fault Sessions 18-22 kept meeting (R138/R140) and it is unrelated to loads at all.
Recorded, not explained, and not claimed fixed.  The next probe is the null object itself: the
two `HK_ProcessEvent` frames are enough to identify which BP call was dispatched with a null
target if the module logs its own forwards, which it does not do today.
### The soak, measured - and what the counters actually counted

One pair of processes, the documented PLAY set, `soak` phase, 19:13:23 -> 19:59:44 (one pair of processes, 2695 s of wall clock).
Segments are hub / loot / 770 / leave / hub-again / two hard round trips / hub-final, with the
per-key dump sampled through the hub segments.

| segment | ran | iterations | both alive |
|---|---|---|---|
| hub | 577 s of 576 s | 165 | yes |
| loot step (`780_SZ_Depths`, entered by the step itself) | ~13 min | - | yes |
| area770 (`770_SZ_Swamp`, TP_AREA) | 291 s of 288 s | 80 | yes |
| leave (back to the hub) | - | - | yes |
| hub-again | 255 s of 252 s | 68 | yes |
| hard round trip 1 (`auto` -> `997_FinalZoneCutscene` -> `Ruins_Level_00`) | - | - | yes |
| hard round trip 2 (the same again) | - | - | yes |
| hub-final | **482 s of 684 s** | 114 | **NO - the peer died at 19:59:03** |

**MEASURED BODY: 1605 s of 1800 s.**  The shortfall is the last segment, cut by the peer's
death; the four other segments ran their full budget.  Wall clock 2695 s.

**What passed.**

* Two area changes followed on both machines (`780_SZ_Depths` and `770_SZ_Swamp`), each with
  `rig: level engine state ... LoadedLevel=1 actuallyVisible=1` on BOTH, and the leave followed
  26 ms after the host.
* **Two hard round trips - four world loads - all landed both machines in the same world**, with
  zero fault lines through them.
* Shared HP was MEASURED, not assumed: **20 samples, 231 shared-alive observations, 67 pairs
  compared, 0 unequal, 0 unreadable, 0 ghosts.**  The other 164 observations were counted as
  `duplicates=`, and that number is **inflated by a defect in the dump that this run found**:
  `peers=` counted a record whose actor is already gone, which is the normal state of a key that
  died and respawned.  The dump now counts only a live, callable body.  The evidence that the
  counter was the problem, not the enemies: `770_SZ_Swamp` and the first `hub-final` samples
  reported `duplicates=0` with `compared=5`, `compared=26` and `compared=9`, all unequal 0, and
  the one `peers=1` line in the final dump (`BF617311`, hp 250 of 250) had a second record the
  dump itself classified as a ghost - it printed no line for it.
* Both machines ended in `Ruins_Level_00` with `agree=1` and the same area (`-`).
* Zero `ERR` lines.

**What failed, and it is not the same crash.**

The peer died at **19:59:03, 45 min 26 s into the process**, during `hub-final` - after the hard
round trips, in ordinary play.  The dump is a **different fault**:

```
ExceptionAddress: QueensDomain_Win64_Shipping!png_get_uint_16+0x89b1b5  (RVA 0xE420D5)
READ_ADDRESS: 0000000000000130
FAILURE_BUCKET_ID: NULL_CLASS_PTR_READ
rax = 0
```

A null `this` in a UFunction dispatch, not the reference-count decrement of R152.  The module is
in that stack twice, both times as `HK_ProcessEvent+0x2ad`, the pass-through forward, called from
game code.  **R152's fix holds** - eight consecutive loads survive the loop, and four loads
inside this soak cost nothing - but the engine is left in a state that faults later, and that is
a new, open item (**R155**), not a regression of the closed one.  The kill-set diff could not be
measured because one process is gone.

### The live-proof table

| Claim | Verdict | Key line |
|---|---|---|
| The crash reproduces on the unfixed build | **PASS (it is the defect)** | peer dead on load 6 of 6, 7:51 uptime, same RVA and instruction as Session 22 |
| The fix survives repeated hard loads | **PASS** | 8 consecutive loads, both alive after every one, 47 PASS / 0 FAIL |
| One key, one live body | **PASS** | `peers=0` on every ALIVE line in `780_SZ_Depths`; 22 pairs compared, 0 unequal |
| The dump names a duplicate instead of reporting a desync | **PASS** | `peers=` / `mx=` / `id=` on the line; `mx-differ` counted separately |
| A key's HP is comparable across machines | **PASS where keys are unambiguous** | 22 compared / 0 unequal (loot run); see the soak table for the hub |
| A drop exists on both machines under the same key | **PASS** | `key D1DB5E8F#0: BOTH machines hold it` |
| Taking a drop hides the partner's copy; refused twice | **NOT-PROVEN** | the verb runs (`ran=ReceiveActorBeginOverlap`) and consumes nothing; the drop is still standing |
| Two area changes follow | **PASS** - 2 of 2 | `travel: entered the partner's area 770_SZ_Swamp by streaming it` |
| Two hard round trips inside the soak | **PASS** - 2 of 2 (4 loads) | `travel: both machines arrived in ...` |
| Both alive at the end of >= 1800 s of body | **FAIL** - the peer died at 45:26 | 1605 s of 1800 s |
| Kill sets diff 0 | **NOT MEASURED** | | a process died; its dump cannot answer |
| Shared HP measured, not assumed | **PASS** - 67 compared, 0 unequal, 0 unreadable |  20 samples, 231 shared-alive observations (164 counted as `duplicates=` by the pre-fix counter) |
| Zero uncaught faults, zero ERR | 0 FAULT lines and 0 `ERR` lines on both machines; the peer died by an uncaught null-class read at RVA 0xE420D5 (R155) | |

### What a human tester needs to know

Two things are better and one is unchanged.  **A long session with map transitions is safer**:
the mod used to keep pointers into the world a hard load destroys, and that is what killed the
game on the sixth load; it now drops them and goes quiet during the load.  **One screen showing
an enemy at 140 and the other at 30 was never a sync failure** - the machine with 140 was
holding a second body for the same enemy; that is fixed, and a dump now says `peers=` and `mx=`
so the question can be answered from the log.  **Picking up a drop is still not proven**: if you
walk over a drop and it does not disappear, or it disappears on one screen only, that is the
open item.

### What is still open

- **R129's take half.**  Six mechanisms have now been measured failing (teleport in, three
  out-and-back teleports, a fall from 3 m, the class' `Pickup`/`Interact` verbs, a real swept
  move through the volume, and a direct `ReceiveActorBeginOverlap`).  The next probe is the
  content: whether the drop class' pickup body hangs off `ReceiveActorBeginOverlap` at all, and
  real input-driven movement rather than any position write.
- **The engine-side crash (R152) is contained, not explained.**  The module no longer contributes
  to it and eight consecutive loads survive where six killed the unfixed build, but the engine's
  own double destruction of a hash container is not understood.
- **R155 - a null-class UFunction dispatch 45 minutes in.**  Found by this session's soak, a
  different fault from R152, in the same engine-dispatch family.  Not fixed, not explained, and
  the soak below therefore reports a dead peer at 1605 s of body.
- **One spawner GUID can name more than one live enemy** across machines
  (`SpawnerManagesActor` falls back to `true` when `CurrentManagedEnemy` is unreadable).  The
  pair is now visible (`mx=`, `id=`, `mx-differ`) instead of being reported as an HP desync, but
  the identity is still not unique.
- **`peers=` counted dead records in the build that ran the first soak** (a record whose actor
  is gone is the normal case for a key that died and respawned).  The dump now counts only a
  live, callable body; the soak numbers above are from the build BEFORE that correction, which
  is why the hub's `duplicates=` count is high and its `compared=` count is low.  Stated here
  rather than quietly fixed in the table.
- **The `enemykey` token in the log is 8 hex**; `id=` carries all 128 bits now, and the
  harness compares on it, but the DEAD lines still print only the token (the kill-set diff is
  therefore still keyed on 4 bytes).
- **Boss phases, hit reactions, the shop door, peer vitals** - unchanged from Sessions 20-22.
## Session 24 - 2026-11-10 (the null dispatch named, the drop the pickup answers itself)

This branch is `t3/fix-r155-crash-loot-take`, reset to `sync-fix`'s Session 23 merge
(`e06a6fd`).  Body: `tools/session24.ps1`, one phase per invocation (`hardloop`, `loot`,
`smoke`, `soak`).  Configuration is the documented PLAY set with `LogLevel=3`; roles are read
from `session established`: **host=inst1, peer=inst2**.

### R155. THE NULL DISPATCH, NAMED - a reused slot, a garbage transform, a pawn outside the world

Session 23 recorded this as a `NULL_CLASS_PTR_READ` at game RVA `0xE420D5` and called it
unexplained.  It is now named, and the mod's half of it is fixed and measured.

**The full stack, from the kept dump** (`_test/s23-soak/inst2/.../UE4Minidump.dmp`, process up
45 min 26 s, opened with cdb and the module's own `.pdb`; the game's frames resolved from
`.pdata` by `analysis/re/qdlib.py`, because the game ships without symbols):

```
0    QueensDomain  FUN_00E41B90+0x545        RVA 0xE420D5  mov rax,[rsi+0x130]   <- FAULT, rsi=0
1    QueensDomain  FUN_00E54570+0x154        RVA 0xE546C4
2    QueensDomain  FUN_010428E0+0x79         RVA 0x1042959
3    QueensDomain  FUN_011A7AA0+0x2E2        UObject::ProcessEvent
4    winmm         HK_ProcessEvent+0x2ad     the module's pass-through forward
5    QueensDomain  FUN_0265F3CD+0x4E         game code that called ProcessEvent
6    QueensDomain  FUN_00E51760+0x49
...
31   QueensDomain  FEngineLoop::Tick+0x7e0
32   winmm         HK_Tick+0x32               the tick detour's trampoline
```

**The instruction is a missing null check in the game, on an argument the game supplied.**
`rsi` is the function's **third parameter** (`0xE41BC0  mov rsi, r8` at entry) and it is 0.  The
body reads `[rsi+0x130]` - `AActor::RootComponent` - and then `[root+0x1d0]`, the translation
inside `ComponentToWorld`; it reads the same pair off `this` (`[rbx+0x130]`), takes a rotator
from `this->vtable[0x300]`, normalises a direction, tests an interface on `this->[0x258]`, reads
three state bytes at `this+0x574..0x578` and a vector at `this+0x518`.  So it is a
movement/steering routine that turns a body toward a **target actor**, and it dereferences that
target's root component without ever testing the target.  `rax` was 0 at the fault because the
load never completed.  The call reaches it as a Blueprint function: `FUN_00E54570` is the
generated exec thunk (it decodes its parameters through the engine's property accessors) and it
enters the implementation through vtable slot `0x890` of the actor's class - `0xE41B90` is
exactly what that slot holds in the shipped image.

**Which object had the null at +0x130.**  Not a component of a constructed actor and not a
pointer held across teardown: the **target actor itself** was null, i.e. the Blueprint graph
that ran this function passed a null actor to it.  The module's only frames in that stack are
the ProcessEvent forward and the tick trampoline, so the graph is the game's.  What follows is
the mod state that made the game run that graph against nothing.

**The mod's contribution, from the peer's own log.**  At 19:58:30, 33 s before the fault, one
*live enemy key* answered with two records whose class was **not an actor at all**:

```
19:58:30.450 enemykey AD410C56 ALIVE local=1 src=1 hp=? why=nofn seen=0 inworld=0 oflags=00800048 cls=WidgetTree
19:58:30.641 enemykey AD410C56 ALIVE local=1 src=1 hp=? why=nofn seen=0 inworld=1 oflags=00200000 cls=SaveHelper_C
```

`WidgetTree` and `SaveHelper_C` are UObjects.  The enemy record's object-table **slot had been
reused**, and `weak::Ref` cannot see that: a handle names a SLOT (`{index, serial}`), and
`Capture` reads the slot index off whatever object sits at the address it is handed - so a
capture taken *after* a reuse names the new occupant faithfully.  `ObjectIsLive` said yes to
both.  Then, in the soak's ordinary hover-the-enemy step:

```
19:58:56.648 dev teleport near 'enemy' -> enemy at 0,--9223372036854775808,0 dist=+inf; standing at 0,--9223372036854775808,0 offset=140
19:58:56.667 scan: kind enemy WidgetTree guid=AD410C56 at 0,--9223372036854775808,0 dist=0 local-run=1
```

`-9223372036854775808` is `CatFloat` printing a float near `-2^63`: the rig had read a
"transform" out of a WidgetTree (the `RootComponent` offset applied to a UObject that has none)
and **wrote it into the local player pawn** through `K2_SetActorLocationAndRotation` - the line
carries no `FAILED`.  Six seconds later the game ran a Blueprint graph that steers a body toward
a target actor and the target was null.  The plain description: the mod stood the player
9.2e16 m outside the world, and the game then asked who was there.

**Three fixes, at the source and at the guard.**

* **`EnemyRec::cls`** - a record remembers the class it was created for, and `EnemyActor()`
  refuses *and retires* a record whose slot now holds another class.  A respawn or a mirror is a
  new record with a new capture (`EnemyScanStep`, `RegisterMirror`), never a re-point of an old
  one, so class equality is the one identity test a reused slot cannot fake.  Applied at the
  sweep's purge (the one place that sees every record once per cycle), the `near:enemy`
  resolver, the scan, the guid teleport, the per-key dump and the `peers=` count.  The class
  read is guarded (`ObjClassGuarded`), because a slot can also name memory the engine has
  already freed (R139).
* **`motion::SanePos()`** - a transform read is only a transform if it is a *place*: finite and
  inside `|1e7|` cm.  `ReadActorXform` now returns false for a garbage read, and `SetActorXform`,
  `SetActorXformSweep` and `SweepPawnTo` **refuse to write** one, with a rate-limited `WARN` that
  says what was refused.  Both halves matter: a NaN propagates through the engine's own vector
  math, and a 9e18 poisons navigation and streaming.  `test_sanitize.cpp` pins the predicate on
  the two measured values, the NaN/inf cases, and real places (a 41 km placed NPC, the origin, a
  measured `780_SZ_Depths` drop position).
* **`HK_ProcessEvent` refuses a dispatch on a null object** and names the function it would have
  called.  A forward with no `this` cannot do anything but fault.

**Measured on the fixed build** (two `loot` phases, one pair of processes each, the same
hover/teleport/scan steps that produced the garbage transform in Session 23): both machines ran
the whole phase with **0 FAULT lines, 0 ERR lines and no crash dump**, and `enemy record
retired` / `enemy record dropped` fired **0 times** - the class check did not misfire on healthy
records.  The soak below is the harder test: the same shape, hub play after two hard round trips.

**What this proves and what it does not.**  It proves the mod read a value that is not a
coordinate out of an object that is not an actor and wrote it into the player pawn, and that both
halves are now refused.  It does **not** prove the game's missing null check is unreachable: any
graph that passes a null target will still fault in that instruction.  What changed is that the
module no longer manufactures the state that led to it.

### R129. THE TAKE - the pickup answers its OWN event, and the pawn was never in the volume

Six mechanisms had been measured failing across Sessions 21-23.  Two of the reasons were
found this session, and neither was the one anybody was looking for.

**1. The pickup owns the Interact action.**  Session 23 dumped the drop's class chain and
stopped there; that dump is the answer.  `ItemPickup_Base_C` declares
`InpActEvt_Interact_K2Node_InputActionEvent` **itself**, with `InpActEvt_Attack`/`Jump`/`Pause`,
`PickupItemAnim__AllowInput__EventFunc` and the `DoItemPickupPause`/`EndItemPickupPause` pair.
The pickup takes input while its own flow plays.  The act lever had been pressing the *player's*
compiled event (`FS_PlayerChar_C.InpActEvt_Interact...`), which is a different Blueprint and
consumes nothing - and `Pickup`/`Interact` "ran and changed nothing" for the same reason a name
lookup for a Blueprint *override* always runs nothing.

**2. The rig never stood on the drop.**  The take lever logs where the pawn actually ended up,
and on the first instrumented run it printed the same thing five times:

```
take: pawn now 299 cm from the drop, hidden=0
take: pawn now 300 cm from the drop, hidden=0
```

`SweepPawnTo` from a point the capsule was just *teleported* to returns "no movement" when the
start already penetrates geometry, so **every "step onto the drop" leg this rig has ever run -
including the `:on` sweep added for R153 - stood 3 m away** and then reported that nothing
consumed the drop.  That is the mechanism behind R153's "the sweep ran and consumed nothing".
The lever now teleports *onto* the drop (which is what guarantees presence) and keeps the sweep
for the overlap half; the same line then reads `pawn now 0 cm from the drop`.

**What the new lever does.**  `qd.take <drop:<8hex>:<index>>` runs the attempt in stages, each
one a *place* pass and a *press* pass a poll apart - a pickup's own tick is what opens a
proximity gate, so a press in the same frame as the move is a press against the state from
before the move.  The presses, in order: `Activate`, the pickup's own `InpActEvt_Interact`
(resolved by prefix, because the suffix is compiler-generated), `Interact`,
`ReceiveActorBeginOverlap` with the pawn, `Pickup`.  It never calls a payout
(`GrantItemsAndActivate`, `Grant`, `AddItemToInventory`): a take that skips the game's own
grant is a take the two machines can disagree about.  It stops the moment the drop is gone and
says which stage did it.

**What it measured, and it is not a take.**  Both directions ran on a drop both machines held
(`B759900A#0` on the host, `3D0AF1A3#0` on the peer), the pawn stood **0 cm** from the drop,
all five entries ran, and the drop stayed.  The gate line says why:

```
take: gate before stage requireActivation=0 activated=0 acquired=1 isADrop=0 validGUID=n/a
take: gate after the press  requireActivation=0 activated=0 acquired=1 isADrop=0 validGUID=n/a
```

`Acquired` - the bool `ItemPickup_Base_C` carries next to `PickupLocation`, `IsADrop` and
`DropRestoredFromSave` - is **already 1**, before any press, on both drops.  So every entry the
class declares is a no-op by construction, which is exactly what five sessions of "the verb ran
and consumed nothing" were looking at.  The module never writes that property (`grep Acquired`
finds one reader, `qdobjectstate.h`'s object-lane probe, and that lane only tracks *placed*
actors - a rolled drop is runtime-spawned and is skipped).  `isADrop=0` on the host's copy is
the second half of the same picture: that drop is one the *mod constructed* from the partner's
description, and it does not carry the roll's own flags.

**R129's take half is NOT proven, and this is the exact place the next session starts:** read
`Acquired` on the class default object (if the CDO is also 1 the name means something other
than "already taken"), read `PickedUp`/`PickUpEnded` (a multicast delegate, so the take is an
*event*, not a state), and ask `GetInteractInfo` for its whole out-parameter set rather than the
three fields the lever reads today.  What is now proven is that the rig reaches the drop, that
the entry the class declares is the one being pressed, and that one bool on the pickup is
closed.

### The soak, measured

One pair of processes, the documented PLAY set (`switches: flags=1 inventory=1 actors=1 enemy=1
puppet=1 playerBody=1 dialogue=1 stream=1 travel=1 cutscene=1 movers=1 loot=1 suspend=1 pause=1`),
the `soak` phase, 21:05:15 -> 21:48:00.  Segments are hub / loot step / 770 / leave / hub-again /
two hard round trips / hub-final.

| segment | ran | iterations | both alive |
|---|---|---|---|
| hub | 578 s of 576 s | 168 | yes |
| loot step (`780_SZ_Depths`, entered by the step itself) | ~7 min | 12 kill rounds | yes |
| area770 (`770_SZ_Swamp`, TP_AREA) | 291 s of 288 s | 83 | yes |
| leave (back to the hub) | - | - | yes |
| hub-again | 255 s of 252 s | 71 | yes |
| hard round trip 1 (`auto` -> `997_FinalZoneCutscene` -> `Ruins_Level_00`) | - | - | yes |
| hard round trip 2 (the same again) | - | - | yes |
| hub-final | 692 s of 684 s | 175 | yes |

**MEASURED BODY: 1816 s of 1800 s.**  Wall clock including the transitions: 2567 s.

**What passed.**

* **Both machines alive at the end**, 0 `FAULT` lines and 0 `ERR` lines on either machine, and
  no crash dump written.  That is the R155 measurement: two area changes, four world loads and
  30 minutes of hub play in one pair of processes, in exactly the shape that killed the peer at
  45 min in Session 23.
* **Two hard round trips, four world loads, every one landed both machines in the same world**
  (`997_FinalZoneCutscene` / `Ruins_Level_00` on both), each checked after the load.
* **Two area changes followed on both machines**, each with `rig: level engine state ... 
  LoadedLevel=1 actuallyVisible=1` on BOTH, and the leave followed 18 ms after the host.
* **Kill sets diff to zero**: 219 keys on the host (89 dead), 236 on the peer (89 dead), **0
  one-sided**.
* **Shared HP was measured, not assumed**: 25 samples, **259 shared-alive observations, 259
  compared, 0 unreadable, 1 unequal**.  At the end of the run: 128 shared live keys, **128
  compared, 0 unequal**.
* Both machines ended in `Ruins_Level_00` with `agree=1` and the same area (`-`).

**What failed, with the numbers.**

* **The take** (both directions) - see R129 above.  The soak's own verdict line "a loot take was
  performed and checked on both machines" PASSED, and that pass is weaker than it reads: the
  check matches any passing `r129*` verdict and the one that passed is `r129-roll` (three drops
  rolled).  The two `took the drop` verdicts FAILED.  Reported here rather than left to the
  table.
* **One HP pair was unequal** - `E6EA2F3347860316D380D6A06472C4C5: host=544 peer=143.9`, one
  sample of 259, at t+153 s in the hub.  `mx-differ` was 0 for the run, so the two machines
  agree on that key's **MaxHP**; this is one body's health read 400 apart, in the segment before
  the first area change.  It is not explained by a duplicate record (`duplicates=0`) and it is
  not claimed to be anything but open.  Every other comparison in the run - including the 128 at
  the end - was equal.
* **`WORLD DIVERGENCE` lines=4**, one per hard world load, all transient:

  ```
  21:34:08.748 WARN WORLD DIVERGENCE: local map 997_FinalZoneCutscene, partner map Ruins_Level_00
  21:34:48.538 WARN WORLD DIVERGENCE: local map Ruins_Level_00, partner map 997_FinalZoneCutscene
  21:35:26.328 WARN WORLD DIVERGENCE: local map 997_FinalZoneCutscene, partner map Ruins_Level_00
  21:36:09.426 WARN WORLD DIVERGENCE: local map Ruins_Level_00, partner map 997_FinalZoneCutscene
  ```

  Each is the window between one machine loading and the other arriving, and each resolved:
  the per-load check "both machines report <world>" passed after every load and the end state
  agrees.  The harness check is a strict `count == 0`; Session 23 got 0 because its peer loaded
  before the host's world pointer changed.  Recorded as a harness over-strictness, not a desync -
  the substantive world checks pass.

### The live-proof table

| Claim | Verdict | Key line |
|---|---|---|
| The R155 fault is named at instruction level | **PASS** | `mov rax,[rsi+0x130]`, `rsi` = the function's 3rd parameter, 0 |
| The object with the null `+0x130` is identified | **PASS** | the game's Blueprint graph passed a null target actor; `FUN_00E41B90` reads `Target->RootComponent` unchecked |
| The mod's input to it is identified | **PASS** | `cls=WidgetTree` / `cls=SaveHelper_C` under one live enemy key, then `dev teleport ... standing at 0,-9.22e18,0` |
| A record whose slot was reused can no longer be read as an enemy | **PASS (source)** | `EnemyRec::cls` + `EnemyActor()`, applied at the sweep purge, resolver, scan, guid teleport, dump, `peers=` |
| A transform that is not a place cannot be written | **PASS** | `motion::SanePos` in `ReadActorXform` / `SetActorXform` / `SetActorXformSweep`; `test_sanitize` pins it |
| That guard does not misfire on healthy records | **PASS** | 0 `enemy record retired` lines in a 30-minute soak |
| Both alive after >= 1800 s of body | **PASS** | 1816 s of 1800 s |
| Two area changes follow | **PASS** - 2 of 2 | `travel: entered the partner's area 770_SZ_Swamp` |
| Two hard round trips follow | **PASS** - 2 of 2 (4 loads) | `travel: both machines arrived in ...; host releasing the pair` |
| Kill sets diff 0 | **PASS** | host-dead=89 peer-dead=89 diff=0 |
| Shared HP measured, and equal | **PASS with one exception** | 259 compared, 0 unreadable, 1 unequal; end of run 128 compared, 0 unequal |
| Zero uncaught faults, zero ERR | **PASS** | 0 FAULT and 0 `ERR` lines, no dump, on both machines |
| The rig reaches a drop and presses the entry the class declares | **PASS** | `take: pawn now 0 cm from the drop`, `ran=InpActEvt_Interact_K2Node_InputActionEvent` |
| Taking a drop hides the partner's copy and is refused twice | **NOT-PROVEN** | `acquired=1` before any press; the drop stays standing in both directions |

### What a human tester needs to know

**A long session with map transitions is now safer in the way Session 23 could not prove.**  In
one pair of processes, 30 minutes of measured play, two areas and four world loads, nothing
crashed: the peer that used to die at 45 minutes held on to the end.  **Picking up a drop is
still not proven.**  If you walk over a drop and it does not disappear, that is the open item;
the next session starts from the fact that the drop's own `Acquired` flag is already set before
anything presses it.

### What is still open

- **R129's take half.**  The mechanism is now the right one (the pickup's own event) and the
  rig now stands on the drop, but the pickup's `Acquired` bool is 1 before any press, on both a
  rolled drop and a constructed one.  Next: read it on the class default object, read the
  `PickedUp` delegate, and ask `GetInteractInfo` for every out-parameter.
- **One HP pair of 259 was unequal** (`E6EA2F33...`, host 544 / peer 143.9, hub, t+153 s), with
  MaxHP agreeing.  Not explained.
- **The engine-side crash (R152) is contained, not explained**, unchanged from Session 23.
- **R155's game-side instruction is still unchecked**: any Blueprint graph that passes a null
  actor to `FUN_00E41B90` will still fault.  What is fixed is that the module no longer supplies
  the state that made it happen.
- **Enemy records can still be lost after a hard load**: `hub-final`'s first samples reported
  `shared-alive=0` because the peer's table had few live keys 40 s after the load
  (`enemykey n=28 alive=5` at 21:39:22) while the host had many.  The sweep rebuilt them and the
  last sample compared 90 keys, but a soak that samples immediately after a load still measures
  the rebuild rather than the world.
- **`WORLD DIVERGENCE` counts transient lines during a hard travel**; the harness's `== 0` check
  should be "0 outside a transition".
- **One loot phase ended with both processes gone at the same second** (20:58:16), each log
  stopping mid-line, with **0 FAULT lines, 0 ERR lines and no crash folder on either
  machine** - so neither crashed in a way this harness can see. It did not recur: the soak
  that followed ran 43 minutes in the same shape and both processes were alive at the end.
  Recorded because an exit that leaves no trace is worse than a crash that does; the
  evidence that would settle it (the UE engine log) is not being captured - -AbsLog names
  out/<phase>-inst<N>-ue.log and no such file is ever written.
- **Boss phases, hit reactions, the shop door, peer vitals** - unchanged from Sessions 20-23.

---

## Session 25 - 2026-11-12 (the rig that stayed on screen, the take that paid, and a soak with no cutscene in it)

This branch is `t3/rig-placement-loot-take-soak-1`, reset to `sync-fix` (`b26dec7`). Body:
`tools/session25.ps1`, one phase per invocation (`probe`, `rig`, `hardloop`, `loot`, `smoke`,
`soak`, `all`). Configuration is the documented PLAY set with `LogLevel=3`; roles are read from
`session established`: **host=inst1, peer=inst2**. The branch carries the two coordinator fixes
under test (`3769c21` the rig placement rule and fall guard, `b26dec7` the take) plus three
fixes this session's measurements forced, one in each of those two areas and one in the
harness.

### R156. THE RIG: the user's report, the fix's assumption, and what the log actually shows

The report was "spawns player in darkness, no map visible, or in empty space where he just
falls". `3769c21` named the cause from Session 24's peer log -- the rig teleporting to actors of
a sublevel that is resident but not **shown** (z -14047..-15806) -- and fixed it by requiring
`ULevel::bIsVisible` at every resolver plus a fall guard. Both halves were tested this session
and **both were inert on this content, for two different reasons**. The state the user
described is real: it was produced and caught 42 times in one phase.

The baseline is measured from Session 24's own host log, the file the diagnosis came from:
**265 hub hops, 18 ended more than 30 m below the target, worst 25937 cm**
(`target_z=-14047 pawn_z=-32606`; also `target_z=-108 pawn_z=-15755`).

**(1) The fall guard disarmed itself on the very tick it armed.** The first instrumented phase
printed this once per hop and then nothing at all:

```
9:29:00.126 INFO guard: armed for a placement at z=-9 (safe spot 4106,-1744,140)
9:29:00.126 INFO guard: armed, placement z=-9 pawn z=-9 (-109 ms in)
```

`RigArmFallGuard` read its start time with a fresh `Net::Now()` while the pump is handed the
`now` captured at the top of the same tick, so `now - t0` is a small **negative** number; read
as `uint32` it is > 4e9, i.e. "the six-second window has long passed", and the guard cleared
`armed`. Every `near:` placement was therefore unguarded -- a 19 000 cm displacement produced
no `FELL` line. The comparison is signed now, and the guard works:

```
9:36:04.113 dev teleport near 'chest' -> chest at 1473,327,-1437 ... standing at 1455,466,-1437 level=PersistentLevel shown=1
9:36:05.906 WARN rig: FELL after a rig move (placed at z=-1437, now z=-2966); put back at 1450,509,-1339
```

**(2) The visibility refusal never fired: 0 of 128 hops.** Every accept line now names the
target's own level and its visibility bit, and on both machines, for every kind of target, in
the hub and inside `780_SZ_Depths`, the answer was identical: `level=PersistentLevel shown=1`.
128 hops, 128 of those, **0 refusals**. So the rule cannot see the bad targets on this content:
they are not in a hidden level, they are 140-190 m **below** the persistent level's floor, in
the stacked maps that only have geometry when the sublevel which owns that geometry is shown.
Either the game really places these actors in the persistent level, or `ActorLevelOf` answers
"persistent level" for everything; this session cannot separate those two, and it does not have
to -- the rule as written has no effect here, and claiming otherwise would be false.

**(3) What actually moves the player is the GAME, and it is a teleporter.** The host's log has
it 30 times in one 15-minute phase, always about 1.5 s after a `near:npc` placement:

```
9:24:23.595 INFO dev teleport near 'npc' -> npc at -9,177,-0 ... standing at 40,308,-0
9:24:24.726 INFO travel: local destination TempTeleporter/Ruins_Level_00 seq=13
9:24:28.444 INFO scan: local at 1477,-269,-19067
```

The hub's NPC stands on a `TempTeleporter`. The rig places the player beside it, the game
teleports him 190 m down, and from that moment every later hop resolves the nearest actor *down
there* (the scan of that region lists the same spawner names at z -19150). A 64-hop phase walks
the host from z=140 to z=-19060 that way, one hop at a time: the guard undoes each individual
drop, but it restores to a "safe" spot that is itself deep, so the descent survives hop by hop.

**Fixed: a rig spot inside a teleporter's volume is refused, and named:**

```
dev teleport near 'npc': the spot beside it is inside a teleporter's volume, and the game would move the player out of the level; refused
```

That is the same class of rule as the level refusal: a rig target has to be somewhere the
player can stand **and stay**.

**Measured: 64 hops per machine, one pair of processes, in the hub.** This run used the build
with (1) and (2) fixed; the teleporter refusal (3) was added afterwards and is measured in the
soak below (`relusals` column).

| | HOST (inst1) | PEER (inst2) | both |
|---|---|---|---|
| hops driven | 64 | 64 | **128** |
| hops that landed | 64 | 64 | **128** |
| refused: level not shown | 0 | 0 | **0** |
| refused: no target / no answer | 0 | 0 | **0** |
| `rig: FELL` restores | **23** | **19** | **42** |
| hops LEFT more than 3000 cm below target | **0** | **0** | **0** |
| targets whose level answered `shown=1` | 64 | 64 | **128** |
| game teleporter destinations during the phase | 30 | 2 | **32** |
| pawn z, start -> end | 140 -> **-19060** | 140 -> **-503** | |
| screenshots (mean luminance / stddev) | 4, 39.2 / 25.4 | 4, 42.0 / 26.1 | lit 8 of 8 |

The per-hop z test is the one the user asked for: after each hop the pawn's own
`scan: local at` (10 s cadence) is compared with the target's z. **No hop was left more than
3000 cm below its target**; 37 of the 128 were caught mid-drop by the guard and put back
(`worst dz=19121 cm` is one of those, on a hop whose safe spot was already deep -- the restore
is why it is not counted as "left").

**The scene, in pixels.** `capture-hidden.ps1` (PrintWindow, the off-screen window) was called
at hops 20, 40, 60 and at the end, and every image was measured for mean luminance and
standard deviation. The host's four: 29.1/26.6, 27.5/20.5, 42.7/23.5 and 39.2/25.4 means/stds;
the peer's: 34.9/22.4, 37.8/25.0, 58.7/32.9 and 42.0/26.1. All eight are lit and non-uniform.
Looked at, not just measured: the peer's frames show the hub's stone stairs, a seated NPC, a
campfire, chests and the held sword; the host's are darker interiors (a cave mouth, a stone
pillar, a wall) but geometry is visible in each and none is a black screen. The host's darker
frames are the reason its average is 39 and not 60 -- they are the lit interiors of the hub, not
the "darkness" of an unshown level, which measures as a nearly uniform near-black.

### R129. THE TAKE, BOTH DIRECTIONS - the drop is taken when `Acquired` flips, and the item is paid once

`b26dec7` reordered the lever: `Interact` first (it IS the take), then the pickup's own dismiss
presses. Measured in `780_SZ_Depths`, two drops both machines held (`DD452DFD#0` and
`54F6FD50#0`, `Herb_Item_BP_C`), one take each direction. **Every line below is from the run.**

Direction 1, the HOST takes `DD452DFD#0`:

```
9:58:5x  key DD452DFD#0: BOTH machines hold it
9:59:01.084 take: gate before stage requireActivation=0 activated=0 acquired=0 isADrop=1 validGUID=n/a
9:59:02.697 take: stage 0 the pickup's Interact (the take) ran=Interact drop=standing
9:59:02.697 take: gate after the press requireActivation=0 activated=0 acquired=1 isADrop=1 validGUID=n/a
9:59:02.710 loot: local pickup DD452DFD#0 (Herb_Item_BP_C) -- tombstoned for the partner
9:59:02.767 inventory host set 2 entries (live 2) rev=214
9:59:14.953 take: 'drop:DD452DFD:0' TAKEN (Acquired=1, item granted); the showcase was not dismissed by any press, so the actor still stands -- the loot lane counts the Acquired flip as the take
```

and on the PEER, 49 ms later:

```
9:59:02.759 loot: hiding local drop DD452DFD#0 -- the partner took it
9:59:02.984 inventory grant applied: /Game/Items/Consumables/Herb/HealingHerb.HealingHerb +1 (host 1, peer 0)
```

The same drop, taken again on the PEER (the "it must not pay twice" half):

```
9:59:23.996 WARN take: 'drop:DD452DFD:0' resolves to no live drop on this machine
10:00:11.634 WARN rig: drop DD452DFD#0 is already gone on this machine
```

Direction 2, the PEER takes `54F6FD50#0`:

```
10:00:42.812 take: gate before stage requireActivation=0 activated=0 acquired=0 isADrop=1 validGUID=n/a
10:00:44.502 take: stage 0 the pickup's Interact (the take) ran=Interact drop=standing
10:00:44.502 take: gate after the press requireActivation=0 activated=0 acquired=1 isADrop=1 validGUID=n/a
10:00:44.522 loot: local pickup 54F6FD50#0 (Herb_Item_BP_C) -- tombstoned for the partner
10:00:44.542 item intent queued /Game/Items/Consumables/Herb/HealingHerb.HealingHerb x2
10:00:57.038 take: 'drop:54F6FD50:0' TAKEN (Acquired=1, item granted); ...
```

and on the HOST, 28 ms later:

```
10:00:44.550 loot: hiding local drop 54F6FD50#0 -- the partner took it
10:00:44.665 inventory grant applied: /Game/Items/Consumables/Herb/HealingHerb.HealingHerb x1 (peer total 1 rev=313)
10:00:44.665 inventory host set 2 entries (live 2) rev=314
10:01:57.349 WARN rig: drop 54F6FD50#0 is already gone on this machine     <- the second take, refused
```

**The one number that settles Session 24's question.** Session 24 ended with "`acquired=1`
before any press". Here the first gate of a real take reads **`acquired=0`**, and `Interact` is
what sets it: 0 -> 1 in the same line, the lane's tombstone 13 ms later, the inventory 70 ms
after that. The three earlier sessions' "the verb runs and consumes nothing" is explained
exactly: the press that matters is the pickup's own `Interact`, and it was never the first
press.

**The inventory half, per machine and not inferred.** The taker's own lines, in the take
window: one (`inventory host set 2 entries (live 2) rev=214` on the host; `item intent queued
... x2` on the peer), with **0 duplicate and 0 refused** lines. The partner's lines, in the
repeat-take window: **0**. Direction 2's neutral check on the host after its own refusal:
0 gain lines. The verify was `39 PASS, 0 FAIL` for the phase, with kill sets diff 0
(39 dead keys each), shared HP 26 compared / 0 unequal, both machines alive, zero fault and
zero `ERR` lines.

**What is still not proven about R129.** The showcase actor stays standing unless a dismiss
press removes it -- the lever's own line says so ("the showcase was not dismissed by any press,
so the actor still stands"). On this content the dismiss presses run and the actor persists, so
a drop's actor is *consumed by state* (`Acquired`) rather than removed, and a human sees a
pickup that stops paying but may still be on the ground until it hides. That is a content
behaviour the lane handles; it is not the same as "the take did not happen", and the inventory
is the proof that it did.

### The harness: the four fixes this session came for, and one more

(a) **The soak no longer enters the cutscene worlds.** `997_FinalZoneCutscene` is a dark, empty
cutscene map; every hard load costs a whole-world teardown whose only dividend in a play soak is
crash risk. Hard loads stay in the dedicated `hardloop` phase, which does nothing else. The
soak's segments are hub / 780 (inside the loot step) / 770 / leave / hub-again / hub-final.

(b) **Every Stop-All logs WHY before it kills anything**, with the phase, the reason and the
last line each machine wrote:

```
STOP-ALL #3 phase=rig reason=UNHANDLED exception in phase 'rig': Cannot convert ...
  killing inst1 pid=79084; its last log line was: ...
```

That is the missing evidence for Session 24's "both processes gone in the same second, logs cut
mid-line, no fault" -- which is what this harness looks like from outside when it cleans up
after an error. In this session it caught two of its own defects in exactly that shape (a
`$H`/`$h` case-insensitive shadowing and a `-f` precedence bug), and both were fixed rather
than left as "the run died mysteriously".

(c) **`WORLD DIVERGENCE` is counted outside a travel transition** (+/- 20 s around any hard
load or area entry/leave on either machine). Session 24's strict `count == 0` failed on four
lines that each resolved by themselves. This session: 0 total in the loot phase, 0 outside a
transition; the soak's numbers are in its table.

(d) **Shared HP is not sampled for 40 s after a hard load or an area change**, and every
suppressed sample is printed with the time left on the gate. Session 24's `hub-final` opened
with `shared-alive=0` because the tables were still rebuilding.

(e) (brought forward, not asked for) **the per-hop measurement in the `rig` phase**: every hop
is classified, and the pawn's own position after it is read, so "did he fall" is a number and
"the guard caught it" is a different number.

### The soak, measured

One pair of processes, the documented PLAY set (`switches: flags=1 inventory=1 actors=1
enemy=1 puppet=1 playerBody=1 dialogue=1 stream=1 travel=1 cutscene=1 movers=1 loot=1
suspend=1 pause=1`), `soak` phase, 10:04:18 -> 10:43:48. No hard travel, by design (3a).

| segment | ran | iterations | both alive |
|---|---|---|---|
| hub | 578 s of 576 s | 166 | yes |
| loot step (`780_SZ_Depths`, entered by the step itself) | ~9 min | - | yes, both takes |
| area770 (`770_SZ_Swamp`) | 290 s of 288 s | 81 | yes |
| leave (back to the hub) | - | - | yes, the peer followed 17 ms later |
| hub-again | 255 s of 252 s | 62 | yes |
| hub-final | 701 s of 684 s | 145 | yes |

**MEASURED BODY: 1824 s of 1800 s.** Wall clock including the loot step and the transitions:
2370 s. `=== 56 PASS, 3 FAIL ===` -- the three failures are the last three bullets below.

| the run's answers | measured |
|---|---|
| both machines alive at the end | **yes** -- 0 `FAULT` lines, no crash dump on either |
| kill sets | host-dead=**298**, peer-dead=**297**, **diff=1** (`24848FF5` dead only on the host) |
| shared HP | 25 samples, **307 compared, 11 unequal**, 0 unreadable, 0 ghosts, `duplicates=0`, `mx-differ=0` |
| same world | `Ruins_Level_00` on both, `agree=1`, area `-` on both |
| `WORLD DIVERGENCE` | **0 total, 0 in a transition, 0 outside one** (11 travel events seen) |
| faults / `ERR` | 0 `FAULT` lines on both; **1 `ERR` line on the host**, 0 on the peer |
| area changes | 3 (780, 770, leave) -- the counter prints "2" because it counts ENTRIES and the leave is its own verdict |
| loot takes | 2 more, one each direction (`3EEBDB47#0` host takes, `826B568F#0` peer takes), all `r129-*` verdicts PASS |
| rig hops | 475 landed; **0 refused-not-shown**, 2 no-target, **18 `rig: FELL` restores**, 452 of them counted by the soak's own hop counter |
| HP sampling gate | armed at the 780 entry, the 770 entry and the leave; **no sample fell inside a gate** (the 75 s rhythm never landed in the 40 s), so nothing was suppressed -- stated rather than claimed as a save |

**What passed, with numbers.** 1824 s of body in one pair of processes with 475 rig hops
driven, three area changes that all followed on both machines, two more loot takes, **zero
fault lines and no dump**, and **zero `WORLD DIVERGENCE` lines at all** -- (c) has nothing to
excuse in this run because the run never had the pair apart. The area entries and the leave are
in the report with the `rig: level engine state ... LoadedLevel=1 actuallyVisible=1` line from
BOTH machines.

**What failed, precisely.**

* **One key is dead on the host only** (`24848FF5`), so the kill sets diff by 1 where Sessions
  22-24 got 0. The end-of-run dump explains the shape: `host keys=298 (dead 298); peer keys=306
  (dead 297)` -- the host had killed **every** key it knew and the peer had 9 alive. A single
  one-sided death out of 298 is the same class as R146 (a death observed on a copy the machine
  did not run) and is not explained here.
* **11 of 307 shared-HP comparisons are unequal (3.6%), all in the hub.** Seven of the eleven
  comparisons are the same shape: **host 140, peer 40** (six keys) and **host 140, peer 30**
  (one key, `FB667C1A`). 140 is exactly `BookBug_Popcorn_C`'s MaxHP, so the host holds one of
  these at full health while the peer has it damaged. `duplicates=0` and `mx-differ=0` say it is
  NOT Session 23's "two bodies under one key" -- one record on each machine, and they disagree.
  The other shape is one key (`7506BDBA`) at **host 199 / peer 208**, then **189 / 198**: a
  9 HP offset that persists and shrinks as the enemy is damaged. Neither is explained, neither
  is claimed fixed, and together they are this session's top sync item.
* **One `ERR` line on the host**, at 10:23:23 (inside `area770`):
  `ERR actor-state: game writer for guid=14fe95ef-... value=1 read back -999999 (WRITE FAILED)`.
  That is the container-write read-back guard reporting a write that did not stick -- a real
  write failure in `770_SZ_Swamp`, counted as a failure rather than waved through, and the same
  shape PLAYTEST's "progress can lag in a chest-dense area" describes.

**About the end-of-run line `shared live keys at the end: 0`.** It is true and it is not a sync
failure: the host's final dump is 298 keys and **every one is DEAD**, the peer's has 9 alive, so
there is nothing left to compare at that instant. The 307 comparisons above are the 25 samples
taken through the run.

### The live-proof table

| Claim | Verdict | Key line |
|---|---|---|
| The rig never leaves the pawn more than 30 m below its target | **PASS** | 128 hops, `left-dropped=0`, `guarded-back=37` |
| The fall guard actually runs | **FAIL (the defect), then PASS (fixed)** | `guard: armed ... (-109 ms in)` then nothing; after the signed fix, 42 `rig: FELL` in one phase |
| A rig placement the game then moves is undone | **PASS** | `rig: FELL ... placed at z=-1437, now z=-2966; put back at 1450,509,-1339` |
| The scene is lit and has geometry | **PASS** | 8 screenshots, mean luminance 39.2 / 42.0, stddev 25.4 / 26.1, lit 8/8, and looked at |
| A target's level is NAMED, so the visibility rule can be judged | **PASS** | `level=PersistentLevel shown=1` on 128 of 128 hops |
| The visibility rule refuses something on this content | **NO -- 0 of 128** | all 128 targets answered `shown=1`; the rule has no effect here |
| A drop both machines hold can be TAKEN | **PASS, both directions** | `loot: local pickup DD452DFD#0 (Herb_Item_BP_C) -- tombstoned for the partner` / `... 54F6FD50#0 ...` |
| `Acquired` is 0 before the take and 1 after | **PASS** | `gate before ... acquired=0` -> `stage 0 ... Interact` -> `gate after ... acquired=1` |
| The taker's inventory changes exactly once | **PASS** | `inventory host set 2 entries (live 2) rev=214`; peer: `item intent queued ... x2`; `bad lines=0` |
| The partner hides its copy and cannot pay it out again | **PASS** | `loot: hiding local drop DD452DFD#0 -- the partner took it`, then `rig: drop DD452DFD#0 is already gone on this machine`, `gain lines=0` |
| Both alive after >= 1800 s of body | **PASS** | 1824 s of 1800 s |
| Two area changes follow | **PASS** - 3 of 3 | `travel: entered the partner's area 780_SZ_Depths/770_SZ_Swamp`, `travel: left the area 770_SZ_Swamp` |
| Kill sets diff 0 | **FAIL** - diff 1 | `host-dead=298 peer-dead=297 diff=1` (`24848FF5`) |
| Shared HP equal | **FAIL** - 11 of 307 | `host=140 peer=40` x6, `140/30` x1, `7506BDBA` 199/208 then 189/198 |
| Zero uncaught faults | **PASS** | 0 `FAULT` lines, no dump, both machines alive |
| Zero `ERR` | **FAIL on the host** - 1 | `ERR actor-state: game writer ... (WRITE FAILED)` |
| `WORLD DIVERGENCE` outside a transition | **PASS** | 0 total, 0 outside |

### What is still open

- **The visibility rule is inert on this content.** 0 refusals in 128 hops, because every
  target answers `level=PersistentLevel shown=1` -- in the hub AND inside a streamed, shown
  area. Either these actors really live in the persistent level or `ActorLevelOf` answers
  "persistent" for everything; the two cannot be told apart from these runs, and until one is,
  `bIsVisible` is not the test that protects the player. What protects him is the fall guard
  (42 restores) and the teleporter refusal.
- **The rig can still descend in steps.** With the teleporter refusal the soak's pawns stayed in
  the hub's own height band (host z 762-3522 while hopping, peer 1167-2311), which is the fix
  working; but a chain of placements each under the 15 m guard threshold can still walk a pawn
  downhill, and nothing in the lane forbids that.
- **11 of 307 shared-HP pairs disagree** (the `140/40` and `199/208` shapes above), with no
  duplicate record and no MaxHP difference to explain them.
- **One one-sided death** (`24848FF5`) and **one failed container write** (`ERR actor-state`).
- **The play soak has no hard travel in it any more** (3a). Hard loads are covered only by
  `hardloop`, which this session did not run -- so R152's containment is unchanged from
  Session 24 and was not re-measured.
- **The pickup's showcase is never dismissed by the rig**, so a taken drop's actor can stay
  standing. The lane counts the `Acquired` flip (the item is granted once and the tombstone is
  permanent), so this is a visual residue, not a second payout -- but it has not been driven to
  a hidden actor by a human-visible input.
- **Boss phases, hit reactions, the shop door, peer vitals** -- unchanged from Sessions 20-24.

### What a human tester needs to know

Three things changed. **You should no longer end up in the dark**: the placement rule now
refuses a spot that is inside a teleporter's volume (the hub's NPCs stand on one, and that is
what used to drop you 190 m into the stacked maps), and any rig move that leaves you more than
15 m below where your feet were is undone within a few seconds -- measured 42 times in one
phase, each one logged as `rig: FELL ...`. **Picking up a drop works**: walk onto one and it is
taken, the item is granted once, and your partner's copy hides and cannot be taken again. If
you take the same drop twice you will get nothing the second time, by design. **A long session
still has two rough edges**: an enemy can read 140 HP on one screen and 40 on the other, and one
key out of 298 died on only one machine -- if a kill looks one-sided or an enemy's health looks
stale, that is the open item and the log will name the key.

## Session 26 - 2026-11-12 (bosses started together, and the three crashes it took to get there)

Harness: `mod/tools/session26.ps1 -Phase bosses [-Only ...]`; eighteen runs (s26-run1..18).
Each boss is entered with both machines, the HOST is walked into the fight's start volume
(`qd.trigger Name@Area`), and the verdicts read: the peer replayed the entry, one boss key alive
on both, one bar, a hit from each player moves one HP, the death on both, both bars down, and
the death flag in both banks.

### What is proven, live, on both machines

| Boss | Start replayed | Same key | One HP | Bars up/down | Dead on both | Death flag in both banks |
|---|---|---|---|---|---|---|
| Green dragon + rider (780) | yes | 17181166 | 3500->3300 both | yes | yes | `DragonLordDead = 1` (runs 10, 13, 14) |
| Red Lords (740) | yes | DBF4FFD3 / 17E153F4 | 6000->5800 both | yes | yes | `RedLordsDead = 1` (runs 14, 16) |
| Shrine Boss (100) | yes | 03FDBC21 | 260->60 both | yes | yes | (no flag in the content) |
| Whale King (210) | yes | 6AEFB87A | 1500->1300 both | no bar | yes | **missing** |
| Demon Bosses (450) | yes (run 15) | 30CACF5B | 2300->1900 both | yes | yes | **missing** |

### The faults found and fixed, in the order the runs exposed them

1. **Boss flicker / visible on one screen** (user report): a frozen follower took the dragon
   unilaterally while the owner still ran it, so both simulated it. Fix: no unilateral take;
   a boss is sticky (never handed off mid-fight). `qdenemyown.h`, 70 checks.
2. **Partner "fell under the map, came back elsewhere"**: the rig's own fall guard read the
   boss pull-in (a 170 m drop into the arena) as a fall and put the partner back in the hub.
   A mod placement now disarms the guard; the trigger lever guards only a drop past the volume.
3. **Darkness for two minutes after an area loads** (user report): a summoner without
   `GetSaveGUID` fell through to a GObjects-wide search per super class, ~0.55 s a call, one
   call per new enemy per frame -- the host ran at ~2 fps. Classes without it are remembered.
4. **A rider is not its mount**: `JosamiLord_Green.GetHP` answers the dragon's HP while riding
   and after (-496900 after the kill), so HP<=0 recorded the lord dead and the spawner never
   wrote `DragonLordDead` (it waits for both). When the game exposes its own dead bit, that bit
   is the answer (death sweep, enemy dump, bar liveness; `qdenemy.h EffectivelyDead`).
5. **Records cleared by a teleport that never loaded**: the teardown gate for a game-started
   teleporter cleared the enemy records, the world did not change, and every partner boss/kill
   row was refused "for another world" for the rest of the run. The scan rebinds when the
   records were cleared; the gate no longer fires within 15 s of arriving.
6. **Startup crash, three in a row (runs 5-7, R140 family)**: the engine's timer manager
   (`FTimerManager::Tick`, read in Ghidra at 0x142CC9D00) executed a timer whose delegate memory
   had been freed and reused, on the first engine frame after the module's third frame in a
   fresh world. The module now leaves a new world alone for 5 s; runs 8-18 never repeated it.
   The engine fault itself is not fixed.
7. **The bar from another area**: every boss is resident, and an untouched Red Lord took the
   shared bar while the pair fought the dragon (the "bar that never disappeared"). A boss is a
   bar candidate only when its level is shown and it is damaged or within 60 m of a player.
8. **Trigger names repeat across sublevels, and the placed test was wrong**: the FName number
   split makes the cooked `ActivationTrigger4` ("ActivationTrigger", 5) read as a runtime spawn,
   so 740's Red Lords trigger was never indexed. The trigger lane now uses `RF_WasLoaded`
   (`ActorWasLoaded`); the lever takes `Name@Area`; the table is 4096 with a FULL error.
   Widening the placed test for EVERY lane made the object lane write `Activated` on levers it
   had never handled, so it was narrowed back to the trigger lane.
9. **Crash entering 500_AUT, twice (runs 15 peer, 16 host)**: an access violation inside a
   Blueprint on the frame after the object lane set `Activated 0->1` on lift / elevator levers.
   Lift and elevator controls left the object lane (they drive a mover the mover lane carries);
   run 17 and 18 crossed 500_AUT with zero faults.
10. **The Red Lords revive a lone death**: one sister killed alone is stood up by the other.
    The rig kills a boss group within a second; `qd.flag` reads the game bank, not the lane.

### Open

- Death flag for Whale King and Demon Bosses (the kill path ran; the flag write did not).
- The 760 dragon, Necronomicon, Bridge Lord and Giant Fish did not spawn from the rig: their
  spawners have `SpawnOnActivate=0` (or the boss needs its own trap), and their story gates are
  "not yet dead" and pass. A rig gap, not a measured sync fault.
- Boss phases are not a synced value; each machine runs its own copy's phase script on shared HP.
- R140 (engine timer use-after-free) is avoided at startup, not fixed.

## Session 27 - 2026-11-12/13 (the small gaps: partner vitals, loot actor, hit reactions, shop door)

Branch `sync-fix`. Phase `tools/session26.ps1 -Phase gaps`, documented play set, roles
host=inst1, peer=inst2. Six runs (`s27-run1..6`); run 1 was aborted when the relay was found
dropping the new STATUS body.

| Gap | Result | Evidence |
|---|---|---|
| Partner HP on screen | **closed** (runs 2, 3, 5, 6) | peer `rig: selfhp -> 37.0`; host `vitals: partner hp 37/100` 53 ms later, `vitals: drew the partner bar PARTNER  37 / 100 on 1920x1080`; the host screenshot shows the bar at the top centre |
| Partner stamina | **does not exist** | the player Blueprint declares no stamina; only `PlayerBalance.BaseJumpStaminaCost` remains |
| Taken drop actor still standing | **closed** (runs 3, 5, both directions) | `loot: taken drop 54F6FD50#0 still stood 15 s after the take; PutAway ran -- hidden instead`, verdict "left the TAKER's screen" PASS |
| Enemy reaction to a partner hit | **closed** (runs 3, 5, 6) | ordinary enemy: `hit on enemy 1C5045C7 from the other player, 44.0` then `damage: enemy hp via TryApplyDamage (the game's own hit pipeline, hit=empty) 220.0 -> 176.0`, host 176 / peer 176; green dragon IN its fight: `via TryApplyDamage ... 3500.0 -> 3300.0`; a class that refuses every payload (`Guardian_Fishman_BP_C`) plays its own `DoCustomDamagedFX` (`reaction: fallback presentation ... customFX=played`) |
| Shop door (R105) | **closed** (runs 4-6) | host `OpenDoor` from the door's side 0 -> `flag NEW ShopDoorOpened = 1`; peer bank `ShopDoorOpened = 1 (game bank 1, lane table 1)` 40 ms later; peer `flag refresh: ShopDoorOpened=1 -> Shop_Door_00_SM [Shop_Door_00_BP_C].SetToOpenState ran`; both screenshots look into the shop through the open doorway |

### What was wrong, and what changed

1. **The partner's HP was never sent.** STATUS now carries an optional vitals tail (`hp`,
   `hp_max`, `flags` valid/dead; WIRE.md 3.15). The receiver draws a bar with the HUD's own
   `DrawRect`/`DrawText` right after `FS_HUD_C.ReceiveDrawHUD` crosses the ProcessEvent hook.
   **The relay validates every body** and logged `status has 5 byte(s) left over` for the new
   tail, so `qd-core` decodes the optional tail and the relay was rebuilt.
2. **The taken drop**: 15 s after a local take the loot lane runs the pickup's own `PutAway`
   once and hides the actor if it still stands. `PutAway` alone does not complete outside a
   showcase; the hide is what removes it. The take lever's stage 2 is now `PutAway`.
3. **Hit reactions.** Ordinary enemies always took the module's payload through
   `TryApplyDamage`, the game's whole on-hit pipeline. Bosses fell to the clamped HP write.
   Two findings:
   - **A built `FHitResult` sent FIRST stopped drops**: 24 kills in 780 rolled no drop (run 2).
     The empty-hit call is first again (drops back, run 3), and the built hit (enemy, its mesh,
     a point on its body, by reflected field name) is only the retry.
   - **The green dragon's override** (761 bytes) calls the parent `TryApplyDamage` and then
     `CheckForSplit`, and references its intro montage and `DoFlyingStateEntry`. Outside its
     fight the game's own gate refuses every payload; inside the started fight it applies
     (run 6). The HP write remains the last resort and now plays the class's own
     `DoCustomDamagedFX` (and `TookDamageFlash` where a class has it).
   - A `MountedLord` HP drop is also accepted as the game applying the hit, for a mount that
     forwards its hit to a rider. On the green dragon this did not trigger: its override calls
     its parent, so it does not forward.
4. **The shop door was never a lane failure.** `Shop_Door_00_BP_C` is one-sided. Its `Activate`
   takes `(ActivateByLoad, ActorThatTriggered)` and opens nothing from a press; the player's
   open is `OpenDoor`, which saves the NamedSaveData flag `ShopDoorOpened`. The door keeps no
   `Opened` state of its own, and it lives in `VideoConfigMap`, a streamed sublevel of
   `Ruins_Level_00` despite its name. The flag lane carried the flag, but the partner's door had
   been loaded before it and reads the flag only on load. **`FlagRefreshWorld`** (new): when a
   flag from the partner is applied on either side, actors listed for that flag run their own
   load path (`Activate` with `ActivateByLoad=1`, then the visual `SetToOpenState`). The table
   has one entry today; it is the mechanism for the wider "world refreshes when a flag
   applies" story gap.
5. **Rig levers added**: `qd.act near:<name>!<Verb>[@side]` (a named verb, optionally from one
   of four sides of the object), `qd.selfhp`, `qd.nohit` (A/B the hit payload).

### Open from this session

- Run 6: the peer did not replay the green dragon's start trigger once (`trigger: replayed`
  missing), ran its own copy and HP-wrote it locally. That is a boss trigger-replay flake, not
  this lane.
- Run 5 crossed midnight and the harness's time-of-day marks counted two pre-midnight inventory
  lines as "since" (one false FAIL).
- The HP write still overrides a boss's own immunity outside its fight (it was the design: the
  last resort). Whether a partner hit on an immune boss should be dropped instead is open.

## Session 28 - 2026-11-13 (story refresh, bosses that never spawned, death flags)

Harness: `mod/tools/session26.ps1 -Phase story [-Only ...]`; six runs (s28-run1..6). Zero faults,
zero crash dumps, both processes alive at the end of every run.

| Item | Result | Proof |
|---|---|---|
| World refresh on a partner's flag | **closed** (run 1) | host `rig: flag set -- SaveFunctions.SaveNamedData(LibrarianQuestDone, 1, RefreshQuestStates=1) ran`; peer `quest refresh: GI_UpdateQuestState ran for 1 of 1 flag(s) [LibrarianQuestDone] + GI_NotifyWorldStateChanged ran`, then `quest state: LibrarianQuestStateManager (LibrarianQuestDone) InternalState 0 -> 1` 0.2 s later |
| Demon Bosses death flag | **closed** (run 1) | the hell-bat is in the group; `DemonBossesKilled = 1 (game bank 1)` on both |
| ROS dragon spawns | **closed** (runs 2, 3, 5, 6) | peer `boss follow-through (replay) ActivationTrigger11.DoTrigger ran; ROSDragon_Spawner Activate ran -- managed enemy present`; key 8120191E alive on both, one bar, 9999 -> 9799 on both, dead on both |
| Bridge Lord spawns | **closed** (runs 2, 3) | its `BridgeCenterTrigger` overlap event starts it; key 2543387C, 4000 HP, on both |
| Giant Fish | **spawn + flag** (run 4) | treated as a boss (pulled in); `GiantSwampFish_IsDead = 1` on both; the host's copy took a summon key, so HP was not shared that run |
| Whale King | same key on both (run 3); spawn is flaky under the rig | the fight did not start (trap before spawn): deferred `ActivateEnemy` added; runs 4-6 the whale never spawned on the host (proximity + visibility rules vs. the rig's placement) |
| ROS / Whale / Bridge Lord / Necronomicon death flags | **open** | see SYNC-GAPS: written at the end of each boss's own death sequence (montage callback or death dialogue) |
| Necronomicon start | **open** | starts from closing the lectern's read message (`CustomEvent` -> managed enemy `DoIntroAnimation` -> `Activate`); the book enemy never existed under the rig |

### Changes

1. `QuestRefreshQueue` / `QuestRefreshPump` (every applied flag -> the game's own quest refresh),
   with a 4 s watch on the quest managers keyed on the flag.
2. Trigger lane: `BossFollowThrough` after a boss entry, rig and replay; deferred `ActivateEnemy`.
3. `BossStartNote` / `BossStartPump`: a partner's live boss with no copy here starts the local
   spawner (recipe per class), within 300 m, never for a dead key; the peer asks the host about
   bosses the host sent no row for.
4. A boss death on the owner is claimed on the kill log without the mod's immediate
   `StoreEnemySaveState(hp=0)`; the boss's own sequence records it.
5. Rig: `qd.flag set:<Name>=<v>`; `qd.hard` re-entry (the nonce was stripped before the
   "already applied" compare, so a second entry into the same area did nothing);
   `qd.act near:X!Verb` takes `+` for spaces and `~Prefix` for compiler-named events, and fills
   every object parameter; harness `story` phase, staged boss kill, presence retries.

### Open from this session

- ROS dragon / Whale King flags (death montage callback never reached under the rig's kill),
  Bridge Lord / Necronomicon flags (need the death dialogue driven).
- Necronomicon start under the rig; Whale King spawn under the rig.
- Giant Fish key source (spawner vs summon) for HP sharing.
- Ending reached together: the ending cutscene managers (`Ending1CutsceneManager_C`, ...) are
  spawned at runtime, so the cutscene lane falls back to HOLD on the partner; not yet tested.

## Session 29 - 2026-11-14 (boss conclusions, fight starts, the ending, one body per boss)

Harness: `mod/tools/session26.ps1 -Phase story -Only <bosses>`; runs s29-run1..15. Zero FAULT
lines and zero crash dumps in every run; both processes alive at the end of every run.

| Item | Result | Proof |
|---|---|---|
| Why the montage flags never came | **found** (run 1) | boss death watch: the dragon's `Dragon_Ground_Die_Montage` loops 2.3..2.6 s for 45 s -- the downed boss waits for a conversation |
| ROS dragon death flag | **closed** (run 4) | host talk -> `Dragon_ROS_FinalDie` -> `ROSDragonDead`; peer `boss conclusion: Dragon_Ros_BP_C.RewardDialogueEnded ran on the downed copy`, its own `Dragon_ROS_FinalDie`; `= 1` in both banks |
| Whale King | **closed** (run 5) | one key 6AEFB87A, one bar, 1500 -> 1300 on both, conclusion on the peer, `WhaleKingIsDead = 1` in both banks |
| Bridge Lord | **closed** (run 8) | key 2543387C on both (peer mirror), one bar, 4000 -> 3599.9 on both, `EndedDeathDialogue` conclusion on the peer, `BridgeLordDead = 1` in both banks |
| Necronomicon | **closed** (runs 9, 14) | spawn gates measured (`CheckSpawnConditions Success=1`, `Vis Checks Passed=0` while watched); the host's start announced (`left its rest spot`), peer copy follows; one bar, 3000 -> 2600 on both; `Ended` conclusion on the peer; `NecronimiconDefeated = 1` in both banks |
| Giant Fish | **closed** (runs 6, 9) | key 51BC2B8B on both, shared bar, 1800 -> 1600 on both, `GiantSwampFish_IsDead = 1` in both banks |
| Ending (Queen's final state) | **closed** (runs 10, 12-14) | `cutscene: local started Ending1CutsceneManager` on both (or the partner's sequence resolved locally), `Ending1Complete` / `EndingSeen` written by each machine's own game, `partner's cutscene ended; resuming together` |
| Queen's full fight | **open** (run 15) | begun properly (`plays its intro`, announced); phase 1 at HP 0 enters `DoStateTransition` and waits (orb / special sword / second form) -- not yet driven or synced |
| Second ending / duplicate Queen | **closed** (runs 13-14) | retired copies never rescanned; a recorded-dead boss copy born after the death retired quietly |
| Operator's mouse | **closed** (run 14 on) | `test input: 5 of 5 user32 calls hooked`; 1 (host) and 10 (peer) cursor/focus calls refused in run 14 |
| Peer actor-state ERR spam | **closed** | a failed (guid, value) write is not retried for 60 s (164 identical ERR lines in run 6) |

### Changes

1. `BossDeathWatch` (diagnostic): a dead boss's montage, position and playing state for 45 s.
2. `BossConclusionNote/Pump`: a partner's conclusion flag runs the boss's own dialogue-end event
   on the local downed copy.
3. `kBossBegin` + `BossBeginPump` / `BossBeginApply` / `BossBeginQueuePump`.
4. Boss identity: `GiantFish_Enemy_BP_C`; last readable boss HP kept 30 s.
5. `RetireActor` / `IsRetiredActor`; deferred `ActivateEnemy` skipped within 2 s; recorded-dead
   boss copy born after the death retired quietly.
6. `qdwrite.h`: failed actor-state write memo.
7. Test instances: user32 cursor/focus hooks, `WS_EX_NOACTIVATE`, foreground hand-back.
8. Rig: verb outputs logged (`rig: verb X outputs:`), spawn verbs ignore the visibility check,
   `near:dialogue!End` ends the player's own conversation; harness `Talk`/`Begin` lists,
   `Stay`, `Near`, `Wait`, probes opt-in by `probe:<Name>`.

### Open

- The Queen's second phase (orb cutscene, special sword, `Queen_Nude_Data` form, Sipeha branch).

### Still the harness's, not the mod's

- The rig's placement drops a player out of some arenas (the swamp, the bridge, the castle for
  the peer, the Whale King's ledge): `rig: FELL after a rig move`. A real player walks in.


## Session 29, continued - 2026-11-14 (one Queen, fight starts from either machine, the screen after the ending)

Harness: `session26.ps1 -Phase story -Only 'Queen'`, runs 33-43. Run 43: 47 PASS, 3 FAIL. All
three failures are checks the run never got to measure, not wrong values: the HP sampler took
1 sample in the whole run (gated around the area entry, the fight and the leave; 56 and 5 in
runs 41/42), and no orb attack was fired in any run (0 `Orb_Projectile` lines on either machine;
the rig kills the Queen before she reaches it). The boss's own HP was equal (2200/2200).

| Gap | Status | Proof |
|---|---|---|
| Two Queens (both machines ran her) | **closed** (runs 37, 43) | Root cause: the ownership table held 256 records, the castle has ~600 enemies, so `ownership table full` and the Queen got no record and ran on both. Now 1024 records behind a hashed GUID index. A double run is also resolved by role: peer `enemy owner: C88FFC0C (boss) was run by BOTH machines; this machine yields` |
| Host could not hand the boss to the nearer peer | **closed** (run 38) | the host never saw a peer row for the boss (`sawRemote=0`); boss rows now go out every tick, and the boss ACK counts as seen. Run 43: `PASS [2] the PEER runs the Queen while its player is the nearer one` |
| Boss bouncing between owners inside 3 m | **closed** (run 38) | a grab boss uses a 300 cm margin, and the role tie-break does not apply inside it |
| Fight start not relayed when the talker's machine is the follower | **closed** (runs 39, 43) | any copy may announce the begin; a follower applies it once its copy exists (`BossBeginApplyRow`). `PASS [2] the PEER's copy began from the partner's start` |
| Black screen on both after the ending video | **closed by fallback** (runs 42, 43) | the ending's `EndingWakeOnBeach` holds a camera fade; its second half (wake montage, fade in) never runs. 15 s after the ending `PostEndingWakeOnBeach` is called, and at 25 s `StopCameraFade`. Run 43 shots 35 s after the ending: mean 166.7 / 168, `PASS the screen is not black` on both |
| Grab overlay / locked movement left after the ending | **closed** | `GrabCleanupNow` 2 s after the local ending ends: `UnlockFromOrbStuff`, `FadeOutQuickTPOverlay`, collision and walking |
| Peer falls after the area-leave follow (run 42: z 140 -> -16556) | **closed** (run 43) | the follow places the player on the partner's own spot +1 m and holds it: `landing: holding this player at 4106,-1744,240 ... (area-leave follow)`, `released after 1515 ms`, then `local pos 4106,-1744,140` |
| Game teleports held by the landing hold | **closed** | only logged now: `landing: the game moved this player to -1586,-130,-41 -- its own teleport, left to the game` |

### Open

- Why the game never plays its own wake-up after the ending (the rig boots straight into
  gameplay past the main menu; not ruled out as the cause).
- The ending's `ClearAllEnemySaveStates` lets the Queen spawner make a new Queen; not yet
  compared with the single-player game.
- The orb check (the Queen's orbs aimed at the peer's own player) depends on an orb attack
  landing in the harness window.

## Session 29, runs 44-46 - 2026-11-14 (black world in the fight, the double spawn, the attacks on the peer)

Run 46: 50 PASS, 4 FAIL. No crash, one Queen (C88FFC0C on both), HP compared 61 of 61 equal (in
the castle), kill sets diff 0, the world drawn on both screens during the fight.

| Gap | Status | Proof |
|---|---|---|
| Host world black during the whole Queen fight (HUD only, runs 42-44) | **closed** (runs 45, 46) | both machines published ~18 hub levels while standing in the castle and each vetoed the other's hide of `050_Ocean` (~20/s); the manager's queue never drained and the Queen-intro fade waited on it. Now no veto and no forcing while the partner is within 60 m (`PartnerIsNear`); a far partner's level still gets one allow per 10 s. Run 46 fight shots: mean 88.7 / 83, `PASS the world is drawn during the fight` |
| Double Queen from a spawn race (run 45: host built a copy of the peer's Queen 1.1 s before its own spawner made her; new keys A7EC3543 / 083326E5, host died) | **closed** (run 46) | while a boss trap this machine activated has no enemy yet (4 s), a received spawn waits in the queue: `enemy spawn: guid=... waits -- this machine's own boss spawner was activated`. Run 46: C88FFC0C on both, no summon keys |
| Host crash in `EnemyIsBoss` (run 45: its static counter overwritten, wild write) | **guarded** | the counter is bounds-checked before use; the writer is not found (the minidump holds no .data) |
| The Queen's attacks reach the peer's own player | **closed** (runs 44, 46) | the peer's own copy fired: `effect: local #1 .../Queen_FastProjectile_BP_C` at 20:17:54 (run 46), 1-4 s after the first hits. She does not attack before she is hit, so the harness check now runs after the hits |
| HP never measured in the story run | **closed** (run 46) | a sample is taken from the fight's own dumps: compared=61 unequal=0 |
| `both machines hold the SAME area after the leave` | **harness** | the `world:` line is written every ~10 s; the check read the line from before the leave (host '-' at 20:21:32). Now waits 12 s |

### Open

- The game's own wake-up after the ending still does not play, with no veto active (run 46). The
  mod's fallback stops the held fade at 25 s and the world is drawn (run 46 post-ending shot: the
  beach at night facing a rock, mean 4-6, so the brightness check cannot tell it from black).
  The main-menu skip on the rig is the remaining suspect.

---

## 16 November 2026 -- the combined soak (every shipped switch, one pair): three seam bugs

Detail and the full results table: **[mod/doc/features/final-soak.md](features/final-soak.md)**.
Rig: `mod/tools/session-finalsoak.ps1`, one boot, one pair, the SHIPPED `mod/qdcoop.ini`
verbatim (only `RelayHost` and `LogLevel` rewritten).

This pass existed to answer a question no per-lane run can: what happens when **all** the
switches are on at once. It found three defects that each live in the seam between a lane and
the configuration, and none of them was visible from a single-lane session.

- **The shipped ini was silently truncated at 8192 bytes** (`qdconfig.h ConfigLoad` read a
  fixed 8192-byte prefix; the file had grown to 21265). Thirteen shipped-on switches --
  `PlayerBody`, `DialogueView`, `StreamUnion`, `MoverSync`, `PlatformRide`, `LootSync`,
  `SuspendGuard`, `SuppressMenuPause`, `CombatVfx`, `DeathRespawn`, `QuestItemSync`,
  `KillXPSync`, `PartnerSaveSync` -- ran at their compiled default of `0`. The boot line
  printed the truth and nothing acted on it. Fixed by reading to EOF; the parse loop is now
  `ConfigParseText()` so it is testable, and `test_config.cpp` (new, 42 assertions) reads the
  repository's real ini and asserts the LAST switch arrives -- plus that the first 8192 bytes
  alone do NOT contain it, which is the assertion that pins the bug.
- **A respawn cleared `bDead` but left `IsDead` set** (`qdrespawn_engine.h`), and `IsDead` is
  what every other lane reads. Measured: the host respawned at `1:32:59` and then, for four
  minutes while it stood there fighting, logged
  `dialogue view: not hijacking the local player: the local player is dead` 16 times and
  `save: the partner's save could not be mirrored within 20 s (blocked: the local player is dead)`.
  Both flags are now written, read back, and reported on the respawn line.
- **The relay dropped the whole enemy lane above 20 live enemies** (`protocol.rs`
  `Kind::EnemyRows`): the header's `n` is the LEDGER size, but a guard compared it against the
  per-datagram writer cap, so a 23-enemy world produced
  `malformed datagram ... (kind 0x14): enemy rows per datagram exceeds limit: 23 > 20`
  **1163 times in one run** and neither peer saw a single enemy row. Fixed to the same
  chunked-container rule `OBJECTS` already uses; the encoder keeps the cap.

Also in this pass: six bodies that had no shared wire vector (`KILL_XP`, `SAVE`,
`PLATFORM_RIDE`, `RESPAWN_DOWN/UP/REST`) got one in all three places, byte-identical between
native and Rust (80 vectors matched, 0 failed, 0 missing); and `WIRE.md` section 3 was made
contiguous -- **§3.21 `TRIGGER` had no section at all** and is now written, `§3.23
PLATFORM_RIDE` was moved back inside section 3 (it had been sitting after the "Reliable
kinds" block), and `3.26/3.27` renumbered to `3.25/3.26` with every cross-reference updated.

Live-proven in the combined run: door open **and** close with the door's own animation; a pot
broken on one machine and broken on the other with NO drop spawned on the receiver; a secret
wall opened through the object lane; keys granted and then USED to zero with the peer's copy
removed by the tombstone; a kill paying both players; a rest mirrored by the partner's own
writer with the echo bracketed; a death that stacks to 90 %, respawns at the rest point with
`input=1 walking=1`, and holds the cap; three weapon families equipped, drawn by the partner
and attacked; and a lift ridden with the place carried while it moves.

Not proven: the crossbow's projectile (it equipped and fired, but the game answered
`montage none => none` -- the pawn was in the previous weapon's recovery), and the 300 s idle
soak, which a concurrent delegate's live session contaminated (both sessions wrote the same
`qdcoop-qd1.log`; the harness now refuses to run without the LIVE lock).

## Session 30 - 2026-11-16 (boss phases: the state machine that was said not to exist)

Harness: `mod/tools/session-bossphase.ps1 -Phase bphase [-Only <bosses>]`, a `bphase` phase
added to the Session 26 rig and run through `mod/tools/run-gapboss.ps1` (which holds the LIVE
lock for the whole session and releases it). Play set, 27 switches, with `BossPhaseSync=1`.

**The measurement that unblocked GAP rank 1 and rank 2.** `SYNC-GAPS` and `WIRE.md` both said
boss phases were inexpressible because `Phase`, `bBoss` and `OnSubStateChanged` appear "only in
the exe name block and in zero cooked packages". That is true of those NAMES and false of the
mechanism. The cooked `Queen_BP` package (1194 names, 161 exports) imports the NATIVE base class
`FS_EnemyCharacterBase`, which carries a generic state machine -- `CurrentStateIndex` and
`CurrentSubState` (int32, readable and writable), the setters `SetCurrentStateIndex`/
`SetCurrentSubState`, and the events `OnStateIndexChanged`/`OnSubStateChanged` -- and
`Queen_BP` itself exports `DoStateTransition`, `OnPhaseChanged`, `DoOrbAnimation`, `OrbAttack`,
`SetUpPlayerForOrbCutscene`, `SetUpBossForOrbCutscene`, `OrbProjectileHitPlayer`, `KillOrbs`,
`UnlockFromOrbStuff`, `SipehaTransitionEnded`, `Ending1Complete`, `Ending2CutsceneEnded`, and
imports `Queen_Nude_Data`, `Queen_P1_P2_Transition_Montage` and all three ending/Sipeha
cutscene managers.

| Item | Result | Proof |
|---|---|---|
| The state machine resolves | **closed** | `enemy sync: FS_EnemyCharacterBase found` on both machines |
| The Queen's state is READABLE | **closed** | `rig: boss phase lever -- Queen_BP_C guid=C88FFC0C CurrentStateIndex=0 CurrentSubState=0` |
| The owner publishes a TRANSITION | **closed** | `boss phase: sent guid=C88FFC0C state=1 sub=0 rev=4 (TRANSITION)` (4:04:28.186) |
| The follower drives its OWN copy through the game's setter+event | **closed** | peer `boss phase: C88FFC0C driven here state=1 sub=0 (state written, OnStateIndexChanged ran)` (4:04:30.929) |
| A repeated state does not re-run the transition | **closed** | driving state 1 again produced no second `driven here` line |
| The arena rule for an absent player | **closed (stated + logged)** | `boss arena: fight guid=C88FFC0C CLOSED present=2 (the fight CONTINUES with the player who is here; the absent one may walk back in)` |
| Both machines agreed through it | **closed** | `host=Ruins_Level_00 peer=Ruins_Level_00 agree=1`, `area='600_Castle'` both, `WORLD DIVERGENCE 0`, kill sets `diff=0` |
| Nothing crashed | **closed** | `faults=0 err=0` both, no dump, both alive at the end |

Run 1 was 28 PASS / 7 FAIL, and **all 7 FAILs are harness framing**, each verified in the
transcript: the class-resolve check windowed 40 s from the phase's own mark while the lane
resolves the class once at boot; the `return0` step drove state 0 onto a copy already at 0
(no change, so a correct lane published nothing); and the shared-HP gate was armed for the
whole phase, so the end-of-run comparison had `compared=0` -- a check that prevented its own
measurement. All three are fixed in the harness.

### Changes

1. `qdbossphase.h`: the rules -- revision ordering (strictly older is stale, the SAME revision
   with different values heals, a newer one applies), `Decide`/`BarrierFor` for the arena,
   `IsAuthority` (the machine that runs the boss; the survivor takes over after 6 s of owner
   silence), `ShouldPublish`/`NextRev`, `EndingFlag`/`EndingMatchesBoss`, `PhaseTrack`/`TrackSet`.
2. `K_BOSS_PHASE` 0x5C / `K_BOSS_ARENA` 0x5D / `K_BOSS_END` 0x5E, reliable; bodies 29/22/22
   bytes; native and Rust byte-identical (`test_vectors`: 100 matched, 0 failed, 0 missing).
3. The engine half (`bossphase_lane` in `proxy.cpp`): read the int32 property with its SIZE
   checked; drive with the property FIRST and the event SECOND (the order is the contract);
   refuse any ordinal outside `-1..=255` rather than writing it into a Blueprint graph.
4. A rig lever `qd.bphase <Class>[=<state>[,<sub>]]#nonce` that reads with no `=` and drives
   through the SAME call the follower uses, so the run measures the lane rather than the lever.

### Open

- The Queen's transition was driven by the rig, not by playing the fight to HP 0: the harness
  cannot reliably reach the phase threshold. What that leaves unmeasured is the trigger, not
  the crossing -- `DoStateTransition` itself is not called directly (the lane writes the
  property and dispatches the graph's own `OnStateIndexChanged`).
- Arena post-process (`Queen_BossArena_PPC` et al.) and per-cue boss music remain unsynced;
  see `mod/doc/features/gap-boss.md` §5 for why each needs a decision first.
- The ending's own wake-up is unchanged and remains a rig question (one solo control run).
