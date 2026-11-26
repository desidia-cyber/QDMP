# QDMP (Queens Domain co-op mod) — co-op mod and the analysis behind it

Currently ABANDONED because there are already enough coop mods for the game. But if you can find something useful here you can use it. 

Two things live here, and they are meant to be read in this order.

**[`analysis/`](analysis/)** — the reverse-engineering corpus for *Queen's Domain*
(UE 4.27.2, `WindowsNoEditor`): engine identity, the pak format and full asset listing,
the complete save schema, the binary's name surface, the live substrate addresses, the
sync surface, and a gap register that says what is closed and how. Start at
[`analysis/ANALYSIS.md`](analysis/ANALYSIS.md); [`analysis/INDEX.md`](analysis/INDEX.md)
is the map.

**[`mod/`](mod/)** — a two-player online co-op mod built on that analysis and on the
substrate of the earlier `dollsMP` co-op mod. Start at [`mod/README.md`](mod/README.md).
It builds a `winmm.dll` proxy, ships a Rust relay, and documents what is proven versus
what is still a runtime proof.

## Status, stated plainly

The mod is a **vertical slice**. The build verification, the engine substrate, the save
container read, the wire format and the state machines are all tested offline (209
native assertions, 146 Rust tests, and a byte-for-byte cross-check between the two wire
implementations).

The mod has been run against the live game. [`mod/doc/LIVE-TEST-LOG.md`](mod/doc/LIVE-TEST-LOG.md)
records 30 sessions (2026-11-07 to 2026-11-16) on a two-instance test rig: launches and joins,
the `qdcoop.log` probe block, kill-set and world sync, boss fights (one session reached the
ending on both machines), and soaks of up to 30 minutes. Each result is marked in that log as
proven or still open. Write paths that have not passed their live proofs stay switched **off**
in `qdcoop.ini`.

## What is not in this repository

- `/QueensDomain/` — the shipped game build (2.1 GB). Not ours, and not needed: every
  address in `mod/native/src/targets.h` is regenerated from it by
  `mod/tools/gen_targets.py`.
- Build outputs (`mod/rust/target/`, `mod/native/build/`, `mod/native/test/build/`).
- Generated analysis caches (`analysis/re/*.bin`, `analysis/data/*.json`, …) — about
  57 MB of artefacts reproducible from the committed scripts.

See [`.gitignore`](.gitignore) for the exact rules and the reasoning.
