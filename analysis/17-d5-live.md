# 17 — D5 live: `StoreEnemySaveState` on a real kill

**Result: CLOSED.** The enemy-persistence write was observed on an actual kill, with
`RemainingHP = 0`, keyed by an authored GUID, invoked by the owning spawner.

---

## What was done

Clean install (no UE4SS), isolated `-UserDir`, 640×360, off-screen/held. **Frida 16.7.19**
(Python 3.9) attached to the process with hooks on:

```
StoreEnemySaveState       0x00E3BF10
ApplyDamage              0x028D2190
UFS_EnemyAction::OnEnemyDied 0x00E36240
```

To get a kill without a long trek, three native calls were used on the game thread
(via a hook on `FEngineLoop::Tick` at `0x007753A0`):

* **teleport/pin the enemy** — `AActor::K2_SetActorLocation` implementation
  `0x0265B400`, signature `(actor, const FVector*, bool bSweep, FHitResult*, ETeleportType)`;
  called every tick to hold the enemy in front of the player;
* **disable the enemy's tick** — `AActor::SetActorTickEnabled` `0x026636F0`, so the
  Dragonfly ("Meganeura", `Dragonfly_Base_BP_C`) could not kill the level-1 player first;
* **real mouse input** — `SendInput` left-button clicks (PostMessage `WM_LBUTTONDOWN`
  did *not* produce an attack; `SendInput` did).

The target was `Dragonfly_Base_BP_C` at `0x1dee9fc40c0`, pinned 100 units in front of
the player at the Knight's Encampment. It died to sword hits; the HP bar emptied.

---

## The evidence

Five `StoreEnemySaveState` calls, all identical except the timestamp:

```json
{"event":"StoreEnemySaveState","at":1793997096107,"worldctx":"0x1df0e3ab270",
 "guid":"5ae3b37b-4f721b25-bd55a3b5-958ab1c2","respawn":0,"hp":0,
 "lr":"0x7ff75e4d2146"}
{"event":"StoreEnemySaveState","at":1793997096865, ... same guid, respawn 0, hp 0 ...}
{"event":"StoreEnemySaveState","at":1793997097612, ... }
{"event":"StoreEnemySaveState","at":1793997098363, ... }
{"event":"StoreEnemySaveState","at":1793997099056, ... }
```

Interpretation:

| Fact | Meaning |
|---|---|
| `worldctx = 0x1df0e3ab270` | that address is the **`Dragonfly_Spawner_C`** found in the actor scan at (1948, −815, −900). The spawner, not the enemy, is the caller — exactly as D4 predicted |
| `guid = 5ae3b37b-4f721b25-bd55a3b5-958ab1c2` | the spawner's **authored FGuid** (the `GUIDs` property), stable across the five writes |
| `respawn = 0.0` | `RespawnTime` field written as 0 |
| **`hp = 0.0`** | **`RemainingHP` is 0, not −1** — the D5 item 1 question |
| `lr = 0x7ff75e4d2146` | caller inside the Blueprint ubergraph; `EnemySpawner_C` is the sole writer |
| five calls in ~2.5 s, same key | `TMap::Add` of the same key converges; the write is **per death/pass, not per save** |

**`ApplyDamage` and `OnEnemyDied` never fired** during the player's sword kill. This is
the static prediction confirmed live: the game's damage/death path is the FB attribute
component plus `EnemyBase_C` bytecode, not `AActor::TakeDamage`, and `OnEnemyDied` only
fires when the enemy had an action running. **`StoreEnemySaveState` is the correct hook.**

## What this closes, and what remains

* D5 #1 — ✅ `StoreEnemySaveState` on a kill, `RemainingHP = 0`.
* D5 #2 — ✅ it fires per death/pass, not per save; repeated calls are the same key and
  converge (idempotent), so a mod may apply it as a set-assignment safely.
* D5 #3 — the kill did not go through any native `Die` we could observe, and the
  BP death path is the only one; no separate native `AFS_EnemyCharacterBase::Die`
  was needed to explain the behaviour.
* **Residual (D4, not D5):** the kill → wait `RespawnTime` → kill-again test was not run.
  Structurally the key is the spawner's authored GUID, so a respawn reuses it; this is
  the one test that would catch a rotating `GUIDs` index.

## Addendum to D8

Melee/mouse-button actions are **not** reliably injectable with `PostMessage`
`WM_LBUTTONDOWN`; they required `SendInput`. Keyboard actions (`Tab`, `Esc`, `P`, `E`)
did work through `PostMessage`. So the input split is: keyboard → PostMessage OK;
mouse buttons → SendInput; mouse look → raw input (needs real hardware events).

## Reproduction

```
_live\frida_tp.js / frida_tp.py      # hooks + game-thread teleport/pin/tickoff
_live\sendinput-click.ps1            # real LMB clicks
_live\frida_tp.log                   # the five StoreEnemySaveState records
```
