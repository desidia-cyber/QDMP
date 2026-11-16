# play-p2b — bosses on both machines, and the attack lever measured

Delegate id: `play-p2b`. Continues `play-p2` (CAMPAIGN-ROUTE R16-R29, bosses B2/B3/B4).
Rig: `mod/tools/session-p2b.ps1`. Deliverable: `mod/doc/playthrough/p2.md` §3 (updated).

play-p2 closed four gaps as "the boss existed on the host only" and "real attacks moved no
boss HP". Both statements were true, and both had a single, findable cause. This pass fixes
the two mod bugs, proves the boss body/bar on both screens, and measures the attack lever
against an ORDINARY enemy so the answer is no longer ambiguous.

## 1. The mod bugs (fixed in `mod/native/src/proxy.cpp`)

### 1a. `EnemySpawnPump` — a boss's body depended on where the partner stood

`EnemySpawnPump` describes an owned enemy's CLASS on `K_ENEMY_SPAWN`, and the receiver
constructs a copy from it. The send was gated by `kMirrorRelevanceCm` (25 m to the partner's
drawn position) and `havePeer`. For a boss that gate deadlocks: the partner cannot stand
beside a boss it does not have, and the class description is what would give it one.

Measured (play-p2 run 6, `300_LIB`, and the rig's own `qd.tp near:DemonSpider` on both
machines landing 30.6 m apart because each machine resolves the name to its OWN actor):

```
HOST 7:26:10.457 enemy id: spawner E6CBFA0B (Necronomicon_BP_C)
PEER 7:26:10.615 WARN boss phase: the partner says boss E6CBFA0B is at state 0 but this
                machine holds no live copy -- nothing to drive
HOST 7:26:23.003 WARN audit: DESYNC boss e6cbfa0b… host=0 peer=-1 [absent on the peer]
(no `enemy spawn: described guid=E6CBFA0B` anywhere in that session)
```

The fix: **a boss is not an audience question.** For a record the boss lane has identified
(`r.isBoss`), the relevance radius and `havePeer` are both skipped, and the class is
re-offered every `kBossRedescribeMs` (2.5 s) while the boss lane holds no ACK for that key
(`bosslane::PeerAcked`, the peer's own "I hold a live copy" answer). Ordinary enemies keep the
25 m rule exactly as before, so the receiver's queue cannot fill with bodies nobody can see.
The silent `continue`s in the loop also got a rate-limited line for "no class asset path", so
"the mod never asked" and "the engine refused" can never again look like "the audience was
too far".

Session 20's original reason for the radius (a boss described 177 m away, `mirrorPathSent`
set forever, and nothing re-sent when the peer finally arrived) is answered rather than
reverted: the re-offer loop is what makes a lost first description harmless.

### 1b. `BossPump` — the one shared bar could bind to a bystander, permanently

Candidates are ranked by HP fraction, ties by the LOW 32 BITS OF THE KEY. Measured:

```
HOST 7:26:01…  boss: bar key=14FE95EF hp=65535 phase=0 host=1 bound=1   (every 3 s, whole fight)
HOST 7:26:10.477 boss: guid=E6CBFA0B is a boss                        (the encounter boss)
```

`14FE95EF` is a `Boner_ExecutionerMiniboss_BP_C` at full HP standing elsewhere in `300_LIB`.
The Necronomicon was also at full HP, and `E6CBFA0B > 14FE95EF`, so the key order picked the
bystander — and because the bar then never changed, the peer was never even ASKED about the
real encounter boss. Both screens showed a bar for a miniboss nobody was fighting.

The fix: at equal HP, prefer the candidate NEARER the players (nearest of the local player and
the partner's drawn position), with a 500 cm band so two bosses at a similar range cannot swap
the bar every tick; the key order stays as the final deterministic tie so both machines still
agree. "Most damaged wins" is unchanged.

## 2. The attack lever — measured, and the mod is not the cause

`qd.attack` calls every `InpActEvt_<action>*` binding on the pawn (combat-grip's fix). On this
rig, at 180 cm, with the sword drawn and explicitly equipped, it does not damage anything:

```
control (ordinary enemy, Boner_Standard_Fighter_BP_C, 100 HP, 180 cm away)
  pawn state: CurrentInHandWeapon=ptr… WeaponsArePutAway=0 HurtBoxActive=0
  draw:       rig: attack EquipWeapon1 gap 300 ms x1
  equip:      rig: equip EquipItem(…/RustySword_Item, slot 0) ran=1
  5 rounds of Attack / SecondaryAttack  ->  HP 100 -> 100
  shared damage lane on the SAME enemy  ->  HP 100 ->  75
```

The montage line names what the input actually played, and it is a hit RESPONSE, not an
attack (`Player_FPArms_LeftHand_MeleeResponse_Montage`; in the dead case
`Player_FPArms_Death_Montage`). So "a montage changed" is not "the attack fired" — the same
lesson combat-grip recorded in the other direction.

Conclusion, stated as narrowly as it was measured: **no real player attack on this rig
damages any enemy, boss or ordinary**, with the weapon drawn, the weapon item explicitly
equipped, at 180 cm, god off — while the same enemy's HP is provably writable (the intent
lane lowered it 100 → 75). The gap is the melee HIT (the game never opens the hitbox:
`HurtBoxActive=0`, and the arms play their response layer instead of a swing), not the boss,
not the damage lane, and not enemy ownership.

## 3. Coverage

* Every boss the boss lane identifies (`r.isBoss`, from `EnemyIsBoss` on the actor or its
  spawner) is mirrored regardless of audience: B2 Whale King, B3 Necronomicon, B4 Demon
  Spider + Hellbat, the dragons, the Shrine bosses, the Queen. Nothing is hard-coded.
* Every ordinary enemy keeps the 25 m relevance rule (unchanged).
* The bar tie-break applies to every boss, not just the ones this pass exercised.

## 4. Files

| file | change |
|---|---|
| `mod/native/src/proxy.cpp` | `EnemyRec::mirrorPathAt`; `EnemySpawnPump` (boss bypass + re-offer + no-path diagnostic); `bosslane::PeerAcked`; `BossPump` (distance tie-break) |
| `mod/tools/session-p2b.ps1` | NEW rig: phases `bosses` / `damage` / `rest`; deletes the death-penalty sidecar; reads `Health`, not `HP`; places both players by enemy KEY |
| `mod/doc/playthrough/p2.md` | the corrected per-step table |
| `mod/doc/progress/play-p2b.md` | the running journal |

## 5. Build / test

`mod\native\build.bat` → `winmm.dll` 1,840,128 bytes. `mod\native\test\run_tests.bat` →
**56 test executables pass, 0 failed**. `cargo` untouched (no wire change).

## 6. Proof

See `mod/doc/playthrough/p2.md` §3 for the per-step table and the log lines.
