# play-p2 progress journal

**Status: DONE FOR THIS PASS (07:56).** `WorldSetSync` PROVEN and shipped as 1; `PuzzleSync` measured
not-provable, left 0 with the reason in the ini. Deliverable `mod/doc/playthrough/p2.md` complete with
the per-step table, the lane evidence, the rig findings and the successor list. No lock held, no
process of mine running (verified: the three processes now alive belong to play-p1, whose name is in
`_test/locks/live/owner.txt`).

**Run 6 (`-Phase story`, 07:17-07:50) — 74 pass / 19 fail, all eight areas entered and followed.**
The 19 failures are characterised in `p2.md` §3 notes: B3/B4 spawned on the HOST only (`peers=0`, no
peer row) and the demon bodies never took damage; the real `Attack` input moved no boss HP; `HP` is
`absent` under that property name so the magma claim is unmeasured; R17's fishwife name matched no
actor; R24's teleporter step used the refused `qd.tp …40`; R27's `near:Knight_Nu_NPC` resolved to
`Ghost_Ujji`; R26's nudge was refused because the PEER owned the lift (it boarded first); R16's
necklace count needs the quest necklace item, not the accessory. Several PASSes that read an empty
set are explicitly corrected in the doc (the peer's "both bodies dead" is an artefact).

**Everything a successor needs is in `p2.md` §4 (rig/mod findings) and §6 (the eight measured gaps),
and the rig already implements the strict live-lock rule (Assert-LiveLock + 5-min owner.txt heartbeat
+ own-pid-only cleanup).**

Part owned: CAMPAIGN-ROUTE R16-R29 + bosses B2 (Whale King), B3 (Necronomicon), B4 (Demon Bosses).
Deliverable: `mod/doc/playthrough/p2.md` (worldset + puzzle sections already written); screenshots
under `_test/playthrough/p2/`.

---

## 1. What I established before writing anything (with evidence)

| finding | evidence |
|---|---|
| Shipped ini has `WorldSetSync=0`, `PuzzleSync=0`; everything else 1 | `mod/qdcoop.ini` `[Rules]` |
| The lane's own switch check: `worldsync::Tick` returns immediately unless `world_set_sync`; **`PuzzleSync` is nested inside it**, so PuzzleSync cannot work with WorldSetSync=0 | `qdworld_engine.h::Tick` (line ~985) |
| Relay built 03:15:42 predates kinds 0x60/0x61/0x62/0x64/0x65 in `protocol.rs` (mtime 05:18); `Kind::from_u8` returns `Err(UnknownKind)` → relay counts them malformed and drops them | `qd-core/src/protocol.rs::from_u8` tail; `qd-relay/src/lib.rs` lines 234-256 (`Envelope::decode`) and `malformed` counter |
| Native build is current: `winmm.dll` 05:55:56 newer than `proxy.cpp` 05:55:12 | file timestamps |
| Seed save `analysis/save-backup-before-live/World0.sav` has **no `UnlockedTeleporters` property at all**, `SavePointsVisited` = `GR_Camp_Save`, no `TeleporterUnlockCount` flag | `qdsave.py dump` |
| Teleporter placements I need: `Teleporter_Aqr` in 200_AQ, `Teleporter_Lib` in 310_LIB_exterior, `Teleporter_Maus` in 420_MAUS_mid, `Teleporter_GR` in 100_GR | `analysis/pak/answers.md` lines 269-281 |
| Unlock mechanism = `Teleporter_C`'s own `RegisterSphere` begin-overlap → `Array_AddUnique`; the lane applies a partner unlock either by in-place append or by dispatching that same event | `blueprint-surface.md` §Teleporter (export list); `qdworld_engine.h::ApplySet/RegisterTeleporter` |
| Proof log lines to read: sender `worldset: teleporters holds 1 entry in this save` + `worldset: sent teleporters rows 0..0 of 1 rev=N`; receiver `worldset: the partner's teleporters rev=… carries 1 entry here 1 row(s), 1 new`, `worldset: teleported … unlocked through the game's own RegisterSphere (dispatched)` / `worldset: applied teleporters from the partner's unlock -- appended=… via the game=…`, then both `worldset: teleporters holds N entries in this save` | `qdworld_engine.h::ReadLocalSets/PublishSet/ApplySet` |
| Puzzle proof lines: `puzzle: sent N prop row(s) rev=N` on the turner, `puzzle: applied N of M prop row(s) from the partner rev=N` on the other, and `qd.world=dump` prints `puzzle: N prop(s) held, sent=… applied=… actors=…` plus `prop key=… yaw=… seq=…` | `qdworld_engine.h::PuzzleObserve/Publish/OnPuzzle/DumpToLog` |
| Book classes: `Lib_Book_00..04_BP_C` (287 placements) are `FS_StaticMeshActor_C` with **no function and no state property**; the 5 used by the puzzle are `Lib_Book_00..04` meshes + `Lib_Book_Open_BP_C`. `LIB_BookRecepticle_BP` is a **key item** asset. No book-manager class exists in the pak | `qdpuzzle.h` header; `analysis/pak/blueprints` decode quoted there; `pak-files.txt` grep for `Library/.*(Book\|Puzzle\|Shelf)` |
| ⇒ there is **no rig lever that rotates an arbitrary actor** (only `qd.mover` on tracked movers), so "book turned by one" must be produced by a real in-game verb or be reported as not-doable; empirical test planned in 300_LIB: `qd.obj`, `qd.gate near:<book>`, `qd.act near:Lib_Book_00_BP!Interact`, then read `puzzle:` lines on both | `proxy.cpp` lever census (qd.tp/act/flag/trigger/mover/attack…); no rotation lever |
| Boss rows from the rig's own table: B2 `WhaleKingSpawner`/`Whaleking_Boss`/`WhaleKingIsDead`; B3 `Necronomicon_Boss_Spawner`/`Necronomicon_BP`/`NecronimiconDefeated`, start = lectern read → `CustomEvent`, and it needs `Stay` + `Talk=@('Necronomicon_BP!Interact','dialogue!End')`; B4 `ActivationTrigger4`/`DemonSpider`+`Hellbat`/`DemonBossesKilled` in 450_MAUS_darkmaze | `mod/tools/session26.ps1` `$script:Bosses` |
| B2/B3/B4 were already concluded live in earlier sessions (B2 run 5 key `6AEFB87A`; B3 runs 9/14; B4 s26 run 15 / flag s28) — so my run is a re-proof on the shipped ini, not a first proof | `LIVE-TEST-LOG.md` lines 5884-5886, 178 |
| Magma hazard is host-only today (GAP 5): `DemonBoss_FightController_C` spawns despawnable magma puddles; `HazardSync=1` exists in the shipped ini, lane = `qdhazard.h` 0x64/0x65 — I must check it hurts whoever stands in it on both | `COVERAGE-MATRIX.md` §C11 / GAP 5; `qdcoop.ini` `HazardSync=1` |
| A launched game does NOT survive the pwsh command that started it (R14) ⇒ each phase is ONE script invocation: boot → steps → screenshots → stop | `session26.ps1` header, "PHASES" paragraph |
| **The "book turned" mechanism does not exist in this content.** `Lib_Book_00_BP_C` decoded from the pak: 47 names, 6 exports, parent `FS_StaticMeshActor_C`, one `StaticMeshComponent` + `ENVControlData_C`, **no function, no property, no mobility/interact**. `Lib_Book_Open_BP_C` is the same shape. `Library_RotateBridge_C` lives at `Content/Blueprints/Library_RotateBridge.uasset` (SavableActor + Elevator_Base ⇒ mover lane). `LIB_BookRecepticle_BP` is an **item pickup** (parent `AQR_KeyRecepticle_BP_C`, `FS_ItemData_C`, `SaveHelper_C`, BoxComponent) i.e. the book puzzle is "insert collected book items into receptacles", whose state is a **SavableActor (lane 0x12)**, not a transform. `SuperCubePuzzleDoor` (the lane's other class) has **no asset in this pak**. | `python analysis/pak/code/blueprints.py <path>` runs above; `analysis/pak-files.txt` |

**Consequence for PuzzleSync:** the transform lane's only shipped actors are static decor with no
player verb, so a live "book turned by one" **cannot be produced by a player action**. I will still
run the lane and report what it measures, and I will prove the *real* library puzzle's intermediate
state through the lane that actually carries it (the receptacle SavableActor + the completion flag).
PuzzleSync stays `0` unless a reachable turn appears in the run.

## 2. Locks / machine state at journal creation

- `_test/locks/build` owner `view-fix2` (05:55), `_test/locks/live` owner `view-fix2` (05:40).
- A **live session by view-fix2 was running at 06:01-06:02** (pids 21876/42860 + relay 33636,
  `qdcoop-qd1.log` growing, `audit: DESYNC map … partner map title_screen`). I did NOT touch it.
- I hold no lock yet and started no process. Nothing of mine is running.

## 2b. Locks / run state — 06:15

- view-fix2's run finished at **06:09:13** (`_test/sview-fix2/out/report-view-fix2.txt`, its relay log
  ends 06:09:08) and its processes are gone; it left `_test/locks/live` **stale at 34 min**.
  Per rule 4 I removed that stale lock and took **LIVE = play-p2** at 06:15. BUILD lock is still
  view-fix2's (19 min, cargo ran 06:13) — I did not take it; I do not need it yet.
- The relay was rebuilt by view-fix2's cargo at **06:13:06** from this worktree's `protocol.rs`, so
  it contains kinds 0x60/0x61/0x62 — the stale-relay blocker is gone without me building.
- `mod/tools/session-p2.ps1` written and **parse-checked** (1013+ lines, `PARSE OK`); it boots a
  fresh pair under `_test/p2`, writes the shipped ini + `WorldSetSync=1 PuzzleSync=1`, grants the
  library book keys / raider necklaces / meal tickets with `QDCOOP_GIVE`, and has phases
  `worldpuzzle|aq|lib|maus`. Screenshots → `_test/playthrough/p2/`.
- **RUN 1 STARTED 06:16**: `session-p2.ps1 -Phase worldpuzzle` (background job `pwsh-1`,
  console at `_test/p2/console-worldpuzzle.txt`).

## 3. Files changed by me

- `mod/tools/session-p2.ps1` (NEW, mine): the four-phase rig. Parse-checked.
- `mod/doc/progress/play-p2.md` (this file).

## 3b. Live run log

**RUN 1 (06:14-06:15, `worldpuzzle`) — harness aborted early; the lane itself is ALIVE.**
`_test/p2/out/report-worldpuzzle.txt`, console `_test/p2/console-worldpuzzle.txt`.
Two harness defects, both fixed in the script:
1. `Enter-Area` used `$host` as a local — PowerShell's read-only automatic `$Host` ⇒
   `Cannot overwrite variable Host`. Renamed `$hostLine`.
2. `Get-RunLines` started at `hooked FEngineLoop`, but the `switches:` line is written BEFORE it
   (log line 5 vs line 8) ⇒ the switch check reported FAIL. Now the run marker is the
   `===== qdcoop pid` banner (sentinel −1 so a banner at line 1 is not mistaken for "not found").
   Also fixed the `(in the save)` key regex (log lines carry a timestamp prefix).
What the run DID measure (quotes from `qdcoop-p21.log`):
```
6:14:43.761 INFO switches: ... worldSet=1 puzzle=1
6:14:46.626 INFO session established: role=HOST   (peer 6:14:53)
6:14:57.505 INFO worldset: teleporters holds 1 entry in this save
6:14:57.505 INFO worldset: sent teleporters rows 0..0 of 1 rev=1
6:15:08.394 INFO worldset: the partner's teleporters rev=1 carries 1 entry here 1 row(s), 0 new
6:15:09.061 WARN worldset: TeleporterUnlockCount could not be raised to 1: active save changed; refusing until re-init
```
⇒ the lane is armed on both machines and the sets already agree; the live save starts with **one**
destination, so the union proof must ADD a destination both did not have. Run 1 also proved the
relay is clean (`0 malformed`, relay rebuilt 06:13 by view-fix2 from this worktree's protocol.rs).
The `could not be raised ... active save changed` WARN is recorded: the derived counter is refused
while the save is still re-initialising; the SET is the truth and the counter is re-raised later.

**RUN 2 (06:16-06:27, `worldpuzzle`) — 8 pass / 6 fail; the failures were MY harness, not the mod.**
Evidence in `qdcoop-p21.log` / `qdcoop-p22.log`:
1. **The peer-follow check was wrong.** The peer reacts to the host's announcement *before* the host's
   own engine line is written: peer `6:23:13.857 travel: entered the partner's area 420_MAUS_mid by
   streaming it` vs host `6:23:15.824 rig: level engine state ...`. My mark was taken after the host
   wait, so the peer line was already behind the cursor. **Fixed:** the peer mark is taken first.
2. **`qd.tp near:<teleporter>` is refused by design**: `the spot beside it is inside a teleporter's
   volume, and the game would move the player out of the level` (`RigStandBeside`, radius 300 cm).
   So the rig cannot place a player ON a teleporter. **Fixed:** place the pawn 450 cm away FACING it
   (`near:` already sets yaw to the target) and post a REAL W key-down/up to the game window
   (`Send-Key`, WM_ACTIVATE + WM_KEYDOWN/UP, the same window `capture-hidden.ps1` uses), so the
   player WALKS into the RegisterSphere — the only unlock path the content has. Fallback if walking
   does not fire it: dispatch the teleporter's own `RegisterSphere` begin-overlap via `qd.act`.
3. Also measured (useful): the area follow works for 100_GR, 200_AQ, 310_LIB_exterior, 420_MAUS_mid,
   300_LIB — five areas entered, five follows. But the peer's own log line for the follow says
   `stream travel: ... NO level list pushed (no level list known for 'X'); flags forced instead
   visible=1 loaded=1 engineLoaded=1 engineVisible=0`. The `engineVisible=0` is at the instant of
   forcing; the screenshots at each step are what decide whether the peer's geometry is drawn.

**RUN 3 (06:29-06:40, `worldpuzzle`) — THE WORLDSET PROOF IS IN THE LOG (18 pass / 7 fail; the 7 are
verdict-reader defects, now fixed).** Evidence:

```
HOST 6:31:00.171 dev teleport near 'Teleporter_GR' -> ... offset=450   (placement only; refused at 40)
HOST 6:32:13.054 dev teleport near 'Teleporter_Aqr' -> ... offset=450 level=PersistentLevel shown=1
     then a REAL W key hold (Send-Key, WM_ACTIVATE+WM_KEYDOWN/UP)
HOST 6:32:13.697 worldset: teleporters holds 2 entries in this save          <-- the walk-in unlock
HOST 6:32:13.699 worldset: sent teleporters rows 0..1 of 2 rev=2
PEER 6:32:14.005 worldset: the partner's teleporters rev=2 carries 2 entries here 2 row(s), 1 new
PEER 6:32:14.499 worldset: applied teleporters from the partner's unlock -- appended=1 via the game=0 left=0 now 2 entries
PEER 6:32:14.978 worldset: teleporters holds 2 entries in this save
HOST 6:36:55.642 worldset[teleporters]: save=2/2 union=2 rev=2  /  Aquatic_Teleporter (in the save) / Ruins_Teleporter (in the save)
     worldset counter TeleporterUnlockCount = 2 (set size 2)
PEER 6:37:07.233 worldset[teleporters]: save=2/2 union=2 rev=3 applied=1   (same two keys in the save)
```
**That is `WorldSetSync` proven end to end: the HOST unlocked `Aquatic_Teleporter` by walking into the
placed `Teleporter_Aqr`; the set went on the wire in the same 2 ms; the PEER appended it to its own
save (in-place append, `via the game=0`); both dumps then show `save=2/2` with the identical
`Aquatic_Teleporter` + `Ruins_Teleporter` and `TeleporterUnlockCount = 2`.**

What the 7 FAILs were (all reader bugs, all fixed):
1. `sent`/`peer received`/`apply` waits used marks taken AFTER the unlock ⇒ the four lines (~1 s
   after the overlap) were already behind the cursor. Now the pair's marks are taken before the walk.
2. The save's identity is the DESTINATION asset name (`Aquatic_Teleporter`), not the placed actor's
   (`Teleporter_Aqr`). The verdict now compares the two IN-SAVE SETS and names what was added.
3. `Read-Flag` never returned a number: the read line is `rig: flag dump -- X = 2 (game bank 2, ...)`
   and my regexes required no space / end-of-line. Also the lever ignores the SAME instruction twice,
   so a second read needs `#<nonce>`. Both fixed (set also uses the nonce).
4. `QDCOOP_GIVE` has a **1024-byte** buffer and key items need the `+` prefix
   (`+path` = `SaveFunctions_C.AddItemToInventory`, the pickup path). My 29-path list was ~1.3 kB and
   truncated ⇒ `inventory sent rev=1 entries=0` and no book was ever in the bag. Now 5 books + 4
   necklaces + 1 meal ticket, all `+`, ~870 bytes.

Puzzle facts measured in run 3:
```
puzzle: 0 prop(s) held, sent=0 applied=0 actors=0        (player at the area entry: the lane only
                                                          observes props within 40 m)
rig: object dump -- ... skipped-savables 1931, no-state 6172
savkey 592d62f4... door Default__AqrStatuePuzzleDoor_C    (a savable that IS here)
LIB_BookRecepticle_BP4 [LIB_BookRecepticle_BP_C] forced verb Interact ran=yes state 0 -> 0
decor book verb Interact: Lib_Book_00_BP_C ran=no state 0 -> 0
```
⇒ the receptacle family is placed and answers `Interact`; the decor book answers nothing; `sent` rose
only because props entered the 40 m window (a first observation, NOT a turn). The verdict now decides
a turn by the SAME prop key having a DIFFERENT yaw, and separately checks that the rows which did
travel were `applied` on the partner.

**RUN 4 (06:41-06:46, `worldpuzzle`) — 23 pass / 3 fail. WorldSetSync is PROVEN; PuzzleSync is not
provable and the reason is measured.**
```
HOST 6:43:55.981 dev teleport near 'Teleporter_Aqr' ... offset=450 shown=1   (then a real W hold)
HOST 6:43:56.793 worldset: teleporters holds 2 entries in this save
HOST 6:43:56.795 worldset: sent teleporters rows 0..1 of 2 rev=2
PEER 6:43:57.032 worldset: the partner's teleporters rev=2 carries 2 entries here 2 row(s), 1 new
PEER 6:43:58.853 worldset: applied teleporters from the partner's unlock -- appended=1 via the game=0 left=0 now 2 entries
PEER 6:43:59.173 worldset: teleporters holds 2 entries in this save
HOST worldset[teleporters]: save=2/2 union=2 · Aquatic_Teleporter (in the save) · Ruins_Teleporter (in the save)
PEER worldset[teleporters]: save=2/2 union=2 rev=3 applied=1 · the same two keys
BOTH worldset counter TeleporterUnlockCount = 2 (set size 2)
```
The only worldset FAIL is my dump filter: the `(in the save)` key lines do not contain the word
`worldset`, so `Dump-World` dropped them before the parser saw them (**fixed**: filter now includes
`in the save`). The evidence above is read straight from the two logs.

Puzzle (all measured):
```
puzzle: 463 prop(s) held, sent=65 applied=43 actors=256   (HOST; the peer's rows DID apply here)
puzzle: 462 prop(s) held, sent=46 applied=0  actors=256   (PEER; nothing to apply: h->p rows miss)
decor book Interact/Activate/Open/Rotate/Push/Toggle on Lib_Book_00_BP_C: ran=no, yaw unchanged
props with a changed yaw = 0
LIB_BookRecepticle_BP [LIB_BookRecepticle_BP_C] forced verb Interact ran=yes state 0 -> 0  (x4)
```
Two more real findings:
* The lane's actor table is capped at **256** while 462-463 props are within its 40 m window, and the
  two machines iterate GObjects in different orders, so the PEER cannot resolve most of the HOST's
  rows (`applied=0` in that direction) while the HOST resolves the peer's (`applied=43`). Since a
  static prop's received pose EQUALS its local pose, nothing visible is wrong today, but that cap is
  the first thing to raise if a real rotating puzzle is ever added.
* `SetActorXform` on these `FS_StaticMeshActor_C` props applies without a visible change (their pose
  never changes), so "applied" here means "the engine call ran", not "a book moved".

**RUN 5 (07:12-07:15, `-Phase story` = aq+lib+maus in one boot) — reached 200_AQ for the first time
on any rig, synced the raider conversation, then BOTH INSTANCES AND MY RELAY WERE KILLED by another
delegate's session at 07:14:51, not by a crash.**
Evidence for the kill (so nobody chases a phantom crash):
```
qdcoop-p21.log / p22.log both end at 7:14:51.7xx with normal lines (no FAULT, no exception)
qdcoop-qd1.log  7:15:14  1834 B      <-- another delegate's instances, launched ~07:14:55
qdcoop-qd2.log  7:15:12  1663 B
my relay pid 15176: GONE (their Stop-All kills 'qd-relay' by name)
no CrashReportClient dir newer than 06:41, no Application-event entry, no engine log
```
⇒ **the LIVE lock is not being honoured** (I took it 07:12:09, refreshed 07:14:00; their script's
phase-start `Stop-All` kills every `QueensDomain-*` and `qd-relay` process). A successor should
re-check `_test/p2/inst*/qd.*` activity and the other delegates' qdcoop-qd*.log mtimes before
trusting a "both died" result, and should know that a mid-run kill looks exactly like a crash in the
report.

What run 5 DID measure (200_AQ — **never reached by any previous rig**, CAMPAIGN-ROUTE §5):
```
7:13:21.349 rig: level engine state 200_AQ LoadedLevel=1 actuallyVisible=1            (HOST)
7:13:27.948 travel: entered the partner's area 200_AQ by streaming it (loaded and SHOWN here)  (PEER)
7:13:33.838 dev teleport near 'Raider_NPC_BP' -> Raider_NPC_BP at -7,-260,-24 … offset=200
7:13:39.716 dev interact: forced verb Interact on Raider_NPC_BP [Raider_NPC_BP_C] ran=yes
7:13:40.841 rig: dialogue cursor (after the press) talking=1 asset=Raider_Dialogue_00 sequenceIndex=0 line=0 entry=0 options=2 chosen=65535
7:13:46.876 rig: dialogue cursor (after the press) talking=1 asset=Raider_Dialogue_00 sequenceIndex=1 line=1 …
7:13:44.398 WARN audit: DESYNC dialogue <whole set> host=4 peer=4 age=3016
7:14:03.021 INFO audit: dialogue digest differed but every item matches (4 vs 4 items)
7:14:41.834 INFO audit: dialogue healed (4 items; it had been disagreeing)
HOST inventory sent rev=82 entries=5  (the boot grants DID land this time)
```
⇒ R16's *interaction* is proven and the dialogue is identical on both (the auditor's dialogue
does-not-persist). The **necklace count stayed 0**: the raider's dialogue has **2 options**
(`options=2 chosen=65535`), and pressing "advance" never picks one, so the turn-in was never offered.
Script fixed: each round now presses `opt0` and `opt1`.

## 3b-2. STRICT LOCK RULE applied to the rig (07:40, coordinator's new rule)

`mod/tools/session-p2.ps1` now enforces it in code:
* `Assert-LiveLock` runs before anything is launched. If `_test/locks/live` exists and the owner is
  not `play-p2`, it refuses **unless** `owner.txt` is > 25 min old **and** no
  `QueensDomain-Win64-Shipping`/`qd-relay` process is running; only then does it break the lock.
  Refusal exits 2 with a line saying why — it does not launch, seed or install anything.
* `Update-LockHeartbeat` rewrites `owner.txt` (`play-p2 <ISO time>`) at least every 5 minutes; it is
  called from every wait loop, the boot/session wait and the settle sleep, so a 30-minute session
  keeps a fresh owner file the whole time.
* `Stop-All` kills **only** `$script:relayProc` and the pids recorded in `_test/p2/inst*.pid`
  (plus a recorded `_test/p2/relay.pid`), never by process name. Both incidents (06:46 stale-read,
  07:14 mutual kill) are recorded above; this is the fix.

Also: R24/R27's teleporter steps still use `qd.tp near:... 40`, which the rig refuses (inside the
volume) — they need the same place-at-450 + real-W-walk sequence the worldset proof uses. R17's
`near:Fishwife_Ghost_BP` found no live actor in 210_AQ_underground and its "peer sees the same
conversation" verdict read a stale mark (a *Raider* dialogue BLOCK line) — both need fixing before
those steps can be judged.

**Build/test status (07:06, under the BUILD lock, released):**
* `mod\native\build.bat` → **built** `winmm.dll` (1,836,544 B, sha A6ED4F63…), and the binary carries
  `worldset: applied`, `puzzle: sent`, `hazard`, `join:`, `flag intent`, `RegisterSphere`.
  (An earlier string check of mine read the MASTER checkout's stale DLL because `[IO.File]` uses the
  process CWD, not PowerShell's location — absolute paths only.)
* `mod\native\test\run_tests.bat` → **39 of 40 executables pass 0 failed** (`test_gapworld 131`,
  `test_wire 121`, all others green). **The 40th, `test_ending.cpp`, does not COMPILE**: it uses
  `wire::BossEndBody`, `wire::WriteBossEnd/ReadBossEnd`, `wire::kBossEndBytes`,
  `wire::kBossPlayerPeer`, which the current `qdwire.h` does not define. That is play-p3's/ending
  lane's code and a leftover of the 06:33 shared-header incident — **I did not touch it**; it needs
  the owner to finish the restore. Full log: `_test/p2/native-tests.txt`.
* `cargo test --workspace` → **145 + 5 + 18 passed, 0 failed** (log `_test/p2/cargo-tests.txt`).

**Done since the last journal update:** `WorldSetSync=1` is in `mod/qdcoop.ini` (BUILD lock taken
06:49, released 06:49; `PuzzleSync` left `0` with the measured reason written beside it). All reader
fixes are in the rig: in-save key parse (dump filter now keeps `in the save` lines), flag reads with
`#nonce` and the `= N (` form, `dev props:` HP parse (`HP=i100`), boss placement before attacks,
rider boarding checks, NPC-name fallbacks, and per-phase timing trimmed.

**Exact next steps for a successor**
1. Wait for `_test/locks/live` to disappear (or its `owner.txt` to be > 25 min old AND no
   `QueensDomain`/`qd-relay` process). Refresh your own `owner.txt` every ~5 min while running.
2. Take LIVE as `play-p2`; run
   `powershell -NoProfile -ExecutionPolicy Bypass -File mod\tools\session-p2.ps1 -Phase story`
   (console → `_test/p2/console-story.txt`, report → `_test/p2/out/report-story.txt`).
3. Read the report's SUMMARY; copy the per-step evidence into `mod/doc/playthrough/p2.md` §3; add
   the screenshot pairs from `_test/playthrough/p2/`.
4. Release the LIVE lock (stop-all is in the script) and finish the doc.
If the slot never comes: report R16-R19/R20-R29 as **NOT RUN by this part**, citing the existing
Session 26/28/29 evidence for B2/B3/B4 (`LIVE-TEST-LOG.md` 5884-5886) and mark 200_AQ / 310 / 320 /
400 / 420 / 450 as never-before-reached (CAMPAIGN-ROUTE §5 "never reached").

## 4. Per-step table

| step | status (run 6, 07:17-07:50) |
|---|---|
| R16 200_AQ raider camp | interaction + dialogue sync PASS; necklace count 0 (needs the quest item) |
| R17 210_AQ fishwife egg dialogue | NOT-REACHABLE by name (`near:Fishwife_Ghost_BP` matched nothing); the peer"dialogue" PASS in the report is a stale-mark false positive |
| R18 210_AQ dark cave | **PASS** — `AQR_OpenedDarkCave` 0->1 on BOTH |
| R19 210_AQ arena doors/pickups + B2 | doors + pickups interact (partial); **B2 did not spawn** (`triggered 0->0`) |
| R20 300_LIB bestiary | partial (0, peer read raced) |
| R21 300_LIB Knight Budaen key | interact PASS on A; specific key not observed |
| R22 300_LIB book puzzle | **PASS (control + state agree)**; flag 0/0 |
| R23 300_LIB B3 Necronomicon | host body + kill + `NecronimiconDefeated=1` BOTH; **no peer body**; real attacks moved no HP |
| R24 310_LIB teleporter | NOT-REACHABLE this run (used the refused `qd.tp ...40`; script fixed, needs the walk) |
| R25 320_LIB vault relic | partial: flag 0/0, relic not taken |
| R26 400_MAUS lift/lever/doors | **rider PASS** (`BOARDED`, place on id=C6616CB4), door 0->1, lever ran; mover nudge refused (peer owned it) |
| R27 420_MAUS relics + Knight Nu | `near:Knight_Nu_NPC` resolved to `Ghost_Ujji` (wrong NPC); relic flag 0/0 |
| R28 450_MAUS Chef/King quests | **PASS (interaction + both flags 0/0 agree)** |
| R29 450_MAUS B4 demons + magma | bodies on HOST only (2300/940, alive); no peer rows; `DemonBossesKilled=0`; magma unmeasured (`HP=absent`) |
| WorldSetSync proof | **PROVEN**, shipped as 1 |
| PuzzleSync proof | NOT provable in this content, stays 0 (documented) |

## 5. Exact next steps for a successor

1. Take `_test/locks/build`; `cargo build --release` in `mod/rust` (relay MUST be rebuilt or
   0x60-0x62 are dropped); run `cargo test` in qd-core and `native/test/run_tests.bat`; release.
2. Write `mod/tools/session-p2.ps1` modelled on `mod/tools/session-gapworld.ps1` (same helpers:
   install proxy, write ini overrides, seed 2 instances under `_test/p2`, start relay, launch
   `run-hidden.ps1`, wait for `session established`), with `-Phase worldpuzzle|aq|lib|maus`.
3. Take `_test/locks/live`; run one phase; screenshots via `capture-hidden.ps1`; `qd.audit` both;
   release live lock; repeat per phase.
4. Only if the worldset lines and puzzle-row lines appear on BOTH machines: flip
   `WorldSetSync=1`/`PuzzleSync=1` in `mod/qdcoop.ini` under the BUILD lock and re-run once to
   confirm the shipped file boots with them on.
