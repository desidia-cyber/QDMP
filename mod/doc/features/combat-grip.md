# combat-grip — grip calibration, finger curl, the weapon-anchored arc, and the attack trigger

Delegate id: `combat-grip`. Round 3 of the `combat-vfx` feature.

This file is the delegate's own record; the authoritative feature doc is
`mod/doc/features/combat-vfx.md` (its **Round 3** section, which this work wrote).

## What was asked, and what was actually wrong

Three things were left open, and all three had the same shape: the code existed (or was
specified) but nothing had ever made the game perform a **weapon-specific attack**, so the
half that depends on one could only be unit-tested.

1. **Grip + finger curl** (wip `R48`) — a spec was written in round 2, not ported.
2. **The swing arc anchored to the weapon** (wip `R45`) — same.
3. **Weapon-specific attacks and a ranged projectile** — rounds 1 and 2 concluded synthetic
   attack input "does not arrive" and stopped.

The third turned out to be a *bug in the search*, not a limit of the rig, and fixing it
unblocked the proof of the first two.

## The finding that unblocked everything

The pawn declares the compiled input event for an action **more than once**:

```
FS_PlayerChar_C.InpActEvt_Attack_K2Node_InputActionEvent
FS_PlayerChar_C.InpActEvt_Attack_K2Node_InputActionEvent
```

`Fn::ResolvePrefix` returns only the **first**, and calling that one runs (`called=1`) and
does nothing — the arms' active montage stays `none`. Calling the **second** plays the
opener. Measured in one tick:

```
rig: atkcall InpActEvt_Attack_K2Node_InputActionEvent keyEventParam=0 called=1 montage none -> none
rig: atkcall InpActEvt_Attack_K2Node_InputActionEvent keyEventParam=0 called=1 montage none -> Player_Stab_Charge_Montage  CHANGED
local montage Player_Stab_Charge_Montage -> kind 14 sent 14
```

So "the event ran" is not evidence it did anything — the same lesson the interaction lane
records, in a different disguise. The lever now calls **every** event with the prefix, in
declaration order.

Neither event declares a `KeyEvent` parameter, so press and release are the same call and
`count` means "how many times the binding fires".

## Design (≤15 lines)

- `qd.attack` = `<action>[,gap_ms][,count]` calls every `InpActEvt_<action>*` on the pawn,
  `count` times, `gap_ms` apart; `#nonce` makes the same action fireable twice.
- `Info:<word>` / `Call:<action>` / `List` are diagnostics (signatures, per-event montage
  movement, the pawn's functions).
- Grip: `CalibrateGrip` measures `inv(L_body) * L_fp` once from the two rest poses;
  `ApplyEquip` composes it onto every held item, retried (≤3) until the rest pose exists.
- Fingers: `CalibrateFingers` resolves 5 chains × 2 hands (38 bones) and their rest locals;
  `Animate` curls them about each segment's local X while that hand holds something.
- Arc: sender marks a camera-relative arc with `life_ms = 1`; receiver takes the origin from
  `Weapon_R`, composes the sender's view pitch, attaches with a read-back, and logs `socket 1`.
- `qd.proj` (test-only) spawns a real projectile class owned/instigated by the pawn — a lane
  probe, not evidence of a shot.

## Files changed

| file | change |
|---|---|
| `mod/native/src/qdavatar.h` | `QConj`, `QToRotator`, `XfRelativeTo`; `Body` grip/finger state (`gripCal`, `grip[2]`, `gripTries`, `weaponRest[2]`, `weaponRestOk[2]`, `fingersReady`, `fbone`, `flocal`, `fcount`); `CalibrateGrip`; `CalibrateFingers`; finger curl at the end of `Animate`; `CaptureRest` captures the weapon-bone rest transforms |
| `mod/native/src/proxy.cpp` | `ApplyEquip`: grip + bounded retry; `PuppetPump` + dev-mirror path re-apply after the rig lands; `g_peerPitchQ` from the PLAYER row; `SpawnMeleeArc` weapon-anchored + socket-attached + `e.life_ms = 1`; `RigFireAction`, `RigPressAction`, `RigAttackInfo`, `RigAttackCall`, `DevAttackLever`; `DevSpawnProjectile`; two pump registrations |
| `mod/tools/session-grip.ps1` | the round-3 proof rig (grip/fingers/arc/attacks, 2 instances) |
| `mod/tools/session-atkdiag.ps1` | short single-instance rig that found the two-event bug |
| `mod/tools/session-gun.ps1` | the crossbow/projectile proof (2 instances) |
| `mod/tools/session-axeham.ps1` | single-instance rig for the axe/hammer question (written, not run) |
| `mod/doc/features/combat-vfx.md` | the Round 3 section |

All shared-file edits are small, marked `[combat-grip]`, and confined to the functions above.

## Coverage

- **Grip + fingers**: every hand-held item the equipment lane can carry — the calibration is
  one frame difference applied to every item, and the curl is driven by "does this hand hold
  something", not by a list.
- **Arc**: every melee arc the sender publishes (`MeleeArc_Default`, `MeleeThrust_Default`,
  and the weapon-specific systems), since the change is at the receiver.
- **Attacks**: the lever can fire ANY action binding the pawn declares. Live-proven for
  sword, spear, claw/katar, scythe and **crossbow**; axe and hammer fired but produced no
  montage in the sampled window.

## Proof

**Build**: `mod/native/build.bat` clean, `1488896`→`1491456` bytes, no warnings.

**Tests**: `mod/native/test/run_tests.bat` — all suites pass, `test_combatvfx` **275**,
unchanged (this work adds no new pure function that a unit test could reach: the new code is
engine calls and one transform identity whose only meaningful check is a live frame).

**Live** (two instances, `CombatVfx=1`, off-screen; reports copied to
`_test/combat-grip/grip-report.txt` and `_test/combat-grip/gun-report.txt`):

1. *Grip + fingers* — both machines: `body: rest pose captured, hips at 93.1 cm`,
   `body: finger chains calibrated (38 bones)`, `body: grip calibrated (FP socket -> body
   weapon bone)`.
2. *Seven families in the partner's hand* — `body: hand R now holds ...` for
   RustySword, Axe_00_SM, CyberHammer_00_SM, BasicSpear_00_SM, FinalClaw_SM,
   RayneScythe (skeletal), HeavyRifle_Mesh (skeletal).
3. *Weapon-specific attacks* — `Player_PolearmStab_1_Montage -> kind 8`, `local montage
   Player_ClawSlash_LeftDown_Montage -> kind 7`, `Player_FPArms_Katar_Charge_Attack_Montage
   -> kind 14 sent 14`; partner: `peer action kind 14 #2`, `kind 14 #6`, `kind 9 #2`.
4. *Arc at the weapon* — `arc: shown .../MeleeArc_Default roll=-149 at weapon -55,-6,146
   params set 5 socket 1` (was `{0,12,150}` at the head).
5. *Crossbow shot* — `local montage Player_Gun_FireLight -> kind 15 sent 15`,
   `effect: local #1 .../LuudeCrossbow_Projectile_C v=1109,0,-16`; partner
   `effect: .../LuudeCrossbow_Projectile_C shown at 4228,-1721,186`; impact systems shown.
6. *Zero access violations* on both instances in both runs.

**Screenshots**: `_test/grip-proof/*.png` and `_test/gun-proof/*.png`, all real 944×501
renders (PrintWindow + `PW_RENDERFULLCONTENT`). Round 1's "black client area" did not
reproduce.

## Open / for the coordinator

- **The axe and the hammer produced no montage** in the sampled window (events ran). The
  lever's nonce/repeat bugs are fixed, so a re-run should settle it — a timing question, not
  a missing mechanism.
- **The HeavyRifle refuses to fire**: an energy weapon with no charge, and
  `WeaponSystem.AllWeapons` is empty (R39). The crossbow satisfied "a gun/bow/crossbow shot";
  the rifle's charge gate is game content.
- **`CombatVfx` stays 1.** The crossbow shot (item 5 above) is the gun-family proof that was
  outstanding, so the switch is now supported by a real triggered shot as well as a lane probe.
- **Other delegates share this checkout**: my edits are confined to the marked regions; the
  large `proxy.cpp` diff includes their work.
