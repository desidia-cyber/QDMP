# Implementation notes

How the mod is put together, and the decisions that are not obvious from the code.
Companion documents: [`WIRE.md`](WIRE.md) (the byte format), [`REUSE.md`](REUSE.md)
(what came from `dollsMP`), [`SYNC-DESIGN.md`](SYNC-DESIGN.md) (per-system spec),
[`API-GAPS.md`](API-GAPS.md) (what is still unproven).

---

## 1. The load path

```
Windows loader
  -> maps winmm.dll from the exe's directory (the exe statically imports winmm)
     -> DllMain(DLL_PROCESS_ATTACH)
        -> DisableThreadLibraryCalls
        -> CreateThread(BootstrapThread)          [do nothing else under the loader lock]
     -> exe entry point
        -> CRT init -> main -> UE4 boot
BootstrapThread (our own thread)
  -> ConfigLoad, LogOpen
  -> VerifyTargets()          refuse on any byte mismatch
  -> wait for GWorld != null && ObjectCount() > 1000
  -> RuntimeValidate()        prove FName, GObjects, globals
  -> MH_Initialize + hook FEngineLoop::Tick (+ SetGamePaused, StoreEnemySaveState)
  -> Net::Start               UDP relay
  -> SaveInit(world) + RunApiProbe()
  -> g_engineReady = true     the frame pump begins
```

**Why the work moves off `DllMain`:** the loader lock is held there, and the game's
CRT has not initialised. Anything that touches a UObject, opens a socket or writes a
log line belongs on our own thread. This also means a failure in bootstrap is a
missing log line rather than a loader failure in an unrelated module.

**Why it waits for `GWorld` rather than sleeping:** gating on an event, not a timer, is
the rule the previous project paid for when a 5-second post-load timer lost a race
against the level manager.

---

## 2. The six layers

| Layer | Where | Touches the game? | Switch |
|---|---|---|---|
| 0 build verify | `targets.h`, `VerifyTargets()` | reads file bytes only | always on |
| 1 substrate | `qdsubstrate.h` | reads globals | always on |
| 2 probe | `RunApiProbe()` in `proxy.cpp` | reads, plus one **no-op write** | `Enabled` |
| 3 mirror | `MirrorPump()` | reads | `Enabled` |
| 4 flag sync | `HostPushFlags()` / `PeerApplyFlags()` | **writes** | `FlagSync` |
| 5 body | `PuppetPump()` / `SpawnPuppetOnce()` | **writes** | `PuppetSync`, `PlayerBody` |
| 6 world | `StreamPump()`, `TravelPump()`, `CutscenePump()`, `MoverPump()`, `LootPump()`, `SuspendPump()`, `DialoguePump()` | **writes** | `StreamUnion`, `TravelSync`, `CutsceneSync`, `MoverSync`, `LootSync`, `SuspendGuard`, `DialogueSync` |

Layer 2's no-op write is the exception that makes the ordering work: it is a write to
the game's own save container, but it writes back the value that is already there, so
it cannot corrupt progression while still proving the write path.

---

## 3. Why the four "missing" APIs are not RVAs

The analysis listed four write-side APIs it could not pin. Three of them
(`SaveNamedData`, `AddItemToInventory`, `RunActionWithinContext`) turned out to have
**no native implementation at all** — they are Blueprint functions compiled to
bytecode, reached through `UObject::ProcessEvent`. The confirmed evidence:

- `GetNamedSaveData` has a native implementation (`0x00E2F6D0`), because the game calls
  it from C++.
- At `0x00E60250` the game itself does the Blueprint dance: build a param frame
  `{FName@0, bool@8, int32@0xC}`, resolve the UFunction with `UObject::FindFunction`
  (`0x011A0050` = `obj->Class->FindFunctionByName(name, true)`, confirmed from its
  disassembly, which reloads `rcx` from `[obj+0x10]`), then dispatch through
  `[vtable+0x220]`.

So `qdreflect.h` does the same thing generically: look up the UFunction by name, walk
its properties to learn the parameter offsets, pack named arguments, dispatch, then
**read the value back through the native getter**. That last step is the acceptance
test — `called=yes` is not evidence the call ran.

### The property-layout search

`UStruct::PropertyLink` / `FProperty::Offset_Internal` are build-specific and were not
part of the RE pass. Rather than type them from a different 4.27 build, `DiscoverLayout()`
searches bounded candidate offsets and accepts only the combination that reproduces the
**measured** `GetNamedSaveData` frame `{FName@0, bool@8, int32@12}`. If nothing matches,
the write path reports REFUSE and stays off.

That is a real gate, not a formality: a wrong `Offset_Internal` produces plausible
garbage (the corpus's "right number, wrong address" failure mode), and the oracle is a
full three-field signature match rather than a "looks reasonable" check.

---

## 4. Container geometry: what is measured and what is discovered

| Container | How it is found | Confidence |
|---|---|---|
| the save object | `GetActiveBaseSave(WorldContext)` `0x00E2D100`, validated by class name `FS_SaveSlot_C` | HIGH (the game calls it the same way) |
| `NamedSaveData` member offset | property walk of the save class | discovered, then cross-checked |
| `NamedSaveData` map stride | `ProbeMap()` tries {12,16,20,24,32} × value offsets {8,12,16,20} and accepts only a combination where **two or more keys agree with the native getter and none disagree** | discovered, cross-checked |
| `InventoryContents` | `save+0x28`, stride `0x18`, value `+8` | measured by `SYNC-DESIGN.md` from `0x00E2EA40` |
| `EnemySaveStates` | `save+0x270`, values are **tagged streams**, not a fixed struct (16-byte GUID key + 141-byte stream; the "157-byte stride" in the analysis is 16+141) | measured by the save tooling |

The stride probe exists because the corpus's own figures disagree — the "12-byte"
number is the *drop cache key*, not a save container element. A single assumed stride
would be the wrong instrument, so the set is wide and the getter decides.

---

## 5. Corrections made during implementation

Recorded because the corpus's discipline is that a claim's direction and strength are
separate properties, and these are cases where the code changed the claim.

1. **`WIRE.md` §3.6/3.7/3.9 originally had no `produced` count.** The first native
   encoder invented one (and patched it after the fact with a `memcpy` into the
   already-written header). The Rust implementation did not, and neither did the
   spec. The encoder was wrong; the patched-count design was abandoned because a
   count that is patched can disagree with the bytes next to it.

2. **`level_union` field order.** The native encoder wrote `has_names` before
   `bits_len`; the spec and the Rust side put `bits_len` first. A receiver reading the
   wrong order takes the first bitset byte for a flag — a bug that would only appear
   once there were more than 8 levels loaded.

3. **`QuantizePos` overflowed.** `(int32_t)(2147483000.0f + 0.5f)` is undefined
   behaviour because the nearest float to 2147483000 is 2147483648. It produced
   `INT32_MIN` for large inputs — i.e. a teleport across the map instead of a clamp.
   Fixed by computing in `double`. `test_wire.cpp` caught it; the fix is in the file
   with the measurement in the comment.

4. **`QuantizeAngle` wrapped incorrectly.** The explicit wrap mapped 180° to 0°. The
   fix is to *not* wrap: 16-bit truncation is the wrap, and it makes +180° and -180°
   encode identically, which is what interpolation depends on.

5. **The `for /f` + vswhere bug recurred.** The original `build.bat` comment warns that
   `%ProgramFiles(x86)%` contains a `)` that terminates a parenthesised block early.
   The command substitution `for /f ... in (\`"%VSWHERE%" ...\`)` re-opens exactly that
   hazard and degenerated into a bare `vswhere.exe`. Fixed by writing vswhere's output
   to a file and reading that.

6. **`EnemySaveStates` is not a fixed struct.** The save tooling found each record is a
   16-byte GUID key plus a 141-byte *tagged* stream (`bValid`, `RespawnTime`,
   `RemainingHP`) plus `None`. The 157-byte figure is the per-entry total. This matters
   for any code that tries to read the record by struct offset.

---

## 6. Threading and the game-thread rule

- The socket thread only moves bytes. `Net::Ingest` decodes the envelope, handles
  session/ack/keepalive, and pushes everything else onto a bounded queue.
- `HK_Tick` drains up to 16 packets per frame and applies them. **Every UObject touch
  happens here.**
- A full queue is a drop, not a block. A stalled consumer must never stall the socket;
  the state channels repair within a second.
- `QDLOG` is callable from either thread: one `SRWLOCK`, one append handle, a bounded
  stack buffer, no allocation, no CRT varargs.

## 7. Failure behaviour

| Condition | Behaviour |
|---|---|
| image hash / target bytes mismatch | log the target, its RVA and the byte index, then **do not install hooks** |
| FName decoder does not reproduce `GameEngine` | **refuse** — every name lookup would be garbage |
| GObjects liveness probe fails | **refuse** |
| save object not `FS_SaveSlot_C` | log the misidentification, leave reads off |
| property-layout oracle fails | reads still work (native getter), **writes refuse** |
| map stride not confirmed | enumeration off, single-key reads unaffected |
| relay unreachable | local-only mirror mode, logged once |
| peer silent 2 s / 30 s | puppet frozen / hidden, **never destroyed** |

Every row here logs an explicit line, because the corpus records that a probe which
"runs fine" and writes nothing has usually failed silently.

---

## 8. What the test suite actually covers

`native/test/run_tests.bat` builds and runs three executables, 209 assertions:

| executable | assertions | covers |
|---|---|---|
| `test_wire` | 55 | envelope exact bytes and refusals, player row round-trip, quantiser bounds and saturation, writer/reader overflow refusal, flag chunking, intent decode |
| `test_vectors` | 19 | **native encoder vs `doc/vectors.txt` generated by `cargo test -p qd-core`** — the cross-implementation lock |
| `test_state` | 135 | motion (interpolation, teleport snap, extrapolation cap, freeze/hide, angle wrap, clock skew), puppet (no duplicate spawn, backoff deadline, level-change invalidation, never destroy on silence), stream (union, add-only under churn, staleness, overflow), dialogue (mutual exclusion, stale seq, first-answer-wins, death release), flags (monotonicity, stale intent, name-sorted enumeration) |

The suite demonstrably fails: during development it reported 7 failures in
`test_state`, 5 in `test_wire`, and 19 in `test_vectors`, and in every case the code
was wrong rather than the expectation — except three assertions that encoded a
misunderstanding of the design (`host absolute may lower`, `additive +1 cannot lower`,
`30 s` vs `5 s` of silence), which were corrected with the reasoning written into the
test.
