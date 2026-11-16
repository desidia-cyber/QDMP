# gap-combat — enemy-spawned hazards, damage in both directions, enemy attack animations

Date: 2026-11-16
Owner: `gap-combat`
Closes: COVERAGE-MATRIX GAP 5 (spawned hazards are host-only), GAP 13 (`DAMAGE` points one
way), GAP 14 (enemy attack animation mirroring unproven).

---

## 1. What was broken, measured

**GAP 5 — a spawned hazard exists on one machine.** An enemy or a boss spawns its damaging
fields at RUNTIME: `DemonBoss_FightController_C` drops despawnable magma puddles along a
spline and applies lava damage (`SpawnMagmaPuddle` / `UpdateMagmaPuddles` /
`Check Puddles Colliding With Player` / `LavaDamage` / `DamageAmount` / `TryApplyDamage`,
`analysis/pak/answers-2.md` §D9.4), the Queen and the Witch drop `*_AOE` volumes, the
dragons leave fire / poison / sticky-binding puddles.  Every one of them existed on the
machine that spawned it and on no other, so the partner stood in visible-or-invisible lava
and was either burned or untouched depending on which machine happened to run the boss.
*Placed* hazards (`DamagingCollision_C` ×489, the `*_TrapDecoy_C` families) are in the level
on both machines and were never part of this gap.

**GAP 13 — `K_DAMAGE` pointed host → peer only.** Ownership follows the nearer player
(`qdenemyown.h`), so the enemy standing beside the PARTNER is run by the partner.  Its
swing at the host resolved on the partner's machine and stopped there: `OnPlayerDamage`
refused every packet that was not arriving at a peer (`!g_net.IsPeer() return`), and both
senders (`TrySendEnemyDamage`, `PlayerBodyObserve`) bailed out on anything that was not the
host.  The host was silently damage-immune to every enemy its partner ran.

**GAP 14 — enemy attack animation mirroring was unproven.** `PlayEnemyMontage` resolves
`Montage_Play` reflectively, sets its arguments by name, guards the call and reads
`GetCurrentActiveMontage` back — but a single `Montage_Play verified by read-back` line in
one earlier session was all the evidence that existed, and nothing counted playbacks, so
"an enemy's attacks, hit reactions and deaths animate on the follower copy" could not be
stated for a fight.  Two defects made it fail in one: (a) `OnEnemyDetail` gated on a row
younger than 1 s, while enemy rows are a 16-row rotation over every enemy the owner runs
(~600 records in the castle ⇒ a given enemy's row is ~2 s old **by design**), so the attack
montages were thrown away exactly when there were many enemies; (b) there was no counter,
so a run could not distinguish "played" from "a line appeared somewhere".

---

## 2. The design

### 2.1 The rule that makes hazards safe (§`qdhazard.h`)

A player's HP is owned by exactly one machine — the one that player is on.  Therefore the
damage a hazard does to a player is applied **by that player's machine**:

* the machine that spawned the hazard lets its **own game** do its job on its **own** player;
* the other machine receives the hazard — a collision-less copy for the eye, plus the
  geometry — and applies the damage to **its own** player while that player stands in it;
* the copy has collision **and** actor tick off, so the receiving game cannot apply it a
  second time, and the machine that spawned it never touches the partner's HP at all.

That is "the damage applied to the local player by that player's own machine, from the
mirrored hazard", and double damage is impossible by construction rather than by a guard.

An enemy **projectile** is the same shape with a single hit: the receiving machine's copy
(the EFFECT lane's, already proven) is tested against its own player and applies once, and
its damage travels **once per class** rather than once per shot.

### 2.2 Identity

A spawned hazard has no GUID and no cooked path.  Its key is a hash of
`(class path, quantised spawn point, ordinal among coincident spawns)`, so the two machines
derive it independently and agree without negotiating — the same shape the summon identity
already uses (`qdenemyid.h`).  Domain word `'HAZ1'` in the fourth word keeps it from
colliding with an `FGuid`.

### 2.3 Wire (`WIRE.md` §3.34a/§3.34b, `K_HAZARD` 0x64, `K_HAZARD_INFO` 0x65)

| kind | reliable | body |
|---|---|---|
| `K_HAZARD` 0x64 | no (5 Hz, superseded) | `rev u32, n u16, n × {key 16, px,py,pz i32, radius_cm u16, tick_ms u16, amount_centi i32, flags u8}` (37 B a row, ≤24 rows) |
| `K_HAZARD_INFO` 0x65 | yes | `kind u8, key 16, px,py,pz i32, amount_centi i32, radius_cm u16, tick_ms u16, flags u8, cls str` |

`HI_FIELD` = this key exists and is a `<class>`; `HI_FIELD_END` = it is gone;
`HI_CLASS_DMG` = a projectile CLASS hits for N.  The amount is a **description**, never an
instruction.

### 2.4 How a hazard is found, generically

* the spawn ring the combat-vfx lane already uses (`ReceiveBeginPlay` → `g_fxRing`) is
  drained with **this lane's own cursor**, so both lanes see every spawn;
* the class name decides the shape — field vocabulary (`puddle`, `magma`, `lava`, `aoe`,
  `hazard`, `damaging`, `firefield`, `firezone`, `flamewall`, `spikefield`, `acidpool`,
  `poisonpool`, `poisoncloud`, `gascloud`, `damagezone`, `damagefield`) and the
  projectile vocabulary (`projectile`, `travellingwave`, `arcprojectile`, `groundwave`,
  `bolt`, `arrow`), with the engine's own `ProjectileMovement` as the primary signal;
  montages / materials / cues / decals / decorations are refused by name;
* **attribution**: the actor is accepted only when its `Owner`/`Instigator` chain reaches an
  enemy record, a class deriving from `EnemyBase_C`, or a class named `*Boss*` /
  `*FightController*` / `*Enemy*`.  A hazard the LOCAL PLAYER spawned (a weapon's own fire
  puddle) is refused, so this lane can never burn the partner with the other player's weapon;
* the volume, amount and cadence are read from the actor and then its owner chain
  (`DamageAmount`/`LavaDamage`/`DamagePerTick`/`DPS`…, `DamageRadius`/`AoeRadius`…/a shape
  component's `SphereRadius` or `BoxExtent`, `DamageTickRate`/`TickInterval`…), and anything
  that does not resolve is the stated small fallback (**radius 300 cm, amount 5, cadence
  500 ms**) with `HU_HAZARD_GUESSED` set and named in every log line;
* **two hazards at the same place are one hazard** (100 cm, horizontal): a spawner commonly
  produces a volume and a coincident decorative actor for the same effect.

### 2.5 Damage both ways (§`qdenemydmg.h` + `damage_both_ways`)

`K_DAMAGE` is addressed to the machine that owns the **victim's** HP.  Each machine owns
exactly one player, its own, so:

* the **sender** is whoever RUNS the enemy — the `O_LOCAL` gate, not a role;
* the **receiver** applies it to `g_localPawn`, whichever role it has;
* the sender never applies it, so a hit lands exactly once.

`PlayerBodyObserve` (the game's own hit on the partner's body) forwards in both directions
for the same reason — the body is always the PARTNER's, so the hit belongs to the other
machine — and refuses a hazard's hit, because the hazard lane already owns that damage on
the victim's machine.

### 2.6 Enemy attack animations

* `OnEnemyDetail`'s freshness gate is now the lane's own dead-row bound (5 s) instead of 1 s,
  with the reason logged and counted (`animStale=`), so a fight with hundreds of enemies
  stops discarding the attack montages it exists to mirror;
* the lane has its own scoreboard: `animApplied` (verified playbacks), `animFailed`,
  `animStale`, reported in the once-a-second `enemy detail:` line and in every
  `enemy detail applied … played` line.

---

## 3. Coverage — the hazard classes this handles

Enumerated from `analysis/pak/pak_assets.txt` (the shipped archive) and the class vocabulary
those names use.  All are handled by the *rules*, not by a class list:

**Enemy/boss spawned damaging fields (12 puddle classes + the AOE volume family)**
`DemonSpider_MagmaPuddle`, `Dragon_FirePuddle` (+`_FX`), `FirePuddle`, `Fisher_SZ_FirePuddle`,
`Manticore_Disease_Puddle`, `Manticore_Weaken_Puddle`, `ROS_Dragon_PoisonPuddle` (+`_FX`),
`ROS_Dragon_StickyBindingPuddle` (+`_FX`), `SpectralSword_FirePuddle` (+`_BP`);
`Queen_AOE`, `Queen_P1_AOE`, `Queen_P2_AOE`, `Queen_P2_AOE_Swipe`, `Witch_AOE`,
`Boner_Witch_AOE`, `Voxord_AOE`, `SuperCube_AOE`, `Zweihander_AOE`, `Orbiter_AOE`,
`Orbiter_Attack_AOE`, `Balooner_Aoe`, `Balooner_Big_Aoe`, `LargeRobotAOEEdges`,
`WaterMiniboss_Disease_AOE*`, `FishMan_AOE_*`, and any other actor whose class name carries
the field vocabulary.

**Enemy projectiles (~120 classes)** — every class the EFFECT lane already draws from a
machine that runs the shooter: `Enemy_Projectile_Base`, `Enemy_GroundWave_Projectile`,
`QueenLaser_GroundWave`, `GraveBoss_GroundWaveProjectile`, `GraveBoss_SlashProjectile`,
`Hellbat_FireProjectile`, `Hellbat_FireSonicProjectile`, `Bat_SonicProjectile`,
`BigBat_SonicProjectile`, `Barnacle_*_Projectile*`, `Fishmaner_Projectile*`,
`GuardianFishman_Projectile`, `Golem_*_Projectile`, `Dragonfly_*_Projectile`,
`EyePeaSyringProjectile`, `FishSpawnProjectile`, `DiseaseSlime_SpawnProjectile`,
`Crowprojectile`, `BigNut_Projectile_BP`, `BombLizerProjectile`, `Josami_*Projectile*`,
etc.

**Not handled, by decision**
* PLACED hazards (`DamagingCollision_C` ×489, `*_TrapDecoy_C` ×784, `DS_Plate_*`): they are
  in the level on both machines, so each machine's own volume already damages its own
  player.  Replicating them would double-burn.
* An AoE the boss applies **inline**, with no spawned actor to mirror (a bare
  `TryApplyDamage` around `GetPlayerCharacter` with no volume actor).  There is nothing on
  the wire to describe; the lane cannot invent a volume, and inventing one would burn a
  player standing somewhere the boss never hit.  Named here rather than silently missed.
* Damage-over-time applied by the game through a STATUS effect already rides `STATUS`
  (COVERAGE-MATRIX C9).

---

## 4. Switches

| `[Rules]` key | default | scope |
|---|---|---|
| `HazardSync` | 0 | mirror an enemy/boss-spawned hazard; each machine hurts only its own player |
| `DamageBothWays` | 0 | `K_DAMAGE` describes a hit on the OTHER machine's player |

Both are separate because they are separate risks: the first writes damage to the local
player from a mirrored volume, the second widens an existing, proven receiver.

---

## 5. Files

* **new** `mod/native/src/qdhazard.h` — the classification, identity, containment, cadence,
  de-duplication and rate-bound rules (all pure).
* **new** `mod/native/test/test_hazard.cpp` — 90 assertions pinning every one of them.
* **new** `mod/tools/session-gapcombat.ps1` — the two-instance proof.
* `mod/native/src/proxy.cpp` — `#include "qdhazard.h"`; the hazard lane
  (`Hz*`, `HazardPump`, `OnHazards`, `OnHazardInfo`, `HzIsHazardActor`); the tick call;
  the two dispatch cases; the two-way gates in `TrySendEnemyDamage`, `OnPlayerDamage` and
  `PlayerBodyObserve`; the montage scoreboard in `PlayEnemyMontage`/`OnEnemyDetail`/
  `EnemyDetailPump`; the `qd.hazard` rig lever.
* `mod/native/src/qdwire.h` — `K_HAZARD` 0x64, `K_HAZARD_INFO` 0x65, their bodies, writers and
  readers; `K_HAZARD_INFO` in `IsReliableKind`.
* `mod/native/src/qdconfig.h`, `mod/qdcoop.ini` — `HazardSync`, `DamageBothWays`.
* `mod/rust/qd-core/src/protocol.rs` — `Kind::Hazard`/`HazardInfo`, `Body::Hazard`/
  `HazardInfo`, encode/decode, reliability classes, `MAX_HAZARD_ROWS`/`MAX_HAZARD_CLASS`.
* `mod/doc/WIRE.md` §3.34a/§3.34b.

---

## 6. Proof

Build: `mod/native/build.bat` → `winmm.dll` built; `mod/native/test/run_tests.bat` → **all
test executables passed** (38 suites, including the new `test_hazard` 90/90).
Rust: `cargo test -p qd-core --release` → 145 + 5 pass; `qd-relay` rebuilt so the relay
accepts the two new kinds (`unknown packet kind` would have dropped every hazard datagram —
the `K_KILL_XP` lesson).

Live: see §7 below (filled from `_test/combatrun/out/report-gapcombat.txt`).

---

## 7. Live proof — two instances, a real fight

`mod/tools/session-gapcombat.ps1` (two instances + `qd-relay`, shipped `qdcoop.ini`,
`HazardSync=1`, `DamageBothWays=1`).  Final run **12/12 verdicts PASS**, 0 FAULT lines, both
processes alive at the end.

### GAP 14 — enemy attack animations on the follower copy

The follower's own scoreboard, in a live fight, reported every second:

```
[2] 3:57:16.196 DBG  enemy anim: applied=37 failed=0 stale=0 candidates=0
[2] 3:57:15.028 INFO enemy anim: Montage_Play verified by read-back guid=160EDA31
```

**37 verified playbacks, 0 failures, 0 stale drops**, and 37 read-back-verified
`Montage_Play` lines on the follower (0 on the owner, which is correct — the owner does not
mirror to itself).  The counter and the line agree, which is the point of adding the counter:
the previous evidence was one line in one earlier session and could not be stated for a fight.

### GAP 13 — damage both ways

The PEER (inst2) described a hit from an enemy **it** runs; the HOST applied it to **its own**
pawn:

```
[2] 3:56:39.100 INFO rig: pdmg -> guid=AA40EA1B amount=25.0 centi=2500 sent=1 (G4 receiver provocation)
[1] 3:56:39.574 INFO damage from enemy AA40EA1B melee: 25.0 applied hp 181.0 -> 156.0
```

474 ms after the peer's send, on the host, naming the same guid and the same amount, with the
HP movement.  No `damage dropped:` line was produced (counted before/after).  Before this
change the host refused every such packet at `if (!g_net.IsPeer()) return;`.

### GAP 5 — spawned hazards, both halves

**(a) The natural case, no rig lever.**  In the fight the PEER's own game spawned a Fisher's
fire puddle; the lane published it and the HOST received it:

```
[2] 3:50:27.107 INFO hazard: published key=6080525A /Game/Enemies/Fisher_00/Fisher24/Fisher_00/Fisher_SZ_FirePuddle.Fisher_SZ_FirePuddle_C at -16237,6489,-17131 r=300cm amount=165.0 every 500ms (ESTIMATED)
[1] 3:50:44.731 INFO hazard: the partner spawned /Game/Enemies/Fisher_00/Fisher24/Fisher_00/Fisher_SZ_FirePuddle.Fisher_SZ_FirePuddle_C key=6080525A r=300cm amount=165.0 every 500ms
```

Note the three consecutive rows at different positions: that puddle MOVES, and the 8 cm
change threshold is what lets it follow without a packet per frame.

**(b) The damage half, measured.**  The HOST spawned a real hazard class on its own player
(`qd.hazard`, owned by a real enemy 70 cm away — the shape `DemonBoss_FightController_C`
produces); the PEER heard about it, DREW it, and burned on its own player:

```
[1] 3:57:01.950 INFO hazard: published key=A150C808 /Game/Enemies/Dragon/Dragon_FirePuddle.Dragon_FirePuddle_C at 1807,-3160,2311 r=300cm amount=10.0 every 500ms (ESTIMATED)
[2] 3:57:02.179 INFO hazard: the partner spawned /Game/Enemies/Dragon/Dragon_FirePuddle.Dragon_FirePuddle_C key=A150C808 r=300cm
[2] 3:57:02.179 INFO hazard: copy of /Game/Enemies/Dragon/Dragon_FirePuddle.Dragon_FirePuddle_C drawn at 1807,-3160,2311 r=300cm (collision off, tick off)
[2] 3:57:02.547 INFO hazard: standing in ...Dragon_FirePuddle_C key=A150C808 r=300cm 10.0 (ESTIMATED amount) applied hp 180.0 -> 170.0
[2] 3:57:02.841 INFO hazard: standing in ...Dragon_FirePuddle.Dragon_FirePuddle_C key=A150C808 r=300cm 10.0 (ESTIMATED amount) applied hp 170.0 -> 160.0
[2] 3:57:03.384 INFO hazard: standing in ...Dragon_FirePuddle.Dragon_FirePuddle_C key=A150C808 r=300cm 10.0 (ESTIMATED amount) applied hp 160.0 -> 150.0
```

**23 applications in 30 s, 17 with measured HP movement** (0.8/s: the hazard's own 500 ms
cadence plus the co-location delay — not per frame).  **The spawner logged 0
`hazard: standing in` lines**: it never applies the mirror to itself, which is the
no-double-damage half, and it holds by construction rather than by a guard.

The first run of the very same script measured the two honest REFUSALS that shaped the
final result, and they are recorded here rather than deleted:

* the peer applied 0 times because the two players were **40 km apart** (its own player was
  nowhere near the hazard) — a refused precondition, not a lane failure;
* the peer's applications then logged `NO effect hp 0.0 -> 0.0` because the fight had already
  beaten its player to 0 HP — the script now revives both victims (`qd.selfhp 400`) and the
  verdict requires measured HP movement rather than an attempt.

### Which switch was flipped on which evidence

`HazardSync=1` and `DamageBothWays=1` are both **1** in `mod/qdcoop.ini`, on the lines above.

---

## 8. Open risks / for the coordinator

1. **An AoE a boss applies with no spawned actor cannot be mirrored.**  If a boss calls
   `TryApplyDamage` around `GetPlayerCharacter` without spawning a volume, there is nothing
   on the wire to describe, and inventing a volume would burn a player where the boss never
   hit.  The Demon Bosses' magma (the case GAP 5 named) DOES spawn puddle actors, so it is
   covered; an inline-only AoE, if one exists, is not.
2. **Amount and radius are read reflectively** (`DamageAmount`/`LavaDamage`/…,
   `DamageRadius`/`AoeRadius`/a shape component).  When they do not resolve the value is the
   stated small fallback, flagged `HU_HAZARD_GUESSED` and named in every log line — measured
   on two different puddles: the Fisher puddle resolved `amount=165.0`, the dragon puddle
   resolved `amount=10.0` (and `60.0` from a different owner on an earlier run), always with
   the `(ESTIMATED)` marker on the radius.  A hazard whose real damage is much larger than
   either would under-hit the partner rather than over-hit them.
3. **Hazard keys are positional.**  A hazard that TELEPORTS further than 100 cm keeps its
   key, so it is one hazard that moved; a hazard re-spawned at a new point is a new hazard.
   Both behaviours are what the receiver wants, but a boss that respawns the SAME puddle
   identity at a new place will record two keys and the old one expires by staleness (2 s).
4. **`qd.hazard` is a rig lever, not a product feature.**  It spawns a real class with a real
   enemy owner; it exists because the rig cannot make a boss drop a puddle on demand.  It is
   inert without its control file.
5. **`DamageBothWays` widens an existing lane.**  A machine that both RUNS the enemy and owns
   the victim still applies locally and never sends (the sender always suppresses), so the
   only new traffic is the case that used to be a no-op.  What is not covered is an enemy both
   machines believe they run — `qdenemyown.h`'s conflict resolution is what stops that, and
   it is unchanged.
