# live-ip2 — Round 3: the six gaps round 2 left open

Owner: `live-ip2` (worktree `sync-fix`). Round-2 docs: `world-interact.md`,
`progression.md` ("Round 2 live proof"). This round closes what those measured as open.

## The finding that unblocked everything

**The relay was dropping whole lanes.** The deployed `qd-relay.exe` predated
Kinds `0x40` (KILL_XP) and `0x41` (SAVE) and its `OBJECTS`/`ENEMY_ROWS` decoders demanded
`n` rows from *every* chunk, where `n` is the **whole set size** (WIRE.md §3.20) and a
datagram legitimately carries a prefix. Measured in the round-2 run logs:

```
[qd-relay] malformed datagram from 127.0.0.1:55581 (kind 0x2a): unexpected end of data: needed 128 bits, 0 available   x624
[qd-relay] malformed datagram from 127.0.0.1:54889 (kind 0x14): ...                                                   x301
[qd-relay] malformed datagram from 127.0.0.1:55581 (kind 0x40): unknown packet kind 0x40                              x6
```

`ACTOR_STATES` and `INVENTORY` already had the "stop when the datagram ends" guard; `OBJECTS`
and `ENEMY_ROWS` did not. **Every host → peer object row was destroyed before either peer saw
it**, which is why round 2's object-lane claims only ever worked peer → host (an intent is one
datagram) — and why KILL_XP and SAVE could never arrive at all.

Fixes: `stop when the reader is empty` in both decoders (`qd-core/src/protocol.rs`), plus the
Kinds themselves were already present in the source (the stale *binary* was the problem — this
rig builds and runs the fresh one).

## Files

* `mod/native/src/qdinteract.h` — `DirectBoolClass` (secret walls), `WantsExplicitAnim`,
  `DirectionAllowed`.
* `mod/native/src/qdprogression.h` — `IsProgressSaveTrigger` / `IsSuspendTrigger`,
  `SaveRoute`/`ROUTE_SLOT`/`ROUTE_SUSPEND`, `SaveBoard::OnPeerSave(seq, route, now)`,
  `WantedRoute`.
* `mod/native/src/qdinventory.h` — `HostPublisher::Has/DocNote/DocClear` + `TombLedger`.
* `mod/native/src/qdwire.h` — `SaveBody::route`, `WriteSave(seq, role, route)`,
  short-body-tolerant `ReadSave`.
* `mod/native/src/proxy.cpp` — `HK_SaveGameToSlot` + `FnSaveGameToSlot`/`g_oSaveGameToSlot`,
  `FStringToAnsi`, `SaveRouteOfSlot`, `SaveAnnounce`, route-aware `ProgressionSaveToDisk`,
  `DirectBoolClass` exemption in `ObjectsScanStep`, always-animate-door in both actor-state
  apply sites, `DirectionAllowed` gates, tombstone collector, tombstone-aware removal pump,
  actor-state stale-basis accept.
* `mod/rust/qd-core/src/protocol.rs` — OBJECTS/ENEMY chunk guards; `Save.route` + `save_route`.
* `mod/rust/qd-relay/tests/relay.rs` — `a_chunked_objects_body_is_forwarded_not_dropped`.
* `mod/tools/session_r3.ps1` — the Round 3 rig.
* Tests: `test_interact.cpp` (+18), `test_progression.cpp` (+19), `test_inventory.cpp` (+13).

## Design (≤15 lines)

1. **Secret walls** are published on `OBJECTS` (their own `Opened` bool, cooked-path key) even
   though they are `SavableActor_C`, because their own `Activate` writes no container entry.
   Applied through `SetToFinalState`/`Activate`; never `Interact`, never a key.
2. **A door is always animated on the receiver** (`WantsExplicitAnim`), so a close plays even
   when the door's own bool already reads the wanted value.
3. **Key to zero** is a published `count 0` tombstone in quest-item mode, bounded by a ledger
   of paths this publisher has carried and cleared when the count rises. The receiver consumes
   with the game's own `WrConsumeItem`, gated by `progression::RemovalNeeded`.
4. **Stale basis** on the actor-state lane is accepted and reported; the host decides on its
   own state (the object lane's round-2 fix).
5. **Kill XP** rides the game's real death funnel (`HK_StoreEnemySaveState` / `qd.hit`).
6. **Save mirroring** detects `UGameplayStatics::SaveGameToSlot` by HOOK (the one primitive
   every save reaches; a native caller never crosses `ProcessEvent`), and carries the ROUTE.

## Build / tests

* `native/build.bat` → `winmm.dll` exit 0.
* `native/test/run_tests.bat` → **all test executables passed**.
  `test_interact` 222 (+18), `test_progression` 144 (+19), `test_inventory` 1332 (+13);
  `test_vectors` 48/48 against qd-core (the SAVE body grew one byte, so the vectors file did
  not need to change -- it does not carry a `save` sample; the Rust and native readers both
  accept the old 5-byte body).
* `cargo test --release` (private `target-ip2` dir, so a concurrent delegate holding
  `target\release\qd-relay.exe` cannot block it) → **143 + 5 + 18 passed, 0 failed**.

## Live proof

`mod/tools/session_r3.ps1`, five runs (r3, r3b, r3c, r3d, r3e). Final run: **36 PASS / 3
FAIL**, all three being the rig's own attribution rather than the lane (both fixed in the
rig afterwards). Per-item verdicts and log lines are in the Round 3 sections of
`world-interact.md` and `progression.md`.

| item | verdict | key line |
|---|---|---|
| RELAY (unblocked 1/3/5/6) | **PROVEN** | `relay malformed total=0` (was 624 + 301 + 6) |
| 1. secret walls | **PROVEN** | `object intent applied: SecretWall25 key=DDD5E8D1 state=1 visual=SetToFinalState prop=Opened 1->1` |
| 2. door close animation | **PROVEN** | `interact: door anim Deactivate key=709D84E1 state=0` on the PARTNER |
| 3. key used to zero | **PROVEN** | `quest item removed here: …AquaDoorKey_1_Data -1 (host holds 0, this machine held 1)` |
| 4. actor-state stale basis | **PROVEN** | `accepted-with-report=0, refused=0`; live intent sent and applied |
| 5. kill XP both players | **PROVEN** both ways | `xp: paid 140 to the local player (412 -> 552)` + `xp: this death paid 140 here` |
| 6. save on both | **PROVEN** both ways | `save: wrote this machine's own suspend save (the partner's rest)` + B's file timestamp moved |

## Switches flipped

`KillXPSync=1`, `PartnerSaveSync=1`. `InteractSync`/`ObjectSync`/`ActorStateSync`/
`QuestItemSync` were already 1 from round 2 and are unchanged.

## Open risks / for the coordinator

* **The relay must be rebuilt and redeployed** with any native build that uses `0x40`/`0x41`.
  A stale relay silently drops those lanes; it is loud in its own log and nowhere else.
* `direct bool` secret walls are now on `K_OBJECTS`, which the receiver applies with
  `SetToFinalState`/`Activate`. A wall class that exposes no readable state bool is still not
  published (the existing `carries no readable state property` line names it).
* The save mirror asserts a SUCCESSFUL WRITE for `ROUTE_SLOT` (`LastSaveTime` moves) but only
  "the call returned" for `ROUTE_SUSPEND` -- the suspend writer writes a different file and
  does not move that stamp. The live evidence above shows the file timestamp moving, which is
  the stronger oracle, but it is the RIG that reads it, not the module.
* `qdsuspend.h`'s hold (`BlockSuspendSaves`, peer-only) stops the game's TIMER. A mirrored
  save is an explicit request and is not covered by that hold by design; see progression.md.
