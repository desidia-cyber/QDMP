# 18 — D4 residual: the respawn cycle does not rotate the key

**Result: CLOSED.** Two kills of the same spawner's enemy, **121 s apart**, wrote the
**same authored GUID from the same spawner**. The GUID also matched the kill in a
*different process* run. The `GUIDs`/`CurrentIndex` rotation risk is refuted for this
spawner.

---

## Method

Clean install, isolated `-UserDir`, 640×360, held by a background job. Frida 16.7.19
attached with the `StoreEnemySaveState` (`0x00E3BF10`) hook. To get repeatable kills, the
enemy was held in front of the player each tick with `AActor::K2_SetActorLocation`
(impl `0x0265B400`), its tick was disabled (`AActor::SetActorTickEnabled` `0x026636F0`),
and the sword was swung with real `SendInput` left clicks.

The spawner in this run: **`Dragonfly_Spawner_C` @ `0x1930107f250`** at (1948, −815, −900).
The enemy: `Dragonfly_Base_BP_C` ("Meganeura") @ `0x192e3c6d9c0`.

## The evidence

```
kill #1  t=1794011880303  worldctx=0x1930107f250
         guid=5ae3b37b-4f721b25-bd55a3b5-958ab1c2  respawn=0  hp=0
kill #2  t=1794012001587  worldctx=0x1930107f250
         guid=5ae3b37b-4f721b25-bd55a3b5-958ab1c2  respawn=0  hp=0
```

* Δt = **121 s** between the first and second kill.
* The **same actor address** was still present at the pinned spot after the first death,
  and it was damageable again — the spawner re-enables/reuses its instance
  (`RespawnTime = 0`).
* Both writes carry the **same GUID** and the **same spawner** (`WorldContext`).
* The same GUID also appears in the *earlier process* (session of `17-d5-live.md`,
  different spawner address, same GUID) — so the GUID is authored, not minted per process
  or per spawn.

Because `StoreEnemySaveState` ends in `TMap::Add` on `save+0x270`, a second call with the
same key **updates** the existing element; it does not add a second one. So the
"kill → respawn → kill" cycle yields one key, updated twice.

## What this closes

D4-i1 / the D4 live test: *"the spawner could rotate between multiple `GUIDs` entries"*.
It did not. The persisted key is stable across respawn and across process restarts.

## Residual (not a gap)

No further D4 work. The only thing not exercised is a spawner that owns **more than one**
`GUIDs` entry; this spawner demonstrably stays on one. If a multi-GUID spawner is ever
found, the same probe applies.

## Reproduction

```
_live\frida_tp.js / frida_tp.py   # Store hook + game-thread pin/tickoff
_live\sendinput-click.ps1         # real LMB
_live\d4_kill1.log, d4_kill2.log  # the two recorded kills
```
