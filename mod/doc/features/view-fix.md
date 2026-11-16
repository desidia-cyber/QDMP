# view-fix — the dialogue spectator camera, and the talker's place on the viewer's screen

Written 2026-11-16 (session `view-fix`, branch `sync-fix`). Module: `mod/native/src/qdview.h`
(new), glue in `mod/native/src/proxy.cpp`, switch `ViewCamera` in `qdconfig.h` / `qdcoop.ini`.
Unit test: `mod/native/test/test_view.cpp` (64 assertions). Live harness:
`mod/tools/session-viewfix.ps1` (`-Mode before|fix`, `-SkipWalk`).

## The two user-visible defects

**A. The spectator camera was inside the NPC.** `DlgSpectatorShow` called
`SetViewTargetWithBlend(npcActor)`. An NPC carries no camera component, so UE falls back to
the actor's own transform: the POV became the NPC's **origin** — inside its mesh, at its
feet, with the player's own control rotation. The dialogue box stayed readable, the NPC was
invisible (a camera inside a mesh sees only back faces, which are culled) and the ground
clipped through the near plane. Evidence: the user's screenshot
`_test/sdlg-spectator/shots/sdlg-spectator-during-inst1.png` (box up, no NPC, terrain
clipping) and the old line in `proxy.cpp:DlgSpectatorShow`.

**B. Parts of the map and the NPC missing on the watching machine.** Three causes, all
measured:

1. **The camera cut happened before the talker's place existed here.** Streaming here is the
   game's own level-list stack (`qdstream.h`), and a dialogue can start the same second the
   talking player enters an area. The old lane blended immediately.
2. **The rig run that produced the shot had `StreamUnion=0`.** That session wrote a minimal
   `qdcoop.ini` with only the dialogue lanes (`switches: ... dialogue=1 dialogueView=1
   stream=0 ...`, `_test/sdlg-spectator/out/report-spectator.txt`), so nothing kept the
   talker's levels resident on the viewer. The shipped ini has `StreamUnion=1`, but the
   spectator lane must not depend on another lane's switch for its own view.
3. **A stale/out-of-order PLAYER row put the camera 3.4 km from the conversation**
   (measured in this session's run 2: `camera -> the talker's eye (4167,-1812,232)` and then
   `camera followed the talker to 567,-587,72 … pitch=83.4` — the shot was of the canopy).

## What was built

### A. A temporary camera at the talker's eye (`qdview.h` + `proxy.cpp`)

* `qdview::Compose(talker, aim, halfHeight)` — eye = the PLAYER row's capsule CENTRE + 0.8 ×
  the capsule half height (90 cm measured ⇒ 72 cm, i.e. 162 cm above the feet); then **80 cm
  back** along the talker→NPC direction, **45 cm** to the talker's right and **15 cm** up;
  aimed at the NPC's head (UE pitch is positive UP). At a 2 m conversation the talker's head
  sits ~19° left of centre, inside a 90° horizontal FOV: the shot is the talker's view, with
  their own body in frame.
* `proxy.cpp:ViewNpcAim` — the head point is **measured**, in this order, each answer bounded
  to what a person can be (20..400 cm above the actor's origin):
  1. `UKismetSystemLibrary.GetActorBounds` (`bOnlyCollidingComponents=true`) — the engine's
     own bounds, needing no knowledge of the Blueprint's component names;
  2. a primitive component's `Bounds` property;
  3. the capsule's half height (+ its relative Z);
  4. a default that depends on whether the **root component is a capsule** (origin = capsule
     centre ⇒ +76) or not (origin = feet ⇒ +160).
  Which source answered, and the height, is logged once per NPC class.
* `proxy.cpp:ViewCamShow` spawns an engine `CameraActor` (fallback `CineCameraActor`) through
  the proven deferred-spawn pair, forces `Mobility=Movable` (read back), and writes location
  AND rotation (`ViewCamSetPose` — `SetActorXform` with pitch filled in).
* `proxy.cpp:ViewCamPump` cuts (`SetViewTargetWithBlend`) when the place is on screen, keeps
  the camera on the talker (>25 cm / >3° moves only, so it does not fight the blend), and
  after the blend back destroys the actor 700 ms later. The world-reset path drops the handle
  without reading it.
* Fallbacks: talker row missing **or not at this conversation** (`qdview::AnchorPlausible`,
  12 m) ⇒ `ComposeFacingNpc` (in front of the NPC, using the NPC's own yaw); no `CameraActor`
  class in the build ⇒ the spectator keeps its own view and the log says so.

### B. The talker's place is made to exist, and the cut waits for it

* `proxy.cpp:ViewCamSetNeed` reads the NPC's `ULevel` (`ActorLevelOf`) and its area list
  (`stream::LevelListForArea`, the pak-read map). An NPC in the persistent level (most of the
  hub) has nothing to stream, and that is logged as such rather than "not visible".
* `proxy.cpp:ViewForceNpcLevel` — staged, cheap lever first: the level's residency/visibility
  on its `ULevelStreaming` object (what the union does for a partner who is elsewhere); if the
  level is still not VISIBLE after 1.5 s, the game's own entry for the area
  (`PushAreaLevelList` → `AddLevelListToStack`). The forced level is registered with
  `g_force.Need` for the whole conversation so the union does not release it mid-view.
* `view::CamState` holds the cut until the engine's own `ULevel::bIsVisible` bit is set
  (bounded: 4 s, then it cuts anyway and says so — a camera that never cuts is worse than one
  that cuts into a hole, and the log distinguishes them).
* The union is made view-aware: `StreamVetoes` and `StreamPump` no longer apply the "both
  players are together, the local game streams for both" rule while the spectator camera is
  engaged (`ViewCamEngaged()`), because the view is not at the pawn.
* `proxy.cpp:ViewAnchorFor` — the talker's place is the NEWEST row (`g_peerPos`), not the
  interpolated one, and only when it is within 12 m of the NPC. A teleport or an out-of-order
  frame therefore cannot anchor the camera to the wrong place.
* The NPC can be missing at SHOW time (its level is not even resident). The pump re-resolves it
  once a second; the camera is spawned and placed the moment it appears.

## Coverage

* Every class deriving from `NPC_Base_C` (the set `FindNpcById` already walks), i.e. every
  dialogue NPC, and every `FS_DialogueData` asset.
* Every level the NPCs live in: the level is read from the actor's own outer chain, so no
  level name is hard-coded.
* Camera class: `CameraActor`, else `CineCameraActor`.
* Not covered: a build with neither camera class (reported, old behaviour kept); a spectator
  whose `PuppetSync=0` sees no partner BODY in frame (the partner body is the puppet lane's
  switch) — the NPC and the world are still framed correctly; the spectator's dialogue BOX
  shows the widget's own placeholder speaker/line (`ShowDialogueSequenceEntry` is not callable
  in this build, a pre-existing limitation of the display lane, visible in the user's own
  screenshot as "Bored Knight") — the camera work here does not change it.

## Proof

* Build: `mod/native/build.bat` clean (the module compiles under the shared `/W3` flags).
* Unit tests: `mod/native/test/run_tests.bat` — whole suite green; `test_view.cpp` **68/68**
  (placement numbers, angle signs and wrap, move tolerance, the anchor bound, the eye-to-eye
  rule for an unmeasured NPC height, the cut bound, the 700 ms destroy hold).
* Live, final run (`-Mode fix`, `-SkipWalk`; report `_test/sview-fix/out/report-view-fix.txt`,
  shots `_test/sview-fix/shots/`): **20 PASS / 0 FAIL**, and the shot is the conversation:
  * `switches: … dialogueView=1 viewCamera=1 stream=1 …`
  * `NPC aim for Fishwife_Ghost_BP_C is 160 cm above the actor origin (from humanoid default …)`
  * `geometry -- camera (567,-587,72) pitch=-4.1 yaw=148.5 talkerRow (505,-519,-19) aim
    (392,-480,140) dist=216 npc Fishwife_Ghost_BP [Fishwife_Ghost_BP_C] hidden=0`
  * `camera -> the talker's eye (567,-587,72) pitch=-4.1 yaw=148.5; npc resolved hidden=0`
  * `dialogue view: update line=1 …`, `HIDE …`, `camera restored (ran=1)`,
    `temporary camera destroyed`; both machines alive, no fault.
  * `_test/sview-fix/shots/view-fix-during-inst1.png` (B, the spectator): the talker's own
    body over the left shoulder, the NPC's spectral form at the crosshair 2.2 m ahead, the
    shore/pots/foliage/water all drawn, no near-plane clipping — against
    `_test/sdlg-spectator/shots/sdlg-spectator-during-inst1.png` (the user's report), where the
    same moment rendered from inside the NPC's mesh: no NPC, terrain through the near plane.
    `view-fix-after-inst1.png`: the same machine's own camera, back on its pawn.
  * The NPC in this content is a spectral creature drawn as a dark translucent form; the
    framing puts it dead centre, but "clearly visible" is limited by the asset's translucency
    (compare `view-fix-during-inst2.png`, the talking machine's own view, which shows the same
    form and clips through terrain even there).
* **Three real bugs were found by the live runs and fixed** (they are why the final numbers
  above are what they are):
  1. `GetActorBounds` with every component counted answered a head **1880 cm** above the NPC's
     origin (a light/FX component), so the camera aimed at the sky → colliding components only
     + a 20..400 cm bound on the answer + a capsule-root default.
  2. The talker's place came from the movement interpolator, which answered the position from
     **before** a rig teleport (an out-of-order row): the camera cut 3.4 km away with
     `pitch=83.4` → the newest PLAYER row (`g_peerPos`) plus the 12 m anchor bound.
  3. Even a plausible row can carry a *guessed* NPC height: the default said "origin at the
     feet" (+160) while this NPC's origin is its capsule centre, so the camera looked 84 cm
     over its head → `view::Compose` now ignores an UNMEASURED height and looks at the
     talker's own eye height (eye to eye).
* Earlier runs 1–2 (same lane, before those fixes) are kept in the same report/log history and
  are what the two "measured" notes above cite.


