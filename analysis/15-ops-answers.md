# 15 — Ops / packaging answers: E5 (single instance), E6 (build guard), E2 (two instances)

Scope: the three items that gate **Phase 5 (shipping)** in `ANALYSIS.md` §8 and are listed
under `10-gaps-register.md` §E. Nothing here launches the game and nothing under
`QueensDomain\` was modified.

Artefacts produced by this pass:

| file | what it is |
|---|---|
| `analysis/re/ops_probe.py` | reproduces every E5 fact from the shipped image (imports → call sites → argument strings → global xrefs) |
| `analysis/re/ops_probe.json` / `.out` | its output on this build |
| `analysis/re/build_guard.py` | the runnable build guard (`PASS`/`REFUSE`, `--selftest`) |
| `analysis/re/build_guard.json` | guard output on the real image |
| `analysis/re/measure_two_instances.ps1` | prepared two-instance memory rig (not run) |

Image under test:

```
QueensDomain-Win64-Shipping.exe
  bytes        81,374,720
  sha256       79CCDC6CF9E1E85512D52D2CB1C3FFA963D400BE1555051A3523C982D181D1E6
  ImageBase    0x140000000
  SizeOfImage  0x5043000
```

Status: **E5 CLOSED (static)** · **E6 CLOSED** · **E2 PARTIAL**

---

## E5 — Is there a single-instance mutex? Does the game enforce it?

### E5.1 Answer

**Yes there is a mutex, named `UnrealEngine4`, created with `bInitialOwner = TRUE`.
No, the game does not enforce a single instance.** It computes a first-instance flag,
stores it, and carries on; the two places that read the flag are a shader-cache cleanup
guard and an ID-naming branch. Neither ends the process.

The name is fixed and machine-global (session-local Win32 namespace, not `Global\`), i.e.
**any two instances on one PC collide on the same object** — and that is exactly what makes
the live test in §E5.5 decisive rather than fiddly.

### E5.2 The sync-object import surface (exhaustive over the import table)

Enumerated from `DIRECTORY_ENTRY_IMPORT` with pefile; call sites found by a raw byte scan of
`.text` for `FF 15` / `FF 25` + `rel32` whose target is the IAT slot (so every indirect call
in the section is seen, not only the ones a recursive disassembler would follow).

| API | IAT thunk | call sites | name passed |
|---|---|---|---|
| `CreateMutexW` | `0x0373B7E8` | **2** — `0x0077D6E0`, `0x00FAF1AE` | **`L"UnrealEngine4"`** and a runtime `FString` |
| `CreateEventW` | `0x0373B8B8` | 9 | **NULL at all 9** (unnamed) |
| `CreateSemaphoreA` | `0x0373B4E0` | 1 | **NULL** |
| `CreateFileMappingW` | `0x0373B440` | 3 | **NULL at all 3** (`lpName` is the 6th argument) |
| `ReleaseMutex` | `0x0373B7E0` | 4 | — |
| `GetLastError` | `0x0373B7D0` | used at the mutex site | — |
| `CloseHandle` | `0x0373B838` | — | — |
| `CreateToolhelp32Snapshot` | `0x0373B918` | 1 | — (process snapshot, not an object) |
| `CreateProcessW` | `0x0373B2F0` | 1 | — |

Not imported at all (checked against the whole table, not a name guess):

```
CreateMutexExW   OpenMutexW      CreateEventExW   OpenEventW
CreateSemaphoreW OpenSemaphoreW  OpenFileMappingW CreateNamedPipeW
FindWindowW      FindWindowExW
```

That matters: the other three classic single-instance mechanisms — **open an existing
mutex**, **find a window by class/title**, **take a named pipe** — are not available to this
binary. Whatever single-instance behaviour exists must go through `CreateMutexW`.

### E5.3 The `UnrealEngine4` mutex, in full

Created inside `FUN_0077D610` (`0x77D610..0x77D846`), at RVA **`0x0077D6E0`**:

```
0077D6AD lea   rdx,[0x0376CAD0]        ; source literal, UTF-16
0077D6C0 ... wcscpy loop into [rsp+0x30]
0077D6D7 mov   ebx, 1                  ; bInitialOwner
0077D6DE xor   ecx, ecx                ; lpMutexAttributes = NULL
0077D6E0 call  qword ptr [CreateMutexW]
0077D6E6 mov   [0x04964EE0], rax       ; handle -> global
0077D6ED test  rax, rax
0077D6F0 je    0x77D737                ; creation failed -> FALSE
0077D6F2 call  qword ptr [GetLastError]
0077D6F8 cmp   eax, 0xB7               ; ERROR_ALREADY_EXISTS
0077D6FD je    0x77D717                ; <-- another instance is running
0077D6FF lea   rdx,[0x0376CAF0]        ; L"NEVERFIRST" command-line switch
0077D709 call  0x00F7A200              ; FParse::Param
0077D712 movzx eax, bl                 ; TRUE  (first instance)
0077D715 jmp   0x77D739
0077D717 mov   rax, [0x04964EE0]
0077D723 mov   rcx, rax
0077D726 call  qword ptr [ReleaseMutex] ; release the ownership it just took
0077D72C mov   qword ptr [0x04964EE0], 0
0077D737 xor   al, al                   ; FALSE (not first instance)
0077D739 mov   [0x0473B768], al         ; first-instance flag
```

Read that path again: on `ERROR_ALREADY_EXISTS` the code **releases the mutex and zeroes the
handle, then falls through into the rest of the function**. There is no return, no
`ExitProcess`, no message box. The duplicate is *noticed*, then ignored.

Two string literals become usable knowledge from this:

| RVA | encoding | text | role |
|---|---|---|---|
| `0x0376CAD0` | UTF-16LE | `UnrealEngine4` | the mutex name |
| `0x0376CAF0` | UTF-16LE | `NEVERFIRST` | command-line override → forces `IsFirstInstance() == FALSE` |
| `0x0376B7E0` | UTF-16LE | `Multiprocess` | read by the *consumer* of the flag (below) |
| `0x039804E0` | UTF-16LE | ` -Multiprocess` | help text |

The handle global is **`0x04964EE0`** and the flag global is **`0x0473B768`**.

### E5.4 Who reads the flag — and what they do

Both globals were resolved from the code above and every reference was re-decoded before
being accepted (`ops_probe.py::xrefs_verified`), because a desynced linear-sweep index entry
can otherwise look like a reference that does not exist.

Handle `0x04964EE0` — 7 references, all inside `FUN_0077D610` and `FUN_00786520`. The
second one is the shutdown path: load handle → `ReleaseMutex` → zero it. Nothing else
touches the mutex.

Flag `0x0473B768` — **one write, two reads**:

**(a) `0x00782D45` in `FUN_0078271E`**

```
00782D45 cmp  byte [0x0473B768], 0
00782D4C je   0x78302D                     ; not first instance -> skip
00782D52 ...  FParse::Param(CmdLine, L"Multiprocess")
00782D68 jne  0x78302D                     ; -Multiprocess -> skip
00782D6E ...  FPlatformProcess::CleanShaderWorkingDirectory   (0x0376B800)
```

i.e. *"only the sole instance may delete the shared shader working directory"*. Both edges
lead to `0x78302D`, which is ordinary logging (`FMsg::Logf` at `0xF4BFF0`). **Not fatal.**
Incidentally this is the strongest circumstantial evidence in the binary: the engine
explicitly anticipates `-Multiprocess` coexistence, and merely declines to let such an
instance wipe a cache another one is using.

**(b) `0x00DA2C98` in `FUN_00DA2B20`** — the start is taken from the 12-byte `CC` pad
before `0xDA2B20` (`push rbp; push rsi; mov rbp,rsp`), because `.pdata` claims this function
begins at `0xDA2C84`, which is mid-body — the documented `.pdata` unreliability from
`09-live-substrate.md` §6, not a different routine.

```
00DA2C98 cmp  byte [0x0473B768], 0
00DA2CA4 jne  0xDA2CF3          ; first instance -> path A
00DA2CA6 test al, al            ; al = FParse::Param(CmdLine, L"StableNullID")
00DA2CA8 jne  0xDA2CF3          ; or the switch -> path A
00DA2CAA ...                    ; path B
```

Both paths converge on `0xE723D0` (a string-format helper) and free two heap buffers. It
chooses how an ID is spelled. **Not fatal.**

**No user-facing wording exists anywhere in the image.** A scan for
`already running`, `another instance`, `multiple instance`, `single instance`,
`only one instance`, `instance of the game` over `.rdata`/`.data` in **both** encodings
(ASCII strings ≥ 6 chars, and UTF-16LE runs ≥ 6 chars — 36,336 wide strings examined)
returns no instance-related message. The only two wide hits are scalability-cvar blurbs
(`…MBP regions…`, `03EBE390` / `03EBE5D0`) that happen to contain a matching phrase, and
`Cannot create GameplayScreenshotInstance - One with the name "%s" already exists!`
(`03902AB0`), which is about screenshots. A refusal path with no message text is not
credible; a refusal path whose only artefact is a `ReleaseMutex` is not one at all.

### E5.5 PROVEN vs UNVERIFIED

**Proven (static, reproducible with `ops_probe.py`):**
- `CreateMutexW` is called twice; the first passes `L"UnrealEngine4"`, `bInitialOwner=TRUE`,
  `NULL` attributes, at `0x0077D6E0`.
- The result is checked against `ERROR_ALREADY_EXISTS` (0xB7) and, when it matches, the
  mutex is released and the handle zeroed — execution continues.
- `IsFirstInstance` is stored at `0x0473B768`; the flag has exactly two readers in the
  whole image and neither terminates the process.
- No `OpenMutexW`, `FindWindowW`, `FindWindowExW`, `CreateNamedPipeW` or
  `OpenFileMappingW` import exists.
- Every other kernel sync object this binary creates (`CreateEventW` ×9,
  `CreateSemaphoreA` ×1, `CreateFileMappingW` ×3) is **unnamed**.
- The second `CreateMutexW` site (`0x00FAF1AE`, `FUN_00FAF0E0`) is a generic
  create-and-wait helper whose name is a run-time `FString` (its single caller
  `0x028BC538` passes the global `FString` at `0x04B86918`, which is zero in the file
  image). It waits with a timeout and closes on failure — a lock, not a guard.

**Unverified (needs the game running):**
- That two processes launched from the same image actually coexist (predicted: yes).
- That no *other* mechanism (file lock, socket bind on a fixed port, a lock file inside
  `Saved`) refuses the second instance. Those are not kernel named objects and this probe
  does not cover them. The live test below settles it in one run.

**Confidence:** HIGH that the mutex exists and is checked; **HIGH** that it does not gate
startup (two consumers, both benign, plus a clean negative control available); MEDIUM that
nothing else refuses a second instance, because that is a different mechanism class.

> A negative must name its instrument: the "no other mechanism" claim is bounded by
> (i) the import-table scan — exhaustive, whole table, so any *named kernel object* API is
> covered; (ii) the ASCII+UTF-16 string scans above; (iii) the verified xref set on the two
> globals. It is **not** bounded for file/port locking, which those three instruments cannot
> see.

### E5.6 Exact live-test procedure — PREPARED, NOT RUN

Instrument: the Win32 named-object table, read from the same session as the game. The name
in the binary has no `Global\` prefix, so the object lives at
`\Sessions\<n>\BaseNamedObjects\UnrealEngine4` and `[System.Threading.Mutex]::OpenExisting`
(the default, session-local namespace) addresses exactly it.

```powershell
# --- 0. baseline -------------------------------------------------------------
Get-Process QueensDomain-Win64-Shipping -ErrorAction SilentlyContinue   # expect: none

# --- 1. instance 1 -----------------------------------------------------------
$bin1 = 'G:\Freespace\Queens_Domain\_live\inst1\QueensDomain\Binaries\Win64'
$p1 = Start-Process -FilePath "$bin1\QueensDomain-Win64-Shipping.exe" `
  -ArgumentList '-UserDir="G:\Freespace\Queens_Domain\_live\inst1saved"',
                '-saveddirsuffix=qd1','-SaveToUserDir','-windowed' `
  -WorkingDirectory $bin1 -PassThru

# --- 2. the object appears, and is OWNED -------------------------------------
$m = $null
for ($i = 0; $i -lt 60 -and -not $m; $i++) {
    try { $m = [System.Threading.Mutex]::OpenExisting('UnrealEngine4') }
    catch { Start-Sleep -Seconds 1 }
}
$m -ne $null          # OBSERVE: True   -> the named mutex exists
$m.WaitOne(0)         # OBSERVE: False  -> the game holds it (bInitialOwner=TRUE)

# --- 3. instance 2, different save dir --------------------------------------
$bin2 = 'G:\Freespace\Queens_Domain\_live\inst2\QueensDomain\Binaries\Win64'
$p2 = Start-Process -FilePath "$bin2\QueensDomain-Win64-Shipping.exe" `
  -ArgumentList '-UserDir="G:\Freespace\Queens_Domain\_live\inst2saved"',
                '-saveddirsuffix=qd2','-SaveToUserDir','-windowed' `
  -WorkingDirectory $bin2 -PassThru

# --- 4. what "not enforced" looks like --------------------------------------
Start-Sleep -Seconds 90
$p1.HasExited                                  # OBSERVE: False
$p2.HasExited                                  # OBSERVE: FALSE  (if True -> enforced)
if ($p2.HasExited) { $p2.ExitCode }            # only meaningful if it exited
(Get-Process -Id $p2.Id -ErrorAction SilentlyContinue).MainWindowHandle  # OBSERVE: nonzero

# --- 5. exactly one named object, still owned -------------------------------
$m.WaitOne(0)                                  # OBSERVE: False (still owned by p1)
handle64.exe -a -p $p1.Id UnrealEngine4        # Sysinternals; EXPECT one hit in p1
handle64.exe -a -p $p2.Id UnrealEngine4        # EXPECT no hit in p2

# --- 6. the mutex must disappear with instance 1 ----------------------------
$p1.CloseMainWindow(); $p1.WaitForExit(60000)
try { [System.Threading.Mutex]::OpenExisting('UnrealEngine4') } catch { 'gone' }  # OBSERVE: gone

# --- 7. DECISIVE single-instance control ------------------------------------
#   -NEVERFIRST forces IsFirstInstance() == FALSE while only ONE instance runs.
#   If the game still boots and plays, the flag provably does not gate startup.
$p3 = Start-Process -FilePath "$bin1\QueensDomain-Win64-Shipping.exe" `
  -ArgumentList '-NEVERFIRST','-UserDir="G:\Freespace\Queens_Domain\_live\inst1saved"',
                '-saveddirsuffix=qd1','-SaveToUserDir','-windowed' `
  -WorkingDirectory $bin1 -PassThru
Start-Sleep -Seconds 120
$p3.HasExited                                  # OBSERVE: False -> not enforced (conclusive)
```

What each observation proves:

| observation | reading |
|---|---|
| step 2: `OpenExisting` succeeds, `WaitOne(0)` is `False` | the mutex is created at startup and owned by the game |
| step 4: `$p2.HasExited -eq $false`, `MainWindowHandle -ne 0` | a second instance runs normally |
| step 5: `handle64` finds the object only in `p1` | the second instance did *not* become the owner — it exercised the `ERROR_ALREADY_EXISTS` path |
| step 6: the object is gone after `p1` exits | one owner, no leak, and the second instance is unaffected |
| step 7: `-NEVERFIRST` boots fine | single-instance detection is computed but unused — **conclusive on its own** |

If step 4 instead shows `$p2` exiting with a nonzero code, expect `-Multiprocess` on the
command line to be the escape hatch (that is what the `0x782D45` consumer implies), and
E5 becomes "enforced, name `UnrealEngine4`, bypass `-Multiprocess`".

Detection range of this procedure: it sees the *named-object* mechanism only. A file lock or
a fixed-port bind would show up as "instance 2 starts but fails to open its world", which is
why step 7 asks whether the game actually reaches gameplay, not merely whether the process
survives.

---

## E6 — Build guard

### E6.1 What it does

`analysis/re/build_guard.py <path-to-exe>` — pure Python, pefile only, no capstone needed
for the guard path.

1. **SHA-256** of the file, and **SizeOfImage** from `OPTIONAL_HEADER`. `SizeOfImage` is
   compared against `QD_IMAGE_SIZE` in `targets.h`; that check is structural and independent
   of any byte signature.
2. **Parses `targets.h`** (`qd_targets[]`, 19 entries, regex over the generated shape; a
   failed parse is counted and refuses, so a silently shortened table cannot look clean).
3. **`hook` / `util` targets:** compare the first 16 file bytes at the RVA.
4. **`data` targets:**
   - non-zero `expect` (the `UObject::ProcessEvent vtable slot` at `0x0376D258`) → compare bytes.
   - all-zero `expect` (`GWorld`, `GEngine`, `FNamePool`, `Blocks[]`, `GUObjectArray`) →
     **never compare zeros**. The guard first asks whether the RVA has any bytes in the file
     at all. In this image all five sit in the `.data` **virtual tail**, past
     `PointerToRawData + SizeOfRawData`, so the file cannot contain them: they are classed
     `bss-runtime` and reported `RUNTIME-VALIDATED` — explicitly *not* counted as verified.
     If one of them ever *were* file-backed with non-zero bytes (the header having gone
     stale), that is a hard failure.
5. **Structure:** every `hook`/`util` target must be a `.pdata` `RUNTIME_FUNCTION` begin, or
   a known direct-call target (taken from `calls.bin`). Anything else is printed as
   `ANOMALY` — a warning, not a refusal, because `09-live-substrate.md` §6 documents that
   only 24.3 % of `.pdata` begins are also call targets here and that some real functions
   have no unwind entry at all.
6. **Verdict** `PASS` / `REFUSE`, exit 0 / 1, and `--json` (default
   `analysis/re/build_guard.json`) with the sha256, size, declared values, per-target status
   and the failure list.

### E6.2 Result on the real image: **PASS**

```
sha256        79CCDC6CF9E1E85512D52D2CB1C3FFA963D400BE1555051A3523C982D181D1E6
SizeOfImage   0x5043000   (81,374,720 bytes on disk)
declared      0x5043000   (targets.h)

targets 19: 14 byte-verified, 5 runtime-validated, 0 zero-in-file, 0 mismatch
WARN    sha256 not pinned in targets.h; the 16-byte checks are the authority
VERDICT: PASS
```

Per-target, all `OK` except the five documented `.bss` globals (`RUNTIME-VALIDATED`) — no
anomalies, so every hook/util target is a `.pdata` begin or a call target. `targets.h` here
carries no `QD_IMAGE_SHA256`, so the guard records the hash instead of pinning it; adding
the define (as DollsMP's generated header does) upgrades the existing `WARN` to a hard check
with no other change.

### E6.3 `--selftest`: the guard can fail

```
baseline on the real image            : PASS
flip 1 byte at 0x011A7AA0 (UObject::ProcessEvent) : REFUSE  <- caught
flip 1 byte at 0x4ED01FF (unguarded)              : PASS    <- correctly ignored
SELFTEST: PASS   (corrupt-target caught=True, unguarded ignored=True)
```

Both directions are asserted, which is the point:

- the corrupted-target copy **must** REFUSE **and** the failure must name the right target
  (`named` is checked, not just the verdict), otherwise a guard that refuses for an
  unrelated reason would look correct;
- the unguarded copy **must** PASS, otherwise "always refuse" would pass the first test.
  The self-test scans the sections for a byte whose RVA is not inside any target's 16-byte
  window and flips that.

The corruption is applied to an in-memory copy of the bytes and fed to the same
`run_guard()` the CLI uses — the file on disk is never touched, and the tested code path is
byte-for-byte the shipping one.

### E6.4 Residual risk — what this guard cannot see

- A rebuild that keeps `SizeOfImage` and every 16-byte signature but moves a **`data`
  global**. The five `.bss` targets are validated at runtime, not here; that is why the
  header calls them `RUNTIME-VALIDATED` and why `dmp_reflection.h`'s rule ("do not use a
  check=0 target until it has been validated live") must be carried over.
- A rebuild that changes only *inside* a function past its first 16 bytes. Byte signatures
  are entry-point signatures by construction.
- A DRM-wrapped/dumped image. DollsMP's `targets_verify.py` handles that case with `.text`
  entropy; this guard does not, and would simply REFUSE — the safe direction.

---

## E2 — Two instances on one PC

### E2.1 What the file system already tells us

Measured read-only in this pass:

| path | files | bytes | what it is |
|---|---|---|---|
| `%LOCALAPPDATA%\QueensDomain\Saved` | 21 | 165,781 | the **real** save — must never be touched by a test instance |
| `_live\saved` | 18 | 508,212 | `-UserDir` root used by the verified isolated session |
| `_live\inst1saved` | 15 | 345,029 | second isolated `-UserDir` root |
| `_live\inst1` | 26 | 122,296,652 | an instance root: junctions + hard-linked exe + UE4SS (~116 MB, almost all of it UE4SS) |

Both `-UserDir` roots have the shape UE4 expects (`Saved\Config\WindowsNoEditor`,
`Saved\Logs`, `Saved\SaveGames`), so `-UserDir=` is confirmed to be the working lever for
this title too — consistent with `01-binary-forensics.md` (`UserDir=`, `saveddirsuffix`,
`SaveToUserDir` all present, `ShouldSaveToUserDir()` true).

The seeded low-spec profile used by the earlier live session is a real file and is reused
verbatim by the rig:

```ini
[ScalabilityGroups]        sg.ViewDistanceQuality=0 … sg.ShadingQuality=0 (all 0)
[/Script/Engine.GameUserSettings]
ResolutionSizeX=640  ResolutionSizeY=360  FullscreenMode=2  WindowPosX=2200  WindowPosY=1400
```

**Physical memory, measured:** `GlobalMemoryStatusEx` reports **68,564,348,928 bytes =
63.86 GiB**, of which 12.49 GiB was available at the time of writing (80 % load — this
machine was busy running the analysis). That confirms the owner-supplied "64 GB" and
retires the earlier, wrong 22 GB figure.

> Instrument note: `Get-CimInstance Win32_ComputerSystem` is **Access denied** under this
> session's token — that is exactly how the 22 GB error arose. `GlobalMemoryStatusEx` via
> P/Invoke works and is what the rig uses. `Get-PSDrive` also reports 0 free, so the rig does
> not attempt a disk-space check.

### E2.2 The prepared measurement rig

`analysis/re/measure_two_instances.ps1` — **prepared, not run** (nothing was launched).

What it does, in order:

1. **Preflight.** `-DryRun` stops after this. Verifies the exe exists, prints size + SHA-256,
   and (unless `-SkipBuildGuard`) runs `build_guard.py` and **refuses to measure a build the
   mod cannot hook**.
2. **Isolation.** `_live\make-inst1.ps1`'s technique, twice: junction `Engine`,
   `Content\Paks`, `Content\Movies` from the real install; hard-link the exe (same volume, no
   copy); two separate `-UserDir` roots (`instAsaved`, `instBsaved`) each seeded with the
   640×360 windowed profile; `-saveddirsuffix=qdA` / `qdB` on top.
3. **Provable isolation.** It refuses if either `-UserDir` resolves to
   `%LOCALAPPDATA%\QueensDomain\Saved`, snapshots that directory's file list + sizes + mtimes
   before launch and re-checks it after, and reports anything new/modified/removed.
4. **Launch.** `Start-Process … -ArgumentList "-UserDir=… -saveddirsuffix=… -SaveToUserDir
   -windowed" -WorkingDirectory <instance bin dir> -PassThru`, three seconds apart.
5. **Sample every `-SampleSeconds` (default 2 s)** for up to `-DurationSeconds`
   (default 900 s), one CSV row per instance per tick:
   `timestamp, sample, instance, pid, alive, working_set_mb, private_mb, peak_ws_mb,
   threads, handles, responding, total_phys_gb, avail_phys_gb, load_pct, saved_files,
   savegames`.
6. **Clean up.** `Stop-Process -Force` on both PIDs, then verify the real save is unchanged.

Typical use:

```powershell
# dry run — builds the roots, seeds configs, prints the command lines, launches nothing
.\measure_two_instances.ps1 -DryRun

# the measurement
.\measure_two_instances.ps1 -DurationSeconds 900 -SampleSeconds 2
```

Exit codes: `0` all criteria met · `1` memory headroom failed · `2` preflight refused ·
`3` at least one instance died.

### E2.3 Acceptance criterion (encoded in the script)

Let `P = 68.56 GB` (measured physical) and `margin = 25 %` (the `-MarginFraction` default),
so the combined budget is `0.75 × P = 51.4 GB`.

| # | criterion | how the script decides |
|---|---|---|
| AC1 | **both instances reach gameplay** | both PIDs alive at the end of the window, and each one's peak `WorkingSet64` ≥ **8 GB** (title screen settles at ~2.7 GB, gameplay at ~19.5 GB, so 8 GB is well clear of both) |
| AC2 | **combined private bytes below physical RAM with margin** | `max over samples of (PrivateMemorySize64_A + PrivateMemorySize64_B) ≤ 0.75 × P`, i.e. **≤ 51.4 GB**, leaving ≥ 17.1 GB for the OS, both GPU drivers and the page cache |
| AC3 | **isolation held** | the real `%LOCALAPPDATA%\QueensDomain\Saved` snapshot is byte-identical (path, size, mtime) |
| AC4 | **no silent crash** | neither process disappears mid-window (else exit 3) |

The expectation from the earlier single-instance measurement — 19.47 GB working set /
20.03 GB private at 640×360 with every scalability group at 0, on a 63.86 GiB machine — is
`2 × 20.03 = 40.1 GB`, i.e. **AC2 passes with ~11 GB of margin**, and AC1 is the one that
actually needs a real run to confirm (both instances must settle, not just start).

Practical caveats worth carrying into the run:

- Both instances rendering at 640×360 still means two D3D devices and two full texture
  sets; `sg.TextureQuality=0` is already the seeded value and `r.Streaming.PoolSize` is the
  lever if the combined figure comes in higher than 40 GB.
- Launch from a **console that stays open** — `08-live-session.md` records that a harness
  console closing sends `CTRL_CLOSE` to the child and kills the game ~30 s in. The rig keeps
  its own console alive for the whole sampling window, which is sufficient.
- A killed instance leaves a usable `World0_Suspend_*.sav`, so a failed run is resumable —
  but those files land in the instance's own `-UserDir`, never in the real save.

---

## Status summary

| item | status | key finding | confidence |
|---|---|---|---|
| **E5** single-instance mutex | **CLOSED (static)** | `CreateMutexW(L"UnrealEngine4", TRUE)` at RVA `0x0077D6E0`; handle at `0x04964EE0`; on `ERROR_ALREADY_EXISTS` it `ReleaseMutex`es and continues; flag `0x0473B768` has exactly two readers, both benign (shader-cache cleanup guard, ID naming). **Not enforced.** No `OpenMutexW`/`FindWindowW`/named-pipe import exists, and all other kernel sync objects are unnamed | mechanism HIGH · non-enforcement HIGH · "no other mechanism" MEDIUM (file/port locks not covered) |
| **E6** build guard | **CLOSED** | `build_guard.py` run against the shipped image: **PASS** — 19 targets, 14 byte-verified, 5 `RUNTIME-VALIDATED` (the `.bss` globals, never compared as zeros), 0 mismatches, 0 anomalies. `--selftest` **PASS**: a flipped byte on `UObject::ProcessEvent` REFUSEs and is attributed; a flipped unguarded byte still PASSes | HIGH |
| **E2** two instances | **PARTIAL** | Rig prepared and syntax-checked, not run (by instruction). File system measured: three distinct save roots, instance root 116 MB, real save 165,781 B. Physical RAM measured at **63.86 GiB** via `GlobalMemoryStatusEx` (CIM denied). Budget `≤ 51.4 GB` combined private; prior 2 × 20.03 GB ≈ 40.1 GB predicts a pass with ~11 GB margin | facts HIGH · outcome UNMEASURED |
