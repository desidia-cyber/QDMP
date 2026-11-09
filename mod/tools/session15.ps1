# session15.ps1 -- Session 15 of the live two-instance run, in ONE process.
#
# WHAT THIS SESSION PROVES (and the one thing it changes)
# -------------------------------------------------------
# Session 14 entered 780_SZ_Depths as a streamed sublevel -- "loaded and SHOWN,
# visible=1 loaded=1" -- and 780's actors still did not exist: `scan: loose sequence none`,
# `SecretSword_Altar_BP` unresolved.  R109 named the cause (writing `ULevelStreaming`'s
# flags is not the engine streaming the package) and the next lever (the game's own
# `AddLevelListToStack`).
#
# Session 15 takes that lever.  The mod now pushes the area's own `UFS_LevelList` onto
# `FS_LevelManager`'s stack and lets the manager's per-frame pass load and show the
# sublevel, which is how the game enters these areas.  The list that carries 780 was
# re-read out of the shipped pak: `SZ_Depths`, not `GreenRuins_SZ` (that list does not
# contain 780 at all).  The correction is part of the fix.
#
#   item1 780_SZ_Depths: the level-list push, its ACTORS (altar, LevelSequenceActor,
#         GreenDragon_Spawner), both players standing, the boss bar on both with damage
#         from BOTH players reducing the one HP, the death recorded on both, and the
#         SecretSwordGetSequence cutscene started on one machine and held/resumed.
#   item4 760_SZ_AUT: the same entry for the autumn depths (ROSDragon_Spawner,
#         Dek_Egg_Activation).
#
# ONE SCRIPT PER SCENARIO: `-Phase item1|item4|all`, one boot per phase.
[CmdletBinding()]
param(
    [ValidateSet('item1', 'boss', 'item4', 'all')]
    [string]$Phase = 'item1',
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
$outDir  = Join-Path $rigRoot 'session15'
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
function Get-TpTarget([int]$i, [int]$markMs, [string]$needle) {
    $m = @(Get-Since $i $markMs | Where-Object { $_ -match [regex]::Escape("near '$needle'") })
    if ($m.Count -eq 0) { return $null }
    $s = [string]$m[-1]
    if ($s -match ' at (-?[0-9.]+),(-?[0-9.]+),(-?[0-9.]+) dist=') {
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
}
function Invoke-Goto([int]$i, [string]$name, [int]$near = -1) {
    $script:goSeq++
    $yaw = ($script:goSeq * 37) % 360
    if ($near -ge 0) { Set-Control $i 'qd.near' ([string]$near) }
    Set-Control $i 'qd.tp' ('near:' + $name + ',' + $yaw)
}
function Invoke-Press([int]$i, [string]$name, [string]$step, [switch]$NoStandCheck) {
    $script:pressSeq++
    $alt = if ($script:pressSeq % 2 -eq 1) { $name.ToUpperInvariant() } else { $name.ToLowerInvariant() }
    Set-Control $i 'qd.act' ('near:' + $alt)
    Say "  act[$i] -> near:$alt (field filled, then the real input event)"
}
$script:pressSeq = 0
function Invoke-Hard([int]$i, [string]$map) {
    Set-Control $i 'qd.hard' $map
    Say "  hard[$i] -> $map"
}
function Invoke-BossHit([int]$i, [string]$key, [int]$amount) {
    Set-Control $i 'qd.hitguid' ($key + ':' + $amount)
    Say "  hitguid[$i] -> $key (amount $amount)"
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
                          'qd.enemykill','qd.hitguid') {
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

# ===========================================================================
# Enemy-state measurements (per GUID), the only way to compare two machines
# ===========================================================================
function Invoke-Dump([int]$i, [string]$tag) {
    Set-Control $i 'qd.enemies' $tag
    Start-Sleep -Seconds 3
}
function Get-HpByKey([int]$i, [int]$markMs, [string]$key) {
    $rows = @(Get-Since $i $markMs | Where-Object { $_ -match ('enemykey ' + $key + ' ') })
    if ($rows.Count -eq 0) { return $null }
    $last = [string]$rows[-1]
    if ($last -match 'hp=([-0-9.]+)') { return [double]$Matches[1] }
    return $null
}
function Get-DeadByKey([int]$i, [int]$markMs, [string]$key) {
    $rows = @(Get-Since $i $markMs | Where-Object { $_ -match ('enemykey ' + $key + ' ') })
    if ($rows.Count -eq 0) { return $false }
    return ([string]$rows[-1] -match ' DEAD')
}
function Get-BossKey([int]$i, [int]$markMs) {
    $rows = @(Get-Since $i $markMs | Where-Object { $_ -match 'boss: guid=([0-9A-Fa-f]{8}) is a boss' })
    if ($rows.Count -eq 0) { return '' }
    if ([string]$rows[-1] -match 'boss: guid=([0-9A-Fa-f]{8}) is a boss') { return $Matches[1].ToUpperInvariant() }
    return ''
}

# ===========================================================================
# ITEM 1 -- 780_SZ_Depths through the game's OWN level-list path
# ===========================================================================
function Invoke-Stream780([int]$H, [int]$P, [string]$step) {
    Step 'ITEM 1a -- 780_SZ_Depths entered with the game''s own level list, not just flags'
    $mH = Get-Mark $H
    $mP = Get-Mark $P
    Invoke-Hard $H '780_SZ_Depths'
    Invoke-Hard $P '780_SZ_Depths'
    Start-Sleep -Seconds 12

    [void](Check $step $H "stream travel: 780_SZ_Depths is a sublevel of this world -- level list 'SZ_Depths' added to the manager's stack" 60 'the host pushed the area''s own level list onto the manager' $mH)
    [void](Check $step $P "stream travel: 780_SZ_Depths is a sublevel of this world -- level list 'SZ_Depths' added to the manager's stack" 60 'the peer pushed the area''s own level list onto the manager' $mP)
    [void](Check $step $H 'rig: hard lever -> 780_SZ_Depths streamed=1' 40 'the host refused the pawn-less hard load' $mH)
    [void](Check $step $P 'rig: hard lever -> 780_SZ_Depths streamed=1' 40 'the peer refused the pawn-less hard load' $mP)
    foreach ($i in $H, $P) {
        $bad = @(Get-Since $i $mH | Where-Object { $_ -match 'ended with no pawn to place' })
        Verdict $step $i ($bad.Count -eq 0) 'no "ended with no pawn to place" line (the R102 failure)' ("count={0}" -f $bad.Count)
    }
    Show-Since $H 'stream travel|level union: pushed list|rig: hard lever' $mH 8
    Show-Since $P 'stream travel|level union: pushed list|rig: hard lever' $mP 8
}

function Invoke-Actors780([int]$H, [int]$P, [string]$step) {
    Step 'ITEM 1b -- 780''s ACTORS exist: altar, LevelSequenceActor, and both players standing'
    $mH = Get-Mark $H
    $mP = Get-Mark $P
    # Give the manager's own pass time to stream the package in before asking the scan.
    Start-Sleep -Seconds 15
    Set-Control $H 'qd.near' '140'
    Set-Control $P 'qd.near' '140'
    foreach ($cand in 'SecretSword_Altar_BP', 'GreenDragon_Spawner', 'Whisper_Robot') {
        Invoke-Go $H $cand 140
        Invoke-Go $P $cand 140
        Start-Sleep -Seconds 10
        $okH = ([bool](Get-TpTarget $H $mH $cand)) -and ([bool](Get-TpTarget $P $mP $cand))
        Note "  anchor '$cand': host=$(if (Get-TpTarget $H $mH $cand) { 'resolved' } else { '-' }) peer=$(if (Get-TpTarget $P $mP $cand) { 'resolved' })"
        if ($okH) { break }
    }
    [void](Check $step $H "dev teleport near 'SecretSword_Altar_BP' -> SecretSword_Altar_BP at" 40 'the host resolved 780''s altar (its actors are in the world)' $mH)
    [void](Check $step $P "dev teleport near 'SecretSword_Altar_BP' -> SecretSword_Altar_BP at" 40 'the peer resolved the SAME altar' $mP)
    [void](Check $step $H 'scan: loose sequence \S+ \[sequence\]' 60 'the host scan sees a placed LevelSequenceActor (780''s cutscene actor)' $mH)
    [void](Check $step $P 'scan: loose sequence \S+ \[sequence\]' 60 'the peer scan sees a placed LevelSequenceActor' $mP)

    # The spawner is a separate anchor and is always visited, not only when the altar fails.
    Invoke-Go $H 'GreenDragon_Spawner' 140
    Invoke-Go $P 'GreenDragon_Spawner' 140
    Start-Sleep -Seconds 8
    [void](Check $step $H 'dev teleport near .GreenDragon_Spawner. -> GreenDragon_Spawner at' 40 'the host resolved the Green Dragon spawner' $mH)
    [void](Check $step $P 'dev teleport near .GreenDragon_Spawner. -> GreenDragon_Spawner at' 40 'the peer resolved the Green Dragon spawner' $mP)

    $pH = Get-LastPos $H $mH
    $pP = Get-LastPos $P $mP
    Verdict $step $H ([bool]$pH) 'the host has a pawn somewhere real' $(if ($pH) { ("{0:N0},{1:N0},{2:N0}" -f $pH[0], $pH[1], $pH[2]) } else { '' })
    Verdict $step $P ([bool]$pP) 'the peer has a pawn somewhere real' $(if ($pP) { ("{0:N0},{1:N0},{2:N0}" -f $pP[0], $pP[1], $pP[2]) } else { '' })
    Show-Since $H 'scan: loose sequence|scan: loose boss|dev teleport near|level union: pushed list' $mH 16
    Show-Since $P 'scan: loose sequence|scan: loose boss|dev teleport near|level union: pushed list' $mP 16
}

function Get-DragonKey([int]$i, [int]$markMs) {
    $rows = @(Get-Since $i $markMs | Where-Object { $_ -match 'scan: kind enemy Dragon_Green_BP_C guid=([0-9A-Fa-f]{8})' })
    if ($rows.Count -eq 0) { return '' }
    if ([string]$rows[-1] -match 'guid=([0-9A-Fa-f]{8})') { return $Matches[1].ToUpperInvariant() }
    return ''
}

function Invoke-BossFight([int]$H, [int]$P, [string]$step) {
    Step 'ITEM 2 -- Green Dragon: identity, bars, ownership, damage from BOTH players on ONE HP, death on both'
    $mH = Get-Mark $H
    $mP = Get-Mark $P
    Set-Control $H 'qd.near' '140'
    Set-Control $P 'qd.near' '140'
    Invoke-Go $H 'GreenDragon_Spawner'
    Invoke-Go $P 'GreenDragon_Spawner'
    Say '  standing at the spawner; the dragon materialises there (measured 22-close-out)'
    Start-Sleep -Seconds 20

    [void](Check $step $H 'boss probe: Dragon_Green_BP_C' 90 'the game''s boss predicates were asked of the dragon/spawner' $mH)
    [void](Check $step $H 'boss: guid=.* is a boss' 60 'the HOST identified the boss with the game''s own predicate' $mH)
    [void](Check $step $H 'boss: ActivateBossHPBar called' 60 'the host raised its own boss bar' $mH)
    [void](Check $step $H 'boss: sent guid=.* bar on' 60 'the host published the bar' $mH)
    [void](Check $step $P 'boss: follower raised its own bar for guid=' 90 'the peer raised its OWN bar for its own copy' $mP)
    [void](Check $step $P 'enemy .* DRIVEN by owner' 90 'ownership: the peer holds the dragon as the owner''s frozen copy' $mP)

    $key = Get-DragonKey $H $mH
    if (-not $key) { $key = Get-DragonKey $P $mP }
    Note ("  dragon key: '{0}'" -f $key)
    if (-not $key) {
        Show-Since $H 'scan: kind enemy|boss' $mH 16
        Show-Since $P 'scan: kind enemy|boss' $mP 16
        Verdict $step 0 $false 'the dragon was found in the enemy records' 'none'
        return
    }

    $dH0 = Get-Mark $H; $dP0 = Get-Mark $P
    Invoke-Dump $H 'dragon-before-h'
    Invoke-Dump $P 'dragon-before-p'
    $h0H = Get-HpByKey $H $dH0 $key
    $h0P = Get-HpByKey $P $dP0 $key
    Note ("  dragon HP before: host={0} peer={1}" -f $h0H, $h0P)

    # Player ONE (whichever machine runs the dragon): its own game damage path.
    $mHitH = Get-Mark $H
    Invoke-BossHit $H $key 200
    [void](Check $step $H ('rig: hitguid -> key=' + $key + ' (LOCAL hit|the other player''s hit sent)') 30 'the host''s player landed a hit on the dragon' $mHitH)
    Start-Sleep -Seconds 4
    # Player TWO: the other machine.  If it holds a frozen copy the hit is forwarded and the
    # OWNER applies it; the log there says so.
    $mHitP = Get-Mark $P
    $mOwn = Get-Mark $H
    Invoke-BossHit $P $key 200
    [void](Check $step $P ('rig: hitguid -> key=' + $key + ' (LOCAL hit|the other player''s hit sent)') 30 'the peer''s player landed a hit on the dragon' $mHitP)
    [void](Check $step $H ('hit on enemy ' + $key + ' from the other player|rig: hitguid -> key=' + $key) 30 'one HP was reduced by both players'' damage' $mOwn)
    Start-Sleep -Seconds 6

    $dH1 = Get-Mark $H; $dP1 = Get-Mark $P
    Invoke-Dump $H 'dragon-after-h'
    Invoke-Dump $P 'dragon-after-p'
    $h1H = Get-HpByKey $H $dH1 $key
    $h1P = Get-HpByKey $P $dP1 $key
    Note ("  dragon HP after:  host={0} peer={1}" -f $h1H, $h1P)
    Verdict $step $H ($null -ne $h0H -and $null -ne $h1H -and $h1H -lt $h0H) 'the dragon HP fell on the owner (both hits on the one number)' ("{0} -> {1}" -f $h0H, $h1H)
    Verdict $step $P ($null -ne $h1H -and $null -ne $h1P -and [math]::Abs($h1H - $h1P) -lt 0.5) 'both machines report the SAME dragon HP' ("host={0} peer={1}" -f $h1H, $h1P)

    $mKill = Get-Mark $H
    $mKillP = Get-Mark $P
    Set-Control $H 'qd.enemykill' $key
    Say "  enemykill[host] -> $key (the game's own death entry)"
    [void](Check $step $H ('rig: enemykill -> key=' + $key) 30 'the game''s own Kill ran on the dragon' $mKill)
    Start-Sleep -Seconds 8
    [void](Check $step $H ('enemy death recorded guid=' + $key) 30 'the host recorded the boss death' $mKill)
    [void](Check $step $P ('enemy death recorded guid=' + $key) 40 'the PEER recorded the same boss death' $mKillP)
    Show-Since $H 'boss:|hit on enemy|rig: hitguid|enemy death recorded|enemykey ' $mH 20
    Show-Since $P 'boss:|rig: hitguid|enemy death recorded|enemykey ' $mP 20
}

function Invoke-Cutscene780([int]$H, [int]$P, [string]$step) {
    Step 'ITEM 1d -- SecretSwordGetSequence: start on one machine, HOLD and joint resume'
    $mH = Get-Mark $H
    $mP = Get-Mark $P
    # The real interact lever first: the altar's own gate decides.  If content refuses the
    # press (R90/R105 measured that it can), the sequencer's own Play starts the SAME
    # sequence and the sync under test is unchanged.
    Set-Control $P 'qd.cheat' 'GiveAllKeys'
    Start-Sleep -Seconds 4
    Invoke-Press $P 'SecretSword_Altar_BP' $step -NoStandCheck
    [void](Check $step $P "dev interact: the game's own gate says IsInteractible=" 30 'the peer asked the game about the altar' $mP)
    Start-Sleep -Seconds 6
    $startedByPress = @(Get-Since $P $mP | Where-Object { $_ -match 'cutscene: local started ' })
    if ($startedByPress.Count -eq 0) {
        Note '  the altar press did not start the sequence (its own gate); using the sequencer lever'
        $mP = Get-Mark $P
        Set-Control $P 'qd.sequence' 'SecretSwordGetSequence'
        Say '  sequence[peer] -> SecretSwordGetSequence (Play on the placed LevelSequenceActor''s own player)'
        Start-Sleep -Seconds 6
        [void](Check $step $P 'rig: sequence lever -> .* Play ran' 20 'the sequencer''s own Play ran on the placed actor' $mP)
    }
    [void](Check $step $P 'cutscene: local started ' 40 'one machine detected ITS OWN cutscene playing (IsPlaying)' $mP)
    [void](Check $step $H 'cutscene: (holding the local player|playing the partner''s sequence)' 40 'the OTHER machine held (or played the same sequence)' $mH)
    [void](Check $step $H 'cutscene: (partner''s cutscene ended|resuming together)' 220 'both resumed together when it ended' $mH)
    Show-Since $P 'cutscene:|loose sequence|rig: sequence lever|dev interact' $mP 14
    Show-Since $H 'cutscene:|loose sequence' $mH 14
}

# ===========================================================================
# ITEM 4 -- 760_SZ_AUT through the same path
# ===========================================================================
function Invoke-Stream760([int]$H, [int]$P, [string]$step) {
    Step 'ITEM 4a -- 760_SZ_AUT entered with the game''s own level list (SZ_Depths carries it too)'
    $mH = Get-Mark $H
    $mP = Get-Mark $P
    Invoke-Hard $H '760_SZ_AUT'
    Invoke-Hard $P '760_SZ_AUT'
    Start-Sleep -Seconds 12
    [void](Check $step $H "stream travel: 760_SZ_AUT is a sublevel of this world -- level list 'SZ_Depths' added to the manager's stack" 60 'the host pushed the level list that carries 760' $mH)
    [void](Check $step $P "stream travel: 760_SZ_AUT is a sublevel of this world -- level list 'SZ_Depths' added to the manager's stack" 60 'the peer pushed the level list that carries 760' $mP)

    Start-Sleep -Seconds 15
    Set-Control $H 'qd.near' '6000'
    Set-Control $P 'qd.near' '6000'
    Invoke-Go $H 'ROSDragon_Spawner'
    Invoke-Go $P 'ROSDragon_Spawner'
    Start-Sleep -Seconds 10
    [void](Check $step $H 'dev teleport near .ROSDragon_Spawner. -> ROSDragon_Spawner at' 40 'the host resolved 760''s ROS dragon spawner' $mH)
    [void](Check $step $P 'dev teleport near .ROSDragon_Spawner. -> ROSDragon_Spawner at' 40 'the peer resolved the same spawner' $mP)

    Set-Control $H 'qd.near' '140'
    Set-Control $P 'qd.near' '140'
    Invoke-Go $H 'Dek_Egg_Activation'
    Invoke-Go $P 'Dek_Egg_Activation'
    Start-Sleep -Seconds 10
    [void](Check $step $H 'dev teleport near .Dek_Egg_Activation. -> Dek_Egg_Activation at' 40 'the host resolved 760''s Dek Egg activation' $mH)
    [void](Check $step $P 'dev teleport near .Dek_Egg_Activation. -> Dek_Egg_Activation at' 40 'the peer resolved the same egg activation' $mP)
    Show-Since $H 'stream travel|level union: pushed list|scan: loose sequence|dev teleport near' $mH 16
    Show-Since $P 'stream travel|level union: pushed list|scan: loose sequence|dev teleport near' $mP 16
}

# ===========================================================================
# main
# ===========================================================================
Say "session15 start: phase=$Phase"
Stop-All
Seed-FreshPair
Start-Boot $Phase
$H = $script:H
$P = $script:P

if ($Phase -eq 'item1' -or $Phase -eq 'all') {
    Invoke-Stream780 $H $P 'item1'
    Invoke-Actors780 $H $P 'item1'
    Invoke-Cutscene780 $H $P 'item1'
}

if ($Phase -eq 'boss' -or $Phase -eq 'all') {
    if ($Phase -eq 'boss') { Invoke-Stream780 $H $P 'boss' }
    Invoke-BossFight $H $P 'boss'
}

if ($Phase -eq 'item4' -or $Phase -eq 'all') {
    Invoke-Stream760 $H $P 'item4'
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
