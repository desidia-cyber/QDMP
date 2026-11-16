# Desync auditor (`qdaudit.h`, `K_AUDIT` 0x58 / `K_AUDIT_REQ` 0x59 / `K_AUDIT_ITEMS` 0x5A)

Switch: `[Rules] DesyncAudit` (default **1**), `[Rules] AuditMs` (default 10000).
Dev lever: `qd.audit` (force a pass), `qd.auditfork` (inject one divergence — test only).

---

## 1. The question this module answers

Every other module in this tree makes **one** thing converge, and each can only report on
itself. A lane that converged silently looks exactly like a lane that never had anything to
do. So nothing in the mod could answer the one question a finished co-op playthrough leaves
open:

> did the two games ever disagree about the world?

The auditor answers it directly. Every 10 s (plus on a map change, a rest/death, a boss death,
and on demand via `qd.audit`) each machine computes a digest of the synced world state of the
**current shared map**, sends it, and compares. On a disagreement that persists it asks for
the other side's item set and logs the differing items **by name and by both values**.

It is **read-only**. It owns no game state: it reads the containers the other lanes already
maintain, never writes a property, never spawns, never teleports, never calls a Blueprint
verb. That is the whole reason it can stay switched on for a full playthrough.

## 2. How to read the lines

A clean state, once a minute, per machine:

```
audit: OK 12 categories, 431 items
```

One category whose digests disagree (only after the 3 s grace window):

```
audit: DESYNC flags <whole set> host=58 peer=59 age=3104 -- asking for the item set to name the differing items
```

and then, per differing item — **the line a reader acts on**:

```
audit: DESYNC flags QDAuditProof host=-1 peer=1 age=4120 [absent on the host]
```

`host=` and `peer=` are mapped **by role, not by process**: the host's value is `host=` on
both machines, so the two logs can be diffed against each other directly. `-1` means "this
machine does not hold that key at all".

Supporting lines:

```
audit: flags healed (58 items; it had been disagreeing)
audit: category dialogue is enabled on one side only; not compared
audit: DESYNC map <local>=Ruins_Level_00 <partner>=900_SipehaWorld age=0 -- ... data categories are not compared until they agree
audit: pass 412 us over 12 categories, 431 items          (a FORCED pass; the periodic one is DEBUG)
audit: DESYNC OPEN 1 of 12 categories: flags              (the minute summary while anything is open)
```

## 3. Why a digest, and why a grace window

* **Digest, not state.** The state is up to 1024 enemy keys and 256 placed objects against a
  1200-byte body cap. Sending it every ten seconds would be the loudest lane in the session
  for a question whose answer is almost always "nothing". The steady state is **one 141-byte
  body**; the item sets travel only on a disagreement.
* **3 s of grace** (`kGraceMs`). The two machines are never at the same instant: an object
  opened 200 ms ago is applied a frame or two later, and a level being streamed is in the
  union on one side and not yet on the other. A mismatch is reported only once it has
  persisted for 3 s, so an in-flight change is never flagged — and a real desync, which does
  not heal, is not delayed in any way that matters.
* **The digest is order-independent** (a sum of per-item hashes). Both machines discover the
  same objects in their own order — the entity table's order is a scan artefact — so an
  order-dependent digest would report a desync on two machines holding identical state.

## 4. Categories covered

Each is a set of `(key, value)`; the key is always something both machines compute from the
cooked content or from a lane that has already converged — never a pointer, never a scan
ordinal.

| # | category | machine source | key | value |
|---|---|---|---|---|
| 1 | `flags` | the game's own `NamedSaveData` via `NsdEnumerate` | the flag's own name | its int32 |
| 2 | `actors` | `g_actorNow` (`SavableActorStates`) | GUID as 32 hex | its int32 |
| 3 | `objects` | **NOT COMPARED** — see §5a | — | — |
| 4 | `quest` | `g_invNow` **filtered by** `progression::IsQuestItemPath` | the item's soft path | its count |
| 5 | `enemy` | `g_en` records + `g_killLog` + `g_remoteDead` | enemy key as 32 hex | 0 dead, 1 alive |
| 6 | `boss` | `g_en[i].isBoss` + `bossPhase` | boss key as 32 hex | the phase ordinal |
| 7 | `movers` | `g_movers.PeekLocal` | `<mover id hex>:<x\|y\|z\|yaw>` | bucketed 1/8 cm / 1/65536 turn |
| 8 | `levels` | `g_stream.Wanted()` (the **union**) | the level's short name | 1 |
| 9 | `map` | `g_map.Local()` | the map's short name | 1 |
| 10 | `cutscene` | `g_cutSelfState/Name` + `g_cutscene.PeerPlaying` | `s<slot>:<state\|name>` | 0/1, name hash (0 when idle) |
| 11 | `dialogue` | `g_dlg` + `g_talk.RemoteNpc` | `d<slot>:<talking\|npc>` | 0/1, NPC-id hash |
| 12 | `deaths` | `resp::g_self` + `resp::g_peerDeaths` | `host` / `peer` | the death-penalty stack |

Eleven of the twelve are compared; `objects` is reported absent on purpose (§5a).

Buckets exist so a value that legitimately lags a frame is not a disagreement: positions to
8 cm (64 wire units), angles to 1/64 turn. Twice during the work a finer value had to be
**removed** because a live run proved it was not a shared fact rather than a tolerance
problem: the boss HP bucket (§5b) and the idle cutscene name (§5c).

The **whole instance universe** is covered, not one example: every flag in the bank, every
`SavableActorStates` entry, every placed object the `ObjectSync` scan classifies (chests,
doors, pickups, barricades, activatables — the same generic classification `qdobjectstate.h`
publishes), every `Items/KeyItems/` asset, every enemy record the current world produced,
every boss record, every mover record with a local sample, every streamed level in the union.

## 5. Categories that are legitimately LOCAL, and are deliberately excluded

Each one is a thing two machines are **supposed** to differ about, so including it would turn
a correct session into a wall of false desyncs. This list is the design, not an omission:

| excluded | why |
|---|---|
| normal inventory (potions, ammo, tools, weapons, treasure, money) | each player picks their own up; only `Items/KeyItems/` crosses by design (`qdprogression.h`). Category 4 is exactly that subset. |
| the partner's position, velocity, facing, movement mode, animation | 30 Hz **samples of two different players**. Different by construction; their correctness is `qdmotion.h`'s interpolation, judged on screen. |
| per-player status effects, HP, stamina, the HUD | each machine draws its own bars and overlays. |
| XP totals, achievement counters | `KILL_XP` pays one specific award to both players, but a player can gain XP another way; the two counters are then legitimately different. |
| the puppet's equipment transform; projectile / arc / cast VFX | cosmetics with their own lifetime; a lost sample is invisible. |
| the loot set's spawn points and per-death drop ordinal | the ordinal inside one death is assigned by the machine that ran the death, so a raw set diff would compare ordinals rather than facts. |
| which enemies **this** machine is running (`qdenemyown.h` ownership) | ownership follows the nearer player by design. What must agree is who is alive and who is dead — category 5. |
| enemy row position / HP per sample | 20 Hz latest-wins samples, interpolated on the follower. |

### 5a. `objects` is NOT compared, and the reason was measured

The placed-object set is **host-owned**. `g_objActors` is filled by `ObjectsScanStep` inside
`HostPushObjects`, which returns immediately on a peer, so on the peer both the scanned table
and `g_objNow` are empty while the host holds 95 objects. The peer applies the host's rows into
its own game and **keeps no mirror set**, so there is nothing symmetric to digest. Run 1
measured exactly that: `audit: DESYNC objects <whole set> host=95 peer=0` on both machines —
a fact about the instrument's two different *inputs*, not about the world.

So the category is reported absent, and the partner logs one
`audit: category objects is enabled on one side only; not compared`. The lane's own oracle is
its `object apply: ... confirmed=1` echo.

### 5b. The boss item carries the phase, not an HP bucket

Run 1 digested `phase | hp_bucket<<8` and reported three boss records with **identical keys and
identical counts** (`host=3 peer=3`) on both machines — only the HP half differed. The owner
reads HP from its own actor and the follower from the fraction in the owner's 20 Hz row; those
are sampled at different moments of a moving bar, so a bucket they share is a coincidence, not
a fact. The bucket is gone; phase is a published on-change value (WIRE.md §3.9a) and it is what
actually has to agree.

### 5c. The idle cutscene name is not a fact

Run 1 reported `s:0:name` and `s:1:name` differing on two machines that were both playing
nothing. `g_cutSelfName` deliberately keeps the LAST name when idle (it mirrors the `CS_ENDED`
body, which is a log aid), so the auditor now emits name `0` whenever the state is idle.

### 5d. One diff per episode, not one per retry

Run 1 asked for the item set every 2 s and re-printed it each time: **1843** `audit: DESYNC`
lines in four minutes. A category that has been diffed is now re-asked only on the same 30 s
repeat clock the repeat report uses, so an open desync costs one diff per item per half-minute.

## 6. Wire format (WIRE.md §3.27)

```
AUDIT       seq u32 | map_hash u32 | n u8 | n x { cat u8, digest u64, count u16 }    141 B for 12
AUDIT_REQ   seq u32 | cat u8                                                          5 B
AUDIT_ITEMS seq u32 | cat u8 | total u16 | first u16 | { str key, u64 value } ...     cursor
```

All three are **unreliable**: they are periodic state, a lost digest costs one pass, and a
lost item chunk is re-asked for on the request's 2 s retry. Acknowledging the diagnostic lane
would make it the second-most expensive thing in the mod. `AUDIT_ITEMS` continues across ticks
with a `first` cursor (never a fragment — WIRE.md §3.6), so a 1024-key enemy set is ~40 whole
datagrams and never a truncated one.

`map_hash` differing **suppresses** the twelve data categories and reports the split once:
two machines in different worlds disagree about everything, and the split is the one fact
worth reporting.

## 7. Native / Rust byte compatibility

* `qdwire.h`: `K_AUDIT`, `K_AUDIT_REQ`, `K_AUDIT_ITEMS` in `enum Kind`, plus `AuditBody`,
  `AuditCat`, `AuditItem` and their `Write*`/`Read*` functions (`ReadAudit` rejects a category
  byte outside 1..12; `ReadAuditItemsHeader` likewise).
* `qd-core/src/protocol.rs`: `Kind::{Audit, AuditReq, AuditItems}`, `Audit`, `AuditCat`,
  `AuditReq`, `AuditItems`, `AuditItemRow`, `MAX_AUDIT_CATS`, `MAX_AUDIT_KEY`, encode/decode
  arms and `check_audit_cat` shared by both.
* `doc/vectors.txt`: one vector each — `audit`, `audit_req`, `audit_items` — generated by
  `qd-core/tests/wire_vectors.rs` and consumed by `native/test/test_vectors.cpp`.
* The relay forwards bodies byte-for-byte and needs no change (`qd-relay/src/lib.rs`).

## 8. Overhead

Measured on the rig (`audit: pass … us` on a forced pass, and the LANE bytes from the body
sizes below).

| item | value |
|---|---|
| steady state, one machine | 157 B every 10 s = **~15.7 B/s** (141 B body + 16 B envelope) |
| both directions | ~31 B/s total |
| tick cost when nothing is due | 12-iteration comparison; no allocation, no UObject read |
| digest pass (12 categories, hub) | see §9 — measured, sub-millisecond |
| a desync being diffed | one `AUDIT_REQ` (21 B / 2 s) + the dump: `flags` 60 items ≈ 2 datagrams, `enemy` 1000 keys ≈ 40 datagrams — **only while a desync is open** |

The pass is the only part whose cost scales with the world; it is timed with
`QueryPerformanceCounter` and logged, so the claim above is a measurement and not an estimate.

## 9. Proof

### Unit tests

* `native/test/test_audit.cpp` — **77 checks**: digest order-independence, value/key
  sensitivity, key canonicalisation and truncation, bucketing (including the
  half-away-from-zero rule that keeps −31 and +31 in one bucket), HP buckets, the per-item
  diff (identical sets say nothing; one changed value is one row naming that key; a one-sided
  key is marked; the row cap bounds the log while the total still counts), and the whole
  grace/request state machine (silent inside 3 s, reported once past it, not repeated, healed
  once, a diffed episode re-asked only on the repeat clock, a one-sided category is a switch
  difference and never a desync, a world split suppresses the data categories, a stale partner
  is not compared).
* `native/test/test_vectors.cpp` — the three new bodies match `doc/vectors.txt` byte for byte
  (**100 matched, 0 failed, 0 missing**) and round-trip; an unknown category byte is refused.
* `cargo test -p qd-core` — **143 + 5 pass**; `cargo test -p qd-relay` — **18 pass**. The
  vector file is regenerated by `qd-core` and consumed by native, so a drift between the two
  implementations fails a test instead of corrupting a session.
* The rest of the native suite is unchanged and green (`run_tests.bat`, all `test_*.cpp`).

### Live (run 1, `_test/auditrun/out/report-audit.txt`)

Two instances, the shipped `qdcoop.ini`, relay on loopback, `LogLevel=3`. Session established
`inst1=HOST inst2=PEER`.

**The detection chain worked end to end.** Both machines compared 12 categories every 10 s,
detected mismatches, requested the other side's item set, received it as a cursor across
ticks, and printed per-item rows naming the key and **both** values:

```
2:23:12.152 WARN audit: DESYNC flags Arjaot_State host=-1 peer=1 age=10016 [absent on the host]
2:23:12.216 WARN audit: DESYNC flags BrokeBarricade host=-1 peer=1 age=10016 [absent on the host]
2:25:15.091 WARN audit: DESYNC OPEN 5 of 12 categories: flags objects enemy boss cutscene
2:27:15.606 WARN audit: DESYNC OPEN 4 of 12 categories: flags objects boss cutscene
```

The category list between those two summaries **proves the heal path live**: `enemy` was open
at 2:25 and gone at 2:27.

**And run 1 found five defects — in the auditor, not in the session.** Every one is §5a-§5d
plus the flag source: `g_flags` is the APPLY cache (empty on the host, which publishes straight
out of the game's `NamedSaveData`), so the host digested 0 flags and the peer 11 and the
auditor reported the whole bank. All five are fixed in source, the build is green and the unit
tests above cover the last two.

**NOT YET RE-OBSERVED after those fixes:** a clean session printing `audit: OK 12 categories,
N items`, and the `qd.auditfork` proof of an exactly-one-item report (the lever and the job are
in `mod/tools/session-audit.ps1`, whose verdicts are written for exactly that run). Run 1
ended with 4 categories open — all four instrument defects — so the OK line could not print.
The confirming run is the next step; `session-audit.ps1` is ready and takes ~6 minutes.

## 10. What the coordinator should do next

1. Run `mod/tools/session-audit.ps1` (needs the LIVE lock; ~6 min). Expected verdicts: both
   machines `audit: OK 11 categories, N items`; after the fork, exactly one
   `audit: DESYNC flags QDAuditProof host=-1 peer=1` on both; then `audit: flags healed`.
2. If `actors` or `enemy` reports on a clean boot, read §11 first: those two compare the
   host's scanned container with the peer's applied one, and a report there is a real finding
   about that lane — which is the point of the module.

## 11. Open risks

* `CAT_ACTORS` and `CAT_OBJECTS` compare the **host's scanned container** with the **peer's
  applied container**. They agree only if the apply path is complete; a key the peer refused
  to apply is a genuine finding, which is the point — but it means these two categories are
  the most likely source of a REAL report on an imperfect session, and the first thing to read
  when one appears.
* `CAT_ENEMY` compares the two **record tables** plus the two death ledgers. With
  `MirrorSpawn=1` they converge; with it off, a key the peer never constructed is a real
  difference and will be reported as such (correctly — that enemy IS missing on that machine).
* `CAT_FLAGS` enumerates the bank through `FlagBank::At`, which is O(n³) for the whole bank.
  Real banks are tens of entries (the operator's save holds 11, the enumerated universe is 58),
  which is sub-millisecond; a bank of hundreds would be the pass's dominant cost.
* `qd.auditfork` is a **test lever**: it injects a divergence into this machine's auditor view
  only and writes no game state. It exists because every real lane repairs itself by design —
  a real write is broadcast and applied within a second and is correctly reported as *healed*,
  so a non-healing divergence cannot be produced any other way. It is not on any hot path and
  is inert unless the file exists in the instance's UserDir.
