# 19 — Final assessment: what is left after the register

**The register is complete: 50 / 50 items CLOSED, 0 PARTIAL, 0 OPEN**
(`10-gaps-register.md`). Nothing in it blocks starting the mod.

This file separates three things that are easy to confuse:

* **Gaps** — unknowns that could change the design. **None left in the register.**
* **Residuals** — facts measured from a single instrument or at MEDIUM confidence. They are
  verification tasks, not unknowns.
* **Stale doc questions** — older question lists that the register has since answered.
  They are reconciled below; they must not be read as open work.

---

## 1. What the live pass added (all measured)

| Item | Live result | Confidence |
|---|---|---|
| D1 | Status/Inventory, Pause, Photo, Checkpoint all freeze `UWorld::TimeSeconds`; dialogue does not pause | HIGH |
| C9 | Suspend saves alternate `World0_Suspend_0/1` **every 120 s** with no input | HIGH |
| E2 | Two instances, both WORLD-LIVE, combined **5.40 GB WS / 7.28 GB private** | HIGH |
| D8 | Keyboard actions via `PostMessage`; mouse buttons need `SendInput`; mouse look is raw | HIGH |
| D5 | `StoreEnemySaveState` on a real kill: `RemainingHP=0`, authored GUID, caller = the spawner | HIGH |
| D4 | Kill → respawn → kill 121 s apart: **same GUID, same spawner**; also stable across processes | HIGH |

## 2. Residual verification items (not gaps)

None of these changes the design; each is a runtime check or a confidence promotion.

| Item | Why it is a residual | Cheap test |
|---|---|---|
| **A6 `GMalloc` `0x49DFB30`** | UE4SS only; the static pass could not confirm it | read `[base+0x49DFB30]`, check the vtable is in-module, call `Malloc(16)` |
| **A7 `StaticConstructObject_Internal` `0x11AF050`** | UE4SS only | hook it, spawn one actor, confirm the hit |
| **B6 `+0x58` clear** | the ctor sets `bReplicateMovement`; a runtime clear was not swept | read bit 4 of `+0x58` on the live pawn after 10 min of play |
| **B8 "mesh unassigned"** | MEDIUM-HIGH from code/assets, not a live mesh read | read `CharacterMesh0->SkeletalMesh` (expect null) |
| ~~**C10 union**~~ | **CLOSED in `22-close-out.md` §4**: exact manager apply/order pinned; live `Add`+`Flush` replaces the visible set (21→11), so the guard is required at the manager apply, not a volume pass |
| **D1 sub-states** | shop / journal / death screen not individually opened live | open each, read `TimeSeconds` — **shop CLOSED in `22-close-out.md` §2 (open, no pause); journal/death still menu-context only** |
| ~~**D9 boss keys**~~ | **CLOSED in `22-close-out.md` §1**: `DragonLordDead = 1` observed in the suspend save after the Green Dragon defeat |
| ~~**D4 multi-GUID spawner**~~ | **CLOSED in `22-close-out.md` §3**: the only multi-GUID enemy spawner is `LargeRobot_Rusted_Spawner` (`500_AUT`), two GUIDs read live |
| **E5 "no other mechanism"** | file/port locks are outside the instruments | two instances already ran — the behavioural answer is "no enforcement" |
| **E2 footprint** | measured at 640×360 / scalability 0 | repeat at 1080p if the shipping profile matters |

## 3. Stale doc questions — reconciled

| Doc | Stale statement | Correct status |
|---|---|---|
| `ANALYSIS.md` §9 | "two done, one left"; dialogue pause open | all three answered; dialogue does not pause |
| `06-sync-design.md` §4.5 | "does dialogue pause?" | answered: no; menus do |
| `09-live-substrate.md` §4 | "still missing: ProcessEvent/GWorld/GEngine/tick" | all found; §6 merged table |
| `11-live-object-graph.md` §5 | B4/B6/B8 partial/open | all CLOSED |
| `02-pak-inventory.md` | "one open item: entry table" | decoded (`PAK-DECODE.md`) |
| `05-primary-evidence.md` §8 | four offline open items | all four closed |
| `04-sync-surface.md` §9 | nine open questions | 5/6/8 closed; 2/3/4 settled; 9 moot for the puppet design; 1 observed but not instrumented; 7 is a design choice |

## 4. What the register does **not** cover

The register is an **analysis** register. It does not count the implementation itself:
transport, session, puppet, smoothing, the save codec, the sync lanes, packaging. Those are
build tasks with their own tests, not unknowns. The remaining *design decisions* are:

* the pause policy (suppress `SetGamePaused` for menus, keep the UI — recommended in
  `13c-pause-surface.md`);
* loot policy (the game is shared by construction; per-player is a mod-side roll-twice);
* peer inventory (whether the peer's inventory is real or held by the mod — `04` Q7).

The **quest/progression sync map** is now a separate artefact (`23-quest-progression-sync-map.md`):
all 63 `ChallengeDefinitions` are decoded to their `NamedSaveData`, inventory and item-tag
conditions; the 746 placed quest/trigger/setter/chest actors are counted; the 503 key items
are grouped into 26 families. `24-quest-closeout.md` completes the content map: per-quest
trigger graph, the 28-state vocabulary, ending gating (`Ending1–4`) and the NPC reward table,
plus the recorded authority rule — **both players may talk to NPCs; progression is
host-authoritative.**

## 5. Verdict

**No analysis gap or blocker remains.** The substrate is located and byte-verified, the
player/puppet chain is closed, the world-state and systems questions are answered with
artefacts, and the operational questions are measured on this machine. What is left is
runtime verification of two single-source addresses (A6/A7) and a handful of confidence
promotions — plus the build work the register was never meant to count.

**Post-script.** The four residual content items in §2 (C10 union, D9 boss keys, the D4
multi-GUID spawner, and the shop live-open) were subsequently closed with live evidence in
`22-close-out.md`: `DragonLordDead = 1` observed in a suspend save, the shop opened and shown
not to pause, the only multi-GUID enemy spawner identified with its two runtime GUIDs, and
the manager apply/order pinned (with a correction to the DollsNest `0x2A11DE0` citation).
