# Audit of the DollsMP sync doc set — protocol design and lessons for a new UE4.27 co-op mod

Sources read: `_PROTOCOL.md`, `12`, `19`, `20`, `37`, `45`, `64`, `66`, `83` (full, or headings + key
sections), plus `27` and `68`, where the host/guest authority model actually lives. Where something is
not in the docs I say so rather than filling the gap.

---

## 1. The authority model

**Doc 20 is not the co-op authority doc.** It covers what single-machine work does and does not buy. 20 §0:
everything built — reload suppression, respawn, death screen, corpse removal — "is **client-local and
single-machine**. Three of the four are **wrong for co-op as written**, and not because of a bug: they act
on state that does not replicate." The correction:

> "Suppress **only the level reload**. Let the game's own pawn death → despawn → respawn sequence run,
> because that sequence already replicates correctly and already destroys the corpse on the authority."
> — 20 §0

Mechanism (F-001): "`AActor::bHidden` is not a replicated property in UE 4.27". F-004: hiding "changes what
is drawn and nothing else … **A hidden actor is still a participant.**" 20 §4's verdicts generalise: corpse —
remove "**on the authority**, by closing the actor's replication channel"; death screen — "Stays local. Not a
compromise; the correct model."; respawn — "Decided by the authority and replicated"; debuff — "Server-side,
replicated, and it must survive the respawn"; all-dead — "The host must know every player's alive state and
broadcast the outcome."

### 1.1 The one-way rule

27 §1, verbatim:

> "every connected player's world follows the host's world — enemies and NPCs, where they are and what they
> do, doors, quest and world flags, bosses, trees — and never the other way round; loot stays per player;
> fast travel takes everyone."

The split (27 §1): **host** = "The first player to join … Its world is the session's world"; **peer** = "Its
world is overwritten by the host's. **Nothing of its own save is ever sent.**"; **peer actions** = "What a
peer does *during the session* … is a **request**: the host applies it to its own world"; **enemies** =
"The host's copy thinks. A peer shuts its copy's AI down and drives the copy from the host's table"; **fast
travel** = "Synchronised warp … No vote prompt."

68 makes that a *persistence* rule, after 66 broke it: "Doc 66 broke it on purpose, on the assumption that
'the two machines play the same save'. They do not: each player has their own co-op save (doc 62)." With
MERGE on, the host was offered "**every map entry the guest's own save held above the host's** — portals,
gates, quest counters, boss and NPC states the guest earned alone". 68 §2 replaces it: the guest's
pre-session save is **"History"** — "Never requested, whatever later reloads bring back of it"; in-session
changes become a request the host decides (`DmpWsHostAccepts`: "bosses/NPCs only forward, sets add-only");
a flag set *during a level load* is **"Recovered** (step 6): only a key the host has **never shown in this
session**"; requests survive a load ("they used to be cleared"); the host is retried "up to 5 times; only
the first 6 s hold the guest's value". (27 §1 says 9 s before the peer's world is put back; 68 §2 says 5
retries / 6 s — 68 is later, but neither retracts the other.)

The other half: the guest keeps its own world and gets it back — 68 §3, "Before this doc a guest that had
beaten a boss the host had only met came home to that boss 'encountered' again — the guest's own save lost
progress." Values are copied to `own`, filed as `DollsMP-own-world-<slot hash>.bin` bound to the slot name
and NG+ cycle, and "**Given back** … once the guest has had no host for 5 s, and on the next start if the
file is still there (crash in a session)."

### 1.2 How peer requests are gated

Three gates. **(1) Intent, not commit** — 12 §4.6 Rule E4: an intent "carries `based_on_rev` … The host
applies it only if `based_on_rev >= the revision of the last state it sent`", making double-pickup
"**structurally** impossible rather than unlikely". **(2) Per-category acceptance** — `DmpWsHostAccepts` (68
§2, §7c item 3); and critically "Only the guest → host path is filtered: the host's own writes and the host
→ guest push are untouched." **(3) Host-minted keys** — Rule E1b: "`(hostEpoch << 24) | hostSeq` … **Never a
hash, and never minted by two peers.**" Rule E6b adds "**the host may cheat** … every bound, clamp and
plausibility check is **client-scoped**"; Rule S4 the leg most mods lack — a write is applied "**only if its
generation is ≥ the receiver's current generation for that field**", because "ownership assignment plus a
parked brain still lets a stale packet from a former owner write."

### 1.3 Deliberately per-player

Read in date order — the policy reverses.

- **Loot per-player by design**: 27 §1 "Not synchronised, on purpose"; 37 B#17 "by design per player"; 45 §0
  "per player by design; unchanged."
- **64 proposed syncing it** (GAP-01/02/03 — item boxes, strong boxes, shop stock): "The single largest
  *real* desync area is **loot containers** (62 item boxes + 6 strong boxes in one measured map): both
  players can loot the same box and both keep the contents."
- **66 §0 cancels that**, overriding 64: "**Loot boxes, strong boxes and shop stock are NOT synced.** Each
  player loots and buys for themselves … **GAP-01, GAP-02 and GAP-03 of doc 64 are cancelled** — the current
  deterministic per-machine behaviour is correct, not a bug."
- **Economy per-player, as won't-fix** (64 §4): "**Each player keeps their own** inventory, money, build,
  tuning points and palettes" — 11 of the 18 unsent save fields (64 §2.1).
- **Dialogue choices are not per-player**: "dialogue choices are NAMES and are restored only when the session
  did not change them — no max on them" (68 §7c item 3). One event flag is per-player — 83 §2 excludes
  "`EventSH_fl002` ignored (per player)" — and 37 B#15 notes "a live conversation is one player's only".

---

## 2. The lane/blob architecture

**The most important caveat: 12's channel model was never implemented.** 64 §8 documents what ships. Read
both; do not mistake one for the other.

### 2.1 Specified channels (12 §4.1.1)

| Chan | Carries | Reliability | Ordering | Rate | Max msg | Prio |
|---|---|---|---|---|---|---|
| **R0** CONTROL | handshake, identity, desync + ledger digests, resync | reliable, ordered, idempotent (retransmit until acked; dedup by msg id) | total | event; keepalive 1 Hz, ceiling 5 Hz | 256 B | **0** |
| **R1** STATE | per-tick delta: transforms, vitals, modes, enemy rows — "coalesced, one datagram = all entities" | unreliable, latest-wins, N-fold repetition | none | 30 Hz (floor 30, ceiling 60) | formula ‡ | **4** |
| **R2** EVENT | ability activation, montage, loadout revision, ownership epoch, interact intent | reliable, per-author FIFO, exactly-once *effect* where idempotent | per-author FIFO | event; 20 Hz burst | 1000 B | **2** |
| **R3** WORLD | checkpoint set, item box taken, boss transition, drop take, death, damage outcome | reliable, unordered, idempotent — retransmit until acked; dedup by `EventId` | none; dependency optional | event; 10 Hz | 1000 B | **1** |
| **R4a** ADVISORY | mover phase, hold progress, destructible stage, door fraction | unreliable, latest-wins — "an older copy is **discarded**, never queued" | none | 1–5 Hz active, 10 Hz ceiling | 64 B | **5** |
| **R4b** COSMETIC | "**`E-FIRE` … and the cosmetic world toggle — that is all**" | unreliable, unordered, fire-and-forget; "duplicates suppressed by a per-author sequence + sliding bitmap, **not by delivery**" | dedup only | 30 Hz burst | 512 B | **6** |
| **R5** BULK | join snapshot, loadout blob, flag sets, ledger replay | reliable, in-order, chunked with per-chunk ack, resumable, cancelable | total | ≤1 chunk per RTT, ≤10 % of budget | 1000 B chunk | **7** |
| **R6** LIVENESS | ping/pong, keepalive, clock sync, RTT/jitter probe | reliable-ordered for clock sync; rest self-superseding | total | 2 Hz idle, 10 Hz measuring | 32 B | **3** |

‡ "R1's size constraint is a formula, not a constant … `N·(29·rows + 2) + 31 B ≤ 1200` ⇒ **at N=2 at most 20
entity rows per tick; at N=3, 13.** Beyond that the row set is **round-robined across ticks** — never
fragmented."

Priority: "**R0 > R3 > R2 > R6 > R1 > R4a > R4b > R5**". Two rules worth stealing: "**'Never dropped' is a
reliability property, not a priority property** … **Reliability says whether loss is possible; priority says
who waits.**" (12 §4.1.1); "any channel whose rate is set by a timer is a candidate for starvation; any
channel whose rate is set by a human is not" (12 §4.5.1b).

**Kind bytes and direction: the docs specify neither for R0–R6.** Direction is only implied (R1–R3/R4a
bidirectional, R5 host→guest); there is no kind byte because the layer was never built. That is a real gap
and I am not inventing it. Two internal inconsistencies in 12 to check before porting numbers: §0 says "**8
channels**" while §4.1.1 says "**Seven channels**" over a table of eight; §4.2 opens "**147 rows**" but
closes with `76+7+25 = 108`, while §0 says 158.

### 2.2 What actually ships (64 §8) — the real kind bytes

```
DMPW sections (dmp_world.h):
0x10..0x16  save containers EVENT/DISTRICT/CHECKPOINT/BOSS/NPC/DIALOGUE/CHAIN
0x30 PLACE  0x31 TRAVEL  0x32 DEAD  0x33 DOORS  0x34 PROPS  0x35 RTPROPS
0x36 MOVERS 0x37 RULES(host [Rules]) 0x38 RESET 0x39 CHOICES 0x3A BUILD
Suggested: 0x3B LOOTBOX, 0x3C STRONGBOX, 0x3D SHOPSTOCK, 0x3E ABNORMAL, 0x3F MACHINESTATE

DMPE kinds 1–9: MONTAGE, CALL, ABILITY, VARS, SPAWNED, REFS, PART, SHOTS, SHOTDEF
DMPP kinds 1–7: CALL, MSG, MSGEND, PURGE, THROW, SHOTDEF, VITALS
Form markers: 0 humanoid, 1 machine, 2 transition, 254 no body, 255 peer gone
```

Envelope limits and cadence — the only place all five are tabulated (64 §5):

| Lane | Cap | Cadence |
|---|---|---|
| `DMPB` player record | 163 B frame / 145 B body | 60 Hz |
| `DMPW` world blob | 61,440 B | re-sent ~2 s |
| `DMPE` entities | 8,192 B | ~20 Hz |
| `DMPP` player effects | 8,192 B | event; 56 effects/frame, 8 vitals |
| `DMPL` look | 8,192 B | event |

**Chunking:** 27 §1 — "an opaque, checksummed blob in a file; the sidecar chunks it"; the blob is "re-sent
whole every 2 s (up to 60 KB)" (45 §D1). There is **no fragmentation** (12 F-012), and 12 H-6: "**Never
fragment an unreliable stream** … must **not** be fragmented but *split across ticks*" (92.4 % effective loss
at 256 fragments). Registry reliability letters are `M` must-arrive, `B` best-effort latest-wins, `E`
exactly-once effect (12 §4.2) — under 12 H-1: "**`reliable` does not mean `ordered`** in this codebase."

---

## 3. Bandwidth and rate budgets

> "In-map the client sustains `pass ≈ 43/s, gt ≈ 39/s, tx ≈ 38/s, rx ≈ 32/s` … **the binding constraint is
> ~32–43 state-record applications per second, not KB/s.**" — 12 F-019 (`L1`)

| Case | Number | Source |
|---|---|---|
| One player, N=2 / N=1 @30 Hz | 93 B → **2.79 KB/s** / 62 B → **1.86 KB/s** | 12 §4.4 |
| 9 enemies, N=2 @30 Hz | 557 B → 16.7 KB/s | 12 §4.4 |
| **Boss fight, `N_ENEMY`=24** | host uplink **25.6 KB/s**; **≈29.6 KB/s both directions** | 12 F-028 |
| Same, enemies at N=2 | ≈47.6 KB/s — needs two datagrams/tick | 12 F-028 |
| Current byte-aligned record | 148 B `DMPC` ⇒ **4.44 KB/s @30 Hz**; 159 B `DMPB` ⇒ **9.54 KB/s @60 Hz** | 12 §4.4 |
| Progression / flag bank | ≤ **0.4 KB/s** = "**1.6 % of the total**" | 12 §4.4 |
| Sharding the flag bank instead | `17 × 64` = 1088 B/tick = **32.6 KB/s** | 12 §4.4 |

Per-entity 29 B/tick, per-datagram overhead 31 B. (12 §4.4's total line says "≈33.0 B *(29.0 + 4.0 for
`sample_ms`)*" while its own tables use 29 — the stale-constant class the doc itself catalogues.) Two
non-bandwidth ceilings bind first: the **datagram cap** ("at most … **20** enemy rows … at N=2") and the game
thread — "24 enemy rows in **one** `DeltaPacket` = **30/s** … As 24 separate records it is **720
applications/s — 18× the ceiling**". No congestion control, by design: a "fixed ladder with hysteresis, not an
integrator" replaces it (12 §4.5.5).

**What is broken is timing, not loss.** 12 §0: the bridge costs "**latency, not accuracy**: a stable **~150–161
ms** one-way offset"; "**Loss is not the error.** At 0 ms jitter the error goes 0.04 cm → **0.83 cm** from 0 %
to 40 % loss. At 80 ms jitter it goes 102.16 cm → **98.66 cm**, i.e. *down*. Jitter is the entire error term."
And a hard floor: "At **20 Hz** with 40 ms jitter the error is **1457.91 cm** … Gaffer's *sourced* rule is a
playout delay of **3× the send interval** … this project uses a **fixed 100 ms** … **R1's rate floor is
therefore 30 Hz, not a tuning knob.**"

---

## 4. "State, not events", idempotence, and at-most-once

> "The event channel is the **fast path**; the state channel is the **authority and the repair.** … every
> failure degrades to 'temporarily stale', never to 'fired twice'. The worst case is staleness of ≤ 2 s; the
> worst case is never a double application." — 12 §4.6 Rule E8

with the constraint that makes it work: "**the state channel must carry the full truth, not just what changed
recently.** A repair path that only carries deltas cannot repair a peer that missed the baseline."

**Rule E2** requires every effect be "a **set insertion** … an **absolute assignment** … or a **transition
guarded by a monotonic counter**", then: "**An event whose effect is `x += d` is forbidden on the wire.**"
Precedent: TiltedEvolution applied `newHealth = GetActorValue(kHealth) + DeltaHealth` on both peers "and it
diverges permanently the moment one application is lost or doubled." Strong form: "a one-shot is a set-insert
on an id, never a toggle, and correctness must not depend on the receiver's memory … a receiver with **no
memory at all** … is still correct, and the ledger becomes a pure optimisation."

**Why E2 is mandatory here** — 12 §0: "**Rule E2 (structural idempotence) is load-bearing *precisely because*
Rule E6 (producer suppression) cannot be fully implemented.** `RegisterHook` **cannot cancel** a game call".
So "the client's local game may already have opened the chest, and the apply is a no-op if the id is present."
Rule E5 adds that anything periodic "is sent as **`(start_stamp, duration, params)`** … *'An accumulator is
never streamed, it is anchored.'*"

**At-most-once, precisely (12 F-017):**

> "`Session::accept_event(key) -> bool` is a `BTreeSet<u64>` … capped at 4096, evicting the **smallest** key —
> correct only if keys are minted monotonically. The relay **drops** the event when it returns `false` … An
> event that never arrives is never re-requested, never acked, never reconciled. The test … proves 'the boss
> must die exactly once' only in the direction of *not twice*. Separately, the key is a plain `u64` chosen by
> the **sender**, so a hostile or buggy peer can *suppress* a legitimate event by pre-claiming its key."

Fix (Rule E7): "Exactly-once *delivery* is impossible … What is achievable … is **exactly-once effect**:
at-least-once delivery (retransmit until acked, R3) + structural idempotence (Rule E2) + reconciliation (the
join-time ledger digest and the periodic digest on R0)." E1b's eviction argument is the transferable detail:
"Any scheme that lets two peers mint keys in the same space breaks this … a host's live keys are evicted first
and a legitimate one-shot can then be re-applied."

---

## 5. Blockers and remaining work

**19 — verdict:** "**`A1` IS CLOSED and `A2` is CLOSED. NO BLOCKER REMAINS on the death feature.**" §4-A
("BLOCKS CODE") is "**EMPTY … blocked by UNWRITTEN CODE, not by a question**". What remains is downgraded, not
deleted: §4-B offline (a stale figure — "the replication layout has **43** fields; it is **42**"), §4-C
non-blocking runtime, §4-D two players. Sharpest open item D0: "**WHOSE MACHINE RELOADS?** … If the host's
GameMode also handles a CLIENT's death, then one client death reloads the HOST's world and the entire session
resets — which is not a regression, it is a broken mod." Q-19-05 goes to the owner because the written rule
became unbuildable: "**F-005 establishes the 'no reload' half cannot be built**". Recommendation: "**Build
HOST/JOIN first** … the **foundation** … **unblocked**", then vitals.

**64 — verdict:** "The implementation is **further along than its own notes claim**." World/progression is "**10
of 28** fields, and every field that describes the world is in that 10"; of 18 unsent, "**11 are the player's
own economy/appearance** … exactly **3 are world-shared state**". §10's five phases: decide the economy policy
→ close world-sharing holes (01/02/03/05) → observed field failures (07 boss hidden flag, 06 `RESPAWNING`→wipe,
09 key on `CharacterId`) → status and death (04, 08) → **the gating four**: GAP-25 build refusal ("the mod
**refuses co-op** on an unsupported exe"), GAP-26 ("every timing number in the corpus (≈0 ms latency) is
therefore optimistic"), GAP-27 NAT, GAP-28 ("The wire is plaintext and unauthenticated … **Do not ship a 'play
with a friend over the internet' build before this.**"). §11: "What remains is a **thousand bytes of
loot-container state**, a **two-way merge for two existing sections**, a **status lane that replaces event
replay**, and the **death penalty put back on the game's own rails**."

**66 — and 68 overturns its first item.** 66 §0's owner decisions override 64 (loot/shops not synced; enemy
targeting unchanged; death debuff unchanged, "`UDAGameInstance::DeathPenalty` is not to be used"). W1 "Guest
progress reaches the host (DONE, unit-tested)" was **replaced by 68**, which reverted the merge. W3 boss
visibility: "**MEASURED, and there is nothing to fix**"; W4 doll state (thrusters, hit flash, overheat) done;
W5 — checkpoint first-access "ALREADY COVERED", rear/utility reloads "CLOSED: there is no such reload",
cutscene scrub "plumbing proven, scrub not exercised". Read 66 with 68 open: a handoff doc's "DONE" decays
faster than its measurements.

**83 — questline:** paused after pass 1/2 and one replay on 1.3.1. 169 steps across ten NPCs "**0 / 0**"
differing, SH "**31 / 0**", ED "**3 / 0**", "application errors, crash folders: none"; the one real finding
was closed as an artefact (§7); coverage 25 of 190 talks.

**37 / 45 — the gap audits.** 37 §A lists ten session-breaking gaps (2026-10-08): two-PC never possible ("the
relay is started on `127.0.0.1` only"); enemy montages/abilities/BT calls not crossing; run-time spawns and
bosses never executed; "13 of 13 deaths: `COOP DEATH … peer=UNKNOWN session=NOT LIVE`"; no shared health. 45 §0
scores them: most **done**; the stubborn remainder is two-PC play, `map_token` never written, "characters known
by name", session edges, and "everything was measured at about 0 ms latency". 45 §C closes one outright: "Does
the game pause when a player opens the menu? **No**", with the measurement.

---

## 6. Generalisable lessons, quoted

**On claiming** (`_PROTOCOL.md` §1, §7): "**Never claim more than your evidence supports.** Every claim carries
an evidence tier" (L1 measured on this build / L2 vendored source / L3 web with a link / L4 inference / L5
unverified) — "A claim with no tier is a defect"; "**the document IS the work's durable memory**"; "Negative
results are first-class evidence." Then:

- "**An absence may only refute a SPECIFIC NAME, never a BEHAVIOUR.** … it concluded 'there is no respawn
  policy' from the absence of `bRespawnOnRest` — and the property is called **`bDontRespawn`**."
- "**before you write a negative, name the instrument and state what it can detect.** If the answer is 'not
  this kind of thing', you have no result." Also: "**A 100 % coverage figure next to open questions is a red
  flag, not a good sign.**" And "**Check the directory, not the report.** A subagent's closing summary is a
  *claim* about what it wrote; the evidence directory is the *fact*."

**On verification** (ibid):

- "**AN ACCEPTANCE TEST MUST BE ABLE TO FAIL — AND MUST CHECK COMPLETENESS, NOT MERE EXISTENCE.**" Four
  instances, "every one reported success while being wrong"; one save parser's test was `len(p) >= 1`.
- "**DESIGN THE TEST SO THE CONFOUND CANNOT PRODUCE THE SAME OUTPUT AS THE EFFECT** … ask *'what would this
  look like if the thing were absent, broken, or a different object?'* If the answer is 'the same', the test
  cannot answer the question and needs redesigning — not running." Related: "A probe that 'runs fine' and
  writes nothing has usually failed silently" — "**`io.open(path, "w")` cannot create directories**."
- "**the dominant error mode in this programme is not a wrong NUMBER, it is a right number applied at the
  wrong ADDRESS**"; "a constant can AGE … stale *without becoming wrong*, which is why nobody notices."
  Also "**Validate BEFORE you write** … write-last protects against a crash in your code, not against bad input
  you supply."

**On design:**

- "**PREFER STATE WRITES OVER INTERCEPTION — because state writes fail SAFE and cancellation fails OPEN** … a
  state write is idempotent and repeatable … A cancellation is a one-shot at one instant." Order: "1. **Remove
  the trigger** … 2. **Do not author the effect** … 3. **Change the state it reacts to** … 4. **Reconcile
  afterwards.**" — `_PROTOCOL.md` §7
- "**WHEN YOU FIX A TIMEOUT BY ADDING A BRACKET, THE BRACKET NEEDS ITS OWN EXPIRY** … **Every bracket must have
  a deadline, and exceeding it must be an error by name, not a silent extension.**" — ibid; "A bracket suspends
  the deadline; it never extends it" — 12 §4.1.5
- "**WHEN A FINDING SAYS A LIST IS WRONG, REWRITE THE LIST — DO NOT EDIT ONE ENTRY IN IT.**" — ibid
- "**Never destroy a puppet on silence.** … Freeze at 2 s, hide at 30 s, keep the actor valid." — 12 H-4
- "**A version *value* is not a version; a *(layer, value)* pair is.** … never advertise a version you do not
  enforce." — 12 H-13

**UE4/UE4SS-specific — the highest-value block for a different UE4 game** (`_PROTOCOL.md` §7 unless noted):

- "**DO NOT CALL A REFLECTED FUNCTION WHOSE SIGNATURE CONTAINS AN `FName` … It is PREDICTIVE, which is what
  makes it worth memorising**" (`K2_AttachToComponent` crashes; `K2_SetActorLocation` ok ×85).
- "**NEVER bulk-enumerate properties on a live object. Use `CXXHeaderDump` instead.** This rule has cost **five
  crashed runs** … **The failure is non-deterministic**, so 'it worked when I tried it' is not evidence."
- "**NIL-CHECK EVERY OBJECT BEFORE READING A PROPERTY OFF IT — AND CHECK THE ADDRESS FIRST, BECAUSE THE
  OBVIOUS CHECK IS ITSELF A DEREFERENCE**": `nil → GetAddress() ≠ 0 → IsValid() → GetFullName()`; "**`pcall`
  cannot catch it**."
- "**AT RUNTIME, USE THE REFLECTED NAME — THE `A`/`U` PREFIX IS STRIPPED. This is the single highest-cost trap
  in this programme.** … A name that is right in C++ and wrong at runtime fails without an error message."
- "**NEVER call a *generated Blueprint* UFunction from UE4SS Lua** … Calling **native** `AActor` methods on a
  Blueprint *instance* is fine." And "**DO NOT FIX A CLASS-NAME BUG WITH A STRING COMPARISON — RESOLVE THE
  CLASS OBJECT ONCE AND COMPARE OBJECTS.**"
- "**A field that reads a component must state WHICH component, and what happens when it is absent** …
  reading a missing component yields a default rather than an error."
- "**a diagnostic must not be able to kill the process, and a probe that runs on a loading screen must not
  assume gameplay.**" — 20 F-017; "**Reachability of the page is not liveness of the object.**" — 20 F-006
- Facts that keep paying: `bHidden` is not replicated and a hidden actor still collides (20 F-001/F-004); the
  game's own death→despawn→respawn "already replicates correctly" (20 §0); the game has no pause — "Game time
  went 17.37 s → 21.85 s over 4.5 s of wall time; `IsGamePaused` 0" (45 §C); the doll "survives death with an
  unchanged `UObject` header at `+55 s`, unpossessed but alive" (19 F-012); a hidden boss still runs `BootAI`
  unless suppressed (83 §9).

**On process.** "`BLOCKED` is a real status and is not a failure" (`_PROTOCOL.md` §5). "**NEVER write a
`docs/sync/*.md` file through PowerShell text cmdlets** … this console's code page is **CP932** … 345
non-ASCII sites corrupted." "**State the regex, the case flag and the file count — or a negative is not
evidence**" (case-insensitive `\bAck\b` → 472 hits, all tails of "Attack"; case-sensitive → 0). "**Anchor edits
on WHOLE lines or whole rows, never on a prefix** … After any structural edit, re-check the shape." "**A
claim's DIRECTION and a claim's STRENGTH are separate properties — state both.**" From 19: "**A REGISTER CAN
MANUFACTURE BLOCKERS** … **A question is not open merely because *this document* has not answered it**" (H-4).

---

## 7. Quest/progression sync (doc 83)

**What must be synced** — the save object's seven flagged containers (64 §2.1 offsets): `EventParams`
`0x0650`, `ReachedDistricts` `0x06A0`, `ReleasedCheckpoints` `0x07F8`, `BossStates` `0x08D0`, `NPCStates`
`0x0920`, `ReadDialogues` `0x0970`, `ReadDialogueChains` `0x09C0`, plus the dead list `0x0880`. All are among
the 10 synced fields. 83 §2's acceptance test is the reusable part:

> "After each step both saves are compared: event params (cat 0), read marks with StateName (cat 5), NPC
> states (4), chains (6); `EventSH_fl002` ignored (per player)."

And its honest limit, 83 §5: "**Placement and survival, not just the save.** The guide's steps move and kill
NPCs … These come from the shared keys at level load, so equal saves are necessary but not proof."

**How quest items and rewards are granted: not established by 83, and it says so.** 83 §5: "**Rewards/items**
per step (Pearl, Bionic Parts, weapons): the test compares saves, not inventories." What the other docs
establish is narrower: the boss reward volume pays per machine and per fight (68 §4, "each machine's own boss
volume pays its reward"); a replayed win is suppressed by emptying the volume for the length of the original
call (68 §7c item 4 — "guest `this win pays the money, NOT a second copy of its items`, money +100000, no
`MainKey03`"); and a world-sync reward must not double-pay (68 §7).

**Failure modes actually found.**

1. **A stale own-world file manufactured the only quest "bug".** 83 §4 recorded six peer-only flags
   (`M01A01_Portal_01/02`, `M01A02_Portal_02`, `M01A03_Portal_02`, `M02A01_Portal_01/02`). 83 §7: "**Shara
   verdict: not a quest bug and not lost progress.** Talk040 writes no event at all … the six flags came from a
   stale `DollsMP-own-world-*.bin` of an older test, which `saverestore` left behind." Reproduced on purpose:
   "6 of 6 flags back during a 20 s drop, saves equal 2 s after the link returned." **Lesson: the own-world
   restore can fabricate desync during a real disconnect.**
2. **Different save slots on the two instances** — "instance1 `5W0K...`, instance2 `DN_0`; host 24-25 item
   kinds vs peer 6-8" (83 §9). Any cross-machine save comparison is void until checked.
3. **A save that is never written** — "`call gm Save bStartFromRegressionPoint=b:1` **writes no save in a
   battle level** (measured by file mtimes)".
4. **A rig verb swallowing its arguments** — "The F10 voice-chat `talk` verb … printed the voice status and ran
   nothing, so every earlier 'talk' check driven that way was empty."
5. **Gates forced, not honoured** — "All steps were forced with `activate`, regardless of the game's conditions
   (items held, other NPC dead, boss killed)."
6. **Lag mistaken for desync** — "one dialogue read mark seen on one machine a moment before the other … the
   same step reads *agree YES* a poll later. Lag, not a fault."

---

## 8. What these docs do *not* contain

No per-lane direction or kind byte for R0–R6 (never built); **no measured two-machine latency, loss or jitter
anywhere** (45 B6, 64 GAP-26: every number is ≈0 ms, one PC); no inventory-level verification of quest rewards
(83 §5); no encryption or authentication design beyond "a shared secret … and the payload is at least
integrity-checked" (64 GAP-28); no anti-cheat story — "the mod is a proxy DLL" is a stated limitation (64 §4).
