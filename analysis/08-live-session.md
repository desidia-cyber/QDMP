# 08 — Live session: running the game and reading its memory

Date: 2026-11-06. Machine: 22 GB RAM, Windows, real interactive desktop (1920×1080).

Everything below was measured on a **running, isolated instance**. The owner's real save was
never touched (verified by mtime before/after; see §7).

---

## 1. What I set out to do, and what it cost to get there

Goal: run the game, look at it, and read its memory — not just its files.

Four things had to be solved first. All four are reusable for the mod's own test rig.

| Problem | Cause | Fix |
|---|---|---|
| Game exited ~30 s after every launch | `Start-Process` children inherit the harness console; when the tool call returned, the console closed and Windows sent `CTRL_CLOSE` to the game | Hold it in a **background job** that waits on the process, so the console outlives the call |
| Game ignored `-ResX=640 -ResY=360` and opened borderless fullscreen | it reads `GameUserSettings.ini`, which seeds `FullscreenMode=1` | Pre-seed `Saved\Config\WindowsNoEditor\GameUserSettings.ini` with `FullscreenMode=2`, `ResolutionSizeX/Y=640/360`, `WindowPosX/Y` off-screen |
| Game audible | — | DollsMP's `tools/bot/mute.ps1` (per-process WASAPI session mute). Unlike `-nosound` the game's audio engine still runs, so nothing that waits on a sound behaves differently |
| Screenshot captured the wrong window | `shot.ps1 -TitleMatch 'Queen'` matched an **Explorer window** whose title contains `Queens_Domain` | Use `shot.ps1 -ProcessId`. (The title-matching hazard is exactly what that script's own comments warn about.) |

Working setup:

```
640×360 windowed, parked at (2200,1400)  -> entirely off the desktop, invisible
HWND pushed to HWND_BOTTOM               -> never in front of anything
muted at the mixer                        -> silent
captured with PrintWindow(PW_RENDERFULLCONTENT) -> works off-screen
driven with PostMessage                   -> no focus steal, no moving the window
```

**The owner's desktop was never disturbed**: no window ever appeared, no audio, no focus change.

## 2. Driving the menu without stealing focus

`PostMessage(hwnd, WM_KEYDOWN/WM_KEYUP, VK_RETURN)` was enough — Slate dispatches posted key
messages, so the focused menu item activates:

```
title screen  --Enter-->  File Select  --Enter-->  "Load slot 1?"  --Enter-->  gameplay
```

The seeded copy of the owner's save appeared as slot 1 (`Knight's Encampment – Isle of Nea…`,
play time 0:10:38), confirming the save seeding worked and the save format is portable.

## 3. The game runs — and runs without Steam

This is the **empirical confirmation** of the `07-verification.md` conclusion.

The instance was launched with no Steam running and with `steam_api64.dll` unreachable.
It booted, reached the title screen, loaded a save and entered gameplay. So:

> **Delay-load means a missing `steam_api64.dll` is not fatal.** The game is fully playable
> as a plain offline build.

## 4. Visual confirmation of the first-person rig

In gameplay the view is first-person with **a visible right hand and a rusty sword rendered
as a separate first-person model** — not the full body.

That is exactly the structure `02-pak-inventory.md` predicted from the assets:
`Player_FPArms_Arms` + `FPArm_00` are the FP rig, and `Player_FullChar_2026` is a separate
full-body asset that the first-person view does not use.

So the co-op puppet question is answered visually as well as from the asset listing: the
game already renders a **hand-and-weapon model in first person**, and owns a **complete body
asset it does not currently draw there**.

## 5. Memory: what the live process actually looks like

### 5.1 GUObjectArray — found, and stable

Scanned `.data` **by structure** (candidate pointer → chunk table → object → vtable inside the
module) rather than by guessing `FUObjectArray` field offsets, which did not match this build.

```
GUObjectArray              RVA 0x4A64250   (module base + 0x4A64250)
ObjObjects.Objects         at +0x10
NumElements                at +0x24
MaxElements                at +0x20
FUObjectItem stride        0x18
```

Measured live:

| State | NumElements |
|---|---|
| Title screen | **89,189** |
| In gameplay, one level loaded | **491,540** |

Same RVA in both states — the global does not move. The 5.5× jump is the answer to the
owner's question: **at the main menu there is almost nothing loaded to find.**

### 5.2 UObject layout — verified live

Confirmed by reading live objects whose vtable lands inside the module:

```
+0x00  vtable            (inside module range, checked every object)
+0x08  ObjectFlags
+0x0C  InternalIndex
+0x10  Class             (UClass*)
+0x18  Name              (FName: i32 comparison index, i32 number)
+0x20  Outer             (UObject*)
```

Object 0 resolved to a live object with a module vtable, and its `Class` pointer's own
`Name` at `cls+0x18` behaved correctly — i.e. `UClass` inherits `UObject`'s name slot, as
expected.

### 5.3 FName entry layout — this build is NOT the documented one

This is a concrete correction worth recording. The UE4.27 documentation layout is a
**2-byte header** (`bIsWide:1`, `Len:15`) followed by the name. In this binary the entries
are:

```
+0x00  u32  hash
+0x04  u32  length      (includes the trailing NUL)
+0x08  char name[length] (NUL-terminated)
```

Measured directly off a heap dump of the pool, where consecutive entries read cleanly:

```
...greenBread_FullLoaf\0 | 82 15 4a 85 | 0c 00 00 00 | NeverStream\0 |
    b8 e6 5f 24 | 05 00 00 00 | None\0 | f4 03 c5 0d | 08 00 00 00 | Package\0 ...
```

`0c 00 00 00` = 12 = `"NeverStream"` + NUL, `05 00 00 00` = 5 = `"None"` + NUL,
`08 00 00 00` = 8 = `"Package"` + NUL. Unambiguous.

**Why this matters for the mod:** any tool that resolves `FName` must use this layout, not
the one in most UE4 notes. It also explains why my first passes found nothing.

### 5.4 What I did *not* finish

I could not locate the pool's **block table** (the `Blocks[]` array that maps
`index >> 16` → block address), so I could not turn an `FName` index into a string, and
therefore could not *name* the live classes and find the player pawn by name.

Tried, and ruled out:
- `FUObjectArray`-style field-offset scan in `.data` — no hit (layout differs).
- Structural pointer scan — **worked** (that is how GUObjectArray was found).
- `"None\0"` heap scan — found the pool's entries (that is how the layout above was learned).
- Searching memory for the discovered block addresses as plain pointers — **0 hits**, so the
  table is not a simple array of those addresses.
- Sweeping `0x22040000000–0x22048000000` in 64 KB steps against live indices — 1 candidate, no match.

**Cheapest way to finish it** (for the mod, this is worth doing properly):
1. Disassemble `FName::ToString` with capstone and read the pool global it references —
   I have `capstone` and `pefile` locally, and `.pdata` has 242k function bounds to work from.
2. Or let UE4SS do it: it already implements pool discovery and would also prove the
   "can UE4SS hook this game at all" question at the same time.
3. Or read `FNameEntryId` via a live `UFunction` call once `ProcessEvent` is found.

## 6. Two practical findings that change the plan

### 6.1 Memory per instance — corrected

> **CORRECTION (owner supplied).** An earlier revision of this file said the machine had
> 22 GB of RAM and that two instances were therefore impossible. That figure came from a
> `systeminfo` call made in a context where WMI/CIM was **denied** ("Access denied",
> `0x80041003`), so it was not a reliable total. **The machine actually has 64 GB.**

Measured per instance in gameplay, at **640×360 with every scalability group at 0**
(`Get-Process`, which is reliable):

```
Working set   19.47 GB
Private bytes 20.03 GB
Virtual       25.84 GB
Threads       148
```

A second, lighter reading during a later run showed **2.69 GB** working set shortly after
load, so the 19.5 GB is the *settled* figure once streaming has pulled everything in, not a
constant.

**Verdict with the correct RAM: two instances fit on this machine.** 2 × ~20 GB ≈ 40 GB of
64 GB, which leaves headroom. DollsMP's side-by-side single-PC rig is therefore still
viable — with the caveat that peak usage should be measured under real gameplay before
committing to it.

Levers if it does get tight: `sg.TextureQuality`, the texture streaming pool size and
`r.Streaming.PoolSize`, and `-ResX/-ResY`. `-notexturestreaming` is the wrong direction.

Consequences:
- The mod's test harness can keep DollsMP's single-PC shape, but should measure RSS per
  instance and refuse to launch the second one if headroom is thin.
- The *player-facing* requirement ("complete the game with my friend") is
  naturally a two-machine feature, which is the intended usage anyway.

### 6.2 The game writes suspend saves on its own

The `05-primary-evidence.md` schema predicted `ActiveSuspendSlot` and
`SuspendSaveBlockingVolume`. The live session confirms the behaviour — the instance produced:

```
World0_Suspend_0.sav   89,409 B   17:35:47
World0_Suspend_1.sav   89,460 B   17:37:47
```

roughly **two minutes apart**, without any explicit save action.

For co-op this matters: a suspend is written *locally and silently*, so it is a save-write
path the mod must understand — exactly the "module must not save mid-play" hazard DollsMP
documented. It also means a crashed/killed instance leaves a usable resume point, which is
good for the test rig.

## 7. Isolation verified

| | Before | After |
|---|---|---|
| Real `%LOCALAPPDATA%\QueensDomain\Saved\SaveGames` | 5 files, newest 15:30:14 | **unchanged**, same mtimes |
| Scratch `_live\saved\Saved\SaveGames` | 5 seeded files | 5 + 2 new `World0_Suspend_*.sav` |

A full backup of the real save is at `analysis\save-backup-before-live\`.

Nothing was written to the game directory; no pak was modified; no DLL was installed.

## 8. What this session proves about the mod's feasibility

| Claim | Status now |
|---|---|
| The game runs without Steam | **confirmed empirically** |
| The game can be driven headlessly/off-screen without disturbing the user | **confirmed** — PostMessage + PrintWindow + per-process mute |
| `GUObjectArray` can be located in this binary | **confirmed** — RVA `0x4A64250`, verified in two different game states |
| `UObject` and `FUObjectItem` layouts are standard UE4.27 | **confirmed live** |
| `FName` layout is non-standard | **confirmed** — `[u32 hash][u32 len][name]` |
| Two instances on one PC | **refuted on this hardware** — 20 GB per instance |
| The game writes suspend saves silently | **confirmed** |
| Class-level enumeration (find the pawn by name) | **not yet** — blocked on the FName block table |
