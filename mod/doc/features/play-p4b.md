# play-p4b — the three R35 blockers fixed: the SZ door, the enemy set, the key digest

Successor of `play-p4` (`mod/doc/playthrough/p4.md`, `mod/doc/progress/play-p4.md`). Scope: the
secret-zone playthrough R35–R46 and bosses B6–B9, plus the three bugs its first live session
measured.

## What was wrong, and what this change does

### 1. BUG-1 — `SecretZoneDoor_*_BP_C` was carried by NO lane (the entry to every secret zone)

The object lane skipped every actor whose own `GUIDs` array was non-empty
(`ObjectsScanStep` → `ObjectIsSavableOwned`), on the assumption that the actor-state lane would
carry it by GUID. The measured actor says otherwise:

```
dev state: SecretZoneDoor_Blue_BP [SecretZoneDoor_Blue_BP_C] savable=no guid=- index=-1 … guids=1
```

`guids=1` but the `SavableActorStates` container holds **no row** (`index=-1`) — the game writes
that row only for doors it has already saved, so a freshly streamed secret-zone door has none.
Both lanes declined, so a key inserted on one machine opened nothing on the other.

**Fix.** The skip now tests the CARRIER (the container row, `BuildActorStateOwners` — the same
walk the `dev state:` lever prints `index=` from), not the `GUIDs` array. A savable with no row is
published on the object lane by its placed-path key, exactly like a secret wall; a savable that
gains a row moves back to the GUID lane on the next scan.

Coverage: every class `interact::ShapeOfClass` recognises — `SecretZoneDoor_Blue/Green/Red_BP_C`,
the `Door`/`Gate` family, `Chest`, `Pickup`/`Drop`, `Barricade`/`Breakable`, `Activatable` — that
holds no actor-state row. `SZFrame_NoDoor_BP_C` (a frame) classifies as a door but carries no
`Opened`/`DoorClosed`, so it is reported `no-state` and not published; a frame has no state.

**The partner never spends its own key.** The receiving machine is driven through the class' own
visual ladder (`SetToOpenState(direction)`), which is the same call the game's own `OpenDoor` makes
after it consumes a key. `objects::ForbiddenFn` refuses `InsertKey`, `OpenDoor`, `CloseDoor`, `Open`,
`TryUseKey`, `Interact`, `Grant*` and `AddItemToInventory` by name before a call can be built, so
"the receiver also used a key" is structurally impossible rather than a matter of choosing the right
member of a ladder. The rig measures that the host's inventory gains nothing during the step.

### 2. BUG-2 — the enemy alive/dead set could never converge (host 477 vs peer 580)

`EnemyIdentity` (proxy.cpp) falls back, when an enemy has no authored spawner GUID and no summoner,
to `Hash128("<class>@x100,y100,z100")` — "this class at the point my scan first met it". An AI enemy
has walked by then and each machine's scan meets it at a different moment, so **one enemy gets a
different key on each machine**:

```
qd1 8:01:18 enemy id: summon at 352094,-433811,307480 dup=0 key=0494FA1E
qd2 8:01:55 enemy id: summon at 354811,-468518,306407 dup=0 key=765EECAD   (the same DragonFly)
audit: DESYNC enemy 0494fa1e… host=1 peer=-1   /   765eecad… host=-1 peer=1
```

Counted: host 512 spawner ids + **83** positional = 595 records → digest 477; peer 518 + **186** =
704 records → digest 580. The extra positional records (186 − 83 = **103**) are exactly the digest
difference, and the shared identities alone digest **394 on both machines**. A positional key is
never described to the partner either (`EnemySpawnPump` offers only owned keys, inside a 25 m
audience), so the diff was permanent.

**Fix (auditor scope, plus the flag that makes it checkable).** `EnemyRec::posKey` marks the record
whose key came from that last-resort fallback; `AuditCollect(CAT_ENEMY)` leaves those records — and
their kill-log entries — out of the digest, and logs how many it left out once a minute
(`audit: enemy leaves out N machine-local 'class at this point' identities`). The record keeps its
key for the local row lane, which is the only thing such a key can be used for. An adopted key
(`AdoptUnknownKey`) clears the flag: it is the owner's key, so it is shared.

This is an instrument fix, and it is the honest one: two independently spawned ambient enemies are
not one enemy, so comparing their keys can never be right. The coverage statement is in the log line.

### 3. BUG-3 — the key-item digest was filled by the host only

`g_invNow` is written by `ObserveHostInventory`, whose only caller returns on `!IsHost()`. The
peer's `CAT_QUEST` set was therefore empty for the whole session (`audit: the partner asked for
quest (0 items)` on every pass) and reported `DESYNC quest …SkullKeyRelic_Data host=1 peer=-1`
forever for a relic the PEER itself had picked up and applied (`inventory body rev=118 n=1
applied=1`).

**Fix.** `AuditCollect(CAT_QUEST)` now digests each machine's OWN container through
`InventoryEnumerate` — the same walk the peer's pickup lane already runs — so the peer's set is the
inventory the game actually holds. A peer whose apply really failed still shows as a difference;
an empty/absent save is reported as "absent", never as "empty".

## Files changed

| file | function | change |
|---|---|---|
| `mod/native/src/proxy.cpp` | `EnemyRec` | new `posKey` flag (+ the measured reason) |
| | `EnemyIdentity` | clears `posKey` at the top, sets it in the last-resort fallback |
| | `AdoptUnknownKey` | clears `posKey` (an adopted key is shared) |
| | `AuditCollect` `CAT_ENEMY` | omits `posKey` records and their kill-log keys; rate-limited count |
| | `AuditCollect` `CAT_QUEST` | digests the LOCAL container via `InventoryEnumerate` |
| | `ObjectStateOwners` (new) | cached `BuildActorStateOwners` view |
| | `ObjectsScanStep` | the savable skip tests the container row, not `GUIDs` |
| `mod/tools/session_p4.ps1` | R35 | SZ door driven by its real verb `InsertKey` both ways + `qd.obj` dumps |

No wire format changed: no Kind, no intent, no Rust change.

## Switches

No new switch. The three fixes are within lanes that already ship enabled
(`EnemySync=1`, `ObjectSync=1`, `DesyncAudit=1`, `InventorySync=1`), and each one makes an existing
lane carry what it already claimed to carry.

## Proof

Live evidence and the per-step table: `mod/doc/playthrough/p4.md` (section 3) and
`mod/doc/progress/play-p4b.md`. Build: `native/build.bat` clean; `native/test/run_tests.bat`
56 passed / 0 failed.
