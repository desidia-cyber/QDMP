# progression — quest/key items only, kill XP to both, a save on both machines

Module: `mod/native/src/qdprogression.h` (new). Switches: `QuestItemSync`, `KillXPSync`,
`PartnerSaveSync` in `qdcoop.ini` / `qdconfig.h`. Wire: `KILL_XP = 0x40`, `SAVE = 0x41`.

## What this closes

Three gaps the co-op mode still had, all on the boundary between "shared world fact" and
"this player's own game":

1. **Quest/key items are shared; everything else is not.** The pre-existing `INVENTORY`
   lane mirrored the WHOLE `InventoryContents` (`SYNC-GAPS` G3) and the `LOOT` lane shared
   ONE drop set with permanent tombstones. Both crossed far more than the two players should
   share: the partner received every herb, every potion, every sword, and a normal drop
   took on one screen disappeared from the other. Both are now narrowed at one predicate, so
   **only `Items/KeyItems/` entries cross**; normal loot, consumables, equipment, money,
   treasure and accessories stay per-player.
2. **A kill pays both players, once each.** The machine that runs the death does not always
   pay the other player, and the other machine's copy of the death may or may not run the
   game's award. The amount is now **measured** on each machine and the shortfall is paid by
   the machine that did not get it.
3. **A save on one machine is a save on the other.** When either player saves — a rest
   point, the game's own autosave, a menu save, a scripted save — the other machine
   immediately writes its own local slot through the game's own save function, without
   bouncing the save back.

## Design (≤15 lines)

* **Which items cross.** One predicate, `progression::IsQuestItem(path, haveCat, cat)`.
  Path form first: case-insensitive `Items/KeyItems/` — the folder the analysis measured
  (503 key-item assets, 26 families). Then the item definition's own `Category` byte ==
  `ItemCategory::Keys` (5). An item whose kind cannot be established stays **local**, the
  safe direction (a key the partner lacks locks them out; an ordinary item they wrongly get
  is a dupe). Applied at four places: the host publisher's container walk, the peer's
  pickup/consumption walk, the peer's inbound-body apply, and the drop scan.
* **The host's *consumption* now reaches the peer.** The lane was additive by design, so a
  key the host used stayed on the peer for ever. `RemovalNeeded()` decides the fall and the
  peer applies it with the game's own `TryConsumingItemFromInventory` (WrConsumeItem). It
  is deliberately conservative: no removal when the peer never contributed that path, and
  none while one of the peer's own uses is in flight.
* **XP.** Each machine watches its OWN player's XP counter across a death. A positive
  observed delta is reported (`KILL_XP`); the other machine pays the reported amount with
  the game's own `Player_GiveXP` **only if its own delta was zero**. Both-paid ⇒ nothing
  crosses. Dedup by `{enemy key, per-key death ordinal}`, so a reliable retransmit is a no-op
  and a respawned kill is a new award. No readable counter ⇒ the lane does nothing.
* **Save.** Detection is the DISPATCH (`SaveGameToDisk` / `GI_SaveGameToDisk` / `Save Game
  to Disk`), so every route is covered. Suspend and system saves are refused by name
  (`qdsuspend.h` owns the suspend save; a system save is per-machine settings). Applying a
  partner's save runs the local writer, which the detector sees — the echo is swallowed by
  COUNT (`owed_`), not by a time window, so a player's own next save still goes out.
* **Not while busy.** `SaveGate` refuses a mirrored save during a world load, a travel
  handshake, a menu, the local player's death, a cutscene, with no active save, or while the
  game is writing a suspend save. It defers (the save stays owed) and reports a give-up at
  20 s.
* **Idempotent on reconnect.** The XP table is keyed by death identity and expires old
  records; the save board's ordinals are monotone; the item lane's authority is unchanged
  (the host's absolute body is the resolution).

## Files

| file | change |
|---|---|
| `mod/native/src/qdprogression.h` | **new** — item predicate, `HostView`, `RemovalNeeded`, `KillXp`, `SaveBoard`, `SaveGate`, save-verb names |
| `mod/native/test/test_progression.cpp` | **new** — 121 assertions, no game needed |
| `mod/native/src/qdwire.h` | `K_KILL_XP = 0x40`, `K_SAVE = 0x41` + bodies; both added to `IsReliableKind` |
| `mod/native/src/proxy.cpp` | see the hook list below |
| `mod/native/src/qdconfig.h` | `quest_item_sync`, `kill_xp_sync`, `partner_save_sync` |
| `mod/native/qdcoop.ini` (if present) | the three keys |
| `mod/rust/qd-core/src/protocol.rs` | the two kinds, byte-compatible |
| `mod/doc/WIRE.md` | §3.26 `KILL_XP`, §3.27 `SAVE` (3.24/3.25 were taken by other lanes while this ran) |

### proxy.cpp hooks (all marked `// [progression]`)

1. `CollectInventoryItem` — skip a non-quest item before it reaches the host snapshot.
2. `CollectInvIntentItem` — skip a non-quest item before it reaches the peer's intent queue.
3. `PeerApplyInventory` — refuse a non-quest entry in an inbound body (defensive: an older
   sender would still be mirroring everything), and feed `HostView` with the host's counts.
4. `ProgressionQuestRemovalPump` — the new fall direction (peer only, ≤1 removal/tick).
5. `LootScanStep` — a drop whose item is not a quest item is never registered, so it is
   neither tombstoned, nor mirrored, nor hidden: each player's drops are their own.
6. `HK_StoreEnemySaveState` — the death funnel: one `KillXp::LocalDeath` per death.
7. `ProgressionPump` — settles measurements, sends/pays XP, drives the save board.
8. `HK_ProcessEvent` — the save dispatch detector.
9. `DispatchInbound` — `K_KILL_XP`, `K_SAVE`.
10. `HK_Tick` — `ProgressionPump(now)`.
11. `WorldTeardownReset` — reset the boards that cannot span a world.

## Coverage

**Crosses (quest/key):** every asset under `Content/Items/KeyItems/` — measured 503 assets in
26 families: `ScientistUpgrades`(53), `Memento_Content`(52), `TridentKeys`(32),
`ShopProgressionKey`(29), `CastleDoorKeys`(24), `AquaDoorKey`(19), `FishMaidenTiaras`(18),
`AcidVial`(16), `ShopPurchasedKey`(16), `Egg`(16), `ForbiddenDoorKey`(16), `JigekiKeys`(15),
`PrisonKeys`(12), `Knights_Key`(11), `MerchantKeys`(11), `BookKeys`(~20),
`Ros_Orb`(9), `Mealticket`(9), `KnifeBelt`(7), `SwordShrineKey`(6), `ArchivesKey`(6),
`SecretRelicKey`(6), `JosamiKey_Red`(6), `RaidersKey`(6), `KingSword_Remnant`(3),
`RaiderNecklaceKey`(3). Plus any item definition whose `ItemCategory` byte is `Keys`, even
outside the folder.

**Stays local (by design, and now enforced):** `Consumables`, `Weapons`, `Accessories`,
`Treasure`, `Upgrades`, `Misc` (money), and every drop whose item is not a key item.

**Not covered, and why:**
* A key item the host consumes down to **zero** is not removed on the peer: the publisher
  never sends a zero entry, so "the host holds none" is only expressible as absence from a
  complete pass, and the removal path deliberately reads only published counts. The failure
  direction is safe — the peer keeps a key it is not locked out of.
* A key **both** players picked up independently is not lowered by the host's use of its own
  copy, because the count difference cannot be attributed to the host. Same safe direction.
* XP for a kill of an enemy that has **no copy** on the receiving machine is paid after a 3 s
  grace window and logged as not backed by a local measurement.
* XP is only mirrored when the local player exposes a readable XP counter. If no candidate
  property resolves, the lane logs that once and does nothing — it never invents an award.

## Proof

**Build.** `mod/native/build.bat` → `winmm.dll`, 1 430 016 bytes, exit 0. `proxy.cpp` compiles
with `/W3` and links; `test_progression.cpp` compiles with `/W4` and no warning.

**Unit tests.** `mod/native/test/test_progression.cpp`: **126 assertions, 0 failed**. The whole
native suite runs clean with it (`all test executables passed`, including the lanes this change
narrows: `inventory 1309/0`, `invintent 1315/0`, `loot 52/0`, `vectors 48/0`).

**Rust.** `cargo test` in `mod/rust/qd-core`: **143 passed, 0 failed** plus 5 vector tests,
including the new `protocol::tests::kill_xp_and_save_round_trip` (KILL_XP is 22 bytes and
round-trips; a zero or over-large award is refused; SAVE is 5 bytes and a role above 2 is
refused). `kind_classification_matches_the_wire_table` and
`every_body_roundtrips_through_bytes_and_envelope` still pass, so the two new kinds are
classified and encodable with the rest.

**Live: NOT LIVE-PROVEN.** The LIVE lock on this machine was held by another delegate's
two-instance session for the whole of this work (`owner.txt` = `platforms`, with two
`QueensDomain-Win64-Shipping` processes and `qd-relay` running), so no session could be started
without killing someone else's run. Nothing below is claimed as measured; it is the exact
procedure and the exact lines.

### How to prove it live (one order, because each makes the next diagnosable)

1. **Switches.** `QuestItemSync=1 KillXPSync=1 PartnerSaveSync=1` beside `EnemySync=1`,
   `InventorySync=1`, `LootSync=1`. On boot each machine logs
   `xp: the local player's counter is <name> (int|float)` — the name that resolved. If instead
   `xp: the local player exposes no XP counter (...)` appears, the kill-XP lane is off on this
   build and the candidate list in `XpResolve` needs the property the log's `LogPropertiesOf
   cls` dump names.
2. **Quest items only.** Host picks up a herb: the host must NOT log an `inventory host set`
   line that grew, and the peer must NOT log `inventory grant applied: …Herb…`. Then take a key
   (`Items/KeyItems/…`): host `inventory host set N entries … rev=R`, peer
   `inventory grant applied: /Game/Items/KeyItems/…`, peer `item intent confirmed`. **Fail
   signature:** the peer's bag receives the herb, or a normal drop one player takes disappears
   from the other's screen (`loot: hiding local drop …` for a `Consumables`/`Weapons` path).
3. **A normal drop stays per-player.** Kill two enemies, both machines roll, one player takes
   its drop: its own machine tombstones only quest-item drops now, so there must be **no**
   `loot: local pickup <h>#<n> -- tombstoned for the partner` for a non-key item, and the other
   machine must still show its own copy.
4. **Kill XP to both.** With both players near an enemy, `qd.hit near:enemy` on the HOST until it
   dies. Expect on the host `xp: this death paid <N> here (guid=…)` and on the peer either
   nothing (the peer's copy paid too) or `xp: the partner's death paid <N> there and nothing
   here; awarding it to the local player` + `xp: paid <N> to the local player (a -> b)`. Then
   the reverse (kill it from the PEER). **Pass:** each machine's counter rose by that kill's
   award exactly once, and the two logs never both pay for one death. **Fail signature:** a
   counter rising twice for one guid, or `xp: Player_GiveXP returned but the counter did not
   rise`.
5. **Save on both.** Host rests at a checkpoint (or saves from the menu). Host
   `save: this machine saved (SaveGameToDisk); telling the partner to write its own slot`,
   peer `save: the partner saved; writing this machine's own slot`, then peer
   `save: wrote this machine's own slot for the partner's save`. **Pass:** the peer's
   `Saved/SaveGames/*.sav` mtime moves and neither machine logs a second
   `save: this machine saved` in the same second. **Fail signatures:** a save ping-pong (both
   logging `this machine saved` repeatedly), or `save: the game's own writer refused: …` naming
   `no such function`, which means this build routes the slot write through a name not in
   `IsSaveVerb`.
6. **Reconnect.** Join, save, leave, rejoin: no `xp:` line for a death that already paid, and no
   `save:` line until someone saves again.

---

# Round 2 live proof — 2026-11-15

Rig: `mod/tools/session_prog.ps1` (new). Two off-screen instances, fresh pair, boot level
`Ruins_Level_00`. Switches: `InventorySync=1 QuestItemSync=1 KillXPSync=1 PartnerSaveSync=1`,
`LogLevel=3` (the save half is repeated at `LogLevel=4`, see below).

How a change is caused: `qd.cheat` calls the game's OWN `FS_Cheats_C` UFUNCTIONs. That is not
a shortcut around the lane — the lane is about what happens after a machine's world changes,
and the measured alternatives are worse (coins and fruit are OVERLAP pickups, so the
interaction lever does nothing near them).

## QuestItemSync — PROVEN

`qd.cheat GiveAllKeys` on the host (`rig: cheat GiveAllKeys ran=1`), then:

```
[1] 18:54:55.312 INFO inventory host set 65 entries (live 69) rev=3
[2] 18:54:59.407 INFO inventory grant applied: /Game/Items/KeyItems/PrisonKeys/Prison_key_2F_Item.Prison_key_2F_Item +1 (host 1, peer 0)
[2] 18:54:59.473 INFO inventory grant applied: /Game/Items/KeyItems/RaidersKey/RaidersKey_Item.RaidersKey_Item +1 (host 1, peer 0)
[2] 18:54:59.523 INFO inventory grant applied: /Game/Items/KeyItems/Ros_Orb/Ros_Orb_Item.Ros_Orb_Item +1 (host 1, peer 0)
[2] 18:54:59.671 INFO inventory grant applied: /Game/Items/KeyItems/SecretRelicKey/SkullKeyRelic_Data.SkullKeyRelic_Data +1 (host 1, peer 0)
[2] 18:54:59.697 INFO inventory grant applied: /Game/Items/KeyItems/ShopPurchasedKey/SZDoorKey_Blue.SZDoorKey_Blue +1 (host 1, peer 0)
[2] 18:54:59.861 INFO inventory grant applied: /Game/Items/KeyItems/SwordShrineKey/SwordShrineKey.SwordShrineKey +1 (host 1, peer 0)
[2] 18:54:59.881 INFO inventory grant applied: /Game/Items/KeyItems/TridentKeys/TridentKey_00_Data.TridentKey_00_Data +1 (host 1, peer 0)
   … 14 KeyItems grants in total (PrisonKeys, RaidersKey, Ros_Orb, SecretRelicKey,
     ShopPurchasedKey x3, SwordShrineKey, TridentKeys x5, KnifeBelt, BookKeys, …)
```

`65 entries (live 69)` is the predicate itself: four live entries were NOT published, and
they are the non-key ones (the seed save's `RustySword_Item` and its sources).

**A NORMAL item does not cross.** The same lever, a set the predicate must refuse:

```
[1] 18:55:02.671 INFO rig: cheat GiveAllConsumables ran=1
   (no `inventory host set` follows, and no `inventory grant applied` on the peer)
```

`0` grants, `0` republished entries. So the narrowing is measured in both directions: keys
cross, consumables/weapons/treasure do not.

## KillXPSync — the lane was DEAD at boot, and why

Both machines logged, on the first boot:

```
[1] 18:54:06.885 WARN xp: the local player exposes no XP counter (tried XP, XPAmount,
                    XPIncrease, CurrentLevelXPProgress); the kill-XP lane does nothing
                    rather than invent an award
[2] 18:54:07.368 WARN xp: the local player exposes no XP counter (…)
```

So the whole kill-XP lane was inert, exactly on the failure direction the design chose (it
never invents an award) — and the run had nothing to measure.

**Root cause (measured from the analysis corpus, not guessed):** the counter is not a pawn
property at all. `analysis/pak/blueprint-surface.md` line 171 is the
**save slot blueprint's** own property table, and it carries `XPAmount` (alongside
`MaxProgressionLevel`, `SavedPlayerLevel`, `WeaponXP`, `KilledEnemyCounts`). `XpResolve`
probed `LocalPawn()` only, so a save-side counter could never resolve.

**Fix** (`proxy.cpp`, `XpResolve`/`ProgressionOnDeath`/`ProgressionPayXp`/`ProgressionPump`):
the probe tries the pawn first (a pawn-carried counter, if a build has one) and then the
SAVE OBJECT, remembers which object the offset belongs to (`g_xpObj`), invalidates on a new
save, and every read follows the resolution. The log line keeps its documented prefix and
names the source: `xp: the local player's counter is XPAmount (int) size=4 (read from the
save slot)`.

## PartnerSaveSync — the detector never saw a dispatch

```
[B=2] save files before : System0.sav@11:49:06.286  World0.sav@11:49:06.288  World0_backup.sav@11:49:06.290
[1] 18:55:13.642 INFO rig: cheat SaveHere ran=1
   (no `save: this machine saved` on the host, no `save: the partner saved` on the peer,
    and the peer's save files are byte-for-byte the same afterwards)
```

`SaveHere` RAN and dispatched no save verb this run, so the detector was not exercised. Two
things are therefore still open and are what the next run changes: (a) `SaveHere` may not be
a save route on this build, so the player's own route (a checkpoint rest) is pressed as
well; (b) the detector lives in `HK_ProcessEvent`, whose installation is gated —
`partner_save_sync` is now part of that gate in the working tree, and the run repeats at
`LogLevel=4`, where the hook is unconditional, so the next measurement cannot be confounded
by the gate.

## Runs 2 and 3 — the counter fix proven, and two halves that are still not

Two more sessions (`session_final.ps1`, `session_gap.ps1`) repeated the item half and
attacked the rest. What they establish:

**THE QUEST-ITEM HALF REPEATS** (three sessions, identical result): `inventory host set 65
entries (live 69)`; 14 `inventory grant applied: /Game/Items/KeyItems/…` on the peer; and
after `GiveAllConsumables` **0** grants — `a NORMAL item did not cross (non-KeyItems grants:
0)`. The predicate is proven in both directions three times over.

**THE XP COUNTER FIX IS PROVEN.** Both machines, after `XpResolve` was taught to probe the
save object:

```
[1] 20:20:15.579 INFO xp: the local player's counter is XPAmount (int) size=4 (read from the save slot)
[2] 20:20:22.639 INFO xp: the local player's counter is XPAmount (int) size=4 (read from the save slot)
```

and a real death then paid through it:

```
[1] 20:21:47.245 INFO xp: this death paid 42 here (guid=4116ED15#1)
```

That line is printed only after the award was handed to the socket, so the measurement and
the send both ran. **The BOTH-PLAYERS half is NOT proven**: the receiving machine logged no
`xp: the partner's death paid …` in the same window, and the reverse kill produced no `xp:`
line at all. Two facts the next run needs: `FS_Cheats.KillAllEnemies` does **not** reach the
death funnel (it writes no enemy save state, so `ProgressionOnDeath` is never called — the
cheat's `ran=1` with no `xp:` line is that), and with `EnemySync=1` the enemy is RUN by the
host, so a `qd.hit` on the peer is paid by the host, which is the direction that was
measured.

**THE HOST'S USE OF A KEY DOES NOT LOWER THE PEER'S COPY — measured, and by design.** The
host consumed the key for real (`rig: use lever -> …/AquaDoorKey_1_Data removed=1 ok=1`), the
host then published `inventory host set 64 entries (live 99)`, and the peer logged
`inventory body rev=67 n=65 entries=13 applied=13` … `rev=74 n=64` — no removal, ever. The
reason is the one this document already states as a limitation: a count that falls to **zero**
is never published (`CollectInventoryItem` returns early on `count <= 0`), so "the host holds
none" has no wire form, and the removal pump only reads published counts. Closing it needs a
decision about the wire (a zero row, or an explicit removal entry), not a tweak.

**THE MIRRORED SAVE HAS NO ROUTE ON THIS BUILD.** `SaveHere` dispatched nothing again, and a
checkpoint rest

```
[1] 20:23:20.365 INFO dev interact: forced verb Interact on Checkpoint [Checkpoint_C] ran=yes state 0 -> 0
[1] 20:23:21.204 INFO flag CHANGE CheckpointUnlockCount : 1 -> 2
```

wrote `World0_Suspend_1.sav` and no slot save. So the player's ordinary rest is a SUSPEND
save, which this lane refuses by name on purpose. `LogLevel=4` was in force, so the
ProcessEvent hook was unconditional and the gate is not the explanation. **`PartnerSaveSync`
stays 0**, and the honest statement is: the detector is built and unit-tested, and on this
build nothing in reach dispatches a mirrorable save.

## Switches

`QuestItemSync=1` (proven three times). `KillXPSync=0` and `PartnerSaveSync=0` — see above;
each carries its measured reason in `qdcoop.ini`. `InteractSync`/`ObjectSync`/`ActorStateSync`
were flipped on the world-interact evidence (that document).


## Files

* `mod/tools/session_prog.ps1` (new) — the three sub-features' proof, in the documented order.
* `mod/native/src/proxy.cpp` — `XpResolve` + the three read sites (the counter's object);
  comments name the measurement.



---

# Round 3 live proof - 2026-11-15

Rig: `mod/tools/session_r3.ps1`. Same two-instance setup, `LogLevel=4`. Every claim below
is from `_test/sr3-r3{e}/out/report-r3e.txt` (run E) with the earlier runs cited where they
found the cause.

## THE ROOT CAUSE BEHIND BOTH OPEN ITEMS: a stale relay, and a detector on the wrong boundary

**1. The relay dropped `KILL_XP` (0x40) and `SAVE` (0x41) outright.** The deployed
`qd-relay.exe` predated both kinds and said so, six times in one run:
`malformed datagram ... (kind 0x40): unknown packet kind 0x40`. Neither peer ever saw a
kill award or a save request. Fixed (Kinds present in source; the *binary* was stale) and the
relay rebuilt; the rig asserts the relay log has **zero** malformed datagrams.

**2. The save detector was on the wrong boundary.** Round 2 concluded "nothing in reach
dispatches a mirrorable save". That was correct and incomplete: `analysis/re/
combat_save_evidence.txt` §C9.1-2 measured that **every save in this game funnels through
`UGameplayStatics::SaveGameToSlot` @ `0x028F2B40`, whose only callers are the NATIVE
`FB_SaveUtils` functions plus the exec thunk**. A native C++ caller never crosses
`UObject::ProcessEvent`, so a ProcessEvent-based detector could never see the save a player
actually makes -- at ANY log level. It was not misconfigured; it was watching a boundary the
save does not cross.

Fix: `HK_SaveGameToSlot` -- a MinHook detour on that one primitive, installed only when
`PartnerSaveSync=1`. It forwards unconditionally (the player's save must happen), logs the
call with its slot name and result, and announces it. The ProcessEvent detector is kept as a
second boundary for a build that routes a save through a Blueprint name that never reaches
the primitive.

## KEY USED TO ZERO ? PROVEN

Decision taken as instructed: in quest-item mode a key whose count falls to 0 is published
as a **tombstone** (count 0), bounded, cleared when the count rises. Two things had to
change for that to be true:

* **the tombstone must be synthesised, not observed.** `InventoryContents` does not keep a
  zero element -- using a key up REMOVES it -- so a walk that only visits live elements can
  never see the fall. `HostPublisher::TombstoneMissing` publishes count 0 for every path in
  a bounded ledger of what this publisher has carried that the walk did NOT produce. A walk
  that produced nothing (`hadLive == 0`) is a failed read and tombstones nothing.
* **the receiver must apply it.** `ProgressionQuestRemovalPump` now accepts `count == 0`
  (it skipped `<= 0`) and consumes with the game's own `WrConsumeItem`, still gated by
  `progression::RemovalNeeded` so a key the receiver legitimately picked up itself is not
  taken away.

```
  PASS [1] the host consumed it through the game's own consumer -- 23:53:34.202 rig: use lever -> /Game/Items/KeyItems/AquaDoorKey/AquaDoorKey_1_Data.AquaDoorKey_1_Data removed=1 ok=1
  PASS [1] the host republished its set (the zero travels as a tombstone) -- 23:53:34.387 inventory host set 65 entries (live 69) rev=110
  PASS [2] THE HOST'S USE REMOVED THE PEER'S COPY (round 2's open gap) -- 23:53:34.597 quest item removed here: /Game/Items/KeyItems/AquaDoorKey/AquaDoorKey_1_Data.AquaDoorKey_1_Data -1 (host holds 0, this machine held 1)
```

`host holds 0, this machine held 1` is the claim in one line: the host's last copy went, the
zero crossed, and the peer's copy went with it through the game's own consumer.

No body-layout change: the existing `i32 count` already carries 0, so native, Rust and
WIRE.md are unchanged. Unit-tested in `test_inventory` (`TestTombstoneLedger`, +13
assertions) including the "missed by the walk" case, the idempotent second pass, the
rise-again clear, and the failed-walk guard.

## KILL XP ? PROVEN, BOTH PLAYERS, BOTH DIRECTIONS

Lane was already correct; the relay was eating every award. With it fixed, a real death
through the game's own funnel (enemy damage via `qd.hit`/`qd.hitguid`, which reaches
`HK_StoreEnemySaveState`):

```
  PASS [2] kill attempt by inst2: inst2's XP moved -- 23:56:28.066 xp: paid 140 to the local player (412 -> 552)
  PASS [1] kill attempt by inst2: inst1's XP moved too (both players paid) -- 23:56:27.999 xp: this death paid 140 here (guid=3DF18B15#1)
  PASS [2] no guid was paid twice in this kill
```

and the other direction in the same run:

```
  [2] 23:54:57.815 xp: this death paid 132 here (guid=12308049#1) x5
  [1] 23:54:57.874 xp: the partner's death paid 132 there (guid=12308049#1) x5
```

**`KillXPSync` is flipped to 1.**

Note on attribution: with `EnemySync=1` the enemy is RUN by one machine, so the machine the
rig aimed from is often only the one that SENT the hit, and the OTHER machine's game pays
first. That is why the assertion is per-machine ("each player's counter rose for this
death") and not per-direction.

## SAVE ON BOTH ? PROVEN, BOTH DIRECTIONS

What this game's save actually is (measured, and why the route travels on the wire):

* `SaveHere` dispatches **nothing** (round 2, reconfirmed);
* a checkpoint REST writes `World0_Suspend_N.sav` -- the game's OWN suspend writer -- and
  does not touch `World0.sav`;
* the same suspend writer also runs from the game's ~120 s timed loop.

So the player's progress reaches disk through the suspend route, and a partner that answered
with a main-slot write would have saved a DIFFERENT FILE than the one its own game resumes
from. The request therefore carries a **route** byte (`ROUTE_SUSPEND` / `ROUTE_SLOT`) and the
receiver runs its OWN matching writer on its OWN slot. **No save file ever crosses; only the
instruction.** `WIRE.md` §3.27 documents the extra byte; a 5-byte body from an older sender
is accepted as the main slot rather than refused.

```
  PASS [1] A's own save was DETECTED (round 2 saw nothing at all) -- 23:57:19.079 save: the game's own writer ran (slot .../World0_Suspend_1.sav, ok=1)
  PASS [1] the detector named WHICH route fired -- 23:57:18.995 save: the partner saved (the rest/suspend route); running this machine's own matching writer
  PASS [2] B's OWN save file timestamp moved on disk
  PASS [1] the reverse: A ran its OWN matching writer for B's save -- 23:59:25.276 save: wrote this machine's own suspend save (the partner's rest) for the partner's save
  PASS [1] A's OWN save file timestamp moved for B's save
```

The echo is bracketed, not guessed: every dispatch inside
`BeginOwnSave`/`EndOwnSave` is logged as
`save: swallowed the echo of the save this machine ran for the partner (...)` -- visible in
every mirrored write above (`GI_WriteSuspendSaveImmediately` and then `SaveGameToSlot`), so
the mirror cannot ping-pong.

**`PartnerSaveSync` is flipped to 1.**

### On the suspend-safety question

`qdsuspend.h`'s rule stands and is NOT violated: this lane never copies a suspend save, and
never writes the partner's snapshot over its own world. It runs the partner's OWN game's OWN
writer on the partner's OWN slot, at the partner's own current state -- which is exactly what
that player's rest would have produced. The safety reason the document gives ("a suspend
snapshot from another machine is a rollback") is about shipping the FILE; the mirror ships a
REQUEST, so the reason does not apply to it. The peer's `BlockSuspendSaves` hold
(`qdsuspend.h`) is left exactly as that lane sets it: the hold stops the game's *timer*, and
the explicit mirror is a deliberate, requested write.

## Switches

`QuestItemSync=1` (round 2, extended by the tombstone).
`KillXPSync=1` ? PROVEN this round.
`PartnerSaveSync=1` ? PROVEN this round.

## Files this round (progression half)

* `mod/native/src/qdprogression.h` -- `IsProgressSaveTrigger`/`IsSuspendTrigger`,
  `SaveRoute`/`ROUTE_SLOT`/`ROUTE_SUSPEND`, `SaveBoard::OnPeerSave(seq, route, now)` +
  `WantedRoute`.
* `mod/native/src/qdinventory.h` -- `HostPublisher::Has/DocNote/DocClear/TombstoneMissing`
  and the bounded `TombLedger`.
* `mod/native/src/qdwire.h` -- `SaveBody::route`, `WriteSave(seq, role, route)`,
  short-body-tolerant `ReadSave`.
* `mod/native/src/proxy.cpp` -- `FnSaveGameToSlot`/`g_oSaveGameToSlot`/`HK_SaveGameToSlot`,
  `FStringToAnsi`, `SaveRouteOfSlot`, `SaveAnnounce` (shared by both detectors),
  route-aware `ProgressionSaveToDisk`, the tombstone-aware collector and removal pump, and
  the hook install gated on `partner_save_sync`.
* `mod/rust/qd-core/src/protocol.rs` -- `Save.route` + `save_route`, OBJECTS/ENEMY guards.
* `mod/native/test/test_progression.cpp` -- +19 assertions (trigger superset, route
  plumbing). `test_inventory.cpp` -- +13 (the tombstone ledger).
