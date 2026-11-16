# play-p2b progress journal (successor of play-p2)

**Status: research DONE, code fix in progress.** Goal: fix + prove, in order, (1) bosses exist
only on the host, (2) real player attacks do not damage the boss, (3) magma hazard damage,
(4) re-run R17 / R24 / B2 / R27. Then update `mod/doc/playthrough/p2.md`.

Lock state at 07:56: BUILD = `play-p3b` (07:44), LIVE = `play-p1` (07:51, 3 processes running:
pids 22520 relay, 39280, 87948). I hold nothing yet and started nothing.

## 1. FINDINGS FROM THE PREVIOUS RUN'S OWN LOGS (all measured, `_test`/Binaries logs)

### 1a. `peers=0` was MISREAD by play-p2 — it is the healthy value
`EnemyKeyPeerCount` (proxy.cpp:23355) counts DUPLICATE records for the same key on THIS machine
("0 is the healthy answer"). It says nothing about the partner. The real evidence for
"host only" is that `qd.enemies` on the PEER had **no row at all** for the boss, and the
auditor agreed: `audit: DESYNC boss e6cbfa0b… host=0 peer=-1 [absent on the peer]`
(host log, 07:26:23 / 07:27:01).

### 1b. ROOT CAUSE of item 1 — the mirror lane's 25 m audience test deadlocks on a boss
The class-description lane (`EnemySpawnPump`, proxy.cpp:9207) only sends
`K_ENEMY_SPAWN` for an owned enemy that is **within 2500 cm of the partner's drawn position**
(`kMirrorRelevanceCm`, proxy.cpp:9205) and only when `havePeer` (a non-frozen peer sample).
For the Necronomicon that never held:

```
HOST 7:26:10.457 enemy id: spawner E6CBFA0B (Necronomicon_BP_C)
HOST 7:26:22.853 enemykey E6CBFA0B ALIVE local=1 src=0 hp=3000.0 mx=3000.0 peers=0
PEER 7:26:10.615 WARN boss phase: the partner says boss E6CBFA0B is at state 0 but this
                machine holds no live copy -- nothing to drive
HOST 7:26:23.003 WARN audit: DESYNC boss e6cbfa0b… host=0 peer=-1 [absent on the peer]
(no `enemy spawn: described guid=E6CBFA0B` on the host anywhere in the session)
```
and the reason the peer was OUT of that radius is a chicken-and-egg the rig then closed badly:
`qd.tp near:DemonSpider` resolves **each machine's own** placed actor, so
```
HOST 7:41:28.991 dev teleport near 'DemonSpider' -> … -16947,19171,-8369 … standing at -16536,18735,-8219
PEER 7:41:31.951 dev teleport near 'DemonSpider' -> … -15225,16953,-7120 … standing at -14731,16614,-6970
```
— 30.6 m apart, i.e. the peer stood 30 m from the boss, so the host's 25 m test failed and the
peer never got the body. The host finally described the spider at **7:49:56** (the last second of
the run) after the boss lane NACKed it enough times (`kBossNack` re-arms `mirrorPathSent`,
proxy.cpp:10650) — proof the lane itself works and that the gate, not the mechanism, is wrong.
`enemy spawn: described guid=30CACF5B … DemonSpider_BP_C` at 7:49:56.654, peer constructed it at
7:49:56.717.

### 1c. The one shared BOSS BAR picked the wrong boss (and stayed on it)
```
HOST 7:26:01… every 3 s: boss: bar key=14FE95EF hp=65535 phase=0 host=1 bound=1
HOST 7:26:10.477 boss: guid=E6CBFA0B is a boss        <- the real encounter boss, never selected
```
`14FE95EF` is `Boner_ExecutionerMiniboss_BP_C`. Both bosses are at full HP (`frac` 1.0), and the
tie is broken by the LOW 32 BITS OF THE KEY (proxy.cpp:10128-10131), so the miniboss wins and the
Necronomicon is never asked about, never ACKed, never NACKed, never re-described.
(For B4 the same: `boss: bar key=14FE95EF` all through the demon fight.)

### 1d. ROOT CAUSE of item 2 — the HOST PLAYER WAS DEAD for every "real attack"
```
HOST 7:42:09.323 rig: attack Attack fired 2 event(s) keyEvent=0
                 -> montage Player_FPArms_Death_Montage => Player_FPArms_Death_Montage
PEER 7:26:44.356 rig: attack Attack fired 2 event(s) keyEvent=0
                 -> montage none => Player_FPArms_Std_Charge_Attack_Montage  CHANGED
```
The peer's attack input plays a REAL attack montage; the host's plays the **death** arm montage.
Cause: the per-instance respawn sidecar survives between runs —
`7:18:12.992 respawn: restored 7 death stack(s) from …\qdcoop-p21-respawn.ini (max 30%)` — so the
host enters every run at 30 % max HP, and by 7:25 the host had 9 stacks: `respawn: the game
computed max HP 124.0 -> 12.4 (10%, 9 death(s))`, i.e. 12 HP, dead in one hit, respawn every 2 s.
A dead pawn fires no attack, so no boss HP moved. **The mod's damage lane was never exercised.**
Secondary rig defect: both players were parked at `offset=600` (6 m) — far outside melee reach.

### 1e. `triggered 0->0` is NOT a failure
```
HOST 7:40:53.740 trigger: boss follow-through (local) ActivationTrigger4.DoTrigger ran;
                 DemonSpider_Spawner Activate ran -- managed enemy none
HOST 7:40:53.740 rig: trigger 'ActivationTrigger4' begin event: dispatched triggered 0->0
HOST 7:40:59.715 enemy id: spawner 30CACF5B (DemonSpider_BP_C)   <- it DID spawn
```
So R19/B2's `triggered 0->0 => FAIL` verdict in p2.md is a reader bug. The trigger fired and the
spawner ran; `triggered` is the trigger-volume counter, not the spawn result. Same for
`Necronomicon_Boss_Spawner` (7:26:11 `boss follow-through (local) … ActivateEnemy ran -- managed
enemy present`).

### 1f. Item 3 (magma): the hazard lane never saw a magma puddle in that run
`hazard:` lines in 07:40-07:50 are only a Boner projectile. No `magma`/`puddle` class was ever
scanned, because the players were never inside the demon arena (1c/1b), so
`DemonBoss_FightController_C` never spawned puddles. The HP read was ALSO wrong: the pawn's
health property is **`Health`** (a float; `FloatPropC(g_localPawn,"Health")`, proxy.cpp:7380,
14999, 22719, 25543 — the vitals lane and the selfhp lever both use it), while the rig asked for
`HP` (`dev props: local player HP=absent MaxHP=absent`). `near:me!?Health` prints
`Health=i<raw>/f<*100>`, so `f` / 100 is the HP — the rig's own `f` parser already fits.

### 1g. Item 4 name facts
* R17: the route names `Fishwife_NPC_BP_C` **and** `Fishwife_Ghost_BP_C` (CAMPAIGN-ROUTE:101);
  the rig only ever tried `Fishwife_Ghost_BP` → `no live actor`. The pak has
  `Content/Enemies/FishWifeEvil/Fishwife_EvilNPC__DummyData` and `Content/NPCs/...Fishwife`.
* R27: `Content/NPCs/Knight/Knight_Nu/Knight_Nu_NPC_BP` (and `Knight_Nu_Data`,
  `Knight_Nu_Dialogue_00`, `Ghost_Knight_Nu_Dialogue`); `Knight_Nu_QSM` is the quest-state
  manager (CAMPAIGN-ROUTE:111). The rig's `near:Knight_Nu_NPC` resolved to `Ghost_Ujji
  [Knight_Ujji_NPC_Ghost_BP_C]` — a substring/nearest-match failure, not a missing actor.
* R24: `qd.tp near:… 40` is refused by design (inside the teleporter volume); the working
  sequence, already in this rig for WorldSetSync, is place-at-450 + a real W hold.

## 2. FIX PLAN (mine)

A. `EnemySpawnPump` (proxy.cpp): a **Boss** record is described without the 25 m audience test
   and without `havePeer`, and is re-described every ~2.5 s while the boss lane has no ACK for
   it. Non-bosses keep the radius. Every refusal gets a rate-limited line naming the reason
   (the silent `continue`s are why this took a session to find).
B. `BossPump` (proxy.cpp): when two candidates have the same HP fraction, prefer the one NEARER
   the players (500 cm hysteresis) before falling back to the key order.
C. Rig `mod/tools/session-p2b.ps1`: delete the respawn sidecar before boot; `near:me!?Health`
   for HP; `qd.tp guid:<key>` so both machines stand beside the SAME boss; melee at ~200 cm;
   `Fishwife_NPC_BP`/`Fishwife_Ghost_BP` and the exact Knight Nu names; the W-walk teleporter
   sequence for R24; `qd.selfhp` to keep the attacker alive long enough to swing.

## 3. Files changed so far
* `mod/doc/progress/play-p2b.md` (this file).
* `mod/native/src/proxy.cpp`: (a) `EnemyRec::mirrorPathAt`; (b) `EnemySpawnPump` -- a boss is
  described without the 25 m audience test and without `havePeer`, and re-offered every 2.5 s
  while the boss lane has no ACK for it; a missing class path logs a reason instead of a silent
  `continue`; (c) `bosslane::PeerAcked`; (d) `BossPump` -- the HP tie-break is now DISTANCE from
  the nearest player (500 cm band) then the key, so the encounter boss wins over a bystander
  miniboss. **Built** (`winmm.dll` 1,840,128 B) and **all 56 native test executables pass**.
* `mod/tools/session-p2b.ps1` (NEW): phases `bosses` / `damage` / `rest`.

## 3b. LIVE RUN 1 -- `-Phase bosses` (08:14:54-08:21:28), 23 pass / 7 fail
Report `_test/p2b/out/report-bosses.txt`, console `_test/p2b/console-bosses.txt`, logs
`Binaries/Win64/qdcoop-p2b1.log` (HOST) / `qdcoop-p2b2.log` (PEER), screenshots
`_test/playthrough/p2b/p2b-bosses-*.png`.

**ITEM 1 IS FIXED AND PROVEN for B3.** One key, one body, one bar on BOTH machines:
```
HOST 8:16:30.521 enemy id: spawner E6CBFA0B (Necronomicon_BP_C)
HOST 8:16:30.638 enemy spawn: described guid=E6CBFA0B class=…/Necronomicon_BP_C
PEER 8:16:42.005 enemy spawn: constructed guid=E6CBFA0B class=…/Necronomicon_BP_C (by asset
                 path) at 26400,749,14591 -- AI off, driven by the owner's rows (mirrors=2)
PEER 8:16:46.144 boss: peer ACK key=E6CBFA0B -- the shared bar may be raised
HOST 8:16:54.737 boss: bar key=E6CBFA0B hp=65535 phase=0 host=1 bound=1
PEER 8:16:54.871 boss: bar key=E6CBFA0B hp=65535 phase=0 host=0 bound=1
HOST 8:16:52.914 enemykey E6CBFA0B ALIVE local=1 … hp=3000.0 mx=3000.0 cls=Necronomicon_BP_C
PEER 8:16:54.380 enemykey E6CBFA0B ALIVE local=1 … hp=3000.0 mx=3000.0 mirrored=1 cls=Necronomicon_BP_C
HOST 8:20:39 audit: pass 73 us over 11 categories / PEER 8:20:40 audit: pass 165 us
```
The re-offer line appears exactly as designed: `enemy spawn: described guid=E6CBFA0B …
(a boss the partner has not acknowledged; re-offered)`. Both bars carry the SAME key, and the
peer's row is `mirrored=1` -- the two independent facts play-p2 could not produce.

**Closed on the way:** `NecronimiconDefeated = 1` in BOTH banks (8:20); the book died on the
host (`enemykey E6CBFA0B DEAD`) with the peer's retired mirror leaving no row; audit clean.

**Still open from run 1:**
* B3 `real attacks dropped the boss HP` = FAIL: 3000 → 3000 on both, although the input
  definitely fired (`rig: attack Attack fired 2 event(s) … -> montage
  Player_FPArms_LeftHand_MeleeResponse_Montage`, and `SecondaryAttack ->
  Player_FPArms_Std_Charge_Attack_Montage`). The pawn already holds
  `/Game/Items/Weapons/RustedSword/RustySword` (`body: hand R now holds …`), so a weapon is
  NOT the missing piece. The boss had left the pedestal (`boss phase … state=1 (TRANSITION)`
  8:16:36) and the game's own range is 5000 cm (`damage: swing not sent … range=5000`), so
  neither phase nor range explains it. → run 2 is the CONTROL: the same input on an ORDINARY
  enemy in the same session.
* B4 `the host spawned the Demon Spider` = FAIL and `Hellbat` = FAIL **because of a reader bug
  in my rig**: `Read-SpawnKeys` tested `$Matches[2] -match …` and then read `$Matches[1]`, and
  the second `-match` had already replaced `$Matches`. The hell-bat DID spawn
  (`8:21:12.112 enemy id: spawner C84B01FE (Hellbat_BP_C)` and the peer constructed it at
  8:21:12.658). Fixed; the spider was genuinely absent that time and the rig now asks
  `DemonSpider_Spawner!ActivateEnemy` and waits.

## 4. NEXT STEPS
1. Run 2 `-Phase damage` (in flight at 08:23): control enemy + the same input, then the boss.
2. Re-run `-Phase bosses` with the reader fixed (B4 needs the spider).
3. `-Phase rest` for R17/R24/B2/R27.
4. Write the playthrough table + the feature doc.
