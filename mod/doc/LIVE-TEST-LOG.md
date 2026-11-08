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


