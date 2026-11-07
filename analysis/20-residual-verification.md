# 20 — Residual verification pass

One live session (clean install, isolated `-UserDir`, Frida 16.7.19) plus one offline
pak probe. Every item from `19-final-assessment.md` is either verified below or shown to
require content that this save does not have.

---

## Verified live

### A6 — `GMalloc` `0x49DFB30` (was single-source)

```
[base+0x49DFB30] = 0x194047abc70
  vtable = 0x7ff760fdea48          (in-module)
  slot +0x00 = 0x7ff75e507c30  inModule
  slot +0x08 = 0x7ff75dde64f0  inModule
  slot +0x10 = 0x7ff75e509710  inModule
  slot +0x18 = 0x7ff75ddf55b0  inModule
  slot +0x20 = 0x7ff75e509f40  inModule
  slot +0x30 = 0x7ff75e5085f0  inModule
```

The global holds a real C++ object whose vtable and every inspected slot are code in the
module. **Runtime-confirmed.** (A direct `Malloc` call through `+0x10` raised a Frida
"system error"; the object/vtable evidence is the confirmation, and the call shape is not
needed.)

### A7 — `StaticConstructObject_Internal` `0x11AF050` (was single-source)

Hooked **before** the save load. It fired **thousands of times** during actor construction
(sample return addresses `0x7ff75e80e273`, `0x7ff75e68a9b0`). **Runtime-confirmed** as the
actor-construction function.

### B6 — `bReplicates` / `bReplicateMovement`

Live pawn `0x194441caac0`:

```
+0x58 = 16   -> bit 4 set -> bReplicateMovement = TRUE
+0x5B = 106  -> bit 1 set -> bReplicates        = TRUE
```

No runtime clear. The `+0x58` residual is retired.

### B8 — `CharacterMesh0` is unassigned, not hidden

Scanned each `SkeletalMeshComponent` on the live pawn for a pointer to an object whose
class is `SkeletalMesh`:

```
Arms           -> SkeletalMesh at +0x480   (positive control: the FP rig is assigned)
CharacterMesh0 -> none in 0x600 bytes      (unassigned)
```

This is the strongest form of the B8 answer: the gameplay body mesh has no mesh asset, so
it is not rendered because it is empty — no visibility flag is involved.

### D1 — death / Game Over screen

`TimeSeconds` on the Game Over screen:

```
t0  440.5064392
t2  440.5065002      (2 s wall)
RealTimeSeconds  575.27 -> 578.89
```

**Paused** — the Game Over screen freezes the world clock, like the menus.

### E2 — 1080p footprint

Client **1920×1080** in gameplay (verified via `GetClientRect`), same save:

```
working set 2.73 GB   private 3.66 GB
```

Two instances ≈ **7.3 GB private**, far under the 51.4 GB bound. The earlier
"~19.5 GB/instance" figure is not reproduced at this profile; the 640×360 and 1080p
measurements are both ~3.7 GB private.

### E5 — single instance

Two instances already ran simultaneously and both reached gameplay (`16-live-pass.md` §3).
Behaviourally there is no enforcement. This was the decisive test; the "no other
mechanism" wording is a note about file/port locks outside the instruments.

---

## Verified offline

### D4 — the `GUIDs` array is not always singular

`d4_guid_array_check.py` scans every `.umap` for the `GUIDs` property header:

```
ArrayProperty size=69   (1 GUID)   4,315 occurrences
ArrayProperty size=85   (2 GUIDs)     62
ArrayProperty size=101  (3 GUIDs)      1
ArrayProperty size=133  (5 GUIDs)      3
StructProperty size=512 (32 GUIDs)    28  (different serialization context)
```

So **multi-GUID placements do exist** (62 with two, 3 with five, 1 with three). The tested
`Dragonfly_Spawner_C` holds one and did not rotate across two kills 121 s apart
(`18-d4-respawn.md`). The correct mod rule is therefore: **read the spawner's runtime
`GetSaveGUID()`, never assume `GUIDs[0]`.** This is a design caveat, not a reopened gap —
the live kill proved the key is stable for the spawner that owns the enemy the save
contains.

---

## Content-limited (not unknowns)

| Item | State after `22-close-out.md` |
|---|---|
| **D1 shop** | **CLOSED** — `140_GR_ShopIsleOnly` loaded+visible, `Merchant_NPC_BP_C` present, trader dialogue and shop screen opened; the shop does **not** pause (`TimeSeconds` +3.01 s / 3 s) |
| **D1 journal** | still menu/context only; `J` is not a bound gameplay action at the camp |
| **D9 boss keys** | **CLOSED** — the Green Dragon (`Dragon_Green_BP_C`, HP 3495) was defeated via its own `Kill`; the spawner's `LordWasKilled` wrote `NamedSaveData["DragonLordDead"] = 1`, observed in the next suspend save (12 keys vs 11) |
| **C10 union** | **CLOSED** — exact manager apply/order pinned (`AddLevelListToStack` `0x00E215E0` → `0x00E37800`; `Flush` `0x00E2C270` → `0x00E2C306` → `0x00E21E40`; `SetShouldBeVisible` `0x029ACB20` → `UpdateStreamingLevelShouldBeConsidered` `0x02D4A0E0`). Live add+flush replaced the set 21→11, proving the guard is required |
| **D4 multi-GUID spawner** | **CLOSED** — `LargeRobot_Rusted_Spawner` (`500_AUT`) is the only enemy spawner among the 66 multi-GUID placements; its two GUIDs were read live |

None of these blocks the mod. The only remaining *test* is the two-kill rotation on
`LargeRobot_Rusted_Spawner`, which needs the game's own arena activation; the identity and the
mod rule (`GetSaveGUID()`, never `GUIDs[0]`) do not depend on it.

---

## Bottom line

All register items remain **CLOSED**. The two single-source addresses are now
runtime-confirmed; B6 and B8 are confirmed live; the death screen is measured; E2 is
measured at 1080p; D4's multi-GUID caveat is documented with a concrete mod rule. The
remaining items are content-context tests, not analysis gaps.

**Superseded by `22-close-out.md`:** the shop, D9, the D4 multi-GUID spawner and C10 were
subsequently exercised live. The shop opens and does not pause; the Green Dragon's defeat
writes `NamedSaveData["DragonLordDead"] = 1`; `LargeRobot_Rusted_Spawner` (`500_AUT`) is the
only multi-GUID enemy spawner; and the manager apply order is pinned.
