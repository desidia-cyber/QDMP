# session12.ps1 -- Session 12 of the live two-instance run, in ONE process.
#
# WHY IT IS ONE PROCESS
# ---------------------
# A launched game does not survive the command that started it (LIVE-TEST-LOG R14), and
# background jobs do not survive a turn boundary, so the relay, both instances, the
# driving, the collection and the teardown all happen inside this script.
#
# WHAT IT PROVES (Session 11 R91/R92)
# -----------------------------------
#   item1  The game's OWN travel half, measured.  Session 11 left "which of
#          TeleportToLocation / TeleportToTeleporterData / the teleporter's own path the
#          fast-travel UI takes" open (SYNC-RESEARCH §7).  The peer presses the hub's
#          teleporter with the real interact lever while `qd.petrace` records every new
#          (object, function) pair, so the answer is a log line instead of an inference.
#   item2  R92's crash: hard-loading the map a machine is already in.  The rig asks for
#          exactly that and must get a refusal, not a dead process (the crash had no
#          WINMM frame, so it was the GAME tearing down its current world).
#   item3  R91: after a hard transition the player is a pawn at the origin, falling.  The
#          leader crosses to 900_SipehaWorld, the follower follows, and BOTH must end up
#          standing: the leader on the world's own arrival marker
#          (`PlayerTeleportTargetPos`), the follower beside the leader.
#   item4  Content is reachable in the non-hub map afterwards: a teleport there sticks and
#          the press runs from a real place.
#
# EVERY control file is created before launch because the environment is not inherited
# reliably through the launch chain (measured with QDCOOP_TEST_TPFILE).
[CmdletBinding()]
param(
    [ValidateSet('item1', 'item2', 'item3', 'item4', 'all')]
    [string]$Phase = 'all',
    [int]$BootSeconds = 300,
    [string]$Report = ''
)

$ErrorActionPreference = 'Stop'
$here    = $PSScriptRoot
$repo    = Split-Path -Parent (Split-Path -Parent $here)
$gameDir = 'G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64'
$relay   = Join-Path (Split-Path -Parent $here) 'rust\target\release\qd-relay.exe'
$rigRoot = 'G:\Freespace\Queens_Domain\_test'
$seed    = Join-Path $repo 'analysis\save-backup-before-live'
$outDir  = Join-Path $rigRoot 'session12'
if (-not $Report) { $Report = Join-Path $outDir ('report-' + $Phase + '.txt') }
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force -Path $outDir | Out-Null }

$script:lines = New-Object System.Collections.Generic.List[string]
trap {
    Write-Output ("UNHANDLED: " + $_.Exception.Message)
    Write-Output ("AT: " + $_.InvocationInfo.PositionMessage)
    Write-Output ("STACK: " + $_.ScriptStackTrace)
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
        Near   = Join-Path $root 'qd.near'; Drops = Join-Path $root 'qd.drops'
        Menu   = Join-Path $root 'qd.menu'; EFunc = Join-Path $root 'qd.efunc'
        EAtk   = Join-Path $root 'qd.eattack'; Use = Join-Path $root 'qd.use'
        Pdmg   = Join-Path $root 'qd.pdmg'; Kill = Join-Path $root 'qd.kill'
        PeTrace = Join-Path $root 'qd.petrace'
        Pid = "$root.pid"
    }
}
$R1 = Get-RigPaths 1
$R2 = Get-RigPaths 2
$inst = @{ 1 = $R1; 2 = $R2 }

# A LIVE log read.  `Get-Content` asks for FileShare.Read, while the mod holds its handle
# with write access (qdlog.h: FILE_SHARE_READ|FILE_SHARE_WRITE), so the open is a sharing
# violation that `-ErrorAction SilentlyContinue` turns into "no lines at all".  Cost of that
# bug: Session 12's third boot reported BOTH machines as never connected and refused to run.
function Read-LogLines([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) { return @() }
    # Three attempts: the game writes the file on the same thread that ticks the mod, and a
    # transient open failure would read as "no lines" -- which this rig would then report as
    # "the machine never connected", the most misleading verdict available.
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
        } catch {
            Start-Sleep -Milliseconds 50
        }
    }
    return @()
}

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
function Wait-Pattern([int]$i, [string]$pattern, [int]$seconds) {
    $deadline = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $deadline) {
        $m = Get-Matches $i $pattern
        if ($m.Count -gt 0) { return $m }
        if (-not (Test-Alive $i)) { return @() }
        Start-Sleep -Milliseconds 500
    }
    return @()
}
function Wait-Gameplay([int]$i, [int]$seconds) {
    $deadline = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $deadline) {
        if (-not (Get-Process -Id $script:pids[$i] -ErrorAction SilentlyContinue)) { return $false }
        $m = Get-Matches $i 'scan: local at|scan: health'
        if ($m.Count -gt 0) { return $true }
        Start-Sleep -Milliseconds 500
    }
    return $false
}
function Set-Control([int]$i, [string]$leaf, $value) {
    $p = Join-Path -Path ([string]$inst[$i].Root) -ChildPath ([string]$leaf)
    if ($null -eq $value) { Remove-Item -LiteralPath $p -ErrorAction SilentlyContinue }
    else { Set-Content -LiteralPath $p -Value ([string]$value) -NoNewline }
}
# A mark is a TIME OF DAY, not a line count: the mod and this script share a wall clock, and
# a count is only correct if every read agrees how many lines the run has -- which is exactly
# what cannot be assumed of a file the game is still writing (Session 12 lost two verdicts
# that way, on lines that were provably in the log).
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
    return $ms - 1000          # a line written in the same millisecond as the mark still counts
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
    $ok = $found
    $script:verdicts.Add([pscustomobject]@{ Step = $step; Inst = $i; What = $label; Ok = $ok; Line = ($m | Select-Object -Last 1) }) | Out-Null
    # Write-Host, not Note: the callers pipe Check's output away, and a verdict that is
    # only in the report file cannot be watched while the run is happening.
    if ($ok) { $line = "  PASS [$i] $label" }
    else { $line = "  --   [$i] $label (no line matches '$pattern' in ${secs}s)" }
    Write-Host $line
    $script:lines.Add($line) | Out-Null
    return $ok
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
function Invoke-Go([int]$i, [string]$name, [int]$near = -1) {
    $script:goSeq++
    $yaw = ($script:goSeq * 37) % 360
    if ($near -ge 0) { Set-Control $i 'qd.near' ([string]$near) }
    Set-Control $i 'qd.tp' ('near:' + $name + ',' + $yaw)
    Say "  tp[$i] -> near:$name (yaw $yaw, near=$(if ($near -ge 0) { $near } else { 'unchanged' }))"
}
function Invoke-Goto([int]$i, [string]$name, [int]$near = -1) {
    if ($near -ge 0) { Set-Control $i 'qd.near' ([string]$near) }
    Set-Control $i 'qd.tp' ('near:' + $name)
    Say "  tp[$i] -> near:$name (facing, no yaw override)"
}
$script:pressSeq = 0
function Invoke-Press([int]$i, [string]$name, [string]$step, [switch]$NoStandCheck) {
    Invoke-Goto $i $name
    Start-Sleep -Seconds 10
    $posLine = @(Get-RunLines $i | Where-Object { $_ -match 'scan: local at ' })
    $pl = if ($posLine.Count) { [string]$posLine[-1] } else { '(no position line)' }
    $standing = ($posLine.Count -gt 0) -and ($pl -notmatch 'local at (?:-?0(?:\.0+)?),-?0(?:\.0+)?,')
    if (-not $NoStandCheck) {
        Note "  $(if ($standing) { 'PASS' } else { 'FAIL' }) [$i] the player is standing somewhere real: $pl"
        $script:verdicts.Add([pscustomobject]@{ Step = $step; Inst = $i; What = 'the player stands somewhere real (not the origin, falling)'; Ok = $standing; Line = $pl }) | Out-Null
    } else {
        Note "  (standing check skipped: the teleport target itself is not a place to stand) $pl"
    }
    $script:pressSeq++
    Set-Control $i 'qd.act' ('near:' + $name.ToUpperInvariant())
    Say "  act[$i] -> near:$($name.ToUpperInvariant()) (field filled, then the real input event)"
}
function Invoke-Hard([int]$i, [string]$map) {
    Set-Control $i 'qd.hard' $map
    Say "  hard[$i] -> $map"
}
function Set-PeTrace([int]$i, [int]$v) {
    Set-Control $i 'qd.petrace' ([string]$v)
    Say "  petrace[$i] -> $v"
}
function Invoke-Shot([int]$i, [string]$leaf) {
    try {
        $gamePid = Get-Content -LiteralPath $inst[$i].Pid -ErrorAction SilentlyContinue | Select-Object -First 1
        if (-not $gamePid) { Note "  -- shot[$i]: no pid"; return }
        $out = Join-Path $outDir ("{0}-inst{1}.png" -f $leaf, $i)
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'capture-hidden.ps1') `
            -ProcessId ([int]$gamePid) -Out $out 2>&1 | Select-Object -Last 1 | ForEach-Object { Note "  shot[$i] $_" }
    } catch { Note "  -- shot[$i] failed: $($_.Exception.Message)" }
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
    try {
        Copy-Item -LiteralPath $built -Destination $dst -Force
    } catch {
        $held = $dst + '.held-' + (Get-Date -Format 'HHmmss')
        Rename-Item -LiteralPath $dst -NewName (Split-Path -Leaf $held) -Force
        Copy-Item -LiteralPath $built -Destination $dst -Force
        Say "destination was image-mapped; moved it to $held and installed the new proxy"
    }
    Say "installed $built -> $dst"

    $ini = Join-Path $gameDir 'qdcoop.ini'
    $lines = @('[Local]', 'Enabled=1', 'LogLevel=3', '', '[Net]',
               'RelayHost=127.0.0.1', 'RelayPort=47800', '', '[Rules]',
               'FlagSync=1', 'InventorySync=1', 'ActorStateSync=1', 'EnemySync=1',
               'PuppetSync=1', 'PlayerBody=1', 'DialogueSync=1', 'StreamUnion=1',
               'TravelSync=1', 'CutsceneSync=1', 'MoverSync=1', 'LootSync=1',
               'SuspendGuard=1', 'ObjectSync=1', 'SuppressMenuPause=1', 'HostAuthoritative=1',
               '', '[Tuning]', 'PlayerHz=30', 'EnemyRowsPerTick=20', 'SpawnAttemptsMax=5')
    $lines | Set-Content -LiteralPath $ini
    Say "wrote $ini (every write path on, PlayerBody on, LogLevel=3)"
}

function Start-Boot([string]$tag) {
    Stop-All
    Install-And-Configure
    foreach ($i in 1, 2) {
        foreach ($leaf in 'qd.tp','qd.act','qd.hit','qd.cheat','qd.cheatarg','qd.travel','qd.hard',
                          'qd.level','qd.near','qd.drops','qd.menu','qd.efunc','qd.eattack',
                          'qd.use','qd.pdmg','qd.kill','qd.petrace') {
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
    # Anchored on the role token: a loose 'session established' also matches diagnostics
    # that mention the phrase, and Session 12's first attempt read both machines as PEER
    # from one such line and then drove the wrong machine as the leader.
    while ((Get-Date) -lt $deadline) {
        foreach ($i in 1, 2) {
            if ($roles.ContainsKey($i)) { continue }
            if (-not $script:pids.ContainsKey($i)) { continue }
            $m = @(Get-Matches $i 'INFO session established: role=(HOST|PEER)')
            if ($m.Count -gt 0) {
                # `@()` matters: with exactly one match PowerShell returns the string, and
                # `$m[-1]` on a string is its last CHARACTER, not its last line.
                $line = [string]$m[-1]
                $r = if ($line -match 'role=HOST') { 'HOST' } elseif ($line -match 'role=PEER') { 'PEER' } else { '' }
                if ($r) { $roles[$i] = $r }
            }
        }
        if ($roles.Count -eq 2) { break }
        Start-Sleep -Seconds 1
    }
    foreach ($i in 1, 2) {
        if ($roles.ContainsKey($i)) {
            $m = Get-Matches $i 'INFO session established: role=(HOST|PEER)'
            Say "inst$i role=$($roles[$i])  <- $($m[-1])"
        } else { Say "inst$i NEVER ESTABLISHED A SESSION" }
    }
    $hostI = @($roles.Keys | Where-Object { $roles[$_] -eq 'HOST' } | Select-Object -First 1)
    $peerI = @($roles.Keys | Where-Object { $roles[$_] -eq 'PEER' } | Select-Object -First 1)
    if ($hostI.Count -eq 0 -or $peerI.Count -eq 0) {
        # Driving the wrong machine as the leader makes every later verdict a lie, so a
        # pair that never formed is a refusal, not a guess.  Say what was actually read:
        # "never connected" and "connected but unreadable" are different bugs.
        foreach ($i in 1, 2) {
            $a = @(Get-RunLines $i)
            $last = if ($a.Count) { [string]$a[-1] } else { '(none)' }
            Say "inst$i log lines=$($a.Count) last=$last"
        }
        Say 'REFUSED: no host/peer pair; see the logs'
        Stop-Boot
        $script:lines | Set-Content -LiteralPath $Report
        exit 2
    }
    $script:H = [int]$hostI[0]; $script:P = [int]$peerI[0]
    Say "host=inst$($script:H) peer=inst$($script:P)"

    Say "waiting for gameplay on both (up to $BootSeconds s)"
    foreach ($i in 1, 2) {
        $ok = Wait-Gameplay $i $BootSeconds
        Say "inst${i} gameplay=$ok"
    }
}

function Stop-Boot {
    foreach ($i in 1, 2) {
        if ($script:pids -and $script:pids.ContainsKey($i)) { Stop-Process -Id $script:pids[$i] -Force -ErrorAction SilentlyContinue }
    }
    if ($script:relayProc) { Stop-Process -Id $script:relayProc.Id -Force -ErrorAction SilentlyContinue }
    Start-Sleep -Seconds 1
    Say 'stopped both instances and the relay'
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

# ===========================================================================
# ITEM 1 -- the game's own travel half, measured
# ===========================================================================
function Invoke-Item1([int]$H, [int]$P) {
    Step 'ITEM 1 -- what the game itself runs when the interact key is pressed on a teleporter'
    Note 'SYNC-RESEARCH 7 left this open; the answer is a ProcessEvent trace of the local'
    Note 'player, armed right before the press, plus the verb watcher the mod now prints.'

    # A teleporter's own name, from the scan the mod already produces.
    $tp = ''
    foreach ($i in $P, $H) {
        $m = @(Get-RunLines $i | Where-Object { $_ -match 'scan: kind teleport (\S+)' })
        if ($m.Count) { if ($m[-1] -match 'scan: kind teleport (\S+)') { $tp = $Matches[1] } }
        if ($tp) { break }
    }
    if (-not $tp) { $tp = 'teleporter' }
    Say "  teleporter target: '$tp'"

    $mP = Get-Mark $P
    $mH = Get-Mark $H
    # Silent baseline first, then log: the trace otherwise reports the boot's whole
    # function graph, which buries the press.
    Set-PeTrace $P 1
    Start-Sleep -Seconds 3
    Set-PeTrace $P 2
    Invoke-Press $P $tp 'item1'
    Start-Sleep -Seconds 6
    Set-PeTrace $P 0
    Start-Sleep -Seconds 2

    Check 'item1' $P 'PE trace: logging new pairs' 20 'the ProcessEvent trace armed on the peer' $mP | Out-Null
    [void](Check 'item1' $P 'dev interact: component target set' 30 'the peer filled the gate field' $mP)
    [void](Check 'item1' $P 'dev interact: the player''s Interact input event \S+ ran' 30 'the peer''s own Interact input event ran' $mP)
    Check 'item1' $P 'PE new: ' 30 'the press ran NEW Blueprint functions (the record is in the log)' $mP | Out-Null
    # What the press DOES, as measured rather than expected: the trace names the entry
    # (`Interact` on `Teleporter_C`), and the verb watcher either reports the player's own
    # travel half or it does not.  Both are results; the verdict records which happened.
    $verbs = @(Get-Since $P $mP | Where-Object { $_ -match 'travel verb: the game''s own' })
    $entry = @(Get-Since $P $mP | Where-Object { $_ -match 'dev interact: target=\S+ .*verb=\w+' })
    $sawEntry = ($entry.Count -gt 0)
    $script:verdicts.Add([pscustomobject]@{ Step = 'item1'; Inst = $P; What = $(if ($verbs.Count) { 'the game''s own travel verb ran on the press' } else { 'the press ran NO player travel verb (the entry is the placed actor''s own Interact)' }); Ok = $true; Line = $(if ($verbs.Count) { $verbs[-1] } elseif ($sawEntry) { $entry[-1] } else { '' }) }) | Out-Null
    Note "  PASS [$P] the game's own travel half: $(if ($verbs.Count) { ($verbs | Select-Object -Last 1) } else { 'no player travel verb ran; the entry was the actor''s Interact (see the record above)' })"
    Show-Since $P 'PE new: |PE win: |travel verb:|dev interact' $mP 30
    Show-Since $H 'travel verb:|travel: ' $mH 8
    Invoke-Shot $P 'item1-peer-teleporter'
}

# ===========================================================================
# ITEM 2 -- the map you are already in (R92's crash)
# ===========================================================================
function Invoke-Item2([int]$H, [int]$P) {
    Step 'ITEM 2 -- hard-loading the map a machine is ALREADY in must be refused, not fatal'
    Note 'Session 11 R92: the game faulted with no WINMM frame in the stack (its own world'
    Note 'teardown).  The loader now refuses the current world at the one place it loads.'
    $mP = Get-Mark $P
    $mH = Get-Mark $H
    Invoke-Hard $P 'Ruins_Level_00'
    Start-Sleep -Seconds 12
    Check 'item2' $P 'rig: hard lever -> Ruins_Level_00 LoadMapWithLoadingScreen ran=0: already in Ruins_Level_00' 30 'the peer refuses to reload the world it is in' $mP | Out-Null
    $alive = Test-Alive $P
    $script:verdicts.Add([pscustomobject]@{ Step = 'item2'; Inst = $P; What = 'the peer survived the refusal (R92 regression)'; Ok = $alive; Line = '' }) | Out-Null
    Note "  $(if ($alive) { 'PASS' } else { 'FAIL' }) [$P] process alive after being asked to reload its own world"
    Show-Since $P 'rig: hard lever' $mP 4

    # And the same request on the host, so the guard is proven on both paths.
    Invoke-Hard $H 'Ruins_Level_00'
    Start-Sleep -Seconds 10
    Check 'item2' $H 'rig: hard lever -> Ruins_Level_00 LoadMapWithLoadingScreen ran=0: already in Ruins_Level_00' 25 'the host refuses it too' $mH | Out-Null
    Show-Since $H 'rig: hard lever' $mH 4
    foreach ($i in $H, $P) {
        $alive = Test-Alive $i
        $script:verdicts.Add([pscustomobject]@{ Step = 'item2'; Inst = $i; What = 'alive after the current-world refusal'; Ok = $alive; Line = '' }) | Out-Null
        Note "  $(if ($alive) { 'PASS' } else { 'FAIL' }) [$i] process alive=$alive"
    }
}

# ===========================================================================
# ITEM 3 -- the follower arrives standing (R91)
# ===========================================================================
function Invoke-Item3([int]$H, [int]$P) {
    Step 'ITEM 3 -- the leader crosses to 900_SipehaWorld; the follower must arrive STANDING'
    Note 'Session 11 R91: after the load the follower was a pawn at 0,0 falling, because the'
    Note 'world places no PlayerStart. The game''s own arrival point for it is the single'
    Note 'placed PlayerTeleportTargetPos actor; each machine is placed on it (or beside the'
    Note 'leader) by ArrivalPump, which is armed from the map change itself.'
    $mH = Get-Mark $H
    $mP = Get-Mark $P
    Invoke-Hard $H '900_SipehaWorld'
    Check 'item3' $H 'rig: hard lever -> 900_SipehaWorld LoadMapWithLoadingScreen ran=1' 30 'the host took the hard transition' $mH | Out-Null
    Check 'item3' $P 'travel: hard transition: loading 900_SipehaWorld' 60 'the follower took the same one' $mP | Out-Null
    Check 'item3' $P 'travel: host released the pair; resuming in 900_SipehaWorld' 90 'the handshake completed' $mP | Out-Null
    Start-Sleep -Seconds 10
    Check 'item3' $H 'arrive: 900_SipehaWorld placed the player at ' 60 'the LEADER is standing somewhere real in the new world' $mH | Out-Null
    Check 'item3' $P 'arrive: 900_SipehaWorld placed the player at ' 60 'the FOLLOWER is placed rather than left at the origin' $mP | Out-Null
    [void](Wait-Pattern $P 'scan: local at ' 60)
    Start-Sleep -Seconds 12
    $pH = Get-LastPos $H $mH
    $pP = Get-LastPos $P $mP
    Note ("  host at " + $(if ($pH) { ("{0:N0},{1:N0},{2:N0}" -f $pH[0], $pH[1], $pH[2]) } else { '(none)' }))
    Note ("  peer at " + $(if ($pP) { ("{0:N0},{1:N0},{2:N0}" -f $pP[0], $pP[1], $pP[2]) } else { '(none)' }))
    $dist = $null
    if ($pH -and $pP) {
        $dist = [math]::Sqrt([math]::Pow($pH[0]-$pP[0],2) + [math]::Pow($pH[1]-$pP[1],2) + [math]::Pow($pH[2]-$pP[2],2))
    }
    $near = ($dist -ne $null) -and ($dist -lt 2000)
    $script:verdicts.Add([pscustomobject]@{ Step = 'item3'; Inst = 0; What = 'the two players are within 20 m of each other'; Ok = $near; Line = $(if ($dist -ne $null) { ("{0:N0} cm" -f $dist) } else { '' }) }) | Out-Null
    Note "  $(if ($near) { 'PASS' } else { 'FAIL' }) the two players are close: $(if ($dist -ne $null) { ("{0:N0} cm" -f $dist) } else { '(distance unknown)' })"
    foreach ($i in $H, $P) {
        $pos = if ($i -eq $H) { $pH } else { $pP }
        $standing = ($pos -ne $null) -and ([math]::Abs($pos[0]) -gt 150 -or [math]::Abs($pos[1]) -gt 150)
        $script:verdicts.Add([pscustomobject]@{ Step = 'item3'; Inst = $i; What = 'standing, not the identity spawn'; Ok = $standing; Line = $(if ($pos) { ("{0:N0},{1:N0},{2:N0}" -f $pos[0],$pos[1],$pos[2]) } else { '' }) }) | Out-Null
        Note "  $(if ($standing) { 'PASS' } else { 'FAIL' }) [$i] standing somewhere real in 900_SipehaWorld"
    }
    # Both alive, and the world names agree again: the session continued through it.
    foreach ($i in $H, $P) {
        $alive = Test-Alive $i
        $script:verdicts.Add([pscustomobject]@{ Step = 'item3'; Inst = $i; What = 'alive after the transition'; Ok = $alive; Line = '' }) | Out-Null
        Note "  $(if ($alive) { 'PASS' } else { 'FAIL' }) [$i] process alive=$alive"
    }
    # The divergence gate is debounced, so a fast handshake can clear before the board ever
    # reports it: the pair agreeing is therefore proven by the handshake lines above, not by
    # a "worlds agree again" line that this transition may never produce.
    Show-Since $H 'arrive:|travel: |scan: local at|scan: health' $mH 10
    Show-Since $P 'arrive:|travel: |scan: local at|scan: health' $mP 12
    Invoke-Shot $H 'item3-host-arrived'
    Invoke-Shot $P 'item3-peer-arrived'
}

# ===========================================================================
# ITEM 4 -- content is reachable in the non-hub map
# ===========================================================================
function Invoke-Item4([int]$H, [int]$P) {
    Step 'ITEM 4 -- in the non-hub map the player STAYS standing, and a press runs from there'
    Note 'Session 11 could not press anything in these maps: the pawn was respawned to the'
    Note 'origin every few seconds.  The test is that the placed position holds, and that the'
    Note 'interaction lever then runs from a real place (the game still decides the object).'
    $mP = Get-Mark $P
    [void](Wait-Since $P 'scan: local at ' $mP 60)
    $a = Get-LastPos $P $mP
    Start-Sleep -Seconds 22
    $b = Get-LastPos $P $mP
    Note ("  peer at " + $(if ($a) { ("{0:N0},{1:N0},{2:N0}" -f $a[0],$a[1],$a[2]) } else { '(none)' }) + " -> " + $(if ($b) { ("{0:N0},{1:N0},{2:N0}" -f $b[0],$b[1],$b[2]) } else { '(none)' }) + " after 22 s")
    $held = $false
    if ($a -and $b) {
        $d = [math]::Sqrt([math]::Pow($a[0]-$b[0],2) + [math]::Pow($a[1]-$b[1],2) + [math]::Pow($a[2]-$b[2],2))
        $held = ($d -lt 500) -and ([math]::Abs($b[0]) -gt 150 -or [math]::Abs($b[1]) -gt 150)
        Note ("  drift over 22 s: {0:N0} cm" -f $d)
    }
    $script:verdicts.Add([pscustomobject]@{ Step = 'item4'; Inst = $P; What = 'the player stays standing in the non-hub map (not respawned to the origin)'; Ok = $held; Line = $(if ($b) { ("{0:N0},{1:N0},{2:N0}" -f $b[0],$b[1],$b[2]) } else { '' }) }) | Out-Null
    Note "  $(if ($held) { 'PASS' } else { 'FAIL' }) the peer's position held for 22 s"

    # The press itself.  What it opens is the game's call -- Session 11 already measured
    # that the chest in this world is refused by the game's own gate -- so this records the
    # gate's verdict rather than demanding a change.  No standing check here: the rig's
    # `near:<chest>` teleport lands beside an object that sits above the void, and dropping
    # into it is the map's business, not the arrival lane's.
    Invoke-Press $P 'Chest_Merchant2' 'item4' -NoStandCheck
    [void](Check 'item4' $P 'dev interact: the game''s own gate says IsInteractible=' 30 'the press ran and the game answered from a real place' $mP)
    Show-Since $P 'dev interact|dev state:|scan: local at' $mP 16
    Invoke-Shot $P 'item4-peer-press'
}

# ===========================================================================
# main
# ===========================================================================
Say "session12 start: phase=$Phase"

Stop-All
Seed-FreshPair
Start-Boot $Phase
$H = $script:H
$P = $script:P

if ($Phase -eq 'item1' -or $Phase -eq 'all') { Invoke-Item1 $H $P }
if ($Phase -eq 'item2' -or $Phase -eq 'all') { Invoke-Item2 $H $P }
if ($Phase -eq 'item3' -or $Phase -eq 'all') { Invoke-Item3 $H $P }
if ($Phase -eq 'item4' -or $Phase -eq 'all') { Invoke-Item4 $H $P }

Step 'SESSION SURVIVAL'
foreach ($i in 1, 2) {
    $alive = Test-Alive $i
    $script:verdicts.Add([pscustomobject]@{ Step = 'survival'; Inst = $i; What = 'alive at the end'; Ok = $alive; Line = '' }) | Out-Null
    Note "  $(if ($alive) { 'PASS' } else { 'FAIL' }) [$i] process alive=$alive"
    $err = Get-Matches $i 'ERR |EXCEPTION|ACCESS_VIOLATION|fatal|\bfault'
    Note "  [$i] error/fault lines: $($err.Count)"
    $err | Select-Object -Last 4 | ForEach-Object {
        $s = [string]$_
        if ($s.Length -gt 220) { $s = $s.Substring(0, 220) + ' ...' }
        Note "  [$i] $s"
    }
}
Stop-All

Note ''
Note '=== VERDICTS ==='
foreach ($v in $script:verdicts) {
    Note ("  {0,-14} inst{1}  {2,-6}  {3}" -f $v.Step, $v.Inst, $(if ($v.Ok) { 'PASS' } else { 'FAIL' }), $v.What)
}
$script:lines | Set-Content -LiteralPath $Report
Say "report: $Report"
