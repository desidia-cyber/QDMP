# 21 — The DollsMP "enemies lose their brain" lesson, and whether QD has it

The owner asked whether the failure that plagued DollsMP — **enemy AI constantly dying because
level chunks kept reloading or being hidden/visible** — applies to Queen's Domain, and whether
the same fix is needed.

**Answer: the mechanism is relevant; the exact engine trigger is not present in QD.** The fix
must be aimed at QD's own level manager and the `ULevelStreaming` visibility decision, not
copied blindly from DollsNest.

---

## 1. The DollsMP lesson, as measured there

From `docs/sync/44-host-loads-around-the-peer.md` and the 1.3.1 root-cause report:

* **The encounter unit is a streamed sublevel.** Each machine originally held only what was
  around its *own* camera, so with the peer far away the host had neither the peer's enemies
  nor its destructibles (doc 43: host 28, peer 914).
* **The rule: the loaded-sublevel set is the union of both players' streaming sets.** The host
  publishes its set and the client force-loads the difference; it **never unloads a sublevel
  the host still holds**. Loads are raised, not blocking; lighting stays the camera's.
* **"Loaded" is not enough — it must be *shown*.** F-44-3: a hidden always-loaded level "has no
  collision and its actors are **not in the world**".
* **The 1.3.1 root cause:** the engine's streaming pass briefly marks the guest's areas "not
  needed"; the mod raises them; the engine had already recorded "make invisible", and when it
  re-checks a level that passes every test **it never resets that decision**. The area is
  removed anyway, every enemy in it loses its brain, and gets a new switched-off one when the
  area returns — thousands of brain swaps per session, enemies that stop mid-fight and aim but
  never fire.
* **The fix:** immediately before the engine acts on an area the guest or host needs, check the
  engine's own tests (should be loaded, should be visible, no unload request); if they pass but
  the recorded decision says "invisible", set the decision back to "visible". Only install the
  hook if the game code matches the build byte for byte.

---

## 2. QD's streaming model (measured)

| Fact | Evidence |
|---|---|
| QD does **not** use engine `ALevelStreamingVolume`s | map export census: **0** `LevelStreamingVolume` instances; it has 6 `AFS_LevelManagerVolume`, 49 `LevelManagerTrigger_C`, 43 `LevelStreamingDynamic`, 1 `LevelStreamingAlwaysLoaded` |
| The persistent world is `Ruins_Level_00` with 42 dynamic sublevels | `stream_volumes.py` |
| Streaming is a **game-level stack of lists** | `FS_LevelManager`; `AddLevelListToStack` `0x00E37800`, `RemoveLevelListFromStack` `0x00E38B30`, `FlushLevelManagerActions` `0x00E2C270`, `SetFreezeLevelListUpdates` `0x00E3A740`, `ClearTriggerLevelLists` `0x00E27630` |
| Two stacks at `manager+0x298` / `+0x2A8`; entries are `{UFS_LevelList*, int32 priority}`; dedup by pointer+priority | `0x00E378CD..0x00E3799F` |
| The manager is created lazily | `GetLevelManager` `0x00E2EE50` resolves `world+0x118` (GameInstance) and stores the manager at `GameInstance+0x3B0`; live: `0x227d8ada500` |
| Visibility is applied per level, then `ULevelStreaming::SetShouldBeVisible` | apply loop `0x00E21E40`; setter `0x029ACB20` sets **byte `+0xBA` bit 3** — the *same field* as DollsNest (F-44-2: `+0xBA`, visible `0x08`) |
| `UWorld::ProcessLevelStreamingVolumes` is present but has no volumes to act on | code near `0x029A66B0` reads `StreamingLevels +0x88` / `EditorStreamingVolumes +0xD0`, same as DollsNest; with 0 `ALevelStreamingVolume` instances it does nothing |

**Live confirmation of the precondition:** the camp's list is `GreenRuins_OuterIsles`, which
contains `140_GR_ShopIsleOnly`. The level is therefore in the loaded list — but **no merchant
actor exists in the world** (object scan for shop/merchant actors with a location: none). That
is exactly F-44-3: a hidden level's actors are not in the world. The shop is inaccessible for
the same reason an enemy becomes absent/brain-dead.

---

## 3. Is the DollsNest failure mode relevant here?

**Yes in mechanism, no in the exact trigger.**

* The exact 1.3.1 bug lives in the engine's *volume pass*; QD has no engine streaming volumes,
  so that trigger cannot fire from QD content.
* The precondition is present and measured: **a sublevel that is loaded but not shown has no
  actors in the world** (the missing merchant). If a union implementation raises
  `SetShouldBeLoaded`/`SetShouldBeVisible` behind the manager's back, a level the union needs can
  still be hidden or removed by a recorded "invisible/unload" decision — the same class of bug,
  at the `ULevelStreaming` level rather than the volume pass.
* QD's enemy model makes the symptom concrete: `EnemySpawner_C` (authored GUID, `EnemySaveStates`
  keyed by GUID) lives inside the sublevel. Remove the sublevel and the spawner and its enemy
  are gone; on return the spawner respawns from the save record (`bValid`/`RemainingHP`). A
  *live* enemy therefore comes back with a fresh, off AI, and only its last stored HP/death
  state survives — the same "brain dies, new switched-off brain" behaviour.

---

## 4. What QD needs instead of a blind port

1. **Union through the game's own stack.** Add both players' lists with
   `AddLevelListToStack` (dedup is `{list, priority}`), and remove a list only when **neither**
   player needs it. Do not run a per-machine "my camera only" policy.
2. **Show, not just load.** F-44-3 applies: the union's levels must have `bShouldBeVisible`
   set, or their actors (including enemy spawners) are absent. QD's own apply path is
   `0x00E21E40` → `SetShouldBeVisible` `0x029ACB20`.
3. **Guard the recorded decision.** The QD analogue of the DollsNest fix: when a union level
   passes the manager's tests but the `ULevelStreaming` recorded decision is "invisible", reset
   it to visible just before the engine acts. The field to watch is `+0xBA` bit 3 (visible);
   the recorded decision and the function that acts on it are the remaining RE step (§5).
4. **Do not reload chunks to sync.** A reload destroys the AI/actors and resets state; stream in
   the background and keep the union.
5. **Host lists all characters in the union** (F-44-4), not just those near the host.

---

## 5. What remains — RESOLVED in `22-close-out.md`

All three bullets below were closed in the close-out pass; the text is kept so the earlier
reasoning is not rewritten, with the answer appended.

* **The apply path of the manager.** A live `AddLevelListToStack` accepted an entry into stack A
  (manager `0x227d8ada500`, entry priority 100) and de-duplicated it, but a raw add plus
  `FlushLevelManagerActions(world, 1)` did **not** change the level set (55 `LevelStreaming`
  objects, 22 visible, identical before and after). So the manager's *apply* is the
  volume/trigger path, not the flush. The union implementation must go through that path (or
  replicate its apply) — this is the next build task.
  **Resolved:** the earlier add used the wrong calling convention (it passed a `{list,prio}`
  entry instead of the `UFS_LevelList*`). The correct call changes the set: `Add` appends to
  `+0x298`/`+0x2A8` and the immediate `Flush` applies the last resolved list via
  `0x00E2C306` → `0x00E21E40`. It **replaces** the visible set (measured 21 → 11), so the union
  must not use `Flush` as its apply. Full chain and field map: `22-close-out.md` §4.
* **The recorded-decision function.** QD's `ULevelStreaming` visibility flag is `+0xBA` bit 3,
  the same as DollsNest, but the function that records/acts on the stale "invisible" decision
  (DollsNest `game+0x2A11DE0`) was not pinpointed in QD. Finding it is the last RE step before
  the guard can be written.
  **Resolved/corrected:** the QD analogue is **`UWorld::UpdateStreamingLevelShouldBeConsidered`
  `0x02D4A0E0`** (the tail call of `SetShouldBeVisible` `0x029ACB20`), structurally identical to
  DollsNest `0x2D72590`. `DollsNest game+0x2A11DE0` is *not* that function in the matching
  build — it lands inside `FUN_142a11c90`'s cleanup, not a function start. Guard at the manager
  apply, not at a volume pass.
* **Shop / boss content.** The shop merchant is absent while its level is hidden; proving the
  shop's pause needs the level shown first. No boss actor is live in this save, so D9's
  defeat-key write remains a content test.
  **Resolved:** the shop sublevel `140_GR_ShopIsleOnly` was loaded+visible, the merchant
  `Merchant_NPC_BP_C` was present, the shop screen opened, and it does **not** pause. The
  Green Dragon boss was spawned, defeated, and `NamedSaveData["DragonLordDead"] = 1` was
  observed in the next suspend save. See `22-close-out.md` §1–2.

---

## 6. Bottom line

The DollsMP AI-death lesson **is relevant**: QD streams enemy-bearing sublevels, and a hidden
sublevel's actors are absent (measured with the merchant). A union that merely raises flags
without resetting the recorded hide decision can reproduce the same "enemies lose their brain"
symptom. The *exact* 1.3.1 volume-pass trigger is not present here, so the fix belongs at QD's
manager + `ULevelStreaming` visibility decision — same shape, different hook point.
