# session10.ps1 -- Session 10 of the live two-instance run, in ONE process.
#
# WHY IT IS ONE PROCESS
# ---------------------
# A launched game does not survive the command that started it (LIVE-TEST-LOG R14), so
# the relay, both instances, the driving, the collection and the teardown all happen
# inside this script.
#
# WHAT IT COVERS (Session 10's five items)
# ----------------------------------------
#   r83    Item 1: reproduce the host's silent exit at the suspend reload under load,
#          now with WER LocalDumps, -abslog, a flushed mod log, and memory/handle
#          samples.  The Session 9 dump pinned it to DialoguePump + stale g_blockedNpc.
#   items  Items 2-5: controlled value change on a fresh save pair; the loot hiding
#          half; reaching cutscene/elevator/boss content; dialogue ends on death.
#
# EVERY control file is created before launch because the environment is not inherited
# reliably through the launch chain (measured with QDCOOP_TEST_TPFILE).
[CmdletBinding()]
param(
    [ValidateSet('r83', 'items', 'all')]
    [string]$Phase = 'r83',
    [int]$BootSeconds = 300,
    [int]$Runs = 2,
    [string]$Report = ''
)

$ErrorActionPreference = 'Stop'
$here    = $PSScriptRoot
$repo    = Split-Path -Parent (Split-Path -Parent $here)
$gameDir = 'G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64'
$exe     = Join-Path $gameDir 'QueensDomain-Win64-Shipping.exe'
$relay   = Join-Path (Split-Path -Parent $here) 'rust\target\release\qd-relay.exe'
$rigRoot = 'G:\Freespace\Queens_Domain\_test'
$seed    = Join-Path $repo 'analysis\save-backup-before-live'
$outDir  = Join-Path $rigRoot 'session10'
$dumpDir = Join-Path $outDir 'dumps'
if (-not $Report) { $Report = Join-Path $outDir ('report-' + $Phase + '.txt') }
foreach ($d in $outDir, $dumpDir) {
    if (-not (Test-Path $d)) { New-Item -ItemType Directory -Force -Path $d | Out-Null }
}

$script:lines = New-Object System.Collections.Generic.List[string]
trap {
    Write-Output ("UNHANDLED: " + $_.Exception.Message)
    Write-Output ("AT: " + $_.InvocationInfo.PositionMessage)
    Write-Output ("STACK: " + $_.ScriptStackTrace)
    $script:lines.Add("UNHANDLED: " + $_.Exception.Message) | Out-Null
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
        Pid = "$root.pid"
    }
}
$R1 = Get-RigPaths 1
$R2 = Get-RigPaths 2
$inst = @{ 1 = $R1; 2 = $R2 }

function Get-RunLines([int]$i) {
    $p = $inst[$i].Log
    if (-not (Test-Path -LiteralPath $p)) { return @() }
    $all = @(Get-Content -LiteralPath $p -ErrorAction SilentlyContinue)
    if ($all.Count -eq 0) { return @() }
    $start = 0
    for ($k = $all.Count - 1; $k -ge 0; $k--) {
        if ($all[$k] -match 'hooked FEngineLoop') { $start = $k; break }
    }
    return @($all[$start..($all.Count - 1)])
}
function Get-Matches([int]$i, [string]$pattern) {
    return ,@(Get-RunLines $i | Where-Object { $_ -match $pattern })
}
function Wait-Pattern([int]$i, [string]$pattern, [int]$seconds) {
    $deadline = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $deadline) {
        $m = Get-Matches $i $pattern
        if ($m.Count -gt 0) { return $m }
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
function Get-Mark([int]$i) { return (Get-RunLines $i).Count }
function Test-Alive([int]$i) {
    if (-not $script:pids.ContainsKey($i)) { return $false }
    return [bool](Get-Process -Id $script:pids[$i] -ErrorAction SilentlyContinue)
}
function Get-Since([int]$i, [int]$mark) {
    $a = @(Get-RunLines $i)
    if ($a.Count -le $mark) { return ,@() }
    return ,@($a[$mark..($a.Count - 1)])
}
function Wait-Since([int]$i, [string]$pattern, [int]$mark, [int]$seconds) {
    $deadline = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $deadline) {
        $m = @(Get-Since $i $mark | Where-Object { $_ -match $pattern })
        if ($m.Count -gt 0) { return ,$m }
        if (-not (Test-Alive $i)) { return ,@() }
        Start-Sleep -Milliseconds 500
    }
    return ,@()
}

$script:verdicts = New-Object System.Collections.Generic.List[object]
function Check([string]$step, [int]$i, [string]$pattern, [int]$secs, [string]$label,
               [int]$Mark = 0, [switch]$Absent) {
    if (-not (Test-Alive $i)) {
        $script:verdicts.Add([pscustomobject]@{ Step = $step; Inst = $i; What = $label; Ok = $false; Line = 'INSTANCE DIED' }) | Out-Null
        Note "  FAIL [$i] $label -- the instance is no longer running"
        return $false
    }
    $m = Wait-Since $i $pattern $Mark $secs
    $found = ($m.Count -gt 0)
    $ok = if ($Absent) { -not $found } else { $found }
    $script:verdicts.Add([pscustomobject]@{ Step = $step; Inst = $i; What = $label; Ok = $ok; Line = ($m | Select-Object -Last 1) }) | Out-Null
    if ($ok) { Note "  PASS [$i] $label" }
    else { Note "  --   [$i] $label $(if ($Absent) { '(the line APPEARED)' } else { "(no line matches '$pattern' in ${secs}s)" })" }
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
function Get-LastPos([int]$i) {
    $m = Get-Matches $i 'scan: local at '
    if ($m.Count -eq 0) { return $null }
    if ($m[-1] -match 'local at (-?[0-9.]+),(-?[0-9.]+),(-?[0-9.]+)') {
        return @([double]$Matches[1], [double]$Matches[2], [double]$Matches[3])
    }
    return $null
}
function Invoke-ActNear([int]$i, [string]$name) {
    Set-Control $i 'qd.act' ('near:' + $name)
    Say "  act[$i] -> near:$name"
}
$script:goSeq = 0
function Invoke-Go([int]$i, [string]$name, [int]$near = -1) {
    $script:goSeq++
    $yaw = ($script:goSeq * 37) % 360
    if ($near -ge 0) { Set-Control $i 'qd.near' ([string]$near) }
    Set-Control $i 'qd.tp' ('near:' + $name + ',' + $yaw)
    Say "  tp[$i] -> near:$name (yaw $yaw, near=$(if ($near -ge 0) { $near } else { 'unchanged' }))"
}
function Invoke-Cheat([int]$i, [string]$fn, [string]$arg = '') {
    if ($arg) { Set-Content -LiteralPath $inst[$i].CheatArg -Value $arg -NoNewline } else { Remove-Item -LiteralPath $inst[$i].CheatArg -ErrorAction SilentlyContinue }
    Set-Content -LiteralPath $inst[$i].Cheat -Value $fn -NoNewline
    Say "  cheat[$i] -> $fn $(if ($arg) { "(arg $arg)" })"
}
function Set-Cheats { param($fn) }

function Stop-All {
    Get-Process -Name 'QueensDomain-Win64-Shipping', 'qd-relay' -ErrorAction SilentlyContinue |
        ForEach-Object {
            Say "  stopping leftover $($_.ProcessName) pid=$($_.Id)"
            Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue
        }
    Start-Sleep -Seconds 2
}

function Install-And-Configure([switch]$AllSwitches) {
    $built = Join-Path $repo 'mod\native\build\winmm.dll'
    if (-not (Test-Path -LiteralPath $built)) { Say "REFUSED: built proxy not found at $built"; exit 2 }
    $dst = Join-Path $gameDir 'winmm.dll'
    try {
        Copy-Item -LiteralPath $built -Destination $dst -Force
    } catch {
        # Windows refuses to overwrite an image that a process still maps, and the
        # process can be one the session cannot enumerate.  Renaming the mapped image
        # works (the mapping follows the file); the next launch loads the new one.
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

function Enable-WerLocalDumps {
    $exeName = 'QueensDomain-Win64-Shipping.exe'
    $key = "HKCU:\Software\Microsoft\Windows\Windows Error Reporting\LocalDumps\$exeName"
    try {
        New-Item -Path $key -Force | Out-Null
        New-ItemProperty -Path $key -Name DumpFolder -Value $dumpDir -PropertyType ExpandString -Force | Out-Null
        New-ItemProperty -Path $key -Name DumpType -Value 2 -PropertyType DWord -Force | Out-Null
        New-ItemProperty -Path $key -Name DumpCount -Value 10 -PropertyType DWord -Force | Out-Null
        Note "WER LocalDumps: $key -> $dumpDir (DumpType=2 full)"
    } catch { Note "WER LocalDumps setup failed: $($_.Exception.Message)" }
}

function Get-ProcSample([int]$i) {
    if (-not $script:pids.ContainsKey($i)) { return $null }
    $p = Get-Process -Id $script:pids[$i] -ErrorAction SilentlyContinue
    if (-not $p) { return $null }
    return [pscustomobject]@{
        SetMB   = [math]::Round($p.WorkingSet64 / 1MB, 1)
        PrivMB  = [math]::Round($p.PrivateMemorySize64 / 1MB, 1)
        Handles = $p.HandleCount
    }
}
function Sample-Line([int]$i, [string]$when) {
    $s = Get-ProcSample $i
    if (-not $s) { Note "  [$i] $when : process gone"; return }
    Note ("  [$i] {0}: working={1} MB private={2} MB handles={3}" -f $when, $s.SetMB, $s.PrivMB, $s.Handles)
}

# ===========================================================================
# A boot: teardown, install, switches, relay, both instances, roles
# ===========================================================================
function Start-Boot([string]$tag) {
    Stop-All
    Install-And-Configure
    foreach ($i in 1, 2) {
        foreach ($leaf in 'qd.tp','qd.act','qd.hit','qd.cheat','qd.cheatarg','qd.travel','qd.hard',
                          'qd.level','qd.near','qd.drops','qd.menu','qd.efunc','qd.eattack',
                          'qd.use','qd.pdmg','qd.kill') {
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
    while ((Get-Date) -lt $deadline) {
        foreach ($i in 1, 2) {
            if ($roles.ContainsKey($i)) { continue }
            if (-not $script:pids.ContainsKey($i)) { continue }
            $m = Get-Matches $i 'session established'
            if ($m.Count -gt 0) { $roles[$i] = if ($m[-1] -match 'role=HOST') { 'HOST' } else { 'PEER' } }
        }
        if ($roles.Count -eq 2) { break }
        Start-Sleep -Seconds 1
    }
    foreach ($i in 1, 2) {
        if ($roles.ContainsKey($i)) {
            $m = Get-Matches $i 'session established'
            Say "inst$i role=$($roles[$i])  <- $($m[-1])"
        } else { Say "inst$i NEVER ESTABLISHED A SESSION" }
    }
    $hostI = @($roles.Keys | Where-Object { $roles[$_] -eq 'HOST' } | Select-Object -First 1)
    $peerI = @($roles.Keys | Where-Object { $roles[$_] -eq 'PEER' } | Select-Object -First 1)
    if ($hostI.Count -eq 0 -or $peerI.Count -eq 0) {
        Say 'ABORT: no host/peer pair; see the logs'
        $script:H = 1; $script:P = 2
    } else { $script:H = [int]$hostI[0]; $script:P = [int]$peerI[0]; Say "host=inst$($script:H) peer=inst$($script:P)" }

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
# ITEM 1 -- R83: the host survives the suspend reload; two full runs
# ===========================================================================
function Invoke-R83Run([int]$run) {
    Step "R83 RUN $run -- the suspend reload with a blocked NPC, under load, fresh pair" ''
    Stop-All
    Seed-FreshPair
    Start-Boot ("r83-$run")
    $H = $script:H; $P = $script:P
    Sample-Line $H 'boot'
    Sample-Line $P 'boot'

    foreach ($i in 1, 2) {
        $m = Get-Matches $i 'switches:|NAMED-SAVE ORACLE|container offsets:|enemy sync: FS_EnemyCharacterBase|player body: class'
        $m | Select-Object -Last 6 | ForEach-Object { if ($_.Length -gt 300) { Note ("  [$i] " + $_.Substring(0,300) + ' ...') } else { Note "  [$i] $_" } }
    }

    # Let the enemy set grow to the load this scenario is about (~250).
    Say 'sitting in gameplay while the spawners fill the world'
    Start-Sleep -Seconds 45
    foreach ($i in 1, 2) {
        Sample-Line $i 'pre-scenario'
        $m = Get-Matches $i 'enemy sync: ([0-9]+) live enemies'
        Note "  [$i] last live-enemy line: $(if ($m.Count) { $m[-1] } else { '(none)' })"
    }

    # The condition the crash needed: a peer conversation whose NPC this host blocks.
    $mH = Get-Mark $H; $mP = Get-Mark $P
    Invoke-Go $P 'npc' 200
    Start-Sleep -Seconds 8
    Invoke-ActNear $P 'npc'
    Check 'r83' $P 'dialogue: local started' 30 'the peer starts a conversation' $mP | Out-Null
    Check 'r83' $H 'dialogue: BLOCK ' 40 'the host blocks that NPC (g_blockedNpc is set)' $mH | Out-Null
    Show-Since $H 'dialogue:' $mH 6
    Show-Since $P 'dialogue:' $mP 6

    # The reload that killed it.
    Sample-Line $H 'before reload'
    Invoke-Cheat $H 'SuspendSave'
    Start-Sleep -Seconds 12
    $mH2 = Get-Mark $H
    Invoke-Cheat $H 'LoadSaveSlot'
    Say 'waiting for the reload to complete (or the process to die)'
    $deadline = (Get-Date).AddSeconds(150)
    $survived = $true
    $droppedSeen = $false
    $n = 0
    while ((Get-Date) -lt $deadline) {
        if (-not (Test-Alive $H)) { $survived = $false; break }
        if (-not $droppedSeen) {
            $d = @(Get-Since $H $mH2 | Where-Object { $_ -match 'dialogue: blocked NPC dropped with its world' })
            if ($d.Count -gt 0) { $droppedSeen = $true }
        }
        if (($n % 10) -eq 0) { Sample-Line $H "reload +$($n*5)s"; Sample-Line $P "reload +$($n*5)s" }
        $n++
        if ($droppedSeen -and (@(Get-Since $H $mH2 | Where-Object { $_ -match 'save layer re-initialised \(new active save\)' }).Count -gt 0)) { break }
        Start-Sleep -Seconds 5
    }
    $alive = Test-Alive $H
    Note "  $(if ($alive) { 'PASS' } else { 'FAIL' }) [$H] alive after the suspend reload"
    $script:verdicts.Add([pscustomobject]@{ Step = 'r83'; Inst = $H; What = 'host alive after the suspend reload'; Ok = $alive; Line = '' }) | Out-Null
    Sample-Line $H 'after reload'
    Sample-Line $P 'after reload'

    $m = @(Get-Since $H $mH2)
    $drop = @($m | Where-Object { $_ -match 'dialogue: blocked NPC dropped with its world' })
    Note "  $(if ($drop.Count) { 'PASS' } else { '--  ' }) [$H] the stale blocked NPC was dropped without a read"
    if ($drop.Count) { Note "  [$H] $($drop[-1])" }
    $script:verdicts.Add([pscustomobject]@{ Step = 'r83'; Inst = $H; What = 'stale blocked NPC dropped with its world'; Ok = ($drop.Count -gt 0); Line = ($drop | Select-Object -Last 1) }) | Out-Null

    $rel = @($m | Where-Object { $_ -match 'save layer re-initialised \(new active save\)' })
    Note "  $(if ($rel.Count) { 'PASS' } else { '--  ' }) [$H] the reload completed ($($rel.Count) re-init line(s))"

    $fault = @($m | Where-Object { $_ -match 'ERR |EXCEPTION|ACCESS_VIOLATION|fatal|\bfault' })
    Note "  [$H] error/fault lines after the reload: $($fault.Count)"
    $fault | Select-Object -Last 4 | ForEach-Object { Note "  [$H] $_" }

    # Did the game leave a UE crash report / WER dump for this run?
    $crash = Get-ChildItem (Join-Path $inst[$H].Root ('Saved_' + $inst[$H].Suffix + '\Crashes')) -Directory -ErrorAction SilentlyContinue |
             Where-Object { $_.LastWriteTime -gt (Get-Date).AddMinutes(-10) }
    Note "  [$H] new UE crash report dirs: $(@($crash).Count)"
    foreach ($c in @($crash)) { Note "  [$H] $($c.Name) $($c.LastWriteTime)" }
    $dumps = Get-ChildItem $dumpDir -Filter '*.dmp' -ErrorAction SilentlyContinue | Where-Object { $_.LastWriteTime -gt (Get-Date).AddMinutes(-10) }
    Note "  WER dumps for this run: $(@($dumps).Count)"
    foreach ($d in @($dumps)) { Note "  dump: $($d.FullName)" }

    # The enemy load line this scenario is about.
    $en = Get-Matches $H 'enemy sync: ([0-9]+) live enemies'
    Note "  [$H] $($en.Count) 'enemy sync: N live enemies' line(s); last: $(if ($en.Count) { $en[-1] } else { '(none)' })"
    Show-Since $H 'save layer re-initialised|player body: spawned|dialogue: blocked NPC dropped|enemy sync:|scan: local at' $mH2 12

    Stop-All
    Start-Sleep -Seconds 2
}

# ===========================================================================
# ITEMS 2-5 -- fresh save pair
# ===========================================================================
function Seed-FreshPair {
    Say 're-seeding both instances from the pristine pre-live save'
    foreach ($i in 1, 2) {
        $r = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'make_test_instance.ps1') `
                -Name ("inst{0}" -f $i) -Index $i -SourceSave $seed -Root $rigRoot -Suffix $inst[$i].Suffix -Force 2>&1
        $r | Where-Object { $_ -match 'FAIL|REFUSED|seeded|save dir|Instance ready' } | ForEach-Object { Note "  $_" }
        if ($LASTEXITCODE -ne 0) { Say "REFUSED: make_test_instance refused instance $i"; exit 2 }
    }
}

function Invoke-ItemsRun {
    Step 'ITEMS 2-5 -- fresh save pair: chest, loot, content, dialogue death' ''
    Stop-All
    Seed-FreshPair
    Start-Boot 'items'
    $H = $script:H; $P = $script:P
    Sample-Line $H 'boot'; Sample-Line $P 'boot'

    Step 'LOAD -- switches, oracle, containers, class, hooks'
    foreach ($i in 1, 2) {
        $m = Get-Matches $i 'switches:|NAMED-SAVE ORACLE|container offsets:|enemy sync: FS_EnemyCharacterBase|player body: class'
        $m | Select-Object -Last 5 | ForEach-Object {
            $s = [string]$_
            if ($s.Length -gt 300) { $s = $s.Substring(0, 300) + ' ...' }
            Note "  [$i] $s"
        }
    }

    # -----------------------------------------------------------------------
    # ITEM 5 -- dialogue ends on death (hub start map: an NPC and live enemies)
    # -----------------------------------------------------------------------
    Step 'ITEM 5 -- dialogue ends on death; the partner NPC lock is released'
    $m5P = Get-Mark $P; $m5H = Get-Mark $H
    Invoke-Go $P 'npc' 200
    Start-Sleep -Seconds 8
    Invoke-ActNear $P 'npc'
    Check 'item5' $P 'dialogue: local started' 30 'a conversation started on the peer' $m5P | Out-Null
    Check 'item5' $H 'dialogue: BLOCK ' 40 'the host blocked the partner NPC' $m5H | Out-Null
    # Take the god floor away, then call the player class' own Kill through the rig.
    # `qd.pdmg` was refused by the receiver in the previous run:
    #   damage dropped: attacker guid=... does not resolve on this machine
    # so the game's own death entry is the reliable origin here.
    Set-Control $P 'qd.god' $null
    Start-Sleep -Seconds 2
    $m5P2 = Get-Mark $P; $m5H2 = Get-Mark $H
    Set-Control $P 'qd.kill' '1'
    Check 'item5' $P 'rig: kill -> FS_PlayerChar_C.Kill ran=1' 25 'the rig called the player class own Kill' $m5P2 | Out-Null
    Check 'item5' $P 'dialogue: local death ended the conversation .* lock released' 40 'the conversation ended and the lock was released' $m5P2 | Out-Null
    Check 'item5' $H 'dialogue: unblock ' 40 'the host released its blocked NPC' $m5H2 | Out-Null
    Show-Since $P 'rig: kill|damage:|dialogue:' $m5P2 12
    Show-Since $H 'rig: kill|dialogue:' $m5H2 8
    Set-Control $P 'qd.kill' $null
    Set-Control $P 'qd.god' '400'

    # -----------------------------------------------------------------------
    # ITEM 2 -- a controlled value change in a world that has a closed chest
    # -----------------------------------------------------------------------
    Step 'ITEM 2 -- hard-load 900_SipehaWorld, then the peer opens a closed chest'
    Say '  the hub boot does not stream the merchant chest; 900_SipehaWorld is a full map and the fresh save has it closed'
    $m2loadH = Get-Mark $H; $m2loadP = Get-Mark $P
    Set-Control $H 'qd.hard' '900_SipehaWorld'
    [void](Wait-Pattern $H 'rig: hard lever -> 900_SipehaWorld' 90)
    Start-Sleep -Seconds 20
    Check 'item2' $H 'rig: hard lever -> 900_SipehaWorld .*ran=1' 5 'the host loaded 900_SipehaWorld' $m2loadH | Out-Null
    Check 'item2' $P 'travel: hard transition: loading 900_SipehaWorld|travel: the partner arrived in 900_SipehaWorld|travel: hard phase 2 map=900_SipehaWorld' 40 'the peer loaded the same world' $m2loadP | Out-Null
    [void](Wait-Pattern $P 'scan: (?:kind )?door ' 90)
    $door = ''
    foreach ($line in @(Get-RunLines $P)) {
        if ($line -match 'scan: kind door (\S+) guid=\S+ .*opened=0') { $door = $Matches[1]; break }
    }
    $chest = ''
    foreach ($line in @(Get-RunLines $P)) {
        if ($line -match 'scan: (?:kind )?chest (\S+) guid=\S+ .*opened=0') { $chest = $Matches[1]; break }
    }
    $target = if ($door) { $door } elseif ($chest) { $chest } else { '' }
    $isDoor = [bool]$door
    if (-not $target) { Say '  NO CLOSED DOOR/CHEST in the peer scan; item 2 cannot run' }
    else {
        Say "  peer=$P opens '$target' ($(if ($isDoor) { 'door' } else { 'chest' }); value=0 opened=0 in its own scan)"
        $m2P = Get-Mark $P; $m2H = Get-Mark $H
        Invoke-Go $P $target 120
        Start-Sleep -Seconds 8
        if ($isDoor) {
            # The chest/door's own Interact is gated by the interaction component and
            # changes nothing here.  The game's door origin is `FindDoorOpenTarget`
            # (sets the component target) then `ForceOpenDoor` -- the measured G2 path.
            Invoke-Cheat $P 'FindDoorOpenTarget'
            Start-Sleep -Seconds 2
            Invoke-Cheat $P 'ForceOpenDoor'
            Check 'item2' $P 'rig: cheat ForceOpenDoor .*ran=1' 20 'the peer called the game door open' $m2P | Out-Null
        } else {
            Invoke-ActNear $P $target
        }
        Check 'item2' $P 'actor-state local change' 30 'the peer container changed locally' $m2P | Out-Null
        Check 'item2' $P 'actor-state intent sent' 30 'the peer offers the change' $m2P | Out-Null
        Check 'item2' $H 'actor-state intent applied' 30 'the host applies the intent' $m2H | Out-Null
        Check 'item2' $H 'actor-state refresh ok .* opened=1' 60 'the host object visibly changes (opened=1)' $m2H | Out-Null
        Check 'item2' $P 'actor states applied=.* confirmed=1' 60 'the peer confirms the round trip' $m2P | Out-Null
        Check 'item2' $P 'actor-state refresh ok .* opened=1' 60 'the peer object visibly changes (opened=1)' $m2P | Out-Null
        Invoke-Go $H $target 120
        Start-Sleep -Seconds 8
        Invoke-ActNear $H $target
        Check 'item2' $H 'dev state: .* opened=1' 30 'the host probe reads the object OPEN' $m2H | Out-Null
        $again = @(Get-Since $P $m2P | Where-Object { $_ -match 'actor-state intent sent' })
        $okAgain = ($again.Count -le 1)
        $script:verdicts.Add([pscustomobject]@{ Step = 'item2'; Inst = $P; What = 'a second open is not a second intent'; Ok = $okAgain; Line = "peer intents=$($again.Count)" }) | Out-Null
        Note "  $(if ($okAgain) { 'PASS' } else { 'FAIL' }) peer intents for the chest: $($again.Count)"
        foreach ($i in 1, 2) {
            $dup = @(Get-RunLines $i | Where-Object { $_ -match 'flag CHANGE TotalChestsOpened : ([0-9]+) -> ([0-9]+)' } | Where-Object { $_ -match ': 0 -> 2|: 1 -> 3|: 2 -> 4' })
            $script:verdicts.Add([pscustomobject]@{ Step = 'item2'; Inst = $i; What = 'TotalChestsOpened is not double-counted'; Ok = ($dup.Count -eq 0); Line = ($dup | Select-Object -First 1) }) | Out-Null
            Note "  $(if ($dup.Count -eq 0) { 'PASS' } else { 'FAIL' }) [$i] TotalChestsOpened not bumped twice"
        }
        Show-Since $P 'dev interact|actor-state|actor states|flag ' $m2P 14
        Show-Since $H 'dev interact|dev state|actor-state|actor states|flag ' $m2H 14

        # The reverse: the HOST opens the next closed object the host's own scan names.
        $second = ''
        foreach ($line in @(Get-RunLines $H)) {
            if ($line -match 'scan: kind (?:chest|door) (\S+) guid=\S+ .*opened=0' -and $Matches[1] -ne $chest) { $second = $Matches[1]; break }
        }
        Step 'ITEM 2b -- the reverse: the host opens a second closed object; peer applies'
        if ($second) {
            Say "  host=$H opens '$second'"
            $m2bH = Get-Mark $H; $m2bP = Get-Mark $P
            Invoke-Go $H $second 120
            Start-Sleep -Seconds 8
            if ($second -match 'Door') {
                Invoke-Cheat $H 'FindDoorOpenTarget'
                Start-Sleep -Seconds 2
                Invoke-Cheat $H 'ForceOpenDoor'
            } else { Invoke-ActNear $H $second }
            Check 'item2b' $H 'actor-state host set ' 30 'the host container changes' $m2bH | Out-Null
            Check 'item2b' $H 'actor-state refresh ok .*opened=1' 30 'the host object reads OPEN' $m2bH | Out-Null
            Check 'item2b' $P 'actor states applied=[1-9]' 60 'the peer applies the host absolute body' $m2bP | Out-Null
            Check 'item2b' $P 'actor-state refresh ok .*opened=1' 60 'the peer object reads OPEN' $m2bP | Out-Null
            Show-Since $H 'dev interact|dev state|actor-state host set|actor states sent' $m2bH 10
            Show-Since $P 'actor states applied|actor-state refresh ok|dev state' $m2bP 10
        } else {
            Note '  -- item2b: no second closed chest/door in the host scan; the reverse uses the Sipeha fight trigger'
            $m2bH = Get-Mark $H; $m2bP = Get-Mark $P
            Invoke-Go $H 'SipehaFightStart' 200
            Start-Sleep -Seconds 8
            Invoke-ActNear $H 'SipehaFightStart'
            Check 'item2b' $H 'actor-state host set ' 30 'the host container changes' $m2bH | Out-Null
            Check 'item2b' $H 'actor-state refresh ok .* value=1' 30 'the host object reads value=1' $m2bH | Out-Null
            Check 'item2b' $P 'actor states applied=[1-9]' 60 'the peer applies the host absolute body' $m2bP | Out-Null
            Check 'item2b' $P 'actor-state refresh ok .* value=1' 60 'the peer object reads value=1' $m2bP | Out-Null
            Show-Since $H 'dev interact|dev state|actor-state host set|actor states sent' $m2bH 10
            Show-Since $P 'actor states applied|actor-state refresh ok|dev state' $m2bP 10
        }

        # Lane assignment: a chest with a GUID is ACTOR_STATES; the OBJECTS lane skips it.
        Step 'ITEM 2c -- which lane carries which class'
        foreach ($i in 1, 2) {
            $obj = @(Get-RunLines $i | Where-Object { $_ -match 'objects sent rev=.* objects=([0-9]+)' })
            $trk = @(Get-RunLines $i | Where-Object { $_ -match 'object sync: tracking ' })
            $last = if ($obj.Count) { [string]$obj[-1] } else { '(none)' }
            if ($last.Length -gt 160) { $last = $last.Substring(0, 160) + ' ...' }
            Note "  [$i] objects sent lines: $($obj.Count); last: $last"
            Note "  [$i] object sync tracking lines: $($trk.Count)"
            $trk | Select-Object -Last 3 | ForEach-Object {
                $s = [string]$_
                if ($s.Length -gt 180) { $s = $s.Substring(0, 180) + ' ...' }
                Note "  [$i] $s"
            }
        }
        $chestSav = @(Get-RunLines $H | Where-Object { $_ -match 'scan: chest .* savable=yes|scan: kind chest ' })
        Note "  [$H] chest scan lines with a GUID: $($chestSav.Count)"
        Note '  chests with GUIDs belong to ACTOR_STATES; the OBJECTS lane skips them by rule.'
    }

    # -----------------------------------------------------------------------
    # ITEM 3 -- loot hiding in a fresh map (both copies alive at the kill)
    # -----------------------------------------------------------------------
    Step 'ITEM 3 -- loot: both machines hold the drop, one takes it, the other hides it'
    $m3H = Get-Mark $H; $m3P = Get-Mark $P
    foreach ($i in 1, 2) { Set-Control $i 'qd.drops' '1' }
    Invoke-Go $H 'enemy' 400
    Start-Sleep -Seconds 10
    $hp = Get-LastPos $H
    if ($hp) {
        Set-Control $P 'qd.near' 200
        Set-Control $P 'qd.tp' ("{0},{1},{2},90" -f $hp[0], $hp[1], $hp[2])
        Say ("  peer moved onto the host's ground: {0:N0},{1:N0},{2:N0}" -f $hp[0], $hp[1], $hp[2])
    } else { Say '  -- host position unreadable; the peer stays where it is' }
    Start-Sleep -Seconds 8
    foreach ($i in 1, 2) { Set-Control $i 'qd.hit' '400' }
    Start-Sleep -Seconds 12
    foreach ($i in 1, 2) { Set-Control $i 'qd.hit' $null }
    Start-Sleep -Seconds 4
    Check 'item3' $H 'rig: hit enemy .* ran=1' 20 'the host lands a local kill' $m3H | Out-Null
    Check 'item3' $P 'rig: hit enemy .* ran=1' 20 'the peer lands a local kill' $m3P | Out-Null
    Check 'item3' $H 'loot: drop ' 30 'the host ledger learns a local drop' $m3H | Out-Null
    Check 'item3' $P 'loot: drop ' 30 'the peer ledger learns a local drop' $m3P | Out-Null
    Say "  peer=$P walks to the ledger drop and takes it"
    Invoke-Go $P 'loot' 40
    Start-Sleep -Seconds 6
    Invoke-ActNear $P 'loot'
    Check 'item3' $P 'loot: local pickup .* -- tombstoned for the partner' 45 'the pickup is tombstoned' $m3P | Out-Null
    Check 'item3' $H 'loot: hiding local drop .* -- the partner took it' 60 'the host hides its copy' $m3H | Out-Null
    Show-Since $P 'rig: hit|dev interact: resolved|loot:' $m3P 14
    Show-Since $H 'rig: hit|loot:' $m3H 14

    # -----------------------------------------------------------------------
    # ITEM 4 -- the Sipeha fight trigger, then the remaining hard maps
    # -----------------------------------------------------------------------
    Step 'ITEM 4 -- trigger the Sipeha fight (cutscene/boss), then scan the hard maps'
    $m4H = Get-Mark $H; $m4P = Get-Mark $P
    Invoke-Go $H 'SipehaFightStart' 200
    Start-Sleep -Seconds 8
    Invoke-ActNear $H 'SipehaFightStart'
    Start-Sleep -Seconds 15
    Check 'item4' $H 'boss: guid=.* is a boss|boss: ActivateBossHPBar called|cutscene: local started' 25 'the trigger started a boss/cutscene on the host' $m4H | Out-Null
    Check 'item4' $P 'boss: follower raised its own bar|cutscene: partner started|cutscene: playing the partner|cutscene: holding the local player' 30 'the peer reacted' $m4P | Out-Null
    Show-Since $H 'cutscene:|boss:|dev interact|scan: loose' $m4H 14
    Show-Since $P 'cutscene:|boss:' $m4P 10

    $maps = @('990_B33sment', '997_FinalZoneCutscene', '998_QueenCutscenes', '999_SipehaCutscenes')
    foreach ($map in $maps) {
        if (-not (Test-Alive $H) -or -not (Test-Alive $P)) { Note '  -- a machine died before the map pass finished'; break }
        Say "  host=$H hard-loads $map"
        $mmH = Get-Mark $H; $mmP = Get-Mark $P
        Set-Control $H 'qd.hard' $map
        Set-Control $P 'qd.hard' $map
        [void](Wait-Pattern $H ("rig: hard lever -> " + $map) 90)
        Start-Sleep -Seconds 25
        Check 'item4' $H ("rig: hard lever -> " + [regex]::Escape($map) + " .*ran=1") 5 "host hard-loaded $map" $mmH | Out-Null
        Check 'item4' $P ("rig: hard lever -> " + [regex]::Escape($map) + " .*ran=1") 5 "peer hard-loaded $map" $mmP | Out-Null
        Check 'item4' $P 'travel: hard transition: loading|travel: the partner arrived|travel: hard phase 2 map=' 40 "peer followed $map" $mmP | Out-Null
        $scan = @(Get-Since $H $mmH | Where-Object { $_ -match 'scan: loose|movers: tracking|boss:|cutscene:' })
        $scan | Select-Object -First 10 | ForEach-Object {
            $s = [string]$_
            if ($s.Length -gt 220) { $s = $s.Substring(0, 220) + ' ...' }
            Note "  [$H] $s"
        }
        Check 'item4' $H 'cutscene: local started' 10 "a cutscene fired in $map" $mmH | Out-Null
        Check 'item4' $H 'movers: tracking ' 10 "a mover is tracked in $map" $mmH | Out-Null
        Check 'item4' $H 'boss: guid=.* is a boss' 10 "a boss is present in $map" $mmH | Out-Null
    }

    Step 'SESSION SURVIVAL'
    foreach ($i in 1, 2) {
        $alive = Test-Alive $i
        $script:verdicts.Add([pscustomobject]@{ Step = 'survival'; Inst = $i; What = 'alive at the end'; Ok = $alive; Line = '' }) | Out-Null
        Note "  $(if ($alive) { 'PASS' } else { 'FAIL' }) [$i] process alive=$alive"
        Sample-Line $i 'end'
        $err = Get-Matches $i 'ERR |EXCEPTION|ACCESS_VIOLATION|fatal|\bfault'
        Note "  [$i] error/fault lines: $($err.Count)"
        $err | Select-Object -Last 6 | ForEach-Object {
            $s = [string]$_
            if ($s.Length -gt 200) { $s = $s.Substring(0, 200) + ' ...' }
            Note "  [$i] $s"
        }
    }
    Stop-All
}
# ===========================================================================
# main
# ===========================================================================
Say "session10 start: phase=$Phase"
Enable-WerLocalDumps
if ($Phase -eq 'r83' -or $Phase -eq 'all') {
    for ($r = 1; $r -le $Runs; $r++) { Invoke-R83Run $r }
}
if ($Phase -eq 'items' -or $Phase -eq 'all') {
    Invoke-ItemsRun
}

Note ''
Note '=== VERDICTS ==='
foreach ($v in $script:verdicts) {
    Note ("  {0,-8} inst{1}  {2,-6}  {3}" -f $v.Step, $v.Inst, $(if ($v.Ok) { 'PASS' } else { 'FAIL' }), $v.What)
}
$script:lines | Set-Content -LiteralPath $Report
Say "report: $Report"


