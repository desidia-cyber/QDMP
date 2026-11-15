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
