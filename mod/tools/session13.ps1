# session13.ps1 -- Session 13 of the live two-instance run, in ONE process.
#
# WHAT THIS SESSION PROVES
# ------------------------
# Session 12 closed R91: a hard transition now leaves the player standing (marker first,
# partner second).  This session uses that to reach content and drive it with the real
# interaction lever instead of inferring from probes:
#
#   item1  A closed chest/door opens on the PEER through the real E press, the HOST
#          applies it (`actor-state refresh ok ... opened=1`), it is not lootable again,
#          and `TotalChestsOpened` is not double counted.  Then the HOST opens one and the
#          PEER applies it.
#   item2  Loot hiding: both players beside the SAME enemy (authored GUID), kill it, take
#          the drop on one machine, and the other machine logs `loot: hiding local drop`.
#   item3  A reachable world with a boss encounter and a LevelSequence trigger: the Whale
#          King arena in `210_AQ_underground` (its own `PlayerTeleportTargetPos` marker),
#          and the Sipeha arena in `900_SipehaWorld`.  Boss bar on both, cutscene start /
#          hold and joint resume.
#   item4  A 5-minute two-player combat soak in one world: no crash, the death lists agree,
#          warning classes counted.
#
# Every control file is created before launch because the environment is not inherited
# reliably through the launch chain (measured with QDCOOP_TEST_TPFILE).
[CmdletBinding()]
param(
    [ValidateSet('probe', 'tp', 'tp2', 'hub', 'door', 'lootHub', 'lootMossy', 'final', 'soakHub', 'world780', 'items780', 'world900', 'items900', 'soak', 'all')]
    [string]$Phase = 'probe',
    [int]$BootSeconds = 300,
    [int]$SoakSeconds = 300,
    [string]$Report = ''
)

$ErrorActionPreference = 'Stop'
$here    = $PSScriptRoot
$repo    = Split-Path -Parent (Split-Path -Parent $here)
$gameDir = 'G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64'
$relay   = Join-Path (Split-Path -Parent $here) 'rust\target\release\qd-relay.exe'
$rigRoot = 'G:\Freespace\Queens_Domain\_test'
$seed    = Join-Path $repo 'analysis\save-backup-before-live'
$outDir  = Join-Path $rigRoot 'session13'
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
function Get-Control([int]$i, [string]$leaf) {
    $p = Join-Path -Path ([string]$inst[$i].Root) -ChildPath ([string]$leaf)
    if (-not (Test-Path -LiteralPath $p)) { return '' }
    return (Get-Content -LiteralPath $p -Raw -ErrorAction SilentlyContinue)
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
    $script:verdicts.Add([pscustomobject]@{ Step = $step; Inst = $i; What = $label; Ok = $found; Line = ($m | Select-Object -Last 1) }) | Out-Null
    if ($found) { $line = "  PASS [$i] $label" }
    else { $line = "  --   [$i] $label (no line matches '$pattern' in ${secs}s)" }
    Write-Host $line
    $script:lines.Add($line) | Out-Null
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
# The act lever ignores a repeated instruction (`strcmp(buf, applied)`), so a second press
# on the same target must LOOK different.  Case is the cheapest difference: the resolver is
# case-insensitive (`HasCI`), the lever's comparison is not (`strcmp`).
$script:pressSeq = 0
function Invoke-Press([int]$i, [string]$name, [string]$step, [switch]$NoStandCheck) {
    $m0 = Get-Mark $i
    Invoke-Goto $i $name
    Start-Sleep -Seconds 10
    # Only a position observed AFTER this press started counts.  Session 13's first
    # attempt passed the standing check on a stale hub line from before the load.
    $posLine = @(Get-Since $i $m0 | Where-Object { $_ -match 'scan: local at ' })
    $pl = if ($posLine.Count) { [string]$posLine[-1] } else { '(no position line)' }
    $standing = ($posLine.Count -gt 0) -and ($pl -notmatch 'local at (?:-?0(?:\.0+)?),-?0(?:\.0+)?,')
    if (-not $NoStandCheck) {
        Note "  $(if ($standing) { 'PASS' } else { 'FAIL' }) [$i] the player is standing somewhere real: $pl"
        $script:verdicts.Add([pscustomobject]@{ Step = $step; Inst = $i; What = 'the player stands somewhere real (not the origin, falling)'; Ok = $standing; Line = $pl }) | Out-Null
    } else {
        Note "  (standing check skipped: the teleport target itself is not a place to stand) $pl"
    }
    $script:pressSeq++
    $alt = if ($script:pressSeq % 2 -eq 1) { $name.ToUpperInvariant() } else { $name.ToLowerInvariant() }
    Set-Control $i 'qd.act' ('near:' + $alt)
    Say "  act[$i] -> near:$alt (field filled, then the real input event)"
}
function Invoke-Hard([int]$i, [string]$map) {
    Set-Control $i 'qd.hard' $map
    Say "  hard[$i] -> $map"
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
            if ($m.Count -gt 0) {
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

function Wait-NoOrigin([int]$i, [int]$mark, [int]$seconds) {
    $deadline = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $deadline) {
        $p = Get-LastPos $i $mark
        if ($p -and ([math]::Abs($p[0]) -gt 150 -or [math]::Abs($p[1]) -gt 150)) { return $p }
        if (-not (Test-Alive $i)) { return $null }
        Start-Sleep -Milliseconds 500
    }
    return (Get-LastPos $i $mark)
}

# ===========================================================================
# ITEM 1 -- a closed chest/door, opened on the peer with the real E press
# ===========================================================================
function Invoke-ChestPair([int]$H, [int]$P, [string]$step, [string]$world) {
    Step "ITEM 1 -- ${world}: peer opens a closed chest; host applies; then host opens one"
    $mP = Get-Mark $P
    $mH = Get-Mark $H
    Invoke-Press $P 'closedchest' $step
    [void](Check $step $P 'dev interact: the game''s own gate says IsInteractible=' 40 'the peer asked the game whether the chest is interactible' $mP)
    [void](Check $step $P 'dev interact: (the input event changed the object|target=.*verb=)' 40 'the peer''s real press ran its verb' $mP)
    [void](Check $step $P 'actor-state intent sent guid=' 40 'the peer published the opened state as an intent' $mP)
    [void](Check $step $H 'actor-state intent applied: guid=' 40 'the host applied the peer''s intent' $mH)
    [void](Check $step $H 'actor-state refresh ok .* opened=1|forced=1' 40 'the host''s own copy is now visually/semantically open' $mH)
    [void](Check $step $P 'actor states applied=.*confirmed=1' 40 'the peer got the host echo (confirmed)' $mP)
    [void](Check $step $H 'flag intent sent TotalChestsOpened=|intent applied: TotalChestsOpened=' 20 'the chest counter crossed as a flag' $mH)
    Show-Since $P 'dev interact|actor-state|flag intent|actor states applied' $mP 20
    Show-Since $H 'actor-state|intent applied|flag' $mH 20

    # The host's half: another closed chest, opened on the authority, applied by the peer.
    $mH2 = Get-Mark $H
    $mP2 = Get-Mark $P
    Invoke-Press $H 'closedchest' $step
    [void](Check $step $H 'dev interact: the game''s own gate says IsInteractible=' 40 'the host asked the game whether the second chest is interactible' $mH2)
    [void](Check $step $H 'dev interact: (the input event changed the object|target=.*verb=)' 40 'the host''s real press ran its verb' $mH2)
    [void](Check $step $P 'actor states applied=' 40 'the peer applied the host''s absolute set' $mP2)
    [void](Check $step $P 'actor-state refresh ok .* opened=1|forced=1' 40 'the peer''s copy is open too' $mP2)
    Show-Since $H 'dev interact|actor-state host set|flag' $mH2 16
    Show-Since $P 'actor states applied|actor-state refresh' $mP2 16

    # The counter: every value it took, so a double count is a number, not an opinion.
    Step "ITEM 1b -- TotalChestsOpened values seen in this run"
    foreach ($i in $H, $P) {
        $vals = @(Get-RunLines $i | Where-Object { $_ -match 'TotalChestsOpened' })
        if ($vals.Count -eq 0) { Note "  [$i] no TotalChestsOpened line at all"; continue }
        $vals | Select-Object -Last 12 | ForEach-Object { Note "  [$i] $_" }
    }
}

# ===========================================================================
# ITEM 2 -- loot hiding on one machine when the other takes the drop
# ===========================================================================
function Invoke-LootHiding([int]$H, [int]$P, [string]$step) {
    Step "ITEM 2 -- loot hiding: both beside the SAME enemy, kill, take the drop on the peer"
    $mH = Get-Mark $H
    $mP = Get-Mark $P
    # Stand close enough that the drop's own overlap radius is reached after the kill.
    Set-Control $H 'qd.near' '60'
    Set-Control $P 'qd.near' '60'
    Invoke-Goto $H 'enemy'
    Invoke-Goto $P 'enemy'
    Start-Sleep -Seconds 8
    # What each machine is looking at: the GUIDs must agree (authored key, adopted copy).
    foreach ($i in $H, $P) {
        $e = @(Get-Since $i $mH | Where-Object { $_ -match 'scan: kind enemy ' })
        if ($e.Count) { Note "  [$i] $($e[-1])" }
    }
    Set-Control $H 'qd.hit' '100000'
    Say '  hit[host] -> 100000 (one shot)'
    [void](Check $step $H 'rig: hit enemy guid=' 45 'the host applied a damaging hit' $mH)
    [void](Check $step $H 'enemy death recorded guid=' 45 'the host recorded the death' $mH)
    Set-Control $H 'qd.hit' $null
    Start-Sleep -Seconds 3
    foreach ($i in $H, $P) {
        $d = @(Get-Since $i $mH | Where-Object { $_ -match 'loot: drop ' })
        if ($d.Count) { $d | Select-Object -Last 4 | ForEach-Object { Note "  [$i] $_" } }
        else { Note "  [$i] no loot: drop line yet" }
    }
    [void](Check $step $H 'loot: drop ' 30 'the killer rolled a drop locally' $mH)
    [void](Check $step $P 'loot: drop ' 30 'the other machine holds its own copy of the drop' $mH)

    # Take it on the peer.
    $mP2 = Get-Mark $P
    $mH2 = Get-Mark $H
    Invoke-Press $P 'drop' $step -NoStandCheck
    Start-Sleep -Seconds 6
    $pick = @(Get-Since $P $mP2 | Where-Object { $_ -match 'loot: local pickup ' })
    $hide = @(Get-Since $H $mH2 | Where-Object { $_ -match 'loot: hiding local drop ' })
    Verdict $step $P ($pick.Count -gt 0) 'the peer took the drop (ledger tombstoned for the partner)' $(if ($pick.Count) { [string]$pick[-1] } else { '' })
    Verdict $step $H ($hide.Count -gt 0) 'the HOST hid its local copy (the measured hiding half)' $(if ($hide.Count) { [string]$hide[-1] } else { '' })
    Show-Since $P 'loot:' $mP2 12
    Show-Since $H 'loot:' $mH2 12
}

# ===========================================================================
# ITEM 3 -- boss bar and cutscene, in a reachable boss world
# ===========================================================================
function Invoke-BossCutscene([int]$H, [int]$P, [string]$step) {
    Step "ITEM 3 -- boss bar and cutscene in 210_AQ_underground (Whale King)"
    $mH = Get-Mark $H
    $mP = Get-Mark $P
    # Stand the pair inside the arena, beside the boss spawner.
    Invoke-Goto $H 'WhaleKingSpawner'
    Invoke-Goto $P 'WhaleKingSpawner'
    Say '  waiting 35 s for the encounter to start and the bars to rise'
    Start-Sleep -Seconds 35
    foreach ($i in $H, $P) {
        $b = @(Get-Since $i $mH | Where-Object { $_ -match 'scan: loose boss |scan: kind enemy |boss: |scan: health ' })
        if ($b.Count) { $b | Select-Object -Last 8 | ForEach-Object { Note "  [$i] $_" } }
        else { Note "  [$i] no boss/enemy line in the arena window" }
    }
    [void](Check $step $H 'boss: guid=.* is a boss' 45 'the host identified a boss by the game''s own predicate' $mH)
    [void](Check $step $H 'boss: ActivateBossHPBar called' 45 'the host raised its own boss bar' $mH)
    [void](Check $step $H 'boss: sent guid=.* bar on' 45 'the host published the bar' $mH)
    [void](Check $step $P 'boss: follower raised its own bar for guid=' 60 'the peer raised its OWN bar for its own copy' $mH)

    # The cutscene half.  Two measured routes: a manager property on the player, or a
    # placed LevelSequenceActor whose own SequencePlayer is playing.
    $mC = Get-Mark $P
    foreach ($i in $H, $P) {
        $c = @(Get-Since $i $mH | Where-Object { $_ -match 'cutscene:|scan: loose (sequence|cutscene)' })
        if ($c.Count) { $c | Select-Object -Last 8 | ForEach-Object { Note "  [$i] $_" } }
    }
    $started = @(Get-Since $P $mH | Where-Object { $_ -match 'cutscene: local started ' })
    $held    = @(Get-Since $H $mH | Where-Object { $_ -match 'cutscene: (holding the local player|playing the partner''s sequence)' })
    $resumed = @(Get-Since $H $mH | Where-Object { $_ -match 'cutscene: partner''s cutscene ended; resuming together' })
    Verdict $step $P ($started.Count -gt 0) 'a local cutscene was detected (manager or placed LevelSequenceActor)' $(if ($started.Count) { [string]$started[-1] } else { '' })
    Verdict $step $H ($held.Count -gt 0) 'the partner was held (or played the same sequence)' $(if ($held.Count) { [string]$held[-1] } else { '' })
    Verdict $step $H ($resumed.Count -gt 0) 'both were resumed together when it ended' $(if ($resumed.Count) { [string]$resumed[-1] } else { '' })
    Show-Since $P 'cutscene:|loose sequence|loose cutscene' $mH 12
    Show-Since $H 'cutscene:|loose sequence|loose cutscene' $mH 12
}

# ===========================================================================
# ITEM 4 -- five-minute two-player combat soak
# ===========================================================================
function Invoke-Soak([int]$H, [int]$P, [int]$seconds) {
    Step "ITEM 4 -- $seconds s two-player combat soak in 780_SZ_Depths"
    $mH = Get-Mark $H
    $mP = Get-Mark $P
    Set-Control $H 'qd.hit' '200'
    Set-Control $P 'qd.hit' '200'
    Say '  both machines hit the nearest locally-run enemy every 3 s'
    $deadline = (Get-Date).AddSeconds($seconds)
    $nextMove = (Get-Date).AddSeconds(20)
    while ((Get-Date) -lt $deadline) {
        if (-not (Test-Alive $H) -or -not (Test-Alive $P)) { break }
        if ((Get-Date) -ge $nextMove) {
            Invoke-Goto $H 'enemy'
            Invoke-Goto $P 'enemy'
            $nextMove = (Get-Date).AddSeconds(20)
        }
        Start-Sleep -Seconds 5
    }
    Set-Control $H 'qd.hit' $null
    Set-Control $P 'qd.hit' $null
    Start-Sleep -Seconds 3

    $aliveH = Test-Alive $H
    $aliveP = Test-Alive $P
    Verdict 'soak' $H $aliveH 'host alive at the end of the soak'
    Verdict 'soak' $P $aliveP 'peer alive at the end of the soak'

    # Death lists, by authored GUID: the same death must appear on both machines.
    $deathPat = 'enemy death recorded guid=([0-9A-Fa-f]{8})|enemy ([0-9A-Fa-f]{8}): the owner says dead'
    $deathsH = @(Get-Since $H $mH | Where-Object { $_ -match $deathPat } |
                 ForEach-Object {
                     if ($_ -match 'enemy death recorded guid=([0-9A-Fa-f]{8})') { $Matches[1].ToUpperInvariant() }
                     elseif ($_ -match 'enemy ([0-9A-Fa-f]{8}): the owner says dead') { $Matches[1].ToUpperInvariant() }
                 } | Sort-Object -Unique)
    $deathsP = @(Get-Since $P $mP | Where-Object { $_ -match $deathPat } |
                 ForEach-Object {
                     if ($_ -match 'enemy death recorded guid=([0-9A-Fa-f]{8})') { $Matches[1].ToUpperInvariant() }
                     elseif ($_ -match 'enemy ([0-9A-Fa-f]{8}): the owner says dead') { $Matches[1].ToUpperInvariant() }
                 } | Sort-Object -Unique)
    Note ("  host deaths: {0}; peer deaths: {1}" -f $deathsH.Count, $deathsP.Count)
    Note ("  host-only: {0}" -f (($deathsH | Where-Object { $deathsP -notcontains $_ }) -join ','))
    Note ("  peer-only: {0}" -f (($deathsP | Where-Object { $deathsH -notcontains $_ }) -join ','))
    $union = @($deathsH + $deathsP | Sort-Object -Unique)
    $agree = ($deathsH.Count -eq $deathsP.Count) -and
             (@($deathsH | Where-Object { $deathsP -notcontains $_ }).Count -eq 0) -and
             (@($deathsP | Where-Object { $deathsH -notcontains $_ }).Count -eq 0)
    Verdict 'soak' 0 $agree ("the two kill lists agree ({0} unique GUIDs)" -f $union.Count) `
        ("host={0} peer={1}" -f $deathsH.Count, $deathsP.Count)

    # Warning classes, counted by the token after WARN.
    foreach ($i in $H, $P) {
        $warn = @(Get-Since $i $mH | Where-Object { $_ -match '\bWARN\b' })
        $classes = @{}
        foreach ($w in $warn) {
            if ($w -match '\bWARN\s+([A-Za-z0-9_.-]+):') { $k = $Matches[1] } else { $k = '(unlabelled)' }
            $classes[$k] = 1 + [int]$classes[$k]
        }
        Note ("  [$i] WARN lines={0} classes={1}" -f $warn.Count,
              (($classes.GetEnumerator() | Sort-Object Value -Descending | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join ' '))
        $err = @(Get-Since $i $mH | Where-Object { $_ -match 'ERR |EXCEPTION|ACCESS_VIOLATION|\bfault' })
        Note ("  [$i] error/fault lines={0}" -f $err.Count)
        $err | Select-Object -Last 3 | ForEach-Object { Note "  [$i] $_" }
        Verdict 'soak' $i ($err.Count -eq 0) 'no error/fault line in the soak' ("count={0}" -f $err.Count)
    }
    # Desync-shaped warnings: the lanes' own names for a divergence.
    $desync = @((Get-Since $H $mH) + (Get-Since $P $mP) | Where-Object {
        $_ -match 'desync|diverge|disagree|mismatch|wrong world|stale' })
    Note ("  desync-shaped warning lines (both machines): {0}" -f $desync.Count)
    $desync | Select-Object -Last 6 | ForEach-Object { Note "  $_" }
    Verdict 'soak' 0 ($desync.Count -eq 0) 'zero desync-shaped warning lines' ("count={0}" -f $desync.Count)
}

# ===========================================================================
# The 780 world: no PlayerStart, no arrival marker -- the identity spawn is the
# arriving pawn, and the scenario moves it onto the altar's own ground before the
# arrival window closes.  210_AQ_underground was tried and REFUSED: it is a
# streaming sublevel with no GameMode, so a hard load of it produces NO PAWN at all
# (`arrive: ... ended with no pawn to place`), not even the identity spawn.
# ===========================================================================
function Invoke-Travel780([int]$H, [int]$P, [string]$step) {
    Step "TRAVEL -- both machines to 780_SZ_Depths (no PlayerStart, no marker)"
    Note 'Measured: 210_AQ_underground produces NO pawn; 780 produces the identity pawn,'
    Note 'so the scenario teleports it onto the altar before the arrival window closes.'
    $mH = Get-Mark $H
    $mP = Get-Mark $P
    Invoke-Hard $H '780_SZ_Depths'
    [void](Check $step $H 'rig: hard lever -> 780_SZ_Depths LoadMapWithLoadingScreen ran=1' 40 'the host took the hard transition' $mH)
    [void](Check $step $P 'travel: hard transition: loading 780_SZ_Depths' 70 'the follower took the same transition' $mP)
    [void](Check $step $P 'travel: host released the pair; resuming in 780_SZ_Depths' 100 'the handshake completed' $mP)
    Start-Sleep -Seconds 4
    Set-Control $H 'qd.near' '120'
    Set-Control $P 'qd.near' '120'
    Invoke-Goto $H 'SecretSword_Altar_BP'
    Invoke-Goto $P 'SecretSword_Altar_BP'
    $pH = Wait-NoOrigin $H $mH 40
    $pP = Wait-NoOrigin $P $mP 40
    Note ("  host at " + $(if ($pH) { ("{0:N0},{1:N0},{2:N0}" -f $pH[0], $pH[1], $pH[2]) } else { '(none)' }))
    Note ("  peer at " + $(if ($pP) { ("{0:N0},{1:N0},{2:N0}" -f $pP[0], $pP[1], $pP[2]) } else { '(none)' }))
    $standH = ($pH -ne $null) -and ([math]::Abs($pH[0]) -gt 150 -or [math]::Abs($pH[1]) -gt 150)
    $standP = ($pP -ne $null) -and ([math]::Abs($pP[0]) -gt 150 -or [math]::Abs($pP[1]) -gt 150)
    Verdict $step $H $standH 'host is standing off the identity spawn in 780' $(if ($pH) { ("{0:N0},{1:N0},{2:N0}" -f $pH[0], $pH[1], $pH[2]) } else { '' })
    Verdict $step $P $standP 'peer is standing off the identity spawn in 780' $(if ($pP) { ("{0:N0},{1:N0},{2:N0}" -f $pP[0], $pP[1], $pP[2]) } else { '' })
    Show-Since $H 'arrive:|travel: |dev teleport near' $mH 12
    Show-Since $P 'arrive:|travel: |dev teleport near' $mP 12
    Start-Sleep -Seconds 4
}

function Invoke-BossCutscene780([int]$H, [int]$P, [string]$step) {
    Step 'ITEM 3 -- 780_SZ_Depths: SecretSwordGetSequence cutscene + Green Dragon boss'
    # --- cutscene: the altar Blueprint starts the placed LevelSequenceActor ------------
    $mH = Get-Mark $H
    $mP = Get-Mark $P
    Invoke-Press $P 'SecretSword_Altar_BP' $step
    [void](Check $step $P 'dev interact: the game''s own gate says IsInteractible=' 30 'the peer asked the game about the altar' $mP)
    [void](Check $step $P 'dev interact: (the input event changed the object|target=.*verb=)' 30 'the peer''s real press ran a verb' $mP)
    [void](Check $step $P 'cutscene: local started ' 60 'the peer detected its own cutscene (placed LevelSequenceActor)' $mP)
    [void](Check $step $H 'cutscene: (holding the local player|playing the partner''s sequence)' 40 'the partner was held or played the same sequence' $mH)
    [void](Check $step $H 'cutscene: partner''s cutscene ended; resuming together' 120 'both resumed together' $mH)
    Show-Since $P 'dev interact|cutscene:|loose sequence' $mP 16
    Show-Since $H 'cutscene:|loose sequence' $mH 16

    # --- boss: the Green Dragon spawner ------------------------------------------------
    $mH2 = Get-Mark $H
    Invoke-Goto $H 'GreenDragon_Spawner'
    Invoke-Goto $P 'GreenDragon_Spawner'
    Say '  waiting 35 s beside the Green Dragon spawner for the encounter and the bars'
    Start-Sleep -Seconds 35
    foreach ($i in $H, $P) {
        $b = @(Get-Since $i $mH2 | Where-Object { $_ -match 'scan: kind enemy |scan: loose boss |boss: |scan: health ' })
        if ($b.Count) { $b | Select-Object -Last 8 | ForEach-Object { Note "  [$i] $_" } }
        else { Note "  [$i] no boss/enemy line in the arena window" }
    }
    [void](Check $step $H 'boss: guid=.* is a boss' 60 'the host identified a boss by the game''s own predicate' $mH2)
    [void](Check $step $H 'boss: ActivateBossHPBar called' 40 'the host raised its own boss bar' $mH2)
    [void](Check $step $H 'boss: sent guid=.* bar on' 40 'the host published the bar' $mH2)
    [void](Check $step $P 'boss: follower raised its own bar for guid=' 60 'the peer raised its OWN bar for its own copy' $mH2)
    Show-Since $H 'boss:|scan: kind enemy|scan: health' $mH2 14
    Show-Since $P 'boss:|scan: kind enemy|scan: health' $mH2 14
}

# ===========================================================================
# main
# ===========================================================================
Say "session13 start: phase=$Phase"
Stop-All
Seed-FreshPair
Start-Boot $Phase
$H = $script:H
$P = $script:P

if ($Phase -eq 'world780' -or $Phase -eq 'all') {
    Invoke-Travel780 $H $P 'travel780'
    Invoke-BossCutscene780 $H $P 'item3'
}

if ($Phase -eq 'items780' -or $Phase -eq 'all') {
    Invoke-Travel780 $H $P 'travel780'
    Invoke-ChestPair $H $P 'item1' '780_SZ_Depths'
    Invoke-LootHiding $H $P 'item2'
}

function Invoke-Travel900([int]$H, [int]$P, [string]$step) {
    Step "TRAVEL -- both machines to 900_SipehaWorld (marker arrival, Session 12)"
    $mH = Get-Mark $H
    $mP = Get-Mark $P
    Invoke-Hard $H '900_SipehaWorld'
    [void](Check $step $H 'rig: hard lever -> 900_SipehaWorld LoadMapWithLoadingScreen ran=1' 40 'the host took the hard transition' $mH)
    [void](Check $step $P 'travel: hard transition: loading 900_SipehaWorld' 70 'the follower took the same transition' $mP)
    [void](Check $step $P 'travel: host released the pair; resuming in 900_SipehaWorld' 100 'the handshake completed' $mP)
    Start-Sleep -Seconds 8
    [void](Check $step $H 'arrive: 900_SipehaWorld placed the player at ' 50 'the host is placed at the marker' $mH)
    [void](Check $step $P 'arrive: 900_SipehaWorld placed the player at ' 50 'the peer is placed (marker or partner)' $mP)
    # A fresh mark: a scan line from before the load is not a position in this world.
    $mH2 = Get-Mark $H
    $mP2 = Get-Mark $P
    $pH = Wait-NoOrigin $H $mH2 40
    $pP = Wait-NoOrigin $P $mP2 40
    Note ("  host at " + $(if ($pH) { ("{0:N0},{1:N0},{2:N0}" -f $pH[0], $pH[1], $pH[2]) } else { '(none)' }))
    Note ("  peer at " + $(if ($pP) { ("{0:N0},{1:N0},{2:N0}" -f $pP[0], $pP[1], $pP[2]) } else { '(none)' }))
    Verdict $step $H (($pH -ne $null) -and ([math]::Abs($pH[0]) -gt 150 -or [math]::Abs($pH[1]) -gt 150)) 'host stands off the identity spawn in 900' $(if ($pH) { ("{0:N0},{1:N0},{2:N0}" -f $pH[0], $pH[1], $pH[2]) } else { '' })
    Verdict $step $P (($pP -ne $null) -and ([math]::Abs($pP[0]) -gt 150 -or [math]::Abs($pP[1]) -gt 150)) 'peer stands off the identity spawn in 900' $(if ($pP) { ("{0:N0},{1:N0},{2:N0}" -f $pP[0], $pP[1], $pP[2]) } else { '' })
    Show-Since $H 'arrive:|travel: ' $mH 10
    Show-Since $P 'arrive:|travel: ' $mP 12
    Start-Sleep -Seconds 4
}

function Invoke-SipehaArena([int]$H, [int]$P, [string]$step) {
    Step 'ITEM 3 -- 900_SipehaWorld: Sipeha fight-start trigger, boss bar and cutscene'
    $mH = Get-Mark $H
    # First the spawner (the actor that owns the encounter), then the activation trigger.
    Invoke-Goto $H 'Sipeha_Spawner'
    Invoke-Goto $P 'Sipeha_Spawner'
    Say '  standing beside Sipeha_Spawner for 30 s'
    Start-Sleep -Seconds 30
    Invoke-Goto $H 'SipehaFightStartTrigger'
    Invoke-Goto $P 'SipehaFightStartTrigger'
    Say '  standing on SipehaFightStartTrigger for 30 s'
    Start-Sleep -Seconds 30
    foreach ($i in $H, $P) {
        $b = @(Get-Since $i $mH | Where-Object { $_ -match 'scan: loose boss |scan: loose sequence |scan: kind enemy |boss: |cutscene: |scan: health ' })
        if ($b.Count) { $b | Select-Object -Last 12 | ForEach-Object { Note "  [$i] $_" } }
        else { Note "  [$i] no boss/enemy/cutscene line in the arena window" }
    }
    [void](Check $step $H 'boss: guid=.* is a boss' 40 'the host identified a boss by the game''s own predicate' $mH)
    [void](Check $step $H 'boss: ActivateBossHPBar called' 30 'the host raised its own boss bar' $mH)
    [void](Check $step $H 'boss: sent guid=.* bar on' 30 'the host published the bar' $mH)
    [void](Check $step $P 'boss: follower raised its own bar for guid=' 40 'the peer raised its OWN bar for its own copy' $mH)
    [void](Check $step $P 'cutscene: local started ' 30 'a cutscene was detected on the peer' $mH)
    [void](Check $step $H 'cutscene: (holding the local player|playing the partner''s sequence)' 30 'the partner was held' $mH)
    [void](Check $step $H 'cutscene: partner''s cutscene ended; resuming together' 60 'both resumed together' $mH)
    Show-Since $H 'boss:|cutscene:|scan: loose boss|scan: loose sequence|scan: kind enemy' $mH 16
    Show-Since $P 'boss:|cutscene:|scan: loose boss|scan: loose sequence|scan: kind enemy' $mH 16
}

if ($Phase -eq 'soakHub') {
    Invoke-Soak $H $P $SoakSeconds
}

if ($Phase -eq 'lootMossy') {
    Step 'ITEM 2 -- hub: both beside the SAME Mossy_Fighter_BP_C, kill, take the drop'
    $mH = Get-Mark $H
    $mP = Get-Mark $P
    Set-Control $H 'qd.near' '60'
    Set-Control $P 'qd.near' '60'
    Invoke-Goto $H 'Mossy_Fighter_BP_C'
    Invoke-Goto $P 'Mossy_Fighter_BP_C'
    Start-Sleep -Seconds 10
    foreach ($i in $H, $P) {
        $e = @(Get-Since $i $mH | Where-Object { $_ -match 'scan: kind enemy ' })
        if ($e.Count) { Note "  [$i] $($e[-1])" }
        $t = @(Get-Since $i $mH | Where-Object { $_ -match 'dev teleport near' })
        if ($t.Count) { Note "  [$i] $($t[-1])" }
    }
    Set-Control $H 'qd.hit' '100000'
    Say '  hit[host] -> 100000 (one shot)'
    [void](Check 'item2' $H 'rig: hit enemy guid=' 45 'the host applied the hit' $mH)
    [void](Check 'item2' $H 'enemy death recorded guid=' 45 'the host recorded the death' $mH)
    Set-Control $H 'qd.hit' $null
    Start-Sleep -Seconds 6
    foreach ($i in $H, $P) {
        $d = @(Get-Since $i $mH | Where-Object { $_ -match 'loot: ' })
        if ($d.Count) { $d | Select-Object -Last 10 | ForEach-Object { Note "  [$i] $_" } }
        else { Note "  [$i] no loot: line yet" }
    }
    $dropH = @(Get-Since $H $mH | Where-Object { $_ -match 'loot: drop ' })
    $dropP = @(Get-Since $P $mH | Where-Object { $_ -match 'loot: drop ' })
    Verdict 'item2' $H ($dropH.Count -gt 0) 'the killer rolled a drop locally' $(if ($dropH.Count) { [string]$dropH[-1] } else { '' })
    Verdict 'item2' $P ($dropP.Count -gt 0) 'the other machine holds its own copy of the drop' $(if ($dropP.Count) { [string]$dropP[-1] } else { '' })
    $mP2 = Get-Mark $P
    $mH2 = Get-Mark $H
    Invoke-Press $P 'loot' 'item2' -NoStandCheck
    Start-Sleep -Seconds 10
    $pick = @(Get-Since $P $mP2 | Where-Object { $_ -match 'loot: local pickup ' })
    $hide = @(Get-Since $H $mH2 | Where-Object { $_ -match 'loot: hiding local drop ' })
    Verdict 'item2' $P ($pick.Count -gt 0) 'the peer took the drop (tombstoned for the partner)' $(if ($pick.Count) { [string]$pick[-1] } else { '' })
    Verdict 'item2' $H ($hide.Count -gt 0) 'the HOST hid its local copy' $(if ($hide.Count) { [string]$hide[-1] } else { '' })
    Show-Since $P 'loot:' $mP2 16
    Show-Since $H 'loot:' $mH2 16
}

if ($Phase -eq 'final') {
    # ---- item 2 in the hub: the enemy both machines share, then the ledger lever ----
    Step 'ITEM 2 -- hub: kill the shared enemy, take the drop on the peer'
    $mH = Get-Mark $H
    Set-Control $H 'qd.near' '60'
    Set-Control $P 'qd.near' '60'
    Invoke-Goto $H 'enemy'
    Invoke-Goto $P 'enemy'
    Start-Sleep -Seconds 10
    foreach ($i in $H, $P) {
        $e = @(Get-Since $i $mH | Where-Object { $_ -match 'scan: kind enemy ' })
        if ($e.Count) { Note "  [$i] $($e[-1])" }
    }
    Set-Control $H 'qd.hit' '100000'
    Say '  hit[host] -> 100000 (one shot)'
    [void](Check 'item2' $H 'rig: hit enemy guid=' 45 'the host applied the hit' $mH)
    [void](Check 'item2' $H 'enemy death recorded guid=' 45 'the host recorded the death' $mH)
    Set-Control $H 'qd.hit' $null
    Start-Sleep -Seconds 5
    foreach ($i in $H, $P) {
        $d = @(Get-Since $i $mH | Where-Object { $_ -match 'loot: ' })
        if ($d.Count) { $d | Select-Object -Last 8 | ForEach-Object { Note "  [$i] $_" } }
        else { Note "  [$i] no loot: line yet" }
    }
    $dropH = @(Get-Since $H $mH | Where-Object { $_ -match 'loot: drop ' })
    $dropP = @(Get-Since $P $mH | Where-Object { $_ -match 'loot: drop ' })
    Verdict 'item2' $H ($dropH.Count -gt 0) 'the killer rolled a drop locally' $(if ($dropH.Count) { [string]$dropH[-1] } else { '' })
    Verdict 'item2' $P ($dropP.Count -gt 0) 'the other machine holds its own copy of the drop' $(if ($dropP.Count) { [string]$dropP[-1] } else { '' })

    # Take the nearest ledger drop on the peer.  `near:loot` is the LEDGER lever; `near:drop`
    # matched a placed Panacea pickup in the first attempt and never touched the spawn.
    $mP2 = Get-Mark $P
    $mH2 = Get-Mark $H
    Invoke-Press $P 'loot' 'item2' -NoStandCheck
    Start-Sleep -Seconds 8
    $pick = @(Get-Since $P $mP2 | Where-Object { $_ -match 'loot: local pickup ' })
    $hide = @(Get-Since $H $mH2 | Where-Object { $_ -match 'loot: hiding local drop ' })
    Verdict 'item2' $P ($pick.Count -gt 0) 'the peer took the drop (tombstoned for the partner)' $(if ($pick.Count) { [string]$pick[-1] } else { '' })
    Verdict 'item2' $H ($hide.Count -gt 0) 'the HOST hid its local copy' $(if ($hide.Count) { [string]$hide[-1] } else { '' })
    Show-Since $P 'loot:' $mP2 14
    Show-Since $H 'loot:' $mH2 14

    # ---- item 1 at the door's EXACT root, which `near:` always offsets past ----
    Invoke-Travel900 $H $P 'travel900'
    Step 'ITEM 1 -- 900: exact-root press on Shop_Door_00_SM after GiveAllKeys'
    Set-Control $P 'qd.cheat' 'GiveAllKeys'
    Start-Sleep -Seconds 4
    Set-Control $P 'qd.near' '40'
    $mP3 = Get-Mark $P
    $mH3 = Get-Mark $H
    Set-Control $P 'qd.tp' '-2048,7202,2343,0'
    Say '  tp[peer] -> exact door root -2048,7202,2343'
    Start-Sleep -Seconds 6
    Set-Control $P 'qd.act' 'near:shop_door_00_sm'
    Say '  act[peer] -> near:shop_door_00_sm (exact-root press)'
    Start-Sleep -Seconds 8
    [void](Check 'item1' $P 'dev interact: the game''s own gate says IsInteractible=1' 30 'the door is interactible' $mP3)
    [void](Check 'item1' $P 'dev interact: the input event changed the object' 30 'the exact-root press CHANGED the door' $mP3)
    [void](Check 'item1' $P 'actor-state intent sent guid=' 30 'the peer published the opened state' $mP3)
    [void](Check 'item1' $H 'actor-state intent applied: guid=' 30 'the host applied it' $mH3)
    [void](Check 'item1' $H 'actor-state refresh ok .* opened=1|forced=1' 30 'the host''s door is open' $mH3)
    [void](Check 'item1' $P 'actor states applied=.*confirmed=1' 30 'the peer got the host echo' $mP3)
    Show-Since $P 'dev interact|actor-state|actor states' $mP3 24
    Show-Since $H 'actor-state|intent applied|refresh ok' $mH3 20
    foreach ($i in $H, $P) {
        $vals = @(Get-RunLines $i | Where-Object { $_ -match 'TotalChestsOpened' })
        if ($vals.Count -eq 0) { Note "  [$i] no TotalChestsOpened line at all" }
        else { $vals | Select-Object -Last 8 | ForEach-Object { Note "  [$i] $_" } }
    }
}

if ($Phase -eq 'door') {
    Invoke-Travel900 $H $P 'travel900'
    Step 'ITEM 1 -- 900: GiveAllKeys, then the peer opens Shop_Door_00_SM'
    $mP = Get-Mark $P
    Set-Control $P 'qd.cheat' 'GiveAllKeys'
    Say '  cheat[peer] -> GiveAllKeys'
    Start-Sleep -Seconds 5
    [void](Check 'item1' $P 'rig: cheat GiveAllKeys ran=1' 25 'the peer got the shop key' $mP)
    # A small offset keeps the pawn on the door's own ledge instead of 140 cm past it.
    Set-Control $P 'qd.near' '60'
    $mP2 = Get-Mark $P
    $mH2 = Get-Mark $H
    Invoke-Press $P 'Shop_Door_00_SM' 'item1'
    [void](Check 'item1' $P 'dev interact: the game''s own gate says IsInteractible=1' 30 'the game itself says the door is interactible' $mP2)
    [void](Check 'item1' $P 'dev interact: the input event changed the object' 30 'the peer''s real press CHANGED the door' $mP2)
    [void](Check 'item1' $P 'actor-state intent sent guid=' 30 'the peer published the opened state' $mP2)
    [void](Check 'item1' $H 'actor-state intent applied: guid=' 30 'the host applied it' $mH2)
    [void](Check 'item1' $H 'actor-state refresh ok .* opened=1|forced=1' 30 'the host''s door is open' $mH2)
    [void](Check 'item1' $P 'actor states applied=.*confirmed=1' 30 'the peer got the host echo (confirmed)' $mP2)
    Show-Since $P 'dev interact|actor-state|actor states' $mP2 24
    Show-Since $H 'actor-state|intent applied|refresh ok' $mH2 20
    # If the keyed press still did not move it, force the game's own open path on the peer
    # so the LANE is proven even when the content's own gate refuses the press.
    $mP3 = Get-Mark $P
    $mH3 = Get-Mark $H
    Set-Control $P 'qd.cheat' 'ForceOpenDoor'
    Say '  cheat[peer] -> ForceOpenDoor (the lane fallback, named in the log)'
    Start-Sleep -Seconds 8
    Show-Since $P 'rig: cheat|actor-state|dev state' $mP3 16
    Show-Since $H 'actor-state|intent applied' $mH3 16
}

if ($Phase -eq 'lootHub') {
    Step 'ITEM 2 -- hub: both beside the SAME enemy, kill, take the drop on the peer'
    $mH = Get-Mark $H
    $mP = Get-Mark $P
    Set-Control $H 'qd.near' '60'
    Set-Control $P 'qd.near' '60'
    Invoke-Goto $H 'enemy'
    Invoke-Goto $P 'enemy'
    Start-Sleep -Seconds 10
    foreach ($i in $H, $P) {
        $e = @(Get-Since $i $mH | Where-Object { $_ -match 'scan: kind enemy ' })
        if ($e.Count) { Note "  [$i] $($e[-1])" }
    }
    Set-Control $H 'qd.hit' '100000'
    Say '  hit[host] -> 100000 (one shot)'
    [void](Check 'item2' $H 'rig: hit enemy guid=' 45 'the host applied the hit' $mH)
    [void](Check 'item2' $H 'enemy death recorded guid=' 45 'the host recorded the death' $mH)
    Set-Control $H 'qd.hit' $null
    Start-Sleep -Seconds 4
    foreach ($i in $H, $P) {
        $d = @(Get-Since $i $mH | Where-Object { $_ -match 'loot: ' })
        if ($d.Count) { $d | Select-Object -Last 6 | ForEach-Object { Note "  [$i] $_" } }
        else { Note "  [$i] no loot: line yet" }
    }
    [void](Check 'item2' $H 'loot: drop ' 30 'the killer rolled a drop locally' $mH)
    [void](Check 'item2' $P 'loot: drop ' 30 'the other machine holds its own copy of the drop' $mH)
    $mP2 = Get-Mark $P
    $mH2 = Get-Mark $H
    Invoke-Press $P 'loot' 'item2' -NoStandCheck
    Start-Sleep -Seconds 8
    $pick = @(Get-Since $P $mP2 | Where-Object { $_ -match 'loot: local pickup ' })
    $hide = @(Get-Since $H $mH2 | Where-Object { $_ -match 'loot: hiding local drop ' })
    Verdict 'item2' $P ($pick.Count -gt 0) 'the peer took the drop (tombstoned for the partner)' $(if ($pick.Count) { [string]$pick[-1] } else { '' })
    Verdict 'item2' $H ($hide.Count -gt 0) 'the HOST hid its local copy' $(if ($hide.Count) { [string]$hide[-1] } else { '' })
    Show-Since $P 'loot:' $mP2 14
    Show-Since $H 'loot:' $mH2 14
}

if ($Phase -eq 'hub') {
    Step 'ITEM 3 (hub) -- the Shrine Boss encounter streamed into Ruins_Level_00'
    $mH = Get-Mark $H
    $mP = Get-Mark $P
    Invoke-Goto $H 'ShrinebossActivationTrigger'
    Invoke-Goto $P 'ShrinebossActivationTrigger'
    Say '  standing on the Shrine boss activation trigger for 30 s'
    Start-Sleep -Seconds 30
    Invoke-Goto $H 'ShrinebossSpawner'
    Invoke-Goto $P 'ShrinebossSpawner'
    Say '  standing beside the Shrine boss spawner for 30 s'
    Start-Sleep -Seconds 30
    foreach ($i in $H, $P) {
        $b = @(Get-Since $i $mH | Where-Object { $_ -match 'scan: loose boss |scan: kind enemy |boss: |cutscene: |scan: health |dev teleport' })
        if ($b.Count) { $b | Select-Object -Last 14 | ForEach-Object { Note "  [$i] $_" } }
        else { Note "  [$i] no boss/enemy/cutscene line in the Shrine window" }
    }
    [void](Check 'item3' $H 'boss: guid=.* is a boss' 40 'the host identified a boss by the game''s own predicate' $mH)
    [void](Check 'item3' $H 'boss: ActivateBossHPBar called' 30 'the host raised its own boss bar' $mH)
    [void](Check 'item3' $H 'boss: sent guid=.* bar on' 30 'the host published the bar' $mH)
    [void](Check 'item3' $P 'boss: follower raised its own bar for guid=' 40 'the peer raised its OWN bar' $mH)
    [void](Check 'item3' $P 'cutscene: local started ' 30 'a cutscene was detected on the peer' $mH)
    [void](Check 'item3' $H 'cutscene: (holding the local player|playing the partner''s sequence)' 30 'the partner was held' $mH)
    [void](Check 'item3' $H 'cutscene: partner''s cutscene ended; resuming together' 60 'both resumed together' $mH)
    Show-Since $H 'boss:|cutscene:|scan: loose boss|scan: kind enemy' $mH 16
    Show-Since $P 'boss:|cutscene:|scan: loose boss|scan: kind enemy' $mH 16
}

if ($Phase -eq 'tp2') {
    Step 'TELEPORTER 2 -- the hub TempTeleporter and an exact-root press'
    $mP = Get-Mark $P
    Set-Control $P 'qd.cheat' 'UnlockAllTeleporters'
    Start-Sleep -Seconds 4
    [void](Check 'tp2' $P 'rig: cheat UnlockAllTeleporters ran=1' 20 'teleporters unlocked' $mP)
    Set-Control $P 'qd.petrace' '1'
    Start-Sleep -Seconds 2
    Set-Control $P 'qd.petrace' '2'
    Invoke-Press $P 'TempTeleporter' 'tp2'
    Start-Sleep -Seconds 8
    Set-Control $P 'qd.petrace' '0'
    [void](Check 'tp2' $P 'dev interact: the game''s own gate says IsInteractible=' 25 'the hub TempTeleporter was asked' $mP)
    Show-Since $P 'dev interact|PE new:|travel verb:|travel: local destination|travel: hard transition' $mP 30
    # Exact root position for the measured Teleporter_GR (no 140 cm offset).
    $mP2 = Get-Mark $P
    Set-Control $P 'qd.tp' '13415,-1321,4237,0'
    Say '  tp[peer] -> exact Teleporter_GR root 13415,-1321,4237'
    Start-Sleep -Seconds 8
    Set-Control $P 'qd.act' 'near:teleporter_gr'
    Say '  act[peer] -> near:teleporter_gr (exact-root press)'
    Start-Sleep -Seconds 10
    [void](Check 'tp2' $P 'dev interact: the game''s own gate says IsInteractible=' 25 'the exact-root press was asked' $mP2)
    Show-Since $P 'dev interact|PE new:|travel verb:|travel: local destination|travel: hard transition|dev teleport' $mP2 30
    Show-Since $H 'travel: followed|travel: hard transition|travel: local destination' $mP2 12
}

if ($Phase -eq 'world900' -or $Phase -eq 'all') {
    Invoke-Travel900 $H $P 'travel900'
    Invoke-SipehaArena $H $P 'item3'
}

if ($Phase -eq 'tp') {
    Step 'TELEPORTER -- unlock all, then the real E press on the hub node'
    $mP = Get-Mark $P
    Set-Control $P 'qd.cheat' 'UnlockAllTeleporters'
    Say '  cheat[peer] -> UnlockAllTeleporters'
    Start-Sleep -Seconds 5
    [void](Check 'tp' $P 'rig: cheat UnlockAllTeleporters ran=1' 30 'the peer unlocked every fast-travel node' $mP)
    Set-Control $P 'qd.petrace' '1'
    Start-Sleep -Seconds 3
    Set-Control $P 'qd.petrace' '2'
    Invoke-Press $P 'teleport' 'tp'
    Start-Sleep -Seconds 12
    Set-Control $P 'qd.petrace' '0'
    [void](Check 'tp' $P 'dev interact: the game''s own gate says IsInteractible=' 30 'the press asked the game about the node' $mP)
    Show-Since $P 'dev interact|PE new:|travel verb:|travel: local destination|travel: hard transition|rig: cheat' $mP 30
    # A named node as well: the class walk may have picked a fragment.
    $mP2 = Get-Mark $P
    Invoke-Press $P 'Teleporter_GR' 'tp'
    Start-Sleep -Seconds 10
    Show-Since $P 'dev interact|PE new:|travel verb:|travel: local destination|travel: hard transition' $mP2 24
    # And the mod's travel lever on the same actor, as the fallback the task names.
    $mP3 = Get-Mark $P
    Set-Control $P 'qd.travel' 'near:teleport'
    Say '  travel[peer] -> near:teleport'
    Start-Sleep -Seconds 12
    [void](Check 'tp' $P 'rig: travel lever -> ' 30 'the game''s own TeleportToLocation ran with the node''s LocationData' $mP3)
    Show-Since $P 'rig: travel|travel: local destination|travel: hard transition|travel: followed' $mP3 20
    Show-Since $H 'travel: followed|travel: hard transition' $mP3 20
}

if ($Phase -eq 'items900' -or $Phase -eq 'all') {
    Invoke-Travel900 $H $P 'travel900'
    Step 'ITEM 1 -- 900_SipehaWorld: the peer opens the closed, interactible door'
    $mP2 = Get-Mark $P
    $mH2 = Get-Mark $H
    Invoke-Press $P 'Shop_Door_00_SM' 'item1'
    [void](Check 'item1' $P 'dev interact: the game''s own gate says IsInteractible=1' 40 'the game itself says this door is interactible' $mP2)
    [void](Check 'item1' $P 'dev interact: (the input event changed the object|target=.*verb=)' 40 'the peer''s real press ran its verb' $mP2)
    [void](Check 'item1' $P 'actor-state intent sent guid=' 40 'the peer published the opened state' $mP2)
    [void](Check 'item1' $H 'actor-state intent applied: guid=' 40 'the host applied the peer''s intent' $mH2)
    [void](Check 'item1' $H 'actor-state refresh ok .* opened=1|forced=1' 40 'the host''s own copy is open' $mH2)
    [void](Check 'item1' $P 'actor states applied=.*confirmed=1' 40 'the peer got the host echo (confirmed)' $mP2)
    Show-Since $P 'dev interact|actor-state|actor states' $mP2 20
    Show-Since $H 'actor-state|intent applied' $mH2 16
    # The other direction: the host acts on the authority, the peer refreshes.
    $mH3 = Get-Mark $H
    $mP3 = Get-Mark $P
    Invoke-Press $H 'Shop_Door_00_SM' 'item1'
    [void](Check 'item1' $H 'dev interact: the game''s own gate says IsInteractible=1' 40 'the host sees the same gate' $mH3)
    [void](Check 'item1' $H 'dev interact: (the input event changed the object|target=.*verb=)' 40 'the host''s real press ran its verb' $mH3)
    [void](Check 'item1' $P 'actor states applied=' 40 'the peer applied the host''s set' $mP3)
    [void](Check 'item1' $P 'actor-state refresh ok .* opened=|forced=1' 40 'the peer''s copy followed the host' $mP3)
    Show-Since $H 'dev interact|actor-state host set' $mH3 16
    Show-Since $P 'actor states applied|actor-state refresh' $mP3 16
    Step 'ITEM 1b -- TotalChestsOpened values (a door must not double count one)'
    foreach ($i in $H, $P) {
        $vals = @(Get-RunLines $i | Where-Object { $_ -match 'TotalChestsOpened' })
        if ($vals.Count -eq 0) { Note "  [$i] no TotalChestsOpened line at all" }
        else { $vals | Select-Object -Last 10 | ForEach-Object { Note "  [$i] $_" } }
    }
    Invoke-LootHiding $H $P 'item2'
}

if ($Phase -eq 'soak' -or $Phase -eq 'all') {
    Invoke-Travel900 $H $P 'travel900'
    Invoke-Soak $H $P $SoakSeconds
}

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
