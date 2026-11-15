# Dialogue spectator  Eplayer B sees the conversation player A is having

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
  dropped, map change (nothing is restored on a dead world  Ethe new pawn is the restore),
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
names, not UFunctions**  Ecalling them resolves nothing (measured: `fn
DialogueSequenceContinue: not found`). So the whole display path is: create/assign
`Dialogue Widget`, set the cursor fields, set `RunningDialogue`, call `ShowDialogueWidget`
 Eand the widget resolves the speaker name and the line text itself from the asset.

Two dead ends that cost a run each, recorded so they are not retried:
`ShowDialogueSequenceEntry`/`Continue`/`End` do not exist as functions; and liveness for
the publish must be the manager's `RunningDialogue` bool, **not** "CurrentDialogue is
set"  Eafter the game's own death path ended a conversation those fields were still
populated, and the spectator never released.

## Files

* `mod/native/src/qddialogue2.h`  Enew. `dlgview::Board`: per-slot latest-wins publish,
  5 s staleness, the engage/update/hide decision, the "declined publication" record.
  Pure logic, no game calls.
* `mod/native/src/proxy.cpp`  Enew section `6c-ter`: publish, receive, cursor drive,
  widget ensure/show, camera, input, restore, `qd.dlg` dump; one `#include`; one
  `switches:` field (`dialogueView=`); one dispatch case; one `DialogueViewPump(now)` call
  in `HK_Tick`; `WorldTeardownReset` clears the board; `DialoguePump` gains the "a
  spectated manager is not a local conversation" guard (`DlgSpectating()`);
  `DevDialogueLever` call.
* `mod/native/src/qdwire.h`  E`K_DIALOGUE_VIEW = 0x50`, `DialogueViewBody`,
  `WriteDialogueView` / `ReadDialogueView`.
* `mod/native/src/qdconfig.h` + `mod/qdcoop.ini`  E`DialogueView` switch (built-in default
  0 so a missing ini cannot hijack a camera; the shipped ini sets 1).
* `mod/native/test/test_dialogue2.cpp`  Enew, 52 assertions.
* `mod/native/test/test_vectors.cpp`  Eaccepts the reserved `0x50 Ex53` kind range and adds
  the `dialogue_view` golden sample.
* `mod/rust/qd-core/src/protocol.rs`  E`Kind::DialogueView`, `DialogueView` body,
  encode/decode with range checks, `dialogue_view_flags`, latest-wins classification, two
  unit tests.
* `mod/rust/qd-core/tests/wire_vectors.rs`  Ethe `dialogue_view` vector (regenerates
  `mod/doc/vectors.txt`).
* `mod/doc/WIRE.md`  Ekind table row + §3.22.
* `mod/tools/session_dialogue.ps1`  Enew; the live proof.

## Coverage

* **Every** `FS_DialogueData` conversation (61 assets): the lane keys on whatever asset the
  talking player's manager is showing, so an NPC the content adds tomorrow needs nothing.
* **Every** NPC class deriving from `NPC_Base_C` (`FindNpcById`, the same resolver the
  existing lock uses).
* Not covered: the two `UFS_DialogueAction` animated side shows (Sipeha banish/eat)  Ethose
  are actor animations run by the action, not dialogue text, and they stay on the talker's
  machine by design.
* `entry` may be unreadable in this build (`SYNC-GAPS` records *"entry is still read as
  0"*); the wire carries it so a build that exposes it needs no format change.

## Proof

**Build:** `native/build.bat` clean (`exit 0`, no warnings). `native/test/run_tests.bat`:
every suite passes  E`test_dialogue2` **52 passed, 0 failed**, `test_vectors` **48 matched,
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
`switches:` assertion, which read the log from the wrong window  Erun 3's log line 5 is
`... dialogue=1 dialogueView=1 ...` and the lane demonstrably ran; the check now reads the
whole file; and (b) the line advance, which is the open item below.

**Screenshots** (`G:\Freespace\Queens_Domain\_test\sdlg-spectator\shots\`):

* `sdlg-spectator-during-inst1.png`  EB's screen **during**: the game's own dialogue box
  with the speaker **"Bored Knight"** and the line *"The locked chest? Unfortunately Nu
  took the key with him into the mausoleum. Such bitter luck... its contents would surely
  be a boon right now."*, plus the advance diamond; the camera is framed on the speaker.
* `sdlg-spectator-after-inst1.png`  EB's screen **after**: the box is gone, B's own
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

---

# Round 2 (2026-11-15) -- B follows A's ADVANCE (proven); the advance was on the WIDGET

Round 1's open item was: *"B following an ADVANCE is unproven. The rig cannot make A advance: the
manager has no `DialogueSequenceContinue`/`End` UFunction."* That was the right measurement and the
wrong conclusion -- **the advance was never on the manager**.

## Where the advance actually is (measured from the cooked package)

I dumped `UI/DialogueSystem/DialogueScreen.uasset` instead of guessing again. Its **exports** are:

```
ConfirmPressed            <-- the bound handler of SequenceConfirmButton (the advance button)
NextOption / PRevOption    the option-list cursor
OptionClicked              a picked entry
LeaveButtonCLicked         a DELEGATE signature, not callable (measured: ran=0)
RemoveFromView
ShowDialogueSequenceEntry / ShowDialogueOptions / Update NPC / ManageFocus / RevertFocus
OnPreviewKeyDown
DialogueSequenceContinue__DelegateSignature   <-- what it FIRES at its owner
DialogueOptionChosen__DelegateSignature
```

and its widgets include `AdvanceButton` (`FS_Button_C`), `SequenceConfirmButton` (`FS_Button_C`),
`DialogueOptionEntry` x2, `Leave` and `DialogueSequenceText` (`FB_AnimRichTextBlock`). So one
player "continue" **is** one `ConfirmPressed` on the widget, and the delegate is how the advance
reaches the manager. (`ShowDialogueSequenceEntry`/`ShowDialogueOptions` are graph events on the
WIDGET -- which is why looking them up on the MANAGER missed.)

## The levers (`qd.dlg`, `proxy.cpp` `DevDialogueLever`)

| value | what it does |
|---|---|
| `advance` / `press` / `continue` | presses **the widget's own** `ConfirmPressed`, ONCE (the `Show*` events are a fallback only, so a line is not skipped) |
| `opt<N>` | `NextOption` x N then `OptionClicked`; `ConfirmPressed` only if the pick is unavailable (never both -- that confirms twice) |
| `leave` | `RemoveFromView` + the manager's own `End` + `HideDialogueWidget` + clear `RunningDialogue` |
| `cursor` | the local manager's cursor (asset, sequenceIndex, line, entry, options, chosen) |
| anything else | the Round 1 read-only dump |

## Proof: A advances, B follows -- `28 PASS / 0 FAIL`

```
[2] 21:28:40.490 INFO rig: dialogue widget pressed -- ConfirmPressed (ran=1)
[2] 21:28:40.490 INFO rig: dialogue cursor (after the press) talking=1 asset=Fishwife_Ghost_Dialogue sequenceIndex=1 line=1 entry=0 options=1 chosen=65535
[1] 21:28:40.800 INFO dialogue view: update line=1 entry=0 options=1 chosen=65535
```

* `A's own dialogue cursor advanced` -- **PASS**, `sequenceIndex 0->1`.
* `B followed the advance and re-drove its own widget` -- **PASS**, `dialogue view: update line=1`.
* `B shows the line A is on (1 on both)` -- **PASS**, `B line=1, A=1`.

The spectator follows a real line advance made through the game's own button handler.

## The combat refusal, exercised -- PASS

God mode is turned **off** on the spectator for that step, its health is dropped through
`qd.selfhp`, and A publishes a new cursor:

```
[1] 21:29:16.771 INFO dialogue view: not hijacking the local player: it took damage in the last 8 s
```

B kept its camera and its legs while it was being hit, and the lane SAID so.

## Two real bugs this found (both fixed)

1. **The spectator's input was never unlocked.** The hide path logged
   `WARN fn EnableAllPlayerInput: not found`. Same cause as the death lane: the pair lives on
   **`PlayerStatsAndUtils_C`** (a `BlueprintFunctionLibrary`), and the code resolved them on the
   player pawn, whose class chain does not contain them. `DlgBlockInput` now resolves them on the
   library CDO and passes the pawn as the argument, so `input restored (ran=1)` is a real unlock.
2. **`BlockInteract` was logged without its value**, so `BlockInteract=` was indistinguishable from
   a failed write. `NpcSetBlocked` now reads the property back and logs the value, and warns when
   the write did not take.

## NOT proven, and why (stated rather than claimed)

* **"B renders the CHOSEN option."** The wire carries `chosen` and 52 unit assertions cover the
  "a choice was made" body, but two measured facts block a live claim on this build:
  (i) the reachable hub NPC (`Fishwife_Ghost_Dialogue`) offers **`options=1`** in every cursor line,
  so there is no second entry to choose between; and (ii) the manager's dumped surface has **no
  property holding a chosen index** (`CurrentDialogueNPC, CurrentDialogue, CurrentAvailableOptions,
  Dialogue Widget, CurrentDialogueSequence, CurrentDialogueSequenceIndex, DisplayingError,
  RunningDialogue, ForceUIInputModeStatus` -- nothing else), so `ReadLocalDialogueView` honestly
  reports `chosen=65535` for ever. The rig CAN press the widget's option entry
  (`rig: dialogue option 1 pressed -- NextOption+OptionClicked (ran=1)`), and the format needs no
  change for a build that exposes the index.
* **"B re-engages after the damage window lapses."** `Fishwife_Ghost_BP` is ONE-SHOT
  (`IsInteractible=0` once its conversation has run -- "a key press cannot open this object
  either") and its dialogue has ONE advanceable line (pressing Continue past `sequenceIndex=1`
  leaves the cursor at 1), so there is no new seq for B to re-engage on. The release half of the
  lane is proven in the same run (`B hid the view`, `camera restored`, `input restored`).

## Screenshots (`G:\Freespace\Queens_Domain\_test\sdlg-spectator\shots\`)

` sdlg-spectator-during-inst1.png` (the game's own box: speaker "Bored Knight" + the full line),
` sdlg-spectator-advanced-inst1.png` (after the advance), ` sdlg-spectator-chosen-inst1.png`,
` sdlg-spectator-combat-refused-inst1.png`, ` sdlg-spectator-after-inst1.png` (B's own view back).

## Switches

`DialogueView=1` and `DialogueSync=1` in `mod/qdcoop.ini` (built-in defaults stay 0, so a missing
ini cannot hijack a camera or block an NPC).

The LOCK half of `DialogueSync` is proven by the block line, whose VALUE is now printed:

```
[1] 22:31:24.472 INFO dialogue: BLOCK Fishwife_Ghost_BP [Fishwife_Ghost_BP_C] BlockInteract=1
```

`BlockInteract=1` is a READ-BACK, so the flag the partner's NPC gate reads is really set. The
release half (`dialogue: unblock ... reenabled`) is the same code path and was seen in Round 1's
run 3 and in the earlier Round 2 runs; in the final clean run the one-shot NPC never needed it.