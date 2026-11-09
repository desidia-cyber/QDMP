# Save-file tooling and the two-instance test rig

Four tools, one rule: **never write a save you were not explicitly pointed at.**

The co-op mod makes one machine authoritative over the flag bank in
`NamedSaveData`. That needs a save codec, a way to keep two instances from
touching the same file, a way to seed instances, and a way to undo a mistake.
These four tools are that, and nothing else.

```
qdsave.py               GVAS reader/writer for FS_SaveSlot_C (read, flags, apply, diff, selftest)
save_guard.ps1          refuses a save path before an instance is launched
make_test_instance.ps1  creates one isolated instance and prints its launch line
backup.ps1              timestamped, hash-verified backup of a save directory
```

Requirements: Python 3.12 (`python` on PATH, stdlib only) and Windows PowerShell
5.1 or later. Nothing here needs the game to be running.

---

## Why the codec was rebuilt rather than imported

`analysis/gvas.py` proved the container layout and is the reference for the
header and tag rules. It has no writer, and a writer is the point of this work.
Rather than import across directories from a scratch analysis folder, `qdsave.py`
carries the measured layout (with the same traps documented) and adds the two
things the analysis reader does not have:

* an **exact-fit parser** -- every property must consume exactly its declared
  size, and every container exactly its value region, so a desynchronised stream
  is an error rather than plausible garbage;
* a **splice writer** -- only `NamedSaveData` is regenerated; every other byte is
  copied through unchanged.

`selftest` pins both against the three sample saves in
`analysis/save-backup-before-live/`.

---

## qdsave.py

### `--root` is mandatory

Every command that touches a file takes `--root <dir>` and echoes it:

```
$ python qdsave.py flags --root G:\Freespace\Queens_Domain\mod\testgame\inst1\Saved\SaveGames World0.sav
root: G:\Freespace\Queens_Domain\mod\testgame\inst1\Saved\SaveGames
Arjaot_State=1
BrokeBarricade=1
CheckpointUnlockCount=1
...
```

The echoed root is the **resolved** path (junctions and `..` collapsed). Every
path argument is resolved the same way and refused with exit code 2 unless it
lands inside that root:

```
$ python qdsave.py dump --root .\inst1\Saved\SaveGames C:\Windows\System32\drivers\etc\hosts
REFUSED: path escapes --root
  root   : G:\...\inst1\Saved\SaveGames
  arg    : C:\Windows\System32\drivers\etc\hosts
  resolved: C:\Windows\System32\drivers\etc\hosts
```

Exit codes: **2** = refused to run (root or path guard), **1** = the data could
not be processed, **0** = success.

`--no-echo` suppresses the root line for the JSON commands, so
`qdsave.py dump --no-echo ... | jq` works. The root is also a field inside the
dump JSON, so nothing is lost.

### `dump`

Whole save as JSON: header, all top-level properties, per-property types, map
encodings, and an explicit `undecoded` list.

```
python qdsave.py dump --root <R> --no-echo World0.sav > world0.json
```

Measured on the sample `World0.sav` (79,823 bytes, 30 properties):

| Property | Decoded shape | Count |
|---|---|---|
| `NamedSaveData` | `Map<Name,int32>` -- the flag bank | **11** |
| `SavableActorStates` | `Map<FGuid,int32>` (16-byte binary key) | **7** |
| `EnemySaveStates` | `Map<FGuid,{bValid,RespawnTime,RemainingHP}>` | **414** |
| `InventoryContents`, `Equipment` | `Map<Object,int32>` | 1 each |
| `EquipmentSlots` | `Map<int32,Object>` | 1 |
| `WeaponCharges` / `DaggerCharges` | `Map<Object,float>` | 106 / 26 |
| `SavePointsVisited`, `CollectedNotes` | `Array<Object>` | 1 / 5 |
| `NPCsInJournal` | `Set<Object>` | 2 |
| `SaveLevel`, `SavedArea`, `ActiveSuspendSlot`, `CurrentPlayerHealth`, ... | scalars | -- |

Map keys are JSON strings. A `Map<int32,...>` therefore shows `"2000"`, and an
`FGuid` key shows as 32 lowercase hex characters in the order the file stores
them (the same spelling `analysis/gvas.py` produces).

`map_encodings` records how each struct-valued map was decoded, e.g.:

```json
"EnemySaveStates": { "key": "struct16", "value": "struct_stream" }
```

That is the exact-fit decision made explicit rather than hidden.

### `flags`

The co-op flag bank only, sorted, one `key=value` per line. This is the wire
format for host -> peer comparison.

### `apply-flags`

```
python qdsave.py apply-flags --root <R> World0.sav --json flags.json [--dry-run]
```

`flags.json` is `{"Name": 5, ...}`. `dump` output and `{"flags": {...}}` wrappers
are also accepted, so a dump can be fed straight back in.

**The monotonic rule.** Story facts and counters only go up. For an existing
key, a lower value is refused and reported; it is never written. A higher value
is applied, an equal value is a no-op, and a key that does not exist yet is
added. Applying is per key, so one refusal does not block the rest:

```
$ python qdsave.py apply-flags --root <R> World0.sav --json flags.json
root: ...
REFUSED Arjaot_State: monotonic guard would lower 1 -> 0 (not written)
apply   CheckpointUnlockCount: 1 -> 7
same    BrokeBarricade = 1
wrote G:\...\World0.sav (79823 -> 79812 bytes); backup: G:\...\World0.sav.bak
applied=1 added=0 unchanged=1 refused=1
```

Writing is atomic (`temp file` + `os.replace`), and the rebuild is **verified
before** the original is replaced: the new bytes are re-parsed and the flag map
must match exactly, otherwise the write is aborted and the file is untouched.

A `.bak` of the pre-write bytes is created next to the save. It is written only
when it does not already exist, so the first write preserves the original file
for good instead of a later run overwriting that recovery copy.

### `diff`

Per-container added / removed / changed entries between two saves. Only
containers that actually differ are reported; scalar differences go under
`scalars`.

```
python qdsave.py diff --root <R> --no-echo before.sav after.sav
```

```json
{
  "containers": {
    "NamedSaveData": {
      "added":   { "QD_CoopProbe": 3 },
      "removed": {},
      "changed": [ { "key": "CheckpointUnlockCount", "a": 1, "b": 7 } ]
    }
  },
  "scalars": {}
}
```

### `selftest`

```
python qdsave.py selftest [--keep]
```

Runs every command as a subprocess against copies of the three sample saves, in
its own scratch root, and asserts with **exact values** -- never `len(x) >= 1`,
which would pass on a parser that had quietly stopped decoding. It checks:

* exact counts (11 / 7 / 414) and the exact `NamedSaveData` and
  `SavableActorStates` contents, plus all 414 enemy records;
* every other decoded container's exact size and the one undecoded property;
* `dump -> apply-flags -> dump` is a no-op **and** leaves the file
  byte-identical with no `.bak` created;
* the monotonic guard fires on a lowered value while a raised value in the same
  call still lands;
* a new key is added, `--dry-run` writes nothing, `diff` reports exactly the
  expected change;
* the root guard exits 2 for an absolute escape, a `../` escape, a missing
  `--root`, a missing root directory, and a `--json` outside the root;
* malformed input fails without a traceback.

Sample location defaults to `analysis/save-backup-before-live/`; override with
`QDSAVE_SAMPLES`. The scratch root is created beside the script and removed
afterwards (`--keep` preserves it).

---

## save_guard.ps1

Run before launching any instance.

```
powershell -NoProfile -ExecutionPolicy Bypass -File .\save_guard.ps1 `
    -SavePath "G:\Coop\inst1\Saved\SaveGames" `
    -OtherInstancePath "G:\Coop\inst2\Saved\SaveGames" `
    -InstanceRoot "G:\Coop\inst1"
```

It prints every check it performed with the values compared, then `PROCEED`
(exit 0) or `REFUSED` (exit 1). It refuses when the save path is:

* **missing** -- not created, typo, or an unmounted drive. Creating it here would
  hide the mistake, so it is a refusal.
* **relative** -- resolves against whatever the working directory happens to be.
* **a duplicate** -- equal to, containing, or contained by another instance's
  save directory. `-OtherInstancePath` takes a list.
* **at, above, or inside the real profile**
  (`%LOCALAPPDATA%\QueensDomain\Saved\SaveGames`). "Above" matters: a `-UserDir`
  of `...\QueensDomain\Saved` would have the game recreate `SaveGames` beneath
  it, which is the same outcome as pointing at the profile directly.

`-AllowRealProfile` exists for an explicit one-off recovery and prints a warning
when used. A test rig must never need it.

```
  [FAIL] real profile                : C:\Users\...\SaveGames IS the real profile
                                 profile: C:\Users\...\SaveGames
                                 pass -AllowRealProfile only for an explicit recovery

REFUSED: do not launch an instance with this save path.
  - save path IS the real profile
```

## make_test_instance.ps1

```
powershell -NoProfile -ExecutionPolicy Bypass -File .\make_test_instance.ps1 `
    -Name inst1 -Index 1 `
    -SourceSave G:\Freespace\Queens_Domain\analysis\save-backup-before-live
```

Creates `<Root>\inst1\Saved\SaveGames\` (default `Root` = `<repo>\mod\testgame`),
copies the source save files into it, writes `instance.json` (name, index,
source hashes, per-file hashes), runs `save_guard.ps1` against the result, and
prints the exact launch command:

```
& "G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64\QueensDomain-Win64-Shipping.exe" -UserDir="G:\Freespace\Queens_Domain\mod\testgame\inst1"
```

**The source save is only ever read.** It is hashed before the copy, hashed
again after, and the copy is hashed too; a mismatch aborts the run.

`-Index` must be unique under the rig root -- two instances claiming one index
makes every later comparison meaningless, so it is a refusal. `-Force` re-seeds
an existing instance.

`-UserDir` is the lever because UE4 derives the save directory from
`FPaths::ProjectUserDir()`, and saves land in `<UserDir>\Saved\SaveGames`. The
game also accepts **`-saveddirsuffix=<suffix>`**, which renames the `Saved`
directory to `Saved_<suffix>` inside the same user dir. The rig uses `-UserDir`
because it moves config and logs as well as saves.

## backup.ps1

```
powershell -NoProfile -ExecutionPolicy Bypass -File .\backup.ps1 `
    -SaveDir "G:\Coop\inst1\Saved\SaveGames" -Keep 10
powershell -NoProfile -ExecutionPolicy Bypass -File .\backup.ps1 -List
```

Creates `<repo>\mod\backups\<yyyyMMdd-HHmmss>\` with the files and a
`manifest.json`, then **verifies after copying**: the source is hashed before the
copy, re-hashed afterwards, and the copy is hashed. All three must agree. The
source is hashed twice on purpose -- the game writes suspend saves on its own
schedule, so a backup taken while it runs can race a writer, and a single
hash pair would silently accept that.

Verification failure exits 1 and the manifest records `"verified": false`, so a
caller can refuse to launch on a bad backup. `-Keep` prunes the oldest snapshots.

Restoring is a deliberate manual file copy. This script does not overwrite a live
save directory; for undoing a single `qdsave.py apply-flags`, the `.bak` beside
the save is the intended path.

---

## The live-session rig (`sessionN.ps1`) and its read-only levers

Each session is ONE PowerShell process per scenario, because a launched game does not survive
the command that started it.  `session17.ps1` is the current one:
`-Phase smoke|item1|item2|item3|item4|soak`, one boot per phase, each phase in its own instance
root under `_test/`.

Every lever's control file takes a trailing `#<nonce>`, so that "press this again" is
expressible: the needle is the whole string, and the nonce is what makes it new.  Measured the
hard way — `qd.act` and `qd.tp` did not strip one, so a repeat press was resolved as a *name* and
drove a different actor of that class 56 km away, while a repeat teleport was parsed as hex and
silently refused.

The three levers added in Session 17 only READ, which is why they can be pointed at candidates
safely:

| Control | What it does | Why it exists |
|---|---|---|
| `qd.obj` | dumps the whole object inventory: `objkey` for this lane's tracked set, `savkey` for the savables the lane hands to ACTOR_STATES, each with its container value, its own `Opened` bool, `world=`, `dist=`, `at=` and `path=` | the chests and doors this content places are SAVABLES, so a dump of the object lane alone reported "no chests" about a world full of them |
| `qd.flag` | reads one `NamedSaveData` flag (or `all`) out of this machine's bank | `TotalChestsOpened` rides WORLD_FLAGS, so "not double-counted" was unfalsifiable from a log |
| `qd.gate` | asks the game's own `GetInteractInfo` about one actor (`near:<name>` or `near:guid:<8hex>`) and presses nothing | `dev interact` was the only caller of that probe AND it pressed, so choosing a candidate cost a press |
| `qd.tp guid:<8hex>` | stands beside a saved WORLD OBJECT, not only an enemy (Session 17) | `near:<name>` matched a same-named chest in a different level 56 km away |
| `qd.act near:guid:<8hex>` | presses exactly the actor with that saved GUID | same reason; the GUID is the one handle that names one actor |

---

## What this refuses to do

* **Write outside `--root`.** Every read and write is resolved and bounded.
  This is the direct fix for the DollsMP incident where a save editor ignored its
  `--root` and edited the operator's real save.
* **Lower a flag.** The monotonic guard reports a lower value and writes nothing
  for that key. There is no override flag: a lowered story flag is never the
  intended operation, and an accidental one is silent.
* **Overwrite the pristine backup.** `.bak` is created once, on the first write.
* **Write a save it has not re-read.** The rebuilt bytes are parsed and compared
  before `os.replace`.
* **Rewrite properties it did not decode.** Only `NamedSaveData`'s value region
  is regenerated; the other 29 properties, the header, the terminator and the
  trailing `uint32` are copied byte for byte.
* **Allow a duplicate, relative, missing, or real-profile save path** through
  `save_guard.ps1`, without an explicit override for the last one.
* **Modify a seed save.** `make_test_instance.ps1` hashes the source around the
  copy and aborts on any change.
* **Back up into the directory being backed up.**
* **Police the real profile on read.** `qdsave.py` obeys `--root` and nothing
  else; if you point `--root` at the real profile it will operate there. That
  refusal is `save_guard.ps1`'s job, and it is deliberately a separate tool so
  the gate runs before the game, not during a debugging command.
* **Edit the save while the game owns it.** These tools are offline tools. The
  mod itself must let the game write the file, and that is not implemented here.

---

## Save schema status

Decoded fully: `NamedSaveData`, `SavableActorStates`, `EnemySaveStates`,
`InventoryContents`, `Equipment`, `EquipmentSlots`, `WeaponCharges`,
`DaggerCharges`, `SavePointsVisited`, `CollectedNotes`, `NPCsInJournal`,
`SaveLevel`, `SavedArea`, `CurrentPlayerHealth`, `ActiveSuspendSlot`, and the
remaining scalars -- 29 of the 30 properties in `World0.sav`.

Not decoded: **`ItemChangedEvent`**, a `MulticastInlineDelegateProperty` of 141
bytes. It is a transient script delegate, not persisted progression, and it
carries no documented wire layout. `dump` preserves it verbatim and lists it
under `undecoded`; nothing here ever rewrites it.

Also preserved but not interpreted: the 51-entry custom-version table (echoed in
the dump header) and the trailing `uint32 0` after the `None` terminator.
