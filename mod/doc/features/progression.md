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

