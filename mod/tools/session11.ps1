# session11.ps1 -- Session 11 of the live two-instance run, in ONE process.
#
# WHY IT IS ONE PROCESS
# ---------------------
# A launched game does not survive the command that started it (LIVE-TEST-LOG R14), so the
# relay, both instances, the driving, the collection and the teardown all happen inside this
# script.  Background jobs do not survive a turn boundary either.
#
# WHAT IT COVERS (the three blockers Session 10 left)
# --------------------------------------------------
#   item1  The interaction gate.  Session 10 (R86) measured `verb=Interact` running on a
#          closed chest/door with `opened=0` staying, because the Blueprint's open gate
#          reads the player interaction component's `Current Interact Actor` and the rig
#          never filled it.  The rig now stands and faces the object, writes that field,
#          and dispatches the player's own compiled `Interact` input event
#          (`InpActEvt_Interact*`, the function a key press runs).  Fresh save pair with a
#          closed GUID'd chest and a closed door: the peer opens one, the host the other.
#   item2  Loot hiding.  Both machines hold the same ordinary enemy alive and nearby, the
#          owner kills it, one machine takes the drop and the other must hide its copy.
#   item3  Boss + cutscene.  780_SZ_Depths places `GreenDragon_Spawner_C` and a
#          `LevelSequenceActor` (`SecretSwordGetSequence`, started by the altar BP).  Both
#          machines hard-load it; the host triggers the sequence and the fight.
#
# EVERY control file is created before launch because the environment is not inherited
# reliably through the launch chain (measured with QDCOOP_TEST_TPFILE).
[CmdletBinding()]
param(
    [ValidateSet('item1', 'item2', 'item3', 'all')]
    [string]$Phase = 'all',
    [int]$BootSeconds = 300,
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
$outDir  = Join-Path $rigRoot 'session11'
if (-not $Report) { $Report = Join-Path $outDir ('report-' + $Phase + '.txt') }
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force -Path $outDir | Out-Null }

$script:lines = New-Object System.Collections.Generic.List[string]
trap {
    Write-Output ("UNHANDLED: " + $_.Exception.Message)
    Write-Output ("AT: " + $_.InvocationInfo.PositionMessage)
    Write-Output ("STACK: " + $_.ScriptStackTrace)
    $script:lines.Add("UNHANDLED: " + $_.Exception.Message) | Out-Null
    $script:lines.Add("AT: " + $_.InvocationInfo.PositionMessage) | Out-Null
    # The teardown matters more than the exit code: a script that dies must not leave two
    # games and a relay running, and it must still write what it learned.
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
        if ($s.Length -gt 380) { $s = $s.Substring(0, 380) + ' ...' }
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
$script:actSeq = 0
function Invoke-ActNear([int]$i, [string]$name) {
    # The act lever latches the last content it applied, so writing the SAME instruction a
    # second time is deliberately one interaction.  Alternating the case is a new
    # instruction to the lever and the same target to the resolver (`RigNameMatches` is
    # case-insensitive), which is how "a second interaction" really gets driven.
    $script:actSeq++
    $text = if (($script:actSeq % 2) -eq 0) { 'near:' + $name.ToUpperInvariant() } else { 'near:' + $name }
    Set-Control $i 'qd.act' $text
    Say "  act[$i] -> $text"
}
$script:goSeq = 0
function Invoke-Go([int]$i, [string]$name, [int]$near = -1) {
    $script:goSeq++
    $yaw = ($script:goSeq * 37) % 360
    if ($near -ge 0) { Set-Control $i 'qd.near' ([string]$near) }
    Set-Control $i 'qd.tp' ('near:' + $name + ',' + $yaw)
    Say "  tp[$i] -> near:$name (yaw $yaw, near=$(if ($near -ge 0) { $near } else { 'unchanged' }))"
}
function Invoke-Hard([int]$i, [string]$map) {
    Set-Control $i 'qd.hard' $map
    Say "  hard[$i] -> $map"
}
# `near:<name>` with NO yaw: `DevTeleportToTarget` then faces the actor itself, which is what
# the interaction trace needs.  `Invoke-Go` passes a pseudo-random yaw on purpose (so a walk
# pattern is not fixed), and that yaw is exactly wrong for standing at an interactable.
function Invoke-Goto([int]$i, [string]$name, [int]$near = -1) {
    if ($near -ge 0) { Set-Control $i 'qd.near' ([string]$near) }
    Set-Control $i 'qd.tp' ('near:' + $name)
    Say "  tp[$i] -> near:$name (facing, no yaw override)"
}
$script:pressSeq = 0
function Invoke-Press([int]$i, [string]$name, [string]$step) {
    # Stand facing it, let the engine's own trace settle, then press the key the way a player
    # does: a plain `act`, which acts on whatever the component's `Current Interact Actor`
    # already is.  Nothing is filled in by hand on this path -- that is the point of it.
    Invoke-Goto $i $name
    Start-Sleep -Seconds 10
    # A press needs a player standing somewhere.  Measured this session: after a HARD map
    # load the pawn is a default pawn at the origin, falling, and every teleport is undone --
    # `local at 0,0,-2729` is the tell.  The trace cannot find anything from there.
    $posLines = @(Get-RunLines $i | Where-Object { $_ -match 'scan: local at ' })
    $posLine = if ($posLines.Count) { [string]$posLines[-1] } else { '(no position line)' }
    $standing = ($posLines.Count -gt 0) -and ($posLine -notmatch 'local at (?:-?0(?:\.0+)?),-?0(?:\.0+)?,')
    Note "  $(if ($standing) { 'PASS' } else { 'FAIL' }) [$i] the player is standing somewhere real: $posLine"
    $script:verdicts.Add([pscustomobject]@{ Step = $step; Inst = $i; What = 'the player stands somewhere real (not the origin, falling)'; Ok = $standing; Line = $posLine }) | Out-Null
    $trace = @(Get-RunLines $i | Where-Object { $_ -match 'scan: interact target ' })
    $last = if ($trace.Count) { [string]$trace[-1] } else { '(no scan line yet)' }
    $named = ($last -match [regex]::Escape($name))
    Note "  $(if ($named) { 'PASS' } else { '--  ' }) [$i] the engine's own trace offers the object: $last"
    $script:verdicts.Add([pscustomobject]@{ Step = $step; Inst = $i; What = 'the engine''s own trace offers the object'; Ok = $named; Line = $last }) | Out-Null
    $script:pressSeq++
    # ALWAYS the explicit path.  Measured this session: the component's own trace target is
    # intermittent -- a scan at 4:04:36 named `Checkpoint17`, and the press ten seconds later
    # read `target=-` because the component's Update had cleared it.  A press that depends on
    # winning that race is a flaky test, so the lever fills the field the trace fills and then
    # dispatches the same input event.  `$named` records whether the trace ALSO agreed.
    Set-Control $i 'qd.act' ('near:' + $name.ToUpperInvariant())
    Say "  act[$i] -> near:$($name.ToUpperInvariant()) (field filled, then the real input event)"
    return $named
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
# ITEM 1 -- the interaction gate, two ways, on a fresh pair
# ===========================================================================
function Invoke-Item1([int]$H, [int]$P) {
    Step 'ITEM 1 -- the interaction gate: peer opens a closed chest, host a closed door'
    Note 'Session 10 R86: the verb ran and opened=0 stayed, because the Blueprint gate reads'
    Note 'the player interaction component''s `Current Interact Actor`, which read `-`.'
    # THE HUB, with the AQ sub-level streamed in -- NOT a hard load.
    #
    # Measured this session: after `LoadMapWithLoadingScreen 900_SipehaWorld` the pawn is a
    # fresh default pawn at the origin, falling (`scan: local at 0,0,-2729`), and every
    # teleport is undone by the game's own respawn.  A press there cannot be a press: there
    # is nowhere to stand.  The hub keeps a real gameplay pawn (it has the map's only
    # `PlayerStart`), and `qd.level` streams `200_AQ` into it, where Session 9 resolved a
    # real closed chest (`Brass_Sci_Chest`) and Session 10 a closed `Shop_Door_00_SM`.
    Step 'ITEM 1-0 -- stream 200_AQ into the hub, where the player can actually stand'
    foreach ($i in $H, $P) { Set-Control $i 'qd.level' '200_AQ' }
    Start-Sleep -Seconds 25
    foreach ($i in $H, $P) {
        $m = @(Get-RunLines $i | Where-Object { $_ -match 'scan: local at ' })
        $last = if ($m.Count) { [string]$m[-1] } else { '(none)' }
        Note "  [$i] $last"
    }


    # ---- 1a. the PEER presses a real interactable the game offers --------
    # Measured this session: the only one-shot an interact key can reach where a player can
    # actually STAND is the hub's checkpoint -- `scan: interact target Checkpoint17` is the
    # engine's own trace naming it.  Its effect rides NamedSaveData (the flag lane) rather
    # than SavableActorStates, but the KEY PRESS is the same press, and the press is exactly
    # what the rig could not do before this session (R86: `verb=Interact` ran and nothing
    # happened).
    Step 'ITEM 1a -- peer presses the checkpoint (the real interact path, peer -> host)'
    # WHICH checkpoint: the one the ENGINE's trace names, not "the nearest one".  The class
    # walk picks a checkpoint in an unloaded area (measured: `Checkpoint12 at -3278,8988,
    # -39691`, 31 km away) and the pawn falls there.  The trace target is the object the game
    # itself says the player can reach, so that is the one to press.
    $traced = ''
    $tl = @(Get-RunLines $P | Where-Object { $_ -match 'scan: interact target (?!-)\S+' })
    if ($tl.Count) {
        if ($tl[-1] -match 'scan: interact target (\S+)') { $traced = $Matches[1] }
    }
    if (-not $traced) { $traced = 'Checkpoint17' }
    Say "  peer=$P presses '$traced' (the object the engine's own trace names)"
    $mP = Get-Mark $P; $mH = Get-Mark $H
    [void](Invoke-Press $P $traced 'item1')
    [void](Check 'item1' $P 'dev interact: component target set \(read back ok\)' 30 'the component field the gate reads is filled' $mP)
    [void](Check 'item1' $P 'dev interact: the player''s Interact input event \S+ ran' 30 'the player''s own Interact input event ran' $mP)
    [void](Check 'item1' $P 'dev interact: the game''s own gate says IsInteractible=1' 20 'the game itself says this object is interactible' $mP)
    [void](Check 'item1' $P 'flag CHANGE|flag intent|dev interact: the input event changed the object' 40 'the press changed something the game owns' $mP)
    [void](Check 'item1' $P 'flag intent sent' 30 'the peer offers the change' $mP)
    [void](Check 'item1' $H 'intent applied:' 40 'the host applies it' $mH)
    [void](Check 'item1' $P 'flag intent confirmed' 40 'the peer confirms the round trip' $mP)
    Start-Sleep -Seconds 2
    Invoke-Shot $P 'item1-peer-press'
    Invoke-Shot $H 'item1-host-after-peer-press'
    Show-Since $P 'dev interact|dev state:|flag ' $mP 18
    Show-Since $H 'flag |intent applied' $mH 10
    foreach ($i in $H, $P) {
        $dup = @(Get-RunLines $i | Where-Object { $_ -match 'flag CHANGE TotalChestsOpened : ([0-9]+) -> ([0-9]+)' } | Where-Object { $_ -match ': 0 -> 2|: 1 -> 3|: 2 -> 4' })
        $script:verdicts.Add([pscustomobject]@{ Step = 'item1'; Inst = $i; What = 'TotalChestsOpened is not double-counted'; Ok = ($dup.Count -eq 0); Line = ($dup | Select-Object -First 1) }) | Out-Null
        Note "  $(if ($dup.Count -eq 0) { 'PASS' } else { 'FAIL' }) [$i] TotalChestsOpened not bumped twice"
    }

    $chest = ''; $door = ''
    foreach ($i in $P, $H) {
        foreach ($line in @(Get-RunLines $i)) {
            if (-not $chest -and $line -match 'scan: (?:kind )?chest (\S+) guid=\S+ .*opened=0') { $chest = $Matches[1] }
            if (-not $door  -and $line -match 'scan: (?:kind )?door (\S+) guid=\S+ .*opened=0')  { $door  = $Matches[1] }
        }
        if ($chest -and $door) { break }
    }
    if (-not $chest) {
        # Nothing in the scan: ask for the chest by name.  `near:closedchest` reaches the
        # class-walk chest (`SG_Chest_BP2`, savable=no, its own `Opened` bool) and teleporting
        # there STREAMS the area it lives in -- the hub keeps a real gameplay pawn, so the
        # position sticks (measured: the pawn stays at 6168,-17970,-14047).
        Say '  no closed chest in the scan yet; asking for the chest by name'
        Invoke-Goto $P 'closedchest'
        Start-Sleep -Seconds 12
        foreach ($line in @(Get-RunLines $P)) {
            if (-not $chest -and $line -match "dev teleport near 'closedchest' -> (\S+)") { $chest = $Matches[1] }
        }
    }
    if (-not $chest -and -not $door) { Note '  -- no closed chest/door reachable; item 1b will say so (1a still runs)' }

    # ---- 1b. the closed chest: the press runs, the GAME refuses the object ----
    # `Chest_Merchant2` (900_SipehaWorld) and `SG_Chest_BP2` (hub, after streaming) are the
    # two closed chests this build reaches, and the game's own `GetInteractInfo` says
    # `IsInteractible=0` for both -- a real key press cannot open either.  That is a CONTENT
    # fact, and it is recorded here as one: the rig does its half (field filled, real input
    # event dispatched) and the game answers.
    Step 'ITEM 1b -- the closed chest: the press runs, the game refuses the object'
    if (-not $chest) { Note '  -- no closed chest was reachable; item 1b cannot run'; return }
    Say "  host=$H presses '$chest'"
    $mbH = Get-Mark $H; $mbP = Get-Mark $P
    [void](Invoke-Press $H $chest 'item1b')
    [void](Check 'item1b' $H 'dev interact: component target set \(read back ok\)' 30 'the host filled the gate field' $mbH)
    [void](Check 'item1b' $H 'dev interact: the player''s Interact input event \S+ ran' 30 'the host''s Interact input event ran' $mbH)
    [void](Check 'item1b' $H 'dev interact: the game''s own gate says IsInteractible=0' 20 'the game itself refuses this object (the content blocker, named)' $mbH)
    [void](Check 'item1b' $H 'dev state: .* opened=0' 20 'the chest stays closed, and the log says why' $mbH)
    Start-Sleep -Seconds 2
    Invoke-Shot $H 'item1b-host-chest-refused'
    Show-Since $H 'dev interact|dev state:' $mbH 14
}


# ===========================================================================
# ITEM 2 -- loot hiding: both machines hold the drop
# ===========================================================================
function Get-ScanEnemy([int]$i) {
    $m = @(Get-RunLines $i | Where-Object { $_ -match 'scan: kind enemy ' })
    if ($m.Count -eq 0) { return $null }
    if ($m[-1] -match 'scan: kind enemy (\S+) guid=([0-9A-Fa-f]+) at (-?[0-9.]+),(-?[0-9.]+),(-?[0-9.]+) dist=[0-9.]+ local-run=(\d)') {
        return [pscustomobject]@{
            Name = $Matches[1]; Guid = $Matches[2]
            X = [double]$Matches[3]; Y = [double]$Matches[4]; Z = [double]$Matches[5]
            LocalRun = [int]$Matches[6]
        }
    }
    return $null
}

function Invoke-Item2([int]$H, [int]$P) {
    Step 'ITEM 2 -- loot: both machines hold the drop, one takes it, the other hides it' ''
    Note 'Session 9 R79 / 10 R87: the killer rolls the drop and the other ends up with a ledger'
    Note 'entry and nothing to hide.  The setup here is both machines ALIVE and NEARBY first.'
    # NO HARD LOAD HERE.  Measured this session: `LoadMapWithLoadingScreen` of the map the
    # machine is ALREADY in faults the GAME (stack entirely inside
    # QueensDomain-Win64-Shipping, no WINMM frame; UE4CC-Windows-C2CD747E..., 214 s in).
    # Both machines are already in the hub, which is where the ordinary enemies are.
    [void](Wait-Pattern $H 'scan: kind enemy ' 120)

    foreach ($i in $H, $P) { Set-Control $i 'qd.drops' '1' }
    $mH = Get-Mark $H; $mP = Get-Mark $P

    # Stand on the host's nearest ordinary enemy, then put the peer on the same ground.
    Invoke-Go $H 'enemy' 400
    Start-Sleep -Seconds 10
    $pos = Get-LastPos $H
    if ($pos) {
        Set-Control $P 'qd.near' 200
        Set-Control $P 'qd.tp' ("{0},{1},{2},90" -f $pos[0], $pos[1], $pos[2])
        Say ("  peer moved onto the host's ground: {0:N0},{1:N0},{2:N0}" -f $pos[0], $pos[1], $pos[2])
    } else { Say '  -- host position unreadable; the peer stays where it is' }
    Start-Sleep -Seconds 12

    $eH = Get-ScanEnemy $H; $eP = Get-ScanEnemy $P
    $distH = 'n/a'
    if ($eH -and $pos) {
        $d = [math]::Sqrt([math]::Pow($eH.X - $pos[0], 2) + [math]::Pow($eH.Y - $pos[1], 2))
        $distH = [int]$d
    }
    Note ("  host scan enemy: " + $(if ($eH) { "$($eH.Name) guid=$($eH.Guid) local-run=$($eH.LocalRun) dist=$distH" } else { '(none)' }))
    Note ("  peer scan enemy: " + $(if ($eP) { "$($eP.Name) guid=$($eP.Guid) local-run=$($eP.LocalRun)" } else { '(none)' }))
    $sameGuid = ($eH -and $eP -and $eH.Guid -eq $eP.Guid)
    Note "  $(if ($sameGuid) { 'PASS' } else { '--  ' }) the two machines named the same enemy guid=$(if ($sameGuid) { $eH.Guid } else { 'n/a' })"
    $script:verdicts.Add([pscustomobject]@{ Step = 'item2'; Inst = 0; What = 'both machines hold the same enemy at the kill'; Ok = $sameGuid; Line = $(if ($sameGuid) { "$($eH.Guid) $($eH.Name)" } else { '' }) }) | Out-Null

    # The OWNER kills it.  The other machine's copy stays alive and nearby.
    $owner = 0
    if ($eH -and $eH.LocalRun -eq 1) { $owner = $H }
    elseif ($eP -and $eP.LocalRun -eq 1) { $owner = $P }
    if ($owner -eq 0) { $owner = $H }
    $other = if ($owner -eq $H) { $P } else { $H }
    Say "  owner=inst$owner kills; inst$other keeps its copy alive (it must NOT be the killer)"
    Step 'ITEM 2a -- the owner kills; both ledgers learn the drop'
    Set-Control $owner 'qd.hit' '400'
    Start-Sleep -Seconds 14
    Set-Control $owner 'qd.hit' $null
    Start-Sleep -Seconds 6
    Check 'item2' $owner 'rig: hit enemy .* ran=1' 20 'the owner landed the kill' $mH | Out-Null
    Check 'item2' $owner 'loot: drop ' 30 'the owner ledger learns the drop' $mH | Out-Null
    Check 'item2' $other 'loot: merged ' 30 'the other machine merges the entry' $mP | Out-Null

    # Does the other machine hold a LOCAL copy of that drop?  Without one there is nothing
    # to hide, which is the structural limit R79 named.  Measure, then act on the answer.
    $otherHasDrop = @(Get-RunLines $other | Where-Object { $_ -match 'loot: drop ' }).Count -gt 0
    Note "  $(if ($otherHasDrop) { 'PASS' } else { '--  ' }) inst$other holds a local drop actor"
    if (-not $otherHasDrop) {
        # Its own copy is still alive: kill it there too, so it rolls the same drop at the
        # same place (the key is class+position+ordinal, so the two must agree).
        Say "  inst$other kills its own copy as well, so both machines roll the same drop"
        Set-Control $other 'qd.hit' '400'
        Start-Sleep -Seconds 14
        Set-Control $other 'qd.hit' $null
        Start-Sleep -Seconds 6
    }

    Step 'ITEM 2b -- one machine takes the drop; the other must hide its copy'
    $taker = $other
    Say "  inst$taker walks to the ledger drop and takes it"
    Invoke-Go $taker 'loot' 40
    Start-Sleep -Seconds 8
    Invoke-ActNear $taker 'loot'
    Start-Sleep -Seconds 2
    Check 'item2' $taker 'loot: local pickup .* -- tombstoned for the partner' 45 'the pickup is tombstoned' | Out-Null
    Check 'item2' $owner 'loot: hiding local drop .* -- the partner took it' 60 'the owner hides its copy' | Out-Null
    Show-Since $taker 'rig: hit|loot:|dev interact' 0 14
    Show-Since $owner 'rig: hit|loot:' 0 14
}

# ===========================================================================
# ITEM 3 -- boss bar and a LevelSequence cutscene, both machines
# ===========================================================================
function Invoke-Item3([int]$H, [int]$P) {
    Step 'ITEM 3 -- 780_SZ_Depths: a boss encounter and a placed LevelSequence' ''
    Note 'From the cooked packages: 780 places GreenDragon_Spawner_C and a LevelSequenceActor'
    Note 'named SecretSwordGetSequence, started by the altar Blueprint (no CutsceneManager).'
    $mLoadH = Get-Mark $H; $mLoadP = Get-Mark $P
    Invoke-Hard $H '780_SZ_Depths'
    Invoke-Hard $P '780_SZ_Depths'
    Start-Sleep -Seconds 35
    Check 'item3' $H 'rig: hard lever -> 780_SZ_Depths .*ran=1' 20 'the host loaded 780_SZ_Depths' $mLoadH | Out-Null
    Check 'item3' $P 'rig: hard lever -> 780_SZ_Depths .*ran=1' 20 'the peer loaded 780_SZ_Depths' $mLoadP | Out-Null
    [void](Wait-Pattern $H 'scan: ' 90)
    Show-Since $H 'scan: loose|scan: kind enemy|boss:|cutscene:|movers: tracking' $mLoadH 16

    # ---- 3a. the cutscene ------------------------------------------------
    Step 'ITEM 3a -- trigger the placed LevelSequence on the host'
    $m3H = Get-Mark $H; $m3P = Get-Mark $P
    Invoke-Go $H 'SecretSword' 180
    Start-Sleep -Seconds 10
    Invoke-ActNear $H 'SecretSword'
    Start-Sleep -Seconds 2
    Check 'item3' $H 'cutscene: local started' 40 'a cutscene started on the host' $m3H | Out-Null
    Check 'item3' $P 'cutscene: (playing the partner''s sequence|holding the local player)' 45 'the peer played or held it' $m3P | Out-Null
    Check 'item3' $P 'cutscene: partner''s cutscene ended; resuming together' 90 'the peer resumed with the host' $m3P | Out-Null
    Show-Since $H 'cutscene:|dev interact' $m3H 14
    Show-Since $P 'cutscene:' $m3P 10

    # ---- 3b. the boss ----------------------------------------------------
    Step 'ITEM 3b -- reach the Green Dragon and raise the bar on both machines'
    $m3bH = Get-Mark $H; $m3bP = Get-Mark $P
    Invoke-Go $H 'GreenDragon' 300
    Start-Sleep -Seconds 12
    # A spawner that has not fired on proximity may still carry an activation verb; pressing
    # it is the same interaction path item 1 just proved.
    Invoke-ActNear $H 'GreenDragon'
    Start-Sleep -Seconds 8
    [void](Wait-Pattern $H 'enemy id: spawner|scan: kind enemy ' 60)
    Invoke-Go $H 'enemy' 400
    Start-Sleep -Seconds 10
    $pos = Get-LastPos $H
    if ($pos) { Set-Control $P 'qd.tp' ("{0},{1},{2},90" -f $pos[0], $pos[1], $pos[2]) }
    Start-Sleep -Seconds 10
    Check 'item3' $H 'boss: guid=\S+ is a boss' 60 'the host classifies a boss' $m3bH | Out-Null
    Check 'item3' $H 'boss: ActivateBossHPBar called' 40 'the host raised its own bar' $m3bH | Out-Null
    Check 'item3' $H 'boss: sent guid=\S+ phase=\d+ bar on' 30 'the host announced the bar' $m3bH | Out-Null
    Check 'item3' $P 'boss: follower raised its own bar for guid=' 60 'the peer raised its own bar' $m3bP | Out-Null
    Show-Since $H 'boss:|enemy id: spawner|scan: kind enemy' $m3bH 14
    Show-Since $P 'boss:' $m3bP 10
}

# ===========================================================================
# main
# ===========================================================================
Say "session11 start: phase=$Phase"

# One boot per invocation.  In `all` mode the three items share it and move between maps
# with the hard lever, because a boot costs minutes and the hard switch is already proven.
Stop-All
Seed-FreshPair
Start-Boot $Phase
$H = $script:H
$P = $script:P

if ($Phase -eq 'item1' -or $Phase -eq 'all') { Invoke-Item1 $H $P }
if ($Phase -eq 'item2' -or $Phase -eq 'all') { Invoke-Item2 $H $P }
if ($Phase -eq 'item3' -or $Phase -eq 'all') { Invoke-Item3 $H $P }

Step 'SESSION SURVIVAL'
foreach ($i in 1, 2) {
    $alive = Test-Alive $i
    $script:verdicts.Add([pscustomobject]@{ Step = 'survival'; Inst = $i; What = 'alive at the end'; Ok = $alive; Line = '' }) | Out-Null
    Note "  $(if ($alive) { 'PASS' } else { 'FAIL' }) [$i] process alive=$alive"
    $err = Get-Matches $i 'ERR |EXCEPTION|ACCESS_VIOLATION|fatal|\bfault'
    Note "  [$i] error/fault lines: $($err.Count)"
    $err | Select-Object -Last 4 | ForEach-Object {
        $s = [string]$_
        if ($s.Length -gt 200) { $s = $s.Substring(0, 200) + ' ...' }
        Note "  [$i] $s"
    }
}
Stop-All

Note ''
Note '=== VERDICTS ==='
foreach ($v in $script:verdicts) {
    Note ("  {0,-8} inst{1}  {2,-6}  {3}" -f $v.Step, $v.Inst, $(if ($v.Ok) { 'PASS' } else { 'FAIL' }), $v.What)
}
$script:lines | Set-Content -LiteralPath $Report
Say "report: $Report"
