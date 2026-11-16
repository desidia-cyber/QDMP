# play-p4b — progress journal (successor of play-p4)

**STATUS (08:30): diagnosis DONE for all three bugs from the archived 08:00 logs (no live lock
yet — `play-p2b` holds LIVE, refreshing). Root causes measured; no code edited yet.**

Task: fix + live-prove BUG-1 (secret-zone door carried by no lane), BUG-2 (enemy set 477/580),
BUG-3 (key-item digest), then run R36/R37/R39/R43/R45/R46 + re-fight two of B6..B9.

## 1. ROOT CAUSES (measured from `_test/p4run/out/run-0800-qd{1,2}.log`)

### BUG-2 — the enemy set diverges because ~100 enemies are keyed by CLASS + LIVE POSITION
The audit `CAT_ENEMY` set is built from `g_en` (`proxy.cpp` `AuditCollect`, 28324). The
divergent keys are the identity *fallback*, not spawner keys:

```
qd1 (host) 8:01:18.525 enemy id: no summoner for /Game/Enemies/DragonFly/Dragonfly_Base_BP.Dragonfly_Base_BP_C; keyed by class and spawn point
qd1        8:01:18.525 enemy id: summon at 352094,-433811,307480 dup=0 key=0494FA1E
qd2 (peer) 8:01:55.956 enemy id: summon at 354811,-468518,306407 dup=0 key=765EECAD
qd1 8:03:37 audit: DESYNC enemy 0494fa1e… host=1 peer=-1 [absent on the peer]
qd1 8:03:40 audit: DESYNC enemy 765eecad… host=-1 peer=1 [absent on the host]
```
`EnemyIdentity` (`proxy.cpp:6905-6936`) falls back to `Hash128(class "@" x100,y100,z100)` when the
enemy has no authored spawner GUID and no summoner. That position is sampled when THIS machine's
scan first meets the actor — an AI enemy has wandered by then, and each machine's scan meets it at
a different moment, so the same enemy gets different keys. A positional key is never described to
partner (`EnemySpawnPump` only offers owned keys and stays inside a 25 m audience), so it exists
on one machine forever → the diff can never heal.

Counts (measured): host 512 spawner ids + **83** positional = 595 records → digest 477;
peer 518 spawner ids + **186** positional = 704 records → digest 580. Difference of positional
records = 186 − 83 = **103** = exactly the digest difference (580 − 477).
**Conclusion: an AUDITOR SCOPE error** — it compares identities that are machine-local by
construction. Fix: mark the fallback identity on the record and exclude it from `CAT_ENEMY`
(with a rate-limited count so the coverage stays honest), and exclude its kill-log entries too.

### BUG-3 — the peer digests an inventory cache that only the host fills
`g_invNow` is written only by `ObserveHostInventory` (`proxy.cpp:17681`), which is called only
from `HostPushInventory`, which returns on `!IsHost()`. So the peer's `CAT_QUEST` set is always
empty: `audit: the partner asked for quest (0 items)` ×N on qd2, and
`DESYNC quest …SkullKeyRelic_Data host=1 peer=-1` forever, while the peer's *applied body* really
does hold the item (`inventory body rev=118 n=1 entries=1 applied=1`).
**Auditor scope error again** (exactly the `CAT_OBJECTS` pattern already documented at 28299).
Fix: digest each machine's OWN container (`InventoryEnumerate` + `CollectInventoryItem`), not the
host-only cache.

### BUG-1 — the object lane's sql-savable skip tests `GUIDs`, but the carrier tests the container
`ObjectsScanStep` (`proxy.cpp:19301`) skips any actor with a non-empty `GUIDs` array:
`if (ObjectIsSavableOwned(o) && !DirectBoolClass) continue;`
The measured SZ door has `guids=1` **but** `savable=no index=-1` (no `SavableActorStates` row), so
`ACTOR_STATES` has nothing to key on while `OBJECTS` refuses it → carried by neither lane.
Fix: make the skip instance-based on what actually carries the object — the container row
(`BuildActorStateOwners`, the same helper the `dev state:` lever prints from). Then the door's own
`Opened` (already read by `ObjectStateOf`, `SH_DOOR`) is published on 0x2A and the partner is put
into the state through `SetToOpenState` — the `ForbiddenFn` list keeps `InsertKey`/`OpenDoor`/
`Interact` unreachable, so the partner's key is never consumed.

## 2. Files changed
(none yet)

## 3. Live runs
(none yet)

## 4. NEXT STEPS
1. BUILD lock → the three fixes (all in `proxy.cpp`, small targeted edits) → `native/build.bat`
   + `native/test/run_tests.bat` → release.
2. LIVE lock (poll play-p2b; strict rule) → R35 with `qd.cheat GiveAllKeys` + `!InsertKey` for
   BUG-1, `audit` both sides; then R36/R37/R39/R43/R45/R46; two boss re-fights (B6/B7 or B8/B9)
   after reading `mod/doc/progress/play-p2b.md` for the peer boss-spawn fix state.
3. Update `mod/doc/playthrough/p4.md` + this journal after every run.
