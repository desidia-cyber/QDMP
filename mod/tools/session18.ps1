# session18.ps1 -- Session 18: the R132 crash, one process per scenario.
#
# WHAT THIS SESSION PROVES
# ------------------------
# Session 17 ended on one number: the documented PLAY set (PLAYTEST.md section 4) survived
# 499 s of a ten-minute soak, against 256 s before two guards, while the SHIPPED defaults
# (every write path off) ran the full 600 s.  Three crash addresses were recorded and none
# of them was understood:
#
#     EXCEPTION_ACCESS_VIOLATION reading 0x0000000100000017 / 0x000001951e4d50bc
#       WINMM+0x3b357   WINMM+0x6f4d7   WINMM+0x37988
#
# Session 18 resolved those through the /MAP the build already emitted:
#
#     0x3b357 -> EnemySpawnPump    0x6f4d7 -> ScanLocalEquip    0x37988 -> EnemyIdentity
#
# and every one of them dereferences a cached UObject* the engine had already freed.  The
# fix is a weak handle (native/src/qdweak.h): {InternalIndex, slot SerialNumber} resolved
# through GUObjectArray, so the cached address is never read.  Every cached actor/object
# pointer in proxy.cpp is now one of those.
#
# THE PHASES, AND WHY EACH IS A SEPARATE BOOT
# -------------------------------------------
#   smoke      one short boot: the new binary loads, a session forms, the lanes still
#              answer.  A weak-handle conversion touches almost every lane, so this runs
#              FIRST and cheaply -- a broken conversion must not cost a 30-minute run.
#   soak-hub   THE HEADLINE.  The exact soak body that crashed (Session 17's Invoke-Soak:
#              both players teleported toward the dragon's spawner, then 3 s of hits and a
#              re-anchor every other iteration), repeated -Repeat times BACK TO BACK in ONE
#              pair of processes.  Back-to-back matters: a fix that merely moves the first
#              crash later is only visible on the second run.
#   soak-780   the same body after entering 780_SZ_Depths through the game's level list,
#              where both machines hold the same authored enemy GUIDs -- so the kill-set
#              diff and the shared-HP comparison mean something (PLAYTEST section 8: hub
#              kill lists diverge by design because each machine runs its own spawners).
#
# ONE COMMAND PER SCENARIO.  Fresh pairs are re-seeded from analysis/save-backup-before-live
# so every phase starts from the same save.  Roles are read from `session established`.
#
# ON CRASH EVIDENCE: WER LocalDumps needs a write to HKLM, which this account does not have
# (checked: not an Administrator).  Crash evidence therefore comes from (a) the module's own
# log, which records the last fault line, and (b) UE's own Saved/Crashes folder, whose
# CrashContext.runtime-xml names the faulting module.  Stated here so a reader does not
# assume a full dump exists and conclude it was lost.
[CmdletBinding()]
param(
    [ValidateSet('smoke', 'soak-hub', 'soak-780', 'all')]
    [string]$Phase = 'all',
    [int]$BootSeconds = 300,
    [int]$SoakSeconds = 600,
    # Consecutive soaks inside ONE boot.  2 is the proof the brief asks for: the first run
    # proves the crash is gone, the second proves it is not merely postponed.
    [int]$Repeat = 2,
    [string]$Report = '',
    # The PLAY set is the configuration under test, so it is the DEFAULT here.  The shipped
    # set (all write paths off) is the control and is selected explicitly.
    [switch]$ShippedDefaults
)

$ErrorActionPreference = 'Stop'
$here    = $PSScriptRoot
$repo    = Split-Path -Parent (Split-Path -Parent $here)
$gameDir = 'G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64'
$relay   = Join-Path (Split-Path -Parent $here) 'rust\target\release\qd-relay.exe'
$rigBase = 'G:\Freespace\Queens_Domain\_test'
$rigRoot = Join-Path $rigBase ('s18-' + $Phase)
$seed    = Join-Path $repo 'analysis\save-backup-before-live'
$outDir  = Join-Path $rigRoot 'out'
if (-not $Report) { $Report = Join-Path $outDir ('report-' + $Phase + '.txt') }
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force -Path $outDir | Out-Null }

$script:lines = New-Object System.Collections.Generic.List[string]
trap {
    Write-Output ("UNHANDLED: " + $_.Exception.Message)
    Write-Output ("AT: " + $_.InvocationInfo.PositionMessage)
    $script:lines.Add("UNHANDLED: " + $_.Exception.Message) | Out-Null
    $script:lines.Add("AT: " + $_.InvocationInfo.PositionMessage) | Out-Null
    try { Stop-All } catch {}
    try { $script:lines | Set-Content -LiteralPath $Report } catch {}
    exit 1
}
function Say($m) {
    $s = "[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $m
    Write-Output $s
    $script:lines.Add($s) | Out-Null
}
function Note($m) { Write-Output $m; $script:lines.Add($m) | Out-Null }

function Get-RigPaths([int]$i) {
    $root   = Join-Path $rigRoot ("inst{0}" -f $i)
    $suffix = "qd$i"
    [pscustomobject]@{
        Index  = $i; Root = $root; Suffix = $suffix
        Log    = Join-Path $gameDir ("qdcoop-{0}.log" -f $suffix)
        Tp     = Join-Path $root 'qd.tp';   Act  = Join-Path $root 'qd.act'
        Scan   = Join-Path $root 'qd.scan'; God  = Join-Path $root 'qd.god'
        Hit    = Join-Path $root 'qd.hit';  Cheat = Join-Path $root 'qd.cheat'
        CheatArg = Join-Path $root 'qd.cheatarg'; Travel = Join-Path $root 'qd.travel'
        Hard   = Join-Path $root 'qd.hard'; Level = Join-Path $root 'qd.level'
        List   = Join-Path $root 'qd.list'; Near = Join-Path $root 'qd.near'
        Drops  = Join-Path $root 'qd.drops'; Menu = Join-Path $root 'qd.menu'
        EFunc  = Join-Path $root 'qd.efunc'; EAtk = Join-Path $root 'qd.eattack'
        Use    = Join-Path $root 'qd.use';   Pdmg = Join-Path $root 'qd.pdmg'
        Kill   = Join-Path $root 'qd.kill';  PeTrace = Join-Path $root 'qd.petrace'
        Enemies = Join-Path $root 'qd.enemies'; Sequence = Join-Path $root 'qd.sequence'
        EnemyKill = Join-Path $root 'qd.enemykill'; HitGuid = Join-Path $root 'qd.hitguid'
        Obj    = Join-Path $root 'qd.obj';  Flag = Join-Path $root 'qd.flag'
        Gate   = Join-Path $root 'qd.gate'
        Pid = "$root.pid"
    }
}
$R1 = Get-RigPaths 1
$R2 = Get-RigPaths 2
$inst = @{ 1 = $R1; 2 = $R2 }

# The logs are open for write by the game the whole time; the FileShare flags are what make
# reading a live log a retry instead of an "it is in use" failure.
function Read-LogLines([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) { return @() }
    for ($try = 0; $try -lt 3; $try++) {
        try {
            $fs = [System.IO.File]::Open($path, [System.IO.FileMode]::Open,
                                         [System.IO.FileAccess]::Read,
                                         ([System.IO.FileShare]::ReadWrite -bor [System.IO.FileShare]::Delete))
            try {
                $sr = New-Object System.IO.StreamReader($fs, [System.Text.Encoding]::UTF8, $true)
                $txt = $sr.ReadToEnd()
                $sr.Dispose()
            } finally { $fs.Dispose() }
            return @($txt -split "`r?`n")
        } catch { Start-Sleep -Milliseconds 50 }
    }
    return @()
}
# Everything the RUN produced, i.e. after the last `hooked FEngineLoop`.  `switches:` is
# printed during load, BEFORE that line, so it is read from the whole file instead.
function Get-RunLines([int]$i) {
    $p = $inst[$i].Log
    if (-not (Test-Path -LiteralPath $p)) { return @() }
    $all = @(Read-LogLines $p)
    if ($all.Count -eq 0) { return @() }
    $start = 0
    for ($k = $all.Count - 1; $k -ge 0; $k--) {
        if ($all[$k] -match 'hooked FEngineLoop') { $start = $k; break }
    }
    return @($all[$start..($all.Count - 1)])
}
function Get-Matches([int]$i, [string]$pattern) {
    return @(Get-RunLines $i | Where-Object { $_ -match $pattern })
}
# The LOAD-TIME lines (`targets:` / `substrate:` / `switches:`) are printed while the module
# initialises, BEFORE the frame-pump hook is installed, so Get-RunLines -- which starts at
# `hooked FEngineLoop` -- cannot see them.  That is exactly the trap Session 17 recorded for
# `switches:`; this is the same read applied to the other two, and over the whole file.
function Get-LoadMatches([int]$i, [string]$pattern) {
    return @(Read-LogLines $inst[$i].Log | Where-Object { $_ -match $pattern })
}
# The same read, but WAITING: the peer's `substrate:` line lands a second or two after its
# `session established`, so a single read at the moment the roles are known can miss it and
# report a FAIL on a healthy build.  Waiting for the load line is the honest form.
function Wait-LoadMatch([int]$i, [string]$pattern, [int]$seconds) {
    $deadline = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $deadline) {
        $m = @(Get-LoadMatches $i $pattern)
        if ($m.Count -gt 0) { return $m }
        Start-Sleep -Milliseconds 300
    }
    return @()
}
function Get-LineMs([string]$ln) {
    if ($ln -match '^\s*(\d{1,2}):(\d{2}):(\d{2})\.(\d{3})') {
        return ([int]$Matches[1] * 3600000 + [int]$Matches[2] * 60000 +
                [int]$Matches[3] * 1000 + [int]$Matches[4])
    }
    return -1
}
function Get-Mark([int]$i) {
    $now = Get-Date
    $ms = $now.Hour * 3600000 + $now.Minute * 60000 + $now.Second * 1000 + $now.Millisecond
    return $ms - 1000
}
function Test-Alive([int]$i) {
    if (-not $script:pids.ContainsKey($i)) { return $false }
    return [bool](Get-Process -Id $script:pids[$i] -ErrorAction SilentlyContinue)
}
function Get-Since([int]$i, [int]$markMs) {
    $out = @()
    foreach ($ln in @(Get-RunLines $i)) {
        $t = Get-LineMs ([string]$ln)
        if ($t -ge 0 -and $t -ge $markMs) { $out += $ln }
    }
    return $out
}
# The same read, MEMOISED per instance and mark.
#
# WHY: the kill-set comparison asks "the last enemykey line for THIS key" once per shared
# key, and there can be hundreds.  With Get-Since uncached that is one full re-read and
# re-split of a 20+ MB log per key, which measured at over an hour of analysis for a
# ten-minute soak -- the run survived and then the REPORTING did not finish.  The log only
# grows at the tail, so caching the parsed window is safe as long as the cache is keyed by
# the mark and refreshed when the caller says new lines must be seen.
$script:sinceCache = @{}
function Get-Window([int]$i, [int]$markMs, [switch]$Fresh) {
    $key = "$i|$markMs"
    if (-not $Fresh -and $script:sinceCache.ContainsKey($key)) { return $script:sinceCache[$key] }
    $v = @(Get-Since $i $markMs)
    $script:sinceCache[$key] = $v
    return $v
}
# A single pass over the run, split into per-instance arrays ONCE, for the loops that scan
# every key.  Same result as Get-Since $i 0, without the quadratic re-read.
function Get-RunWindow([int]$i) { return @(Get-Window $i 0) }
function Wait-Since([int]$i, [string]$pattern, [int]$mark, [int]$seconds) {
    $deadline = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $deadline) {
        $m = @(Get-Since $i $mark | Where-Object { $_ -match $pattern })
        if ($m.Count -gt 0) { return $m }
        if (-not (Test-Alive $i)) { return @() }
        Start-Sleep -Milliseconds 500
    }
    return @()
}
function Wait-Gameplay([int]$i, [int]$seconds) {
    $deadline = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $deadline) {
        if (-not (Test-Alive $i)) { return $false }
        $m = @(Get-Matches $i 'scan: local at|scan: health')
        if ($m.Count -gt 0) { return $true }
        Start-Sleep -Milliseconds 500
    }
    return $false
}

function Set-Control([int]$i, [string]$leaf, $value) {
    # Writing a lever file is a plain Set-Content, but the GAME is reading the same file
    # every frame, so the write can collide with the reader and throw "Stream was not
    # readable" (or a sharing violation) for no bad reason.  Measured: one such throw
    # aborted a soak at t+60 s through the trap, which reports as a harness failure and
    # throws away a live run.  A lever instruction is idempotent, so the honest fix is to
    # retry a few times before failing.
    $p = Join-Path -Path ([string]$inst[$i].Root) -ChildPath ([string]$leaf)
    for ($try = 0; $try -lt 6; $try++) {
        try {
            if ($null -eq $value) { Remove-Item -LiteralPath $p -ErrorAction SilentlyContinue }
            else { Set-Content -LiteralPath $p -Value ([string]$value) -NoNewline -ErrorAction Stop }
            return
        } catch [System.IO.IOException], [System.UnauthorizedAccessException] {
            Start-Sleep -Milliseconds 120
        } catch {
            if ($try -ge 5) { throw }
            Start-Sleep -Milliseconds 120
        }
    }
    Say "  WARN: could not write the lever '$leaf' for instance $i after 6 tries"
}

$script:verdicts = New-Object System.Collections.Generic.List[object]
function Check([string]$step, [int]$i, [string]$pattern, [int]$secs, [string]$label,
               [int]$Mark = 0) {
    if (-not (Test-Alive $i)) {
        $script:verdicts.Add([pscustomobject]@{ Step = $step; Inst = $i; What = $label; Ok = $false; Line = 'INSTANCE DIED' }) | Out-Null
        Note "  FAIL [$i] $label -- the instance is no longer running"
        return $false
    }
    $m = Wait-Since $i $pattern $Mark $secs
    $found = ($m.Count -gt 0)
    $script:verdicts.Add([pscustomobject]@{ Step = $step; Inst = $i; What = $label; Ok = $found; Line = ($m | Select-Object -Last 1) }) | Out-Null
    if ($found) { $line = "  PASS [$i] $label" }
    else { $line = "  --   [$i] $label (no line matches '$pattern' in ${secs}s)" }
    Note $line
    return $found
}
function Verdict([string]$step, [int]$i, [bool]$ok, [string]$what, [string]$line = '') {
    $script:verdicts.Add([pscustomobject]@{ Step = $step; Inst = $i; What = $what; Ok = $ok; Line = $line }) | Out-Null
    Note ("  {0} [{1}] {2}{3}" -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $i, $what,
          $(if ($line) { " -- $line" } else { '' }))
}
function Step([string]$title, [string]$body) { Note ''; Note "### $title"; if ($body) { Note $body } }
function Show-Since([int]$i, [string]$pattern, [int]$mark, [int]$n) {
    $m = @(Get-Since $i $mark | Where-Object { $_ -match $pattern })
    if ($m.Count -eq 0) { Note "  [$i] (no line matches '$pattern' since the step started)"; return }
    $m | Select-Object -Last $n | ForEach-Object {
        $s = [string]$_
        if ($s.Length -gt 400) { $s = $s.Substring(0, 400) + ' ...' }
        Note "  [$i] $s"
    }
}
function Get-LastPos([int]$i, [int]$mark = 0) {
    $rows = @(Get-Since $i $mark | Where-Object { $_ -match 'scan: local at ' })
    if ($rows.Count -eq 0) { return $null }
    if ($rows[-1] -match 'local at (-?[0-9.]+),(-?[0-9.]+),(-?[0-9.]+)') {
        return @([double]$Matches[1], [double]$Matches[2], [double]$Matches[3])
    }
    return $null
}
$script:goSeq = 0
# `qd.tp near:<name>,<yaw>` -- the yaw cycles so a repeat is a real instruction, not a
# no-op (the lever ignores an unchanged string).
function Invoke-Go([int]$i, [string]$name, [int]$near = 140) {
    $script:goSeq++
    $yaw = ($script:goSeq * 37) % 360
    Set-Control $i 'qd.near' ([string]$near)
    Set-Control $i 'qd.tp' ('near:' + $name + ',' + $yaw)
}
function Invoke-Hard([int]$i, [string]$map) {
    Set-Control $i 'qd.hard' $map
    Say "  hard[$i] -> $map"
}

function Stop-All {
    Get-Process -Name 'QueensDomain-Win64-Shipping', 'qd-relay' -ErrorAction SilentlyContinue |
        ForEach-Object {
            Say "  stopping leftover $($_.ProcessName) pid=$($_.Id)"
            Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue
        }
    Start-Sleep -Seconds 2
}

function Install-And-Configure {
    $built = Join-Path $repo 'mod\native\build\winmm.dll'
    if (-not (Test-Path -LiteralPath $built)) { Say "REFUSED: built proxy not found at $built"; exit 2 }
    $dst = Join-Path $gameDir 'winmm.dll'
    try { Copy-Item -LiteralPath $built -Destination $dst -Force }
    catch {
        $held = $dst + '.held-' + (Get-Date -Format 'HHmmss')
        Rename-Item -LiteralPath $dst -NewName (Split-Path -Leaf $held) -Force
        Copy-Item -LiteralPath $built -Destination $dst -Force
        Say "destination was image-mapped; moved it to $held and installed the new proxy"
    }
    # The PDB goes beside it so a crash dump can be symbolicated at all.  Without it a stack
    # is a list of RVAs; with it, a list of function names and lines.
    $pdb = Join-Path $repo 'mod\native\build\winmm.pdb'
    if (Test-Path -LiteralPath $pdb) {
        Copy-Item -LiteralPath $pdb -Destination (Join-Path $gameDir 'winmm.pdb') -Force
        Say "installed winmm.pdb beside it (symbols for a dump)"
    }
    Say "installed $built -> $dst"

    $ini = Join-Path $gameDir 'qdcoop.ini'
    if ($ShippedDefaults) {
        $srcIni = Join-Path $repo 'mod\qdcoop.ini'
        $text = Get-Content -LiteralPath $srcIni -Raw
        $text = $text -replace '(?m)^RelayHost=.*$', 'RelayHost=127.0.0.1'
        $text = $text -replace '(?m)^RelayPort=.*$', 'RelayPort=47800'
        $text = $text -replace '(?m)^LogLevel=.*$', 'LogLevel=3'
        $text | Set-Content -LiteralPath $ini -NoNewline
        Say "wrote $ini from the SHIPPED qdcoop.ini (relay pinned to loopback, LogLevel=3)"
        Copy-Item -LiteralPath $ini -Destination (Join-Path $outDir 'qdcoop-shipped.ini') -Force
    } else {
        # THE DOCUMENTED PLAY SET, verbatim from doc/PLAYTEST.md section 4 -- the
        # configuration R132 says crashes.  LogLevel=3 because this is a test run and the
        # debug lines are what make a fault findable.
        $lines = @('[Local]', 'Enabled=1', 'LogLevel=3', '', '[Net]',
                   'RelayHost=127.0.0.1', 'RelayPort=47800', '', '[Rules]',
                   'FlagSync=1', 'InventorySync=1', 'ActorStateSync=1', 'ObjectSync=1',
                   'EnemySync=1', 'PuppetSync=1', 'PlayerBody=1', 'DialogueSync=1',
                   'StreamUnion=1', 'TravelSync=1', 'CutsceneSync=1', 'MoverSync=1',
                   'LootSync=1', 'SuspendGuard=1', 'SuppressMenuPause=1', 'HostAuthoritative=1',
                   '', '[Tuning]', 'PlayerHz=30', 'EnemyRowsPerTick=20', 'SpawnAttemptsMax=5')
        $lines | Set-Content -LiteralPath $ini
        $sw = @(Get-Content -LiteralPath $ini | Where-Object { $_ -match '=' -and $_ -notmatch '^;' })
        Say ("wrote $ini from the documented PLAY set ({0} switches)" -f $sw.Count)
        Copy-Item -LiteralPath $ini -Destination (Join-Path $outDir 'qdcoop-play.ini') -Force
    }
}

function Seed-FreshPair {
    Say 're-seeding both instances from the pristine pre-live save'
    foreach ($i in 1, 2) {
        $r = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'make_test_instance.ps1') `
                -Name ("inst{0}" -f $i) -Index $i -SourceSave $seed -Root $rigRoot -Suffix $inst[$i].Suffix -Force 2>&1
        $r | Where-Object { $_ -match 'FAIL|REFUSED|seeded|save dir|Instance ready' } | ForEach-Object { Note "  $_" }
        if ($LASTEXITCODE -ne 0) { Say "REFUSED: make_test_instance refused instance $i"; exit 2 }
    }
}

function Start-Boot([string]$tag) {
    Stop-All
    Install-And-Configure
    foreach ($i in 1, 2) {
        foreach ($leaf in 'qd.tp','qd.act','qd.hit','qd.cheat','qd.cheatarg','qd.travel','qd.hard',
                          'qd.level','qd.list','qd.near','qd.drops','qd.menu','qd.efunc','qd.eattack',
                          'qd.use','qd.pdmg','qd.kill','qd.petrace','qd.enemies','qd.sequence',
                          'qd.enemykill','qd.hitguid','qd.obj','qd.flag','qd.gate') {
            Set-Control $i $leaf $null
        }
        Set-Control $i 'qd.scan' '1'
        Set-Control $i 'qd.god' '400'
        if (Test-Path -LiteralPath $inst[$i].Pid) { Remove-Item -LiteralPath $inst[$i].Pid -ErrorAction SilentlyContinue }
        $old = $inst[$i].Log
        if (Test-Path -LiteralPath $old) { Move-Item -LiteralPath $old -Destination ($old + '.prev') -Force }
    }
    $env:QDCOOP_AUTOLOAD = '0'
    $env:QDCOOP_TEST_NEAR = '140'
    $env:QDCOOP_TEST_HITMS = '3000'

    if (-not (Test-Path -LiteralPath $relay)) { Say "REFUSED: relay not built at $relay"; exit 2 }
    $relayLog = Join-Path $outDir ("relay-$tag.log")
    Remove-Item -LiteralPath $relayLog -ErrorAction SilentlyContinue
    $script:relayProc = Start-Process -FilePath $relay -ArgumentList @('--bind', '127.0.0.1:47800') `
            -RedirectStandardOutput $relayLog -RedirectStandardError (Join-Path $outDir ("relay-$tag.err")) `
            -WindowStyle Hidden -PassThru
    Say "relay pid=$($script:relayProc.Id) -> $relayLog"
    Start-Sleep -Seconds 2

    $script:pids = @{}
    foreach ($i in 1, 2) {
        $ueLog = Join-Path $outDir ("$tag-inst{0}-ue.log" -f $i)
        Remove-Item -LiteralPath $ueLog -ErrorAction SilentlyContinue
        $launch = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'run-hidden.ps1') `
                    -UserDir $inst[$i].Root -Suffix $inst[$i].Suffix -AbsLog $ueLog 2>&1
        $gp = ($launch | Where-Object { $_ -match '^\d+$' } | Select-Object -Last 1)
        if (-not $gp) { Say "LAUNCH FAILED for instance $i"; $launch | ForEach-Object { Note $_ } }
        else { $script:pids[$i] = [int]$gp; Set-Content -LiteralPath $inst[$i].Pid -Value $gp; Say "inst$i pid=$gp (off-screen)" }
        Start-Sleep -Seconds 3
    }

    Say "waiting for a session (up to $BootSeconds s)"
    $deadline = (Get-Date).AddSeconds($BootSeconds)
    $roles = @{}
    while ((Get-Date) -lt $deadline) {
        foreach ($i in 1, 2) {
            if ($roles.ContainsKey($i)) { continue }
            if (-not $script:pids.ContainsKey($i)) { continue }
            $m = @(Get-Matches $i 'INFO session established: role=(HOST|PEER)')
            if ($m.Count -gt 0 -and $m[-1] -match 'role=(HOST|PEER)') {
                $roles[$i] = $Matches[1]
                Note ("  [{0}] {1}" -f $i, ([string]$m[-1]).Trim())
            }
        }
        if ($roles.Count -eq 2) { break }
        if (-not (Test-Alive 1) -and -not (Test-Alive 2)) { break }
        Start-Sleep -Seconds 2
    }
    $script:H = $null; $script:P = $null
    foreach ($i in 1, 2) {
        if ($roles.ContainsKey($i) -and $roles[$i] -eq 'HOST') { $script:H = $i }
        if ($roles.ContainsKey($i) -and $roles[$i] -eq 'PEER') { $script:P = $i }
    }
    if (-not $script:H -or -not $script:P) {
        Say 'REFUSED: no HOST/PEER pair was established'
        foreach ($i in 1, 2) {
            Note "  [$i] alive=$(Test-Alive $i)"
            Show-Since $i 'session|REFUS|ERR |PROBE|substrate' 0 12
        }
        Stop-All
        exit 2
    }
    Say "roles: HOST=inst$($script:H) PEER=inst$($script:P)"

    # THE SAVE MUST BE LOADED BEFORE ANY LEVER TOUCHES THE WORLD.  Measured: without this
    # wait, `qd.hard 780_SZ_Depths` fired 39 s in, while the title screen was still up, and
    # the game answered `arrive: 780_SZ_Depths ended with no pawn to place` -- the level
    # loaded with no GameMode to spawn a player, so every phase below silently had no pawn.
    # `scan: local at` is the mod's own statement that it has a player to report.
    Say "waiting for gameplay on both (up to $BootSeconds s)"
    foreach ($i in @($script:H, $script:P)) {
        $ok = Wait-Gameplay $i $BootSeconds
        Say "inst${i} gameplay=$ok"
        Verdict 'boot' $i $ok 'the machine reached gameplay (it has a pawn to report)' ''
    }
}

function Enter-Area([int]$H, [int]$P, [string]$area, [string]$anchor, [string]$step) {
    Step "$step -- both players into $area through the game's own level list"
    $mH = Get-Mark $H
    $mP = Get-Mark $P
    Invoke-Hard $H $area
    Invoke-Hard $P $area
    Start-Sleep -Seconds 12
    [void](Check $step $H "level list '.*' added to the manager's stack" 60 'the host pushed the area level list' $mH)
    [void](Check $step $P "level list '.*' added to the manager's stack" 60 'the peer pushed the same list' $mP)
    Start-Sleep -Seconds 12
    Invoke-Go $H $anchor 400
    Invoke-Go $P $anchor 400
    Start-Sleep -Seconds 10
    [void](Check $step $H ('dev teleport near .' + [regex]::Escape($anchor) + '. -> ' + [regex]::Escape($anchor) + ' at') 40 'the host stands at the anchor' $mH)
    [void](Check $step $P ('dev teleport near .' + [regex]::Escape($anchor) + '. -> ' + [regex]::Escape($anchor) + ' at') 40 'the peer stands at the same anchor' $mP)
    $pH = Get-LastPos $H $mH
    $pP = Get-LastPos $P $mP
    Verdict $step $H ([bool]$pH) "the host has a pawn in $area" $(if ($pH) { ("{0:N0},{1:N0},{2:N0}" -f $pH[0], $pH[1], $pH[2]) } else { '' })
    Verdict $step $P ([bool]$pP) "the peer has a pawn in $area" $(if ($pP) { ("{0:N0},{1:N0},{2:N0}" -f $pP[0], $pP[1], $pP[2]) } else { '' })
}

# The world each machine reports itself in.  `session established` is a LOAD-TIME line and
# its `level=` field is empty here, so the world is read from the run-time lines that DO
# carry it (`scan:`/`travel:` name the map) and, failing that, from the level-pin lever's
# own acknowledgement.  Reported rather than assumed: a blank means "not stated", which is
# honest, instead of a wrong map name.
function Get-World([int]$i) {
    $m = @(Get-Matches $i 'level pin: .* -> |hard transition: loading \S+|arrive: (?:placed|[a-z_0-9]+)')
    if ($m.Count -gt 0) {
        $s = [string]$m[-1]
        if ($s -match 'hard transition: loading (\S+)') { return $Matches[1] }
        if ($s -match 'level pin: \S+ -> (\S+)') { return $Matches[1] }
    }
    $lv = @(Get-Matches $i 'level union: .*\b([0-9]{3}_[A-Za-z_0-9]+)\b')
    if ($lv.Count -gt 0 -and [string]$lv[-1] -match '\b([0-9]{3}_[A-Za-z_0-9]+)\b') { return $Matches[1] }
    return ''
}

# ---------------------------------------------------------------------------
# The soak body.  This is Session 17's Invoke-Soak, unchanged in behaviour: get the pair
# toward the arena, then hit the nearest locally-run enemy every 3 s and re-anchor every
# other iteration.  What is NEW is that it runs -Repeat times inside one boot and records
# how far each run got, so "the crash is gone" and "the crash moved" are different answers.
# ---------------------------------------------------------------------------
function Invoke-OneSoak([int]$H, [int]$P, [string]$step, [int]$run, [string]$anchor) {
    $m0 = Get-Mark $H
    # `switches:` is a LOAD-TIME line (printed before the frame-pump hook), so it must be read
    # from the whole log, not from Get-Since.  It was previously read inside a bare `{ ... }`
    # block, which in PowerShell is a script-block LITERAL that is never executed -- so the
    # variable stayed empty and the soak reported "SHIPPED" for a run that was the play set.
    $swLine = ''
    $swm = @(Read-LogLines $inst[$H].Log | Where-Object { $_ -match 'switches: ' })
    if ($swm.Count -gt 0) { $swLine = ([string]$swm[-1]).Trim() }
    if ($swLine) { Note ("  [$H] " + $swLine) }
    $isPlay = $swLine -match 'enemy=1'
    Note ("  this soak ran the {0} set" -f $(if ($isPlay) { 'DOCUMENTED PLAY' } else { 'SHIPPED (all write paths off)' }))
    Verdict $step $H ([bool]$swLine) 'the host logged its switch set' $swLine
    Verdict $step $H $isPlay 'the switch set under test is the documented PLAY set' $swLine
    Note ("  world: host='{0}' peer='{1}'" -f (Get-World $H), (Get-World $P))

    if ($anchor) {
        Invoke-Go $H $anchor 400
        Invoke-Go $P $anchor 400
        Start-Sleep -Seconds 10
    }

    $start = Get-Date
    $end = $start.AddSeconds($SoakSeconds)
    $n = 0
    $died = $false
    while ((Get-Date) -lt $end) {
        if (-not (Test-Alive $H) -or -not (Test-Alive $P)) { $died = $true; break }
        $n++
        # Hit the nearest locally-run enemy on each machine, and re-anchor every 20 s so the
        # fight keeps finding enemies as the ones in reach die.  The re-anchor is also what
        # drives the streaming churn that freed the actors this session's fix is about.
        Set-Control $H 'qd.hit' ([string](200 + ($n % 3) * 100))
        Set-Control $P 'qd.hit' ([string](200 + ($n % 3) * 100))
        if (($n % 2) -eq 0) {
            Invoke-Go $H 'enemy' 200
            Invoke-Go $P 'enemy' 200
        }
        Start-Sleep -Seconds 3
    }
    $elapsed = [int]((Get-Date) - $start).TotalSeconds
    Note ("  soak run {0}: {1} s of {2} s, iterations={3}" -f $run, $elapsed, $SoakSeconds, $n)
    Verdict $step $H (-not $died) 'both processes survived the soak' $(if ($died) { "a process died after ${elapsed}s" } else { "${elapsed}s" })
    foreach ($i in @($H, $P)) {
        $alive = Test-Alive $i
        Verdict $step $i $alive 'the process is alive at the end of the soak' $(if ($alive) { "${elapsed}s" } else { '' })
    }

    # The kill sets, per GUID, from each machine's own dump.
    if (-not (Test-Alive $H) -or -not (Test-Alive $P)) {
        Note '  (kill-set comparison skipped: a process is gone, so its dump cannot answer)'
        Verdict $step $H $false 'the per-GUID kill sets DIFF TO ZERO' 'a process died; the dump is unreadable'
        return
    }
    $mH = Get-Mark $H; $mP = Get-Mark $P
    Set-Control $H 'qd.enemies' ('s18-h-' + (Get-Date -Format 'HHmmss'))
    Set-Control $P 'qd.enemies' ('s18-p-' + (Get-Date -Format 'HHmmss'))
    Start-Sleep -Seconds 4
    # ONE read of each run, parsed into {key -> last line}, instead of a log rescan per key.
    $rowsH = @(Get-Window $H 0 -Fresh)
    $rowsP = @(Get-Window $P 0 -Fresh)
    $keysH = @{}; $keysP = @{}; $lastH = @{}; $lastP = @{}
    foreach ($ln in $rowsH) {
        if ([string]$ln -match 'enemykey ([0-9A-Fa-f]{8}) (ALIVE|DEAD)') {
            $k = $Matches[1].ToUpperInvariant()
            $keysH[$k] = $Matches[2]
            $lastH[$k] = $ln
        }
    }
    foreach ($ln in $rowsP) {
        if ([string]$ln -match 'enemykey ([0-9A-Fa-f]{8}) (ALIVE|DEAD)') {
            $k = $Matches[1].ToUpperInvariant()
            $keysP[$k] = $Matches[2]
            $lastP[$k] = $ln
        }
    }
    $deadH = @($keysH.Keys | Where-Object { $keysH[$_] -eq 'DEAD' })
    $deadP = @($keysP.Keys | Where-Object { $keysP[$_] -eq 'DEAD' })
    $onlyH = @($deadH | Where-Object { -not $keysP.ContainsKey($_) -or $keysP[$_] -ne 'DEAD' })
    $onlyP = @($deadP | Where-Object { -not $keysH.ContainsKey($_) -or $keysH[$_] -ne 'DEAD' })
    Note ("  host keys={0} (dead {1}); peer keys={2} (dead {3})" -f $keysH.Count, $deadH.Count, $keysP.Count, $deadP.Count)
    Note ("  dead only on the host: {0} -> {1}" -f $onlyH.Count, ($onlyH -join ','))
    Note ("  dead only on the peer: {0} -> {1}" -f $onlyP.Count, ($onlyP -join ','))
    Verdict $step $H ($onlyH.Count -eq 0 -and $onlyP.Count -eq 0) 'the per-GUID kill sets DIFF TO ZERO' ("host-dead=$($deadH.Count) peer-dead=$($deadP.Count) diff=$($onlyH.Count + $onlyP.Count)")

    # Shared enemies: HP equal on both, for every key both machines hold alive.
    $common = @($keysH.Keys | Where-Object { $keysP.ContainsKey($_) -and $keysH[$_] -eq 'ALIVE' -and $keysP[$_] -eq 'ALIVE' })
    Note ("  keys alive on BOTH machines: {0}" -f $common.Count)
    $unequal = 0; $checked = 0
    foreach ($k in $common) {
        if (-not $lastH.ContainsKey($k) -or -not $lastP.ContainsKey($k)) { continue }
        $a = if ([string]$lastH[$k] -match 'hp=([-0-9.]+)') { [double]$Matches[1] } else { $null }
        $b = if ([string]$lastP[$k] -match 'hp=([-0-9.]+)') { [double]$Matches[1] } else { $null }
        if ($null -eq $a -or $null -eq $b) { continue }
        ++$checked
        if ([math]::Abs($a - $b) -ge 0.5) {
            ++$unequal
            Note ("    HP MISMATCH {0}: host={1} peer={2}" -f $k, $a, $b)
            Note ("      host: " + ([string]$lastH[$k]).Trim())
            Note ("      peer: " + ([string]$lastP[$k]).Trim())
        }
    }
    Verdict $step $P ($unequal -eq 0) 'the HP of shared enemies is EQUAL on both machines' ("checked=$checked unequal=$unequal")

    # Warnings and errors, by class.
    #
    # FAULT AND ERROR ARE COUNTED SEPARATELY, and that distinction is the point of this
    # session.  Session 17's gate was `ERR |EXCEPTION|ACCESS_VIOLATION|fatal` in one bucket,
    # which cannot tell "the process faulted" from "a lane's own write was refused" -- and the
    # 780 soak measured 8005 `ERR actor-state: game writer ... read back N (WRITE FAILED)`
    # lines with ZERO ACCESS_VIOLATION and ZERO EXCEPTION in either log.  A shared gate would
    # have reported that run as "a fault", which is the opposite of what happened.
    foreach ($i in @($H, $P)) {
        $fault = @(Get-Since $i $m0 | Where-Object { $_ -match 'ACCESS_VIOLATION|EXCEPTION|fatal|\bfault' })
        $err = @(Get-Since $i $m0 | Where-Object { $_ -match ' ERR ' })
        $warn = @(Get-Since $i $m0 | Where-Object { $_ -match ' WARN ' })
        $classes = @{}
        foreach ($w in ($warn + $err)) {
            $s = [string]$w
            $c = '(unlabelled)'
            if ($s -match '(?:WARN|ERR)\s+([a-z\-]+):') { $c = $Matches[1] }
            if ($classes.ContainsKey($c)) { $classes[$c]++ } else { $classes[$c] = 1 }
        }
        Note ("  [{0}] FAULT lines (ACCESS_VIOLATION/EXCEPTION/fatal)={1}" -f $i, $fault.Count)
        Note ("  [{0}] ERR lines={1}  WARN lines={2}  classes={3}" -f $i, $err.Count, $warn.Count,
              (($classes.GetEnumerator() | Sort-Object Value -Descending | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join ' '))
        foreach ($e in ($fault | Select-Object -Last 4)) {
            $s = [string]$e
            if ($s.Length -gt 220) { $s = $s.Substring(0, 220) + ' ...' }
            Note "  [$i] $s"
        }
        Verdict $step $i ($fault.Count -eq 0) 'no FAULT lines during the soak (the crash claim)' ("faults=$($fault.Count)")
    }
}

# ===========================================================================
# SMOKE -- the new binary loads and the lanes still answer
# ===========================================================================
function Invoke-Smoke([int]$H, [int]$P, [string]$step) {
    Step 'SMOKE -- the weak-handle build loads, connects, and the lanes answer'
    # The equip scan is a ONE-SHOT: it fills its cache on the first gameplay frame and never
    # prints again.  So its line is captured from the load log BEFORE any mark is taken --
    # measuring it with a mark taken here asks "did it happen after I started looking", which
    # is the wrong question and reports a FAIL on a build that already did it.
    $eqHost = @(Wait-LoadMatch $H 'equip scan: RelativeScale3D' 30)
    $eqPeer = @(Wait-LoadMatch $P 'equip scan: RelativeScale3D' 30)
    $revHost = @(Wait-LoadMatch $H 'local equipment rev ' 30)
    foreach ($i in @($H, $P)) {
        # `targets:` reads "36 byte-verified, 1 relocated-pointer, 6 runtime-validated,
        # 0 mismatched" -- the word after the count is byte-verified, not "verified".  The
        # pattern matches the SHAPE (a count and the refusal count), so a build whose numbers
        # change is still recognised and a MISMATCH still fails.
        $t = @(Wait-LoadMatch $i 'targets: \d+ byte-verified.* 0 mismatched' 30)
        Verdict $step $i ($t.Count -gt 0) 'the build verified its target table (0 mismatched)' $(if ($t.Count) { ([string]$t[-1]).Trim() } else { '' })
        $s = @(Wait-LoadMatch $i 'substrate: globals=1 fname=1' 30)
        Verdict $step $i ($s.Count -gt 0) 'the substrate validated (globals + FName)' $(if ($s.Count) { ([string]$s[-1]).Trim() } else { '' })
        $r = @(Get-LoadMatches $i 'REFUS')
        Verdict $step $i ($r.Count -eq 0) 'the build did not refuse to run' $(if ($r.Count) { ([string]$r[-1]).Trim() } else { '' })
        $sw = @(Get-LoadMatches $i 'switches: ')
        Verdict $step $i ($sw.Count -gt 0) 'the switch set this run used is recorded' $(if ($sw.Count) { ([string]$sw[-1]).Trim() } else { '' })
    }
    # ScanLocalEquip is the SECOND crash site (WINMM+0x6f4d7), and its cache fill is the one
    # line that proves the whole scan body ran to completion.
    Verdict $step $H ($eqHost.Count -gt 0) 'ScanLocalEquip completed its cache fill (the second crash site)' $(if ($eqHost.Count) { ([string]$eqHost[-1]).Trim() } else { '' })
    Verdict $step $P ($eqPeer.Count -gt 0) 'the peer''s ScanLocalEquip completed too' $(if ($eqPeer.Count) { ([string]$eqPeer[-1]).Trim() } else { '' })
    Verdict $step $H ($revHost.Count -gt 0) 'the equipment lane published a revision' $(if ($revHost.Count) { ([string]$revHost[-1]).Trim() } else { '' })
    # The scan lane reads the local pawn THROUGH the weak handle now: if the handle did not
    # resolve, `scan: local at` never appears and every lane below would silently do nothing.
    $m = Get-Mark $H
    Set-Control $H 'qd.scan' '1'
    [void](Check $step $H 'scan: local at ' 40 'the weak-resolved local pawn is readable (scan: local at)' $m)
    $m = Get-Mark $P
    Set-Control $P 'qd.scan' '1'
    [void](Check $step $P 'scan: local at ' 40 'the peer reads its own pawn through the handle' $m)
    # The enemy lane is where EnemyRec::actor lives; the object lane is where the object
    # records live.  Both must answer for the conversion to be called done.
    $m = Get-Mark $H
    Set-Control $H 'qd.enemies' ('smoke-h-' + (Get-Date -Format 'HHmmss'))
    [void](Check $step $H 'enemykey ' 30 'the enemy dump answers (EnemyRec::actor resolves)' $m)
    $m = Get-Mark $H
    Set-Control $H 'qd.obj' ('smoke-obj-' + (Get-Date -Format 'HHmmss'))
    [void](Check $step $H 'rig: object dump -- ' 30 'the object dump answers (object records resolve)' $m)
    # The equipment scan is the OTHER crash site (ScanLocalEquip) and is checked above, from
    # the load log, because it is a one-shot.  Here the live lane is what matters: the pawn is
    # read through the weak handle every 10 s by the scan pump.
    Show-Since $H 'scan: local at|equip scan|local equipment rev|session established' 0 8
}

# ===========================================================================
# main
# ===========================================================================
Say ("session18 start: phase={0} switches={1} repeat={2} soak={3}s" -f $Phase,
     $(if ($ShippedDefaults) { 'SHIPPED' } else { 'DOCUMENTED PLAY' }), $Repeat, $SoakSeconds)
Stop-All
Seed-FreshPair
Start-Boot $Phase
$H = $script:H
$P = $script:P

if ($Phase -eq 'smoke') {
    Invoke-Smoke $H $P 'smoke'
}

if ($Phase -eq 'soak-hub' -or $Phase -eq 'all') {
    Step 'SOAK-HUB -- the R132 crash, repeated back to back in one boot' `
         'Session 17 measured 256 s -> 499 s at this configuration. The claim here is 600 s, twice.'
    for ($run = 1; $run -le $Repeat; $run++) {
        Step ("soak-hub run {0} of {1}" -f $run, $Repeat)
        # The dragon's spawner is where Session 17's soak fought; the anchor is passed the
        # same way so the two sessions are comparable line for line.
        Invoke-OneSoak $H $P ("soakhub{0}" -f $run) $run 'GreenDragon_Spawner'
        if (-not (Test-Alive $H) -or -not (Test-Alive $P)) {
            Note ("  run {0} ended the process; the back-to-back claim cannot be met" -f $run)
            break
        }
    }
}

if ($Phase -eq 'soak-780' -or $Phase -eq 'all') {
    Enter-Area $H $P '780_SZ_Depths' 'SecretSword_Altar_BP' 'soak780'
    Step 'SOAK-780 -- the same body where both machines hold the same authored enemy GUIDs'
    Invoke-OneSoak $H $P 'soak780' 1 $null
}

Step 'SESSION SURVIVAL'
foreach ($i in @($H, $P)) {
    $alive = Test-Alive $i
    $script:verdicts.Add([pscustomobject]@{ Step = 'survival'; Inst = $i; What = 'alive at the end'; Ok = $alive; Line = '' }) | Out-Null
    Note "  $(if ($alive) { 'PASS' } else { 'FAIL' }) [$i] process alive=$alive"
    $fault = Get-Matches $i 'ACCESS_VIOLATION|EXCEPTION|fatal|\bfault'
    Note "  [$i] FAULT lines (ACCESS_VIOLATION/EXCEPTION/fatal): $($fault.Count)"
    $fault | Select-Object -Last 4 | ForEach-Object {
        $s = [string]$_
        if ($s.Length -gt 220) { $s = $s.Substring(0, 220) + ' ...' }
        Note "  [$i] $s"
    }
}
Stop-All

Note ''
Note '=== VERDICTS ==='
foreach ($v in $script:verdicts) {
    Note ("  {0,-10} inst{1}  {2,-6}  {3}  {4}" -f $v.Step, $v.Inst, $(if ($v.Ok) { 'PASS' } else { 'FAIL' }), $v.What, $v.Line)
}
$pass = @($script:verdicts | Where-Object { $_.Ok }).Count
$fail = @($script:verdicts | Where-Object { -not $_.Ok }).Count
Note ("=== {0} PASS, {1} FAIL ===" -f $pass, $fail)
$script:lines | Set-Content -LiteralPath $Report
