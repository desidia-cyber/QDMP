# 13b — Pause / time-dilation surface: every state that stops the world clock

Scope: **every UI and system state** in Queen's Domain that pauses, dilates, or
otherwise stops the world clock — not just dialogue. Question driving it: if two
co-op machines run this game and one player opens a menu, do their clocks diverge?

Binary: `QueensDomain-Win64-Shipping.exe`, UE 4.27.2, ImageBase `0x140000000`.
All addresses are **RVA**. Pak: `QueensDomain-WindowsNoEditor.pak`, 18 585 game
packages, plaintext and uncompressed.

Reproduce everything with:

```
python analysis/re/pause_surface.py            # all sections
python analysis/re/pause_surface.py --exe      # exe only, seconds
```

Raw output behind every claim: `analysis/re/pause_surface_evidence.txt`.

---

## Verdict

**MIXED.** The game does stop its world clock, but only for a small, specific set
of states, and the mechanism is *not* one flag that every menu flips.

Three facts decide it:

1. **Exactly three packages in the entire game call `SetGamePaused`** — the player
   character, the photo-mode pawn, and the weapon-upgrade machine. No menu widget
   does. (Proven, pak import tables.)
2. **No native code calls the pause API at all.** Each pause entry point has
   exactly one direct caller in the executable — its own `exec*` thunk. Every
   real call arrives from blueprint bytecode through `ProcessEvent`. (Proven,
   direct-call index.)
3. **The pause is applied to the caller's own `UWorld`** via
   `UEngine::GetWorldFromContextObject`. There is no networking in this build, so
   nothing replicates it. (Proven, implementation disassembly.)

Consequence for co-op: when player A enters a pausing state, **A's world stops and
B's world keeps running**. The clocks diverge by the wall-clock duration A spends
in that state. Every state marked `pauses = Y` below is a divergence point.

---

## Instruments, and what each one can and cannot see

The single most important methodological point: **the exe is the wrong instrument
for this question, and saying so is part of the answer.**

| # | Instrument | Detection range | Blind to |
|---|---|---|---|
| A | pair table + `.pdata` (`nativepairs.bin`) | resolves a reflected `UFunction` name to its `exec*` thunk and hence to the native implementation | nothing on the native side |
| B | direct-call index (`calls.bin`) | direct `E8`/`E9` transfers inside `.text` | indirect calls, blueprint dispatch |
| C | pak **import tables** | a blueprint binding a *static/native* `UFunction` — an import is created iff the call exists | blueprint→blueprint calls (they are name-resolved virtual calls and create **no** import) |
| D | pak **name tables** | the blueprint→blueprint layer: a virtual call needs the function name present | cannot distinguish "calls X" from "declares X" or "has a property X" |
| E | pak **bytecode object refs** | attributes a native bind to the exact exporting blueprint function: cooked bytecode stores `int32 -(import+1)` | blueprint→blueprint targets (stored as export indices, dense and noisy) |

Instrument C is the one that carries the weight. It is a *positive* instrument: an
import cannot be created by a comment or a variable — only by a real call.

Its converse deserves care: "no import" does **not** mean "no call" when the call
is virtual. That is why D and E exist, and why rows below that rest on D alone are
marked with lower confidence.

---

## Part 1 — The native surface (proven)

### 1.1 Name → thunk → implementation

The pair table has **two copies per name**. One copy points every name at
`0x0119D280`, the generic blueprint-VM dispatcher — it carries no information.
Only the copy whose thunk ends in a real `.pdata` function is usable. That
validation was applied to all 43 reflected names matching
`Pause|TimeDilation|GlobalTime|TickEnabled|TickableWhenPaused|GamePaused|ActorTick`.

| Name | exec thunk | impl | size | direct callers |
|---|---|---|---|---|
| `SetGamePaused` | `02DC3580` | `028F3180` | 0x91 | 1 |
| `IsGamePaused` | `02DC07B0` | `028E8DB0` | 0x2F | 1 |
| `SetGlobalTimeDilation` | `02DC39F0` | `028F3410` | 0x51 | 1 |
| `GetGlobalTimeDilation` | `02DBF0C0` | `028DF810` | 0x45 | 1 |
| `GetWorldDeltaSeconds` | `02DC01B0` | `028E1CE0` | 0x33 | 1 |
| `GetUnpausedTimeSeconds` | `02DBFD80` | `028E1AF0` | 0x33 | 1 |
| `GetRealTimeSeconds` | `02DBFAB0` | `028E0850` | 0x33 | 5 |
| `SetTickableWhenPaused` | `02D590F0` | `02664C00` | — | 1 |
| `SetActorTickEnabled` | `02D58C10` | `026636F0` | 0x3C | 20 |
| `GetActorTimeDilation` | `02D54EA0` | `02652E30` | 0x42 | 7 |

Two rows are deliberately excluded from conclusions:

* `SetComponentTickEnabled` resolves to `011B18A0`, a shared bit-set helper used by
  12 812 thunks. It is not a distinct implementation.
* `IsComponentTickEnabled`, `IsReconstructionPaused`, `ActionPause`, `OnPauseCapture`,
  `PauseARSession` have **no usable copy** — only the VM-dispatcher copies exist.
  For those five names this instrument yields nothing at all.

### 1.2 What `SetGamePaused` actually does — `028F3180`

```
028F3192  mov  rcx, [rip + 0x22b1bef]      ; GEngine (0x04BA4D88)
028F3199  call 02CDEDF0                    ; UEngine::GetWorldFromContextObject
028F31A3  mov  rax, [rax + 0x180]          ; w0 = world + 0x180
028F31AF  mov  rcx, [rax + 0x38]           ; w0 -> array data
028F31B6  movsxd rax, [rax + 0x40]         ; w0 -> array count
028F31CB  mov  r9,  [rcx]                  ; element 0
028F31E5  mov  rax, [r9]                   ; vtable
028F31FD  call qword ptr [rax + 0xA88]     ; virtual slot 0xA88(bool)
```

So: resolve the **context object's own world**, walk a container hanging off it,
and call a virtual on the first element with the boolean.

Proven: the world is the *caller's* world, and the boolean is forwarded to a
single virtual call. Inferred (by contract, not by symbol): the container is the
player-controller list and slot `0xA88` is `APlayerController::SetPause` — that is
what `UGameplayStatics::SetGamePaused` is defined to do, and no other engine
function has this shape. **Nothing replicates the call.**

### 1.3 The clock fields, proven by their getters

| Field | Getter | Instruction |
|---|---|---|
| `UWorld::DeltaTimeSeconds` +`0x5B0` | `GetWorldDeltaSeconds` `028E1CE0` | `028E1CFE  movss xmm0,[rax+0x5B0]` |
| `UWorld::UnpausedTimeSeconds` +`0x5A4` | `GetUnpausedTimeSeconds` `028E1AF0` | `028E1B0E  movss xmm0,[rax+0x5A4]` |
| `UWorld::RealTimeSeconds` +`0x5A8` | `GetRealTimeSeconds` `028E0850` | `028E086E  movss xmm0,[rax+0x5A8]` |
| `AWorldSettings` time dilation +`0x2E8` | `GetGlobalTimeDilation` `028DF810` | `028DF83B  movss xmm0,[rax+0x2E8]` |

`0x02D37250` is `UWorld::GetWorldSettings(bool bCheckStreamingPersistent, bool
bChecked)` — **derived, not symbol-proven**: it is called as `(world, 0, 1)` from
`GetGlobalTimeDilation`, `SetGlobalTimeDilation` and `GetActorTimeDilation`, and
its result carries a float at `+0x2E8` that `GetGlobalTimeDilation` returns
directly. That matches `GetWorldSettings` and identifies `0x2E8` as the *world
settings* dilation, i.e. the global one.

`SetGlobalTimeDilation` `028F3410` fetches world settings and calls the virtual at
`[vtable + 0x658]` with the new float — `AWorldSettings::SetTimeDilation`.

`AActor::GetActorTimeDilation` `02652E30` multiplies world dilation by
`[actor + 0x98]`, which is `AActor::CustomTimeDilation`.

### 1.4 Two different ways to stop the clock — they must not be conflated

| Mechanism | Entry point | Effect |
|---|---|---|
| **Pause** | `SetGamePaused` → `APlayerController::SetPause` → world pause flag | actor ticks suppressed; `DeltaTimeSeconds` driven to 0 |
| **Dilation** | `SetGlobalTimeDilation` → `AWorldSettings::SetTimeDilation(0)` | world still ticks, but scaled by 0 — clock effectively frozen, `RealTimeSeconds` still advances |

Both stop *gameplay* time. Only pause stops the tick. A state that dilates to 0
still runs its own tick logic, which matters for co-op because non-dilated actors
(`bIgnoreTimeDilation`) keep moving.

### 1.5 `IsGamePaused` is not a bare flag read

`028E8DB0` tail-jumps to `029A43C0`, a 138-byte predicate that consults
`GetWorldSettings`, world flag bits `[world+0x10C] & 0x40`,
`UWorld::InternalGetNetMode` (`02D39CF0`, compared to `3` = `NM_Client`),
`[world+0x620]`, and `PauseDelay` at `[world+0x5B4]` vs `[world+0x5A0]`.
It is a compound "is the world paused **or** in a pause-delay / seek-freeze state"
test — not usable as a cheap single-flag hook.

### 1.6 The exe has no game-side native pause callers

`SetGamePaused`, `IsGamePaused`, `SetGlobalTimeDilation`, `GetGlobalTimeDilation`
and `SetTickableWhenPaused` each have **exactly one** direct caller: their own
exec thunk. `Get-`/`Set-` pairs are therefore driven 100 % from blueprint.

**This is the reason the rest of this document is a pak analysis.** Any claim
resting on "xrefs to the pause function" alone would have found only the thunk.

---

## Part 2 — Who binds what (proven, instrument C)

Every package in the pak whose import table names a pause/dilation entry point.
This is a *complete* list for these names — an import exists iff the blueprint
calls the function.

### `SetGamePaused` — 3 packages

| Package | Note |
|---|---|
| `Player/FS_PlayerChar.uasset` | the player character; also imports `IsGamePaused` |
| `UI/PhotoMode/Photomode_Pawn.uasset` | photo mode |
| `Blueprints/UpgradeMachine/WeaponUpgradeMachine.uasset` | the forge / upgrade machine |

### `SetGlobalTimeDilation` — 2 packages

| Package | Note |
|---|---|
| `LD45/Blueprints/Base_LD45_GM.uasset` | the game mode; the caller is the blueprint function **`Calculate and Apply Full TD`** |
| `Blueprints/Utils/ShaderCompileManager.uasset` | loading / shader-compile stall |

### `SetTickableWhenPaused` — 3 packages

| Package | Note |
|---|---|
| `UI/Menus/Journal/Journal_RWM_BP.uasset` | journal render-world manager |
| `UI/Menus/Journal/Journal3DVisualizer.uasset` | journal 3D item preview |
| `LD45/Blueprints/Checkpoint/Checkpoint.uasset` | checkpoint / rest crystal |

This trio matters: an actor asks to keep ticking *because the world is paused while
it is on screen*. It is in-game evidence that the journal and checkpoint screens
are shown against a stopped world — evidence the game itself provides, independent
of any disassembly.

### `IsGamePaused` — 8 packages

`FS_PlayerChar`, `FS_GameInstance`, `Base_LD45_GM`, `ItemMergeAnim_Comp`, and four
typewriter text widgets (`AwakenTextWidget`, `EndingTextWidget`, `RitualText`,
`IntroTextWidget`). The text widgets poll it to pause their own reveal timers —
consumers, not causes.

### `GetGlobalTimeDilation` — 12 packages

Item pickup / give animation components, `SuperCubeAltar`, `Grimoire_UI_Projector`,
`Memento_Pickup_Base`, `TridentKey_02_BP`, `Food_Pedestal_Base`, `JarHammer` and
`TridentKeyTrident` weapon visuals, `Base_LD45_GM`, `FS_CommonGM`,
`WeaponEvoAnimation`. All of these **read** dilation to scale their own animation —
they slow down when the world slows. They do not cause it.

### Negative result that matters most

**Not one of the menu widgets imports any pause entry point.** Checked
individually: `PauseMenu`, `GameMenu`, `GameplayModalMenu`, `OptionsMenu`,
`SaveMenu`, `DieUI`, `ReadTextUI`, `Challenges_Menu`, `DialogueManager`,
`DialogueScreen`, `FS_InspectItem`, `FS_Modal`, `PhotoMode_HUD`, `ShopMenu`,
`WeaponUpgrade_UI`, `KeyUseMenu`, `StatusMenu`, `SaveSlotWidget`.

The widget layer is inert. Pausing is owned entirely by the player character
(plus the two exceptions above). That single fact is what makes the state table
resolvable at all: the question is not "does widget X pause", it is "does the
player's pause path run for state X".

---

## Part 3 — Inside the player character (proven, instrument E)

`FS_PlayerChar` is 770 exports / 1 695 imports. Cooked bytecode stores a native
call as `int32 -(import_index+1)`; the byte immediately before the operand is a
constant `0x68` in every hit, which is a usable structural signature.

Method validated first on two small assets before being trusted:
`Photomode_Pawn` (2 hits, both `-(i+1)`, both in `ExecuteUbergraph_Photomode_Pawn`)
and `WeaponUpgradeMachine` (1 hit in `ExecuteUbergraph_WeaponUpgradeMachine`). The
control test — searching for the *positive* import index — produced scattered
nonsense (`SetUpTravelLimit`), confirming the negative encoding.

### Result

| Reference | uexp offset | owning export |
|---|---|---|
| `SetGamePaused` | `0x00033DF3` | `ExecuteUbergraph_FS_PlayerChar` |
| `SetGamePaused` | `0x000342FC` | `ExecuteUbergraph_FS_PlayerChar` |
| `IsGamePaused` | `0x0003394A` | `ExecuteUbergraph_FS_PlayerChar` |

`ExecuteUbergraph_FS_PlayerChar` spans uexp `[0x000006E7, 0x0003958A)`.
There are **exactly two** `SetGamePaused` sites in the whole package.

### What the two sites are

Decoding import references within ±200 bytes (the ubergraph is one merged blob, so
this is a semantic neighbourhood, not an enclosure proof):

**Site `0x33DF3` — the pause-on path**
```
-159  GetPlayerController
-121  FS_PlayerController
 -24  SetCursorShowableOnWake
  +0  SetGamePaused
 +31  GetPlayerController
 +69  FS_PlayerController
+166  SetCursorShowableOnWake
-228  CacheInputStatePreUIMode
```

**Site `0x342FC` — the pause-off path**
```
-192  FS_PlayerController
 -95  HideCursor
 -71  GetPlayerController
 -58  Default__WidgetBlueprintLibrary
 -41  SetInputMode_GameOnly
  +0  SetGamePaused
 +33  Default__WidgetBlueprintLibrary
 +50  GetAllWidgetsOfClass
 +73  PauseMenu_C
+115  Default__KismetArrayLibrary
+140  Array_Length
+181  Conv_IntToBool
```

The pause-off branch reads, in order: cast to `FS_PlayerController` → hide cursor →
`SetInputMode_GameOnly` → unpause → `GetAllWidgetsOfClass(PauseMenu_C)` → count
them. That is a pause-menu teardown, and it is the strongest single piece of
evidence that the world pause is bound to **that** widget family rather than to
"any menu".

`IsGamePaused` sits next to `IsInUIInputMode`, `IsValid`, `BooleanOR` (three
times) and `DialogueManager_C` — a composite "should gameplay input be suppressed"
predicate.

### The player's UI-mode vocabulary

`FS_PlayerChar` declares, among 605 functions:

`EnterUIMode`, `ExitUIMode`, `OpenPauseMenu`, `OpenSaveMenu`, `ClosedSaveMenu`,
`OpenShopmenu`, `ClosedShopmenu`, `OpenKeymenu`, `KeymenuClosed`, `OpenStatus`,
`TryAutoPause`, `CheckForOverlayPause`, `TogglePhotoMode`, `EnterPhotoMode`,
`Cache Input States`, `Restore Cached Input States`, `IsInModalMenu`,
`IsInReadTextMenu`, `LockInputForLoadingOrCutscene`,
`Set Input Mode to UIOnly with Mouselock Setting`.

Important: `EnterUIMode` (`0x3ACB7`, 143 B) and `ExitUIMode` (`0x3AD46`, 145 B)
are **separate exports and do not contain the pause** — the two `SetGamePaused`
sites are elsewhere in the ubergraph. So "enters UI mode" and "pauses the world"
are two distinct operations, even though they usually happen together.

### Name-table references (instrument D — weaker)

| Name | packages whose name table contains it |
|---|---|
| `EnterUIMode` | IntroSplashRead, DemoEndTrigger, Teleporter, WeaponUpgradeMachine, Grimoire_Logic, FS_PlayerChar |
| `ExitUIMode` | Teleporter, WeaponUpgrade_UI, Grimoire_Logic, ElecPuzzleTerminal, FS_PlayerChar, GameMenu, GameplayModalMenu, PauseMenu |
| `OpenPauseMenu` | FS_PlayerChar, FS_PlayerController_BP |
| `SetPauseTimeDilation` | EnemyBase, Base_LD45_GM, TimeDilationManagement, TimeDilationManager, FS_PlayerChar |
| `IsTimeDilationPaused` | FS_GameInstance, TimeDilationManagement, FS_PlayerChar, ItemMergeAnim_Comp |

`EnterUIMode`/`ExitUIMode` being referenced by Teleporter, Grimoire, and the
upgrade UIs shows the UI-mode pair is generic. Because the pair provably does not
itself pause, those callers are **not** automatically pausing states — but this is
exactly the residual uncertainty flagged in the table.

### What I could not recover, and why

The exact ubergraph **event** owning each `SetGamePaused` call is **not
established**. I attempted the standard reconstruction — each blueprint event
forwards an entry offset into `ExecuteUbergraph_<Class>` as `EX_IntConst` — and it
produced a plausible 166–352 entry map, but:

* the recovered entries cover only `+0x0F .. +0x1BB6C` of a `+0x38EA3` ubergraph;
* the two pause sites (`+0x3370C`, `+0x33C15`) lie outside that range;
* the bytes at the claimed entry offsets do not decode as bytecode.

So the map was **rejected rather than used**. Consequence: "pause menu" below is
inferred from the `PauseMenu_C` neighbourhood plus the declared function names,
not proven by event naming. `analysis/re/pause_surface.py` section G records the
negative result.

---

## Part 4 — Per-state table

Legend: **pauses** = calls `SetGamePaused` on its own world. **dilates** = changes
global or custom time dilation. `*` = inherited: the world is already paused
because the state is reached from the pause hub, not because it pauses itself.

| State | Pauses? | Dilates? | What is written | `UWorld` time stops? | Co-op: A opens, B doesn't → clocks diverge? | Conf. |
|---|---|---|---|---|---|---|
| **Pause menu (Esc)** | **Y** | N | `SetGamePaused` `028F3180` from uexp `0x33DF3` | Y | **Y** — full stop for the whole visit | HIGH |
| **Photo mode** | **Y** | N | `SetGamePaused` imported by `Photomode_Pawn` | Y | **Y** | HIGH |
| **Weapon upgrade machine** | **Y** | N | `SetGamePaused` imported by `WeaponUpgradeMachine` | Y | **Y** | HIGH |
| Options / settings (via pause hub) | Y* | N | inherits pause-menu pause | Y | Y | MEDIUM |
| Challenges / guides (via pause hub) | Y* | N | inherits pause-menu pause | Y | Y | MEDIUM |
| Journal / bestiary / notes | Y* | N | inherits; `Journal_RWM_BP` + `Journal3DVisualizer` set tickable-when-paused | Y | Y | MEDIUM-HIGH |
| Save menu (checkpoint rest) | Y* | N | `Checkpoint` sets tickable-when-paused; `OpenSaveMenu` is player-owned | Y | Y | MEDIUM |
| Key-use menu | Y* | N | `OpenKeymenu` / `KeymenuClosed` player-owned; no own pause | Y | Y | LOW-MEDIUM |
| Shop / vendor | **U** | N | `ShopMenu` imports no pause entry point | ? | ? | LOW |
| Inventory / equipment / status | **U** | N | `StatusMenu`, `EquipDisplay`, `InventoryEntry` import none | ? | ? | LOW |
| Item inspect / merge | N | **Y** | `ItemMergeAnim_Comp`, `MergeActorAnim` use `CustomTimeDilation` | N | **N** | MEDIUM |
| Item pickup / give animation | N | **Y** | `GetGlobalTimeDilation` + `CustomTimeDilation` | N | N | MEDIUM |
| Dialogue | **N** | N | `DialogueManager`/`DialogueScreen` import no pause entry point | N | **N** | MEDIUM |
| Cutscene / LevelSequence | N | N | cutscenes are separate maps (`997/998/999_*Cutscene`), i.e. a level load | N/A | N/A — different map | MEDIUM |
| Death / game over | U | N | `DieUI` imports no pause entry point | ? | ? | LOW |
| Read-text / notes overlay | U | N | `ReadTextUI` imports none; `IsInReadTextMenu` is a player flag | ? | ? | LOW |
| Steam overlay / focus loss | Y | N | `CheckForOverlayPause` + `TryAutoPause`, gated by `ForceNoOverlayPause` | Y | Y | MEDIUM |
| Enemy pause / hit-stop / slow-mo | N | **Y** | `Calculate and Apply Full TD` in `Base_LD45_GM` → `SetGlobalTimeDilation` | N (ticks, scaled) | N — per-world, both machines run it locally | HIGH |
| Shader compile (loading) | N | **Y** | `ShaderCompileManager` → `SetGlobalTimeDilation` | N | N | MEDIUM |
| Cutscene text widgets | N | N | poll `IsGamePaused` to pause their own typewriter timers | — | — | HIGH (as consumers) |

### The three "U" rows

Shop, inventory/status and death screen are **undetermined, not negative**. The
instrument that would settle them is the same one that failed for event
attribution: instrument C proves they do not call the pause API themselves, but
they could still be routed through a player-side path that pauses. I am not
willing to call them either way, because the two mechanisms produce different
co-op behaviour and guessing would hide a real divergence risk.

If it matters before implementation, the cheap empirical test is a live one: open
each state and read `UWorld::DeltaTimeSeconds` (`+0x5B0`) via the existing
substrate (`analysis/re/step11_gw2.py` style). It is a 3-line probe once the
process is attached, and it settles all three at once.

---

## Part 5 — Co-op consequence, stated precisely

Because `SetGamePaused` resolves the **calling object's** world
(`028F3199` → `UEngine::GetWorldFromContextObject`) and this build has no
networking, the pause is a purely local `UWorld` state. Therefore:

* **A pauses, B does not.** A's actor ticks stop; B's continue. A's
  `DeltaTimeSeconds` goes to 0; B's does not.
* Divergence equals A's wall-clock time in the state, not a fixed amount. Any
  host-authoritative design must therefore treat these states as "A is
  time-frozen", and cannot simply replicate actor state during them.
* The time-dilation system is **not** a divergence source in the same way: both
  machines run the same gameplay logic locally, so a hit-stop on A is a local
  effect. It only diverges if the trigger is itself unsynchronised.
* The three HIGH-confidence pausing states are exactly the ones a co-op mod must
  decide about first: forbid them in co-op, make them non-pausing, or accept and
  account for the freeze.

---

## Falsification notes — what would overturn this

1. **"Pauses" could be inherited rather than own.** Rows marked `Y*` assert the
   world is *already* paused because the state is reached from the pause hub. If
   `PauseMenu` were not actually the owner of that pause pair, these rows collapse
   to the same "U" as shop/inventory. The `PauseMenu_C` teardown at uexp
   `0x342FC+73` is the only direct support; it is neighbourhood evidence, not
   enclosure proof.
2. **A fourth pausing path could exist outside `FS_PlayerChar`.** Refuted for the
   native API by instrument C (only 3 importing packages, complete list). It is
   *not* refuted for a blueprint-internal "fake pause" that stops simulation
   without calling the engine — e.g. disabling actor ticks wholesale via
   `SetActorTickEnabled` (20 direct callers, and imported by EnemySpawner,
   ItemPickup_Base, Door_Base, EnemyBase, NPC_Ghost_Base, and others). That would
   be a per-actor stop, not a world-clock stop, but it would still desync. It is
   outside what this pass proved.
3. **`EnterUIMode` might pause after all.** Its 143-byte export body does not
   contain the call, and the call sites are in the ubergraph, so *as written* it
   does not. If the entry-offset reconstruction I rejected were correct, an
   `EnterUIMode` block containing the pause would make Teleporter, Grimoire and
   the upgrade UIs pausing states too. This is the single highest-value thing a
   live `DeltaTimeSeconds` probe would settle.
4. **`IsGamePaused` is a poor probe.** It is not a flag read (`029A43C0`). Any
   future instrumentation built on it will be wrong at the edges — use
   `GetWorldDeltaSeconds` or the raw `+0x5B0` field instead.
5. **`SetComponentTickEnabled` was excluded.** Its resolved "implementation" is a
   shared bit-set helper with 12 812 callers, so any count derived from it is
   meaningless. It is not evidence of anything.

---

## Reproducibility

| Artefact | Contents |
|---|---|
| `analysis/re/pause_surface.py` | re-derives every RVA above and prints sections A–G |
| `analysis/re/pause_surface_evidence.txt` | raw section output: thunk/impl table, implementations, pak scan results, bytecode attribution, export geometry, rejected entry map |
| `analysis/re/_pak_pause_hits.json` | complete package→pause-import map (cache, regenerable with `--refresh`) |
| `analysis/re/_mech_mentions.json` | package→mechanism-name mention map |
| `analysis/re/_player_pause_refs.json` | uexp offsets and neighbourhoods of the three player pause references |

Caches exist only for speed; `python analysis/re/pause_surface.py --refresh`
recomputes them from the pak.
