# Queen's Domain co-op mod

A two-player online co-op mod for **Queen's Domain** (Unreal Engine 4.27.2,
`WindowsNoEditor`), built on the substrate and the hard-won lessons of the
`dollsMP` co-op mod for *Dolls Nest*.

The analysis behind it lives in [`../analysis/`](../analysis/) (start at
[`../analysis/ANALYSIS.md`](../analysis/ANALYSIS.md)).  This directory is the
implementation.  Read [`doc/IMPLEMENTATION.md`](doc/IMPLEMENTATION.md) for how the
layers fit together and [`doc/API-GAPS.md`](doc/API-GAPS.md) for exactly what is
still unproven.

---

## Status: read this before you install it

This is a **vertical slice**, not a finished mod.  It is written to be honest about
which parts are proven and which are not, because the corpus it was built from
records that "a claim's DIRECTION and a claim's STRENGTH are separate properties".

**Proven, by measurement:**

| Layer | State |
|---|---|
| Build verification (image hash + 34 byte-verified targets, 6 runtime-validated) | **works** — refuses on any mismatch |
| Substrate (`GUObjectArray`, `FName` pool, `GWorld`/`GEngine`, `ProcessEvent`, `Tick`) | **works** — every RVA is in `targets.h`, read out of the shipped image |
| Save container read (`GetNamedSaveData`, direct native call at `0x00E2F6D0`) | **works** |
| Wire format, native and Rust | **works** — 19/19 byte-for-byte vector matches between the two implementations |
| DLL build | **works** — `native\build\winmm.dll`, 226 304 B, 180 forwarded exports, 0 real code |
| Test suite | **209 native assertions + 146 Rust tests, all green** |

**Proven by independent measurement inside this work** (details in
[`doc/IMPLEMENTATION.md`](doc/IMPLEMENTATION.md) §4): the save tooling decoded all 30
properties of `World0.sav` (29 interpreted, `ItemChangedEvent` preserved but not
interpreted), confirming `NamedSaveData` = 11 entries, `SavableActorStates` = 7,
`EnemySaveStates` = 414; and it established that `EnemySaveStates` values are **tagged
streams**, not a fixed struct — so the "157-byte stride" in the analysis is 16-byte key
+ 141-byte stream.

**Proven at load time, on the live game, by the probe:**

The four write-side APIs the analysis could not pin are resolved *at runtime* and
logged with a pass/fail each.  In particular the `NamedSaveData` writer is closed by
performing a **no-op write of the value already present, then reading it back** —
which cannot corrupt progression and does prove the path.  See
[`doc/API-GAPS.md`](doc/API-GAPS.md) items 1-4.

**NOT yet proven live** (each has its own switch in `qdcoop.ini`, default OFF):

- the puppet body spawn (the engine deferred-spawn pair — proof pending, `PuppetSync=0`)
- the level-streaming union (`StreamUnion=0`) — built: vetoes both
  `SetShouldBeVisible` and the byte-verified `SetShouldBeLoaded`, refuses to apply the
  partner's levels while the two machines are in different maps, and makes the partner
  follow a travel to the same destination actor
- enemy sync (`EnemySync=0`) — built: ownership follows the nearer player so an enemy
  fights the player who can actually reach it, driven copies follow the owner's rows,
  montages and status effects ride `ENEMY_DETAIL`, hits and records go back as intents,
  and an enemy the host runs can damage a distant partner
- two-player dialogue (`DialogueSync=0`) — built: per-slot talk state, partner's NPC
  blocked through `NPC_Base`'s own `BlockInteract` property
- the inventory mirror (`InventorySync=0`) — built: the host publishes its inventory
  whole, the peer grants only what it lacks, and the peer's own pickups go back as intents
- one-shot world objects (`ActorStateSync=0`) — built: chests, doors and barricades, host
  absolute set plus the peer's changes as intents
- pausing suppression (`SuppressMenuPause=0`)

The peer's own progression now reaches the host (`FlagSync=1`, an `INTENT kind 1` per
raised `NamedSaveData` key), which is what makes quest steps, barricades and
`CheckpointUnlockCount` shared rather than local.

What is still not synced at all is listed in [`doc/SYNC-GAPS.md`](doc/SYNC-GAPS.md);
what has been *measured* about the game behind those gaps is in
[`doc/SYNC-RESEARCH.md`](doc/SYNC-RESEARCH.md).

Enabling one of those before its proof has passed will most likely work, and will
certainly not be diagnosable if it does not.

> **Not yet run against the live game.**  Everything above that says "works" was
> verified offline: the DLL builds, the target table verifies against the shipped
> image, the encoders agree with the Rust side, and the state machines pass their
> tests.  The first live run is the next step, and the probe output in `qdcoop.log` is
> what to read first.

---

## Layout

```
mod/
  native/
    build.bat            build winmm.dll
    proxy.def            GENERATED: 180 forwarders to the real System32 winmm.dll
    src/
      targets.h          GENERATED: the verified target table (sha256 + expected bytes)
      exports.h          GENERATED: the export names, for the self-check
      fwd_symbols.c      GENERATED: link stubs (the linker needs a symbol per forwarder)
      proxy.cpp          DllMain, target refusal, hooks, the frame pump
      qdlog.h            allocation-free log sink
      qdconfig.h         qdcoop.ini
      qdsubstrate.h      image base, FName, GObjects, ProcessEvent, runtime validation
      qdreflect.h        UFunction lookup, property-layout discovery, reflective calls
      qdsave.h           save object, NamedSaveData read/write, TMap decoding
      qdwrite.h          the oracle-gated save writes (NamedSaveData, AddItemToInventory)
      qdwire.h           the wire format, native side
      qdnet.h            UDP relay transport, reliability, game-thread handoff
      qdmotion.h         snapshot interpolation            (pure, tested)
      qdpuppet.h         puppet lifecycle state machine    (pure, tested)
      qdstream.h         level union guard, map divergence (pure, tested)
      qdtravel.h         who follows whom, and once      (pure, tested)
      qddialogue.h       dialogue lock                     (pure, tested)
      qdflags.h          monotonic flag bank               (pure, tested)
      qdenemy.h          enemy ownership/drive model       (pure, tested)
      qdenemyown.h       ownership by proximity            (pure, tested)
      qdenemydmg.h       when a host-run enemy hits the peer (pure, tested)
      qdpending.h        the peer's outbound flag intents  (pure, tested)
      qdinventory.h      inventory publication and grants  (pure, tested)
      qdinvintent.h      the peer's own pickups, cumulative (pure, tested)
      qdactorstate.h     one-shot world objects            (pure, tested)
    test/                run_tests.bat + one executable per pure module
  third_party/minhook/   vendored, MIT
  rust/                  the canonical wire format + the relay server
  tools/                 generators, save tooling, install script
  doc/                   WIRE.md, IMPLEMENTATION.md, REUSE.md, SYNC-DESIGN.md, API-GAPS.md,
                         SYNC-GAPS.md (what is not synced), SYNC-RESEARCH.md (what the
                         game actually does behind those gaps)
  qdcoop.ini             the shipped default configuration
```

---

## Build

```bat
cd mod
python tools\gen_targets.py                    :: regenerate targets.h from the exe
python tools\gen_proxy_def.py                  :: regenerate proxy.def (already committed)
native\build.bat                               :: -> native\build\winmm.dll
native\test\run_tests.bat                      :: pure-module tests
```

`build.bat` locates MSVC through `vswhere` and refuses to build if any generated
header is missing, rather than emitting a DLL with a stale address table.

**Why the proxy is `winmm.dll` and not `version.dll` or `dxgi.dll`** — measured from
the shipped image's import directory, not assumed:

- `version.dll` is **not imported** by `QueensDomain-Win64-Shipping.exe`, so a
  `version.dll` proxy would never be loaded.  (The previous project shipped one
  because *its* game did import it.)
- `dxgi.dll` **is** imported, but this install carries `vkd3d-proton.cache`, so a
  D3D compatibility layer is or has been in play; shadowing dxgi would break
  rendering in a way that looks like a game bug.
- `winmm.dll` **is** imported, is not a KnownDLL, is graphics-free, and all 180 of
  its exports are forwarded by a generated `.def` — so owning the name breaks
  nothing else in the process.

---

## Install

```powershell
cd mod
.\tools\install.ps1                       # copies winmm.dll + qdcoop.ini next to the exe
.\tools\install.ps1 -Uninstall            # removes exactly those two files
```

Then run a session. `launch.ps1` refuses if the mod is not installed (two vanilla
instances look exactly like "the mod does not work"), runs the save guard once per
instance *before* starting anything, and gives each instance its own `-UserDir`:

```powershell
# 1. start the relay
rust\target\release\qd-relay.exe --bind 127.0.0.1:47800

# 2. start two isolated instances
.\tools\launch.ps1 -Root G:\qdc-test -DryRun     # inspect first
.\tools\launch.ps1 -Root G:\qdc-test
```

`tools\make_test_instance.ps1` seeds an instance from a real save;
`tools\qdsave.py` reads and writes save files under a mandatory `--root`;
`tools\backup.ps1` snapshots with hash verification on both sides.

The installer and launcher never touch the game's saves.  There were no DLLs beside the
shipping exe, so nothing is overwritten.

Then read `<game dir>\qdcoop.log`.  A correct install starts with:

```
qdcMp bootstrap: pid=... base=0x... image=79CCDC6C...
targets: 34 verified, 6 runtime-validated, 0 mismatched
substrate: globals=1 fname=1(GameEngine) gobjects=1 objects=...
======== API probe ========
PROBE save-object: PASS (FS_SaveSlot_C [...])
PROBE param-layout: PASS  PropertyLink=0x... Next=0x... ...
PROBE NamedSaveData-map: PASS  stride=... (cross-checked against the getter)
PROBE writer: PASS  no-op write of N accepted and read back equal
======== probe complete ========
```

If the image hash does not match, the mod **refuses to run** rather than half-work.
That is deliberate: a wrong RVA is an access violation at best and silent corruption
at worst.

---

## Design rules this code obeys

Every one of these is a measured failure from the previous project, not a
preference.  The full list with its evidence is in
[`doc/REUSE.md`](doc/REUSE.md).

1. **Only `UObject` access goes on the game thread.**  Sockets and decoding live on
   their own thread and hand records over through a bounded queue.
2. **State, not events.**  Every synced container is re-sent whole.  A lost or
   duplicated datagram changes nothing.
3. **A value is never lowered.**  Progression is monotonic; a peer's action is an
   *intent* the host validates and echoes.
4. **Idempotence over delivery.**  Exactly-once *effect*, not exactly-once delivery.
5. **`called=yes` is not evidence the call ran.**  Every write is read back.
6. **Validate every handle.**  Reflection can return a valid-looking object of the
   wrong type, and driving it is an access violation `__try` cannot catch.
7. **Never trust a cached handle across a map change.**
8. **Gate on game events, not timers.**
9. **Never destroy a puppet on silence** — freeze at 2 s, hide at 30 s, keep the
   actor valid.
10. **Split across ticks, never fragment.**  Fragmenting an unreliable stream
    measured 92.4 % effective loss.

---

## What is not here yet

- The relay binary is `mod/rust/qd-relay` (see [`rust/README.md`](rust/README.md));
  a packaged `qdcoop-launcher` is not written yet — `tools/install.ps1` plus two
  instances launched with `-UserDir=` is the current test rig.
- Enemy spawner→GUID binding (needed for the peer to apply enemy persistence) is the
  next slice after the B7 puppet proof.
- Authentication: the relay is plaintext in v1.  LAN/direct-IP only; the launcher
  refuses a public relay unless `AllowPlaintextInternet=1`, and prints why.
