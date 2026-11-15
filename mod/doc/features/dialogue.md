# Dialogue spectator — player B sees the conversation player A is having

Owner id: `dialogue`. Module: `mod/native/src/qddialogue2.h` (new) + the spectator lane in
`proxy.cpp`; wire kind `0x50`. Companion lane: `qddialogue.h` (`K_DIALOGUE` 0x21) already
publishes *who* is talking to *which* NPC and blocks that NPC on the partner's machine.

## The gap this closes

`qddialogue.h` stops two players from starting the same conversation and blocks the
partner's NPC, but it does **not** show the conversation: measured on the rig (Session 29,
run 12) the user report was *"a dialogue box on one instance but not on the other"*. A talks
to a quest NPC; B keeps walking around, blind to a quest decision being made.

## Design (≤15 lines)

* The talking machine publishes the **cursor** its `DialogueManager` is showing: the
  asset's object name, `CurrentDialogueSequenceIndex`, an entry index, the option count,
  the option it picked, and the NPC's stable placed-object id. Unreliable, latest-wins,
  on change + 1 Hz (`K_DIALOGUE_VIEW` 0x50).
* The text is **not** sent: it lives in the cooked `FS_DialogueData` asset (61 of them),
  which both machines loaded; the asset's object name is the identity.
* The receiver drives **its own game's dialogue widget** with that cursor: it assigns
  `CurrentDialogue` / `CurrentDialogueNPC` / `CurrentDialogueSequenceIndex`, sets the
  manager's own `RunningDialogue`, creates the game's `DialogueScreen_C` widget through
  `UWidgetBlueprintLibrary::Create` when `Dialogue Widget` is null, adds it to the
  viewport, and calls `ShowDialogueWidget`.
* Camera: `PlayerController.SetViewTargetWithBlend(speaker NPC, 0.4 s)`, restored to the
  local pawn on release.
* Movement: the game's own `DisableAllPlayerInput` plus `SetIgnoreMoveInput` /
  `SetIgnoreLookInput` on the local controller; restored with `EnableAllPlayerInput` and
  the same pair.
* Refusals: a player who took damage in the last 8 s is not hijacked; neither is a player
  in their own conversation/cutscene/map transition, or dead. The refusal is remembered
  against that publication so the 1 Hz republish does not re-decide it.
* Release happens once and is idempotent: conversation ended, sender stale (5 s), session
  dropped, map change (nothing is restored on a dead world — the new pawn is the restore),
  or the local player becoming busy/combat. Every lever is logged with its result.

## What the game actually exposes (measured, `qd.dlg` on a live pair)

The `DialogueManager` is a Blueprint **component** on the player pawn. Its surface, read
from the live object:

```
CurrentDialogueNPC            ObjectProperty  @184
CurrentDialogue               ObjectProperty  @192
CurrentAvailableOptions       ArrayProperty   @200   (TArray; Num at +8)
Dialogue Widget               ObjectProperty  @216   <-- the widget it shows
CurrentDialogueSequence       StructProperty  @224   (40 bytes)
CurrentDialogueSequenceIndex  IntProperty     @264
DisplayingError/RunningDialogue/ForceUIInputModeStatus  Bool @268/@269/@270
```

and its display UFunctions are **`ShowDialogueWidget` (0 params)** and `HideDialogueWidget`
(the hide path ran: `widget hidden=1`). `ShowDialogueSequenceEntry`, `ShowDialogueOptions`,
`DialogueSequenceContinue`, `End`, `EndDialogue` and `SetUpForDialogue` are **graph node
names, not UFunctions** — calling them resolves nothing (measured: `fn
DialogueSequenceContinue: not found`). So the whole display path is: create/assign
`Dialogue Widget`, set the cursor fields, set `RunningDialogue`, call `ShowDialogueWidget`
— and the widget resolves the speaker name and the line text itself from the asset.

Two dead ends that cost a run each, recorded so they are not retried:
`ShowDialogueSequenceEntry`/`Continue`/`End` do not exist as functions; and liveness for
the publish must be the manager's `RunningDialogue` bool, **not** "CurrentDialogue is
set" — after the game's own death path ended a conversation those fields were still
populated, and the spectator never released.

## Files

* `mod/native/src/qddialogue2.h` — new. `dlgview::Board`: per-slot latest-wins publish,
  5 s staleness, the engage/update/hide decision, the "declined publication" record.
  Pure logic, no game calls.
* `mod/native/src/proxy.cpp` — new section `6c-ter`: publish, receive, cursor drive,
  widget ensure/show, camera, input, restore, `qd.dlg` dump; one `#include`; one
  `switches:` field (`dialogueView=`); one dispatch case; one `DialogueViewPump(now)` call
  in `HK_Tick`; `WorldTeardownReset` clears the board; `DialoguePump` gains the "a
  spectated manager is not a local conversation" guard (`DlgSpectating()`);
  `DevDialogueLever` call.
* `mod/native/src/qdwire.h` — `K_DIALOGUE_VIEW = 0x50`, `DialogueViewBody`,
  `WriteDialogueView` / `ReadDialogueView`.
* `mod/native/src/qdconfig.h` + `mod/qdcoop.ini` — `DialogueView` switch (built-in default
  0 so a missing ini cannot hijack a camera; the shipped ini sets 1).
* `mod/native/test/test_dialogue2.cpp` — new, 52 assertions.
* `mod/native/test/test_vectors.cpp` — accepts the reserved `0x50–0x53` kind range and adds
  the `dialogue_view` golden sample.
* `mod/rust/qd-core/src/protocol.rs` — `Kind::DialogueView`, `DialogueView` body,
  encode/decode with range checks, `dialogue_view_flags`, latest-wins classification, two
  unit tests.
* `mod/rust/qd-core/tests/wire_vectors.rs` — the `dialogue_view` vector (regenerates
  `mod/doc/vectors.txt`).
* `mod/doc/WIRE.md` — kind table row + §3.22.
* `mod/tools/session_dialogue.ps1` — new; the live proof.

## Coverage

* **Every** `FS_DialogueData` conversation (61 assets): the lane keys on whatever asset the
  talking player's manager is showing, so an NPC the content adds tomorrow needs nothing.
* **Every** NPC class deriving from `NPC_Base_C` (`FindNpcById`, the same resolver the
  existing lock uses).
* Not covered: the two `UFS_DialogueAction` animated side shows (Sipeha banish/eat) — those
  are actor animations run by the action, not dialogue text, and they stay on the talker's
  machine by design.
* `entry` may be unreadable in this build (`SYNC-GAPS` records *"entry is still read as
  0"*); the wire carries it so a build that exposes it needs no format change.

## Proof

**Build:** `native/build.bat` clean (`exit 0`, no warnings). `native/test/run_tests.bat`:
every suite passes — `test_dialogue2` **52 passed, 0 failed**, `test_vectors` **48 matched,
0 failed, 0 missing** (was 43; the new golden sample). One unrelated suite
(`test_interact`, another delegate's lane) has 1 failure that is not mine.
`cargo test -p qd-core` **140 + 5 passed**; `cargo test -p qd-relay` **17 passed**; the
release relay was rebuilt so it admits kind 0x50.

**Live (run 3, `mod/tools/session_dialogue.ps1`):** host=inst1 (B), peer=inst2 (A),
`DialogueView=1`. A talked to the ghost NPC through the game's own interact path.

```
[2] dialogue: local started with Fishwife_Ghost_BP [Fishwife_Ghost_BP_C]
[2] dialogue view: publish asset=Fishwife_Ghost_Dialogue line=0 entry=0 options=1 seq=2
[1] dialogue view: partner is showing 'Fishwife_Ghost_Dialogue' line=0 entry=0
[1] dialogue view: cursor set=7 (dialogue=1 npc=1 line=1 entry=0)
[1] dialogue view: created the game's dialogue widget DialogueScreen_C
      (assigned to Dialogue Widget=1 AddToViewport=1)
[1] dialogue view: widget visible=1 (widgetExisted=0 RunningDialogue=1 ShowDialogueWidget=1)
[1] dialogue view: camera -> Fishwife_Ghost_BP [Fishwife_Ghost_BP_C] (ran=1)
[1] dialogue view: input locked (ran=1)
[1] dialogue view: SHOW from slot 1 asset=Fishwife_Ghost_Dialogue line=0 entry=0 ...
[2] rig: flag set -- SaveFunctions.SaveNamedData(CoopDialogueProbe, 1, ...) ran
[1] rig: flag dump -- CoopDialogueProbe = 1 (game bank 1, lane table 1)
[2] rig: flag dump -- CoopDialogueProbe = 1 (game bank 1, lane table 1)
[2] rig: kill -> FS_PlayerChar_C.Kill ran=1
[2] dialogue: local death ended the conversation (end=1 ended=1); lock released
[1] dialogue view: HIDE -- the partner's conversation ended
      (widget hidden=1 RunningDialogue cleared=1 dialogue ended=1)
[1] dialogue view: camera restored (ran=1)
[1] dialogue view: input restored (ran=1)
```

Verdict: **21 PASS / 2 FAIL**. The two failures are (a) the harness's own
`switches:` assertion, which read the log from the wrong window — run 3's log line 5 is
`... dialogue=1 dialogueView=1 ...` and the lane demonstrably ran; the check now reads the
whole file; and (b) the line advance, which is the open item below.

**Screenshots** (`G:\Freespace\Queens_Domain\_test\sdlg-spectator\shots\`):

* `sdlg-spectator-during-inst1.png` — B's screen **during**: the game's own dialogue box
  with the speaker **"Bored Knight"** and the line *"The locked chest? Unfortunately Nu
  took the key with him into the mausoleum. Such bitter luck... its contents would surely
  be a boon right now."*, plus the advance diamond; the camera is framed on the speaker.
* `sdlg-spectator-after-inst1.png` — B's screen **after**: the box is gone, B's own
  first-person view (sword, HUD) is back. Camera and input restored.

## Open items

1. **B following an ADVANCE is unproven.** The rig cannot make A advance: the manager has
   no `DialogueSequenceContinue`/`End` UFunction and the dialogue widget's continue is an
   input event the talker's machine has disabled. Everything the advance would exercise is
   in place (a new cursor is a new `seq`, the widget is re-shown, 52 unit assertions cover
   SHOW/UPDATE/HIDE), but the next session needs a lever that presses the dialogue widget's
   own continue (its surface is dumped by `qd.dlg`).
2. The widget's text is recomputed by the game from `CurrentDialogue` +
   `CurrentDialogueSequenceIndex`; `CurrentDialogueSequence` (the 40-byte struct) is left
   empty on the spectator and the text still rendered, so it is not needed for the line.
3. Combat refusal is damage-recency only (8 s). It is unit-tested; the live run had god-mode
   on, so the refusal was not exercised live.
