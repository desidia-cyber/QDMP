# play-p3b — progress journal

**Status: STARTED. Machine busy (play-p2's live session). Build/test tree VERIFIED GREEN — the
coordinator's "test_ending.cpp does not compile" premise is STALE; nothing was lost. Live part not
started yet: waiting for the LIVE lock.**
Part: continues `play-p3` — CAMPAIGN-ROUTE R30-R34, R47, R49 · B5, B10, +BS · all four endings
(`mod/doc/playthrough/p3.md`). Checkout `G:\Freespace\Queens_Domain\.worktrees\sync-fix`.
Deliverable to extend: `mod/doc/playthrough/p3.md`.

## 1. The coordinator's request vs. what is actually in the tree

Request was: "FIRST fix the native test build — test_ending.cpp does not compile because
`wire::BossEndBody` / `kBossEndBytes` / `kBossPlayerPeer` are missing from qdwire.h (lost in the 06:33
overwrite incident)".

**Measured: all three symbols are present and the suite builds and runs.** The 06:33 revert WAS
repaired — by the owner at **06:40:23** (play-p3's journal, "SHARED-HEADER INCIDENT", says so; the
successor reads it as already done). Evidence, this session:

| check | result |
|---|---|
| `grep BossEnd mod/native/src/qdwire.h` | `BossEndBody` L845, `WriteBossEnd` L852, `ReadBossEnd` L860, `kBossEndBytes` L873 (=22), `kBossPlayerPeer` L803 |
| working `qdwire.h` blob | `7dd2f53074af…`, 78602 bytes — **byte-identical to `refs/checkpoints/coop-20261116-0650`** |
| `refs/checkpoints/coop-20261116-0605` | also has `BossEndBody` + `kBossPlayerPeer` (77189 bytes) |
| `mod/native/build.bat` | **BUILD OK**, `winmm.dll` 1,839,104 bytes |
| `mod/native/test/run_tests.bat` | **exit=0**, **40 suites**, *all test executables passed* |
| `test_ending` | **90 passed, 0 failed** (built 07:51:36, no build errors in `test_ending.build.log`) |
| `test_bossphase` | **81 passed, 0 failed** |
| `test_wire` | **121 passed, 0 failed** |
| `cargo test -p qd-core` | **145 + 5 passed, 0 failed** |

Byte-compatibility of the ending body is also intact: native `BossEndBody` = guid(16) | rev(u32) |
ending(u8) | who(u8) = 22 bytes = `kBossEndBytes` = Rust `protocol.rs` `struct BossEnd { id, rev,
ending, who }` (L1266-1273, `Kind::BossEnd = 0x5E` L150).

So **no edit was needed and none was made**. I did not touch `qdwire.h` — restoring "from a checkpoint"
would have rewritten a file other delegates depend on, for no reason.

## 2. Live-run blocker (why the live part has not started)

- `_test/locks/live` = `play-p2`, `owner.txt` mtime 07:18:57 → **stale by the 25 min rule**, BUT
  `qd-relay` pid 87024 + two `QueensDomain-Win64-Shipping` (43604, 69660) started 07:17 are **still
  running**. The STRICT LIVE LOCK rule says **WAIT** while processes run. I am waiting.
- **Consequence found:** `cargo build --release` fails with
  `failed to remove …\target\release\qd-relay.exe — Access is denied` because play-p2's relay is
  image-mapped from that exact path. **The release relay (06:13:06) is OLDER than
  `qd-core/src/protocol.rs` (06:31:18)**, so `K_BOSS_END` (0x5E) forwarding through that binary is
  unproven. Rebuild it as soon as no relay is running (the brief requires this anyway: "a stale
  qd-relay.exe drops new kinds").
- `qd-relay/src/lib.rs` was last touched 09.11 → the relay's forwarding is a plain kind-blind forward;
  the risk is the `Kind::from_u8` enumeration in the *binary*, not the relay logic.

## 3. Code read while waiting (facts for the run)

- **F1. `BOSS_PHASE` follower half can work.** `EnemyByGuid` (proxy.cpp:6641) does **not** exclude
  mirrors, so `bossphase_lane::OnPhase` → `DriveLocal` will find the peer's constructed copy and write
  `CurrentStateIndex`/`CurrentSubState` + dispatch `OnStateIndexChanged`/`OnSubStateChanged`.
  R1 measured the real transition as `state=0 sub=2`, i.e. **the sub-state is the carrier**;
  `DriveLocal` calls `OnSubStateChanged(sub)` when the sub write succeeds (proxy.cpp:10847-10855) —
  that is the half that must fire for phase 2.
- **F2. RISK in the ending lane's copy lookup.** `ending_lane::FindCopy` (proxy.cpp:11230-11256)
  **skips mirrors** in its first loop; the only way it can return the peer's Queen is the
  `g_bossKeyOn` → `EnemyByGuid(g_bossKey)` fallback, which does not test `mirror` but requires the
  shared-bar key to be set and to match. If the peer has no bar (`g_bossKeyOn == 0`) or a stale key,
  `ApplyLocal` reports `kDriveNoCopy` and the ending is never driven there. **Candidate fix:** accept a
  mirror of the right class in `FindCopy` for the ending path (the phase lane already drives mirrors).
  Decide after the first live run shows whether the fallback fires.
- **F3. Mirror relevance is 25 m.** `kMirrorRelevanceCm = 2500` (proxy.cpp:9205) and
  `EnemySpawnPump` (9207-9263) requires `havePeer` + `g_own.Running(guid)` on the owner. So the peer
  only gets a Queen copy if it is **within 25 m of her** when the host describes her — which is exactly
  what the R1 rig fix (`Invoke-Go $P 'Queen_Spawner_BP' 300` then `Queen_BP` 500) is for.
- **F4. The rig's wake check is strict and correct:** `Invoke-EGQueen` waits 35 s after the ending and
  requires `ending wake: the player's wake-up played` on **each** machine plus a non-black screenshot
  (`Mean -gt 3`, `session-endgame.ps1:3047-3058`).
- **F5. The rig measures her phase cursor on both machines** every third strike via `qd.bphase`, and
  requires host `boss phase: sent … (TRANSITION)` **and** peer
  `boss phase: … driven here … OnStateIndexChanged ran` (`session-endgame.ps1:3013-3014`).

## 3b. The rig lacked the brief's mandatory audit step — added

`grep qd.audit mod/tools/session-endgame.ps1` → **no match** (the rig never asked the auditor). The
playthrough brief requires a forced `qd.audit` after every step and the answer recorded. Added
`Invoke-AuditPair` (session-endgame.ps1:3184) and called it after every endgame step
(R30 3210, R31 3214, R32 3218, R34 3222, R33 3255, B10 3258, R47 3264, R49 3266).
It forces `qd.audit` on BOTH machines, reads `audit: pass N us over C categories, K items` (a forced
pass) or `audit: DESYNC …`, and **FAILs on "no audit answer"** rather than treating silence as clean.
Script re-parsed with the PowerShell language parser: **0 errors**, 3346 lines.

## 4. Files changed by me

| file | change |
|---|---|
| `mod/tools/session-endgame.ps1` | **+`Invoke-AuditPair`** (L3184) and 8 calls — the brief's per-step forced audit. Parses clean; not yet exercised live. |
| `mod/doc/progress/play-p3b.md` | this journal (new) |

No native/Rust source edit was required for the coordinator's request.

## 5. Locks / processes

- **BUILD lock: taken by play-p3b 07:44:20 → released 07:56** (owner.txt = play-p3b; `rmdir` after the
  build+test pass).
- **LIVE lock: not held.** `play-p2`'s, with live processes → waiting.
- No processes of mine are running.

## 6. Exact next steps (for a successor, in priority order)

1. **Wait for** no `qd-relay` / `QueensDomain-Win64-Shipping` / `cl.exe`, then take the LIVE lock
   (`mkdir` + `owner.txt` = `play-p3b`, refresh every 10 min).
2. Rebuild + deploy the relay under the BUILD lock (fixes the 0x5E-forwarding doubt):
   `cargo build --release` in `mod/rust`, then assert `relay malformed total=0`.
3. `powershell -NoProfile -ExecutionPolicy Bypass -File mod\tools\session-endgame.ps1 -Phase endgame
   -Only Queen -ShippedDefaults` — the proof this part is owed: peer **in the arena**, real fight,
   `sub` transition on **both**, ending on both, wake on both.
4. `-Only Sipeha:Banish`, `-Only Sipeha:Eat`; then `-Only Scientist,Castle,CastleExt,Worlds`.
5. Fill `mod/doc/playthrough/p3.md` §1 rows with the verdicts + screenshot pairs; only then decide on
   `BossPhaseSync`/`EndingSync`.
