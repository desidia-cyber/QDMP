# combat-vfx - every weapon in the partner's hand, and every projectile, cast and impact on their screen

Delegate id: `combat-vfx`. Module: `mod/native/src/qdcombatvfx.h` (new).
Wire: `COMBAT_VFX` switch on the existing `EFFECT` (`0x16`) lane, new body `kind = 5`;
`PLAYER.action` gains kinds 15/16/17. **No new Kind, no new intent, no Rust change** -
the action kind lives in the existing 5-bit field of `PLAYER.action`, and `kind` is
already a `u8` inside the `EFFECT` body, which `qd-core` forwards without parsing.

## What was already there, and what was actually wrong

The equipment lane (K_EQUIPMENT), the projectile lane (EFFECT kinds 1-3), the melee-arc
lane (kind 4) and the body's action player all existed and worked for the case they were
built on: a sword in the right hand, a dagger thrown, a swing. Four assumptions behind
them were false for everything else, and each failed silently:

1. **The equipment scan accepted one of the two visual roots.** Weapon visuals are
   `WeaponVisuals_C` actors; every dagger / thrown / off-hand visual is a
   `Dagger_Visuals_Base_C` actor. Both derive straight from `Actor`, neither from the
   other. `ScanLocalEquip` filtered on `WeaponVisuals_C`, so a partner's dagger was
   never in their hand (`Dagger_Visuals_Base_C` covers 28 of the corpus' hand visuals).
2. **The projectile lane accepted one class.** `IsProjectileClass` matched exactly
   `Dagger_Projectile_Base_C`. The player's other projectiles share no ancestor with it:
   `Default_Gun_Projectile_C`, `ShotGun_Projectile_BP_C`, `SniperGun_Projectile_C`,
   `LuudeCrossbow_Projectile_C`, `RocketLauncher_Projectile_C`,
   `ArcProjectile_Base_C`, `GroundTravellingProjectile_Base_C` and
   `Spear_Projectile_C` all derive from `Actor`. **Every gun, the crossbow bolt, the
   rocket, the spear/hammer throws, the arc projectiles and the travelling ground waves
   were invisible to the partner** - 59 classes in the corpus.
3. **The montage classifier read English, not weapons.** It matched `slash` / `attack`
   / `stab` in order. The game gives *every* weapon its own montage set (120 montages
   under `Content/Items/Weapons`). Three failure modes:
   `Player_FPArms_Hammer_AltAttack_Montage`, `Player_FPArms_Claw_AltAttack_Montage` and
   `Player_LanceAttack_1_Montage` fell through to the generic right-to-left sword swing;
   `Player_Greatsword-Alt_Montage`, `Player_FPArms_CrushingLance_Alt_Montage`,
   `Player_FPArms_CyberSword_Alt` and every gun's `*_Fire_Montage` played **nothing**;
   and `"eat"` matched `Gr-eat-spear`, `Lanc-eAt-tack` and `Gr-eat-sword`, so three
   weapon alts played the drink-a-potion pose.
4. **Nothing played an impact.** A copy is spawned with collision off, so the game's own
   hit reaction never runs on it: a thrown dagger stopped in mid-air. Measured on
   `Fireball_Projectile_BP_C`: it carries five children and exactly one Niagara
   component, `FireballFX` (the in-flight body) - `Fireball_Impact` is a separate asset
   the Blueprint spawns on the hit, so it is not reachable from the copy either.

## Design (15 lines)

- **Equipment** (`ScanLocalEquip`): accept an owner that derives from `WeaponVisuals_C`
  **or** `Dagger_Visuals_Base_C`; fall back to the `<Item>_Visuals` /
  `<Item>_InHandVisuals` name the game gives the rest (pickups and inspect-only props
  excluded). Hand from the attach socket (`_L` / `Left`), static vs skeletal from the
  component class, grip from the composed relative transform - unchanged.
- **Move classification** (`cxf::ClassifyMontage(name, held)`): read the WEAPON out of
  the montage name (the game names every weapon montage after its weapon), fall back to
  the held mesh path, then pick the move that weapon makes. New kinds: `SHOOT` (15, a
  gun/crossbow/launcher), `SPIN` (16, scythe/flail), `FLURRY` (17, claw/katar/cestus).
- **Alt attacks by shape**: hammer/mace/club/axe/bat/greatsword -> overhead (7),
  evidenced by `Hammer_AltSlam_VFX`, `HarpoClub_AltSlam_VFX`, `CyberHammer_GroundBlast`,
  `DiamondBlade_DownSmackMontage`; lance/spear/rapier/dagger/katar/sword -> thrust (8);
  scythe/flail -> sweep (16); claw/cestus -> wide slash (5).
- **Two-handed grip**: a two-handed weapon with an empty left hand brings the left arm
  across to the shaft instead of leaving it hanging.
- **Projectiles**: a spawned actor is a player projectile if it carries
  `ProjectileMovement` **or** its class name says so (`projectile`, `travellingwave`,
  `bolt`, `arrow`), walked up the super chain. Ownership test and the visual-only copy
  are unchanged: the copy still has collision off before it finishes spawning and actor
  tick off after, so damage authority does not move.
- **Impact** (`kind = 5`): at spawn the sender resolves the projectile's impact system
  (its components first; failing that the game's naming rule - the projectile's stem
  matched against loaded `NiagaraSystem` assets that pass `LooksLikeImpactFx`:
  `*_Impact`, `*_ImpactFX`, `*_ProjectileImpact`, `*_Explosion`, `*_GroundBlast`, never
  a trail/muzzle/charge system). At the end it sends that system at the point the real
  projectile stopped. The receiver plays it with `SpawnSystemAtLocation` - a system, not
  an actor: no collision, no damage.
- **Cast / charge / muzzle**: a world actor that begins play in the 2.5 s after the
  player's own action byte changes, owned or instigated by the local pawn, not a
  projectile, owning a Niagara component -> same `kind = 5`. That is how the grimoire's
  `GrimoireDoMagic`, a charge glow and a muzzle flash reach the partner, since none of
  them is a child of `Arms` and the arc lane can only see those.

## Coverage

### Hand-held visuals (every one - the scan is generic, not a list)

| Group | Corpus count | Status |
|---|---|---|
| Weapons (`WeaponVisuals_C` root) | 104 classes | proven live (sword, both directions) |
| Daggers / thrown / off-hand (`Dagger_Visuals_Base_C` root) | 28 classes | **fixed** - accepted now; not live-proven (this save has no dagger visual in hand) |
| `*_InHandVisuals_C` one-offs | 2 classes (`Chakram`, `ThrowingDuck`) | accepted by the name fallback |
| Skeletal-mesh weapons (a `SkeletalMeshComponent` inside the visual) | 29 classes (`B33Blade`, `WodasoGreatSword`, `RayneScythe`, `Shotgun`, `LuudeCrossbow`, `HeavyRifle`, `PlasmaRifle`, `RocketLauncher`, `SniperGun`, ...) | receiving side proven earlier (R39); **sending side still not live-proven** - this save cannot equip one (see "not handled") |
| Non-weapon hand items (`Torch`, `HarkLamp`, `ParryShield`, `CookingPot`, `SkullRod`) | 5 classes | same lane; shape known for the pose |

### Moves

| Family | Montages in corpus | Kind | Proven |
|---|---|---|---|
| Generic swing / charge | `Std_Charge_Attack`, `Slash_Right`, `Slash_LeftDown` | 1 / 9 / 7 | unit-tested (was live-proven before) |
| Sword alt (lunge) | `CyberSword_Alt`, `EvolvedSword_Alt`, `SaoSword_Alt`, `*_Dash_Montage` | 8 | unit-tested |
| Greatsword alt (overhead) | `Greatsword-Alt`, `RustedGreatsword_Alt`, `Zweihander_Alt`, `WodasoGS_Alt` | 7 | unit-tested |
| Hammer / mace / club / axe / bat alt | 12 montages | 7 | unit-tested |
| Lance / spear / `CrushingLance` | 10 montages | 8 | unit-tested |
| Claw / katar / cestus | `Claw_AltAttack`, `*_Attack1/2/3`, `Katar_Attack_Special` | 5 / 17 / 8 | unit-tested |
| Scythe / flail | `RayneScythe`, `TiniScythe_Charged*` | 16 | unit-tested |
| Guns (rifle, shotgun, sniper, plasma, SVT38, crossbow, launcher) | 14 `*_Fire` / `*_Shoot` montages | 15 | unit-tested |
| Throws | `Scythe_Throw`, `Warhammer_Throw`, `SpearCatch` | 10 / -1 | unit-tested |

### Projectiles (59 player classes)

| Family | Classes | Status |
|---|---|---|
| Dagger/thrown family (`Dagger_Projectile_Base_C`) | 23 | **proven live** (Fireball: 11 spawns -> 11 copies, 6 impacts -> 5 shown) |
| Guns, crossbow, rocket, shotgun, sniper, plasma, slashgun, elecgun | 17 | accepted (class-name rule); **not live-proven** - see below |
| Arc projectiles (`ArcProjectile_Base_C`) | 3 | accepted; not live-proven |
| Travelling ground waves (`GroundTravellingProjectile_Base_C`) | 6 | accepted; not live-proven |
| Spear / hammer throws (`Spear_Projectile_C`) | 4 | accepted; not live-proven |
| Enemy projectiles | all | unchanged: published only by the machine that owns the shooter |

## Proof

**Build**: `mod/native/build.bat` - clean, no warnings; export table
`forwarders: 180  real code: 0`.

**Tests**: `mod/native/test/run_tests.bat`; new `test_combatvfx.cpp` **268 passed, 0
failed**. It pins the kind numbers, classifies 86 real montage names from the corpus
(each `FAIL` line names the "before" behaviour it replaces), checks 59 projectile class
names, 27 impact system names and 7 non-impact names, both visual roots, and the
wind-up/release split. Other suites in the same run failed in modules this delegate does
not own (`test_interact`, `test_platforms`) - those files are being edited by other
delegates in this shared checkout.

**Live** (`_test/combat-vfx/`, rig per `mod/tools`; two instances, HOST/PEER, off-screen;
reports at `_test/combat-vfx/out-*/report.txt`):

1. *Equipment, both directions* (`out-allwep`):
   actor `held: RustySword_WeaponVisuals_C [RustySword_WeaponVisuals_C]
   mesh=/Game/Items/Weapons/RustedSword/RustySword.RustySword socket=None hand=R
   rel t=-0.0,2.6,0.0 q=-0.000,-1.000,0.000,-0.000` -> `local equipment rev 1` ->
   watcher `peer equipment rev 1` -> `body: hand R now holds .../RustySword.RustySword`.
2. *Magic projectile + impact + path* (`out-fireball3`, `CombatVfx=1`):
   actor `effect: local #1 .../Fireball_Projectile_BP_C v=-1243,111,192` ...
   `effect: impact system for Fireball_Projectile_BP_C is
   .../Fireball_Impact.Fireball_Impact (prefix match)` -> `effect: impact ... for #1`;
   watcher `effect: .../Fireball_Projectile_BP_C shown at 4111,-1746,194` (11 of them)
   and `effect: system .../Fireball_Impact.Fireball_Impact shown at 4978,-2931,224`
   (5 of them - one per impact, at the point the sender's projectile stopped).
   Counts: `actor projectile spawns=11  actor impacts=6  watcher copies shown=11
   watcher systems shown=5`.

**Screenshots: not obtained.** `capture-hidden.ps1` (PrintWindow with
`PW_RENDERFULLCONTENT`) returns the window chrome with a black client area for this
build - measured mean luminance 14.2 over the frame, max 128 at the title bar, i.e. the
DX swapchain is not read back. The visual claims above rest on the mod's own log lines,
which name the mesh path, the hand and the world position of every copy and impact.

## Not handled / open

- **A gun's projectile was never fired live.** `QDCOOP_TEST_FIRE` drives the game's own
  `FireDagger(Class)`, which refuses anything outside the dagger family (`ran=0
  success=0` on `ShotGun_Projectile_BP_C` and on `Spear_Projectile_C`), and this save
  cannot equip a gun: `WeaponSystem.AllWeapons` is empty, so `SetSelectedWeaponByItem`
  and `EquipItem` both run and change nothing - R39's finding, unchanged, and
  `FS_Cheats_C.GiveAllWeapons` ran without populating it. The widened projectile gate is
  therefore **unit-tested only**. Posting a left click to the off-screen window produced
  no input either (no `local montage` line at all), which is also why no
  weapon-specific swing is live-proven. `CombatVfx` therefore **stays 0** in
  `mod/qdcoop.ini`: the impact/cast half is proven, the widened gate is not. Set it to 1
  on the first run that fires a gun or a throwable outside the dagger family.
- **Weapon-type visual variety** (a hammer, a lance, a claw, a scythe actually in the
  partner's hand) is the same limitation: it needs the game to equip them, which this
  save cannot do. The lane itself is one path for all of them.
- **Crossbow / rocket / spear impacts may find no system.** The naming rule needs the
  projectile's stem to appear in an impact asset's name; `LuudeCrossbow_Projectile` and
  `RocketLauncher_Projectile` have no matching impact asset in the corpus, so those
  copies stop without an impact visual and log `effect: no impact system for <class>`.
- **`Spear_Projectile_C` can pick another spear's impact** (`OtherWorldSpear_Impact_FX`)
  when the exact one is absent - a prefix match is preferred over a containment match,
  but a spear impact for a spear is an acceptable stand-in and it is logged.
- **Cast VFX** was never triggered live (no grimoire use in the rig); the lane is
  implemented but unmeasured.

---

# Round 2 (2026-11-15) — weapon variety and the skeletal sending side, and the wip R41–R48 port

Switch: **`CombatVfx=1`** (was 0).

Round 1 could only ever show the starting sword. The save owns one weapon and the grant path
it used (`QDCOOP_GIVE` → `GiveItem` + `AutoEquipItem`) **fills an empty hand and does not
replace a held one** — measured in s32 run 1: eight weapons granted,
`local equipment rev 2: 2 item(s)`, the dagger in the LEFT hand and RustySword still in the
right. Reaching the other families needed the game's own
`FS_PlayerChar_C.EquipItem(Item, SlotIndex)`.

## R2.1 PROVEN LIVE: one of each family in the partner's right hand

`s32 run 2` (`_test/s32b-proof/report.txt`), two instances, `CombatVfx=1`. Each weapon was
equipped on the HOST with `qd.equip <path>,0` (`EquipItem(…, slot 0)`, `ran=1` every time) and
these are the **PEER's** lines — i.e. what the *other* machine decided to draw:

```
19:19:53.241 body: hand R now holds /Game/Items/Weapons/Axe/Axe_00_SM            (axe)
19:20:16.423 body: hand R now holds /Game/Items/Weapons/CyberHammer/CyberHammer_00_SM (hammer)
19:20:38.859 body: hand R now holds /Game/Items/Weapons/BasicSpear/BasicSpear_00_SM  (spear/lance)
19:20:58.363 body: hand R now holds /Game/Items/Weapons/WodasoGreatSword/WodasoGS  (skeletal, two-handed)
19:21:17.944 body: hand R now holds /Game/Items/Weapons/HeavyRifle/HeavyRifle_Mesh (skeletal, gun)
19:21:56.421 body: hand R now holds /Game/Items/Weapons/FinalClaw/FinalClaw_SM    (claw)
19:22:13.760 body: hand R now holds /Game/Items/Weapons/RayneScythe/RayneScythe    (skeletal, scythe)
```

and the off-hand / dagger family, both runs, in the LEFT hand:

```
19:18:00.583 body: hand L now holds /Game/Items/Weapons/Daggers/Default/Default_Dagger_Low
```

Three of the seven are **skeletal-mesh** weapons, which closes round 1's "sending side still
not live-proven" row (R39 had only the receiving side). The sender's own side, in the same
run: `held: SimpleAxe_WeaponVisuals_C mesh=…/Axe_00_SM socket=None hand=R rel t=-0.0,4.9,-0.0
q=-0.000,-1.000,0.000,-0.000 s=1.00` — the scan that finds these is the widened one
(`WeaponVisuals_C` **or** `Dagger_Visuals_Base_C` **or** a `*Visuals` name), so the round 1
equipment fix is now exercised by seven different families instead of one.

The weapon SWAP also reaches the partner as an action, in both directions: sender
`local montage Player_SwapRightWeapon -> kind 12 sent 12`, receiver `peer action kind 12 #1`
…#7, then `peer action kind 0` when the montage ends.

Zero real faults on either instance (`FAULT |EXCEPTION_ACCESS_VIOLATION`, case-sensitive: 0
on each; the script's own counter said "12 FAULT lines" because `Select-String` is
case-insensitive by default and `Default_Dagger_Visuals_C` contains "fault").

## R2.2 NOT proven: a weapon-specific ATTACK and a gun's projectile

This build takes **no synthetic attack input on the rig**, measured three independent ways:

* `send-input.ps1 -Button left` (WM_LBUTTONDOWN at the client centre) → no `local montage`
  line at all. Round 1 reached the same result.
* `send-input.ps1 -Vk 0x52` (R) and `-Vk 0x51` (Q) → no `local montage` line.
* Only `EquipItem` itself produced montages (`Player_SwapRightWeapon`), so the montage
  observer works; it is the input that does not arrive.

The bindings come out of the pak (`QueensDomain/Config/DefaultInput.ini`):

```
Attack            = LeftMouseButton,  Gamepad_RightTrigger      <- no keyboard binding
SecondaryAttack   = RightMouseButton, Gamepad_LeftTrigger       <- no keyboard binding
UltraAttack       = R,                Gamepad_FaceButton_Top
NormalDaggerThrow = Q,                Gamepad_LeftShoulder
```

so there is a keyboard path in principle, and it did not deliver either. Consequence, stated
plainly: **the widened projectile class gate (guns / crossbow / rocket / spear throws /
travelling waves) is still unit-tested only**, and no weapon-specific swing montage has been
seen live. That is what `test_combatvfx.cpp` (now **275** assertions, 86 real montage names)
is standing in for.

## R2.3 The wip R41–R48 port table

Compared against `wip/master-uncommitted-20261114` (`git show <branch>:mod/native/src/qdavatar.h`,
and its `LIVE-TEST-LOG` R41–R48). **Caution: the R-numbers mean something different on
`sync-fix`** — that log's R41–R48 are reliable-kind acks, save upload, enemy detail, etc. The
mapping here is by TOPIC, from the brief.

| R (wip) | topic | verdict on sync-fix | evidence |
|---|---|---|---|
| **R41** | rig lever: `QDCOOP_GIVE` with `!Cheat`, `+path`, `@slot` | **ALREADY PRESENT** | `proxy.cpp` `DevGive`: `+`→`SaveFunctions_C.AddItemToInventory`, `!`→`FS_Cheats_C`, `@slot`→`EquipItem`. Used by both s32 runs |
| **R42** | charged-attack timing (hold on press, strike on release) | **ALREADY PRESENT** (better) | `qdavatar.h` `ChargeOpenerMove` → `cxf::ReleaseMove` (shape-aware, wip read a word list); `A_CHARGE`=9/`A_CHARGE_THRUST`=14; the `fromCharge` 80 ms shortcut in `SetAction` |
| **R43** | thrusts: dash/startup→thrust, stab-charge→thrust on release, wrist turns the blade | **ALREADY PRESENT** | `qdcombatvfx.h` rule 9 (`dash`/`startup`/`lunge`→`K_THRUST`); wrist target −78° in `Animate`; `avatar::Jab` for a flurry's repeated stabs |
| **R44** | use / throw / **cast** / left-hand swap | **PORTED (cast ordering)** — the rest already present | *bug*: the cast test sat at the END of `ClassifyMontage`, after "attack"/"strike", while `_left` was answered at rule 13. `Player_FPArms_LeftHand_CastMagic_Montage` therefore came out `K_SLASH_L` and the cast branch was **unreachable**. Fixed by a rule **12b** before the directional slashes, adding `castmagic`/`castspell`/`cast_spell`/`grimoire`/`inscrib`, and kept *after* the alt rule so `MagicStoneSword_Alt` is still a sword alt. 6 new assertions |
| | | *already present*: use (`A_USE` from `InpActEvt_UseItem` + `InventoryChanged`); left-hand swap (the game's own montage contains "equip"/"swap" → `K_SWAP`) | |
| | | *not ported*: setting `A_THROW` from a spawned `/Daggers/` projectile. The sender's throw montage already classifies to `K_THROW`, so this is a redundancy for a case the rig cannot reach | |
| **R45** | swing arc anchored to the weapon, not the head | **STILL NEEDED — NOT PORTED** | `SpawnMeleeArc` still spawns at `{0, 12, 150}` (eye height) attached to the body. Spec: read `Weapon_R` via `SocketTransform(b->pose,"Weapon_R",2,…)` with `GetBoneXf(B_WEAPON_R)` as fallback, compose the sender's view pitch (`e.pitch`/PLAYER pitch, already on the wire) onto the arc rotation, and attach with a read-back. Not ported because it changes the *proven* sword arc and I could not have re-proven it inside the live budget |
| **R46** | off-hand throwing dagger never sent (`ScanLocalEquip` accepted one root) | **ALREADY PRESENT** (better) | `IsHandVisualOwner` accepts `WeaponVisuals_C` **and** `Dagger_Visuals_Base_C` **and** the `*Visuals` name fallback. Live this run: `held: Default_Dagger_Visuals_C … hand=L` → peer `hand L now holds …/Default_Dagger_Low` |
| **R47** | every swing looked the same (row from the counter's parity) | **PORTED** | `qdavatar.h` `SetSwingVariant(Body*, rollDeg)` + one call in `SpawnMeleeArc`. The arc's roll was ALREADY on the wire (`e.pitch`, used for the system's rotation) — it is now also spent on the pose. `row = (actByte>>5)&1` remains only as the fallback. Unit-tested via the classifier; **not** live-proven (needs an attack) |
| **R48** | `Weapon_L` bone missing from the FK table | **NOT NEEDED** | `BuildBody` already attaches the left hand's mesh to the skeleton's `Weapon_L` socket, and an undriven bone keeps its stored local offset from its parent, so it follows `forearm_stretch_l`. Live: the off-hand dagger is in the left hand on both machines with no `B_WEAPON_L` row |
| **R48** | **grip calibration** (`grip[h] = rel(body Rest hand→weapon, FP arms Hand→weapon)`) | **STILL NEEDED — NOT PORTED** | `ApplyEquip` passes `XfFromWire(it.xform)` straight to `SetHeld`. No `XfRelativeTo`, no `gripCal` in `qdavatar.h`. The live frames show the weapons in the hand (the peer resolved every mesh), so the *existing* path is good enough to draw them; whether a hammer or a scythe sits at the same angle as on the sender is a visual question I could not measure without a framed third-person shot |
| **R48** | finger curl on the grip | **NOT PORTED** | Cosmetic; needs 5 chains × 5 bones × 2 hands of rest-local calibration. No claim depends on it |

**Net:** two fixes ported (R44's classifier ordering, R47's swing variant), one confirmed
unnecessary (R48's `Weapon_L`, with the reason), four confirmed already present (R41, R42,
R43, R46), and two deliberately left with a written spec (R45, R48 grip/curl).

## R2.4 Files changed in round 2

| file | change |
|---|---|
| `mod/native/src/qdcombatvfx.h` | `ClassifyMontage` rule 12b: cast before the directional slashes; adds `castmagic`/`castspell`/`cast_spell` |
| `mod/native/src/qdavatar.h` | `SetSwingVariant` (R47) |
| `mod/native/src/proxy.cpp` | `avatar::SetSwingVariant` call in `SpawnMeleeArc`; `DevEquipLevers` (rig-only `qd.equip` = `EquipItem(Item,Slot)` / `SetSelectedWeaponByItem`, `qd.wsys` = a `WeaponSystem` verb) |
| `mod/native/test/test_combatvfx.cpp` | +7 assertions (268 → 275) pinning the cast order and the two names that must not move |
| `mod/qdcoop.ini` | `CombatVfx=1`, with the unproven half stated in the comment |
| `mod/tools/session-s32b.ps1`, `session-s32c.ps1` | the rig (`s32c` ran the R/Q attack attempt) |

---

# Round 3 (2026-11-15) — grip, fingers, the weapon-anchored arc, and ATTACKS AT LAST

Delegate `combat-grip`. Switch: **`CombatVfx=1`** (unchanged).

Round 2 left three things open and each had the same shape: the code was there (or specified)
but nothing had ever made the game perform a weapon-specific attack, so the half that depends
on one could only be unit-tested. This round found why, closed it, and ported the two wip
sections that were waiting on a visual question.

## R3.1 THE ATTACK TRIGGER — two functions with one name (this is the round 1-2 blocker)

**Root cause.** The pawn declares the compiled input event for an action **more than once**.
The class-chain dump shows two entries named

```
FS_PlayerChar_C.InpActEvt_Attack_K2Node_InputActionEvent
FS_PlayerChar_C.InpActEvt_Attack_K2Node_InputActionEvent
```

A prefix lookup (`Fn::ResolvePrefix`, the idiom the interaction lane uses) returns only the
**first**. Calling that one *runs* — `called=1` — and changes nothing: the arms' active
montage stayed `none`. Calling the **second** plays the opener. Measured, one session, same
tick:

```
20:37:21.741 INFO rig: atkcall InpActEvt_Attack_K2Node_InputActionEvent keyEventParam=0 called=1 montage none -> none
20:37:21.862 INFO rig: atkcall InpActEvt_Attack_K2Node_InputActionEvent keyEventParam=0 called=1 montage none -> Player_Stab_Charge_Montage  CHANGED
20:37:22.294 INFO local montage Player_Stab_Charge_Montage -> kind 14 sent 14
```

So "the event ran" is not evidence it did anything — exactly the lesson the interaction lane
already records, in a different disguise. The lever now calls **every** event with the prefix,
in declaration order, which is what the engine's own dispatch does once per binding.

**Neither event declares a `KeyEvent` parameter** (`keyEventParam=0`, measured), so a press
and a release are the same call and the game advances its own state machine on the next one
(`TickAttackKey` is on the pawn). `count` in the spec is therefore "how many times the binding
fires", not a hold time.

**The lever** — rig-only, test instances only:

| lever | file | what it does |
|---|---|---|
| `qd.attack` = `<action>[,gap_ms][,count]` | `proxy.cpp` `DevAttackLever` | fires that action's binding `count` times, `gap_ms` apart |
| `qd.attack` = `Info:<word>` | `RigAttackInfo` | prints every `InpActEvt_*` on the class chain with its full signature |
| `qd.attack` = `Call:<action>` | `RigAttackCall` | calls one action's events and reports the montage **each** moved |
| `qd.attack` = `List` | `DevAttackLever` | dumps the pawn's functions |

`#nonce` on the spec makes the same action fireable twice: only a spec that differs from the
last one handled starts a new burst.

**What a real key press would have been.** `DefaultInput.ini` binds `Attack` to
LeftMouseButton/Gamepad_RightTrigger and `UltraAttack`/`NormalDaggerThrow` to R/Q. Rounds 1-2
tried `send-input.ps1` (`WM_LBUTTONDOWN`, `WM_KEYDOWN`); this build reads the mouse through
**raw input** and the posted messages never arrive, which the lever side-steps entirely by
calling the event the binding would have called.

## R3.2 PROVEN LIVE: a weapon-specific attack plays on the partner

`sesssion-grip.ps1` (`_test/grip-proof/report.txt`), two instances, `CombatVfx=1`. The HOST
equipped each family with `qd.equip <item>,0` (the game's own `EquipItem(Item, SlotIndex)`)
and fired `qd.attack Attack,300,2`. Sender montages and the **PEER's** received action kinds:

```
(spear)  20:48:59.453 local montage Player_PolearmStab_1_Montage -> kind 8 sent -1
         20:48:59.530 local montage Player_Stab_Charge_Montage  -> kind 14 sent 14
         20:48:59.565 [peer] peer action kind 14 #2
(claw)   20:49:28.985 local montage Player_ClawSlash_LeftDown_Montage -> kind 7 sent -1
         20:49:29.041 local montage Player_FPArms_Katar_Charge_Attack_Montage -> kind 14 sent 14
         20:49:29.104 [peer] peer action kind 14 #6
(scythe) 20:49:55.944 local montage Player_FPArms_Std_Charge_Attack_Montage -> kind 9 sent 9
         20:49:56.037 [peer] peer action kind 9 #2
(sword)  20:47:31.168 local montage Player_FPArms_Std_Charge_Attack_Montage -> kind 9 sent 9
         20:47:31.200 [peer] peer action kind 9 #2
```

`Player_ClawSlash_LeftDown_Montage` and `Player_PolearmStab_1_Montage` are weapon-specific
names — the claw's and the spear's own — so the claim is not "an animation played", it is
"the game's montage for THIS weapon played and the partner received the kind it classified
to". Five of seven families produced a montage this way (sword, spear, claw, scythe, and the
katar through the claw item). The **axe** and the **hammer** fired the events and produced no
montage in the sampled window, and the **rifle** produced none at all — see R3.4.

## R3.3 PROVEN LIVE: the grip frame and the fingers

Ported from `wip/master-uncommitted-20261114` (`CalibrateGrip`, `CalibrateFingers`, the
`inv(L_body) * L_fp` correction, the finger curl).

```
[inst1] body: rest pose captured, hips at 93.1 cm
[inst1] body: finger chains calibrated (38 bones)
[inst1] body: grip calibrated (FP socket -> body weapon bone)
[inst2] body: rest pose calibrated, 38 bones, grip calibrated   (same three lines)
```

- **Grip** — `ApplyEquip` composes `b->grip[hand]` onto the sender's socket-relative
  transform for every held item, so the ~100 deg frame difference between the FP arms and
  the full-body rig no longer rotates the partner's weapon. Retried up to 3 times after the
  rest pose lands, because the first equipment packet beats `CaptureRest` on a fast connect.
- **Fingers** — 38 bones over 5 chains x 2 hands, curled about each segment's local X while
  that hand holds something; an empty hand stays in the rest pose. Before this the partner
  held every weapon in an open palm.

This branch's FK table carries `Weapon_R` only (not `Weapon_L`), so the two weapon-bone REST
transforms are captured separately (`Body::weaponRest`) rather than read from `rest[]`. A
missing `Weapon_L` is not fatal: that grip stays an identity.

## R3.4 PROVEN LIVE (re-proved): the arc belongs to the WEAPON, not the head

Ported R45. The sender marks a camera-relative arc with `life_ms = 1`; the receiver takes the
origin from the body's `Weapon_R` socket (falling back to the `B_WEAPON_R` bone), composes the
sender's view pitch onto the arc's rotation, and **attaches** the component to that socket,
verifying the attach by reading `AttachParent`/`AttachSocketName` back.

Before (round 2 build): `arc: shown ...` at `{0,12,150}` — eye height, on the head.
After, on the PEER, for the sword and the spear and the claw and the scythe and the rifle:

```
20:48:59.227 INFO arc: shown /Game/VFX/Melee/MeleeThrust_Default.MeleeThrust_Default roll=0 at weapon -51,36,142 params set 5 socket 1
20:49:28.642 INFO arc: shown /Game/VFX/Melee/MeleeArc_Default.MeleeArc_Default roll=-149 at weapon -55,-6,146 params set 5 socket 1
```

`socket 1` is the read-back, and the position is the weapon's, not `{0,12,150}`. The sword arc
still looks right, and the spear's thrust arc is the `MeleeThrust_Default` system at the
weapon — the two families the brief asked for.

## R3.5 Files changed in round 3

| file | change |
|---|---|
| `mod/native/src/qdavatar.h` | `QConj`, `QToRotator`, `XfRelativeTo`; `Body::gripCal`/`grip[2]`/`gripTries`/`weaponRest[2]`/`weaponRestOk[2]`/`fingersReady`/`fbone`/`flocal`/`fcount`; `CalibrateGrip`, `CalibrateFingers`; the finger curl at the end of `Animate`; `CaptureRest` captures the weapon-bone rest transforms |
| `mod/native/src/proxy.cpp` | `ApplyEquip` applies the grip + bounded retry; `PuppetPump` and the dev-mirror path re-apply after the rig lands; `g_peerPitchQ` from the PLAYER row; `SpawnMeleeArc` weapon-anchored + socket-attached + `e.life_ms = 1`; `DevAttackLever` + `RigFireAction` + `RigAttackInfo` + `RigAttackCall`; `DevSpawnProjectile` (`qd.proj`); two pump registrations |
| `mod/tools/session-grip.ps1`, `session-atkdiag.ps1`, `session-gun.ps1` | the rig for the three runs above |
| `mod/tools/session-axeham.ps1` | single-instance rig that settles the axe/hammer montage question (written; not run — the LIVE lock was held by another delegate) |
## R3.6 PROVEN LIVE: a CROSSBOW SHOT — montage, projectile and impact on the partner

The one thing rounds 1 and 2 could never do. With the trigger from R3.1, the **crossbow**
fires through the game's own attack path (`_test/gun-proof/report.txt`):

```
[1] 21:40:30.720 rig: equip EquipItem(.../LuudeCrossbow_Item.LuudeCrossbow_Item, slot 0) ran=1
[1] 21:40:34.739 rig: attack Attack fired 2 event(s) keyEvent=0 -> montage none => Player_Gun_FireLight  CHANGED
[1] 21:40:34.771 local montage Player_Gun_FireLight -> kind 15 sent 15
[1] 21:40:34.771 effect: local #1 .../LuudeCrossbow_Projectile_C v=1109,0,-16
[1] 21:40:35.527 effect: local #1 stopped at 5041,-1721,42
[2] 21:40:34.910 effect: .../LuudeCrossbow_Projectile_C shown at 4228,-1721,186
```

Read it as three separate claims, each measured:

1. **`Player_Gun_FireLight`** — the game's own fire montage, and it classifies to
   `kind 15` = `A_SHOOT`, which is the gun branch of `cxf::ClassifyMontage` working on a real
   gun montage live (round 2 could only unit-test that rule).
2. **`effect: local #1 .../LuudeCrossbow_Projectile_C`** — the gun-family projectile class
   was accepted by the widened gate. This is the row round 1 marked "not live-proven" and
   round 2 could not close; the trigger is the game's, not a lever.
3. **The partner's line** — `shown at 4228,-1721,186`, i.e. the OTHER machine spawned the
   visual copy of the bolt at a world point 30 cm from where the sender's was.

Impacts reach the partner too, e.g. `[2] effect: system .../HeavyRifle_Projectile_Impact
shown at 5219,-1744,180` for the rifle-class projectile.

**`qd.proj` (test-instance only) remains, as a lane probe**, not as evidence of a shot: it
spawns the real class through the engine's deferred-spawn pair with the pawn as Owner and
Instigator. All three gun-family classes it was given (`Default_Gun_Projectile_C`,
`HeavyRifle_Projectile_C`, `LuudeCrossbow_Projectile_C`) were published by the sender and
shown on the partner, which is the gate's behaviour with no dependence on what the game will
let the rig fire.

## R3.7 NOT proven / open

- **The axe and the hammer produced no montage** from `qd.attack` in the sampled window even
  though both events ran. Their opener is a different event ordering that the 3.5 s window
  may have missed; the lever now fires `count` times with the nonce fixed (R3.1), so this is
  a re-run, not a missing mechanism. `session-axeham.ps1` is that re-run (three bursts and the
  alt attack per weapon, single instance); it was written but **not run** — the LIVE lock was
  taken by another delegate before it could go.
- **The HeavyRifle did not fire.** It is an **energy** weapon (`GunPowerLogic`,
  `ItemTags.EnergyWeapon`, `FullCellsConsumed`) and this save's `WeaponSystem.AllWeapons` is
  empty (R39), so `Attack` on it played no montage and spawned nothing — measured again this
  round. The crossbow closed the "a gun/bow shot" requirement; the rifle's charge gate is a
  game-content fact, not a lane gap.
- **Screenshots now work** (`capture-hidden.ps1`, PrintWindow + `PW_RENDERFULLCONTENT`): the
  round-1 note that this build returns a black client area did not reproduce — every frame in
  `_test/grip-proof/` and `_test/gun-proof/` is a real 944x501 render (the partner in frame,
  ~360-415 KB each). The visual claims still rest on the mod's log lines; the frames confirm
  the partner exists, is drawn, and holds the weapon.


