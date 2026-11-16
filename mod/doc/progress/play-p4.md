# play-p4 — progress journal

**STATUS (08:20): one LIVE run done (R35, slot 08:00:06-08:04:10). 14 pass / 7 fail; two real
sync bugs (BUG-1 secret-zone door carried by no lane, BUG-2 enemy set 477 vs 580) and one
auditor divergence (BUG-3 key-item digest) recorded with exact lines. R36-R46 rig-ready, not
run (lock contention + my remaining budget). No shared source file edited; no build of my own;
no lock held; no processes left running.**

Deliverable written: `mod/doc/playthrough/p4.md` (full results, the three bugs with log lines,
the pak census, prior-evidence table, next steps).

## LIVE RUN 1 — 2026-11-16 08:00
* Command: `powershell -NoProfile -ExecutionPolicy Bypass -File mod\tools\session_p4.ps1 -Part all -Only R35`
* Report: `_test/p4run/out/report-p4-all.txt` (11,327 B)
* Archived logs: `_test/p4run/out/run-0800-qd{1,2}.log` (my run; copied before another part rotated them)
* Shots: `_test/playthrough/play-p4/p4-all-R35-arrive-inst{1,2}.png`, `…-R35-after-inst{1,2}.png`
* Switches actually used (the tree's shipped ini at 06:49, NOT changed by the rig):
  `BossPhaseSync=1 WorldSetSync=1 PuzzleSync=0 EnemySync=1 DesyncAudit=1`
* Roles: inst1=HOST, inst2=PEER. Relay `malformed datagram=0, unknown kind=0`.
* PASS: area follow into 700_SZ_GR on both (`level engine state ... LoadedLevel=1
  actuallyVisible=1`; peer `travel: entered the partner's area 700_SZ_GR by streaming it`);
  `SZ_Door_02` host→peer (`actor states applied=5 … rev=91`, peer
  `interact: door anim SetToOpenState key=844532DB state=1`); rune `SipehaRune_GR_SZ`
  (`actor-state intent applied` + `refresh ok …SipehaRune`); `SecretWall25`
  (`object intent applied … visual=SetToFinalState`); vault relic `SR_GMS_Acquired=1` in BOTH
  banks; skull relic key grant on the host.
* FAIL → the three findings, all OPEN, all outside my modules:
  1. **BUG-1 `SecretZoneDoor_Blue_BP_C` is carried by NO lane.** Peer `forced verb Activate …
     ran=yes state 0 -> 0`; `dev state: … savable=no guid=- index=-1`; `Opened=0` on both; no
     `object sync: tracking … SecretZoneDoor_Blue_BP` line in 95 tracked objects. Its real path
     is `InsertKey` (package carries `InsertKey`/`OpenDoor`/`VisualKeyComponent`). This is the
     entry door to every secret zone and it is NOT synced. Owner: `qdobjectstate.h`/`qdinteract.h`
     (publish a GUID-less `SavableActor_C` subclass on the object lane; skip must be
     instance-based). Confirming experiment for the next slot: `qd.cheat GiveAllKeys` then
     `near:SecretZoneDoor_Blue_BP!InsertKey`, read `Opened`.
  2. **BUG-2 enemy set diverges on area entry**: `audit: DESYNC enemy <whole set> host=477
     peer=580`, `enemy has 179 differing items`, named `5accfe19935cf31152c3030e4739abfd
     host=-1 peer=1`, still `DESYNC OPEN … enemy` at the end of the run. Owner: enemy lanes.
  3. **BUG-3 key-item digest**: peer picks the relic (`item intent sent … rev=117`,
     `inventory grant applied … x1` on the host, peer `inventory body rev=118 n=1`) yet the peer's
     own digest says `DESYNC quest …SkullKeyRelic_Data host=1 peer=-1` for the rest of the run.
     Owner: progression/inventory (`qdinventory.h`, `g_invNow` used by `CAT_QUEST`).

## EXACT NEXT STEPS (successor)
1. Take the LIVE lock per the strict rule, then run the remaining steps one part at a time:
   `-Part secrets -Only R36,R37` → `-Part prison` (R38/B6) → `-Part swamp` (R39,R43,R42/B7) →
   `-Part final` (R41/B8, R44/B9, R45, R46). Reports land in `_test/p4run/out/`.
2. Before R35 is re-run, add the `GiveAllKeys` + `!InsertKey` attempt for BUG-1 (one block in the
   R35 section) so the "is it the verb or the lane" question is answered in one shot.
3. When a run finishes, the rig archives its own logs; keep those files.
4. My rig has no remaining unbuilt edits and no lock; nothing to clean up.

## Rig (final state, parse-checked)
`mod/tools/session_p4.ps1` — parts `secrets|prison|swamp|final|all`, `-Only R35,R44` selection,
strict-lock compliant (`Acquire-Live` with the >25 min + zero-process staleness test,
`Get-ForeignLive` refusal, `Stop-All` kills only its own pids, `Touch-Lock`/`Rest` heartbeat),
archives logs, writes the report on abort. God mode is never enabled (the lever latches on first
read and even `0` turns on a 400 HP floor), so boss fights are real.

## Per-step table
| step | status |
|---|---|
| R35 700_SZ_GR | **RAN: 14 pass / 7 fail** — doors(reverse), rune, wall, vault relic, key grant PASS; SZ door FAIL (BUG-1); audit FAIL (BUG-2/3) |
| R36 710/720 | NOT RUN (rig ready) |
| R37 730_HOD | NOT RUN (rig ready) |
| R38 740_SZ_Prison + B6 | NOT RUN; prior proof Session 26 runs 14/16 (`RedLordsDead=1` both banks, `DBF4FFD3`/`17E153F4`) |
| R39 750_SZ_MAUS2 | NOT RUN |
| R40 760_Dekanima | NOT RUN — no route flag of its own (5,894 exports, 3 `SkullKeyRelic_BP_C`) |
| R41 760_SZ_AUT + B8 | NOT RUN; prior trigger+spawn proof LIVE-TEST-LOG L5844; puzzle half is flag-only (p2) |
| R42 770_SZ_Swamp + B7 | NOT RUN; prior Session 29 runs 6/9 (`51BC2B8B`, `GiantSwampFish_IsDead=1`) |
| R43 775_JigekiTemple | NOT RUN; route correction: no JigekiKeys pickup in this map |
| R44 780_SZ_Depths + B9 | NOT RUN; prior Session 26 runs 10/13/14 (`17181166`, `DragonLordDead=1`) |
| R45 790_SG | NOT RUN; the SZ whale placement has never been driven by anyone |
| R46 800_FinalZone | NOT RUN |

## Earlier findings (still valid)
* Route-vs-pak corrections: 720's `SecretZoneDoor_BP2` is an `SZFrame_NoDoor_BP_C` (a frame);
  775 has no `JigekiKeys` pickup (the key is `JIgeki_Key_2_BP_C` in 770); `730_HOD` and
  `760_Dekanima` have no flag of their own.
* Class→lane table (from the cooked packages) is in `mod/doc/playthrough/p4.md` section 1.
  Notably `StoneGardenPuzzleScript_C` is a `SavableActor_C`, so the Stone Garden puzzle's own
  state IS a GUID entry the actor lane can carry (unlike the decor `Lib_Book_*` props).
* p2's measured puzzle result (prior evidence): the decor books have no runnable verb; the
  receptacle's `Interact` is the player path and its state stays `0 -> 0`; with `PuzzleSync=1`
  the partner still logged no `puzzle: applied`.
* Rig-side false checks found and fixed in my script: the `switches:` line is logged BEFORE
  `hooked FEngineLoop` (so it needed the whole log, not the run window), and `FATAL|FAULT`
  matched the word "fault" inside `Default_Lizer_01`.

## Files created
| file | what | state |
|---|---|---|
| `analysis/scratch/p4_census.py` | per-map placed-actor census for my zones | ran; output cited in p4.md |
| `analysis/scratch/p4_classes.py` | class -> super chain + state names | ran; table in p4.md |
| `mod/tools/session_p4.ps1` | the live rig (strict-lock compliant, archives logs) | parse-checked, RUN once (R35) |
| `mod/doc/playthrough/p4.md` | the deliverable: results + bugs + census + next steps | written |
| `mod/doc/progress/play-p4.md` | this journal | current |
