# REUSE — the DollsMP reuse map, implementation-shaped

Source of record: `analysis/03-dollsmp-reuse.md` (213-row matrix, 11 inherited defects,
lessons) plus the actual trees at `G:\Freespace\dollsMP\` and
`G:\Freespace\Queens_Domain\mod\`. This file is the build-facing distillation: one row
per subsystem, what to take, where from, what to change, verdict.

Everything is either **measured** (an artefact exists and is named) or carries an
**evidence tier**. The corpus discipline is carried forward verbatim: `[P]` = proven
from bytes/disassembly, `[I]` = inference with a stated chain, `HIGH/MEDIUM/LOW` as used
in the source documents.

Paths: `DMP` = `G:\Freespace\dollsMP`, `QD` = `G:\Freespace\Queens_Domain`,
`MOD` = `G:\Freespace\Queens_Domain\mod`.

---

## 0. Verdict vocabulary

| Verdict | Means | Equivalent in `03-dollsmp-reuse.md` |
|---|---|---|
| **COPY** | take the file, change only names/paths | AS-IS |
| **RETARGET** | keep the structure, replace constants/tables/game paths | PORT WITH LIGHT EDITS / ADAPT |
| **REWRITE** | the design survives, the code does not | REWRITE |
| **DO NOT PORT** | read it for lessons only; shipping it is harmful | DROP |

The original matrix used five verdicts (AS-IS, PORT WITH LIGHT EDITS, ADAPT, REWRITE,
DROP). Four are enough here because "ADAPT" and "PORT WITH LIGHT EDITS" differ only in
how much prose the port costs, which the "what to change" column already carries.

---

## 1. The README is stale — read this before you copy anything

`DMP/README.md` advertises the **first** architecture: UE4SS + `Mods/DollsMP/Scripts/main.lua`
+ a file bridge, with "in-game puppet rendering … not finished". That is two months out
of date. `DMP/HANDOFF.md` §9 rule 1 states the shipped rule: **"No UE4SS, no Lua.
Everything in-game is in `dxgi.dll`."**

| Claim in `DMP/README.md` / `DMP/mod/native/build.bat` | Reality in the shipped tree | Tier / artefact |
|---|---|---|
| UE4SS + Lua client is the mod | Gen-2 is a native proxy DLL + Rust staticlib; UE4SS is removed from every instance; the launcher **refuses to launch** if `dwmapi.dll` or `UE4SS.dll` is present | [P] `DMP/tools/coop-native.ps1`, `DMP/mod/native/src/proxy.cpp` |
| Puppet rendering not finished | Puppet, world sync, quests, bosses, movers and dialogue were all built | [P] `DMP/HANDOFF.md`, `DMP/mod/native/src/dmp_puppet.h`, `dmp_world.h` |
| Proxy is `dxgi.dll` | The **shipped** name is `version.dll`, forwarded to `version_orig.dll`; the launcher deletes `dxgi.dll` because "an out-of-System32 `dxgi.dll` breaks the Steam overlay" | [P] `DMP/crates/dollsmp-launcher/src/main.rs:630-650`; `DMP/mod/native/src/exports.h` is generated from `System32\version.dll` |
| — | `QD`'s own proxy is already targeted at **`winmm.dll`** (`MOD/native/proxy.def`, `MOD/native/src/exports.h` generated from `System32\winmm.dll`), because the shipping exe imports `winmm.dll` | [P] `MOD/native/proxy.def` header line 2 |

**Consequence for this port.** The valuable asset is the native-module architecture and
its 47 design-record headers, not the Lua client the README advertises. Two proxy-name
decisions are in play and they are **not** the same: DollsMP moved dxgi → version.dll to
save the Steam overlay; QD has chosen winmm.dll from the import table. `analysis/05` §7
recommends `version.dll` for QD — that recommendation predates the `winmm.dll` choice in
`MOD/`, and it is a live inconsistency (see §7).

---

## 2. The reuse matrix

| # | Subsystem | What to take | From (exact) | What to change | Verdict |
|---|---|---|---|---|---|
| 1 | Bit packing | LSB-first bit writer/reader, varint, zig-zag, f32 | `DMP/crates/dollsmp-core/src/bitstream.rs` — **already done** in `MOD/rust/qd-core/src/bitstream.rs` (23 KB, 22 tests) | nothing; it is the same code lineage and already fixes defects 1 and 5-sibling | **COPY** |
| 2 | Reliability | 16-bit seq/ack window, `ReliableTracker`, `RttEstimator` (SRTO = `clamp(srtt+4·jitter,100,2000)`) | `DMP/core/reliability.rs` → `MOD/rust/qd-core/src/reliability.rs` (28 KB, 23 tests) | nothing; explicit "no congestion control" stays true for a 2-player link | **COPY** |
| 3 | Session authority | `Session`, `MAX_PLAYERS = 2`, **first to join = host**, `JoinError`, `reinstate` | `DMP/crates/dollsmp-server/src/lib.rs` | drop `world_seed`/`snapshot_revision` until the save codec exists; keep join-order authority | **RETARGET** |
| 4 | Relay / transport router | UDP router (`Server::bind/handle_datagram/poll`), **`NetImpair` fault injection** (`delay,jitter,loss` re-read every 1 s) | `DMP/crates/dollsmp-server/src/relay.rs` (82 KB) | message tag table → QDCOOP kinds (`WIRE.md` §2); keep `NetImpair` unchanged — it is how smoothness is *proved* | **RETARGET** |
| 5 | Wire framing | 7-byte header + 15 tags, unknown-tag = hard error, at-most-once `accept_event` with a **monotonic** key `(hostEpoch<<24)|hostSeq` | `DMP/crates/dollsmp-server/src/clientproto.rs` | replaced by `WIRE.md`: 16-byte envelope, `QDC1`, kinds 0x01–0x30. `MOD/rust/qd-core/src/protocol.rs` is the implementation | **RETARGET** |
| 6 | Smoothing / interpolation | `smoothing.rs` in full: adaptive buffer (grow immediately, `BUFFER_SHRINK_TAU_S = 0.6`, cap 120 ms/s), rate-nudged playout clock (0.88–1.12), snap rules | `DMP/crates/dollsmp-game/src/smoothing.rs` (~400 lines) | constants only: `render_delay_ms 100`, `max_extrapolation_ms 250`, `buffer_capacity 32`, `teleport_threshold_cm 2000`, drift 100 ppm; **lower the implied-speed guard from 100 000 cm/s** to ~3 000 cm/s (QD is a walking game) [I] | **COPY** |
| 7 | Quantization | `Quantizer`, `PositionBounds`, `RotatorCodec`, `VelocityCodec`; `max_error()` includes one f32 ULP | `DMP/core/quantize.rs` → `MOD/rust/qd-core/src/quantize.rs` | QD's wire already fixes the *units* (1/8 cm position, 1/65536 turn yaw/pitch, 1/8 cm/s velocity saturated to i16 — `WIRE.md` §3.5), so the Dolls ±200 000 cm box and 22-bit/axis defaults are **not** used | **RETARGET** |
| 8 | RNG | PCG-XSH-RR 64/32, Lemire bounded, golden vector test | `DMP/core/rng.rs` → `MOD/rust/qd-core/src/rng.rs` | none. QD needs it only for the shared-seed cases; loot rolls must stay local (§5.5) | **COPY** |
| 9 | Flag-bank semantics | monotonic rev, host-minted, never-lower, intent gated on `based_on_rev`, additive-vs-absolute mode | design only in `DMP/docs/sync/76`, `79` (rules E1b/E2/E4) | QD's version exists: `MOD/rust/qd-core/src/flags.rs` (14 KB, 13 tests) | **RETARGET** |
| 10 | PE / RVA tooling | `peutil.py` (fast RVA↔offset), `native_tables.py`, `fncarry.py`, `fnindex.py`, `refldump.py`, `targets_verify.py` | `DMP/tools/builddiff/` | re-point at `QD/QueensDomain/QueensDomain/Binaries/Win64/QueensDomain-Win64-Shipping.exe`; `targets.h` is already generated by `MOD/tools/gen_targets.py` (sha256 `79CCDC6C…D1E6`, `SizeOfImage 0x05043000`) | **RETARGET** |
| 11 | RE method tools | `native_fn.py` (what a reflected native *really* reads), `kismet_dis.py` (cooked BP bytecode), `uprops.py` (class defaults + placed actors) | `DMP/tools/re/` | none; QD's corpus already has `analysis/re/*.py` in the same shape (`_d4_multiguid_owner.py`, `_challenge_full.py`, `pause_surface.py`) | **COPY** |
| 12 | Proxy generator | `gen_proxy_def.py` → `.def` + `exports.h` + `fwd_symbols.c`, with the System32-landing check per forwarder | `DMP/tools/gen_proxy_def.py` → `MOD/tools/gen_proxy_def.py` (7.8 KB) | already retargeted: source module is `System32\winmm.dll`, 180 named exports | **COPY** |
| 13 | Launcher core | `CreateProcessW` + job object, monitor enumeration/DPI, SHA-256 | `DMP/crates/dollsmp-launcher/src/{win,monitors,sha}.rs` | exe path, save paths, ini key names. Keep the rule at `win.rs:167`: helpers go in the kill-on-close job, **the game does not** | **RETARGET** |
| 14 | Launcher folder-build flow | junction `Engine`+`Content`, copy exe, copy `Plugins` minus `crashpad_handler.exe`, `steam_appid.txt` when a `.bind` section exists, place proxy + `_orig`, write ini, seed the co-op save from a **copy**, fingerprint the real save before/after | `DMP/crates/dollsmp-launcher/src/main.rs:593-704` | every identifier is Dolls' — but the *sequence* is right and was learned the hard way | **RETARGET** |
| 15 | Launcher GUI | egui settings shell: comment-preserving ini editor, debounced autosave, key-conflict detection, log tail, launch/stop | `DMP/crates/dollsmp-launcher-gui` | regenerate every page from QD's `qdconfig.h`; `rules_table.rs` is 64 Dolls gameplay rules and is **content, not code** | **RETARGET** |
| 16 | Steam tunnel | `ISteamNetworkingMessages` UDP tunnel (~995 lines, zero crates, `GetProcAddress` with v023/22/21/20 fallbacks), lobby discovery by password hash | `DMP/crates/dollsmp-launcher/src/steam.rs` | **QD must not depend on it**: `steam_api64.dll` lives only under `Engine\Binaries\ThirdParty\Steamworks\Steamv163\Win64`, not next to the exe, and `analysis/07-verification.md` Check 2 measured the game running with it unreachable. Keep the tunnel as an *optional* transport, never the only one | **RETARGET (optional)** |
| 17 | Steam mock | mock `steam_api64.dll` + `test_tunnel.py` — tests the tunnel with no Steam and no second account | `DMP/tools/steam_mock/` | none; QD ships Steamworks v1.63 so the mock must be built against that ABI | **COPY** |
| 18 | Backup script | content-addressed snapshots, dedup, restore-takes-a-safety-snapshot, and the deliberate rule **the 10 GB pak is not backed up** | `DMP/tools/backup.ps1` | QD's pak is 1.98 GB; keep the policy, change the paths and the game list | **RETARGET** |
| 19 | Instance scripts | `make-test-instance.ps1` (junction Content/Engine, hard-link the exe), `launch-instances.ps1` (`-UserDir` is the real lever), `seed-saves.ps1` | `DMP/tools/` | QD measured the same lever and a **trap**: `-saveddirsuffix=qd1` redirects `Saved\`→`Saved_qd1\` and hides a seeded save, so drop the suffix (`analysis/16-live-pass.md` §0). No single-instance mutex is enforced (`analysis/10` E5) | **RETARGET** |
| 20 | Typecheck | `clang --target=x86_64-pc-windows-msvc` against stub headers | `DMP/tools/typecheck/` | none; directly reusable for a Windows-only DLL on a non-Windows host | **COPY** |
| 21 | Packaging | launcher exe + relay + sidecar + `mod\version.dll` + ini template + README + "share logs", built with the **static CRT** (`+crt-static`) | `DMP/tools/coop-package/`, `build-coop-package.ps1` | drop the sidecar if the DLL owns the socket (QD does: `MOD/native/src/qdnet.h`); replace the DLL name with `winmm.dll` | **RETARGET** |
| 22 | Bot / driver | `dmpbot.py` command-file driver (launch/state/snap/diff/savebackup/net/kill) + snapshot comparison | `DMP/tools/bot/dmpbot.py` | keep the driver; all ~40 `scen_*.py` are Dolls content and must be rewritten from `SYNC-DESIGN.md` | **RETARGET** |
| 23 | Settings / ini | one switch per subsystem, defaults-on-read, reported bad values, `[Rules]`/`[Local]` split | `DMP/mod/native/src/dmp_settings.h` + `dmp_settings_io.h` | already re-shaped as `MOD/native/src/qdconfig.h` (subsystem switches default **off** for anything that writes) | **RETARGET** |
| 24 | Save codec | UE4 `GVAS` + tagged-property codec, map/array readers, self-test | `DMP/tools/save-editor/dnsav.py` (75 KB) | rewrite the vocabulary; **build the root-path guard first** (`dnsaveedit.py --root X` once ignored `--root` and edited the owner's real save). QD already has `MOD/tools/qdsave.py` (52 KB, self-testing) | **REWRITE** |
| 25 | Lua client | 7 699-line `main.lua` + 30 probe mods | `DMP/mod/lua/` | **do not ship** — two writers on one file and two dolls for one peer. Its comments are the best single source of lessons (§C.1, C.3, C.4) | **DO NOT PORT** |
| 26 | `mod/src` | `dllmain.cpp` (799 lines), UE4SS C++ mod sketch | `DMP/mod/src/dllmain.cpp` | never shipped: it "needs UE4SS's generated import library to build". Useful as a spec, not as code. Its `update_puppet()` ends with "the puppet actor itself is spawned and moved in the next iteration" | **DO NOT PORT** |
| 27 | `mod/cpp` | `dollsmp_net.cpp` (23.6 KB) — a `dollsmp_net.dll` exposing `dmp_open/close/send_state/…` to Lua | `DMP/mod/cpp/dollsmp_net.cpp` | the C-ABI-over-UDP idea is now done properly in `crates/dollsmp-ffi`; QD does the same job in `qdnet.h` | **DO NOT PORT** |
| 28 | Native proxy body | 74 181 lines, 47 headers, 29 targets, byte-verified RVAs | `DMP/mod/native/src/` | **100 % Dolls'**: every address is one image and every offset is `ADA*`. What transfers is the architecture (§3.15) and ~15 pure/tested headers | **REWRITE** |
| 29 | `dmp_game.h` pattern | "the game's object model as arithmetic we can test" — offsets from a generated dump, never by name | `DMP/mod/native/src/dmp_game.h` | QD's equivalent is `MOD/native/src/qdsubstrate.h` + `targets.h` | **RETARGET (pattern)** |
| 30 | FFI discipline | no panic across the boundary, no allocation crossing, ABI version, a self-test that **can fail** | `DMP/crates/dollsmp-ffi/src/lib.rs` (ABI + self-test) | `MOD/native/src/qdwire.h` + `qd-core` vectors take the same role; keep the "a bit that can fail" property | **COPY (discipline)** |

---

## 3. Subsystem detail — the parts where the detail is the value

### 3.1 Transport + relay — RETARGET

Take `relay.rs` and `liveness.rs` whole. Neither knows about Dolls Nest. The relay is
"a router, not a protocol": plain `send_to`, no seq, no ack; reliability lives above
(repeated state, idempotent events). Fifteen tags, unknown tag = hard error. Session
liveness: `HELLO_REFRESH_MS 1000`, `SESSION_STALE_MS 6000`, `READY_TIMEOUT_MS 45 000`,
`REAP_MS 60 000`, `RECONNECT_GRACE_MS 120 000`, `SETTLE_MS 500`, asserted strictly
increasing because "the 45 s ready-timeout must sit *inside* the 60 s reap". `StallClock`
**suspends** deadlines over bracketed stalls and never extends them.

Two things must change:

1. **The sidecar question is already answered for QD.** DollsMP's sidecar exists only
   because UE4SS's Lua had no socket. QD's proxy is native and `MOD/native/src/qdnet.h`
   owns the socket directly (own thread, bounded inbound queue of 64, drained by the tick
   hook). Do **not** port `dollsmp-sidecar` or the file bridge.
2. **Steam is not the transport.** `analysis/07-verification.md` Check 2 measured the
   game running with `steam_api64.dll` unreachable. UDP relay is the design; the Steam
   tunnel is optional.

`NetImpair { delay_ms, jitter_ms, loss_per_10k }`, re-read every second from
`run/net.txt`, is the reason DollsMP can claim "20 % loss, 40 ms jitter → 67 cm mean
error". Lift it unchanged; it is how `SYNC-DESIGN.md`'s acceptance tests get a fault
axis instead of a happy path.

### 3.2 Reliability — COPY

`reliability.rs` is pure arithmetic with no game or transport knowledge, and it already
carries fixes for DollsMP defects 2, 3 and 4 (§4). Keep the deliberate absence of
congestion control — correct for two players, and it must be re-affirmed in QD's design
note rather than silently inherited.

The live DollsMP wire had **no** reliability layer: `DMPC` is a router, and the reliable
core (`DMP1`, 18-byte header + ack/seq/RTT/delta) was "built and unit-tested, **not on
the live path**". QD's `WIRE.md` makes the same choice deliberately: apart from the
`HELLO`/`WELCOME` handshake, `INTENT` is the only reliable kind, and everything else is
absolute state re-sent (`WIRE.md` §2, §4). Do not "restore" the DMP1 layer as a shipping
dependency; it is unproven on a live path.

### 3.3 Session authority — RETARGET

The reusable part is the model, not the struct: **the server owns session facts, the host
owns simulation.** With `MAX_PLAYERS = 2` and join order as the canonical authority, this
is exactly what QD needs. `Session::accept_event(key)` keeps a `BTreeSet<u64>`, rejects
duplicates, and bounds history at 4096 by evicting the **smallest** key — sound only
under a monotonic key contract `(hostEpoch << 24) | hostSeq`, **never a hash**. A test
pins the failure mode of a non-monotonic key. Keep the test, keep the contract; `WIRE.md`
§4 turns it into `INTENT` deduplicated by `(slot, seq)`.

`reopen_barrier()` clears readiness but never `started`, so a stale `ClientReady` cannot
re-close the barrier — that is the map-travel path. QD needs this verbatim (§SYNC-DESIGN
"travel" and "level union").

### 3.4 Smoothing / interpolation — COPY

`smoothing.rs` (~400 lines, no engine) is the best-reasoned file in the DollsMP repo and
the only part of `dollsmp-game` that ports essentially unchanged. The five claims it
encodes, each of which was a measured failure:

- interpolation needs a sample on **either side** of render time, so the peer is drawn in
  the past; extrapolating from "now" turns every late packet into a visible correction;
- the buffer must be **adaptive** — grown immediately, shrunk with a time constant
  (`BUFFER_SHRINK_TAU_S = 0.6`, capped at 120 ms/s); a fixed 15 ms/s release made a
  single 60 ms spike cost 6.5 s of needless latency;
- a **rate-nudged playout clock** (0.88–1.12) beats recomputing "now − buffer";
- **lead-and-chase overshoots every turn**; snapshot interpolation does not — do not
  "simplify" to chase-the-newest;
- whole-millisecond stamps cost 3 cm at 30 m/s → keep a sub-millisecond byte in the
  sample. `WIRE.md` §3.5 carries `sample_ms` as `u32`; the sub-ms correction is not in
  v1 and is a known regression against the Dolls lesson (LOW risk at walking speed).

### 3.5 Quantization — RETARGET

Code is parameterised and UE-correct (cm, `FRotator` ranges); only the defaults are
Dolls-shaped. QD's wire has already chosen different units (`WIRE.md` §3.5): i32 position
in 1/8 cm (268 m range), i16 yaw/pitch in 1/65536 turn, i16 velocity in 1/8 cm/s. So the
Dolls `±200 000 cm` box and 22 bits/axis are **not** reused. What is reused is
`max_error()` including one f32 ULP, and the discipline of deriving bounds from the
game's own world scale rather than copying a number.

### 3.6 PE / RVA tooling — RETARGET

`tools/builddiff/` is the highest-value toolchain in the DollsMP repo for a new game,
because every native mod starts by finding functions in a stripped 79 MB exe. QD has
already re-derived the critical subset:

| DollsMP tool | QD equivalent | Status |
|---|---|---|
| `peutil.py` (RVA↔offset, fast) | `MOD/tools/pe_imports.py` (17 KB), `pe_export_table.py` | present |
| targets table generator | `MOD/tools/gen_targets.py` (11 KB) → `MOD/native/src/targets.h` | present |
| `targets_verify.py` + image hash | `analysis/re/build_guard.py` (PASS on the shipped image, `--selftest` flips one byte → REFUSE) | present |
| `native_tables.py` (vtable/pointer-table scan) | `analysis/re/build_pairs.py` → `nativepairs.bin` (803 tables / 7,483 names) | present |
| `fncarry.py` (carry a target across builds) | not built; `analysis/re/xbuild_match.py` + `xbuild_match.json` is the seed | gap |

QD's `targets.h` currently carries 19 targets (14 byte-verified, 5 runtime-validated,
0 mismatch) — the shipped image is `sha256 79CCDC6C…D1E6`, `SizeOfImage 0x05043000`. The
rule to carry: a mismatch means **a different build, not a typo**, and the mod refuses
rather than half-working.

### 3.7 Proxy generator — COPY

`MOD/tools/gen_proxy_def.py` is the already-retargeted generator: it reads
`C:\Windows\System32\winmm.dll`, emits `proxy.def` (180 named exports), `exports.h`
(the name list) and `fwd_symbols.c` (linker-only stubs). `proxy.def` is a **PE
forwarder** table (`Name = C:\Windows\System32\winmm.Name`), and `fwd_symbols.c` exists
only because `link.exe` resolves the right-hand symbol as a global and raises LNK2001
even though the export it emits has no code of ours behind it. Verified with
`pe_export_table.py`.

Keep the runtime check from `proxy.cpp`: each resolved forwarder must land **back inside
System32**. A failed forwarder otherwise shows up only as a loader failure in some
unrelated module that imports this DLL later.

### 3.8 Launcher — RETARGET

The launcher is the second-largest reusable asset after the tooling, and the parts that
matter are mechanical: junctions, `-UserDir`, the kill-on-close job, save seeding, save
fingerprinting. Port order: `win.rs`/`monitors.rs`/`sha.rs` (COPY-ish), then the folder
build (RETARGET), then the GUI (later; it is polish).

Non-obvious rules to carry, each from a real failure:

- `-UserDir` is the real isolation lever; junctioning `Saved` "WOULD DO NOTHING" because
  `FPaths::ShouldSaveToUserDir()` is true. QD confirmed this and found the
  `-saveddirsuffix` trap (§2 row 19).
- The game must **not** be in the kill-on-close job: "closing the launcher cannot kill
  an unsaved game".
- `Plugins` is copied **minus** `crashpad_handler.exe` (a Sentry handler throws a
  Breakpoint dialog from a non-install folder).
- Fingerprint the real save before/after and write `MAIN-SAVE-CHANGED.txt` on any
  difference. This is a cheap, permanent guard against the worst class of bug in this
  project.
- **Save guard**: no/relative/empty/duplicate `-UserDir`, or one at/above
  `%LOCALAPPDATA%\QueensDomain`, ends the process before any save is opened
  (`dmp_session.h::DmpSaveGuardCheck`, exit `0x5A7E`, reason written to a file, no
  override). Reimplement verbatim.

### 3.9 Typecheck — COPY

`clang --target=x86_64-pc-windows-msvc` against stub headers. It is how a Windows-only
DLL gets syntax-checked on another machine. No changes.

### 3.10 Steam mock — COPY

Mock `steam_api64.dll` + `test_tunnel.py`: it tests the launcher's tunnel with no Steam
and no second account. QD ships Steamworks v1.63, so the mock must match that ABI. Even
though Steam is not QD's transport (§3.1), the mock is how the *optional* path stays
testable without a second license.

### 3.11 Backup and instance scripts — RETARGET

`backup.ps1`: content-addressed snapshots, dedup, restore takes a safety snapshot first,
and the deliberate rule that **the pak is not backed up**. QD's pak is 1 976 294 370 B,
so the same rule saves the same 2 GB per snapshot.

`make-test-instance.ps1` is the single most useful test tool: isolated instance sharing
`Content`/`Engine` by junction, exe hard-linked. QD's measurement adds two facts:
`-UserDir` alone is enough isolation, and `-saveddirsuffix` is an *extra* redirection that
can hide a seeded save. Also: both instances fit — 640×360 profile 5.40 GB WS / 7.28 GB
private combined; 1080p ≈ 3.7 GB private each, against a 63.86 GiB machine (E2 CLOSED).

### 3.12 The Lua client — DO NOT PORT

7 699 lines of `main.lua` plus 30 probe mods. Shipping it is actively harmful: it writes
the same `DollsMP\dmp_out.bin` as the DLL and spawns its own puppet, producing "two
writers on one file and two dolls for one peer". The launcher enforces removal and
refuses to launch if `UE4SS.dll` or `dwmapi.dll` is still present.

Read it, do not ship it, because it records in prose every failure that cost time:
`os.clock()` is CPU time (measured 20.781 s while 7.0 s elapsed, ~2.97×); a missing
property read returns a non-nil ghost with a NULL class pointer whose use is an AV that
`pcall` cannot catch; `IsClass()` on a ghost follows the class pointer and crashes while
`IsValid()` does not; `FindAllOf("StaticMeshActor")` returned 60 046 objects and building
that array crashed the game; `StaticFindObject` with an unloaded path crashes instead of
returning nil; `LoopAsync` is unsafe.

### 3.13 `mod/src` (DollsMP) — DO NOT PORT

`DMP/mod/src/dllmain.cpp`, 799 lines, UE4SS C++ mod sketch. It never shipped and the
header says why: it "needs UE4SS's generated import library to build". It is a faithful
mirror of the old protocol and is useful as a spec. The puppet is not implemented:
`update_puppet()` ends with "the puppet actor itself is spawned and moved in the next
iteration of this mod".

Note: QD's `MOD/` has **no** `src/` or `cpp/` directory. The native half is
`MOD/native/src/` (headers only at present), the Rust half is `MOD/rust/`. Nothing in
this repo needs a DollsMP-style `mod/src` shim.

### 3.14 `mod/cpp` (DollsMP) — DO NOT PORT

`DMP/mod/cpp/dollsmp_net.cpp`, 23.6 KB: a `dollsmp_net.dll` exposing
`dmp_open/close/send_state/send_event/hello/bye` to Lua. Same generation as `mod/lua`.
The C-ABI-over-UDP idea is now done properly: DollsMP in `crates/dollsmp-ffi`, QD in
`MOD/native/src/qdnet.h` (socket on its own thread, decoded datagrams handed to the game
thread through a bounded queue).

### 3.15 The 47 native headers — REWRITE the code, keep the shape

74 181 lines, 100 % Dolls Nest-specific: every address is a hardcoded RVA into one image,
every offset (`ADAMachine +0x1598` module pointers, `AssembleData +0x12C8`,
`bIsDying +0x611`) belongs to that game. Roughly **5 % of the code and ~70 % of the
design** transfers. The headers that are pure decision machines with tests, and therefore
port by replacing offsets and enums:

| Header | What survives |
|---|---|
| `dmp_puppet.h` (~22 KB) | spawn / drive / destroy / decide as a state machine; every "absence" bug DollsMP paid for |
| `dmp_motion.h` | snapshot interpolation, action lane, shot lane |
| `dmp_place.h` | where a joiner is put (beside the host) |
| `dmp_director.h` | which player an enemy fights — directly applicable to any 2-player boss |
| `dmp_dlglock.h` | one-player-per-NPC lock published as state with a seq |
| `dmp_progress_engine.h` | held-player win, reward queue, "a dead player still gets the boss reward" |
| `dmp_settings.h` / `dmp_settings_io.h` | ini-with-defaults + save backups |
| `dmp_world_stream.h` | the union idea, not the code |
| `dmp_hud.h` | partner panel/marker structure |

Architecture to keep, in order of difficulty: byte-verified RVAs + image hash at load;
one game-thread entry point (`T9Tick` equivalent); **all file I/O and all maths off the
game thread**; reflection without UE4SS (object array stride *measured*, `ProcessEvent`
by measurement, parameter buffer "arguments first, return value last" at natural
alignment); bulk scans with one `VirtualQuery` per 64 KiB chunk, never per object.

### 3.16 Save codec — REWRITE

`dnsav.py` (75 KB) is a complete UE4 `SaveGame` container + tagged-property codec for
Dolls Nest's save. The *codec approach* is the reusable idea; the vocabulary and game
tables are not. QD has `MOD/tools/qdsave.py` (52 KB, self-testing) in that role already,
and `analysis/gvas.py` + `probe_save_tree.py` + `probe_save_maps.py` as independent
readers.

Two hard requirements, both from incidents:

1. **The root path is mandatory, echoed, and enforced** — `dnsaveedit.py --root X` once
   ignored `--root` and edited the owner's real save.
2. `Array`/`Map` values begin with **two** int32s (a header int + the element count) and
   there is one extra `hasGuid` byte between the tag fields and the value. Getting either
   wrong desynchronises the stream *silently* (`analysis/05` §4).

### 3.17 The ten per-frame/secondary systems that do NOT port as code

Per-map NPC hostility (`dmp_world_npc.h`), ambient motion, movers, doors, props ledgers,
achievements, gestures, scenes, the in-game test rig half (`dmp_bot.h`, 133 KB),
measurement censuses (`dmp_fxcen.h`, `dmp_viscen.h`, `dmp_ai85.h`). All are Dolls content.
The *question* each answers is reusable; the code is not.

---

## 4. The eleven inherited defects

From `analysis/03-dollsmp-reuse.md` §B.5, with the port status in `MOD/rust/`. Verified
by reading the port, not assumed.

| # | File (DollsMP) | The failure | The fix | Rust port status |
|---|---|---|---|---|
| 1 | `crates/dollsmp-ecs/src/rpc.rs:86-93` + `RpcTable::encode` | `bool` writes one bit; `RpcTable::encode` writes a varint length then `write_bytes`, which rejects a non-byte-aligned position → `NotByteAligned`. No test pushed a `bool`. | byte-align before a length-prefixed payload; pin it with a test that a `bool` survives | **FIXED** — `qd-core/bitstream.rs:10-18,314-317,621` ("the entire fix for item 1", plus "the test DollsMP never had") and `error.rs:18` |
| 2 | `crates/dollsmp-net/src/lib.rs:269` | `tick` evicts aged entries from `AckTracker` but not from `Connection.retransmit`; the packet is neither resent nor abandoned and `pending_reliable()` never drops. | one container holding both bytes and timers, so eviction cannot be half-applied | **FIXED** — `qd-core/reliability.rs:13-23,274,677` (single `ReliableTracker::pending`, regression test) |
| 3 | `crates/dollsmp-net/src/lib.rs:155-159` | `make_header` conflates two meanings of "ack": `have_ack()` is the *peer* having acked *us*, which says nothing about which inbound seq to ack. | derive the outgoing ack field only from the inbound `SequenceBuffer` | **FIXED** — `reliability.rs:164,370,789` (`ack_seq`) |
| 4 | `crates/dollsmp-core/src/desync.rs` | `compare_tick` counts a strike per call, not per tick; comparing the same tick twice double-counts. | key strikes on the tick value; a repeat is a no-op | **FIXED** — `reliability.rs:29,522,814` |
| 5 | `crates/dollsmp-core/src/protocol.rs` (`Packet`) | fields public, `encode` casts `payload.len() as u16` without re-validating `MAX_PAYLOAD` → a hand-built packet bypasses the header check and wraps the length | re-validate on encode | **FIXED** — `qd-core/protocol.rs:15,592,1320` |
| 6 | `crates/dollsmp-core/src/delta.rs:93-99,146-153` | fixed-width field deltas: a one-*bit* change costs the same ≈17 B as a 64-bit word; the mask is an unconditional `u64` and the entity is capped at 64 fields | do not port the delta layer; model each container as one absolute body. WIRE.md §2/§4 already does this | **NOT INHERITED (guard held)** — `WIRE.md` §2 states every `WORLD_FLAGS`/`ACTOR_STATES`/`INVENTORY` body is the complete set; `flags.rs` documents rule E2. Risk returns the moment someone adds a delta lane |
| 7 | `dollsmp-server` + `dollsmp-sidecar/tests/wire_format.rs:84-142` | 8 pre-existing failing tests and a field-offset check asserting 141 vs 145 — carried as "known failures" | fix or delete; never ship a red suite as normal | **NOT INHERITED** — fresh crate, green suite; but the lesson must not be re-learned |
| 8 | `crates/dollsmp-server/src/lib.rs` (`session_token`) | token minted, put in `Welcome`, never verified again — `ClientMsg` has no token field, so the credential is advisory | either verify it per message or stop calling it authentication | **REMAINS** — if `Session` is ported, this comes with it. v1 is LAN-only and `WIRE.md` §6 records the gap |
| 9 | `crates/dollsmp-server/src/relay.rs:742-753` | `handle_client_ready` compares `ClientReady.build_id` against that same client's `Hello` build, not the host's — it can only fire if one client changes build mid-session | compare against the **host's** build id | **REMAINS** — port target for `relay.rs`; QD's `WIRE.md` §3.1 makes `build_hash` a refusal, so the check matters more, not less |
| 10 | `crates/dollsmp-server/src/clientproto.rs` | two `ResyncReason` codes dead on the wire (`JoinError::AlreadyStarted` never produced, `HostNotInWorld` never emitted); `applied_rev`/`loadout_revision` explicitly discarded | delete the dead codes; do not build a protocol on them | **REMAINS** — do not port the enum shape; `WIRE.md` has no `RESYNC` kind and should keep it that way until one is needed |
| 11 | `crates/dollsmp-sim/src/lib.rs:309-322` | the simulator pins `sender_ms == recv_ms`, so it **cannot** demonstrate sender-clock interpolation; the mod's claim rested on `smoothing.rs`'s own test, not the simulator | make the simulator model a sender clock, or stop citing it as evidence | **REMAINS** — no `qd-sim` crate exists yet. If one is built, this is the first test |

`03-dollsmp-reuse.md` §E.2 step 6 says "fix the **five** defects in §B.5". That is not a
contradiction, it is a narrowing: items 1–5 are the code defects the Rust port fixes;
6–11 are design/verification/hygiene items that survive as constraints. The port has
indeed closed 1–5 and inherited 8–11 as soon as the relay/sim crates are added.

**Seventh-defect note (measured this pass).** Defect 6's spirit — "a fixed-width encoding
costs more than the data" — also applies to the map geometry the mod probes. See
`API-GAPS.md` §2: the measured `InventoryContents` element stride is **0x18**, while
`MOD/native/src/qdsave.h`'s `MapProbe` only tries `{12,16}` with value offsets `{8,12}`.
That is a live bug in the probe set, not an inherited DollsMP defect.

---

## 5. Numbers for planning

| Bucket | Lines | Reuse as code |
|---|---|---|
| Rust transport/protocol/session | ~17 000 | ~85 % (edits, not rewrites) |
| Rust game domain (`dollsmp-game`) | ~1 700 | ~15 % (smoothing only) |
| Native proxy | 74 200 | **~5 % code, ~70 % design** |
| Lua client | 18 200 | 0 % code, 100 % lessons |
| Tools | 39 500 | ~60 % |
| **Total** | ~156 000 | **~25–30 % of code; ~80 % of knowledge** |

The reusable part is almost exactly the part that is hard to get right twice: transport,
reliability, session, smoothing, PE tooling, packaging.

---

## 6. Port order (what to do first, given `MOD/` as it stands)

1. `qd-core` is already green on bitstream/reliability/rng/error/quantize/flags/protocol.
   Next: the **golden vectors** (`doc/vectors.txt` is referenced by `protocol.rs` and
   `WIRE.md` but does not exist yet), and the native self-test that consumes them.
2. `relay.rs` → QD's tag table, with defects 8 and 9 fixed **before** the first two-machine
   test.
3. `launcher` core + save guard + folder build; the GUI last.
4. `MOD/native/src/qdsave.h` — the write path is designed and the read oracle exists;
   finish the reflection layer against the measured `0x00E60250` frame (see `API-GAPS.md` §1).
5. `tools/builddiff/` + `tools/re/` + `typecheck/` + `steam_mock/` — these pay for
   themselves on the first RVA hunt.
6. `smoothing.rs` with the retuned constants; then `dmp_puppet.h`'s state machine; then the
   deferred spawn proof (`API-GAPS.md` B7).

---

## 7. Inconsistencies found while writing this

Reported, not resolved silently:

1. **Proxy name.** `DMP/README.md` and `DMP/mod/native/build.bat` say `dxgi.dll`;
   `DMP/crates/dollsmp-launcher/src/main.rs:630-650` and `exports.h` say `version.dll`
   (moved because an out-of-System32 `dxgi.dll` breaks the Steam overlay).
   `analysis/05-primary-evidence.md` §7 recommends `version.dll` for QD, but
   `MOD/native/proxy.def` is generated for **`winmm.dll`**. Three names, one decision
   still to be ratified in writing.
2. **"Five" vs "eleven" defects.** §E.2 step 6 says five; §B.5 lists eleven. Reconciled
   above as code-vs-design, but the source does not say so.
3. **`Adjusted` documentation references.** `qd-core/flags.rs` cites
   `mod/doc/sync-docs-audit.md` §1.2 and `mod/native/src/qdflags.h`; neither file exists
   in the tree. `qd-core/protocol.rs` cites `doc/vectors.txt`; it does not exist yet.
4. **DollsMP's own verification tiers are not uniform.** The corpus distinguishes *built*,
   *unit-checked*, *rig-verified*, *owner-confirmed*, and much late work is only the first
   two (doc 79: "built, unit-checked … rig pass pending"; doc 80: "cases are written but
   were NOT run to a result"). Treat the code as a strong reference implementation and the
   verification claims as tiered.
