# PLAYTEST — two real players, Queen's Domain co-op

This is the short version: how to put the mod on two machines, turn it on, connect, and
what to look for.  If something is wrong, the last section says exactly which log lines
to send back.

Written for the current build: **proven on a live two-instance run**, with the rough edges
named at the end.  It is not a finished product; it is a mod you can play and measure.

---

## 1. What you need

| | |
|---|---|
| Game | Queen's Domain, `WindowsNoEditor`, UE 4.27.2 — the same build on both machines |
| Machines | two PCs on the same LAN (or one PC with two instances, for a smoke test) |
| Port | UDP **47800** reachable on the machine that runs the relay |
| Files | `winmm.dll` and `qdcoop.ini` from this repo's `mod/` directory |

Both players must have the **same game build**.  The mod verifies the executable's image
hash at load and **refuses to run** on a mismatch; that is deliberate, because a wrong
address table is an access violation at best.

---

## 2. Install (on each machine)

Copy two files next to the game executable:

```
<game>\QueensDomain\Binaries\Win64\winmm.dll     <- the mod (a proxy, not a patched game)
<game>\QueensDomain\Binaries\Win64\qdcoop.ini    <- the switches
```

`winmm.dll` is a proxy for the system DLL the game already imports: the game loads it
naturally, and every one of the real `winmm` exports is forwarded to the real
`C:\Windows\System32\winmm.dll`.  No file in the game is modified, and no system DLL is
replaced.  To remove it: delete those two files.

The repository has a script that does the same thing and refuses if the game is not where
you said it is:

```powershell
cd mod
.\tools\install.ps1 -GameDir "D:\Games\QueensDomain\QueensDomain\Binaries\Win64"
.\tools\install.ps1 -GameDir "..." -Uninstall       # removes exactly those two files
```

On start, `<game dir>\qdcoop-<suffix>.log` appears.  A healthy start has:

```
targets: 34 verified, 6 runtime-validated, 0 mismatched
substrate: globals=1 fname=1(GameEngine) gobjects=1 objects=...
switches: ... FlagSync=1 ... PlayerBody=1 ...
PROBE writer: PASS ...
```

If you see `REFUSED` instead, the build is not the one this was measured against — stop
and send the first 40 lines of the log.

---

## 3. The relay

One machine runs it; both games connect to it.  It only forwards datagrams; it never
touches a save.

```powershell
mod\rust\target\release\qd-relay.exe --bind 0.0.0.0:47800
```

Then, in **both** `qdcoop.ini` files, point `RelayHost` at that machine's LAN address:

```ini
[Net]
RelayHost=192.168.1.50
RelayPort=47800
```

`127.0.0.1` is right only for two instances on one PC.  The relay is plaintext and
unauthenticated in this version: it is for a LAN.  Over the internet, set
`AllowPlaintextInternet=1` and understand what that means.

---

## 4. `qdcoop.ini` — the switches to turn on

`ActorStateSync`, `ObjectSync`, `EnemySync`, `PuppetSync`, `PlayerBody`, `DialogueSync`,
etc. default to **0** in the shipped file, because each one was enabled only after it was
proven on a live run.  For a play session, turn on this set:

**This is the set Sessions 17–19 used for their ten-minute soaks with both players in
`780_SZ_Depths` and in the hub.**  Session 18 fixed the two crash causes it found; Session 19
fixed the third at its source — a per-class UFunction cache (the dragon's `TryApplyDamage`
running on crawlers and golems) — and removed the Escape-pause exception.  After that fix
the uncaught deaths stopped in every measured run; kill sets still left one stray key in
one 600 s run.  Read the stability note in section 8 before planning a marathon.  `MirrorSpawn` is deliberately absent — it
is a compiled default (on), so a real pair of players gets it without an entry here.
The three lines marked *proven* are the ones Session 17 closed; the one marked *not proven* is
still safe to leave on (it is a read path and a refusal), but do not expect its second half.

```ini
[Local]
Enabled=1
LogLevel=2

[Net]
RelayHost=<relay machine>
RelayPort=47800

[Rules]
FlagSync=1
InventorySync=1
ActorStateSync=1      ; proven: a door/chest opened by one player opens for the other
ObjectSync=1          ; proven: the same for placed objects that carry no save GUID
EnemySync=1
PuppetSync=1          ; the partner's body
PlayerBody=1          ; spawn it as the game's own player class (needs the two above)
DialogueSync=1
StreamUnion=1
TravelSync=1
CutsceneSync=1        ; proven: play on one machine, hold on the other, resume together
MoverSync=1
LootSync=1            ; not proven: the drop hides only if the tombstone converges
SuspendGuard=1
SuppressMenuPause=1   ; a paused host looks like a hang to the partner
HostAuthoritative=1   ; do not set this to 0

[Tuning]
PlayerHz=30
EnemyRowsPerTick=20
```

`LogLevel=2` (info) for play.  `3` (debug) is for a test run — it is much larger and only
worth it when you are reporting a problem.

---

## 5. Connect

Start the relay, then the game on each machine.  Load a save; the mod connects when you
are in the world, and logs one line per machine:

```
session established: role=HOST level=...
session established: role=PEER level=...
```

Exactly one is `HOST` and one is `PEER` — roles are decided by the relay, not by you.  If
both say the same thing, the two games are not talking to the same relay: check
`RelayHost` and the firewall on UDP 47800.

---

## 6. What to try, and what it should look like

Do these in order; each one makes the next diagnosable.

**1. The partner exists.**  Walk into the other player's view.
*You should see* a body with the partner's gear that walks, turns and swings where they do.
*Log:* `player body: class FS_PlayerChar_C resolved`, `player body: spawned unpossessed at …`,
`puppet at x,y,z … err=0.0cm`.

**2. Shared progression.**  Unlock a checkpoint or a barricade on the peer.
*You should see* it unlocked on both screens.
*Log:* peer `flag intent sent`, host `intent applied:`, peer `flag intent confirmed`.

**3. A chest or a door, both ways.**  Stand in front of a closed chest, then press the
interact key.
*You should see* the lid open for **both** players, and the second player cannot loot it
again; the chest's counter does not go up twice.
*Log:* the opener `dev interact: the input event changed the object` →
`actor-state intent sent`; the host `actor-state intent applied` and
`actor-state refresh ok … opened=1`; the opener `actor states applied=… confirmed=1`.
A door opened by the other player shows `actor-state host set` on their machine and
`actor states applied=1` on yours.

**This is the one proven most recently (Session 17), so it is the best one to try:** open a
**chest** in the hub or in `780_SZ_Depths` with the partner watching.  You should see it open
on both screens, and the partner must NOT be able to loot it again.  The lines to expect,
verbatim:

```
<opener>  actor-state intent sent guid=... value=1
<other>   actor-state intent applied: guid=... value=1
<other>   actor-state refresh ok Chest5 [Chest_C] index=0 value=1 ... opened=1
```

Two things a tester should know, because they look like failures and are not:
* A chest's own `opened` flag can read 0 on a chest that IS open — this content keeps the real
  state in the save container's value.  If the object visually opened and cannot be looted
  again, the lane worked.
* The two machines do not hold the same *set* of world objects at every instant (the level
  manager streams one level at a time), so a chest you can see may have no counterpart on the
  partner's machine yet.  Opening it converges once both hold it; opening a chest that lives
  in a level the partner has not streamed in is the case that does nothing.

**4. Enemies.**  Fight a normal enemy with both players nearby.
*You should see* one enemy, attacking the player who is actually near it, and its health
staying in step on both screens.
*Log:* `enemy …: TargetPlayer local player -> the remote body`, `damage sent guid=…`,
and on the other machine `damage from enemy … applied hp a -> b`.

**5. Loot.**  Kill something that drops, then take the drop while the partner is standing
next to you.
*You should see* the drop disappear on both screens and appear in exactly one inventory.
*Log:* `loot: drop <key>#<n>`, then `loot: local pickup … -- tombstoned for the partner`,
and on the other machine `loot: hiding local drop … -- the partner took it`.

**6. Dialogue.**  Talk to an NPC.  Let the partner talk to a **different** NPC.
*You should see* both conversations work, and the same NPC refuse to start twice.
*Log:* `dialogue: local started`, `dialogue: BLOCK <npc>` on the other machine, and
`dialogue: PREVENTED a concurrent start on the same NPC` if you both pick the same one.

**7. Travel.**  Use a teleporter, then a map transition.
*You should see* both players arrive in the same place — including across a hard map
switch, where each machine stands at the destination world's own arrival point.
*Log:* `travel: local destination …` then on the partner `travel: followed …`; a hard map
switch logs `travel: hard transition: loading …`, `travel: both machines arrived …`, and
then one `arrive: <map> placed the player at partner|PlayerTeleportTargetPos_C|…` line per
machine.  If a world places no `PlayerStart` (every map except the hub does not), the
engine drops the arriving player at the origin, and that `arrive:` line is the mod putting
it where the game itself would have.  Asking a machine to load the map it is already in is
**refused** by design (`already in <map>: a hard load of the current world tears it down`) —
that request faults the game, not the session.

**8. A cutscene.**  Trigger one (an altar, an egg, a boss intro).
*You should see* the partner held for the same interval and both of you get control back
together.
*Log:* `cutscene: local started <name>`, on the partner `cutscene: playing the partner's
sequence` **or** `cutscene: holding the local player`, then
`cutscene: partner's cutscene ended; resuming together`.

**9. A boss.**  Walk into a boss arena (the Green Dragon in `780_SZ_Depths`, the Queen,
the Whale King…).
*You should see* the boss health bar on both screens.
*Log:* `boss: guid=… is a boss`, `boss: ActivateBossHPBar called`, `boss: sent … bar on`,
and on the other machine `boss: follower raised its own bar for guid=…`.

**10. A menu.**  Open the pause or inventory menu.
*You should see* the partner keep playing instead of freezing.
*Log:* no `clock: dilation=0.00` while the other machine is unpaused.

**11. Quit and reload.**  Load a save while the partner stays in the world.
*You should see* the partner's progression unchanged after your load.
*Log:* `suspend: peer held BlockSuspendSaves=1`, and after the load
`suspend: host re-applying WORLD_FLAGS, ACTOR_STATES and INVENTORY`.

---

## 7. When something is wrong, send this

One file per machine: `<game dir>\qdcoop-qd1.log` (or `qdcoop.log` — the suffix is the
instance's).  With `LogLevel=3` it is verbose; that is what makes a bug findable.

Along with the file, send:

1. which step above failed, and what you saw on screen;
2. the exact `session established:` line from each machine (it names the role);
3. the first `REFUSED` / `ERR` / `EXCEPTION` line after the failure, if there is one;
4. on a crash: the newest folder under
   `<user dir>\Saved\Crashes\` — its `CrashContext.runtime-xml` names the faulting module.

The lines that answer most questions, verbatim:

| Question | The line that answers it |
|---|---|
| Did the mod load at all? | `targets: 34 verified … 0 mismatched` |
| Which switches are on? | `switches: …` (one line, at start) |
| Did we connect? | `session established: role=HOST|PEER` |
| Is the partner's body real? | `player body: class … resolved` / `player body: spawned …` |
| Did a write land? | `actor-state refresh ok …` / `inventory grant applied: …` |
| Why did it not land? | the nearest `WARN`/`ERR` line above it, and `… failed: <reason>` |
| Is the partner still there? | `peer … silent` (the mod freezes, then hides, then keeps the actor) |

---

## 8. Known rough edges (read before reporting)

- **A long session at the play set is much better but NOT yet reliable — read this first.**
  Session 17 left this open: ten minutes with both players in a busy hub ended in a crash at
  about eight minutes, always inside the mod. Session 18 found the cause of that one and fixed
  it, and then found a second, different one.

  The first cause was a **cached object pointer read after the game had freed the object** — the
  mod held raw addresses to actors and objects and re-checked them with a test that itself reads
  the address it is checking. Every one of those caches now stores the object's *identity* (its
  slot in the game's object table plus that slot's serial number) and looks it up fresh, so a
  freed object reads as "gone" instead of faulting.

  The second cause was found afterwards and from a crash dump: the mod walks the game's whole
  object table every half second and read a slot the engine had freed but not yet cleared. Every
  such walk is now guarded, one slot at a time.

  Measured across Session 18 at this play set, with both players fighting for ten minutes:

  | outcome | runs |
  |---|---|
  | survived the full 600 s, both processes alive, zero faults | **4** (two boots, two builds) |
  | died | **3** — at 248 s, 4130 s, 330 s |

  Two of those three deaths were the mod's, and both were fixed in Session 18. **The 330 s
  death is now explained too (Session 19).** It was not an engine bug: the mod cached the
  first enemy's `TryApplyDamage` UFunction and, when that enemy was the dragon, ran the
  dragon's override on crawlers and golems. In `780_SZ_Depths` that aborted the game with
  `Failed to find function CheckForSplit`; in the hub it corrupted the heap the engine's
  next purge walked, which is the fault the dump showed inside the game's destruction
  pass. `Fn::Resolve` now re-checks the cached UFunction when the object's class changes,
  and the enemy damage API is per class. The longest run remains **105 minutes of
  continuous play-set traffic on both machines with zero faults**.

  Session 19 measured, after that fix: a 300 s hub soak and a 242 s hub soak both clean
  (both alive, kill-set diff 0, shared HP equal, zero fault lines); a 601 s run alive
  with diff 0 and one **caught** ProcessEvent fault (the process lived); a second 601 s
  run alive with **one** stray key (`FA2FC279`, dead only on the host); a third 600 s run
  invalid because the two machines ended in different worlds. So a ten-minute session is
  now expected to survive, kill sets converge to within one stray key, and a long session
  is still not a guarantee. If the game vanishes, look in `<user dir>\Saved\Crashes\` —
  `CrashContext.runtime-xml` names the faulting module, and a `WINMM` entry means the mod.
- **Progress can still lag on the peer in a chest-dense area.** In `780_SZ_Depths` the peer
  logged about 8000 `actor-state: game writer ... (WRITE FAILED)` lines: it wrote a container
  value and read back a different one (±1 or ±2), so its write did not stick. Nothing crashed
  and the counters kept moving, but a chest counter can be a step behind on the second screen.
  The host's own writes were fine (55 such lines, all during the area entry).
- **An enemy's health can still differ between the two screens for a *shared* enemy — much
  less often after Session 19.** Three distinct causes were measured: an enemy whose death
  was recorded could be respawned by its own spawner (fixed: the death stands and the
  respawn is re-killed until a rest); a summon could mint a key that a *different* class
  also used (fixed: the class is part of the summon key); and a key with no row read as
  HP 0 (fixed: it now reports unknown, `hp=?`).  Measured after the fixes: diff 0 in a
  300 s and a 242 s hub soak, and one stray key (`FA2FC279`) in one 601 s run.  This does
  not affect damage you deal or take — it affects the two screens agreeing about an enemy
  neither of you is looking at.
- **Boss HP bars can still show different bosses when the two machines disagree about
  which boss exists.**  The bar itself is now one bar bound to one key (Session 19): the
  host selects the most-damaged live boss, both machines bind their own bar to the actor
  carrying that key, the bar rebinds when the actor changes (respawn, mirror, stream-in),
  a dead key never holds it, and the active boss's death sends one explicit bar-off.  What
  is still open is the *choice*: measured live, the host's active boss was `03FDBC21`
  while the peer had no copy of it at all, and the peer's dragon `17181166` was a key the
  host still held dead — so one screen showed a full bar and the other none.  Next: the
  host must not select a boss the partner has no copy of (a refusal/ack on the boss lane),
  and that alive-vs-dead disagreement must be reconciled.  This does not affect damage.
- **Loot hiding** needs both machines to hold their own copy of the drop.  Session 17 measured
  that the drop often rolls on the machine that did NOT kill the enemy (`loot: drop
  90A52CF3#0 = …/Herb_Item_BP_C` appeared on the peer for a host kill), so the `loot: drop` line
  should be expected on either machine.  The `loot: hiding local drop` half still did not fire in
  any run: the tombstone needs the taker's ledger to reach the machine holding the visible copy,
  and that convergence was not observed inside the window.  The drop still disappears from the
  world on both screens; the *line* is what is missing.
- **Hit reactions for mod-applied damage are blocked on an API, not a lane.**  Session 17 called
  `ApplyDamageToHitboxTarget` — the entry Session 16 named as the next target — and its signature
  settles the question: `Target`, `Hit`, `PositionOverride`, `UseOverride`.  That is the entry an
  *attacker* calls to deal damage, not one a victim implements to take it, so calling it on a
  victim runs and applies nothing (correctly).  This build exposes no take-damage entry that
  carries an amount: `TryApplyDamage` refuses a module-built payload, `DamageHP` is out-only.  HP
  still converges through the documented clamped write, so a shared enemy's health is right on
  both screens — only the *reaction animation* is missing.
- **Cutscenes** are ideal when the game starts them through a cutscene *manager*: the
  partner plays the same sequence.  A sequence started directly by an altar/egg Blueprint
  has no manager to hand over, so the partner is **held consistently** (input locked) for
  the same interval instead of playing it.  You get the same beat, not the same camera.
  (Play, hold and joint resume are all proven — Session 17, `Dek_Egg_Activation`.)
- **Boss bars** are raised per machine from the content's own `EnemyData.Boss` flag; the
  `Enemy_IsPartOfBossEncounter` predicate is NOT implemented on this build's enemy classes.  An
  enemy the content does not call a boss never gets a bar on either machine, however dangerous
  it is.
- **Some bosses will not appear even with both players in the arena.**  The `ROSDragon_Spawner`
  in `760_SZ_AUT` did not fire in four minutes of Session 17 with both players at its exact
  radius, while the level reported `engineVisible=0` — a spawner's own
  `PlayerIsInActivationRange` reads player 0 of *its* machine, and the area's level manager had
  the level loaded but not shown.  If a boss you are expecting is not there, that is the shape of
  this failure; walking away and back is the workaround.
- **The relay is plaintext.**  LAN only unless you set `AllowPlaintextInternet=1` and
  accept that anyone on the path can read and forge the traffic.
- **One writer.**  `HostAuthoritative=1` is not optional.  Two writers on one progression
  container is how both saves become wrong instead of one becoming stale.
- **A hard map load only lands you in a real world.**  `900_SipehaWorld` and
  `990_B33sment` place the arriving player (marker, then partner).  A streaming area such as
  `210_AQ_underground` or `780_SZ_Depths` starts no GameMode when its package is loaded on
  its own, so the game spawns **no pawn at all** and the mod reports
  `arrive: <map> ended with no pawn to place`.  Do not use the hard-load path to reach
  those areas; a human should walk/teleport in-game instead.
- **The hub teleporter does not take the E press in this save.**  Both `TempTeleporter`
  and `Teleporter_GR` answer `IsInteractible=0` even at point-blank range and after
  `UnlockAllTeleporters`; the `Interact` event runs but no travel verb follows.  Fast travel
  therefore has to be done from the game's own UI, not by asking the mod to press the node.
- **The shop door is interactible but does not open on a press.**  `Shop_Door_00_SM` says
  `IsInteractible=1`, the real interact event runs, its own `Interact` runs, and
  `opened=0` stays — even with `GiveAllKeys`.  The lane is fine (a pickup's
  `actor-state refresh ok` lands on the host); the door's own handler is the open question.
  **`IsInteractible=0` is not the same as "this cannot be opened"**: the area chests say 0 and
  open normally when pressed.  Report what the *object* did, not what the gate said.
- **Loot hiding needs the SAME enemy on both machines.**  In `900_SipehaWorld` both
  machines hold the same `Sipeha_BP_C` (`guid=BB49E51D`), but that enemy drops nothing.  In
  the hub the two machines' spawner enemies may have different authored GUIDs, so the drop keys
  cannot match and `loot: hiding local drop` will not fire.  If you see the drop vanish on both
  screens but no hiding line, this is one of the reasons.
- **Kill lists diverge in the hub, by design.**  In a 5-minute soak the host recorded 104
  deaths and the peer 49 (105 unique GUIDs): each machine runs its own spawner enemies.
  This is not a lost row — both processes survived with zero errors and the only warnings
  were the lanes' own stale-actor self-heals.  Compare kill lists only in a world where the
  two machines hold the same authored enemy GUID.
