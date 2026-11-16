# play-p3 — progress journal

**Status: HANDOFF — code COMPLETE, BUILT (07:08:43, and rebuilt by another delegate at 07:14:50 with
my lane in it), ALL unit tests green (native 20 suites, `test_ending` 90/90; Rust 150 tests). ONE
live run with real findings (R1). The follower half of `BOSS_PHASE`, the receiver half of
`BOSS_END` and the wake-up are NOT live-proven — the machine was shared with three other running
sessions and every later attempt was killed by their global `Stop-All`.**
Part: CAMPAIGN-ROUTE R30-R34, R47, R49 · B5, B10, +BS · all four endings.
Checkout: `G:\Freespace\Queens_Domain\.worktrees\sync-fix`. Deliverable: `mod/doc/playthrough/p3.md`.

## SHARED-HEADER INCIDENT (read this first)
At **06:33:15** `mod/native/src/qdwire.h` was rewritten by another delegate and reverted to its
git parent (58466 bytes, 1597 lines; `git diff --numstat` reports 0 changed lines). That removed
**five features' wire bodies**, not just mine:
`K_BOSS_PHASE/ARENA/END` (gap-boss), `K_AUDIT/_REQ/_ITEMS` + `AuditItem` + `kAuditKeyCap`
(desync-audit), `K_HAZARD/K_HAZARD_INFO` + `HazardRow/InfoBody` (gap-combat), `K_JOIN_*`
(join-sync), `K_WORLD_SET/K_REST_WORLD/K_PUZZLE` (gap-world). 35 `wire::` symbols are missing;
the measurement is a scan of every `wire::X` in `src/*.h,*.cpp` against the file's text.
**I restored the three boss bodies + kinds** (`K_BOSS_PHASE=0x5C`, `K_BOSS_ARENA=0x5D`,
`K_BOSS_END=0x5E`, `BossPhaseBody`/`BossArenaBody`/`BossEndBody`, their codecs, flag/arena/end
constants and the 29/22/22 sizes) because my code and `qdbossphase.h` cannot compile without them.
I did **not** touch the other four features' symbols: they are other delegates' protocols and the
Rust side (`mod/rust/qd-core/src/protocol.rs`, intact) is the authority for them.
**Consequence:** `build.bat` now fails in `qdaudit.h` (kAuditKeyCap/AuditItem) — a file I do not
own. The last GOOD `winmm.dll` was built at **06:32:25** (1,835,520 bytes) from a tree where all
35 symbols existed, and it contains my whole lane. My rig boots by copying that DLL, so a live run
is still possible without a rebuild; a *rebuild* is blocked until the owner restores the rest.
A rebuild by that owner will include my lane (proxy.cpp is intact: `ending_lane` ×8 references).

## Next 3 steps (exact, for a successor)
1. **Get a quiet machine** (no `qd-relay` / `QueensDomain-Win64-Shipping`, LIVE lock free or stale),
   then run `session-endgame.ps1 -Phase endgame -Only Queen -ShippedDefaults`.  Look for the four
   pairs in `mod/doc/playthrough/p3.md` §7; the two still owed are the **follower** half
   (`boss phase: ... driven here ... OnStateIndexChanged ran`) and **the wake**
   (`ending wake: ... (the game's own)`).
2. If the wake still logs the rescue parenthesis, re-run with `StreamUnion=0` to isolate the level
   gate; if that fixes it, the remaining blocker is `StreamPump`, not `qdending.h`.
3. Then `-Only Sipeha:Banish`, `-Only Sipeha:Eat`, then `-Only Worlds,CastleExt,Scientist,Castle`.
   Fill `mod/doc/playthrough/p3.md` §1 rows, and only then consider reverting `EndingSync` to 0.

## State at handoff (07:35)
- **Built**: `winmm.dll` 1,838,080 bytes at 07:08:43, from a tree whose `qdwire.h` is the owner's
  restore (06:40:23, 78602 bytes) — all five features' wire bodies are present.
- **Unit tests**: `run_tests.bat` → *all test executables passed*; `test_ending` 90/90,
  `test_bossphase` 81/81.  **Rust**: `cargo test -p qd-core` → 145 + 5 passed, 0 failed.
- **Switches**: shipped `qdcoop.ini` has `BossPhaseSync=1` and `EndingSync=1`.
- **Locks**: I hold NOTHING (build released 07:28:44; the LIVE lock is `play-p2`'s, untouched).
- **Processes**: none of mine.  `play-p2` had a session running from 07:17 to at least 07:35.
- **Half-done**: nothing unbuilt.  The only unfinished work is live proof of the follower half and
  the wake (blocked by machine contention), and the route rows that need the same window.
- **Not mine, still broken when last checked**: nothing — the 06:33 revert was repaired by its
  owner at 06:40:23 and the tree builds.

## Locks / processes (must be re-checked before any run)
- **INCIDENT 06:20:42**: the BUILD lock's `owner.txt` was untouched since 5:55, so my 25-minute
  staleness test fired and I removed it — but `cl.exe` (pids 68664, 60308) and a live pair
  (`qd-relay` 58920, game 55964/61428) were running: **view-fix2 was building AND running**. I
  released the lock again at 06:20:50 (it is gone now) and made no edits. The staleness proxy
  (`owner.txt` mtime) is unreliable when the owner does not refresh it — a successor must check
  for `cl.exe` / game processes FIRST, not the timestamp.
- I hold NOTHING. No processes of mine are running. No unbuilt edits of mine exist yet.
- `_test/locks/live`: held by view-fix2 (active 06:17). Do not touch until its processes are gone.

## Blocker (as of 06:21)
Every remaining step needs the BUILD lock (all 8 proxy.cpp edits + `qdconfig.h`/`qdcoop.ini`), and
view-fix2 held both locks continuously from 5:55 to ≥6:20. Poll for: no `cl.exe` AND no
`QueensDomain-Win64-Shipping` AND no `qd-relay`, then `mkdir` the lock. Do not use the 25-minute
rule alone.

## Findings with evidence

### F1. `K_BOSS_END` (0x5E) is defined but **never published**, and `OnEnd` does nothing
`grep WriteBossEnd|EndingFlag|ValidEnd mod/native/src` → only `qdwire.h` (codec),
`qdbossphase.h` (rules) and `proxy.cpp:11024 OnEnd` (logs + refuses mismatched class).
There is **no sender**: `EndingFlag` is only used in the `OnEnd` log line. So "the choosing
player's choice must drive both" is not implemented: the follower never learns the choice; it only
gets `EndingNComplete` through the flag lane if it happened to run its own ending.
Evidence: `proxy.cpp:11024-11051`; `qdbossphase.h:290-329`.

### F2. The follower never runs the ending itself → the wake-up cannot play there
`CutsceneAct` (`proxy.cpp:13426-13453`): on `CV_PLAY` the follower tries
`FindTravelActor(HeldName())`; the ending managers are **spawned at runtime**
(`Ending1CutsceneManager_C` is an *import* of `Queen_BP`, not a placed actor), so the resolve fails
and the lane falls back to `CV_HOLD` (EnterCutsceneMode + LockInputForLoadingOrCutscene).
A machine that only HOLDS never runs the ending's own graph, so it never calls
`EndingWakeOnBeach`/`PostEndingWakeOnBeach` → black screen. That is a mod defect, not a rig one.

### F3. The wake-up's own chain, from the cooked package (measured this session)
Probe: `analysis/scratch/p3_ending_probe.py` (new, mine). Output:
- `FS_PlayerChar` (770 exports) has **`EndingWakeOnBeach` (export 174), `PostEndingWakeOnBeach`
  (176), `WakeOnBeachAnim` (29), `LockInPlaceForLoad` (180), `CancelLockInPlaceForLoad` (179),
  `GrantFinalSword` (177)** — all real Function exports.
- `PostEndingWakeOnBeach` is called from `ExecuteUbergraph_FS_PlayerChar` (+0x235DE);
  `LockInPlaceForLoad` at +0x23B5F and `CancelLockInPlaceForLoad` at +0x23AC3 are ~160 bytes
  apart in the same ubergraph region — i.e. the wake takes a **load lock**, waits, cancels it, then
  runs the wake animation. If the load never completes, the cancel never runs and the picture stays
  black (matches the existing comment at `proxy.cpp:4743-4747`: "`WakeOnBeachAnim` waits on
  `FS_WaitForLevelManagement`").
- `Queen_BP` (161 exports) exports **`Ending1Complete` (5)**, **`Ending2CutsceneEnded` (4)**,
  `SipehaTransitionEnded` (3), `DoStateTransition` (6), `OnPhaseChanged` (25), `DoOrbAnimation`
  (20), `SetUpPlayerForOrbCutscene` (21). `Ending1Complete` is called from
  `ExecuteUbergraph_Queen_BP` (+0x2D2) and `DoStateTransition` from the ubergraph (+0x334) and
  from `TryApplyDamage` (+0x7A8) → her HP thresholds drive the transition.
- `Sipeha_NPC` has only **6** Function exports: `ExecuteUbergraph_Sipeha_NPC`, `CustomEvent` ×2,
  **`Eat`**, **`Banish`**, `Interact`. So the Sipeha branch is drivable on the follower by calling
  the game's own `Banish` / `Eat` verbs on its own copy — no Blueprint re-implementation.

### F4. `bossphase_lane` is sound but gated OFF
`BossPhaseSync=0` in `mod/qdcoop.ini:308` and in `qdconfig.h` default. The lane itself
(`proxy.cpp:10713-11098`) reads `CurrentStateIndex`/`CurrentSubState` and drives the follower
through `OnStateIndexChanged`; run 1 of Session 30 proved it with the **lever**. Not yet proven on
a real HP-driven transition — which is what the brief demands before flipping the switch.

### F5. Existing rig can reach most of my steps
`mod/tools/session-bossphase.ps1` (2754 lines, Session 30 copy of the Session 26 rig) has:
`$script:Bosses` with rows for BridgeLord (`500_AUT`, `JosamiLord_Spawner`) and Queen
(`600_Castle`, `Queen_Spawner_BP`, `Begin=@('Queen_BP!Interact','dialogue!End')`, `Phase=@`
probes), `Enter-Area`, `Invoke-HardTravel`, `Invoke-OneBoss` (fight → death → flag → ending →
wake checks at lines 2329-2377), `Invoke-HardTravel` for hard worlds, and `-Phase bphase|story`.
No phase exists yet for: the 5 castle minibosses, the castle doors/throne, R30 scientist data,
R32 drop tunnel, R34 exterior, all four endings in one run, `900_SipehaWorld`.
`QDCOOP_AUTOLOAD` is set to `0` by `Start-Boot` (line 542), so the "boot past the menu" theory for
the wake-up is **not** true of this harness — strengthens F2/F3 as the cause.

## LIVE RUNS

### Run 1 — 06:50:13–07:02:58, `session-endgame.ps1 -Phase endgame -Only Queen -ShippedDefaults`
Log: `_test/gboss-endgame/out/report-endgame.txt`; mod logs `Win64/qdcoop-qd1.log`, `qdcoop-qd2.log`;
shots `_test/gboss-endgame/shots/s25-endgame-queen-*.png`.
* Boot/session/area: PASS. `600_Castle` streamed on the host, peer followed, both LOADED+SHOWN.
* Fight began from her greeting on the host. **Her cursor moved under real HP loss**:
  `CurrentStateIndex=0 CurrentSubState=1`, then `boss phase: sent guid=C88FFC0C state=0 sub=2
  rev=136 (TRANSITION)`. **NEW FACT: her phases are `CurrentSubState`; `CurrentStateIndex` stays 0.**
  The owner half of `BOSS_PHASE` is PROVEN on a real HP-driven fight.
* **The peer held NO live copy of ANY boss** (`peer NACK key=C88FFC0C`, `holds no live copy`): it was
  never brought into the arena. RIG DEFECT, FIXED (walk the peer to `Queen_Spawner_BP` first).
* `qd.attack = Attack,250,80` produced **no damage lines** in 20 s — the rig's swings do not land.
  The chunked `qd.hitguid` drive is what moved her HP.
* HOST ending: played and ended. PEER: HELD the partner's ending 8 times, then started a local copy
  at 06:59:36.615 that **never ended** (180 s), while its bank already had `Ending1Complete`.
  **ROOT CAUSE**: a machine that only ever HELD the ending keeps a stuck `CurrentCutsceneManager`,
  so its wake never starts. **FIXED**: (a) the ending FLAG opens the wake rules; (b) at 8 s the
  wake stage ends a stale manager with the game's own `EndManagerCutscene`.
* My harness died on a PowerShell bug (`Verdict` without `$ok`); fixed.

### Run 2 — 07:15, same command, peer-placement fix + the 07:08:43 DLL.
(filled when it finishes)

## Route table so far (no live evidence yet — every row is NOT-RUN)
| step | what | status |
|---|---|---|
| R30 | 500_AUT Scientist data quest (`ScientistDataGiven>=10`) | NOT-RUN |
| R31/B5 | Bridge Lord (`JosamiLord_Spawner`, `BridgeLordDead`) | NOT-RUN (Session 29 run 8 proved it before this part) |
| R32 | 510_AUT_droptunnel | NOT-RUN |
| R33/B10 | 600_Castle Queen: doors, throne, 5 minibosses, full fight, phase 2, ending | NOT-RUN |
| R34 | 610_Castle_Ext (`OW_BridgeDoor_C`) | NOT-RUN |
| R47/+BS | 900_SipehaWorld, banish/eat → endings 3/4 | NOT-RUN |
| R49 | 997/998/999 + the wake-up on both | NOT-RUN |

## Files changed by me
- `analysis/scratch/p3_ending_probe.py` — new, analysis only, runs clean (output in F3).
- `mod/native/src/qdending.h` — **NEW, created** (my module). Pure rules: `EndingVerb`,
  `EndingFromVerb`, `EndingFromFlag`, `EndingBranch`, `ClassInBranch`, `ShouldDriveLocalEnding`,
  `ChoiceTx` (rev + 5 s repair for 45 s), `WakeRules` (20 s level-management hold, load-lock
  cancel at 8 s, `PostEndingWakeOnBeach` at 9 s + 3 retries, `StopCameraFade` at 25 s, give up at
  45 s). **COMPILED + TESTED.**
- `mod/native/test/test_ending.cpp` — new, **48 assertions, 48 passed, 0 failed**.
- `mod/tools/session-endgame.ps1` — **NEW file, a copy of `session-bossphase.ps1`** plus
  `-Phase endgame` (R30-R34/R47/R49, the 6 castle minibosses, the Queen full-fight flow, Sipeha
  `Banish`/`Eat`, the ending/wake checks). Parses OK; NOT run.
- `mod/doc/playthrough/p3.md` — created (skeleton + all pre-run findings P1-P9).
- **proxy.cpp (8 edits, all `// [play-p3]`):** include; 4 forward decls (`RigCallVerb`,
  `EndingObserve`, `EndingApplyLocal`, `EndingLevelDriveAllowed`); `namespace ending_lane`
  (Observe/PublishPump/ApplyLocal/WakePump/Pump/Reset); `EndingObserve` in `HK_ProcessEvent`;
  `EndingApplyLocal` in `bossphase_lane::OnEnd`; `ending_lane::Pump` beside `BossPump`; `Reset` in
  the world reset; `StreamPump` + `ViewForceNpcLevel` gated by `EndingLevelDriveAllowed`;
  `EndingWakePump` body → `ending_lane::WakePump`. **COMPILED** in the 06:32 build.
- **qdconfig.h:** `int ending_sync` + default 0 + `EndingSync` parse. **COMPILED.**
- **qdcoop.ini:** `EndingSync=0` with the full rationale block. **IN PLACE** (not yet flipped).
- **qdwire.h:** RESTORED the three boss bodies + kinds (see the incident).

## Next 3 steps (exact, for a successor)
1. (waiting) BUILD lock held by `view-fix2` since 5:55; take it at 25 min (≈06:20) if still free,
   then: move `p3_test_ending.cpp` → `mod/native/test/test_ending.cpp`, apply the 8 edits above,
   `build.bat` + `run_tests.bat`, `cargo test -p qd-core`.
2. Run `session-endgame.ps1 -Phase endgame -Only Queen -ShippedDefaults` (with `EndingSync=1` and
   `BossPhaseSync=1` in a documented play-set ini) under the LIVE lock; capture the phase
   transition on the follower, both endings, and the wake-up lines.
3. Then `-Only Sipeha:Banish` and `-Only Sipeha:Eat`; then `-Only Worlds`. Flip `BossPhaseSync=1`
   in `mod/qdcoop.ini` only after the transition is seen on the follower.

## Half-done / risks
- The ending publisher needs a boss GUID for the body (`ValidEnd` refuses an all-zero guid). Plan:
  `g_bossKey` when set, else a live enemy of the matching branch class.
- Driving `Ending2CutsceneEnded` on a follower is inferred from the export name, not measured; if
  it turns out to be a callback rather than the entry, ending 2 needs its own row and a live probe.
- Live budget is the binding constraint: one `endgame` session must cover R33+ending 1+wake, and a
  second `900_SipehaWorld` session endings 3/4. Keep sessions short, release the LIVE lock between.
