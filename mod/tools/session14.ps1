# session14.ps1 -- Session 14 of the live two-instance run, in ONE process.
#
# WHAT THIS SESSION PROVES
# ------------------------
# Session 13 left three items NOT-REACHED, each with a measured reason.  This session is
# about those three, and each fix has a unit test beside it:
#
#   item1  KILL CONVERGENCE (R107).  The requirement is per GUID: an enemy killed on one
#          machine ends up dead (or recorded dead and never respawned) on the OTHER, which
#          machine killed it does not matter.  Only the HOST kills, with the peer parked
#          away from the fight so most kills land on keys the peer holds no actor for; then
#          both machines dump their per-GUID final state and the diff must be ZERO.
#          Fix: `qdenemy.h DeadLedger` (a death remembered by KEY and applied when a copy
#          appears) and `KillLog::kMax` 64 -> 512 (104 deaths in one soak overflowed it).
#   item2  780_SZ_Depths / 760_SZ_AUT are STREAMED SUBLEVELS, not worlds: a hard load of one
#          produces NO pawn (R102).  The follow now streams the sublevel and SHOWS it, the
#          way the game's own level manager does, and refuses the pawn-less world load.
#          Then: the Green Dragon boss detected + bar on both, and SecretSwordGetSequence
#          played on one machine, held on the other, joint resume.
#   item3  Loot hiding on an enemy BOTH machines hold by the SAME key.  R106's "different
#          GUIDs" was the nearest-enemy read of a per-machine scan; this run enumerates
#          every key on both, intersects, and drives both players to one of them
#          (`qd.tp guid:<key>`), kills it, takes the drop on one and watches the other hide.
#
# Every control file is created before launch because the environment is not inherited
# reliably through the launch chain (measured with QDCOOP_TEST_TPFILE).
[CmdletBinding()]
param(
    [ValidateSet('item1', 'item2', 'item3', 'all')]
    [string]$Phase = 'item1',
    [int]$BootSeconds = 300,
    [int]$SoakSeconds = 180,
    [string]$Report = ''
)

$ErrorActionPreference = 'Stop'
$here    = $PSScriptRoot
$repo    = Split-Path -Parent (Split-Path -Parent $here)
$gameDir = 'G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64'
$relay   = Join-Path (Split-Path -Parent $here) 'rust\target\release\qd-relay.exe'
$rigRoot = 'G:\Freespace\Queens_Domain\_test'
$seed    = Join-Path $repo 'analysis\save-backup-before-live'
$outDir  = Join-Path $rigRoot 'session14'
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
        Enemies = Join-Path $root 'qd.enemies'; Sequence = Join-Path $root 'qd.sequence'
        EnemyKill = Join-Path $root 'qd.enemykill'
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
                          'qd.use','qd.pdmg','qd.kill','qd.petrace','qd.enemies','qd.sequence',
                          'qd.enemykill') {
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
# SESSION 14 helpers
# ===========================================================================
# `near:<name>` lands each machine beside ITS OWN nearest actor with that name, so a
# scenario that needs "both players at the SAME enemy" cannot use it.  `guid:<8 hex>` is
# the key, and both machines resolve it to their own copy of that one enemy.
$script:guidSeq = 0
function Invoke-GoGuid([int]$i, [string]$key, [int]$near = -1) {
    $script:guidSeq++
    if ($near -ge 0) { Set-Control $i 'qd.near' ([string]$near) }
    $yaw = ($script:guidSeq * 47 + $i * 13) % 360
    Set-Control $i 'qd.tp' ('guid:' + $key + ',' + $yaw)
    Say "  tp[$i] -> guid:$key (yaw $yaw, near=$(if ($near -ge 0) { $near } else { 'unchanged' }))"
}

# The per-GUID state dump (qdenemy.h DeadLedger is what makes a machine report keys it
# holds no actor for).  The lever fires on a CONTENT change, so the token differs each call.
function Invoke-Dump([int]$i, [string]$tag) {
    Set-Control $i 'qd.enemies' $tag
}
function Get-KeyDump([int]$i, [int]$markMs) {
    $rows = @{}
    foreach ($ln in @(Get-Since $i $markMs)) {
        $s = [string]$ln
        if ($s -match 'enemykey ([0-9A-Fa-f]{8}) (ALIVE|DEAD)') {
            $rows[$Matches[1].ToUpperInvariant()] = $Matches[2].ToUpperInvariant()
        }
    }
    return $rows
}
function Get-KeyHp([int]$i, [int]$markMs) {
    $rows = @{}
    foreach ($ln in @(Get-Since $i $markMs)) {
        $s = [string]$ln
        if ($s -match 'enemykey ([0-9A-Fa-f]{8}) (ALIVE|DEAD) local=\S+ src=\S+ hp=(-?[0-9.]+)') {
            $rows[$Matches[1].ToUpperInvariant()] = [double]$Matches[3]
        }
    }
    return $rows
}
function Dump-Both([int]$H, [int]$P, [string]$tag) {
    $mH = Get-Mark $H
    $mP = Get-Mark $P
    Invoke-Dump $H $tag
    Invoke-Dump $P $tag
    Start-Sleep -Seconds 8
    return [pscustomobject]@{
        Host = (Get-KeyDump $H $mH); Peer = (Get-KeyDump $P $mP)
        HostHp = (Get-KeyHp $H $mH); PeerHp = (Get-KeyHp $P $mP)
        HostMark = $mH; PeerMark = $mP
    }
}
function Show-Dump([int]$i, [int]$markMs, [int]$n) {
    Show-Since $i 'enemykey ' $markMs $n
}
# The altar/dragon/sentry resolve the same way on both machines only because both run the
# same cooked package; the teleport line carries the actor's own position, which is the
# ground truth a placement is checked against.
function Get-TpTarget([int]$i, [int]$markMs, [string]$needle) {
    $m = @(Get-Since $i $markMs | Where-Object { $_ -match [regex]::Escape("near '$needle'") })
    if ($m.Count -eq 0) { return $null }
    $s = [string]$m[-1]
    if ($s -match ' at (-?[0-9.]+),(-?[0-9.]+),(-?[0-9.]+) dist=') {
        return @([double]$Matches[1], [double]$Matches[2], [double]$Matches[3])
    }
    return $null
}

# ===========================================================================
# ITEM 1 -- kill convergence, proved per GUID
# ===========================================================================
# R107 measured host=104 deaths, peer=49, 55 GUIDs host-only -- and called it "by design".
# It is not the co-op requirement: the requirement is that an enemy killed on ONE machine
# ends up dead (or recorded dead and never respawned) on the other, regardless of which
# machine killed it.  The 49 were the keys the peer HAD an actor for; a death addressed to
# a copy has nothing to land on when there is no copy.  qdenemy.h's DeadLedger remembers a
# death by KEY and applies it whenever a copy appears, so the two dead SETS must now agree.
#
# The soak kills on the HOST ONLY.  What it can and cannot produce is measured, so the
# evidence is taken from BOTH halves: the two machines' own death records (which is exactly
# what R107 counted: 104 vs 49), and their per-GUID final state (which is the requirement).
function Get-OwnDeaths([int]$i) {
    return @(Get-RunLines $i |
             Where-Object { $_ -match 'enemy death recorded guid=([0-9A-Fa-f]{8})' } |
             ForEach-Object { $Matches[1].ToUpperInvariant() } | Sort-Object -Unique)
}
function Invoke-KillConvergence([int]$H, [int]$P, [int]$seconds) {
    Step "ITEM 1 -- kill convergence: only the HOST kills for ${seconds}s, then both machines dump per-GUID state"
    Note '  Requirement: an enemy killed on one machine is dead on the other, whichever killed it.'
    Note '  R107 (before this fix): host=104 deaths, peer=49, 55 GUIDs host-only (no copy to kill).'

    Set-Control $H 'qd.god' '400'
    Set-Control $P 'qd.god' '400'
    Set-Control $H 'qd.near' '120'
    Invoke-Go $H 'enemy' 120
    Start-Sleep -Seconds 8

    $mH = Get-Mark $H
    Set-Control $H 'qd.hit' '100000'
    Say '  hit[host] -> 100000 (the ONLY machine that kills in this phase)'
    $deadline = (Get-Date).AddSeconds($seconds)
    $nextMove = Get-Date
    while ((Get-Date) -lt $deadline) {
        if (-not (Test-Alive $H) -or -not (Test-Alive $P)) { break }
        if ((Get-Date) -ge $nextMove) {
            # Invoke-Go varies the yaw, so the instruction is always NEW: the tp lever
            # ignores a repeated identical line (measured, Session 13's soak never moved).
            Invoke-Go $H 'enemy'
            $nextMove = (Get-Date).AddSeconds(10)
        }
        Start-Sleep -Seconds 4
    }
    Set-Control $H 'qd.hit' $null
    Start-Sleep -Seconds 12

    $hits = @(Get-Since $H $mH | Where-Object { $_ -match 'rig: hit enemy guid=' })
    $hitRan = @($hits | Where-Object { $_ -match 'ran=1' })
    $hitZero = @($hits | Where-Object { $_ -match 'hp 0\.0 -> 0\.0' })
    Note ("  soak: {0} hit(s) attempted, {1} landed (ran=1), {2} found the target already at 0 HP" -f $hits.Count, $hitRan.Count, $hitZero.Count)
    Note '  (the seeded hub save has its enemies persisted at HP 0 -- measured: `ran=0 hp 0.0 -> 0.0`)'
    $hits | Select-Object -Last 4 | ForEach-Object { Note "  [H] $_" }
    Verdict 'item1' $H ($hits.Count -gt 0) 'the host ran the kill lever for the whole soak' ("{0} hit(s)" -f $hits.Count)

    # ---- R107's own measurement, from each machine's OWN records ---------------------
    Step 'ITEM 1b -- the two machines'' own death records (R107 counted exactly this)'
    $hOwn = Get-OwnDeaths $H
    $pOwn = Get-OwnDeaths $P
    $hostOnlyOwn = @($hOwn | Where-Object { $pOwn -notcontains $_ })
    $peerOnlyOwn = @($pOwn | Where-Object { $hOwn -notcontains $_ })
    Note ("  host recorded {0} death(s) itself; the peer recorded {1} itself" -f $hOwn.Count, $pOwn.Count)
    Note ("  host-only (the peer held no copy for these): {0} -> {1}" -f $hostOnlyOwn.Count, ($hostOnlyOwn -join ','))
    Note ("  peer-only: {0}" -f ($peerOnlyOwn -join ','))
    Verdict 'item1' 0 ($hostOnlyOwn.Count -gt 0) 'the soak produced the R107 condition (keys only ONE machine recorded)' ("{0} host-only" -f $hostOnlyOwn.Count)

    # ---- the requirement: per-GUID FINAL STATE -------------------------------------
    Step 'ITEM 1c -- per-GUID FINAL STATE on both machines (the co-op requirement)'
    $d = Dump-Both $H $P 'k1'
    $hk = $d.Host
    $pk = $d.Peer
    $hostDead = @($hk.Keys | Where-Object { $hk[$_] -eq 'DEAD' })
    $peerDead = @($pk.Keys | Where-Object { $pk[$_] -eq 'DEAD' })
    $killed = @(@($hostDead) + @($peerDead) | Sort-Object -Unique)
    $hostMissing = @($killed | Where-Object { -not ($hk.ContainsKey($_) -and $hk[$_] -eq 'DEAD') })
    $peerMissing = @($killed | Where-Object { -not ($pk.ContainsKey($_) -and $pk[$_] -eq 'DEAD') })
    # The direct proof of the fix: the keys this machine never had a copy for must still be
    # DEAD in its dump -- that is what the DeadLedger is for.
    $notCarried = @($hostOnlyOwn | Where-Object { -not ($pk.ContainsKey($_) -and $pk[$_] -eq 'DEAD') })

    Note ("  host keys={0} (dead {1}); peer keys={2} (dead {3})" -f $hk.Count, $hostDead.Count, $pk.Count, $peerDead.Count)
    Note ("  KILLED SET (dead on either machine): {0} unique GUID(s)" -f $killed.Count)
    Note ("  dead on the host but NOT on the peer: {0} -> {1}" -f $hostMissing.Count, ($hostMissing -join ','))
    Note ("  dead on the peer but NOT on the host: {0} -> {1}" -f $peerMissing.Count, ($peerMissing -join ','))

    Verdict 'item1' 0 ($killed.Count -gt 0) 'the run produced a non-trivial kill set' ("{0} killed GUIDs" -f $killed.Count)
    Verdict 'item1' $H ($hostMissing.Count -eq 0) 'every killed GUID is reported DEAD by the host' ("missing={0}" -f $hostMissing.Count)
    Verdict 'item1' $P ($peerMissing.Count -eq 0) 'every killed GUID is reported DEAD by the peer (the DeadLedger half)' ("missing={0} -> {1}" -f $peerMissing.Count, ($peerMissing -join ','))
    Verdict 'item1' $P ($notCarried.Count -eq 0) 'every key ONLY the host ever recorded is DEAD on the peer' ("carried from the wire={0}/{1}, still missing={2}" -f ($hostOnlyOwn.Count - $notCarried.Count), $hostOnlyOwn.Count, ($notCarried -join ','))
    $diff = $hostMissing.Count + $peerMissing.Count
    Verdict 'item1' 0 ($diff -eq 0) 'the per-GUID kill sets DIFF TO ZERO' ("host={0} peer={1} diff={2}" -f $hostDead.Count, $peerDead.Count, $diff)
    if ($diff -ne 0 -or $notCarried.Count -ne 0) {
        Show-Dump $H $d.HostMark 30
        Show-Dump $P $d.PeerMark 30
    }
}

# ===========================================================================
# ITEM 2 -- 780_SZ_Depths: the streaming mechanism, both players, boss + cutscene
# ===========================================================================
# R102: a HARD load of 780 (or 760, or 210) leaves the machine with NO pawn at all --
# "arrive: ... ended with no pawn to place", and not one `scan: local at` line -- because
# these maps are SUBLEVELS of the persistent world and their package has no GameMode that
# spawns one.  The game enters them by streaming the sublevel in (`AFS_LevelManager`); the
# follow now does the same (proxy.cpp StreamTravelTo) and REFUSES the world load.
function Invoke-Stream780([int]$H, [int]$P, [string]$step) {
    Step 'ITEM 2a -- 780_SZ_Depths reached as a STREAMED SUBLEVEL, never a pawn-less world load'
    $mH = Get-Mark $H
    $mP = Get-Mark $P
    Invoke-Hard $H '780_SZ_Depths'
    Invoke-Hard $P '780_SZ_Depths'
    Start-Sleep -Seconds 10
    [void](Check $step $H 'stream travel: 780_SZ_Depths is a sublevel of this world' 40 'the host streams the sublevel the way the game does' $mH)
    [void](Check $step $H 'rig: hard lever -> 780_SZ_Depths streamed=1' 40 'the host refused the pawn-less hard load' $mH)
    [void](Check $step $P 'stream travel: 780_SZ_Depths is a sublevel of this world' 40 'the peer streams the same sublevel' $mP)
    [void](Check $step $P 'rig: hard lever -> 780_SZ_Depths streamed=1' 40 'the peer refused the pawn-less hard load' $mP)
    foreach ($i in $H, $P) {
        $bad = @(Get-Since $i $mH | Where-Object { $_ -match 'ended with no pawn to place' })
        Verdict $step $i ($bad.Count -eq 0) 'no "ended with no pawn to place" line (the R102 failure)' ("count={0}" -f $bad.Count)
    }

    # Both players are PLACED inside the sublevel: the actor only exists here if the sublevel
    # is loaded and SHOWN, so resolving it is itself the proof that it is in the world.
    # Three of 780's own persistent-level actors are tried, because the altar is the one the
    # CUTSCENE needs and a checkpoint is the one that proves the LEVEL is in the world.
    Set-Control $H 'qd.near' '120'
    Set-Control $P 'qd.near' '120'
    $anchor = ''
    foreach ($cand in 'SecretSword_Altar_BP', 'Checkpoint19', 'Teleporter_Forest', 'Whisper_Robot') {
        Invoke-Go $H $cand 120
        Invoke-Go $P $cand 120
        Start-Sleep -Seconds 8
        $okH = ([bool](Get-TpTarget $H $mH $cand)) -and ([bool](Get-TpTarget $P $mP $cand))
        Note "  anchor '$cand': host=$(if (Get-TpTarget $H $mH $cand) { 'resolved' } else { '-' }) peer=$(if (Get-TpTarget $P $mP $cand) { 'resolved' } else { '-' })"
        if ($okH) { $anchor = $cand; break }
    }
    Verdict $step 0 ($anchor -ne '') 'at least one of 780''s own placed actors resolved on BOTH machines (its level is in the world)' ("anchor='$anchor'")
    if ($anchor -eq '') {
        Show-Since $H 'scan: loose|dev teleport near|stream travel' $mH 14
        Show-Since $P 'scan: loose|dev teleport near|stream travel' $mP 14
        return
    }
    [void](Check $step $H 'dev teleport near' 20 'the host resolved a 780 actor (its sublevel is in the world)' $mH)
    [void](Check $step $P 'dev teleport near' 20 'the peer resolved the same 780 actor' $mP)
    $tH = Get-TpTarget $H $mH $anchor
    $tP = Get-TpTarget $P $mP $anchor
    $same = ([bool]$tH) -and ([bool]$tP) -and
            ([math]::Abs($tH[0] - $tP[0]) -lt 1) -and ([math]::Abs($tH[1] - $tP[1]) -lt 1)
    Verdict $step 0 $same 'both machines resolve the SAME altar at the SAME coordinates' $(if ($same) { ("{0:N0},{1:N0},{2:N0}" -f $tH[0], $tH[1], $tH[2]) } else { 'mismatch' })
    $mHp = Get-Mark $H
    $mPp = Get-Mark $P
    $pH = Wait-NoOrigin $H $mHp 30
    $pP = Wait-NoOrigin $P $mPp 30
    Verdict $step $H ([bool]$pH) 'the host is standing somewhere real in 780 (a pawn exists)' $(if ($pH) { ("{0:N0},{1:N0},{2:N0}" -f $pH[0], $pH[1], $pH[2]) } else { '' })
    Verdict $step $P ([bool]$pP) 'the peer is standing somewhere real in 780 (a pawn exists)' $(if ($pP) { ("{0:N0},{1:N0},{2:N0}" -f $pP[0], $pP[1], $pP[2]) } else { '' })
    # The strong form: the pawn must be standing AT the 780 altar, not merely off the origin
    # (the hub position is off the origin too, which is why "off origin" alone proves nothing).
    if ($tH -ne $null -and $pH -ne $null) {
        $dxH = $pH[0] - $tH[0]; $dyH = $pH[1] - $tH[1]
        $dH = [math]::Sqrt($dxH * $dxH + $dyH * $dyH)
        Verdict $step $H ($dH -lt 800) 'the host stands AT the 780 altar (inside the streamed sublevel)' ("{0:N0} cm away" -f $dH)
    }
    if ($tP -ne $null -and $pP -ne $null) {
        $dxP = $pP[0] - $tP[0]; $dyP = $pP[1] - $tP[1]
        $dP = [math]::Sqrt($dxP * $dxP + $dyP * $dyP)
        Verdict $step $P ($dP -lt 800) 'the peer stands AT the same 780 altar' ("{0:N0} cm away" -f $dP)
    }
    Show-Since $H 'stream travel|rig: hard lever|arrive:|dev teleport near' $mH 10
    Show-Since $P 'stream travel|rig: hard lever|arrive:|dev teleport near' $mP 10
}

function Invoke-Cutscene780([int]$H, [int]$P, [string]$step) {
    Step 'ITEM 2b -- SecretSwordGetSequence: play on one machine, HOLD and joint resume on both'
    # Two routes, both the game's own: the real E press on the altar (the authored trigger),
    # and the sequencer's own `Play` on the placed LevelSequenceActor (the call that altar
    # Blueprint makes).  The press is tried first; if the content's gate refuses it, the
    # sequencer lever starts the SAME sequence and the sync under test is unchanged.
    # The partner's side of the window starts BEFORE the sequence is started: the hold is
    # logged the moment the publishing machine says it is playing.
    $mH = Get-Mark $H
    Set-Control $P 'qd.cheat' 'GiveAllKeys'
    Start-Sleep -Seconds 4
    $mP = Get-Mark $P
    Invoke-Press $P 'SecretSword_Altar_BP' $step -NoStandCheck
    [void](Check $step $P 'dev interact: the game''s own gate says IsInteractible=' 30 'the peer asked the game about the altar' $mP)
    Start-Sleep -Seconds 6
    $startedByPress = @(Get-Since $P $mP | Where-Object { $_ -match 'cutscene: local started ' })
    if ($startedByPress.Count -eq 0) {
        Note '  the altar press did not start the sequence (its own gate); using the sequencer lever'
        $mP = Get-Mark $P
        Set-Control $P 'qd.sequence' 'first'
        Say '  sequence[peer] -> first (Play on the placed LevelSequenceActor''s own player)'
        Start-Sleep -Seconds 6
        [void](Check $step $P 'rig: sequence lever -> .* Play ran' 20 'the sequencer''s own Play ran on the placed actor' $mP)
    }
    [void](Check $step $P 'cutscene: local started ' 40 'one machine detected ITS OWN cutscene playing (the detector reads IsPlaying)' $mP)
    [void](Check $step $H 'cutscene: (holding the local player|playing the partner''s sequence)' 40 'the OTHER machine held (or played the same sequence)' $mH)
    [void](Check $step $H 'cutscene: partner''s cutscene ended; resuming together' 150 'both resumed together when it ended' $mH)
    Show-Since $P 'cutscene:|loose sequence|rig: sequence lever|dev interact' $mP 14
    Show-Since $H 'cutscene:|loose sequence' $mH 14
}

function Invoke-Boss780([int]$H, [int]$P, [string]$step) {
    Step 'ITEM 2c -- the Green Dragon boss in 780: detected on the owner, the bar raised on BOTH'
    $mH = Get-Mark $H
    $mP = Get-Mark $P
    Invoke-Go $H 'GreenDragon_Spawner' 150
    Invoke-Go $P 'GreenDragon_Spawner' 150
    Say '  waiting 45 s beside the Green Dragon spawner for the encounter and the bars'
    Start-Sleep -Seconds 45
    foreach ($i in $H, $P) {
        $b = @(Get-Since $i $mH | Where-Object { $_ -match 'scan: kind enemy |scan: loose boss |boss: |scan: health ' })
        if ($b.Count) { $b | Select-Object -Last 8 | ForEach-Object { Note "  [$i] $_" } }
        else { Note "  [$i] no boss/enemy line in the arena window" }
    }
    [void](Check $step $H 'boss: guid=.* is a boss' 45 'the host identified the boss with the game''s own predicate' $mH)
    [void](Check $step $H 'boss: ActivateBossHPBar called' 40 'the host raised its own boss bar' $mH)
    [void](Check $step $H 'boss: sent guid=.* bar on' 40 'the host published the bar' $mH)
    [void](Check $step $P 'boss: follower raised its own bar for guid=' 60 'the peer raised its OWN bar for its own copy' $mH)
    Show-Since $H 'boss:|scan: kind enemy|scan: health' $mH 12
    Show-Since $P 'boss:|scan: kind enemy|scan: health' $mH 12
}

# ===========================================================================
# ITEM 3 -- loot hiding on a key BOTH machines hold, whose class drops
# ===========================================================================
# R106's two reasons for NOT-REACHED were both per-machine reads of a nearest-enemy scan:
# "the shared enemy drops nothing" (900's Sipeha) and "the hub's copies differ by GUID".
# Neither is a property of the content; the nearest enemy on two different machines simply
# IS two different enemies.  So: enumerate EVERY key on both, intersect, and pick one.
function Invoke-LootCommon([int]$H, [int]$P, [string]$step) {
    Step 'ITEM 3 -- loot hiding: a key BOTH machines hold, both players at it, kill, take, hide'
    $d = Dump-Both $H $P 'loot1'
    $common = @()
    foreach ($k in @($d.Host.Keys)) {
        if ($d.Host[$k] -ne 'ALIVE') { continue }
        if (-not $d.Peer.ContainsKey($k)) { continue }
        if ($d.Peer[$k] -ne 'ALIVE') { continue }
        # A live record is not a live enemy: this save has most of the hub persisted at HP 0,
        # and a 0-HP actor cannot be killed (so it cannot drop).  Pick by HP.
        $hp = $d.HostHp[$k]
        if ($null -eq $hp -or $hp -le 0) { continue }
        $common += $k
    }
    Note ("  host keys={0}; peer keys={1}; COMMON keys ALIVE WITH HP on both={2}" -f $d.Host.Count, $d.Peer.Count, $common.Count)
    if ($common.Count -gt 0) { Note ("  common: {0}" -f ($common -join ',')) }
    Verdict $step 0 ($common.Count -gt 0) 'the two machines hold at least one enemy by the SAME key' ("{0} common key(s)" -f $common.Count)
    if ($common.Count -eq 0) {
        Show-Dump $H $d.HostMark 25
        Show-Dump $P $d.PeerMark 25
        return
    }

    $tries = [Math]::Min($common.Count, 8)
    for ($c = 0; $c -lt $tries; $c++) {
        $key = [string]$common[$c]
        Note ("  -- candidate {0}/{1}: key={2}" -f ($c + 1), $tries, $key)
        Invoke-GoGuid $H $key 140
        Invoke-GoGuid $P $key 140
        Start-Sleep -Seconds 10

        $mH = Get-Mark $H
        $mP = Get-Mark $P
        Set-Control $H 'qd.hit' '100000'
        $hit = @(Wait-Since $H 'rig: hit enemy guid=' $mH 30)
        Set-Control $H 'qd.hit' $null
        $hitKey = ''
        foreach ($ln in $hit) { if ($ln -match 'rig: hit enemy guid=([0-9A-Fa-f]{8})') { $hitKey = $Matches[1].ToUpperInvariant() } }
        if ($hitKey -ne $key) {
            Note ("  the nearest enemy to the host was {0}, not {1}: the hit lever is a nearest-enemy lever" -f $(if ($hitKey) { $hitKey } else { '(none)' }), $key)
            continue
        }
        Start-Sleep -Seconds 4
        $died = @(Get-Since $H $mH | Where-Object { $_ -match 'enemy death recorded guid=' })
        if ($died.Count -eq 0) {
            # Most of this build's enemy classes do not implement TryApplyDamage, so the
            # damage lane cannot produce the death (`ran=0`).  The game's own `Kill` on the
            # actor can, and it goes through the same death path -- so the drop is still the
            # game's drop, not the mod's.
            Note '  the damage lane did not land (this class has no TryApplyDamage): driving the enemy''s own Kill'
            Set-Control $H 'qd.enemykill' $key
            Say "  enemykill[host] -> $key (the game's own death entry)"
            [void](Check $step $H 'rig: enemykill -> key=.* (the enemy''s own (Kill|Die) ran|Die ran)' 20 'the game''s own Kill ran on the shared enemy' $mH)
            Start-Sleep -Seconds 4
            $died = @(Get-Since $H $mH | Where-Object { $_ -match 'enemy death recorded guid=' })
        }
        [void](Check $step $H 'enemy death recorded guid=' 30 'the HOST killed the shared enemy and recorded the death' $mH)
        Start-Sleep -Seconds 6
        $dropsH = @(Get-Since $H $mH | Where-Object { $_ -match 'loot: drop ' })
        $dropsP = @(Get-Since $P $mP | Where-Object { $_ -match 'loot: drop ' })
        Note ("  key={0}: loot:drop lines host={1} peer={2}" -f $key, $dropsH.Count, $dropsP.Count)
        if ($dropsH.Count -eq 0 -or $dropsP.Count -eq 0) {
            Note '  this class drops nothing on one of the machines; trying the next shared key'
            continue
        }
        Note ("  host drop: {0}" -f [string]$dropsH[-1])
        Note ("  peer drop: {0}" -f [string]$dropsP[-1])

        # Take it on ONE machine; the ledger's tombstone must hide the other machine's copy.
        $mTake = Get-Mark $P
        $mWatch = Get-Mark $H
        Invoke-Press $P 'loot' $step -NoStandCheck
        Start-Sleep -Seconds 10
        $pickP = @(Get-Since $P $mTake | Where-Object { $_ -match 'loot: local pickup ' })
        $hideH = @(Get-Since $H $mWatch | Where-Object { $_ -match 'loot: hiding local drop ' })
        $pickH = @(Get-Since $H $mWatch | Where-Object { $_ -match 'loot: local pickup ' })
        $hideP = @(Get-Since $P $mTake | Where-Object { $_ -match 'loot: hiding local drop ' })
        if (($pickP.Count -eq 0 -and $pickH.Count -eq 0) -or ($hideH.Count -eq 0 -and $hideP.Count -eq 0)) {
            # One more attempt from the other machine: a pickup is a press, and the content
            # decides whether this drop answers an E press or an overlap.
            $mTake = Get-Mark $H
            $mWatch = Get-Mark $P
            Invoke-Press $H 'loot' $step -NoStandCheck
            Start-Sleep -Seconds 10
            $pickH = @(Get-Since $H $mTake | Where-Object { $_ -match 'loot: local pickup ' })
            $hideP = @(Get-Since $P $mWatch | Where-Object { $_ -match 'loot: hiding local drop ' })
            $pickP = @(Get-Since $P $mWatch | Where-Object { $_ -match 'loot: local pickup ' })
            $hideH = @(Get-Since $H $mTake | Where-Object { $_ -match 'loot: hiding local drop ' })
        }
        $took = @(@($pickP) + @($pickH))
        $hid = @(@($hideH) + @($hideP))
        Verdict $step 0 ($took.Count -gt 0) 'one machine took the drop (ledger tombstoned)' $(if ($took.Count) { [string]$took[0] } else { '' })
        Verdict $step 0 ($hid.Count -gt 0) 'the OTHER machine hid its local copy of the SAME drop' $(if ($hid.Count) { [string]$hid[0] } else { '' })
        if ($took.Count -gt 0 -and $hid.Count -gt 0) {
            Note ("  SHARED KEY: {0} (both machines held it; the drop is the same derived key)" -f $key)
            Show-Since $H 'loot:' ([Math]::Max(1, $mWatch)) 14
            Show-Since $P 'loot:' ([Math]::Max(1, $mTake)) 14
            return
        }
        Note '  the drop was taken but the partner did not hide its copy; trying the next shared key'
    }
    Note '  no shared key produced a drop on both machines in this run'
}

# ===========================================================================
# main
# ===========================================================================
Say "session14 start: phase=$Phase"
Stop-All
Seed-FreshPair
Start-Boot $Phase
$H = $script:H
$P = $script:P

if ($Phase -eq 'item1' -or $Phase -eq 'all') {
    Invoke-KillConvergence $H $P $SoakSeconds
}

if ($Phase -eq 'item2' -or $Phase -eq 'all') {
    Invoke-Stream780 $H $P 'item2'
    Invoke-Cutscene780 $H $P 'item2'
    Invoke-Boss780 $H $P 'item2'
}

if ($Phase -eq 'item3' -or $Phase -eq 'all') {
    Invoke-LootCommon $H $P 'item3'
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
    Note ("  {0,-10} inst{1}  {2,-6}  {3}  {4}" -f $v.Step, $v.Inst, $(if ($v.Ok) { 'PASS' } else { 'FAIL' }), $v.What, $v.Line)
}
$pass = @($script:verdicts | Where-Object { $_.Ok }).Count
$fail = @($script:verdicts | Where-Object { -not $_.Ok }).Count
Note ("=== {0} PASS, {1} FAIL ===" -f $pass, $fail)
$script:lines | Set-Content -LiteralPath $Report
Say "report: $Report"
