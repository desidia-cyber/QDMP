# Queen's Domain co-op — analysis index

Start here: **`ANALYSIS.md`**.

| File | What it is |
|---|---|
| **`ANALYSIS.md`** | **Master document.** Verdicts, engine, player model, sync surface, reuse, risks, phased plan. |
| `05-primary-evidence.md` | Raw measurements I made directly: engine identity, pak format + full file list, complete save schema, PE facts. |
| `03-dollsmp-reuse.md` | Component-by-component reuse matrix (213 rows), lessons learned, porting plan, 11 inherited defects. |
| `06-sync-design.md` | Per-system sync design: authority model, the three containers, streaming, dialogue, enemies, failure cases. |
| `07-verification.md` | **The two pre-code checks, with method and results** — GUID stability (421/421 cooked) and Steam (delay-load, unreachable). |
| `08-live-session.md` | **The game actually run and its memory read** — off-screen/muted rig, gameplay reached, `GUObjectArray` at RVA `0x4A64250`, live object counts, `FName` layout correction, 20 GB-per-instance finding. |
| `09-live-substrate.md` | **UE4SS works on this game; the engine target table with RVAs** — `GUObjectArray 0x4A64250`, `FNamePool 0x4A27F00`, `GMalloc`, `FName` encoding, two independent confirmations. **Phase 0 starts here.** |
| `10-gaps-register.md` | **Every remaining unknown, with status and the method that closes it.** Read this before writing code. |
| `04-sync-surface.md` | Independent sync-surface pass: save tooling (`gvas.py`, self-testing), class surface, enum/flag taxonomy. |
| `01-binary-forensics.md` | PE structure, exports/imports, the full class surface, UE4SS verdict, hook targets, multi-instance levers. |
| `02-pak-inventory.md` | Asset inventory, plugin list, world/streaming structure, system evidence by asset name, completion checklist. |
| `19-final-assessment.md` | Gaps vs residuals vs stale questions; the 50/50 verdict. |
| `20-residual-verification.md` | Live/offline residual pass (A6/A7, B6/B8, D1, E2, E5, D4). |
| `21-streaming-ai-lesson.md` | The DollsMP "enemies lose their brain" lesson applied to QD's level manager. |
| `22-close-out.md` | **D9 boss → `DragonLordDead=1`, the shop live-open, the D4 multi-GUID spawner, C10's apply/order + the `0x2A11DE0` correction.** |
| `23-quest-progression-sync-map.md` | **The consolidated quest/progression/trigger/quest-item sync map** — 63 decoded challenge definitions, 746 placed triggers, 503 key items, lanes and authority. |
| `24-quest-closeout.md` | **Quest close-out** — all 63 conditions (NSD/inventory/item-tag), the per-quest trigger graph, the 28-state vocabulary, ending gating (Ending1–4), the NPC reward table, and the "both players talk / host progression" authority rules. |

## Data artefacts

| File | Size | Contents |
|---|---|---|
| `pak-files.txt` | 47,716 lines | every path in `QueensDomain-WindowsNoEditor.pak` |
| `pak-files.json` | 6.6 MB | pak index in structured form |
| `exe_names.txt` | 69,091 names | identifiers recovered from the shipping exe |
| `exe_paths.txt` | — | asset-path-shaped strings from the exe |
| `surface_groups.txt`, `exe_surface_grouped.json` | — | the exe surface grouped by game domain |
| `pak_assets.txt`, `pak_umaps.txt`, `pak_plugins.txt` | — | pak summaries |

## Scripts (all read-only against the game)

| File | Purpose |
|---|---|
| `probe_pak_footer.py` | pak footer: version, index offset/size, encryption, compression methods |
| `probe_pak_filelist2.py` | **the one that works** — extracts all 47,716 pak paths |
| `probe_save_gvas2.py` | GVAS header parse (engine version, class name) |
| `probe_save_tree.py` | full tagged-property tree of a save |
| `probe_save_maps.py` | decodes map/array contents including object paths |
| `probe_enemy_state.py` | decodes all 414 `FS_EnemySaveState` records |
| `gvas.py`, `test_gvas.py` | agent's save reader + self-test (parses all five files) |

## Key facts in one screen

- Engine: **UE 4.27.2**, `WindowsNoEditor`, project module `/Script/QueensDomain`.
- Pak: version 11, **unencrypted index, uncompressed**, 44,156 entries, 47,716 paths.
- Player: **full-body model ships** (`Player_FullChar_2026` + skeleton + physics + face/hair/armour),
  separate first-person arms rig.
- Save: `%LOCALAPPDATA%\QueensDomain\Saved\SaveGames\World0.sav` — plaintext GVAS,
  30 properties, fully parsed.
- Progression: `NamedSaveData` (`Map<Name,int32>`, 11 entries in an early save) +
  `SavableActorStates` (`Map<GUID,int32>`) + `EnemySaveStates` (`Map<GUID,{bValid,RespawnTime,RemainingHP}>`, 414).
- No anti-cheat. No game networking. Must build replication — which DollsMP already did.
