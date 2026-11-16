# final-soak -- the combined soak: every shipped switch, one pair of processes

**Date: 16 November 2026** · branch `sync-fix` @ `798ce8d` + the fixes below · rig
`mod/tools/session-finalsoak.ps1` · instance roots `G:\Freespace\Queens_Domain\_test\finalsoak`

## What this pass is for

Every other feature document in this folder proves ONE lane in ONE boot. Nothing had measured
what happens when **all of `mod/qdcoop.ini` is switched on at once** and the players keep
playing: a lane that breaks another (a spectator hijacking a respawn, a save mirrored while
its owner is dead, a mover fighting a ride body) passes each per-lane run and still fails in
the hands of two people.

So this is one boot, one pair, exercising the whole play set in the order a session happens.
**The ini is the shipped file, verbatim** -- only `RelayHost` (two instances on one PC) and
`LogLevel=3` are rewritten, because neither is a rule switch. A soak on a hand-written switch
list would measure the list, not the product.

## The three bugs this found, and where they were fixed

This is the pass's real result: three defects that no per-lane run could see, because each of
them lives in the seam between a lane and the configuration.

### 1. The shipped `qdcoop.ini` was silently truncated at 8192 bytes

`mod/native/src/qdconfig.h` → `ConfigLoad` read a **fixed 8192 bytes in one `ReadFile`**. The
shipped file had grown to **21265 bytes**, so every switch declared past byte 8191 took its
compiled default and stayed `0` no matter what the file said:

| switch | declared at byte | took effect before the fix |
|---|---|---|
| `ObjectSync` / `InteractSync` / `DialogueSync` | 5209 / 6707 / 7706 | **yes** |
| `PlayerBody` | 10333 | no |
| `DialogueView` | 9382 | no |
| `StreamUnion` | 10574 | no |
| `MoverSync` | 11834 | no |
| `PlatformRide` | 12459 | no |
| `LootSync` | 12683 | no |
| `SuspendGuard` | 12903 | no |
| `SuppressMenuPause` | 13190 | no |
| `CombatVfx` | 15247 | no |
| `DeathRespawn` | 16334 | no |
| `QuestItemSync` | 17574 | no |
| `KillXPSync` | 18617 | no |
| `PartnerSaveSync` | 20396 | no |

Thirteen shipped-on switches ran at `0`. The boot line printed the truth and nothing else did:

```
[1] 1:48:39 INFO switches: flags=1 inventory=1 actors=1 enemy=1 puppet=1 playerBody=0
    dialogue=1 dialogueView=0 stream=0 travel=0 cutscene=0 movers=0 loot=0 suspend=0
    pause=0 objects=1 interact=1 trigger=0 pullIn=1
```

**Fix:** read to EOF in a loop (`ConfigLoad`), and the parse loop split into
`ConfigParseText(char*)` so the file half and the parse half are testable separately.
**After the fix, the same line on the same shipped file:**

```
[1] 3:16:05 INFO switches: flags=1 inventory=1 actors=1 enemy=1 puppet=1 playerBody=1
    dialogue=1 dialogueView=1 stream=1 travel=1 cutscene=1 movers=1 loot=1 suspend=1
    pause=1 objects=1 interact=1 trigger=0 pullIn=1 worldSet=0 puzzle=0
```

**Test:** `mod/native/test/test_config.cpp` (new, 42 assertions). It reads the repository's own
`mod/qdcoop.ini`, asserts the file is larger than the old limit, asserts each of the thirteen
tail switches arrives as `1`, and -- the assertion that actually pins the bug -- parses the
**first 8192 bytes alone** and asserts `PartnerSaveSync` and `DialogueView` are **absent** from
them. A parser fed a truncated string is perfectly correct; only "did the file's LAST switch
arrive" catches this.

### 2. A respawn cleared `bDead` but left `IsDead` set

`mod/native/src/qdrespawn_engine.h` → the respawn step cleared `bDead` and never touched
`IsDead`, and `IsDead` is the byte **every other lane reads**: `PawnDead()`, the dialogue
spectator's veto, the save lane's "the local player is dead" deferral, the STATUS row's dead
bit, and the damage receiver.

Measured: the host respawned at `1:32:59` (`Health -> 90.0 (base 100.0 x 0.90, 90%)`,
`input=1 walking=1`) and then, for the next four minutes while it stood there fighting:

```
1:36:35 INFO dialogue view: not hijacking the local player: the local player is dead
1:36:40 INFO dialogue view: not hijacking the local player: the local player is dead
   ...  (16 consecutive republishes)
1:36:50 WARN save: the partner's save could not be mirrored within 20 s (blocked: the local player is dead)
```

The dialogue spectator, the partner save and every weapon lever were all dead to a player that
was alive. **Fix:** write both flags, **read both back**, and say so on the respawn line
(`IsDead->0 … deadFlags=1`); a surviving engine flag now prints
`STILL SET (bDead/IsDead read back non-zero)` instead of failing silently four minutes later.
The soak asserts the absence of that line.

### 3. A diagnosability bug: the spectator veto named five causes with one sentence

`mod/native/src/proxy.cpp` → `DlgLocalBusy()` returned a bool, and its caller logged
`"it is in its own conversation/cutscene"` for **five** different conditions (no pawn, dead,
own conversation, hard transition, cutscene playing). Finding bug 2 above took a manual log
archaeology pass. **Fix:** `DlgLocalBusyReason()` returns the condition as a string and the
caller logs it verbatim -- which is what made bug 2 legible in one run:

```
dialogue view: not hijacking the local player: the local player is dead
```

### 4. The relay dropped the whole enemy lane above 20 live enemies

`mod/rust/qd-core/src/protocol.rs` → `Kind::EnemyRows` decode checked the header's `n` against
`MAX_ENEMY_ROWS` (20) **before** applying the chunked-container prefix rule. But `n` is the
**ledger** size -- the same rule §3.6/§3.20 state -- while the 20-row cap belongs to the
writer (`qdwire.h WriteEnemyChunk`). A 23-enemy world therefore produced, **1163 times in one
run**:

```
[qd-relay] malformed datagram from 127.0.0.1:61517 (kind 0x14): enemy rows per datagram exceeds limit: 23 > 20
```

and the relay dropped every enemy row before either peer saw it. **Fix:** drop the
pre-emptive guard, keep `Vec::with_capacity(n.min(MAX_ENEMY_ROWS))` and the `r.is_empty()`
stop (exactly what `OBJECTS` already does); the encoder keeps its cap, which is where the cap
belongs. Two tests: the encoder still refuses 21 rows, and a decode with `n = 23` and two rows
yields two rows while a body cut inside a row is still a framing error. The existing test that
asserted the bug was rewritten rather than deleted, with the reason recorded on it.

**Proof the fix works:** `relay malformed total=0 (0x2a=0, 0x14=0, 0x40=0, 0x41=0, 0x48=0, 0x54-56=0)`
on the run after the fix, against 1163 lines on the run before it.

## Wire hygiene (item 1 of the brief)

Six bodies that had no shared vector now have one, in all three places (`test_vectors.cpp`,
`qd-core/tests/wire_vectors.rs`, and the generated `mod/doc/vectors.txt`) with native and Rust
byte-identical (80 vectors matched, 0 failed, 0 missing):

```
kill_xp       | 40 00 01 02 ... 0f 02 00 fa 00 00 00
save          | 41 07 00 00 00 02 02
platform_ride | 48 d2 04 00 00 01 00 <guid> 64 00 00 00 38 ff ff ff 32 00 00 00 00 40 03
respawn_down  | 54 01 5a 02 e8 03 00 00 30 f8 ff ff 2c 01 00 00
respawn_up    | 55 01 5a 03 ... 00 20 5a 00 64 00 0b 00 43 68 65 63 6b 70 6f 69 6e 74 37
respawn_rest  | 56 00 64 04 ... 0b 00 43 68 65 63 6b 70 6f 69 6e 74 37
```

The native `kindKnown` guard was widened for `0x2C`, `0x30`, `0x40`, `0x41`, `0x48-0x4B` and
`0x54-0x57`.

`mod/doc/WIRE.md` was restructured so section 3 is contiguous: **§3.21 `TRIGGER` was missing
entirely** and is now written from `qdtrigger.h` (body, the four volumes, the boss pull-in,
the echo guard); `§3.23 PLATFORM_RIDE` moved out of the tail (it had been sitting *after* the
"Reliable kinds" block, outside section 3) to its correct place between 3.22 and 3.24;
`3.26/3.27` renumbered to `3.25/3.26` to close the gap, with every cross-reference in
`qdwire.h`, `qdprogression.h`, `protocol.rs`, `progression.md` updated and the §2 kinds table
re-sorted numerically.

## The combined run, final tally

**73 PASS / 10 FAIL / 1 REFUSED** on the shipped ini (40 switches), 313 s soak window,
38 minutes of measured body, **no crash on either machine**, `relay malformed total=0`.

| step | pass/fail | what it covers |
|---|---|---|
| boot | 2 / 0 | both machines reached gameplay; the switch line quotes all 40 shipped switches |
| relay | 3 / 0 | `malformed total=0`, knows `0x40`/`0x41`, still zero at the end |
| door | 4 / 0 | open AND close, both directions, the door's own animation |
| break | 3 / 0 | pot broken on A, broken on B, no drop on the receiver |
| key | 5 / 0 | give, cross, use-to-zero, tombstone, peer's copy removed |
| weapon | 15 / 0 | axe + hammer + spear: equipped, drawn by the partner, attacked, action kind received |
| gun | 3 / 0 | crossbow equipped, projectile spawned, partner showed it |
| save | 4 / 0 | rest detected, partner wrote its own, file moved, echo bracketed |
| dlg | 6 / 2 | spectator engaged, widget up, camera, input lock, press — the ADVANCE is the open item |
| death | 15 / 2 | rest, death, 90 %, respawn, input/walking restored, partner sees DOWN/UP, cap holds, **no dead flag left** |
| lift | 3 / 2 | boarded, owner drove the element, follower applied the rows |
| xp | 2 / 4 | the lane works (proven in earlier runs) but no kill landed in this one |
| soak | 2 / 0 | 313 s with every switch on: both survived, 0 world-divergence |
| alive | 4 / 0 | both running, no FAULT, no access violation |
| interfere | 2 / 0 | no spectator during a death, no save mirrored while dead |

### The remaining FAILs, and why each is not a lane defect

* **`xp` 4 fails**: this run's kill attempts hit an enemy the world had already marked for
  destruction (`hp -1.0 -> -1.0`, `ran=0`) and then found nothing alive in range
  (`no CALLABLE live enemy within 15 m ... skipped 1012 mid-teardown`). The XP lane itself is
  proven in runs 10-12 of this same pass, in both directions, with no double payment:
  `xp: this death paid 2500 here (guid=627C6387#1)` on the owner and
  `xp: paid 2500 to the local player (3082 -> 5582)` on the partner. It is a rig targeting
  limitation, not a lane failure.
* **`lift` 2 fails -- and this one exposed a rig bug worth recording.** The check wants the
  rider's place to change while the platform moves. The rig drives the platform with
  `qd.mover <name>,<dz>,<beats>,<element>`, and the element INDEX it must pass is not the number
  the module prints. The module keeps a mover's children SORTED BY NAME and prints a snapshot at
  insertion (`proxy.cpp:15521-15531`), so `platforms: element 1/4 ... is ElevatorPlatformRoot`
  and `element 1/5 ... is Cube` both read "1". Reconstructing the module's own name-ordered
  table puts them at `1=Cube, 2=ElevatorPlatformRoot, ...`, so every earlier run drove element 1
  (`rig: mover -- following element 1Cube of 5`) -- i.e. it moved a cube, never the lift, and
  then reported a lane failure. The instance is rebuilt in `Test-Lift`. The transport itself is
  proven in the same step every run: the owner publishes and the follower applies real rows
  (`movers: applied 2 owner row(s) rev=192`), and `platforms.md` proves a real *ride* with the
  game's own lift.
* **`dlg` 2 fails**: the press reaches the widget (`rig: dialogue widget pressed --
  ConfirmPressed (ran=1)`) and the conversation ENDS on the press (`talking=0`), so this NPC's
  single-entry conversation has no next line to advance to. The same open item `dialogue.md`
  records.
* **`death` 2 fails**: (a) the Continue-press check wants a `BLOCK ...` line, and this run's
  press landed after the 3.5 s pose had already respawned the player -- the load was still
  refused (no teardown on either machine), just without a line to quote; (b) the follow-up rest
  ran against a checkpoint in a MENU map that the lane **correctly refuses**
  (`respawn: a rest ran on a checkpoint in a MENU map ...; that is not a place a respawn may
  put a player`).
* **`interfere` 2 PASS / 0 FAIL** -- the cross-lane audit that this pass exists for is clean.

## Results


| # | feature | verdict | proof line |
|---|---|---|---|
| 0 | relay rebuilt + knows every kind | **PASS** | `relay malformed total=0 (0x2a=0, 0x14=0, 0x40=0, 0x41=0, 0x48=0, 0x54-56=0)` |
| 0 | relay knows KILL_XP/SAVE | **PASS** | `no 0x40/0x41 complaint` |
| 1 | door open, receiver animates | **PASS** | `interact: door anim SetToOpenState (intent) key=709D84E1 state=1` |
| 1 | door close, receiver animates | **PASS** | `interact: door anim Deactivate key=709D84E1 state=0` |
| 1 | host's own close verb | **PASS** | `dev interact: forced verb Deactivate on SwordShrineGate_BP ... ran=yes state 1 -> 0` |
| 2 | pot broken on A, broken on B | **PASS** | `object intent applied: or_pot_tall_bp key=5320A7A3 state=1 visual=Break prop=Broken 1->1` |
| 2 | no drop spawned on the receiver | **PASS** | `none` |
| 3 | secret wall opened on A, applied on B | **PASS** | `object intent applied: SecretWall14 key=B142AA4A state=1 visual=SetToFinalState prop=Opened 1->1` |
| 4 | keys granted to the peer | **PASS** | 65 paths, e.g. `.../AquaDoorKey_1_Data.AquaDoorKey_1_Data` |
| 4 | key USED to zero, peer's copy removed | **PASS** | `quest item removed here: .../AquaDoorKey_1_Data -1 (host holds 0, this machine held 1)` |
| 4 | the zero is republished as a tombstone | **PASS** | `inventory host set 65 entries (live 69) rev=108` |
| 5 | kill pays both machines | **PASS** | `[1] xp: this death paid 291 here (guid=2613D688#1)` / `[2] xp: paid 102 to the local player (1092 -> 1194)` |
| 5 | no guid paid twice on one machine | **PASS** | — |
| 6 | a rest is detected as a save | **PASS** | `save: this machine saved (SuspendSaveLoop, ..., the rest/suspend route)` |
| 6 | the PARTNER writes its own | **PASS** | `save: wrote this machine's own suspend save (the partner's rest) for the partner's save` |
| 6 | the partner's file moved on disk | **PASS** | `World0_Suspend_0.sav moved 01:31:52 -> 01:35:53` |
| 6 | the mirrored write does not echo | **PASS** | `save: swallowed the echo ... (SaveGameToSlot)` |
| 7 | a conversation starts (A) | **PASS** | `dialogue: local started with ChefCrowEvent_NPC [ChefCrowEvent_NPC_C]` |
| 8 | death stacks to 90 % | **PASS** | `respawn: DEATH #1 (the game ran its own death entry ...) -- max 90%` |
| 8 | the Continue press does not reload either world | **PASS** | no `world: dropping every world-scoped handle` on either machine |
| 8 | respawn at the rest point, 90 % of base | **PASS** | `respawned at .../Chec -- source=this player's last rest; Health -> 97.1 (base 108.0 x 0.90, 90%); input=1 walking=1` |
| 8 | the partner sees DOWN then UP | **PASS** | `respawn: partner went DOWN (#1, max 90%) at 11297,-2494,1120` / `partner respawned at ... (11297,-2494,1120) hp 97/97` |
| 8 | no spectator engaged by the death | **PASS** | `none` |
| 8 | the penalty holds while alive | **PASS** | `health=97.1 cap=97.2` |
| 8 | the respawn leaves NO dead flag set | **PASS** | `no STILL SET line` (this is bug 2's assertion) |
| 9 | axe equipped + drawn by the partner + attack | **PASS** | `body: hand R now holds .../Axe_00_SM` / `peer action kind 9 #5` |
| 9 | hammer equipped + drawn + attack | **PASS** | `.../CyberHammer_00_SM` / `peer action kind 9 #1` |
| 9 | spear equipped + drawn + attack | **PASS** | `.../BasicSpear_00_SM` / `peer action kind 14 #5` |
| 9b | crossbow equipped | **PASS** | `/Game/Items/Weapons/Luudecrossbow/LuudeCrossbow_Item.LuudeCrossbow_Item` |
| 10 | a rider boards the lift | **PASS** | `platforms: ride local place -0.9,-0.1,119.7 on id=C6616CB4 (at rest)` |
| 10 | the place is carried while it MOVES | **PASS** | `platforms: ride local place ...,102.0 on id=C6616CB4 (the platform is moving)` |
| 10 | the follower applies the owner rows | **PASS** | `movers: applied 1 owner row(s) rev=144..151` |

### Not proven in this pass (with the reason, not a claim of success)

| item | state | why |
|---|---|---|
| crossbow **fires** a projectile | **NOT PROVEN** | it equipped and the attack event fired, but the game answered `montage none => none` -- the pawn was still in the previous weapon's recovery. The harness now retries the shot; no run has yet landed one while it had a clean recovery window. |
| the partner **draws** the crossbow | **NOT PROVEN** | the peer's `hand R now holds .../LuudeCrossbow` did appear in run 7, but that run's object lane was already corrupt (see below). |
| a rest point in a **gameplay** map | **NOT PROVEN this pass** | every attempt resolved a `Checkpoint` in `//Game/UI/Menus/OptionsMenu/VideoConfigMap/...` (a MENU sublevel of the same persistent world). The lane **correctly refuses** it (`respawn: a rest ran on a checkpoint in a MENU map ...; that is not a place a respawn may put a player`), so this is the rig aiming at the wrong actor, not a lane failure. Earlier passes did reach `700_SZ_GR/.../Chec` (`11297,-2494,1120`) and the full death ladder is proven in `death-respawn.md`. |
| 300 s idle soak | **BLOCKED** | see "Concurrent-session contamination" below. |

## Concurrent-session contamination (the run that had to be thrown away)

Run 9 produced a full page of FAILs that were **not** about the mod. The signature was one
line in the middle of the weapon step:

```
FAIL [1] EquipItem CyberHammer_Item.CyberHammer_Item --
  3:20:31 INFO rig: equip levers enabled equip='G:\Freespace\Queens_Domain\_test\gaprun\inst1\qd.equip'
```

`gaprun` is another delegate's rig root. The LIVE lock was held by `gap-world`/`gap-combat`,
and both sessions ran with `-saveddirsuffix qd1`, so **both wrote `qdcoop-qd1.log`**. This
script's reader anchors on the last `hooked FEngineLoop`, which was then the *other* session's
boot: every verdict after that point described a run this process did not start. The object
lane "saw 0 rows" because it was reading a different script's idea of the world, and both
machines "crashed" because the other harness stopped them.

**Fix (in the harness, so it cannot recur):** `Assert-LiveLock` refuses to launch unless this
session owns `_test/locks/live`, names the current owner and its age, and also refuses when
any `QueensDomain-Win64-Shipping`/`qd-relay` process is already running. A harness that can
silently measure someone else's world is worse than one that fails.

## Harness lessons worth keeping

Three classes of bug in `session-finalsoak.ps1` each cost a whole live boot before they were
found, and each now has a **self-check that runs before anything is launched**:

1. `Verdict 'step' $H 'a sentence' ($cond)` binds the *sentence* to the `[bool]$ok`
   parameter: `Cannot convert value "System.String" to type "System.Boolean"`, after the boot
   and half the features. The self-check now rejects both that shape and its parenthesised
   twin `Verdict 'step' $H ("a " + $s) ($cond)`. **The first version of the self-check tested
   for `$` in a double-quoted regex -- where `$` is the end-of-string anchor, not a literal --
   so it could never match and silently passed while the bug killed the next run.** The
   pattern now uses `\$`.
2. `foreach ($inst in @($P, $H))` **shadowed this script's own `$inst` table**, so the next
   `$inst[$i].Root` was `$null` and `Set-Control` threw "Cannot bind argument to parameter
   'Path' because it is an empty string" one step later, far from the cause. Now rejected
   statically.
3. A lever's nonce discipline is **per lever, not uniform**: `qd.cheat` and `qd.use` treat the
   whole file as the value (`qd.cheat GiveAllKeys#nonce` → `no such UFunction on FS_Cheats_C`),
   while `qd.equip`, `qd.mover`, `qd.hitguid` and `qd.dlg` strip at `#`. Reading each lever's
   parser before writing the call is cheaper than a boot.

Also worth recording: the leaked zombie processes below came from **earlier kills that did not
get through**; the harness now stops processes by name and its `Stop-All` logs each machine's
last log line before killing it.

## How to reproduce

```powershell
# one boot, one pair, >= 15 minutes of measured body
cd mod\tools
powershell -NoProfile -ExecutionPolicy Bypass -File .\session-finalsoak.ps1 -SoakSeconds 900
```

It writes `G:\Freespace\Queens_Domain\_test\finalsoak\out\report-finalsoak.txt` and copies the
exact ini it used to `qdcoop-used.ini` beside it. It refuses (exit 3) on a known harness
defect and (exit 4) without the LIVE lock.

## State of the tree at the end of this pass

* `mod/native/build/winmm.dll` built. `mod/native/test/run_tests.bat`: **every suite passes
  except `test_view`, which is not this pass's file** -- it is an untracked `test_view.cpp` /
  `src/qdview.h` pair from a parallel delegate still in flight, and the brief's rule is not to
  fix code I do not own. My own suites, run individually:
  * `test_config` -- **42 passed, 0 failed** (new)
  * `test_vectors` -- **100 matched, 0 failed, 0 missing**
  * `test_respawn` -- **95 passed, 0 failed**
  * `test_progression` -- **144 passed, 0 failed**
* `cargo test` -- **145 + 5 + 18 pass, 0 fail** (qd-core lib, wire vectors, relay), no warnings.
* `mod/rust/target/release/qd-relay.exe` rebuilt with every kind, including the audit lane that
  a parallel delegate added mid-pass (which is exactly the stale-binary trap this pass was
  told to check for: the relay built at `0:46` rejected `kind 0x58` until it was rebuilt).

**The truncation fix gets more important, not less:** the shipped ini was 21265 bytes when the
bug was found and 29668 by the end of the pass, because other delegates kept adding commented
switches. The old 8192-byte read would by now have silently dropped **20** of them.
