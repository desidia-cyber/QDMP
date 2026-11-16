# join-sync — progress journal

**Status: DONE (offline).** BUILT + UNIT-TESTED, **NOT live-proven** — the LIVE lock is held
by an active delegate (`play-p2`, two game instances + relay since 06:41); launching would
collide on the relay port and the shared `gameDir\winmm.dll`/qdcoop.ini. `JoinReconcile`
ships **0**. Everything a live run needs is scripted in `mod/tools/session-join.ps1`.

Last updated: 2026-11-16 06:50 (local)

## ⚠️ Incident and recovery (2026-11-16 06:33)

While fixing a comment in `qdwire.h` I used `Set-Content` (PS 5.1, ANSI default) and then
`git show HEAD:... >` to undo it. **HEAD is far behind the worktree** (the checkpoint commit
`798ce8d` is the old 1590-line qdwire.h; the worktree had 2036 lines of uncommitted delegate
work), so the redirect replaced the worktree file with the OLD version. Recovered from the
coordinator's checkpoint commit `a921b5f6` ("checkpoint: uncommitted delegate work
2026-11-16_0605"), blob `f9da66c2`, which is the full 2036-line file. Verified:
`git diff a921b5f -- mod/native/src/qdwire.h` shows **only my 20 join lines**. Build and
tests were re-run green afterwards. NO other file was touched by that command. Lesson: never
use PowerShell text rewrites on shared files; use the `edit` tool (UTF-8).

## Task (one line)

On every session establish (first connect AND reconnect after a drop) the PEER adopts the
HOST's world/progression state exactly — including REVERTING what the peer has and the host
does not. Reserved wire Kinds 0x68-0x6B, intent 19. Switch `JoinReconcile`.

## Files created so far (all mine, unbuilt)

| file | what |
|---|---|
| `mod/native/src/qdjoin.h` | PURE: the 8 categories, bounded `State`, `Plan`, `BuildPlan` diff with the exclusion rules, the 4 wire bodies (REQ/STATE/END/ACK), the chunk writer/reader. |
| `mod/native/src/qdjoin_engine.h` | ENGINE: state builders (flags via NsdEnumerate, actor container, placed objects, dead enemies via the EnemySaveStates stride probe + the mod ledger, key items via InventoryEnumerate+AssetPath, the 3 save arrays, puzzle poses, map/area), the backup (copy+size verify), the apply (sets→flags→actors→objects→enemy reset→quest→puzzle→persist→travel), the host's chunk server with repeats, the peer's accumulate/apply state machine, the forced audit kick. |
| `mod/native/test/test_join.cpp` | ~120 assertions over every category's direction and the wire. Static tables (State/Plan are ~150 KB). |
| `mod/doc/progress/join-sync.md` | this journal. |

No shared file has been touched yet. No build has run. No locks held by me.

## Findings with evidence (design inputs)

1. **The gap is structural.** `flags::FlagBank::ApplyRemote` refuses to lower
   (`if (value < t_[i].value) return false`); `objects::ApplyAllowed` returns false for
   `hostState == 0`. Actor states CAN lower (`actorstate::AbsoluteAccept` true).
2. **Doors/activatables have a real close path** (`interact::Bidirectional`,
   `interact::Plan` → `SetToOpenState(false)` / `Deactivate`). Chests/pickups/breakables are
   one-way in the normal lane; the join writes the bool directly and logs.
3. **World sets are monotone with no removal** (`worldset::Union::Merge`,
   `worldsync::ApplySet` appends only). Reverting needs an in-place TArray shift + `Num--`
   and a rebuild of `worldsync::g_union` (no edit to qdworldset.h).
4. **EnemySaveStates** is `TMap<FGuid,{u8 bValid,float RespawnTime,float RemainingHP}>` at
   the reflected property offset (measured `save+0x270`), no documented stride. The engine
   probes strides {36,40,32,44,48,52} for `bValid<=1` + finite floats and falls back to the
   mod ledger. The apply uses the rest lane's proven `ClearAllEnemySaveStates` + `StoreEnemySaveState(guid,-1,0)`.
5. **Session establish**: the Rust relay owns roles and sends `K_WELCOME`; both machines
   leave ROLE_NONE on it; `Net::session_` is the relay session id. `g_sessionLive`
   (proxy.cpp:240) is dead. Detection = `IsPeer()` + (session changed OR a >3 s RX gap then
   resumes).
6. **Audit OK**: `AuditPump` prints `audit: OK N categories, M items` only once a minute.
   The engine needs a helper (`AuditForceNow`) that forces a pass and prints the summary
   now; the peer schedules it ~1.5 s after the ACK, and the host forces its pass on the ACK,
   so the peer has a fresh host digest to compare against.
7. **Proxy layout**: everything from line 110 to 28,449 is one anonymous namespace. The
   engine must be included at the end (after the audit block, after `auditl`) and its
   hooks forward-declared at ~166 for `HK_Tick` (25,999) and `DispatchInbound` (19,146).
   `g_joinApplying` must be defined near 240, not in the engine, because
   `HK_StoreEnemySaveState` (26,225) tests it.
8. **Puzzle props** carry no state property; the pose is the actor transform. The join scan
   is a full bounded GObjects walk and is gated on `PuzzleSync` (cost).
9. **Placed objects**: the host's set is world-scoped (its scan sees only its loaded
   levels), so a peer-only object is NOT a divergence; only keys present on both are
   compared. Actor states are save-wide, so a peer-only non-zero entry IS reverted.

## Per-player exclusions (to document in join-sync.md)

- `worldset::FlagStaysLocal` → shop stock (`ShopPurchasedKey*`, `ShopPurchases`,
  `ShopSales`, `NewItemsInShop`) never touched.
- `worldset::IsUnionCounter` → `TotalChestsOpened`, `TotalSecretDoorsOpened`,
  `ShopAmountSpent`: the truth is the SUM; the peer donates its excess as an additive intent
  and then adopts the host.
- `join::FlagMonotoneOnly` → `TotalAchievement*`, `CompletedChallenges`,
  `FullEnergyCellsCount`, `TiniScytheHammerAP`, `IntroMusicState`: raised to a host that is
  ahead, never lowered (a per-player collection/trophy).
- Normal inventory/equipment/currency, XP, `LastTouchedCheckpoint` + save-identity fields,
  settings: never on this lane (out of scope by the brief).

## Planned shared edits (when the BUILD lock is taken)

| file | change |
|---|---|
| `qdwire.h` | `K_JOIN_REQ=0x68`, `K_JOIN_STATE=0x69`, `K_JOIN_END=0x6A`, `K_JOIN_ACK=0x6B` in `enum Kind`; REQ/END/ACK in `IsReliableKind` (STATE stays unreliable, repeated). |
| `qdconfig.h` | `int join_reconcile;` + `ConfigDefaults` + `JoinReconcile` parse key. Default **0** until live-proven. |
| `mod/qdcoop.ini` | `JoinReconcile=` (0 now; 1 after proof). |
| `proxy.cpp` | `#include "qdjoin.h"` (top) + `#include "qdjoin_engine.h"` (end, after audit); fwd decls `JoinReconcilePump`, `JoinBusy`, `joinsync::JoinOn*`, `AuditForceNow`; `g_joinApplying` global; `AuditForceNow` definition in the audit section; 4 dispatch cases; `JoinReconcilePump(now)` call in HK_Tick; gate `PeerFlagIntentPump`/`PeerActorIntentPump`/`PeerObjectIntentPump`/`PeerInventoryIntentPump`/`MirrorVisit` queue/`ProgressionPump` partner-save/`worldsync::Tick` on `JoinBusy()`; `if (g_joinApplying) return;` in `HK_StoreEnemySaveState` after the original call. |
| `mod/rust/qd-core/src/protocol.rs` | the four `Kind` values + `from_u8` + `is_reliable`. |
| `mod/doc/WIRE.md` | §3.34. |

`run_tests.bat` needs NO edit: it globs `test_*.cpp`.

## Build / test / live

- **Build: GREEN.** `mod/native/build.bat` -> `winmm.dll` 1,835,520 bytes; only the
  pre-existing `qddlgbox.h` C4819 encoding warning.
- **Native tests: GREEN for this module.** `test_join` 111 passed / 0 failed. The full
  `run_tests.bat` reports ONE failure: `test_ending.cpp` (untracked new file from another
  delegate at 06:05) fails to compile on `wire::BossEndBody` (it has `using namespace
  qd::ending;` but no `using namespace qd;`). NOT my file; not fixed.
- **Rust: GREEN.** `cargo test -p qd-core` 145+5, `cargo test -p qd-relay` 18.
- **Live: none yet.** LIVE lock held by view-fix2 (two game pids 55964/61428 since 06:17).

## Shared edits made (all built)

| file | change |
|---|---|
| `qdwire.h` | `K_JOIN_REQ/STATE/END/ACK` 0x68-0x6B in `enum Kind`; REQ/END/ACK in `IsReliableKind`. |
| `qdconfig.h` | `join_reconcile` field, default **0**, `JoinReconcile` parse key. |
| `mod/qdcoop.ini` | `JoinReconcile=0` with the proof requirement documented. |
| `proxy.cpp` | `#include "qdjoin.h"` + engine include before the namespace close; `g_joinApplying`; forward decls; `MirrorVisit`/`MirrorObserve` gate; `join=` boot line; `JoinBusy()` gates in PeerFlag/PeerGrant/PeerInventory/PeerActor/PeerObject pumps + `partner_save_sync` + `worldsync::Tick`; 4 dispatch cases; `JoinReconcilePump(now)` in HK_Tick; `if (g_joinApplying) return;` in `HK_StoreEnemySaveState`; `AuditForceNow` (forced digest + immediate OK/OPEN line). |
| `mod/rust/qd-core/src/protocol.rs` | the four `Kind` values, `from_u8`, `name`, `is_reliable`, opaque decode. |
| `mod/doc/WIRE.md` | §3.35. |

## Exact next steps for a successor

1. Take the **LIVE** lock (`G:\Freespace\Queens_Domain\_test\locks\live`) — view-fix2 is
   running two instances; wait for release or the 25-min stale rule.
2. Seed the peer's save AHEAD: copy a test instance, then
   `python mod/tools/qdsave.py apply-flags --root <inst2>\Saved\SaveGames World0.sav --json <json>`
   with a story flag the host lacks (add is allowed; lowering is refused). For an opened
   door / dead enemy / unlocked teleporter, craft `SavableActorStates`/`EnemySaveStates`/
   `SavePointsVisited` with a small Python edit of the seed, or drive them live.
3. Launch inst1 (HOST) then inst2 (PEER) with `-UserDir=...`, the shipped ini with
   `JoinReconcile=1`, and the relay. Expected PEER log:
   `join: session established ... asking the host` -> `join: the host's state is complete`
   -> `join: reconciled to host -- N flags set, M flags reverted, ... travel=..., backup=...`
   -> `audit: OK ...`; the backup dir `join-backup-<stamp>` exists beside the peer's saves.
4. Force a drop (kill/restart the relay or block the port for >3 s); expect
   `join: the link came back ... reconciling` then `join: already in sync with host`.
5. If proven, flip `c.join_reconcile = 1` in `qdconfig.h` and `JoinReconcile=1` in the ini;
   else leave 0 and report NOT live-proven.

## Half-done / risks

- Nothing half-written. BUILD lock released. No LIVE lock, no pids of mine.
- The full native suite has one unrelated compile failure (`test_ending.cpp`, another
  delegate's untracked file). It will block `run_tests.bat`'s exit code until they fix it.
- EnemySaveStates stride probe may fail; ledger fallback documented.
- `WrRefreshActorState` cannot FORCE a close; a chest the host calls shut may stay visually
  open on the peer while the container is 0 (`INCOMPLETE` in the log).
- The join does full object/puzzle GObjects walks (bounded, ~100-300 ms, once).

