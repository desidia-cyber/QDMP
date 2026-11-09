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
ActorStateSync=1
ObjectSync=1
EnemySync=1
PuppetSync=1          ; the partner's body
PlayerBody=1          ; spawn it as the game's own player class (needs the two above)
DialogueSync=1
StreamUnion=1
TravelSync=1
CutsceneSync=1
MoverSync=1
LootSync=1
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

- **Loot hiding** needs both machines to hold their own copy of the drop.  If only the
  killer ever rolls it, the other player has a ledger entry and nothing to hide; the drop
  is still removed from the world on both screens, but the `hiding local drop` line may
  not appear.  This is the half still being measured.
- **Cutscenes** are ideal when the game starts them through a cutscene *manager*: the
  partner plays the same sequence.  A sequence started directly by an altar/egg Blueprint
  has no manager to hand over, so the partner is **held consistently** (input locked) for
  the same interval instead of playing it.  You get the same beat, not the same camera.
- **Boss bars** are raised per machine from the game's own `Enemy_IsPartOfBossEncounter`
  predicate.  An enemy the game does not call a boss never gets a bar on either machine,
  however dangerous it is.
- **The relay is plaintext.**  LAN only unless you set `AllowPlaintextInternet=1` and
  accept that anyone on the path can read and forge the traffic.
- **One writer.**  `HostAuthoritative=1` is not optional.  Two writers on one progression
  container is how both saves become wrong instead of one becoming stale.
