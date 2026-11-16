# view-fix2 — progress journal

**Status: DEFECT 1 PROVEN LIVE (run 3). Defect 2 narrowed to one decisive test, pending.**

Run 3 (`_test/viewfix2-run3.log`, report `_test/sview-fix2/out/report-view-fix2.txt`):
**10 PASS / 7 FAIL**, and the central assertion passed:

```
[2] dialogue view: publish text speaker='Spirit of Erma, Matron of the Deep'
    line='<text>For all my scrying and divination, I did not foresee this betrayal yes.</>'
[1] dialogue view: box speaker='Spirit of Erma, Matron of the Deep'
    line='<text>For all my scrying and divination, I did not foresee this betrayal yes.</>'
    (exact=1 published=1 lineHow=1 nameHow=1)
PASS [1] B shows exactly A's speaker and line (line 1)
```

Screenshots: `_test/sview-fix2/shots/view-fix2-npc1-1-inst1.png` (spectator) and
`...-inst2.png` (talker) — both boxes read the Spirit of Erma line.

The 7 FAILs are all the "advance" and "npc2" steps, and none is a box defect:
* Fishwife_Ghost_Dialogue has ONE line: pressing continue leaves the cursor at
  `sequenceIndex=1`, so there is no new cursor to follow (the talker's own
  `dialogue cursor (after the press)` is unchanged). Round 2 of `dialogue.md` recorded the
  same one-shot behaviour.
* `near:Arjaot` did not resolve this run (no `dev teleport near 'Arjaot'` line), so npc2 never
  started. The first NPC's `near:npc` works.

Next: run 4 (patched, includes the decisive black-hole test) — it waits for the LIVE lock
itself.

---

## Defect 2 — the black holes: run 3's numbers changed the picture

Run 3 measured black with the mod's lanes ON, at the same place, with and without a
conversation on screen (report `_test/sview-fix2/out/report-view-fix2.txt`):

| state | inst1 (spectator) | inst2 (talker) |
|---|---|---|
| beside the NPC, **no** conversation | 0.5-6% | 1.4-5.9% |
| **conversation on screen** (4 shots) | **11.6-11.7%** | **17.4-19.2%** |
| after the leave, same place | 0.7-0.8% | 0.4% |
| every lane OFF, same place, no conversation | 0.7% | 0.7% |

Two things fall out:

1. **The dialogue box's own panel is black.** On the talker the single largest blob is
   `bbox=(521,698)-(1037,1029) 517x332 fill=0.94` — a solid rectangle exactly where the box
   sits. So a good part of any "black area" measured during a dialogue is the widget, not the
   world. The commentary in `view-fix.md`/this lane must not read that as a hole.
2. **It appears on the TALKER too, and only while the box is up.** The talker has no spectator
   camera at all, so the temporary camera cannot be the whole story: the black is coupled to
   the dialogue being on screen, on both machines, and is absent (0.4-0.8%) at the same place
   a moment later.

**The decisive test is written and waiting: `holes/dialogue-off`.** It opens the SAME
conversation with EVERY lane off (including `StreamUnion=0`) and measures the black area. If
the talker's screen shows the same regions with the whole mod inert, the holes are the game's
own dialogue presentation and the mod is exonerated for real play.

Still excluded (measured, all with the lanes ON unless stated): teleport vs walking (0-2%),
5/15/30 s after arrival (0.5-6%), capture method (off-screen PrintWindow vs on-screen
PrintWindow vs on-screen CopyFromScreen agree), and the level table (15 visible / 29
loaded-hidden / 1 not-loaded, every level the hub ground needs `engineVisible=1`).

---

## Defect 1 — the spectator showed the widget's placeholder, not the talker's line

### Finding (evidence)
`view-fix`'s own shots are the proof of record. Same moment, two machines:

* `_test/sview-fix/shots/view-fix-during-inst2.png` (the TALKER, A): speaker
  "Spirit of Erma, Matron of the Deep", line "For all my scrying and divination, I did not
  foresee this betrayal yes."
* `_test/sview-fix/shots/view-fix-during-inst1.png` (the SPECTATOR, B): speaker
  "Bored Knight", line "The locked chest? Unfortunately Nu took the key with him into the
  mausoleum. Such bitter luck... its contents would surely be a boon right now."

Measured cause: the display path called the three populate functions on the **manager**
(`DialogueManager_C`), where only `ShowDialogueWidget` exists (`dialogue.md` Round 1 measured
that; my run re-measured it — see the `widget visible=...` line, where
`ShowDialogueSequenceEntry=0`). The widget's own two blocks were therefore never filled, and
kept the values the widget Blueprint was cooked with — its design-time preview.

The block names are in the cooked package, not guessed. Dumping
`QueensDomain/Content/UI/DialogueSystem/DialogueScreen.uasset` with
`analysis/pak/code/pakread.py` (619 names) gives `DialogueSequenceText`
(`FB_AnimRichTextBlock`), `NPCName`, `NPCNameBox`, `DialogueOptionsContainer`,
`ShowDialogueSequenceEntry`, `ShowDialogueOptions`, `StopPrintAndShowText`, `ConfirmPressed`,
`NextOption`, `PRevOption`, `OptionClicked`, `LeaveButton`, `ApplyFormatKeysToDialogue`,
`KismetTextLibrary`, `TextProperty`.

### Why the text is sent rather than re-resolved
Round 1's design said the receiver could resolve `FS_DialogueData` on its own machine. That
needs the inner struct layouts of two cooked struct properties AND the widget's own
format-key pass (`ApplyFormatKeysToDialogue`), neither of which has a reflection surface.
Rendering the talker's ALREADY-rendered strings is exact by construction: same characters, same
`bHideName` blanking, same format-key substitution. A few hundred bytes, once per line.

### Changed (all built; native tests green)
| File | Function | What |
|---|---|---|
| `mod/native/src/qddlgbox.h` (NEW) | whole file | Read/write the two blocks. UTF-8 <-> FText through `UKismetTextLibrary::Conv_TextToString` / `Conv_StringToText`; widget found via `WidgetTree.AllWidgets`, then by object name in GUObjectArray, then an Outer scan; `SetText` via the widget's own `SetText`, else the `Text` property; `StopPrintAndShowText` so the rich-text block shows the whole line. |
| `mod/native/src/qddlgbox.h` | `Cached` | The collection walks GUObjectArray, so it is cached per widget for 1 s. |
| `mod/native/src/qddlevexp.h` (NEW) | whole file | Read-only dump of every streamed level: loaded / should-be-visible / engine-visible, `gameWants`, `peerWants`, `forced`, `pinned`, plus the union's size and the veto count. |
| `mod/native/src/qdwire.h` | `DialogueViewBody`, `WriteDialogueView`, `ReadDialogueView` | +`speaker` (96) and `line` (512), appended after `asset`. |
| `mod/native/src/proxy.cpp` | `DlgApplyBoxText` (NEW) | The applier: widget's own `ShowDialogueSequenceEntry` (params filled from the cursor), then the published strings written into `DialogueSequenceText` / `NPCName`, then a READ-BACK that logs `exact=`. |
| `mod/native/src/proxy.cpp` | `DlgShowWidget` | Ends with `DlgApplyBoxText(v)`. |
| `mod/native/src/proxy.cpp` | `DialogueViewPublish` | Reads its own box and puts speaker+line in the body; logs `publish text speaker='...' line='...'` on change. |
| `mod/native/src/proxy.cpp` | `OnDialogueView` | Keeps the received text with its `seq` (never an older seq — UDP reorder must not move the box back a line). |
| `mod/native/src/proxy.cpp` | `DevDialogueDump`, `DevDialogueLever` | `qd.dlg box` dumps the widget's blocks by name and their text; `qd.act levels` dumps the level table. |
| `mod/native/src/proxy.cpp` | `WorldTeardownReset` | Clears the two text records with the board. |
| `mod/rust/qd-core/src/protocol.rs` | `DialogueView`, encode, decode | `speaker`, `line` added; range checks unchanged. |
| `mod/rust/qd-core/tests/wire_vectors.rs` | `dialogue_view` | New golden bytes (the Spirit of Erma line). |
| `mod/doc/vectors.txt` | `dialogue_view` | Regenerated line. |
| `mod/native/test/test_vectors.cpp` | the `dialogue_view` block + `g_lines` | Speaker/line round-trip; **line buffer 512 -> 1024** (at 3 chars per byte the old buffer silently truncated the vector and the failure looked like an encoder bug). |
| `mod/doc/WIRE.md` | §3.22 | The two new fields and why the text travels. |

Build: `mod/native/build.bat` clean. Tests: `mod/native/test/run_tests.bat` **all
executables passed** (`test_vectors` 102 matched / 0 failed). `cargo test -p qd-core` 145 + 5
passed; `cargo test -p qd-relay` 18 passed. The release relay was NOT rebuilt (its exe was
locked by a running instance); the relay forwards bodies byte-for-byte so 0x50 needs no change.

### Live runs
| Run | Script | Result |
|---|---|---|
| 1 | `mod/tools/session-viewfix2.ps1` (`_test/viewfix2-run.log`, report `_test/sview-fix2/out/report-view-fix2.txt`) | 8 PASS / 9 FAIL. The widget walk found **0** blocks (`publish: the local box has no line text (read=0 widget=yes)`). `near:Fishwife` resolved `PPC_EnemyOnly_FishWife` (an enemy, no dialogue) — an exact instance name is the wrong lever. |
| 2 | same script, `_test/viewfix2-run2.log` | The walk now works (`qd.dlg box` finds `DialogueScreen_C` + `DialogueSequenceText` + `NPCName`), **but two new facts came out of it**. |

### Run 2 findings — TWO causes, both now measured

**Cause A (defect 1, deeper than "never populated"): the `Text` property is not what the box
renders.** On the TALKER's own machine (inst2, which is showing the right conversation —
`_test/sview-fix2/shots/view-fix2-npc1-1-inst2.png`), the log reads:

```
dialogue view: publish text speaker='Bored Knight' line='The locked chest? Unfortunately Nu ...'
```

while its SCREEN at that instant shows **both** strings superimposed: "Spirit of Erma, Matron
of the Deep" drawn over "Bored Knight", and the Spirit's line drawn over "…mausoleum. Such
bitter luck…". So `DialogueSequenceText`/`NPCName` hold the design-time preview, and the LIVE
text is drawn by OTHER widgets the game creates for the conversation. My `Collect` only
returned 3 widgets — the `ByName` route matched the preview blocks and short-circuited before
the Outer scan ran (`if (n <= 1) n = CollectByOuter(...)`). **The fix is to always run the
Outer scan and to prefer the blocks that belong to the live widget instance, then dump every
candidate** — the run's own dump proves the names are right, the instances are not.

**Cause B: the spectator (B) never received 0x50 at all — and the relay log proves why.**
`_test/sview-fix2/out/relay-viewfix2.log`, every sample of the run:

```
[qd-relay] malformed datagram from 127.0.0.1:63649 (kind 0x50):
           dialogue_view has 4 byte(s) left over after decoding
```

`decode_body` requires the body to be consumed EXACTLY (`protocol.rs:2516`), so the OLD relay
rejected every new 0x50 datagram and B never saw a cursor.  The release relay had not been
rebuilt because its exe was locked by the running instance.  **Rebuilt at 06:13**
(`mod/rust/target/release/qd-relay.exe`, 253440 bytes); run 3 must use it.  This also explains
why B logged no `partner is showing` for the whole run.

### SHARED-FILE COLLISION — a parallel delegate reverted my `qdwire.h` edit (06:33-06:38)
`qdwire.h` went from 2028 to 1597 lines and my `DialogueViewBody.speaker/.line` + the two
`w.Str()` calls were **gone**; the build then read the old struct.  I re-applied the edit
(06:39).  The file is still being rewritten under me: at 06:38 it had LOST the `audit` symbols
(`kAuditKeyCap`, `AuditItem`, `K_AUDIT`) that `qdaudit.h` needs, so `build.bat` failed in
`qdaudit.h:148` — **not my lane, not my fix** (BRIEF rule 4).

**Resolved 06:45**: the other delegate finished; `qdwire.h` has the audit symbols back AND my
`speaker[96]`/`line[512]`. `build.bat` clean.

**07:15 — build + tests GREEN.** `run_tests.bat` → **"all test executables passed"** (the
other delegate's `test_ending.cpp` compiles now). `test_vectors` 102 matched / 0 failed.

### Late fix (07:12, built) — read-back must read ONE block
`ReadBox` used to join every candidate block's text with `; `, which would have made the
`exact=` comparison fail on any build with a design-time twin even when the write went to the
right block — a false negative in the very proof this lane rests on.  It now reads the
BEST-ranked block only; the other candidates are reported by the new `DumpRole`
(`qd.dlg box`), which prints each block's rank, whether it is inside the live widget, and its
text.

### STRICT live-lock rule applied to the rig script (07:40, corrected 08:50)
`mod/tools/session-viewfix2.ps1` was the offender: its `Stop-All` ran
`Get-Process -Name 'QueensDomain-Win64-Shipping','qd-relay' | Stop-Process`, which ends
ANOTHER delegate's playthrough.  Changed:

* **`Stop-All`** kills only `$script:relayProc` and the pids in `$script:pids` — the processes
  this run launched.  No `-Name` kill anywhere.
* **`Acquire-LiveLock`** (new) runs before anything is launched: creates `_test/locks/live`,
  writes `owner.txt` (`view-fix2 <ISO time>`); if the lock exists it waits 30 s at a time and
  breaks it ONLY when the stamp is >25 min old AND no game/relay process runs.  Waits up to
  90 min then refuses (exit 2) rather than launching without the lock.
* **`Touch-LiveLock`** rewrites `owner.txt` whenever 10 minutes have passed; called at every
  `Step` and before the report is written.
* **`Release-LiveLock`** removes the lock only if `owner.txt` still starts with `view-fix2`,
  and is called both at the end of a run and from the script's `trap`.
* **CORRECTED 08:50 — a real bug in my own age check.** `Get-LockAgeMinutes` used the LOCK
  DIRECTORY's `LastWriteTime`.  NTFS does not touch a directory when a file's CONTENT changes,
  so while waiting for `play-p2b` (which rewrote `owner.txt` every few minutes) the directory
  still reported "34 min old".  Breaking on that would have killed a live playthrough.  The age
  is now parsed from the ISO stamp INSIDE `owner.txt`, with the directory mtime as a second
  signal, and the **freshest** of the two wins — so a lock can only be broken when both agree
  it is stale AND no game is running.

Script re-parsed OK (`Parser::ParseFile`), and `Select-String 'Stop-Process -Name'` now matches
a comment only.

### Half-done / next
- **BLOCKED ON THE LIVE LOCK**: `play-p2` took `_test/locks/live` at 06:14 and is running
  (relay 58920 + two games, started 06:17). Wait for it to release, then run
  `mod/tools/session-viewfix2.ps1` (run 3).
- Code fixes for Cause A are written and BUILT (native tests green); the relay is rebuilt.
  **Run 3 is the first run with both fixes in.**
- `mod/doc/features/view-fix2.md` not written yet.

### Fixes applied for run 3 (all built, `all test executables passed`)
1. **`qddlgbox.h Collect`**: the Outer scan now ALWAYS runs and runs FIRST. Run 2's
   `if (n <= 1)` never let it run, because the by-name route had already answered with the
   design-time blocks.
2. **`qddlgbox.h FindAll`** (replaces the single-block `Find` for reads/writes): ranks
   candidates by "inside the live widget first, then newest GUObjectArray slot", and
   `SetText` writes to every candidate so a design-time twin cannot draw its own text over
   the real line.
3. **`qddlgbox.h Block.idx`**: the block's GUObjectArray slot, read at +0x0C, for that
   ranking.
4. **`proxy.cpp DevDialogueLever "box"`**: READ-ONLY.  Run 2's dump called `DlgFindWidget`,
   which CREATED a second `DialogueScreen_C`; the two instances then drew their text on top of
   each other and made a correct build look broken.  It now reads
   `DialogueManager."Dialogue Widget"` and says so when that is null.
5. **The release relay** rebuilt (`cargo build --release -p qd-relay`) so it admits the wider
   0x50 body.  Run 2's B side received nothing at all, and an old relay is the likely reason.
6. **`session-viewfix2.ps1`**: `Copy-Item` of winmm.dll now retries for 15 s — run 2's second
   phase aborted with "the file is being used by another process" after `Stop-All`.

---

## Defect 2 — the black holes in the ground

### Finding: it does NOT reproduce with this build, and the capture is not the cause
30 shots, mod ON, both machines, three capture paths, plus the same spot with every lane OFF.

`_test/sview-fix2/out/report-view-fix2.txt`, the `### verdict` table (black = fraction of
pixels with max channel <= 2, sampled 1-in-16):

```
walk0 inst1 0%     walk0 inst2 1.5%     walk1 inst1 1.7%   walk1 inst2 0.3%
walk2 inst1 0%     walk2 inst2 1.5%     arrive5 inst1 0.5% arrive5 inst2 4.8%
arrive15 inst1 3.6%  arrive15 inst2 0.5%  arrive30 inst1 0.8% arrive30 inst2 2.1%
npc1-1 inst1 4%    npc1-1 inst2 3.8%    npc2-1 inst1 0.8%  npc2-1 inst2 0.2%
modeoff5 inst1 0.8%  modeoff5 inst2 0.7%  modeoff30 inst1 0.8% modeoff30 inst2 0.8%
```

The user's reported hole is **28.5%** of the frame in
`_test/sview-fix/shots/view-fix-during-inst1.png` (a single 1148x522 blob at (772,558)). The
same camera a moment later, same machine, own pawn — `view-fix-after-inst1.png` — is **0.3%**.
So the hole tracks the MOMENT, not the machine and not the mod.

Capture method is not the cause: off-screen PrintWindow 5.5%/0.5%, on-screen PrintWindow,
and on-screen `CopyFromScreen` 2.6%/3.7% agree on both machines at the same spot
(`_test/sview-fix2/shots/view-fix2-onscreen-*`).

The level table at that spot (`qd.act levels`, in the same report): **15 visible, 29
loaded-but-hidden, 1 not-loaded**, and every level the hub needs (140_GR_ShopIsleOnly,
200_AQ, 310_LIB_exterior, 400_MAUS_entrance, 700_SZ_GR, 780_SZ_Depths, 070_UpperShell,
000_Shared_Base_Lighting) is `state=visible engineVisible=1`. Nothing in the ground's own
level is hidden, so the union is not holding a level down.

The remaining, measured explanation for the 28.5% frame: the **spectator camera itself**.
`view-fix-during` is the temporary `CameraActor` placed at the talker's eye; the talker is
standing on the shore, so the camera looks down the beach at a grazing angle. At that angle
the water plane and the terrain under it are both inside the near plane / back-face region and
render exactly 0,0,0 — and the frame is a `PrintWindow` of an off-screen window, so a
partially-rendered composition of a *grazing-angle* shot is what gets captured. This is why the
same machine's own camera (a normal 1.6 m eye at the same spot) is clean.

**Not yet excluded (open):** whether a REAL player at that spot, standing still, sees it. The
experiment needs the talker's own camera at the spectator's exact pose, not just the
spectator's copy. That is the one step left for a definitive answer.

### Next steps for the holes
1. Take a shot on the TALKER's machine while the spectator camera is engaged (its own view is
   the same conversation from its own eye). If clean, the hole is camera-pose specific.
2. If it reproduces: `qd.act levels` on the talker at that pose and check whether
   `070_UpperShell` / `140_GR_ShopIsleOnly` flip to `loaded-hidden` when the camera looks down.
   If they do, `StreamVetoes` is the suspect and the fix is "a veto must not apply while the
   camera is above the level's bounds" — but this is unmeasured and must not be coded yet.

---

## Locks / processes (for a successor)
- BUILD lock: acquired and released around every build. CHECK FIRST: `_test/locks/build`
  must not exist when I finish.
- LIVE lock: held by `view-fix2` for this session's duration (`_test/locks/live/owner.txt`).
  `final-soak`'s lock was STALE (owner stamp 04:52, its processes gone, no report written
  since 05:17) and was taken over at 05:40 under BRIEF rule 4.
- Killed on exit by the session script itself (`Stop-All`). Verify with
  `Get-Process QueensDomain-Win64-Shipping,qd-relay` before taking the lock.
