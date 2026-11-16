# join-sync — reconcile the peer to the host at every session establishment

**Status: BUILT and UNIT-TESTED; NOT live-proven.** The native module builds green
(`winmm.dll` 1,835,520 B) and `test/test_join.cpp` passes **111 / 111**. The live two-instance
proof is the next step and is scripted in `mod/tools/session-join.ps1`. `JoinReconcile`
therefore ships **0** in both `qdconfig.h` and `qdcoop.ini` until that run shows the summary
line, `audit: OK`, and the backup directory (see §7).

Written 2026-11-16. Reserved wire Kinds **0x68–0x6B** (intent 19 remains reserved and
unused). Module: `mod/native/src/qdjoin.h` (pure rules, wire) + `qdjoin_engine.h` (the
engine half, included at the end of `proxy.cpp`).

---

## 1. The gap this closes

Every existing lane makes its own thing converge, and the two that carry most of the world
are **monotone**:

* `flags::FlagBank::ApplyRemote` refuses to lower a value (`if (value < t_[i].value) return
  false`) — correctly, because a rise is a shared fact and a crossing heartbeat must not
  undo a partner's action while the pair plays;
* `objects::ApplyAllowed` returns false for `hostState == 0`, so a placed object is never
  closed or un-broken by the partner;
* the world-set union never removes, and the inventory/key-item lane only adds (the
  conservative removal predicate exists but is not exact);
* `SavableActorStates` *can* be lowered (`actorstate::AbsoluteAccept` is true), but the GUID
  lane only applies values the host has and never invents a 0 for a key the host lacks.

The result is that the converged state is a **union**. A peer whose save is AHEAD — a story
flag the host never set, a door the host's save says is shut, an enemy the host's save says
is alive, a key item the host does not hold, a teleporter the host never unlocked — keeps
every one of those for the whole session, and the desync auditor reports the same items every
minute. At the moment two saves meet, the rule must be the opposite of the rule in play:
**the host is absolute and the peer matches it in both directions.**

`test_join.cpp` pins exactly the parts that fail silently: which keys are compared, in which
direction, and what is excluded.

## 2. What is synced (host wins)

| category | source on each machine | rule |
|---|---|---|
| `flags` | `NamedSaveData` via `NsdEnumerate` | shared: adopt the host exactly (set **or** revert); a shared key the host lacks is written 0 |
| `actors` | `SavableActorStates` (`ActorStateEnumerate`) | the container is save-wide, so absence is 0: adopt a differing value **and** revert a peer entry the host does not hold; then the game's own `RestoreFromSave` (via `WrRefreshActorState`) plus, for doors/levers, the class' own `SetToOpenState(false)` / `Deactivate` |
| `objects` | the `ObjectSync` scan (placed actors: chests, doors, levers, secret walls, breakables) | the host's set is world-scoped, so a peer-only object is not a divergence; a key on both is adopted in **either** direction, including close |
| `enemy` | `EnemySaveStates` (stride-probed; unioned with the mod's own death ledger) | one reset: the game's own `ClearAllEnemySaveStates`, then `StoreEnemySaveState(guid,-1,0)` for every key the host has dead. Dead only on the peer → alive; dead on the host → dead |
| `quest` | `InventoryContents` filtered by `progression::IsQuestItemPath` | exact: add what the peer lacks (`WrGrantItem`), remove what the host lacks / lower an excess (`WrConsumeItem`) |
| `worldset` | `UnlockedTeleporters`, `SavePointsVisited`, `ShopTeleporterDestinations` | the host's set is exact: append what the peer lacks (or the game's own `RegisterSphere` when the array is full), **remove in place** what the host lacks, then rebuild `worldsync::g_union` so the monotone lane cannot put it back |
| `puzzle` | the placed `Lib_Book_*` / `SuperCubePuzzleDoor` transforms | a pose the peer holds differently is adopted through `SetActorXform`; a prop the peer does not hold is not invented |
| `map` | the current `UWorld` short name + `g_rigLevelPin` (the streamed area) | a different world/area is followed through `FollowHardTravel` (which handles both a hard load and a sublevel); "the host left its area" calls `ClearAreaPin` |

Authority, keys and wire are one design: the host serialises what it already holds and never
writes; the peer diffs and applies. Keys are the game's own identities — a flag name, a GUID,
a placed-actor path hash, a soft object path, an asset short name — never a pointer.

## 3. What is NOT synced (per-player, by rule)

This is the exclusion list the task asks for, and it is one predicate per reason:

| excluded | why |
|---|---|
| shop stock keys — `ShopPurchasedKey*`, `ShopPurchases`, `ShopSales`, `NewItemsInShop` | a purchase is the buyer's own act; buying the last unique relic must not empty the partner's shelf. `worldset::FlagStaysLocal` owns the list (`features/gap-world.md` §4). |
| `TotalChestsOpened`, `TotalSecretDoorsOpened`, `ShopAmountSpent` | the pair's truth is the **SUM** of two machines' distinct acts, not the host's value. The peer **donates** its excess to the host as an additive `INTENT kind 1` (mode 0) and then adopts the result; nothing either player opened is lost. (`worldset::IsUnionCounter`.) |
| `TotalAchievement*`, `CompletedChallenges`, `FullEnergyCellsCount`, `TiniScytheHammerAP`, `IntroMusicState` | per-player collection/trophy counters. The reconcile may **raise** a peer to a host that is ahead (a shared goal already met is harmless) but never lowers one. `join::FlagMonotoneOnly`. |
| character level / XP / stats / attributes | entirely out of scope (the brief keeps them per-player). |
| normal inventory, equipment, currency | per-player by design (`qdprogression.h` §2.4). Only key items cross. |
| `LastTouchedCheckpoint`, `SavePointID`, `SaveLocation`, `SaveLevel`, `SavedArea`, `CurrentPlayerHealth` | the respawn point and save identity are the player's own (`worldset::IsPerPlayerProperty`). |
| death-penalty stacks, settings | each machine's own (`qdrespawn.h`). |

Known consequence, stated rather than hidden: because the audit's `flags` category digests
the **whole** `NamedSaveData`, a peer that loaded a save AHEAD on one of the per-player
counters above keeps that value and the auditor will report it. The proof seeds a **story**
flag, not a per-player counter. The flag lane's monotone rule converges them during play.

## 4. The established-session phase

1. The peer sees a session establishment (a new relay session id, or a >3 s RX gap that then
   resumes) and sends `JOIN_REQ`.
2. The host builds one snapshot of its own state and streams it as `JOIN_STATE` chunks (a
   `first` cursor across ticks; empty categories are announced too), then `JOIN_END` with the
   category mask. It repeats the whole pass every 800 ms until the ack (bounded).
3. The peer accumulates. On `JOIN_END`, if every masked category arrived complete, it
   **backs the save files up first** (`<SaveGames>\join-backup-<yyyyMMdd-HHmmss>\`, every
   `*.sav`, size-verified; the path is logged and is part of the summary). A failed backup
   refuses the whole reconcile — the safe direction.
4. It brackets the other lanes' echoes (`JoinBusy()`): the flag mirror records without
   queueing an intent, and the flag/actor/object/inventory intent pumps, the partner-save
   mirror and `worldsync::Tick` all stand down; their detectors are reset so the reconcile's
   writes are a new baseline, not a burst of intents.
5. Apply order: **world sets → flags → savable actor states → objects → one enemy reset →
   key items → puzzle poses → persist the save → follow the map last**. The map is last
   because `FollowHardTravel` tears the world down; every save-scoped write has already
   landed by then. The save is persisted with the game's own writer (`ProgressionSaveToDisk`,
   ROUTE_SLOT) inside the save board's own-save bracket, so a later SOLO load agrees.
6. Summary (exactly the shape the coordinator asked for):
   `join: reconciled to host -- N flags set, M flags reverted, K actor states, E enemies
   revived/killed, I key items added/removed, travel=<map|none>, backup=<path>`, or
   `join: already in sync with host`. A detail line carries the split (enemies revived/killed,
   items added/removed, objects, sets, donated counters).
7. The peer acks; the host forces its auditor, and the peer forces its own 1.5 s later, so the
   pair sees `audit: OK … (forced: the join reconcile finished)` rather than waiting out the
   minute.

The host's state is never written: it only serialises what it already holds.

## 5. Files

### Created (mine)

| file | what |
|---|---|
| `mod/native/src/qdjoin.h` | pure: 8 categories, bounded `State`/`Plan`, `BuildPlan` with the exclusion rules, the four bodies + chunk writer/reader |
| `mod/native/src/qdjoin_engine.h` | engine half: state builders, the save backup, the apply, the host server, the peer state machine, the audit kick |
| `mod/native/test/test_join.cpp` | 111 assertions over every category's direction, the exclusions, the map decision and the wire |
| `mod/doc/features/join-sync.md` | this file |
| `mod/doc/progress/join-sync.md` | the work journal |
| `mod/tools/session-join.ps1` | the two-instance proof run (ready; not yet executed) |

### Shared files changed (small, targeted)

| file | change |
|---|---|
| `qdwire.h` | `K_JOIN_REQ/STATE/END/ACK` 0x68–0x6B; REQ/END/ACK reliable |
| `qdconfig.h` | `join_reconcile` (default 0), `JoinReconcile` ini key |
| `mod/qdcoop.ini` | `JoinReconcile=0` |
| `proxy.cpp` | includes, `g_joinApplying`, forward decls, 4 dispatch cases, the pump call, `JoinBusy()` gates in five peer pumps + the partner save + `worldsync::Tick`, the flag-mirror queue gate, `HK_StoreEnemySaveState` suppression, `AuditForceNow` |
| `mod/rust/qd-core/src/protocol.rs` | the four kinds (`from_u8`, `name`, `is_reliable`, opaque decode — the relay forwards byte-for-byte) |
| `mod/doc/WIRE.md` | §3.35 |

## 6. Coverage

* **Generic, not per-actor.** Every `NamedSaveData` key, every `SavableActorStates` entry,
  every object the `ObjectSync` scan classifies, every `EnemySaveStates` record, every
  `Items/KeyItems/` asset, every element of the three save arrays, every placed puzzle prop,
  and the world/area itself.
* **Door / chest / lever / secret wall / breakable**: a savable actor goes through the
  container write + the class' own `Restore State From Save Data`; a door or lever also gets
  `SetToOpenState(false)` / `Deactivate` (the same call its own close verb makes). A chest,
  pickup or breakable has no close *verb* on this build; the reconcile writes its state bool
  directly and reports `INCOMPLETE` if the class will not take it — the container is right
  either way.
* **Enemies**: the stride of `EnemySaveStates` is not documented, so it is **probed** (36,
  40, 32, 44, 48, 52) against `bValid ∈ {0,1}` and finite floats; the chosen stride is
  logged. If the probe fails, the set falls back to the module's own death ledger (kill log +
  the partner's deaths + records the sweep marked) and says so. The *action* is the rest
  lane's proven `ClearAllEnemySaveStates` + `StoreEnemySaveState`, not a hand-written map
  edit.
* **Not covered**: an enemy death persisted in a previous solo session whose save map cannot
  be decoded (falls back to the ledger); a per-player counter the peer is ahead on (by rule);
  a placed object the peer has not streamed in yet (the next lane body carries it).

## 7. Proof

### Unit tests (this build)

```
cl /nologo /std:c++17 /O2 /EHsc /W4 /MT ... test_join.cpp
111 passed, 0 failed
```

`run_tests.bat` also builds and runs every other suite; the only failure in the tree is
`test_ending.cpp`, an **untracked new file from another delegate** that does not compile
(`wire::` unqualified) — not this module. `cargo test -p qd-core` 145+5 pass, `cargo test -p
qd-relay` 18 pass.

### Live

**NOT YET RUN.** The two-instance proof is scripted in `mod/tools/session-join.ps1` and is
the one thing this document cannot claim. What it must show, per machine:

* peer: `join: session established ... asking the host for its world state`
* peer: `join: the host's state is complete (...); reconciling`
* peer: `join: reconciled to host -- N flags set, M flags reverted, ... travel=..., backup=...`
* peer: `audit: OK … (forced: the join reconcile finished)`
* a `join-backup-<stamp>` directory beside the peer's save files
* after a forced drop and reconnect: `join: already in sync with host`

`JoinReconcile` is **0** until that run exists.

## 8. Open risks / for the coordinator

1. **The `EnemySaveStates` stride is inferred.** The probe is conservative and logged, and
   the fallback does not invent keys, but a live run on a save with both dead and live
   records is what settles it.
2. **A chest the host calls shut may stay visually open** on the peer: `WrRefreshActorState`
   has a force-open backstop but no force-close, and a one-way shape has no close verb. The
   container is 0 and the log says `INCOMPLETE`; the appearance is the game's own restore
   path.
3. **Removing a peer-only teleporter** is an in-place `TArray` shift (`Num--`, no realloc)
   plus a rebuild of `worldsync::g_union`. It is the only place this module edits memory
   around an engine container instead of calling an engine function; a live run must show the
   destination gone from the peer's menu and the count lowered.
4. **`JoinBusy()` is a time window** (the apply plus 2.5 s). A lane that observes slightly
   later than that could still echo one value; its own monotone/echo rules bound the damage.
5. `qdcoop.ini` `JoinReconcile` must be the same on both machines; a one-sided lane is
   reported by the auditor as "enabled on one side only", not as a desync.
