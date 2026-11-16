# view-fix2 — the spectator's dialogue box, and the black holes in the ground

Written 2026-11-16 (session `view-fix2`, branch `sync-fix`). Modules: `mod/native/src/qddlgbox.h`
(new), `mod/native/src/qdlevexp.h` (new), glue in `mod/native/src/proxy.cpp`; wire change in
`mod/native/src/qdwire.h` + `mod/rust/qd-core/src/protocol.rs`. Live harness:
`mod/tools/session-viewfix2.ps1`.

Status: defect 1 **fixed and unit-tested, live proof in progress**; defect 2 **measured, does
not reproduce with this build** — see "Defect 2" for what that does and does not mean.

---

## The two defects, from the coordinator's images

Both defects were inspected directly in the pair the coordinator named:

* `_test/sview-fix/shots/view-fix-during-inst2.png` — inst2, **the talker (A)**: speaker
  "Spirit of Erma, Matron of the Deep", line *"For all my scrying and divination, I did not
  foresee this betrayal yes."*
* `_test/sview-fix/shots/view-fix-during-inst1.png` — inst1, **the spectator (B)**: speaker
  "Bored Knight", line *"The locked chest? Unfortunately Nu took the key with him into the
  mausoleum. Such bitter luck... its contents would surely be a boon right now."* — i.e. the
  widget's design-time preview, not the conversation.
* Both frames have large, exactly-black (0,0,0) regions in the ground/water.

---

## Defect 1 — the spectator's box

### What was actually wrong

Two independent failures, both measured on the rig:

**(i) The display path called the populate functions on the wrong object.** `DlgShowWidget`
called `ShowDialogueSequenceEntry` / `ShowDialogueOptions` on the **manager**
(`DialogueManager_C`), where they are not UFunctions — `dialogue.md` Round 1 measured that, and
every run since prints `ShowDialogueSequenceEntry=0` in the `widget visible=` line. The two
functions live on the **widget** (`DialogueScreen_C`) as graph events. So the spectator's two
text blocks were never filled, and kept what the widget Blueprint was cooked with.
The block names come from the cooked package, not from a guess: dumping
`QueensDomain/Content/UI/DialogueSystem/DialogueScreen.uasset` (`analysis/pak/code/pakread.py`,
619 names) yields `DialogueSequenceText` (`FB_AnimRichTextBlock`), `NPCName` /
`NPCNameBox` (`CommonRichTextBlock`), `ShowDialogueSequenceEntry`, `ShowDialogueOptions`,
`StopPrintAndShowText`, `ConfirmPressed`, `NextOption`, `PRevOption`, `OptionClicked`,
`ApplyFormatKeysToDialogue`, `KismetTextLibrary`, `TextProperty`.

**(ii) Even the talker's own `Text` property is not the rendered line.** Run 2 measured this:
the talker's screen showed the correct conversation
(`_test/sview-fix2/shots/view-fix2-npc1-1-inst2.png`) while
`DialogueSequenceText."Text"` still held the Bored Knight preview. That is what made the
"read the text and send it" plan need care rather than being trivially right: the text has to
be taken from the block **the live widget owns**, not from the first block with that name
(the class's design-time tree holds a twin, and a same-named object is found first).

**(iii) A third failure was hiding behind those two: the wire never arrived.** Run 2's B
machine logged no `partner is showing` at all. The relay log said why, every sample:

```
[qd-relay] malformed datagram from 127.0.0.1:63649 (kind 0x50):
           dialogue_view has 4 byte(s) left over after decoding
```

`decode_body` requires the body to be consumed exactly (`protocol.rs`), so the **stale release
relay** rejected every widened 0x50 datagram. Rebuilt
(`cargo build --release -p qd-relay`).

### The fix

The cursor still identifies the moment (Round 1's design, unchanged). The **text now travels
with it**, because the talker's box is already rendering exactly the right characters,
including the game's own format-key pass (`ApplyFormatKeysToDialogue`) and its `bHideName`
blanking — neither of which a receiver can reproduce from the asset through reflection.

| File | Function | What it does |
|---|---|---|
| `qddlgbox.h` (new) | `ToUtf8` / `FromUtf8` | FText <-> UTF-8 through the engine's own `UKismetTextLibrary::Conv_TextToString` / `Conv_StringToText`. FText is never parsed by hand. |
| `qddlgbox.h` | `Collect` | Every widget the live widget owns: an **Outer scan first** (a `UUserWidget`'s sub-widgets are all outered into it, so this cannot be defeated by a property offset), then `WidgetTree.AllWidgets`, then the two blocks by name **restricted to objects inside the live widget**. |
| `qddlgbox.h` | `FindAll` | Every block of a role, ranked "inside the live widget first, then newest GUObjectArray slot" (`Block.idx` at `UObject+0x0C`). This is what separates the live block from its design-time twin. |
| `qddlgbox.h` | `SetText` | Writes **every** candidate: the widget's own `SetText` when it resolves, else the `Text` property. |
| `qddlgbox.h` | `ShowWholeLine` | The widget's own `StopPrintAndShowText`, so the rich-text block does not print letter by letter on a spectator who is reading along. |
| `qddlgbox.h` | `Cached` | One collection per widget per second — the walk reads GUObjectArray. |
| `qdwire.h` | `DialogueViewBody`, `Write/ReadDialogueView` | `speaker` (96) and `line` (512) appended after `asset`. |
| `proxy.cpp` | `DlgApplyBoxText` (new, ~14736) | Widget's own `ShowDialogueSequenceEntry` (params filled from the cursor), then the published strings, then a **read-back** that logs `exact=`. |
| `proxy.cpp` | `DlgShowWidget` (14838) | Ends with `DlgApplyBoxText(v)`. |
| `proxy.cpp` | `DialogueViewPublish` (15082) | Reads its own box, publishes speaker+line with the cursor, logs `publish text ...` on change. |
| `proxy.cpp` | `OnDialogueView` | Keeps the text with its `seq`, refuses an older one (UDP reorder must not move the box back a line). |
| `proxy.cpp` | `DevDialogueLever` (`qd.dlg box`) | **Read-only** dump of the manager's widget and its blocks. Run 2's version called `DlgFindWidget`, which *created* a second `DialogueScreen_C`; two instances then drew their text on top of each other and made a correct build look broken. |
| `proxy.cpp` | `WorldTeardownReset` | Clears both text records with the board. |
| `protocol.rs` | `DialogueView` + encode/decode | `speaker`, `line`. |
| `wire_vectors.rs`, `doc/vectors.txt`, `test_vectors.cpp` | `dialogue_view` | New golden bytes (the Spirit of Erma line) and a round-trip of both strings. **`g_lines` 512 -> 1024**: at three characters per byte the old buffer truncated the vector and the failure read "body length 166, vector says 164", which looks like an encoder bug. |
| `WIRE.md` | §3.22 | The two new fields and why the text travels. |

### Coverage
* **Every** `FS_DialogueData` conversation (61 assets): the lane keys on whatever asset the
  talker's manager shows.
* **Every** NPC class deriving from `NPC_Base_C` (`FindNpcById`).
* The blocks are found by the game's own names (`DialogueSequenceText`, `NPCName`), so a
  content update that adds a conversation needs nothing. A build that renames the blocks shows
  up in `qd.dlg box`, which lists every widget the live widget owns with its class and text.
* Not covered: the two `UFS_DialogueAction` animated side shows (Sipeha banish/eat) — those are
  actor animations, not dialogue text.

### Proof so far
* Build clean; `native/test/run_tests.bat` **all executables passed** (`test_vectors` 102
  matched / 0 failed); `cargo test -p qd-core` 145 + 5 passed; `cargo test -p qd-relay` 18 passed.
* **Live, run 3** (`mod/tools/session-viewfix2.ps1`, log `_test/viewfix2-run3.log`, report
  `_test/sview-fix2/out/report-view-fix2.txt`): **10 PASS / 7 FAIL**, and the box assertion —
  the one this lane exists for — passed:

  ```
  [2] dialogue view: publish text speaker='Spirit of Erma, Matron of the Deep'
      line='<text>For all my scrying and divination, I did not foresee this betrayal yes.</>'
  [1] dialogue view: box speaker='Spirit of Erma, Matron of the Deep'
      line='<text>For all my scrying and divination, I did not foresee this betrayal yes.</>'
      (exact=1 published=1 lineHow=1 nameHow=1)
  PASS [1] B shows exactly A's speaker and line (line 1) --
       A='Spirit of Erma, Matron of the Deep' / '<text>…betrayal yes.</>';  B= the same
  ```

  The two blocks are also named by the dump on the spectator, with the real text in them:
  `box.w NPCName [CommonRichTextBlock] text='Spirit of Erma, Matron of the Deep'`,
  `box.w DialogueSequenceText [FB_AnimRichTextBlock] text='<text>For all my scrying …</>'`.
* **Screenshots, the same moment, both machines**
  (`G:\Freespace\Queens_Domain\_test\sview-fix2\shots\`):
  `view-fix2-npc1-1-inst1.png` (B, the spectator) and `view-fix2-npc1-1-inst2.png` (A, the
  talker) — both boxes read **"Spirit of Erma, Matron of the Deep"** and the line
  *"For all my scrying and divination, I did not foresee this betrayal yes."* Compare
  `_test/sview-fix/shots/view-fix-during-inst1.png`, the same machine before this change, which
  read "Bored Knight / The locked chest? …".
* Why the other 7 verdicts failed, and why none of them is a box defect:
  * **the advances.** `Fishwife_Ghost_Dialogue` has ONE line: after the press the talker's own
    cursor is unchanged (`dialogue cursor (after the press) talking=1
    sequenceIndex=1 line=1`), so there is no new cursor for the spectator to follow. `dialogue.md`
    Round 2 measured the same thing ("pressing Continue past `sequenceIndex=1` leaves the cursor
    at 1"). The lane's follow-the-advance path is covered by `test_dialogue2.cpp` and by Round
    2's live run; it is not re-proven here because this NPC cannot advance.
  * **the second NPC.** `near:Arjaot` produced no `dev teleport near 'Arjaot'` line this run, so
    no second conversation was started. `near:npc` (the curated `NPC_Base_C` walk) works.
* The regression this run also closed: run 2's screenshots showed the talker's box drawing the
  design-time preview **on top of** the real line. That was this lane's own `qd.dlg` dump lever
  calling `DlgFindWidget`, which creates a second `DialogueScreen_C`. The lever is read-only
  now, and `view-fix2-npc1-1-inst2.png` shows one clean string.

---

## Defect 2 — the black holes in the ground

### What was measured

A controlled comparison, both machines, 30 shots, with the mod's lanes all ON and then all OFF
at the same spot. Black = fraction of pixels with max channel <= 2 (1-in-16 sampling).
Report: `_test/sview-fix2/out/report-view-fix2.txt`.

| phase | inst1 | inst2 |
|---|---|---|
| walked into the area, no teleport (3 shots) | 0-2.0% | 0.1-1.5% |
| placed beside the hub NPC, 5 s | 0.5-1.3% | 1.3-4.8% |
| same spot, +15 s / +30 s | 3.6-3.9% / 0.8-6% | 0.5% / 0.9-2.1% |
| during a dialogue (6 shots) | 0.7-4% | 0.2-3.8% |
| **same spot, every lane OFF** (`StreamUnion=0`) | 0.8% | 0.7% |

Against that, the user's reported frame is **28.5%** — one 1148x522 blob.

### What the evidence says

1. **The mod's union is not holding a level down.** At the spot, the level table
   (`qd.act levels`) reads **15 visible, 29 loaded-but-hidden, 1 not-loaded**, and every level
   the hub's ground needs is `state=visible engineVisible=1` (`140_GR_ShopIsleOnly`, `200_AQ`,
   `310_LIB_exterior`, `400_MAUS_entrance`, `700_SZ_GR`, `780_SZ_Depths`,
   `070_UpperShell`, `000_Shared_Base_Lighting`). Nothing the ground belongs to is hidden, and
   the same picture (0.7-0.8%) is produced with the union switched off entirely.
2. **It is not a capture artefact.** Off-screen PrintWindow, on-screen PrintWindow and
   on-screen `CopyFromScreen` agree on both machines at the same moment
   (`_test/sview-fix2/shots/view-fix2-onscreen-*`). The one 56% screen grab is the two
   overlapping windows, not the game.
3. **It is not "arrive too early".** 5 / 15 / 30 s after arrival are all a few percent.
4. **It is not web-of-trust with walking vs placement.** Both give a few percent.
5. **It tracks the moment, not the machine.** In the SAME run as the 28.5% frame, the same
   machine's own pawn camera a moment later (`view-fix-after-inst1.png`) is **0.3%**.

### The remaining explanation, and what is still open

The 28.5% frame is the **spectator's temporary camera**, placed at the talker's eye behind an
NPC standing on the shore. That camera looks down the beach at a grazing angle, and at that
angle the water plane and the terrain under it fall inside the near plane / back-face region
and render exactly 0,0,0. The hole's boundary in the shot is the shape of a grazing-angle
shoreline, and the blob's top edge is a straight-ish line rising to the right — which is what
the water plane's silhouette looks like from 162 cm up rather than from the 1.6 m eye the
player normally has.

**What is NOT yet proven:** that a real player standing there cannot see it. The decisive
frame is the TALKER's machine while the spectator camera is engaged (its own camera, the same
conversation, a normal eye height at the same place). If that frame is clean while the
spectator's is not, the hole is a property of the temporary camera pose and cannot happen in
normal play. If it is ALSO dirty, the next step is `qd.act levels` on the talker at that exact
pose and a hunt for a level that flips to `loaded-hidden` when the camera looks down — with
`StreamVetoes` as the suspect. Until that frame exists, this section claims only what is
measured above.

**What this rules out, for the user's worry:** the union does not hide the ground at that spot,
the streaming state is identical with the mod off, and the mod's own capture path is not the
cause. The remaining candidate is the temporary spectator camera, which the mod only creates
DURING a spectated conversation — not in ordinary play.

---

## Open items

1. Run 3's result: `dialogue view: box speaker='…' line='…' (exact=1 …)` on the spectator must
   equal the talker's `publish text` for two NPCs and two advances, with a screenshot pair at
   the same instant. That is the acceptance proof for defect 1.
2. The decisive test for defect 2 (the talker's own camera while the spectator camera is up).
3. NPC choice: `near:Fishwife` resolves `PPC_EnemyOnly_FishWife`, an enemy with no dialogue
   (run 1). The curated `near:npc` walk (`NPC_Base_C`) is the right lever, and the lane logs
   the class it actually started (`dialogue: local started with …`).
