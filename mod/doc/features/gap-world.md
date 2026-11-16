# gap-world — fast travel / rest points, rest-and-enemies, puzzle intermediate state, shops, chest counter

**Status: built, unit-tested, and partly live-measured.** The lane runs in a real two-instance
session and the two machines agree about the unlock sets (evidence in §6). The *cross-machine*
halves of items 2/3/4/5 were not exercised, so **both switches ship `0`** — see §6 for exactly
which line is missing and why.

Scope comes from `COVERAGE-MATRIX.md` §3 GAP items **3, 4, 6, 7, 8** and
`CAMPAIGN-ROUTE.md`. The reservation was wire Kinds **0x60–0x63** and intents **17–18**.

---

## 1. What was measured before writing code

Nothing here is guessed; each rule below cites the decode or the disassembly it rests on.

| fact | evidence |
|---|---|
| `FS_SaveSlot_C` carries `UnlockedTeleporters`, `SavePointsVisited`, `ShopTeleporterDestinations` as `Array<Object>`, and `LastTouchedCheckpoint` as `Object` | `analysis/04-sync-surface.md` §2.4 decodes all 30 save properties out of a real `World0.sav` (`SavePointsVisited` = `Array<Object>`, 1 entry; `LastTouchedCheckpoint` = `Object` = `GR_Camp_Save`); `analysis/pak/blueprint-surface.md` §`FS_SaveSlot` lists the same names from the cooked class |
| a **rest clears every enemy record in that machine's save** | `Checkpoint_C` (`LD45/Blueprints/Checkpoint`) imports `ClearAllEnemySaveStates`; its body at `0x00E27460` is `TMap::Reset` on `save+0x270` (`analysis/14-combat-save-answers.md` D4-p1/D4-p7) |
| `Teleporter_C` owns its own unlock call | its imported-C++ list carries `Array_AddUnique` / `Array_Contains` / `Array_Get`, and its UFUNCTION exports include the `RegisterSphere` component-bound begin-overlap event (`analysis/pak/blueprint-surface.md` §`Teleporter`) |
| the 287 `Lib_Book_*` puzzle props have **no state at all** | `RuinsLevel/Library/props/Lib_Book_00_BP.uasset` decodes to 47 names / 6 exports, parent `FS_StaticMeshActor_C`, and **no state property and no function**: every export is `BlueprintGeneratedClass`/`SceneComponent`/`SCS_Node`/`SimpleConstructionScript`/`StaticMeshComponent` (`analysis/pak/code/blueprints.py`) |
| the rest of the puzzle actor set is already on a lane | decoded parents: `AQR_KeyRecepticle_BP_C`, `AQ_OfferingPoint_BP_C`, `RuneButton_C`, `SipehaRune_BP_C`, `Jigeki_Rune_BP_C`, `Puwema_Rune_BP_C`, `Wodaso_Rune_BP_C`, `PTSeal_SipehaRune_BP_C` → `SavableActor_C` (lane `0x12`); `Library_RotateBridge_C` → `SavableActor_C` + `Elevator_Base_C` (lane `0x26`); `Library_LowerBridge_C` → `Activatable_C` (lane `0x2A`, `SH_ACTIVATABLE`); `SG_Door_Beepee_C`, `AqrStatuePuzzleDoor_C` → the door family (lane `0x24`/interact) |
| shop stock keys are NSD properties **inside** a composed bank key | `analysis/23-quest-progression-sync-map.md` §4 (`ShopProgressionKey` 29 assets, `ShopPurchasedKey` 16 assets, `ShopAmountSpent`/`ShopkeepUnlock*` as NSD fields); `analysis/24-quest-closeout.md` §4b (`Merchant_CheckForDucks` reads `ShopProgressionKey/…`, `Merchant_CheckForBoughtALot` reads `ShopAmountSpent`) |

Two conclusions the earlier survey left open, now answered:

* **"does a book expose one property the lanes can carry?"** — No. It exposes none, so the
  intermediate state has to be carried as a transform.
* **"what does a rest do to enemies?"** — It clears the whole `EnemySaveStates` table on the
  machine that rested, because `Checkpoint_C` calls `ClearAllEnemySaveStates`. It is a world
  event, not a UI action.

---

## 2. Design (what is synced, authority, keys, wire)

| item | synced | authority | key | wire |
|---|---|---|---|---|
| 1 fast travel / rest points | the **union** of `UnlockedTeleporters`, `SavePointsVisited`, `ShopTeleporterDestinations` | host, monotone union (never removes) | the element's **short asset name** (`GR_Camp_Save`, `Teleporter_400_AUT`) | `K_WORLD_SET` 0x60, chunked cursor |
| 1 (not synced) | `LastTouchedCheckpoint`, `SavePointID`, `SaveLocation`, `SaveLevel`, `SavedArea`, `CurrentPlayerHealth` | **per player, by rule** | — | never on the wire |
| 2 rest and enemies | "a rest happened" → the same enemy reset on both; bosses/one-time enemies kept dead | host (a peer's rest is a request; the host applies and orders) | rest sequence + point name | `K_REST_WORLD` 0x61 |
| 3 puzzle intermediate state | the rotation of a placed puzzle prop | **last-writer-wins per key** (either player may turn it) | `<world>/<level>/<name>` hashed with seed `'QDWS'` | `K_PUZZLE` 0x62, chunked |
| 4 shops | nothing new — a **filter** on the flag lane | n/a | property-name suffix | reuse `K_WORLD_FLAGS`/`K_INTENT`, keys filtered out |
| 5 chest counter | the counter as a **delta** | host adds | flag name | reuse `K_INTENT` `I_SET_FLAG` with **mode 0 (additive)** |

`LastTouchedCheckpoint` is the one field the brief calls out as "per player — do NOT mirror",
and it is the one thing a naive "mirror the save's object fields" pass would have broken: the
respawn point is what a load restores, and moving the partner's respawn because they walked
past a checkpoint is worse than the gap it would close. `worldset::IsPerPlayerProperty` is
the single list the engine half consults, and it matches exact names (not prefixes) so a
future field that merely starts with an excluded name is not silently dropped.

---

## 3. Files

### Created (owned)

| file | what |
|---|---|
| `mod/native/src/qdworldset.h` | pure: the union board, the wire encoding for a set, the per-player exclusion list, the additive-counter rule, the local-shop-stock rule |
| `mod/native/src/qdrest.h` | pure: the rest episode (dedupe + ordering), the boss/one-time exemption rule, the reset plan, the wire body |
| `mod/native/src/qdpuzzle.h` | pure: the per-key last-writer-wins board and the row encoding |
| `mod/native/src/qdworld_engine.h` | the engine half: save-array access, publish/apply, the rest reset, the puzzle observe/apply, the counter marks, the `qd.world` lever |
| `mod/native/test/test_gapworld.cpp` | 131 assertions over the three modules |
| `mod/tools/session-gapworld.ps1` | the two-instance proof run (installs the build, seeds both instances, drives the switches and the `qd.world` lever, prints PASS/FAIL per claim) |
| `mod/doc/features/gap-world.md` | this file |

### Shared files changed (small, targeted, under the BUILD lock)

| file | change |
|---|---|
| `mod/native/src/qdwire.h` | `K_WORLD_SET` 0x60, `K_REST_WORLD` 0x61, `K_PUZZLE` 0x62 added to `enum Kind` and to `IsReliableKind` (all three are one-shot facts) |
| `mod/native/src/qdconfig.h` | `world_set_sync`, `puzzle_sync` switches + their INI keys + the boot line |
| `mod/qdcoop.ini` | the two switches, documented beside the others |
| `mod/native/src/proxy.cpp` | `#include` ×3 (pure headers, near the other includes), one forward-declaration block, four `DispatchInbound` cases, the local-rest hook in `RestObserve`, the shop-stock filter + the counter-delta mode in the flag lane, one `worldsync::Tick` call in the frame pump, `#include "qdworld_engine.h"` beside `qdrespawn_engine.h` |
| `mod/rust/qd-core/src/protocol.rs` | the three kinds in `Kind`, `from_u8`, `is_reliable` (byte-compatible; no existing encoding touched) |
| `mod/doc/WIRE.md` | §3.31–§3.33 |

No existing wire encoding changed, so `doc/vectors.txt` is untouched and both test suites stay
byte-compatible.

---

## 4. Coverage

### Item 1 — fast travel and rest points

**Handled (generic, not per-actor):**

* every element of the three `Array<Object>` members, whatever asset it names, because the
  lane iterates the array rather than naming destinations. The shipping world places
  **10 real fast-travel teleporters**, **31 checkpoints** outside the dev/UI maps and the
  **3 shop mini-teleport pads** (`analysis/pak/answers.md` Q3); the union board is bounded at
  128 entries per set, above the 43 teleporter placements and 34 checkpoint placements in the
  whole pak including `BirdsTestZone`/`VideoConfigMap`.
* the counters `TeleporterUnlockCount` / `CheckpointUnlockCount` are **derived from the set
  size** and raised whenever the set grows, which is what makes
  `cha_Challenge_AllTeleporters` (10 teleporters **and** 32 checkpoints) reachable for the
  pair. They are deliberately *not* additive: two players unlocking the same destination is
  one destination.

**Deliberately not handled:** `LastTouchedCheckpoint` and the save-identity fields beside it
(§2). A fast travel is still followed through the **existing** travel lane
(`qdtravel.h` / `TravelSync` 0x23) — see §5.

### Item 2 — rest and enemies

**Handled:** a rest on either machine resets the ordinary enemy set on **both**; bosses,
one-time spawners and flag-gated spawners are snapshotted before the clear and written back
with `StoreEnemySaveState(guid, -1, 0)` after it, so a rest cannot revive them on either
machine.

**Limitation, stated:** the exemption test reads the spawner for a boss-ish byte
(`Boss`/`bBoss`/`IsBoss`/`IsABoss`) and for `RequireNamedSaveDataToSpawn`, and exempts the
key the boss bar currently holds. The **Shrine Boss** writes no content flag at all
(`COVERAGE-MATRIX` §4 item 8), so if its spawner exposes none of those properties it is only
protected while its bar is up. The generic fallback is the `<Boss>Dead` flag the other seven
bosses write, which the flag bank carries and never lowers.

### Item 3 — puzzle intermediate state

**Handled:** any placed actor whose class matches `Lib_Book_` (the 287 `Lib_Book_00..04_BP_C`
placements, `Lib_Book_Open_BP_C`, and the RoS copies) plus `SuperCubePuzzleDoor`. The state is
the actor's rotation, which is the only state those classes have (§1).

**Not handled here, because another lane already carries it:** the rune buttons, the pedestals,
the statue-puzzle door and both library bridges are `SavableActor_C`/`Activatable_C`/`Door_Base_C`
descendants, whose state is a GUID-keyed or bool state the existing `0x12`/`0x2A`/interact lanes
already sync. The completion flags (`LibBookPuzzleComplete`, `ROSBookPuzzleComplete`,
`StoneGardenPuzzleFinished`) stay on the flag lane, which never lowers them.

### Item 4 — shops

**Rule implemented:** `ShopPurchasedKey`, `ShopPurchases`, `ShopSales`, `NewItemsInShop` **never
cross in either direction**. Each player buys for themselves with their own coins, and buying the
last unique relic does not remove it from the partner's shelf. What still crosses: the pair's
shared shop progression (`ShopUnlockFinished`, `ShopkeepUnlock1..4`, `ShopProgressionKey/*`) and
`ShopAmountSpent` (item 5's additive rule), because `cha_Quest_Shop_Spend` /
`cha_Quest_Shop_Unlocks` are goals for the pair.

### Item 5 — `TotalChestsOpened`

**Finding: it does diverge, and by construction.** A chest the partner opens is applied on this
machine by the object/actor lane **without its payout** — `qdobjectstate.h` says so explicitly
("the global counters are NOT touched here … incrementing them a second time on the receiver is
exactly the double-count this comment exists to prevent"). So each machine counts only the chests
its own player opened, and the flag lane's monotone-absolute rule converges them to `max(a, b)`.
Two players opening five different chests each see **5**, not 10, and `cha_Challenge_AllChests`
= 108 is unreachable together.

**Fix:** `TotalChestsOpened`, `TotalSecretDoorsOpened` and `ShopAmountSpent` travel as **deltas**
and the host **adds** them. The apply path is the flag bank's own additive mode
(`ApplyIntent(..., absolute=false)`), so no new intent kind was needed. A given-up delta is
rolled back into the mark so the next rise re-sends the accumulated total rather than losing it.

---

## 5. The fast-travel follow question (item 1, second half)

The brief asks: "a fast travel by one player must take the partner along through the existing
travel lane (`qdtravel.h`, `TravelSync`) — verify that, or define the rule."

**Rule (verified in the code, `TravelSync` 0x23):** `LocalTravelDest()` reads the pawn's
`CurrentTeleporterTarget` / `TeleportLocationTarget` / `TempTeleporter` / `TeleporterStartActor`,
requires the carrier to have a `LocationData` property, and publishes `<actor>/<world>`; the
receiver resolves the same placed actor by name and runs `FS_PlayerChar.TeleportToLocation`.
That is exactly the shape a teleporter-menu fast travel takes, so a fast travel **is** followed —
by a mechanism that does not depend on the follower having the destination unlocked, because the
follow calls the actor's own teleport rather than the menu. The unlock set (item 1, first half) is
what makes the follower's own menu list the destination.

`TravelSync` remains its own switch and is not changed by this work.

---

## 6. Proof

### Built and unit-tested (this pass)

```
cl /nologo /std:c++17 /O2 /EHsc /W4 /MT ... test_gapworld.cpp
  131 passed, 0 failed
```

Zero warnings at `/W4`. The tests pin the rules that fail silently in a game: the union is
monotone and refuses a path/space/separator as a name; `LastTouchedCheckpoint` is excluded and a
prefix look-alike is not; a counter that counts distinct events is a sum, not a max; a stock key
never crosses while the shared progression does; a rest is one episode however many times the
checkpoint fires it, an older episode never resets again, and a boss is exempt; a puzzle prop is
last-writer-wins per key and the machine that applied a row does not publish it back.

Three of the tests failed on the first run and each was a **real** defect, not a test typo:
`WriteSetChunk` echoed a past-the-end cursor instead of clamping; the rest episode measured its
de-duplication window from the last *observation* rather than the episode's start, so a
checkpoint that refreshes `PlayerRested` could have suppressed a second real rest for ever; and
`rest::Encode` accepted a flag byte its own `Decode` refuses.

### Live two-instance evidence

Two real sessions were run (`mod/tools/session-gapworld.ps1`, report at
`_test/gaprun/out/report-gapworld.txt`). What they **did** show, quoted:

**The lane is alive and the two machines agree about the unlock sets.**

```
qdcoop-qd1.log (HOST)  3:20:14.590 INFO switches: flags=1 ... worldSet=1 puzzle=1
qdcoop-qd2.log (PEER)  3:20:24.874 INFO switches: flags=1 ... worldSet=1 puzzle=1

qdcoop-qd2.log (PEER)  3:20:59.988 INFO worldset: teleporters holds 1 entry in this save
                       3:20:59.988 INFO worldset: sent teleporters rows 0..0 of 1 rev=1
                       3:20:59.988 INFO worldset: sent savepoints rows 0..0 of 1 rev=1

qdcoop-qd1.log (HOST)  3:21:36.426 INFO worldset[teleporters]: save=1/2 union=1 rev=1 applied=0 refused=0
                       3:21:36.426 INFO worldset[savepoints]:  save=1/2 union=1 rev=1 applied=0 refused=0
                       3:21:36.426 INFO worldset counter TeleporterUnlockCount = 1 (set size 1)
                       3:21:36.426 INFO worldset counter CheckpointUnlockCount = 1 (set size 1)
qdcoop-qd2.log (PEER)  3:21:44.763 INFO worldset[teleporters]: save=1/2 union=1 rev=1 applied=0 refused=0
                       3:21:44.763 INFO worldset counter TeleporterUnlockCount = 1 (set size 1)
                       3:21:44.763 INFO worldset counter CheckpointUnlockCount = 1 (set size 1)
```

`save=1/2` is `Num=1, Max=2`: the save's array has **one spare slot**, which is the in-place
append path of §4 — and the numbers are identical on both machines, including the derived
counters.

**`K_WORLD_SET` crossed the relay and was acked.**

```
qdcoop-qd1.log (HOST)  3:24:32.954 DBG reliable kind=00000060 seq=35 received; ack sent
qdcoop-qd2.log (PEER)  3:24:40.454 INFO flag NEW TeleporterUnlockCount = 1
                       3:24:40.454 INFO flag intent queued TeleporterUnlockCount=1
                       3:24:40.454 INFO flag intent sent TeleporterUnlockCount=1 rev=4 try 1/5
```

**A rest runs the game's own enemy reset, in process.**

```
qdcoop-qd2.log (PEER)  3:21:40.154 INFO rest: world reset (the rig's qd.world=rest) seq=1
                            -- ClearAllEnemySaveStates ran; 0 ordinary enemies respawn here,
                               0 boss/one-time death(s) kept
```

So the mechanism item 2 depends on (the native `ClearAllEnemySaveStates` call through
`0x00E27460`, plus the exempt-death write-back path) executes and reports. **`0 boss/one-time
death(s) kept` because the rig's hub start had no enemy records at all**, so the exemption
branch was not exercised.

**Shop stock did not cross.** Zero `flag CHANGE …ShopPurchasedKey` lines on the host and zero
`flag intent …ShopPurchasedKey` lines on the peer across both sessions — but no purchase was
made, so this is "nothing crossed", not "a purchase was made and stayed local".

**NOT live-proven, and exactly why.** The **cross-machine** half of item 2 was not observed:
the first session's `qd.world=rest` hit a lever bug (it applied the reset locally and never
sent it — found by this run, fixed in `DevPoll`), and the second session's two instances
stalled on the title screen (`dev autoload: Load Slot(0) called`, then `world: local=title_screen`
and no further frames) before the corrected lever was ever polled. Items 3 (puzzle row
applied on the other machine), 4 (a real purchase) and 5 (two chests, one per machine, summing)
were not exercised at all — a hub start places no `Lib_Book_*` prop within 40 m, sells nothing
and opens no chest.

**Therefore both switches ship `0`.** The single-instance mechanics are measured; the
cross-machine claims below are not, and this document says so rather than rounding up.

The session that would close it is the same script with a longer boot wait (the second
instance took 4 minutes to reach the world) and a checkpoint beside the player: the checks in
`session-gapworld.ps1` already assert each line above.

### Switches

| switch | default | why |
|---|---|---|
| `WorldSetSync` | **0** | writes the game's own save arrays and raises two achievement counters; enable only on a run that shows the `worldset:` lines |
| `PuzzleSync` | **0** | rotates placed actors through the engine's own move call |

Both are independent of every existing switch. `TravelSync` is unchanged and is what actually
carries the follow half of item 1.

---

## 7. Open risks / for the coordinator

1. **Growing the save array.** `Array<Object>` with `Num < Max` is appended in place. A
   **freshly loaded** save has `Max == Num` (the loader sizes the array to its contents), and
   reallocating a `TArray` needs the engine allocator — handing it a foreign pointer corrupts it.
   For a teleporter the receiver therefore dispatches the placed `Teleporter_C`'s own
   `RegisterSphere` begin-overlap so the GAME does the add (the trigger lane's proven mechanism).
   For `SavePointsVisited` / `ShopTeleporterDestinations` there is no equivalent narrow verb
   (`Checkpoint_C`'s only writer is `Rest`, which would rest the partner), so when the array is
   exactly full the key is logged and lands on the next pass after the game itself grows the array
   (any local rest/unlock leaves slack). This is the one place the item-1 implementation is not
   complete, and it is visible in the log rather than silent.
2. **The Shrine Boss exemption** (§4) rests on spawner properties the content may not carry.
3. **`ShopPurchasedKey` is a key-item family as well as an NSD suffix.** The filter matches by
   suffix, so a composed key that happens to end in it is caught; the inventory side is untouched
   (key items still ride `QuestItemSync`) — a purchase of a *unique relic* therefore still grants
   the item locally, which is the intended "each player buys for themselves".
4. `IsPuzzlePropClass` matches `Lib_Book_` by substring, which also catches `Lib_Bookshelf_*` and
   `Lib_BookCart_*`. Those are static props that do not move, so the cost is a rotation read, not a
   wrong state. Narrowing it to the five `Lib_Book_` + `Lib_Book_Open` classes is a one-line change
   if a live run shows a cart moving.
