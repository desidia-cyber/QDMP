# 16 — Live pass: D1 (pause), C9 (suspend saves), E2 (two instances), D8 (input)

Measured on the shipped game, clean install (no UE4SS), isolated `-UserDir`,
640×360 windowed, off-screen. Instrument: **Frida 16.7.19** (Python 3.9) attached to the
process and sampling `UWorld`'s clock fields at 10 Hz from the RVAs proven in
`13b-pause-surface.md`:

```
+0x5A0 TimeSeconds           (authoritative game clock)
+0x5A4 UnpausedTimeSeconds
+0x5A8 RealTimeSeconds
+0x5AC AudioTimeSeconds
+0x5B0 DeltaTimeSeconds
+0x5B4 PauseDelay
```

Both instances' module base was `0x7ff75d670000` in this run; the RVAs are module-relative.

---

## 0. Two traps hit and fixed (so the next session does not repeat them)

1. **`-saveddirsuffix=qd1` redirects `Saved\` to `Saved_qd1\`.** With the suffix the game
   found an empty slot set and showed five "Empty File" rows, even though
   `inst1saved\Saved\SaveGames\World0.sav` existed. Dropping the suffix made the seeded
   save appear. `-UserDir` alone is enough isolation; the suffix is an *extra* redirection.
2. **The game must be held by a live process.** Launching it from a command whose process
   tree closes kills it. It ran under a background PowerShell job with a sleep loop
   (`run-real.ps1`); `Wait-Process` is blocked by the sandbox, so the loop uses
   `Start-Sleep` + `Get-Process`.

Also: the UE4SS probe crashed the build at first tick when it called the native statics
(`UGameplayStatics:GetWorldDeltaSeconds`) — a dump was written. Frida reads the fields
directly and does not touch the reflection layer.

---

## 1. D1 — what pauses: **live answer**

`TimeSeconds` slope is measured over the interval a state is open (`ΔTimeSeconds/Δwall`).
1.0 = running, 0.0 = frozen. `RealTimeSeconds` advances in every case.

| State | Opened by | `TimeSeconds` slope | Verdict |
|---|---|---|---|
| Gameplay baseline | — | 0.999–1.001 | running |
| **Status & Inventory** | `Tab` | **0.080** (frozen at `55.502`) | **PAUSES** |
| **Pause menu** | `Esc` | **0.000** (frozen at `57.745`) | **PAUSES** |
| **Photo mode** | `P` | **0.016** (frozen at `105.843`) | **PAUSES** |
| **Checkpoint / Save (obelisk)** | `E` | **0.081** (frozen at `146.986`; `DeltaTimeSeconds` = **0.0000**) | **PAUSES** |
| Journal | `J` | 1.000 | did not open (key had no effect) — untested |
| Dialogue | — | — | not exercised live; static says **no pause** (`13c`, MEDIUM-HIGH) |

**This settles the user's question: inventory/status does pause.** So do the checkpoint/save
UI, the pause menu and photo mode. These were the three `U` rows in `13b`; they resolve to
**YES**. The static "menus inherit the pause" reading was correct; the earlier 3-second
window that suggested otherwise was a measurement artefact.

**Method note that matters:** `DeltaTimeSeconds` (+0x5B0) is **not** a reliable pause
signal — it stayed ~0.007 while the world was frozen. `TimeSeconds` (+0x5A0) is the signal.
(`Interact` on the obelisk did drive `DeltaTimeSeconds` to exactly 0, but Status/Pause/Photo
did not, so only `TimeSeconds` generalises.)

Raw series, Status & Inventory (frozen after the transition):

```
t=55.131 real=73.739 delta=0.0077
t=55.433 real=74.041 delta=0.0095
t=55.502 real=74.110 delta=0.0101   <- menu opened
t=55.502 real=75.445 delta=0.0069   <- TimeSeconds frozen, RealTime advances
```

---

## 2. C9 — suspend saves: **live answer**

While the instance sat in gameplay with no input, the suspend writer produced:

| File | mtime | interval |
|---|---|---|
| `World0_Suspend_1.sav` | **20:43:20** | — |
| `World0_Suspend_0.sav` | **20:45:20** | **120 s** |

(Second instance: `20:41:20` then `20:44:15`.) So the period is **~2 minutes**, the slot
**alternates 0/1**, and it fires **with no user action** — exactly the
`FS_GameInstance_C` `K2_SetTimerDelegate` loop with `BlockSuspendSaves` found statically.
**C9 is closed**: mechanism and cadence measured.

---

## 3. E2 — two instances on one PC: **PASS**

Two processes, same exe, separate `-UserDir` (`inst1saved`, `inst2saved`), both driven to
gameplay. Frida on each PID:

```
pid 43224  TimeSeconds 411.986 -> 414.902   real 456.076 -> 458.992   WORLD-LIVE
pid 36216  TimeSeconds  60.765 ->  63.586   real  72.948 ->  75.769   WORLD-LIVE
```

Memory at the seeded 640×360 / scalability-0 profile:

| Instance | Working set | Private |
|---|---|---|
| inst1 | 2.77 GB | 3.70 GB |
| inst2 | 2.63 GB | 3.58 GB |
| **combined** | **5.40 GB** | **7.28 GB** |

Physical RAM is **63.86 GiB**; the acceptance bound was 0.75× = 51.4 GB combined private.
**7.28 GB is 14 % of the bound** — two instances fit comfortably at this profile. (The
earlier ~19.5 GB/instance figure was at a different, higher resolution/scalability; at the
640×360 profile the real footprint is ~2.7 GB WS each.)

---

## 4. D8 — input path: **live answer**

`PostMessage` drove **keyboard gameplay verbs** and the menus:
`Tab` → Status & Inventory, `Esc` → Pause menu, `P` → Photo mode, `E` → Checkpoint UI.
That confirms the Slate-routed path (`WM_KEYDOWN` → `FSlateApplication` → `PlayerController`)
reaches gameplay actions, not just menus.

Mouse *look* remains the raw-input axis (`RegisterRawInputDevices` + `GetRawInputData`
wired); it was not injected, and `PostMessage` cannot synthesise raw deltas. This matches
the static split exactly.

---

## 5. Isolation

The real save, `%LOCALAPPDATA%\QueensDomain\Saved\SaveGames\World0.sav`, was **unchanged**
throughout (`79823 B`, `2026-11-06 15:30:14`). All writes — including the new suspend saves —
landed in `_live\inst1saved\Saved\` and `_live\inst2saved\Saved\`. The seeded copies were
made with `-UserDir=` and no `-saveddirsuffix`.

---

## 6. Reproduction

```
_live\run-real.ps1                     # launch clean install, hold with a sleep loop
_live\frida_clock.js / frida_clock.py  # 10 Hz TimeSeconds/RealTimeSeconds/DeltaTimeSeconds
_live\frida_probe_pid.py <pid> 3       # per-PID WORLD-LIVE / FROZEN check
_live\qd-input.ps1 -Key <VK>           # PostMessage key (Tab=9, Esc=27, J=74, P=80, E=69)
_live\qd-input-pid.ps1 -ProcessId <pid> -Key <VK>
_live\_marker_summary.py               # per-state TimeSeconds slope -> PAUSED/running
_live\run-inst2.ps1                    # second instance, separate -UserDir
```
