# session23.ps1 -- Session 23: the repeated hard world load, the drop the game can be made
# to take, and a soak that is finally long enough.
#
# WHAT THIS SESSION IS FOR
# ------------------------
# Session 22's longer run failed: the PEER died on the SECOND hard return to
# Ruins_Level_00, the fourth world load in that process, 62 minutes in.  The dump's stack
# is the game's own object destruction -- the mod appears only as the ProcessEvent forward
# and as the tick hook's trampoline -- and the faulting instruction is a 32-bit decrement
# on a pointer read out of a container whose backing store had already been freed.  So the
# first thing this session does is a FAST LOOP: N hard world loads back to back in one pair
# of processes, with both machines required to stay alive after each one.  Session 22 could
# only afford four loads in a 30-minute soak; this phase performs eight inside ten minutes.
#
#   hardloop  N consecutive hard world loads (auto -> a real UWorld, then back to
#             Ruins_Level_00), both machines checked alive and in the same world after
#             every load.  This is the reproduction, and it is the proof.
#   loot      R129's take half: one drop, taken on one machine, refused on the other, run
#             in both directions.  Session 22 could not make the GAME consume a drop; the
#             harness now drives it the way the game does.
#   soak      >= 1800 s of measured body: hub fighting, two area changes, two hard travels
#             out and back, and at least one loot take checked on both machines.
#   smoke     the two-direction loot proof, three consecutive area changes, and one hard
#             travel out and back, in about eight minutes.
#   all       smoke then soak.
#
# PHASES (one .ps1 invocation per phase; a launched game does not survive the command that
# started it -- R14)
#   probe     how long each machine's file-poll levers take (the number the rest waits on).
#
# HONESTY RULES INHERITED FROM SESSION 18
# ---------------------------------------
# FAULT and ERR are counted separately (a lane's refused write is not a crash).  A verdict
# is a measurement, not an expectation: every check prints the line it read, and a phase
# that cannot reach its precondition REFUSES to run rather than reporting a number about a
# run that did not happen.  An HP that could not be read is reported as unreadable and is
# NEVER counted as equal.
[CmdletBinding()]
param(
    [ValidateSet('probe', 'hardloop', 'loot', 'smoke', 'soak', 'all')]
    [string]$Phase = 'all',
    [int]$BootSeconds = 300,
    [int]$SoakSeconds = 1800,
    [int]$HardLoads = 8,
    [int]$WarmSeconds = 0,
    [int]$LootRounds = 12,
    [string]$Report = '',
    [switch]$ShippedDefaults
)

$ErrorActionPreference = 'Stop'
$here    = $PSScriptRoot
$repo    = Split-Path -Parent (Split-Path -Parent $here)
$gameDir = 'G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64'
$relay   = Join-Path (Split-Path -Parent $here) 'rust\target\release\qd-relay.exe'
$rigBase = 'G:\Freespace\Queens_Domain\_test'
$rigRoot = Join-Path $rigBase ('s23-' + $Phase)
$seed    = Join-Path $repo 'analysis\save-backup-before-live'
$outDir  = Join-Path $rigRoot 'out'
if (-not $Report) { $Report = Join-Path $outDir ('report-' + $Phase + '.txt') }
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force -Path $outDir | Out-Null }

$script:lines = New-Object System.Collections.Generic.List[string]
# The run-log cache Get-RunLines keeps, keyed by machine index.  See its comment: this is
# what makes a 90 k-line end-of-run scan finish instead of stalling the soak's last step.
$script:runCache = @{}
trap {
    Write-Output ("UNHANDLED: " + $_.Exception.Message)
    Write-Output ("AT: " + $_.InvocationInfo.PositionMessage)
    $script:lines.Add("UNHANDLED: " + $_.Exception.Message) | Out-Null
    $script:lines.Add("AT: " + $_.InvocationInfo.PositionMessage) | Out-Null
    try { Stop-All } catch {}
    try { $script:lines | Set-Content -LiteralPath $Report } catch {}
    exit 1
}
# NOTE: these write to the HOST, not to the pipeline, and that is not cosmetic.
#
# MEASURED this session: `$key = Find-SharedDrop ...` returned an ARRAY of the note strings
# instead of the key, because every `Note` call inside the function was emitted as function
# output -- the same trap session20.ps1 warns about ("Check writes its note to the pipeline,
# so [bool](Check ...) would be true").  The drop phase then probed 'near:drop::0' and the
# log said `no live actor matches 'drop::0'` about a drop it had just found.  A helper that
# both narrates and returns a value cannot afford a single stray object on the pipeline, so
# narration goes to the host stream and the report file (built from $script:lines) and the
# pipeline is reserved for return values.
function Say($m) {
    $s = "[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $m
    Write-Host $s
    $script:lines.Add($s) | Out-Null
}
function Note($m) { Write-Host $m; $script:lines.Add($m) | Out-Null }

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
        Crashes = Join-Path $root ("Saved_{0}\Crashes" -f $suffix)
        Pid = "$root.pid"
    }
}
$R1 = Get-RigPaths 1
$R2 = Get-RigPaths 2
$inst = @{ 1 = $R1; 2 = $R2 }

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
function Get-RunLines([int]$i) {
    $p = $inst[$i].Log
    if (-not (Test-Path -LiteralPath $p)) { return @() }
    # CACHED (Session 22).  Session 21's end-of-run kill diff "did not finish" on a 92 k-line
    # log because every `Get-Since` re-read and re-split the whole file, and the soak made
    # hundreds of those calls.  The cache is keyed on the file LENGTH, so a log that grew
    # invalidates it and a log that did not is read once -- exact, not a timeout.
    $len = (Get-Item -LiteralPath $p).Length
    if ($script:runCache.ContainsKey($i) -and $script:runCache[$i].Len -eq $len) {
        return $script:runCache[$i].Lines
    }
    $all = @(Read-LogLines $p)
    if ($all.Count -eq 0) { return @() }
    $start = 0
    for ($k = $all.Count - 1; $k -ge 0; $k--) {
        if ($all[$k] -match 'hooked FEngineLoop') { $start = $k; break }
    }
    $lines = @($all[$start..($all.Count - 1)])
    $script:runCache[$i] = [pscustomobject]@{ Len = $len; Lines = $lines }
    return $lines
}
function Get-Matches([int]$i, [string]$pattern) {
    return @(Get-RunLines $i | Where-Object { $_ -match $pattern })
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
    # LINEAR APPEND.  `$out += $ln` is O(n) per append, so this function is O(n^2) -- and
    # Session 21's end-of-run scan "did not finish" on a 92 k-line log for exactly that
    # reason, twice: once for the log re-read (fixed by the cache) and once for the append
    # (fixed here).  A 90 k-line window is 4 billion copies with `+=`.
    $src = Get-RunLines $i
    $out = New-Object System.Collections.Generic.List[string]
    foreach ($ln in $src) {
        $t = Get-LineMs ([string]$ln)
        if ($t -ge 0 -and $t -ge $markMs) { $out.Add([string]$ln) }
    }
    return $out.ToArray()
}
function Get-Window([int]$i, [int]$markMs, [switch]$Fresh) {
    return @(Get-Since $i $markMs)
}
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
function Invoke-Go([int]$i, [string]$name, [int]$near = 140) {
    $script:goSeq++
    $yaw = ($script:goSeq * 37) % 360
    Set-Control $i 'qd.near' ([string]$near)
    Set-Control $i 'qd.tp' ('near:' + $name + ',' + $yaw)
}
# The LANE'S OWN key, not a class: `near:drop:<8hex>:<index>` is the lever Session 18 said
# the next session needed (R129), and it is what makes this proof possible at all.
function Invoke-GoDrop([int]$i, [string]$hash, [int]$index, [int]$near = 400, [int]$above = 0) {
    $script:goSeq++
    $yaw = ($script:goSeq * 37) % 360
    Set-Control $i 'qd.near' ([string]$near)
    $spec = 'near:drop:' + $hash + ':' + $index
    if ($above -gt 0) { $spec += (':z' + [string]$above) }
    Set-Control $i 'qd.tp' ($spec + ',' + $yaw + '#' + (Get-Date -Format 'HHmmssfff'))
}
function Invoke-PressDrop([int]$i, [string]$hash, [int]$index) {
    Set-Control $i 'qd.act' ('near:drop:' + $hash + ':' + $index + '#' + (Get-Date -Format 'HHmmssfff'))
}
function Invoke-Hard([int]$i, [string]$map) {
    # A nonce re-arms an unchanged instruction: the first attempt can arrive before the
    # level table is ready, and the lever must be retryable (R147).
    $nonce = Get-Date -Format 'HHmmssfff'
    Set-Control $i 'qd.hard' ($map + '#' + $nonce)
    Say "  hard[$i] -> $map (nonce $nonce)"
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
    $pdb = Join-Path $repo 'mod\native\build\winmm.pdb'
    if (Test-Path -LiteralPath $pdb) {
        # A debugger that has a dump open maps the installed PDB, so the copy can fail the
        # same way the DLL does.  Move the old one aside and keep going: a stale PDB beside a
        # new DLL is worse than none, because a dump's stack would name the wrong function.
        $dstPdb = Join-Path $gameDir 'winmm.pdb'
        try { Copy-Item -LiteralPath $pdb -Destination $dstPdb -Force }
        catch {
            $heldPdb = $dstPdb + '.held-' + (Get-Date -Format 'HHmmss')
            try { Rename-Item -LiteralPath $dstPdb -NewName (Split-Path -Leaf $heldPdb) -Force } catch {}
            Copy-Item -LiteralPath $pdb -Destination $dstPdb -Force
            Say "the old PDB was mapped; moved it aside and installed the new symbols"
        }
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
    } else {
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
    }
    Copy-Item -LiteralPath (Join-Path $gameDir 'qdcoop.ini') -Destination (Join-Path $outDir 'qdcoop-used.ini') -Force
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

    Say "waiting for gameplay on both (up to $BootSeconds s)"
    foreach ($i in @($script:H, $script:P)) {
        $ok = Wait-Gameplay $i $BootSeconds
        Say "inst${i} gameplay=$ok"
        Verdict 'boot' $i $ok 'the machine reached gameplay (it has a pawn to report)' ''
    }
}

# ---------------------------------------------------------------------------
# The world / area a machine is in, read from the mod's own standing line.
# ---------------------------------------------------------------------------
function Get-WorldState([int]$i) {
    $out = [pscustomobject]@{ World = ''; Partner = ''; Agree = ''; Area = '' }
    $rows = @(Get-Matches $i 'world: local=')
    if ($rows.Count -gt 0) {
        $s = [string]$rows[-1]
        if ($s -match 'world: local=(\S*) partner=(\S*) agree=(\d+)') {
            $out.World = $Matches[1]; $out.Partner = $Matches[2]; $out.Agree = $Matches[3]
        }
        if ($s -match 'area=(\S+)$') { $out.Area = $Matches[1] }
        return $out
    }
    $lv = @(Get-Matches $i 'level name: (\S+) from ')
    if ($lv.Count -gt 0 -and [string]$lv[-1] -match 'level name: (\S+) from ') { $out.World = $Matches[1] }
    $pin = @(Get-Matches $i 'rig: hard lever -> (\S+) streamed')
    if ($pin.Count -gt 0 -and [string]$pin[-1] -match 'rig: hard lever -> (\S+) streamed') { $out.Area = $Matches[1] }
    return $out
}

# ---------------------------------------------------------------------------
# The per-key enemy dump, parsed (Session 21: `why=` is the new field)
# ---------------------------------------------------------------------------
# A dump is triggered by REWRITING the control file (its content is the trigger), and the
# lines it prints are `enemykey <key> <state> local=<0|1> src=<n> hp=<v>|? [why=<reason>
# seen=<0|1> inworld=<0|1>] [mirrored=1] cls=<class>`.  Only the dump prints `enemykey`,
# so a mark-to-now window is exactly one dump -- which is why the soak can now sample it
# repeatedly instead of reading one end-of-run answer.
function Invoke-EnemyDump([int]$i, [string]$tag, [int]$timeoutSec = 60) {
    $mark = Get-Mark $i
    $t0 = Get-Date
    Set-Control $i 'qd.enemies' ('s23-' + $tag + '-' + (Get-Date -Format 'HHmmssfff'))
    # WAIT FOR THE LINES, NOT A FIXED SLEEP.  Measured this session (the `probe` phase): a
    # control-file lever answers in 0.5-1.1 s on both machines ONCE WARM, but the FIRST
    # enemy dump of a boot cost 33.1 s -- the per-class UFunction resolution that makes
    # `GetHP` readable is paid once, per class, by whichever dump happens first.  A fixed
    # 3 s read therefore lost the peer's first dump and the loot phase reported "no enemy
    # key alive on both machines" about two machines that each had hundreds.
    $deadline = (Get-Date).AddSeconds($timeoutSec)
    $rows = @()
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 700
        $rows = @(Get-Since $i $mark | Where-Object { $_ -match 'enemykey [0-9A-Fa-f]{8} (ALIVE|DEAD|GONE)' })
        if ($rows.Count -gt 0) { break }
    }
    $elapsed = [math]::Round(((Get-Date) - $t0).TotalSeconds, 1)
    if ($rows.Count -eq 0) {
        Note ("    [{0}] WARN: no enemykey dump within {1}s of the trigger '{2}'" -f $i, $timeoutSec, $tag)
    } elseif ($elapsed -gt 5) {
        Note ("    [{0}] (the dump for '{1}' took {2}s -- first-use cost)" -f $i, $tag, $elapsed)
    }
    $out = @{}
    foreach ($ln in $rows) {
        $s = [string]$ln
        if ($s -notmatch 'enemykey ([0-9A-Fa-f]{8}) (ALIVE|DEAD|GONE)') { continue }
        $k = $Matches[1].ToUpperInvariant(); $st = $Matches[2]
        $hp = $null
        if ($s -match '\shp=([-0-9.]+)') { $hp = [double]$Matches[1] }
        $why = ''
        if ($s -match ' why=(\w+)') { $why = $Matches[1] }
        $seen = ''; if ($s -match ' seen=(\d)') { $seen = $Matches[1] }
        $iw = ''; if ($s -match ' inworld=(\d)') { $iw = $Matches[1] }
        $cls = ''; if ($s -match ' cls=(\S+)') { $cls = $Matches[1] }
        # Session 23: `mx` (the copy's own MaxHP) and `peers` (how many OTHER records answer
        # for this key here) are what make a duplicate visible.  Two bodies for one key with
        # different MaxHP sit at the same fraction, so they cannot converge and their absolute
        # HP is permanently different -- that is not a desync, and the comparison must skip it.
        $mx = $null; if ($s -match '\smx=([-0-9.]+)') { $mx = [double]$Matches[1] }
        $peers = 0;  if ($s -match '\speers=(\d+)')  { $peers = [int]$Matches[1] }
        $out[$k] = [pscustomobject]@{
            Key = $k; State = $st; Hp = $hp; Why = $why; Seen = $seen; InWorld = $iw
            Cls = $cls; Mx = $mx; Peers = $peers; Line = $s.Trim()
        }
    }
    return $out
}

# Compare the shared live keys.  An unreadable HP is NOT a comparison: it is counted
# separately, with the reason the mod printed.  This is the rule Session 20's end-of-run
# table could not state, because the end-of-run dump was the only sample it had.
function Compare-SharedHp([hashtable]$dh, [hashtable]$dp, [string]$tag) {
    $out = [pscustomobject]@{
        Tag = $tag; AliveBoth = 0; Compared = 0; Unequal = 0
        Unreadable = 0; Ghosts = 0; Duplicates = 0; DupKeys = @(); Whys = @{}; Mismatches = @()
    }
    foreach ($k in @($dh.Keys)) {
        if (-not $dp.ContainsKey($k)) { continue }
        # GONE (the engine has marked the copy for destruction) is neither alive nor dead:
        # it has no HP to compare and it is not a death, so it is counted and skipped.
        if ($dh[$k].State -eq 'GONE' -or $dp[$k].State -eq 'GONE') { $out.Ghosts++; continue }
        if ($dh[$k].State -ne 'ALIVE' -or $dp[$k].State -ne 'ALIVE') { continue }
        $out.AliveBoth++
        # A KEY WITH MORE THAN ONE BODY IS NOT COMPARABLE (Session 23, R151).  `peers` is the
        # module's own count of OTHER records answering for the key, so this is the module's
        # answer, not a guess made here: two bodies with one key and different MaxHP cannot
        # converge (the convergence rule compares fractions) and their absolute difference is
        # permanent.  Counting that as a mismatch is what made Session 22 report a standing
        # `530F7FD0: host=140 peer=30` about one enemy that never disagreed with itself.
        if (($dh[$k].Peers -gt 0) -or ($dp[$k].Peers -gt 0)) {
            $out.Duplicates++
            if ($out.DupKeys -notcontains $k) { $out.DupKeys += $k }
            continue
        }
        $a = $dh[$k].Hp; $b = $dp[$k].Hp
        if ($null -eq $a -or $null -eq $b) {
            $out.Unreadable++
            $w = ''
            if ($null -eq $a -and $null -eq $b) { $w = "$($dh[$k].Why)/$($dp[$k].Why)" }
            elseif ($null -eq $a) { $w = $dh[$k].Why }
            else { $w = $dp[$k].Why }
            if (-not $w) { $w = '?' }
            if ($out.Whys.ContainsKey($w)) { $out.Whys[$w]++ } else { $out.Whys[$w] = 1 }
            continue
        }
        $out.Compared++
        if ([math]::Abs($a - $b) -ge 0.5) {
            $out.Unequal++
            $out.Mismatches += ("{0}: host={1} peer={2}" -f $k, $a, $b)
        }
    }
    return $out
}
function Format-Whys($whys) {
    if (-not $whys -or $whys.Count -eq 0) { return '-' }
    return (($whys.GetEnumerator() | Sort-Object Value -Descending |
             ForEach-Object { "$($_.Key)=$($_.Value)" }) -join ' ')
}

# ===========================================================================
# R129 -- the loot drop: taken on one machine, hidden on the other, and refused
# ===========================================================================
function Get-DropKeys([int]$i, [int]$mark) {
    $out = New-Object System.Collections.Generic.List[object]
    foreach ($ln in @(Get-Since $i $mark)) {
        $s = [string]$ln
        if ($s -notmatch 'loot: drop ([0-9A-Fa-f]{8})#(\d+)') { continue }
        $h = $Matches[1].ToUpperInvariant(); $ix = [int]$Matches[2]
        $have = $false
        foreach ($e in $out) { if ($e.Hash -eq $h -and $e.Index -eq $ix) { $have = $true; break } }
        if (-not $have) { $out.Add([pscustomobject]@{ Hash = $h; Index = $ix }) | Out-Null }
    }
    return $out
}

# Does THIS machine hold a live copy of that key?  The answer is the lane's own resolver:
# `rig: drop <key> resolved to a live actor` versus `... is already gone on this machine`
# versus `rig: no live loot-ledger drop matches`.  Standing 400 cm away so the probe cannot
# be mistaken for a pickup.
function Test-DropHeld([int]$i, [string]$hash, [int]$index, [int]$timeoutSec = 20) {
    $mark = Get-Mark $i
    Invoke-GoDrop $i $hash $index 400
    $held = @(); $gone = @()
    $deadline = (Get-Date).AddSeconds($timeoutSec)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 700
        $held = @(Get-Since $i $mark | Where-Object { $_ -match ('rig: drop ' + $hash + '#' + $index + ' resolved to a live actor') })
        if ($held.Count -gt 0) { break }
        $gone = @(Get-Since $i $mark | Where-Object { $_ -match ('rig: drop ' + $hash + '#' + $index + ' is already gone') })
        if ($gone.Count -gt 0) { break }
    }
    return [pscustomobject]@{
        Held = ($held.Count -gt 0); Gone = ($gone.Count -gt 0)
        Line = $(if ($held.Count) { ([string]$held[-1]).Trim() } elseif ($gone.Count) { ([string]$gone[-1]).Trim() } else { '' })
    }
}

# Stand ON the drop (a pickup is an overlap, not a verb) and then run the game's own
# Interact input event on that exact actor as the fallback the interaction lever documents.
function Invoke-TakeDrop([int]$i, [string]$hash, [int]$index) {
    # A PICKUP IS AN OVERLAP AND AN OVERLAP FIRES ON *BEGIN*.  Session 21 measured the
    # consequence (the killer's own pawn consumed the drop because it was standing there)
    # and Session 22 measured the converse: a pawn parked exactly on a `Herb_Item_BP_C` for
    # thirty seconds picked nothing up, because it was already inside the volume when it
    # arrived, so no BeginOverlap was generated -- and a single teleport into it produced
    # none either.  So the taker STEPS OUT and back IN, three times.
    $consumed = ('loot: local pickup ' + $hash + '#' + [string]$index)
    # DROP ONTO IT FIRST.  A teleport into a volume generates no BeginOverlap (measured:
    # three out-and-back teleports, and a pawn parked on the drop for thirty seconds, all
    # consumed nothing), and the class' `Interact` and `Pickup` verbs run and consume
    # nothing either.  Falling through it is a real move.
    foreach ($h in @(300, 700)) {
        $mark = Get-Mark $i
        Invoke-GoDrop $i $hash $index 0 $h
        if (@(Wait-Since $i $consumed $mark 14).Count -gt 0) {
            Say ("  act[$i] -> fell {0} cm onto near:drop:{1}:{2}; the overlap consumed it" -f $h, $hash, $index)
            return
        }
    }
    foreach ($out in @(500, 800, 250)) {
        $mark = Get-Mark $i
        Invoke-GoDrop $i $hash $index $out
        Start-Sleep -Seconds 2
        Invoke-GoDrop $i $hash $index 0
        if (@(Wait-Since $i $consumed $mark 12).Count -gt 0) {
            Say ("  act[$i] -> out to {0} cm and back onto near:drop:{1}:{2}; the overlap consumed it" -f $out, $hash, $index)
            return
        }
    }
    # Last resort: the interaction lever, which is what a key press runs.
    $mark2 = Get-Mark $i
    Invoke-PressDrop $i $hash $index
    $null = Wait-Since $i $consumed $mark2 12
    # And leave the evidence for the next session: the drop's own UFunction surface, read
    # rather than guessed.  Nothing in the class is manufactured here.
    Set-Control $i 'qd.efunc' ('drop:' + $hash + ':' + [string]$index)
    Start-Sleep -Seconds 3
    Say ("  act[$i] -> near:drop:{0}:{1} (stepping out and back did not consume it; the Interact event was the last resort, and the class surface was dumped)" -f $hash, $index)
}

function Find-SharedDrop([int]$H, [int]$P, $skip, [int]$mark, [int]$maxProbe = 6) {
    $keys = @(Get-DropKeys $H $mark) + @(Get-DropKeys $P $mark)
    $seen = @{}
    $probed = 0
    foreach ($k in $keys) {
        $id = "$($k.Hash)#$($k.Index)"
        if ($seen.ContainsKey($id)) { continue }
        $seen[$id] = $true
        if (@($skip) -contains $id) { continue }
        if ($probed -ge $maxProbe) { Note ("    (stopped after {0} probes; the remaining keys were not tested)" -f $probed); break }
        $probed++
        # Probe BOTH machines for every key: a key only one of them holds is the state the
        # phase has to report, and skipping the second probe hides which side is missing.
        $rh = Test-DropHeld $H $k.Hash $k.Index
        $rp = Test-DropHeld $P $k.Hash $k.Index
        if ($rh.Held -and $rp.Held) {
            Note ("    key {0}: BOTH machines hold it" -f $id)
            return $k
        }
        Note ("    key {0}: host={1} peer={2} ({3})" -f $id,
              $(if ($rh.Held) { 'HELD' } elseif ($rh.Gone) { 'gone' } else { 'absent' }),
              $(if ($rp.Held) { 'HELD' } elseif ($rp.Gone) { 'gone' } else { 'absent' }),
              $(if ($rh.Line) { $rh.Line } else { $rp.Line }))
    }
    return $null
}

# Roll more drops by killing: the drop is the dying enemy's own Blueprint's, so both
# machines roll it locally and hold a copy -- which is exactly the precondition hiding
# needs.
#
# WHY `qd.enemykill` AND NOT `qd.hit`.  Measured (Session 14, recorded in the lever's own
# comment): 123 `qd.hit` swings over 120 s in the hub produced `ran=0 hp 0.0 -> 0.0` every
# time -- most hub enemy classes have no `TryApplyDamage` this module can build a frame
# for -- so there was no death and therefore no drop.  The first loot attempt of this
# session reproduced it exactly:
#
#     rig: hit enemy guid=A8D5C9E1 amount=400 ran=0 hp 0.0 -> 0.0 (local-run)
#
# `qd.enemykill <8hex>` calls the enemy's OWN `Kill`/`Die`, which runs the game's death
# path, and the drop roll is part of that path.  It is applied on BOTH machines so each
# copy dies through its own Blueprint and rolls its own local drop under the same key --
# which is what the hiding half needs to have anything to hide.
function Invoke-KillRounds([int]$H, [int]$P, [int]$rounds, [int]$mark) {
    for ($r = 1; $r -le $rounds; $r++) {
        # THE DAMAGE PATH FIRST.  Measured this session in the hub: `qd.enemykill` (the
        # enemy's own Kill) runs the death but NOT the drop roll on the machine that calls
        # it -- the drop always appeared on the OTHER machine, whose copy died through the
        # mod's damage convergence.  So a kill that rolls a drop HERE has to be a real
        # damage hit (`qd.hit`), which needs a class that implements TryApplyDamage: the
        # hub's mostly do not (`ran=0 hp 0.0 -> 0.0`, Session 14), 780_SZ_Depths' do.
        Note ("    round {0}: hitting the nearest enemy on both machines (damage, the path the drop roll runs on)" -f $r)
        # STAND BACK BEFORE THE BLOW.  Twice measured this session: the drop actor appears
        # only on the machine that did NOT land the killing blow (4 drops from a host kill
        # all on the peer; 3 hub drops from a peer kill all on the host).  The obvious
        # explanation is that the killer's pawn is standing 200 cm from the corpse, the drop
        # spawns at its feet, and the killer's OWN overlap consumes it before the 1/30 s
        # scan slice ever sees the actor -- so the killer never holds the drop it caused.
        # A pickup is an overlap: at 9 m there is nothing to overlap, and the drop has to
        # wait for whoever walks over it.
        Invoke-Go $H 'enemy' 900
        Invoke-Go $P 'enemy' 900
        Start-Sleep -Seconds 6
        Set-Control $H 'qd.hit' '100000'
        Set-Control $P 'qd.hit' '100000'
        Start-Sleep -Seconds 7
        Set-Control $H 'qd.hit' $null
        Set-Control $P 'qd.hit' $null
        Start-Sleep -Seconds 5
        $all = @(Get-DropKeys $H $mark) + @(Get-DropKeys $P $mark)
        $uniq = @{}
        foreach ($k in $all) { $uniq["$($k.Hash)#$($k.Index)"] = $true }
        Note ("    after round {0}: {1} distinct drop key(s); loot: drop lines host={2} peer={3}; merged host={4} peer={5}" -f
              $r, $uniq.Count,
              @(Get-Since $H $mark | Where-Object { $_ -match 'loot: drop ' }).Count,
              @(Get-Since $P $mark | Where-Object { $_ -match 'loot: drop ' }).Count,
              @(Get-Since $H $mark | Where-Object { $_ -match 'loot: merged ' }).Count,
              @(Get-Since $P $mark | Where-Object { $_ -match 'loot: merged ' }).Count)
        if ($uniq.Count -ge 2) { return $uniq.Count }
        Invoke-Go $H 'enemy' 200
        Invoke-Go $P 'enemy' 200
        Start-Sleep -Seconds 4
    }
    # The `Kill` fallback, for a class whose TryApplyDamage this module cannot build a frame
    # for.  It rolls the drop on the OTHER machine, which is still a drop both machines can
    # end up holding once that machine's copy dies.
    for ($r = 1; $r -le 2; $r++) {
        $dh = Invoke-EnemyDump $H ('kill{0}-h' -f $r)
        $hostKeys = @($dh.Keys | Where-Object { $dh[$_].State -eq 'ALIVE' } | Select-Object -First 3)
        if ($hostKeys.Count -eq 0) { break }
        foreach ($k in $hostKeys) {
            Set-Control $H 'qd.enemykill' $k
            Note ("      enemykill {0} on the HOST (fallback for a class with no usable TryApplyDamage)" -f $k)
            Start-Sleep -Seconds 5
        }
        $all = @(Get-DropKeys $H $mark) + @(Get-DropKeys $P $mark)
        $uniq = @{}
        foreach ($k in $all) { $uniq["$($k.Hash)#$($k.Index)"] = $true }
        Note ("    fallback round {0}: {1} distinct drop key(s)" -f $r, $uniq.Count)
        if ($uniq.Count -gt 0) { return $uniq.Count }
    }
    return 0
}

function Invoke-LootDirection([int]$H, [int]$P, [int]$taker, [int]$other, [string]$step, [string]$dir, $skip, [int]$mark) {
    Step ("R129 {0} -- the {1} takes a drop both machines hold; the {2} must hide its copy" -f
          $dir, $(if ($taker -eq $H) { 'HOST' } else { 'PEER' }), $(if ($other -eq $H) { 'HOST' } else { 'PEER' })) `
         'The tombstone is the lane''s own key {source_hash, drop_index}; the lever that addresses it is qd.tp near:drop:<8hex>:<index>.'
    $key = $null
    for ($attempt = 1; $attempt -le 3 -and -not $key; $attempt++) {
        $key = Find-SharedDrop $H $P $skip $mark
        if ($key) { break }
        # A drop only ONE machine holds is the normal first state: the machine that did not
        # do the Kill rolls its own copy when the death reaches it, and its object scan needs
        # a pass to notice the actor.  So wait, look again, and only then kill more.
        Note ("  attempt {0}: no drop both machines hold yet; waiting 10 s for the death to reach the other copy" -f $attempt)
        Start-Sleep -Seconds 10
        $key = Find-SharedDrop $H $P $skip $mark
        if ($key) { break }
        if ($attempt -lt 3) { Invoke-KillRounds $H $P 2 $mark }
    }
    if (-not $key) {
        Verdict $step $H $false 'a drop BOTH machines hold was found' 'no shared drop key exists; hiding cannot be measured'
        return $null
    }
    $id = "$($key.Hash)#$($key.Index)"
    Note ("  the drop under test is {0}" -f $id)

    $mTake = Get-Mark $taker
    $mOther = Get-Mark $other
    Invoke-TakeDrop $taker $key.Hash $key.Index
    $pick = Wait-Since $taker ('loot: local pickup ' + $key.Hash + '#' + [string]$key.Index) $mTake 30
    Verdict $step $taker ($pick.Count -gt 0) 'the taker took the drop (its ledger tombstoned it)' $(if ($pick.Count) { ([string]$pick[-1]).Trim() } else { '' })
    if ($pick.Count -eq 0) {
        Show-Since $taker 'loot:|dev interact|rig: drop|rig: near' $mTake 20
        return $id
    }

    $hide = Wait-Since $other ('loot: hiding local drop ' + $key.Hash + '#' + [string]$key.Index) $mOther 40
    Verdict $step $other ($hide.Count -gt 0) 'the OTHER machine hid its own copy (the measured hiding half)' $(if ($hide.Count) { ([string]$hide[-1]).Trim() } else { '' })
    if ($hide.Count -eq 0) {
        Show-Since $other 'loot:|rig: drop|rig: near|dev interact' $mOther 20
    }
    # The hiding half must be a HIDE, not a disappearance that happened anyway: the same
    # machine still resolves the key and now says it is gone.
    $after = Test-DropHeld $other $key.Hash $key.Index
    Verdict $step $other $after.Gone 'the other machine''s copy is GONE to its own resolver' $after.Line

    # ---- taken twice? -----------------------------------------------------
    # The claim is not "the line is missing": a second take must be REFUSED, and the
    # ledger must still hold one tombstone, not two entries.
    $m2 = Get-Mark $other
    $before = @(Get-Since $other $m2 | Where-Object { $_ -match ('loot: local pickup ' + $key.Hash + '#' + [string]$key.Index) }).Count
    Invoke-TakeDrop $other $key.Hash $key.Index
    Start-Sleep -Seconds 8
    $after2 = @(Get-Since $other $m2 | Where-Object { $_ -match ('loot: local pickup ' + $key.Hash + '#' + [string]$key.Index) }).Count
    $refused = @(Get-Since $other $m2 | Where-Object { $_ -match ('rig: drop ' + $key.Hash + '#' + [string]$key.Index + ' is already gone') })
    Verdict $step $other (($after2 - $before) -eq 0) 'the other machine CANNOT take the drop a second time' ("new pickup lines=$($after2 - $before)")
    Verdict $step $other ($refused.Count -gt 0) 'the second attempt was refused by name (already gone)' $(if ($refused.Count) { ([string]$refused[-1]).Trim() } else { '' })
    # And the drop was not resurrected by the taker's own 1 Hz body.
    $m3 = Get-Mark $other
    Start-Sleep -Seconds 6
    $reoff = @(Get-Since $other $m3 | Where-Object { $_ -match ('loot: drop ' + $key.Hash + '#' + [string]$key.Index) })
    Verdict $step $other ($reoff.Count -eq 0) 'the tombstone was not resurrected by the next state body' ("re-offer lines=$($reoff.Count)")
    return $id
}

function Invoke-LootDrop([int]$H, [int]$P) {
    Step 'R129 -- loot hiding, both directions, in 780_SZ_Depths' `
         'A drop both machines hold; one takes it; the other must hide its copy and refuse a second take.'
    # WHY THE AREA AND NOT THE HUB.  A drop's key is {source_hash, drop_index}, and the
    # source is the enemy.  In the hub each machine runs its OWN spawner enemies with its own
    # authored GUIDs, so the two machines' drop keys are disjoint -- measured this session:
    # three drops the host held and two the peer held, not one key in common
    # (`rig: no live loot-ledger drop matches ... (the scan has 2 drop(s))` on the peer for
    # every host key).  PLAYTEST section 8 already says loot hiding needs the SAME enemy on
    # both machines.  `780_SZ_Depths` is where both machines hold the same authored enemy
    # GUIDs (Sessions 14-20), so that is where this proof has to be made.
    $enterOk = Enter-Area $H $P '780_SZ_Depths' 'SecretSword_Altar_BP' 'loot-area'
    foreach ($v in @($script:verdicts | Where-Object { $_.Step -eq 'loot-area' })) { if (-not $v.Ok) { $enterOk = $false } }
    if (-not $enterOk) {
        Verdict 'r129-roll' $H $false 'the loot proof was run' 'area entry did not converge; refusing to measure drops in two different places'
        return
    }
    $mark = Get-Mark $H
    Invoke-Go $H 'enemy' 200
    Invoke-Go $P 'enemy' 200
    # SETTLE.  Measured: a dump 10 s after gameplay can find 0 callable enemies -- the
    # level's initial spawn set is still being torn down and rebuilt.
    Note '  settling 20 s so the area''s spawn set is live and callable before the first hit'
    Start-Sleep -Seconds 20
    $rolled = Invoke-KillRounds $H $P $LootRounds $mark
    $all = @(Get-DropKeys $H $mark) + @(Get-DropKeys $P $mark)
    $uniq = @{}
    foreach ($k in $all) { $uniq["$($k.Hash)#$($k.Index)"] = $true }
    Note ("  drops rolled this run: {0} distinct key(s): {1}" -f $uniq.Count, (($uniq.Keys | Sort-Object) -join ' '))
    Verdict 'r129-roll' $H ($uniq.Count -gt 0) 'killing an enemy rolled a drop (the lane observed a key)' ("keys=$($uniq.Count)")
    if ($uniq.Count -eq 0) { return }

    $used = @()
    $id1 = Invoke-LootDirection $H $P $H $P 'r129-host' 'direction 1 (HOST takes)' $used $mark
    if ($id1) { $used += $id1 }
    $id2 = Invoke-LootDirection $H $P $P $H 'r129-peer' 'direction 2 (PEER takes)' $used $mark
    if ($id2) { $used += $id2 }
    Note ("  drops exercised: {0}" -f ($used -join ' '))
}

# ===========================================================================
# PROBE -- how long each machine's file-poll levers actually take
# ===========================================================================
# Measured the hard way this session: a 3 s read after writing `qd.enemies` found the
# host's dump and nothing on the peer, and a `qd.tp` written at 11:58:49 was applied at
# 11:59:05.  Every lever in the harness is a file whose CONTENT the game polls, so the
# latency is a property of the run, not of the scenario -- and a harness that assumes
# 3 s turns a slow machine into a failed claim.  This phase measures it, for both kinds
# of lever, and prints the number the rest of the harness waits on.
function Invoke-Probe([int]$H, [int]$P) {
    Step 'PROBE -- file-lever latency on each machine' `
         'qd.enemies (a dump) and qd.tp (a teleport) are both control files the game polls; the 3 s assumption is what lost the peer''s dump.'
    foreach ($i in @($H, $P)) {
        for ($k = 1; $k -le 3; $k++) {
            $mark = Get-Mark $i
            $t0 = Get-Date
            Set-Control $i 'qd.enemies' ('probe-{0}-{1}' -f $k, (Get-Date -Format 'HHmmssfff'))
            $secs = -1
            $deadline = (Get-Date).AddSeconds(40)
            while ((Get-Date) -lt $deadline) {
                Start-Sleep -Milliseconds 400
                $hit = @(Get-Since $i $mark | Where-Object { $_ -match 'enemykey [0-9A-Fa-f]{8} (ALIVE|DEAD|GONE)' })
                if ($hit.Count -gt 0) { $secs = [math]::Round(((Get-Date) - $t0).TotalSeconds, 1); break }
            }
            Note ("  [{0}] dump {1}: answered after {2}s{3}" -f $i, $k, $secs, $(if ($secs -lt 0) { ' (NEVER within 40 s)' } else { '' }))
            Verdict 'probe' $i ($secs -ge 0 -and $secs -le 20) 'the enemy dump answers within 20 s of the trigger' ("dump $k after ${secs}s")

            $mark2 = Get-Mark $i
            $t1 = Get-Date
            Invoke-Go $i 'enemy' 200
            $secs2 = -1
            $deadline = (Get-Date).AddSeconds(40)
            while ((Get-Date) -lt $deadline) {
                Start-Sleep -Milliseconds 400
                $hit = @(Get-Since $i $mark2 | Where-Object { $_ -match 'dev teleport near |dev teleport file ->' })
                if ($hit.Count -gt 0) { $secs2 = [math]::Round(((Get-Date) - $t1).TotalSeconds, 1); break }
            }
            Note ("  [{0}] teleport {1}: answered after {2}s{3}" -f $i, $k, $secs2, $(if ($secs2 -lt 0) { ' (NEVER within 40 s)' } else { '' }))
            Verdict 'probe' $i ($secs2 -ge 0 -and $secs2 -le 20) 'the teleport lever answers within 20 s of the write' ("teleport $k after ${secs2}s")
        }
    }
}

# ===========================================================================
# The soak body: fight, move, and SAMPLE the per-key dump through the run
# ===========================================================================
$script:hpSamples = New-Object System.Collections.Generic.List[object]
# The MEASURED length of each segment, so the soak can report "body" as a sum of what ran
# rather than as a budget it hoped for.  A segment that a dead process cut short records the
# short number, which is the only honest one.
$script:segmentSeconds = @{}

function Invoke-SoakSegment([int]$H, [int]$P, [string]$label, [int]$seconds, [string]$anchor, [int]$sampleEvery = 75) {
    $start = Get-Date
    $end = $start.AddSeconds($seconds)
    $m0 = Get-Mark $H
    $n = 0
    $died = $false
    $lastSample = Get-Date
    $anchors = @('enemy', 'enemy', 'chest', 'enemy', 'npc')
    while ((Get-Date) -lt $end) {
        if (-not (Test-Alive $H) -or -not (Test-Alive $P)) { $died = $true; break }
        $n++
        Set-Control $H 'qd.hit' ([string](200 + ($n % 3) * 100))
        Set-Control $P 'qd.hit' ([string](200 + ($n % 3) * 100))
        # MOVEMENT: a re-anchor every other iteration, cycling the kinds of thing the rig
        # can walk to, so the run is not a stationary turret test.
        if (($n % 2) -eq 0) {
            $kind = $anchors[[int](($n / 2) % $anchors.Count)]
            Invoke-Go $H $kind 200
            Invoke-Go $P $kind 200
        }
        if (((Get-Date) - $lastSample).TotalSeconds -ge $sampleEvery) {
            $lastSample = Get-Date
            $dh = Invoke-EnemyDump $H ($label + '-h-' + $n)
            $dp = Invoke-EnemyDump $P ($label + '-p-' + $n)
            $cmp = Compare-SharedHp $dh $dp ("{0} t+{1}s" -f $label, [int]((Get-Date) - $start).TotalSeconds)
            $script:hpSamples.Add($cmp) | Out-Null
            # The label/index pairs are spelled out one per line because the first version
            # had them off by one and every printed number was its neighbour -- a sample read
            # "shared-alive=378 compared=0 unequal=0" when what it meant was elapsed=378 s,
            # shared-alive=0. A soak number nobody can attribute is worse than no number.
            Note ("    hp sample {0}: shared-alive={1} compared={2} unequal={3} unreadable={4} ghosts={5} duplicates={7} why={6}" -f
                  $cmp.Tag, $cmp.AliveBoth, $cmp.Compared,
                  $cmp.Unequal, $cmp.Unreadable, $cmp.Ghosts, (Format-Whys $cmp.Whys), $cmp.Duplicates)
            foreach ($mm in ($cmp.Mismatches | Select-Object -First 4)) { Note ("      HP MISMATCH " + $mm) }
        }
        Start-Sleep -Seconds 3
    }
    $elapsed = [int]((Get-Date) - $start).TotalSeconds
    $script:segmentSeconds[$label] = $elapsed
    Set-Control $H 'qd.hit' $null
    Set-Control $P 'qd.hit' $null
    Note ("  segment '{0}': {1} s of {2} s, iterations={3}" -f $label, $elapsed, $seconds, $n)
    Verdict ("seg-" + $label) $H (-not $died) 'both processes survived the segment' $(if ($died) { "a process died after ${elapsed}s" } else { "${elapsed}s" })
    if ($anchor) {
        Invoke-Go $H $anchor 400
        Invoke-Go $P $anchor 400
        Start-Sleep -Seconds 8
    }
}

function Enter-Area([int]$H, [int]$P, [string]$area, [string]$anchor, [string]$step) {
    Step "$step -- the HOST enters $area; the PEER must follow" `
         'R147/R150a: entering an area is a streamed sublevel, so the signal is TP_AREA on the travel lane. A machine that only re-announces the area it ALREADY holds is not a follow, which is why "already pinned" is not accepted here (Session 21 counted it and reported a pass for a pair that never moved).'
    $mH = Get-Mark $H
    $mP = Get-Mark $P
    Invoke-Hard $H $area
    [void](Check $step $H ('rig: hard lever -> ' + [regex]::Escape($area) + ' streamed=1') 90 'the HOST streamed the area locally' $mH)
    $hostOk = [bool]$script:verdicts[$script:verdicts.Count - 1].Ok
    if (-not $hostOk) {
        Verdict $step $P $false 'the PEER follows the host into the area' 'the host never entered it'
        return $false
    }
    # ONLY the follow counts.  `travel: area <x> is already pinned` is the refusal R150a named.
    $follow = @(Wait-Since $P ('travel: entered the partner.s area ' + [regex]::Escape($area)) $mP 90)
    Verdict $step $P ($follow.Count -gt 0) 'the PEER followed the host into the area' $(if ($follow.Count) { ([string]$follow[-1]).Trim() } else { '' })
    if ($follow.Count -eq 0) {
        Show-Since $P 'travel:|level union|rig: hard' $mP 10
        return $false
    }
    [void](Check $step $H ('rig: level engine state ' + [regex]::Escape($area) + ' LoadedLevel=1 actuallyVisible=1') 90 'the area is LOADED and SHOWN on the host' $mH)
    [void](Check $step $P ('rig: level engine state ' + [regex]::Escape($area) + ' LoadedLevel=1 actuallyVisible=1') 90 'the area is LOADED and SHOWN on the peer' $mP)
    Start-Sleep -Seconds 6
    if ($anchor) {
        Invoke-Go $H $anchor 400
        Invoke-Go $P $anchor 400
        Start-Sleep -Seconds 8
    }
    return $true
}

# R150a: leaving an area.  The pin used to be a one-way latch and a leave was never
# announced, so "back to the hub" could not happen at all.
function Leave-Area([int]$H, [int]$P, [string]$step) {
    Step "$step -- the HOST leaves its area; the PEER must leave it too" `
         'R150a: the announcement used to run only while the pin was non-empty, so a machine could enter an area and never come out. The leave is announced as dest="-" and ClearAreaPin hides the sublevel through the same lever the pin used.'
    $mH = Get-Mark $H
    $mP = Get-Mark $P
    # `#nonce` so a second leave in one session is not ignored as "the same instruction"
    # (measured: the soak's leave was a no-op because the smoke phase had already written
    # exactly `-`).
    Set-Control $H 'qd.level' ('-#' + (Get-Date -Format 'HHmmssfff'))
    $left = @(Wait-Since $H 'travel: left the area ' $mH 60)
    Verdict $step $H ($left.Count -gt 0) 'the HOST left its area' $(if ($left.Count) { ([string]$left[-1]).Trim() } else { '' })
    $ann = @(Wait-Since $H 'travel: area announcement - ' $mH 60)
    Verdict $step $H ($ann.Count -gt 0) 'the HOST announced the leave' $(if ($ann.Count) { ([string]$ann[-1]).Trim() } else { '' })
    $follow = @(Wait-Since $P 'travel: left the area |travel: the partner left' $mP 90)
    Verdict $step $P ($follow.Count -gt 0) 'the PEER left its area' $(if ($follow.Count) { ([string]$follow[-1]).Trim() } else { '' })
    Start-Sleep -Seconds 4
    $wH = Get-WorldState $H; $wP = Get-WorldState $P
    Verdict $step $P ($wH.Area -eq $wP.Area) 'both machines hold the SAME area after the leave' ("host='$($wH.Area)' peer='$($wP.Area)'")
    return ($follow.Count -gt 0)
}

function Invoke-HardTravel([int]$H, [int]$P, [string]$map, [string]$step) {
    Step "$step -- HARD travel to $map, and the partner must land in it too" `
         'G6: the sender announces HARD_BEGIN, the partner loads the same world through GameInstance.LoadMapWithLoadingScreen, and the host releases the pair only when both arrived.'
    # A NAME THE HUB STREAMS IS NOT A HARD TRAVEL.  `FollowHardTravel` checks the level table
    # first (StreamTravelTo) and a name that is a sublevel of this world is loaded and SHOWN
    # instead of being loaded as a world.  So the step reports which happened: `streamed=1`
    # is a named refusal of the hard path, not a failure of the handshake.  Callers pass the
    # candidates in order, and only a candidate that really announced HARD_BEGIN counts.
    foreach ($cand in @($map -split ',')) {
        $m = $cand.Trim()
        if (-not $m) { continue }
        $mH = Get-Mark $H
        $mP = Get-Mark $P
        Invoke-Hard $H $m
        # `auto` (R150b) makes the MOD pick the world: every name the rig used to try was a
        # sublevel of the world it stood in, so the hard path was never reached.  The lever
        # prints which candidate it picked; the step reads it and checks THAT name.
        $lever = @(Wait-Since $H ('rig: hard lever -> ' + [regex]::Escape($m)) $mH 90)
        if ($lever.Count -eq 0) {
            Note ("  [{0}] the hard lever printed nothing for {1}" -f $H, $m)
            continue
        }
        $leverLine = ([string]$lever[-1]).Trim()
        Note ("  [$H] " + $leverLine)
        $chosen = $m
        if ($m -eq 'auto') {
            $chosen = ''
            foreach ($ln in $lever) {
                if ([string]$ln -match 'auto picked (\S+)') { $chosen = $Matches[1]; break }
            }
            if (-not $chosen) {
                Verdict $step $H $false 'the HOST performed a HARD transition' 'auto found no real UWorld this machine is not already in'
                continue
            }
            Note ("  [$H] auto chose {0}" -f $chosen)
        }
        if ($leverLine -match 'streamed=1') {
            Verdict $step $H $false "the HOST performed a HARD transition to $chosen" 'streamed as a sublevel of this world -- not a hard travel; trying the next candidate'
            continue
        }
        $ann = @(Wait-Since $H ('travel: HARD transition to ' + [regex]::Escape($chosen)) $mH 60)
        Verdict $step $H ($ann.Count -gt 0) "the host announced the hard transition to $chosen" $(if ($ann.Count) { ([string]$ann[-1]).Trim() } else { '' })
        $loaded = @(Wait-Since $P ('travel: hard transition: loading ' + [regex]::Escape($chosen)) $mP 120)
        Verdict $step $P ($loaded.Count -gt 0) "the peer loaded $chosen" $(if ($loaded.Count) { ([string]$loaded[-1]).Trim() } else { '' })
        $both = @(Wait-Since $H ('travel: both machines arrived in ' + [regex]::Escape($chosen)) $mH 120)
        Verdict $step $H ($both.Count -gt 0) "both machines arrived in $chosen (the host released the pair)" $(if ($both.Count) { ([string]$both[-1]).Trim() } else { '' })
        Start-Sleep -Seconds 12
        $wH = Get-WorldState $H; $wP = Get-WorldState $P
        $ok = ($wH.World -eq $chosen -and $wP.World -eq $chosen)
        Verdict $step $P $ok "both machines report $chosen" ("host=$($wH.World) peer=$($wP.World)")
        if ($ok) { return $true }
        Show-Since $H 'travel:|arrive:' $mH 10
        Show-Since $P 'travel:|arrive:' $mP 10
        return $false
    }
    Verdict $step $H $false 'a HARD transition was performed' 'no candidate name reached the hard path'
    return $false
}

# ---------------------------------------------------------------------------
# The end-of-run checks: kill sets, HP, world, area, faults and ERR
# ---------------------------------------------------------------------------
function Invoke-EndChecks([int]$H, [int]$P, [string]$step, [int]$m0) {
    Start-Sleep -Seconds 12
    $wH = Get-WorldState $H; $wP = Get-WorldState $P
    Note ("  world at end: host='{0}' partner='{1}' agree='{2}' area='{3}'" -f $wH.World, $wH.Partner, $wH.Agree, $wH.Area)
    Note ("                peer='{0}' partner='{1}' agree='{2}' area='{3}'" -f $wP.World, $wP.Partner, $wP.Agree, $wP.Area)
    $div = @(Get-Since $H $m0 | Where-Object { $_ -match 'WORLD DIVERGENCE' })
    Verdict $step $H ($div.Count -eq 0) 'no WORLD DIVERGENCE was reported during the run' ("lines=$($div.Count)")
    $sameWorld = ($wH.World -ne '' -and $wP.World -ne '' -and $wH.World -eq $wP.World -and
                  $wH.Partner -eq $wP.World -and $wP.Partner -eq $wH.World -and
                  $wH.Agree -eq '1' -and $wP.Agree -eq '1')
    Verdict $step $P $sameWorld 'both machines are in the SAME world (UWorld + agreement)' ("host=$($wH.World)/partner=$($wH.Partner)/agree=$($wH.Agree) peer=$($wP.World)/partner=$($wP.Partner)/agree=$($wP.Agree)")
    Verdict $step $P ($wH.Area -eq $wP.Area) 'both machines hold the SAME area' ("host='$($wH.Area)' peer='$($wP.Area)'")

    if (-not (Test-Alive $H) -or -not (Test-Alive $P)) {
        Note '  (kill-set comparison skipped: a process is gone, so its dump cannot answer)'
        Verdict $step $H $false 'the per-GUID kill sets DIFF TO ZERO' 'a process died; the dump is unreadable'
        return
    }
    $dh = Invoke-EnemyDump $H 'end-h'
    $dp = Invoke-EnemyDump $P 'end-p'
    $deadH = @($dh.Keys | Where-Object { $dh[$_].State -eq 'DEAD' })
    $deadP = @($dp.Keys | Where-Object { $dp[$_].State -eq 'DEAD' })
    $onlyH = @($deadH | Where-Object { -not $dp.ContainsKey($_) -or $dp[$_].State -ne 'DEAD' })
    $onlyP = @($deadP | Where-Object { -not $dh.ContainsKey($_) -or $dh[$_].State -ne 'DEAD' })
    Note ("  host keys={0} (dead {1}); peer keys={2} (dead {3})" -f $dh.Count, $deadH.Count, $dp.Count, $deadP.Count)
    Note ("  dead only on the host: {0} -> {1}" -f $onlyH.Count, ($onlyH -join ','))
    Note ("  dead only on the peer: {0} -> {1}" -f $onlyP.Count, ($onlyP -join ','))
    foreach ($k in $onlyH) { Note ("    host-only {0}: {1}" -f $k, $dh[$k].Line) }
    foreach ($k in $onlyP) { Note ("    peer-only {0}: {1}" -f $k, $dp[$k].Line) }
    Verdict $step $H ($onlyH.Count -eq 0 -and $onlyP.Count -eq 0) 'the per-GUID kill sets DIFF TO ZERO' ("host-dead=$($deadH.Count) peer-dead=$($deadP.Count) diff=$($onlyH.Count + $onlyP.Count)")

    $end = Compare-SharedHp $dh $dp 'end'
    Note ("  shared live keys at the end: {0}; comparable={1} unequal={2} unreadable={3} ghosts={4} why={5}" -f
          $end.AliveBoth, $end.Compared, $end.Unequal, $end.Unreadable, $end.Ghosts, (Format-Whys $end.Whys))
    foreach ($mm in ($end.Mismatches | Select-Object -First 8)) { Note ("    HP MISMATCH " + $mm) }
    $script:hpSamples.Add($end) | Out-Null

    $cmpTotal = 0; $uneqTotal = 0; $unreadTotal = 0; $aliveTotal = 0; $ghostTotal = 0
    $whys = @{}
    foreach ($s in $script:hpSamples) {
        $cmpTotal += $s.Compared; $uneqTotal += $s.Unequal
        $unreadTotal += $s.Unreadable; $aliveTotal += $s.AliveBoth; $ghostTotal += $s.Ghosts
        foreach ($k in @($s.Whys.Keys)) {
            if ($whys.ContainsKey($k)) { $whys[$k] += $s.Whys[$k] } else { $whys[$k] = $s.Whys[$k] }
        }
    }
    Note ("  HP ACROSS THE RUN: {0} sample(s); shared-alive {1}; COMPARED {2}; unequal {3}; unreadable {4}; ghosts {6}; why {5}" -f
          $script:hpSamples.Count, $aliveTotal, $cmpTotal, $uneqTotal, $unreadTotal, (Format-Whys $whys), $ghostTotal)
    Verdict $step $P ($cmpTotal -gt 0) 'shared HP was actually MEASURED during the run' ("compared=$cmpTotal of $aliveTotal shared-alive observations, $unreadTotal unreadable (never counted as equal)")
    Verdict $step $P ($cmpTotal -gt 0 -and $uneqTotal -eq 0) 'the HP of shared enemies is EQUAL wherever it was readable' ("compared=$cmpTotal unequal=$uneqTotal")

    foreach ($i in @($H, $P)) {
        $fault = @(Get-Since $i $m0 | Where-Object { $_ -match 'ACCESS_VIOLATION|EXCEPTION|fatal|\bfault' })
        $err = @(Get-Since $i $m0 | Where-Object { $_ -match ' ERR ' })
        $warn = @(Get-Since $i $m0 | Where-Object { $_ -match ' WARN ' })
        $classes = @{}
        foreach ($w in ($warn + $err)) {
            $s = [string]$w; $c = '(unlabelled)'
            if ($s -match '(?:WARN|ERR)\s+([a-z\-]+):') { $c = $Matches[1] }
            if ($classes.ContainsKey($c)) { $classes[$c]++ } else { $classes[$c] = 1 }
        }
        Note ("  [{0}] FAULT lines (ACCESS_VIOLATION/EXCEPTION/fatal)={1}" -f $i, $fault.Count)
        Note ("  [{0}] ERR lines={1}  WARN lines={2}  classes={3}" -f $i, $err.Count, $warn.Count,
              (($classes.GetEnumerator() | Sort-Object Value -Descending | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join ' '))
        foreach ($e in ($fault | Select-Object -Last 4)) { Note ("  [$i] " + ([string]$e).Trim()) }
        foreach ($e in ($err | Select-Object -First 4)) { Note ("  [$i] " + ([string]$e).Trim()) }
        Verdict $step $i ($fault.Count -eq 0) 'no FAULT lines during the run (the crash claim)' ("faults=$($fault.Count)")
        Verdict $step $i ($err.Count -eq 0) 'no ERR lines during the run' ("err=$($err.Count)")
        $cr = Get-CrashFolders $i
        if ($cr.Count -gt 0) {
            Verdict $step $i $false 'no crash dump was written' ("crash folders: " + ($cr -join ' '))
            foreach ($f in $cr) { Show-CrashSummary $f }
        } else {
            Verdict $step $i $true 'no crash dump was written' ''
        }
    }
}

function Get-CrashFolders([int]$i) {
    $base = $inst[$i].Crashes
    if (-not (Test-Path -LiteralPath $base)) { return @() }
    return @(Get-ChildItem -LiteralPath $base -Directory -ErrorAction SilentlyContinue |
             Where-Object { $_.LastWriteTime -gt (Get-Date).AddHours(-3) } |
             ForEach-Object { $_.FullName })
}
function Show-CrashSummary([string]$folder) {
    $x = Join-Path $folder 'CrashContext.runtime-xml'
    if (Test-Path -LiteralPath $x) {
        $t = Get-Content -LiteralPath $x -Raw
        if ($t -match '<FaultingModule>([^<]*)</FaultingModule>') { Note ("    crash module: " + $Matches[1]) }
        if ($t -match '<ErrorMessage>([^<]*)</ErrorMessage>') { Note ("    crash error: " + $Matches[1]) }
        if ($t -match '<ExceptionAddress>([^<]*)</ExceptionAddress>') { Note ("    crash address: " + $Matches[1]) }
    }
}

# ===========================================================================
# SMOKE -- the three fixed defects, in one short run
# ===========================================================================
function Invoke-Smoke([int]$H, [int]$P) {
    Step 'SMOKE -- loot both ways, three consecutive area changes, and a real hard travel' `
         'Each of Session 21''s three open items, measured in the order they were reported.'

    # ---- 1. LOOT -----------------------------------------------------------
    Invoke-LootDrop $H $P

    # ---- 2. THREE CONSECUTIVE AREA CHANGES (R150a) --------------------------
    # 780 -> 770 is the second entry Session 21 saw refused; the leave is the third change
    # and the one the old pin could not express at all.
    Step 'R150a -- three consecutive area changes must all follow' `
         'Session 21: the host streamed 770_SZ_Swamp and the peer answered "area 780_SZ_Depths is already pinned on this machine". A -> B -> hub has to work, not just the first entry.'
    $okB = Enter-Area $H $P '770_SZ_Swamp' 'enemy' 'area2'
    Verdict 'r150a' $P $okB 'the second TP_AREA entry followed on both machines' ''
    if ($okB) {
        $okLeave = Leave-Area $H $P 'area-leave'
        Verdict 'r150a' $P $okLeave 'leaving the area followed on both machines (780 -> 770 -> hub)' ''
    } else {
        Verdict 'r150a' $P $false 'leaving the area followed on both machines' 'the second entry did not follow, so there is nothing to leave'
    }

    # ---- 3. ONE REAL HARD TRAVEL, AND BACK (R150b) --------------------------
    Step 'R150b -- a real hard travel, out and back' `
         'Session 21: every name the rig tried from the hub was a sublevel of the world it stood in or that world itself, so the hard path was never reached. `qd.hard auto` picks a real UWorld instead.'
    $out = Invoke-HardTravel $H $P 'auto' 'hard-out'
    Verdict 'r150b' $H $out 'a REAL hard transition (a UWorld load, not a sublevel stream) was performed' ''
    if ($out) {
        $back = Invoke-HardTravel $H $P 'Ruins_Level_00' 'hard-back'
        Verdict 'r150b' $H $back 'the pair came back to Ruins_Level_00 by a hard load' ''
    } else {
        Verdict 'r150b' $H $false 'the pair came back to Ruins_Level_00 by a hard load' 'no hard travel was performed'
    }
}

# ===========================================================================
# HARDLOOP -- the repeated hard world load, back to back, in ONE pair of processes
# ===========================================================================
# A hard load is the only event in this game that tears a UWorld down
# (`LoadMapWithLoadingScreen`).  Session 22 lost the peer on the FOURTH load in a process and
# a 30-minute soak could only afford four, so the defect needed a loop that does nothing
# else.  This phase is that loop AND the proof: both machines alive after every load, and
# both machines reporting the same world after every load.
function Invoke-HardLoop([int]$H, [int]$P, [int]$loads) {
    Step ("HARDLOOP -- {0} consecutive hard world loads, both machines alive after every one" -f $loads) `
         'Session 22: the peer died on the fourth world load in one process, inside the game''s own object destruction, with the mod present only as the ProcessEvent forward. The only way to find out whether a world teardown leaves something dangling is to do it N times in one process and look after every one.'
    # R150b: a name this machine only STREAMS is not a hard travel, so `auto` is the mod's
    # own pick of a real UWorld and `Ruins_Level_00` is the world it has to come back to.
    # The second leg of every round trip is therefore the exact load that killed the peer.
    $legs = @('auto', 'Ruins_Level_00')
    $aliveAll = $true
    $sameAll = $true
    # A BARE LOOP IS NOT THE CRASH'S REPRODUCTION.  Session 22's peer died on the fourth load
    # after twenty-five minutes of play in that process, and six bare loads on the unfixed
    # build all survived (measured this session, recorded in the log).  So the loop can be
    # given a warm-up: a real soak segment first, which is what puts mirrors, loot and
    # partner-body state into the world that the next load tears down.
    if ($WarmSeconds -gt 0) {
        Step ("HARDLOOP WARM-UP -- {0} s of play before the loads" -f $WarmSeconds) `
             'The crash needs state the loop alone does not create, so the loop runs on a world that has been played in.'
        Invoke-SoakSegment $H $P 'warmup' $WarmSeconds 'enemy' 60
    }
    $m0 = Get-Mark $H
    for ($n = 1; $n -le $loads; $n++) {
        $leg = $legs[($n - 1) % 2]
        $step = "load$n"
        Note ''
        Note ("--- hard load {0} of {1}: {2} ---" -f $n, $loads, $leg)
        $ok = Invoke-HardTravel $H $P $leg $step
        if (-not $ok) { $sameAll = $false }
        $aH = Test-Alive $H; $aP = Test-Alive $P
        $alive = ($aH -and $aP)
        if (-not $alive) { $aliveAll = $false }
        Verdict $step $P $alive ("both machines are alive after hard load {0} of {1}" -f $n, $loads) ("host=$aH peer=$aP")
        if (-not $alive) {
            Note ("  REFUSING to continue the loop: a machine died on load {0}" -f $n)
            foreach ($i in @($H, $P)) {
                if (-not (Test-Alive $i)) {
                    Note ("  [{0}] the last 25 lines before the death:" -f $i)
                    Show-Since $i 'travel|arrive|ERR|fault|loot' 0 25
                }
            }
            foreach ($f in (@(Get-CrashFolders $H) + @(Get-CrashFolders $P))) { Show-CrashSummary $f }
            break
        }
        # A load is also the moment a world-scoped handle is most likely to be stale, so the
        # per-key dump is taken after every one: a lane that reads a freed actor would
        # otherwise look like "no faults" until the next soak sample.
        $null = Invoke-EnemyDump $P ('load' + $n)
    }
    $faults = 0
    foreach ($i in @($H, $P)) {
        $f = @(Get-Since $i $m0 | Where-Object { $_ -match 'ACCESS_VIOLATION|EXCEPTION|fatal|\bfault' })
        $faults += $f.Count
        foreach ($e in ($f | Select-Object -Last 3)) { Note ("  [$i] " + ([string]$e).Trim()) }
    }
    Verdict 'hardloop' $P $aliveAll ("both machines survived {0} consecutive hard world loads" -f $loads) ("loads={0}" -f $loads)
    Verdict 'hardloop' $P $sameAll 'every hard load landed both machines in the same world' ''
    Verdict 'hardloop' $H ($faults -eq 0) 'no FAULT lines were logged during the loop' ("faults=$faults")
    return $aliveAll
}

# ===========================================================================
# Soak -- >= 1800 s of body: hub fighting, two area changes, two hard round trips
# ===========================================================================
# The order is chosen so every requirement is reached even if a later one fails:
#   hub -> the loot take (which is what entering 780_SZ_Depths is for) -> 770 -> leave ->
#   hub -> two hard round trips -> hub.
# Area changes are `780` (inside the loot step) and `770`; the hard travels are two round
# trips, i.e. four world loads, in the same pair of processes that ran the body.
function Invoke-Soak([int]$H, [int]$P) {
    Step ("SOAK -- {0} s of measured body on the documented play set" -f $SoakSeconds) `
         'Session 22 ran 1581 s and 1496 s of body and said so. This one budgets the BODY at the full duration, reports the wall clock separately, and puts the two hard round trips INSIDE the run because that is where the crash lives.'
    $m0 = Get-Mark $H
    $wall0 = Get-Date
    $swm = @(Read-LogLines $inst[$H].Log | Where-Object { $_ -match 'switches: ' })
    $swLine = if ($swm.Count -gt 0) { ([string]$swm[-1]).Trim() } else { '' }
    if ($swLine) { Note ("  [$H] " + $swLine) }
    Verdict 'soak' $H ($swLine -match 'enemy=1') 'the switch set under test is the documented PLAY set' $swLine

    $wH0 = Get-WorldState $H; $wP0 = Get-WorldState $P
    Note ("  world at start: host='{0}' area='{1}' peer='{2}' area='{3}'" -f $wH0.World, $wH0.Area, $wP0.World, $wP0.Area)

    # The body is the budget.  Transitions are between the segments and their cost is
    # reported as wall clock, never folded into the soak number.
    $hub    = [int]($SoakSeconds * 0.32)
    $area2  = [int]($SoakSeconds * 0.16)
    $hub2   = [int]($SoakSeconds * 0.14)
    $final  = $SoakSeconds - $hub - $area2 - $hub2

    Invoke-SoakSegment $H $P 'hub' $hub 'enemy' 75

    # ---- area change 1 (780) with the loot take inside it -------------------
    $areaChanges = 0
    $lootAlive = @()
    if (Test-Alive $H -and (Test-Alive $P)) {
        Invoke-LootDrop $H $P
        $areaChanges++          # 780, entered by the loot step itself
        $lootAlive = @($script:verdicts | Where-Object { $_.Step -like 'r129*' -and $_.Ok })
    } else {
        Verdict 'soak' $P $false 'a loot take was performed and checked on both machines' 'a process died before the loot step'
    }

    # ---- area change 2 (770) ------------------------------------------------
    $ok2 = Enter-Area $H $P '770_SZ_Swamp' 'enemy' 'area2'
    Verdict 'soak' $P $ok2 'area change 2 of 2 followed on both machines (770_SZ_Swamp)' ''
    if ($ok2) { Invoke-SoakSegment $H $P 'area770' $area2 'enemy' 75; $areaChanges++ }

    $ok3 = Leave-Area $H $P 'area-leave'
    Verdict 'soak' $P $ok3 'leaving the area followed on both machines (back to the hub)' ''
    if ($ok3) { Invoke-SoakSegment $H $P 'hub-again' $hub2 'enemy' 75 }

    # ---- TWO hard round trips (four world loads) ----------------------------
    $trips = 0
    for ($t = 1; $t -le 2; $t++) {
        $out = Invoke-HardTravel $H $P 'auto' ("hard{0}-out" -f $t)
        Verdict 'soak' $H $out ("hard round trip {0}: the world load out succeeded" -f $t) ''
        if ($out) {
            $back = Invoke-HardTravel $H $P 'Ruins_Level_00' ("hard{0}-back" -f $t)
            Verdict 'soak' $H $back ("hard round trip {0}: the way back succeeded" -f $t) ''
            if ($back) { $trips++ }
        }
    }

    Invoke-SoakSegment $H $P 'hub-final' $final 'enemy' 60

    $body = 0
    foreach ($k in @('hub','area770','hub-again','hub-final')) {
        if ($script:segmentSeconds.ContainsKey($k)) { $body += [int]$script:segmentSeconds[$k] }
    }
    Note ("  segment seconds actually measured: " + (($script:segmentSeconds.GetEnumerator() | Sort-Object Name |
          ForEach-Object { "$($_.Key)=$($_.Value)s" }) -join ' '))
    Note ("  area changes that FOLLOWED: {0}; hard round trips completed: {1} of 2; MEASURED BODY: {2} s of {3} s" -f
          $areaChanges, $trips, $body, $SoakSeconds)
    $wall = [int]((Get-Date) - $wall0).TotalSeconds
    Note ("  soak BODY {0} s; wall clock including transitions {1} s" -f $body, $wall)
    Verdict 'soak' $H ($body -ge $SoakSeconds) "the measured body lasted at least {0} s" ("{0} s of {1} s" -f $body, $SoakSeconds)
    Verdict 'soak' $H ($areaChanges -ge 2) 'at least two area changes followed on both machines' ("followed={0}" -f $areaChanges)
    Verdict 'soak' $H ($trips -ge 2) 'at least two hard travels out and back were performed' ("trips={0}" -f $trips)
    Verdict 'soak' $P ($lootAlive.Count -gt 0) 'a loot take was performed and checked on both machines' ("host verdicts=$($lootAlive.Count)")

    Invoke-EndChecks $H $P 'soak' $m0
}

# ===========================================================================
# LOOT -- R129's take half on its own, so it can be iterated without a soak
# ===========================================================================
function Invoke-LootPhase([int]$H, [int]$P) {
    Invoke-LootDrop $H $P
    Invoke-EndChecks $H $P 'loot' (Get-Mark $H)
}

# ===========================================================================
# main
# ===========================================================================
Say ("session23 start: phase={0} switches={1} soak={2}s" -f $Phase,
     $(if ($ShippedDefaults) { 'SHIPPED' } else { 'DOCUMENTED PLAY' }), $SoakSeconds)
Stop-All
Seed-FreshPair
Start-Boot $Phase
$H = $script:H
$P = $script:P

if ($Phase -eq 'probe' -or $Phase -eq 'all') {
    Invoke-Probe $H $P
}

if ($Phase -eq 'hardloop' -or $Phase -eq 'all') {
    Invoke-HardLoop $H $P $HardLoads
}

if ($Phase -eq 'loot') {
    Invoke-LootPhase $H $P
}

if ($Phase -eq 'smoke' -or $Phase -eq 'all') {
    Invoke-Smoke $H $P
}

if ($Phase -eq 'soak' -or $Phase -eq 'all') {
    Invoke-Soak $H $P
}

Step 'SESSION SURVIVAL'
foreach ($i in @($H, $P)) {
    $alive = Test-Alive $i
    $script:verdicts.Add([pscustomobject]@{ Step = 'survival'; Inst = $i; What = 'alive at the end'; Ok = $alive; Line = '' }) | Out-Null
    Note "  $(if ($alive) { 'PASS' } else { 'FAIL' }) [$i] process alive=$alive"
    $fault = @(Get-Matches $i 'ACCESS_VIOLATION|EXCEPTION|fatal|\bfault')
    Note "  [$i] FAULT lines (ACCESS_VIOLATION/EXCEPTION/fatal): $($fault.Count)"
}
Stop-All

Note ''
Note '=== VERDICTS ==='
foreach ($v in $script:verdicts) {
    Note ("  {0,-12} inst{1}  {2,-6}  {3}  {4}" -f $v.Step, $v.Inst, $(if ($v.Ok) { 'PASS' } else { 'FAIL' }), $v.What, $v.Line)
}
$pass = @($script:verdicts | Where-Object { $_.Ok }).Count
$fail = @($script:verdicts | Where-Object { -not $_.Ok }).Count
Note ("=== {0} PASS, {1} FAIL ===" -f $pass, $fail)
$script:lines | Set-Content -LiteralPath $Report
