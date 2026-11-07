# D1 (broadened) — pause / time-dilation surface: reconciled result

Status: **PARTIAL — mechanism and owners proven; three states undetermined; the live
`DeltaTimeSeconds` probe is the settling test.**

Canonical evidence: **`13b-pause-surface.md`** (the dedicated audit, 460 lines, with
`analysis/re/pause_surface.py`). This file records the independent pass and the
reconciliation between the two. Where they disagree, 13b wins and the reason is below.

Verdict: **MIXED.**

---

## 1. What changed from the first pass

The first pass (this file's earlier version) inferred that **all menus pause**, because
`FS_PlayerChar`'s single `SetGamePaused` site sits near `CacheInputStatePreUIMode`,
`SetInputMode_UIOnlyEx` and `SetCursorShowableOnWake`. That strong form is **wrong**:

* `EnterUIMode` (143 B) and `ExitUIMode` (145 B) are separate exports whose bodies
  **do not contain** the pause call.
* `FS_PlayerChar` has **exactly two** `SetGamePaused` sites:
  * `0x33DF3` — pause **on** (near `CacheInputStatePreUIMode`, `GetPlayerController`,
    `SetCursorShowableOnWake`);
  * `0x342FC` — pause **off** (`SetInputMode_GameOnly` → `HideCursor` →
    `GetAllWidgetsOfClass(PauseMenu_C)` → count).
* No menu widget imports any pause entry point.

So "enters UI mode" and "pauses the world" are **distinct operations**. The pause is
bound to the **pause-menu pair**, not to every menu. What remains genuinely unknown is
whether shop / inventory-status / death route through a player-side path that pauses.

## 2. Proof that carries the answer

| Fact | Instrument | Confidence |
|---|---|---|
| Only **3 packages** import `SetGamePaused`: `FS_PlayerChar`, `Photomode_Pawn`, `WeaponUpgradeMachine` | pak import tables over all packages (positive: an import exists iff the call exists) | HIGH |
| **No native caller** — each pause entry point has exactly one direct caller, its own `exec*` thunk | direct-call index | HIGH |
| Pause applies to the **caller's own `UWorld`** (`028F3199` → `UEngine::GetWorldFromContextObject`); no networking ⇒ nothing replicates it | implementation disassembly | HIGH |
| `SetGlobalTimeDilation` importers: `Base_LD45_GM` (`Calculate and Apply Full TD`), `ShaderCompileManager` | pak import tables | HIGH |
| `UWorld::DeltaTimeSeconds` = `+0x5B0`, `UnpausedTimeSeconds` = `+0x5A4`, `RealTimeSeconds` = `+0x5A8`; `AWorldSettings` dilation = `+0x2E8` | getter disassembly | HIGH |
| `IsGamePaused` is **not** a bare flag read (`029A43C0` is a compound pause/pause-delay/seek-freeze predicate) — do not build a probe on it | implementation disassembly | HIGH |

Two stop mechanisms, not one:

* **Pause** (`SetGamePaused` → `APlayerController::SetPause`): actor ticks stop,
  `DeltaTimeSeconds` → 0.
* **Dilation** (`SetGlobalTimeDilation` / `CustomTimeDilation`): world still ticks, but
  scaled; `RealTimeSeconds` advances.

## 3. Per-state table (corrected)

`Y*` = inherited: the world is already paused because the state is reached from the
pause hub, not because it pauses itself.

| State | Pauses | Dilates | Conf. |
|---|---|---|---|
| Pause menu (Esc) | **Y** | N | HIGH |
| Photo mode | **Y** | N | HIGH |
| Weapon upgrade machine | **Y** | N | HIGH |
| Options / settings (via pause hub) | Y* | N | MEDIUM |
| Challenges / guides (via pause hub) | Y* | N | MEDIUM |
| Journal / bestiary / notes | Y* | N | MEDIUM-HIGH (`Journal_RWM_BP`, `Journal3DVisualizer` set tickable-when-paused) |
| Save menu (checkpoint rest) | Y* | N | MEDIUM (`Checkpoint` sets tickable-when-paused) |
| Key-use menu | Y* | N | LOW-MEDIUM |
| **Shop / vendor** | **U** | N | LOW |
| **Inventory / equipment / status** | **U** | N | LOW |
| **Death / game over** | **U** | N | LOW |
| Read-text / notes overlay | U | N | LOW |
| Item inspect / merge | N | **Y** | MEDIUM |
| Item pickup / give animation | N | **Y** | MEDIUM |
| **Dialogue** | **N** | N | MEDIUM (no pause import; dilates only) |
| Cutscene / LevelSequence | N | N | MEDIUM (separate map load) |
| Steam overlay / focus loss | Y | N | MEDIUM (`CheckForOverlayPause` + `TryAutoPause`) |
| Enemy hit-stop / slow-mo | N | **Y** | HIGH (local to each machine) |
| Shader compile (loading) | N | **Y** | MEDIUM |

**The three `U` rows are undetermined, not negative.** The import instrument proves they
do not call the pause API themselves; it cannot prove they are not routed through a
player-side path.

## 4. Co-op consequence

When A enters a `Y` state, A's world clock stops and B's keeps running; divergence equals
A's wall-clock time in the state. This is **not** dialogue-specific. The mod must decide,
per pausing state: forbid it in co-op, make it non-pausing (suppress the `SetGamePaused`
argument while letting the UI/input work), or accept and account for the freeze. The
dilation states are per-world and local to each machine, so they do not diverge unless the
trigger itself is unsynchronised.

## 5. The one remaining check

A live probe: open shop, inventory/status, and the death screen, and read
`UWorld::DeltaTimeSeconds` (`+0x5B0`) and `UnpausedTimeSeconds` (`+0x5A4`) before/after.
Three lines once attached, and it settles all three `U` rows plus the `EnterUIMode`
question at once. Use `GetWorldDeltaSeconds`/the raw `+0x5B0`, **not** `IsGamePaused`.

## 6. Reproduction

```
analysis/re/pause_surface.py                 # sections A-G (13b's instrument)
analysis/re/pause_surface_own.py             # independent name -> exec -> impl
analysis/re/pause_callers.py                 # native caller counts
analysis/pak/code/probes/bp_pause_scan.py    # complete package -> pause-import map
analysis/pak/code/probes/bp_uimode_scan.py   # which widgets drive UI mode
```
