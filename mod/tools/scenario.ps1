# scenario.ps1 -- one complete two-instance scenario, in ONE process.
#
# WHY IT IS ONE PROCESS
# ---------------------
# A launched game does not survive the command that started it: the harness tears down
# the process tree when the invocation ends (LIVE-TEST-LOG R14, measured).  So a
# background job or a second terminal is not an option -- relay, both instances, the
# driving, the log collection and the teardown all have to happen inside this script.
#
# WHAT IT DOES
# ------------
#   1. makes sure nothing from a previous run is alive
#   2. writes the switch file, the rig control files and a fresh log boundary
#   3. starts qd-relay, launches both instances off-screen
#   4. waits for a session, then for gameplay on both
#   5. drives the scenario named by -Phase
#   6. prints the evidence for every step and a verdict
#   7. stops everything it started
#
# It reads roles from `session established` and never assumes which instance is the
# host: the relay gives the host to whoever handshakes first.
[CmdletBinding()]
param(
    [ValidateSet('probe', 'full')]
    [string]$Phase = 'probe',
    # Seconds to allow for a session + gameplay on both instances.
    [int]$BootSeconds = 180,
    # Seconds per wait inside the scenario.
    [int]$StepSeconds = 90,
    [switch]$KeepRunning,
    [string]$Report = ''
)

$ErrorActionPreference = 'Stop'
$here    = $PSScriptRoot
$repo    = Split-Path -Parent (Split-Path -Parent $here)
$gameDir = 'G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64'
$exe     = Join-Path $gameDir 'QueensDomain-Win64-Shipping.exe'
$relay   = Join-Path (Split-Path -Parent $here) 'rust\target\release\qd-relay.exe'
$rigRoot = 'G:\Freespace\Queens_Domain\_test'
$outDir  = Join-Path $rigRoot 'session6'
if (-not $Report) { $Report = Join-Path $outDir 'report.txt' }
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force -Path $outDir | Out-Null }

$script:lines = New-Object System.Collections.Generic.List[string]
function Say($m) {
    $s = "[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $m
    Write-Output $s
    $script:lines.Add($s) | Out-Null
}
function Note($m) {
    Write-Output $m
    $script:lines.Add($m) | Out-Null
}

function Get-RigPaths([int]$i) {
    $root   = Join-Path $rigRoot ("inst{0}" -f $i)
    $suffix = "qd$i"
    [pscustomobject]@{
        Index  = $i
        Root   = $root
        Suffix = $suffix
        Log    = Join-Path $gameDir ("qdcoop-{0}.log" -f $suffix)
        Tp     = Join-Path $root 'qd.tp'
        Act    = Join-Path $root 'qd.act'
        Scan   = Join-Path $root 'qd.scan'
        God    = Join-Path $root 'qd.god'
        Hit    = Join-Path $root 'qd.hit'
        Pid    = "$root.pid"
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
    # `return ,$res` on purpose: returning a plain array unrolls a one-element result into
    # its string, and `$m[-1]` then indexes a CHARACTER.  Measured: both instances
    # reported role=PEER because the single `session established` line arrived as a char.
    $res = @(Get-RunLines $i | Where-Object { $_ -match $pattern })
    return ,$res
}

function Wait-Pattern([int]$i, [string]$pattern, [int]$seconds, [string]$label) {
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
        $m = Get-Matches $i 'scan: health|scan: local at|dev autoload: in game'
        if ($m.Count -gt 0) { return $true }
        Start-Sleep -Milliseconds 500
    }
    return $false
}

function Set-Control([int]$i, [string]$leaf, [string]$value) {
    $p = Join-Path $inst[$i].Root $leaf
    if ($null -eq $value) { Remove-Item -LiteralPath $p -ErrorAction SilentlyContinue }
    else { Set-Content -LiteralPath $p -Value $value -NoNewline }
}

function Invoke-Act([int]$i) {
    $p = $inst[$i].Act
    $n = 0
    if (Test-Path -LiteralPath $p) { $n = [int](Get-Content -LiteralPath $p -ErrorAction SilentlyContinue | Select-Object -First 1) }
    Set-Content -LiteralPath $p -Value ([string]($n + 1)) -NoNewline
    Say "  act[$i] -> $($n + 1)"
}

# Interact with a NAMED actor.  The interaction component only tracks which actor the
# engine's trace is pointing at; the verb is on the target, so the instruction has to name
# it (measured, Session 6: the component has 5 UFUNCTIONs and none of them is `Interact`).
function Invoke-ActNear([int]$i, [string]$name) {
    Set-Content -LiteralPath $inst[$i].Act -Value ('near:' + $name) -NoNewline
    Say "  act[$i] -> near:$name"
}

function Invoke-Go([int]$i, [string]$name) {
    Set-Content -LiteralPath $inst[$i].Tp -Value ('near:' + $name) -NoNewline
    Say "  tp[$i] -> near:$name"
}

function Show-Log([int]$i, [string]$pattern, [int]$n) {
    $m = Get-Matches $i $pattern
    if ($m.Count -eq 0) { Note "  [$i] (no line matches '$pattern')"; return }
    $m | Select-Object -Last $n | ForEach-Object { Note "  [$i] $_" }
}

# --- evidence windows and verdicts ---------------------------------------------------
# Defined here, not next to the steps, because the first mark is taken as soon as the
# instances reach gameplay.
$script:verdicts = New-Object System.Collections.Generic.List[object]

# A verdict has to be about what happened AFTER the step started.  Measured: matching the
# whole run made "peer raises a flag" PASS on the baseline `flag NEW` lines printed at
# load, before the peer had done anything.
function Get-Mark([int]$i) { return (Get-RunLines $i).Count }

# An instance that has died cannot answer for a lane, and waiting the full window for a
# process that is gone turns one crash into eight misleading timeouts.  Measured: the peer
# exited mid-run and every later step spent its whole timeout before reporting nothing.
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

function Step([string]$title, [string]$body) {
    Note ''
    Note "### $title"
    if ($body) { Note $body }
}

# ---------------------------------------------------------------------------
# 1. teardown of anything left over
# ---------------------------------------------------------------------------
Say "scenario start: phase=$Phase"
Get-Process -Name 'QueensDomain-Win64-Shipping', 'qd-relay' -ErrorAction SilentlyContinue |
    ForEach-Object {
        Say "  stopping leftover $($_.ProcessName) pid=$($_.Id)"
        Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue
    }
Start-Sleep -Seconds 2

# ---------------------------------------------------------------------------
# 2. switches, control files, log boundary
# ---------------------------------------------------------------------------
# Every write path on.  The rig proves lanes; a lane that is off proves nothing.
$ini = Join-Path $gameDir 'qdcoop.ini'
@(
    '[Local]'
    'Enabled=1'
    'LogLevel=2'
    ''
    '[Net]'
    'RelayHost=127.0.0.1'
    'RelayPort=47800'
    ''
    '[Rules]'
    'FlagSync=1'
    'InventorySync=1'
    'ActorStateSync=1'
    'EnemySync=1'
    'PuppetSync=1'
    'DialogueSync=1'
    'StreamUnion=1'
    'SuppressMenuPause=0'
    'HostAuthoritative=1'
    ''
    '[Tuning]'
    'PlayerHz=30'
    'EnemyRowsPerTick=20'
    'SpawnAttemptsMax=5'
) | Set-Content -LiteralPath $ini
Say "wrote $ini (all write paths on)"

foreach ($i in 1, 2) {
    # The teleport/act files are instructions, not switches: a leftover from a previous
    # run would be applied at the new process's first poll and move the player somewhere
    # nobody asked for.
    Set-Control $i 'qd.tp' $null
    Set-Control $i 'qd.act' $null
    Set-Control $i 'qd.scan' '1'
    Set-Control $i 'qd.god' '400'
    Set-Control $i 'qd.cheat' $null
    Set-Control $i 'qd.cheatarg' $null
    # The damage lever is armed only by the one step that wants it: armed from launch it
    # would damage enemies during every earlier step.
    Set-Control $i 'qd.hit' $null
    if (Test-Path -LiteralPath $inst[$i].Pid) { Remove-Item -LiteralPath $inst[$i].Pid -ErrorAction SilentlyContinue }
}

# Environment is the second lever, not the first: measured, it does not always survive
# the launch chain, which is why the control files above exist.
$env:QDCOOP_AUTOLOAD = '0'
$env:QDCOOP_TEST_NEAR = '140'

# ---------------------------------------------------------------------------
# 3. relay + both instances
# ---------------------------------------------------------------------------
if (-not (Test-Path -LiteralPath $relay)) { Say "REFUSED: relay not built at $relay"; exit 2 }
$relayLog = Join-Path $outDir 'relay.log'
Remove-Item -LiteralPath $relayLog -ErrorAction SilentlyContinue
$rp = Start-Process -FilePath $relay -ArgumentList @('--bind', '127.0.0.1:47800') `
        -RedirectStandardOutput $relayLog -RedirectStandardError (Join-Path $outDir 'relay.err') `
        -WindowStyle Hidden -PassThru
Say "relay pid=$($rp.Id) -> $relayLog"
Start-Sleep -Seconds 2

$script:pids = @{}
foreach ($i in 1, 2) {
    $launch = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'run-hidden.ps1') `
                -UserDir $inst[$i].Root -Suffix $inst[$i].Suffix 2>&1
    $gp = ($launch | Where-Object { $_ -match '^\d+$' } | Select-Object -Last 1)
    if (-not $gp) { Say "LAUNCH FAILED for instance $i"; $launch | ForEach-Object { Note $_ }; }
    else { $script:pids[$i] = [int]$gp; Set-Content -LiteralPath $inst[$i].Pid -Value $gp; Say "inst$i pid=$gp (off-screen)" }
    Start-Sleep -Seconds 3
}
if (-not $script:pids[1] -or -not $script:pids[2]) { Say 'ABORT: both instances are required'; }

# ---------------------------------------------------------------------------
# 4. session + gameplay
# ---------------------------------------------------------------------------
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
    $hostI = 1; $peerI = 2
} else { Say "host=inst$($hostI[0]) peer=inst$($peerI[0])" }

Say "waiting for gameplay on both"
foreach ($i in 1, 2) {
    $ok = Wait-Gameplay $i $BootSeconds
    Say "inst${i} gameplay=$ok"
}

# ---------------------------------------------------------------------------
# 5. the scenario
# ---------------------------------------------------------------------------
$H = [int]$hostI[0]
$P = [int]$peerI[0]
# A mark taken as soon as both instances are in gameplay.  The damage lane fires from the
# enemies standing at the spawn as soon as the world is live -- before any step runs -- so
# a per-step mark would miss the only attack this scenario may get.
$mBootH = Get-Mark $H
$mBootP = Get-Mark $P

Step 'LOAD (5.1)' 'substrate, oracle, container offsets, enemy class, streaming hook'
foreach ($i in 1, 2) {
    Show-Log $i 'switches:|NAMED-SAVE ORACLE|container offsets:|enemy sync: FS_EnemyCharacterBase|hooked ULevelStreaming::SetShouldBeLoaded|hooks installed' 12
}

Step 'RIG LEVERS' 'act diagnostic, god, and the safe target list'
foreach ($i in 1, 2) {
    Show-Log $i 'rig: tp control|rig: act control|rig: god control|god: ' 8
}
foreach ($i in 1, 2) {
    Note "  --- inst${i} safe target list ---"
    Show-Log $i 'scan: kind |scan: nearest enemy|scan: interact target|scan: health' 40
}

if ($Phase -eq 'probe') {
    Say 'probe phase: nothing further is driven'
} else {
# ---------------------------------------------------------------------------
# The driven steps.  Every move is `near:<name>` -- the pawn is placed beside a live
# actor the mod itself resolved, never at a coordinate copied out of a log (two of those
# killed the peer in Session 5).
# ---------------------------------------------------------------------------

# --- the act lever: prove an interaction fires ----------------------------------------
Step 'ACT LEVER -- the rig can perform an interaction' 'the diagnostic, a named target, and the target''s own verb'
$mA = Get-Mark $P
Say "  peer=inst$P interacts with the nearest checkpoint"
Invoke-ActNear $P 'checkpoint'
Start-Sleep -Seconds 5
Show-Log $P 'rig: act control|dev teleport near|dev interact' 4
Show-Log $P 'scan: interact target' 2
Check 'act' $P 'dev interact: resolved .checkpoint. -> ' 20 'the named target resolved' $mA | Out-Null
Check 'act' $P 'dev interact: target=\S' 10 'a real target was used' $mA | Out-Null
Check 'act' $P 'dev interact: .*verb=Interact' 10 'the target''s Interact verb ran' $mA | Out-Null

# --- 5.2  G1: the peer's progression reaches the host ---------------------------------
# Driven by the checkpoint interaction of the step above: unlocking a checkpoint raises
# `CheckpointUnlockCount`, which is a NamedSaveData key.  Measured in Session 6:
#   flag CHANGE CheckpointUnlockCount : 1 -> 34 / flag intent sent ... try 1/5 /
#   flag intent confirmed ... / host: intent applied: CheckpointUnlockCount=34
# The mark is therefore the ACT step's, not this step's.
Step '5.2 G1 -- the peer changes a flag' 'the peer''s rise becomes INTENT kind 1 and the host applies it'
Check 'G1' $P 'flag CHANGE|flag intent sent' 10 'the peer sees a rise and sends it' $mA | Out-Null
Check 'G1' $P 'flag intent confirmed' 20 'the peer intent is confirmed' $mA | Out-Null
Check 'G1' $H 'intent applied:' 10 'the host applies the intent' $mBootH | Out-Null
Show-Log $P 'flag CHANGE|flag intent' 6
Show-Log $H 'intent applied|intent refused' 6

# --- 5.3  enemies: kill, ownership, freeze -------------------------------------------
Step '5.3 enemies -- a kill with the peer nearby' 'Paused ok=1, an ownership hand-off, no "NO effect"'
$mEn = Get-Mark $P
Say "  peer=inst$P goes next to the enemy"
Invoke-Go $P 'enemy'
Start-Sleep -Seconds 8
Show-Log $P 'dev teleport near|scan: kind enemy' 4
Check 'enemies' $P 'Paused ok=1' 30 'peer freezes a copy (ownership hand-off)' $mEn | Out-Null
Check 'enemies' $H 'Paused ok=1' 5 'host freezes a copy' 0 | Out-Null
Say "  arming the damage lever on the peer (qd.hit, the module's own TryApplyDamage)"
Set-Control $P 'qd.hit' '400'
Check 'enemies' $P 'rig: hit enemy' 30 'the peer lands a hit' $mEn | Out-Null
Check 'enemies' $P 'ran=1' 15 'TryApplyDamage resolved (ran=1)' $mEn | Out-Null
Check 'enemies' $P 'enemy death recorded|enemy record sent' 20 'the death is recorded/forwarded' $mEn | Out-Null
Check 'enemies' $P 'enemy damage had NO effect' 4 'NO "enemy damage had NO effect"' $mEn -Absent | Out-Null
Set-Control $P 'qd.hit' $null
Show-Log $P 'rig: hit enemy|enemy death recorded|enemy record sent' 5

# --- 5.4  G4: a host-run enemy hurts the distant partner ------------------------------
Step '5.4 G4 -- host-run enemy attacks the distant peer' 'host damage sent, peer applies it to its own pawn'
$mG4p = $mBootP
$mG4h = $mBootH
Say "  peer=inst$P goes far away (another savable, well outside the attack range)"
Invoke-Go $P 'coin'
Start-Sleep -Seconds 8
Show-Log $P 'dev teleport near' 2
Say "  host=inst$H goes next to the enemy and provokes it, then waits to be attacked"
Invoke-Go $H 'enemy'
Start-Sleep -Seconds 6
Show-Log $H 'dev teleport near' 2
# A passive enemy never attacks on its own, and the lane needs an ATTACK.  A few points
# of damage is the provocation; the amount is small so the enemy survives to swing back.
Set-Control $H 'qd.hit' '5'
Check 'G4' $H 'damage sent guid=' 90 'host sends measured enemy damage' $mG4h | Out-Null
Check 'G4' $P 'damage from enemy .*applied' 10 'peer applies it to its own pawn' $mG4p | Out-Null
# 5.4 asks that this line NOT appear.  It is checked over the whole session, not the
# step, because the attack that fires first is the one at the spawn.
Check 'G4' $P 'player damage had NO effect' 5 'NO "player damage had NO effect"' $mG4p -Absent | Out-Null
Show-Log $H 'damage sent guid=' 4
Show-Log $P 'damage from enemy' 4
Set-Control $H 'qd.hit' $null

# --- 5.5  dialogue ------------------------------------------------------------------
Step '5.5 dialogue -- the partner cannot start the NPC this machine is talking to'
$mDl = Get-Mark $P
$mDlh = Get-Mark $H
Say "  peer=inst$P interacts with the nearest NPC"
Invoke-ActNear $P 'npc'
Start-Sleep -Seconds 6
Show-Log $P 'scan: class NPC|dev interact' 4
Show-Log $P 'dialogue: ' 6
Check 'dialogue' $P 'dialogue: local started' 20 'peer starts a conversation' $mDl | Out-Null
Check 'dialogue' $H 'dialogue: BLOCK' 20 'partner blocks that NPC (BlockInteract=1)' $mDlh | Out-Null

# --- 5.6  G2: actor state -----------------------------------------------------------
Step '5.6 G2 -- the peer changes a savable actor state' 'actor-state intent sent -> host applied -> peer applied=1'
$mG2p = Get-Mark $P
$mG2h = Get-Mark $H
Say "  peer=inst$P goes next to the door, then calls FindDoorOpenTarget + ForceOpenDoor"
Invoke-Go $P 'door'
Start-Sleep -Seconds 4
Set-Control $P 'qd.cheat' 'FindDoorOpenTarget'
Start-Sleep -Seconds 2
Set-Control $P 'qd.cheat' 'ForceOpenDoor'
Check 'G2' $P 'rig: cheat ForceOpenDoor' 20 'the peer calls ForceOpenDoor' $mG2p | Out-Null
Check 'G2' $P 'actor-state intent sent' 30 'peer sends the actor-state intent' $mG2p | Out-Null
Check 'G2' $H 'actor-state intent applied:' 20 'host applies it' $mG2h | Out-Null
Check 'G2' $P 'actor states .*confirmed=1' 30 'peer confirms the round trip' $mG2p | Out-Null
Show-Log $P 'rig: cheat|actor-state|actor states' 6
Show-Log $H 'actor-state intent' 4

# --- 5.7  G3: the peer's own pickup -------------------------------------------------
Step '5.7 G3 -- the peer gains an item' 'item intent sent -> host grant -> item intent confirmed'
$mG3p = Get-Mark $P
$mG3h = Get-Mark $H
Say "  peer=inst$P takes every key (GiveAllKeys: InventoryContents)"
Set-Control $P 'qd.cheat' 'GiveAllKeys'
Check 'G3' $P 'rig: cheat GiveAllKeys' 20 'the peer calls GiveAllKeys' $mG3p | Out-Null
Check 'G3' $P 'item intent sent' 30 'peer sends its own gain' $mG3p | Out-Null
Check 'G3' $H 'inventory grant applied:' 30 'host grants it' $mG3h | Out-Null
Check 'G3' $P 'item intent confirmed' 30 'peer sees it confirmed' $mG3p | Out-Null
Show-Log $P 'item intent' 8
Show-Log $H 'inventory grant' 6

# --- 5.8  streaming and travel ------------------------------------------------------
Step '5.8 streaming and travel' 'level names with no refusal, a travel, and the session survives'
foreach ($i in 1, 2) {
    $names = Get-Matches $i 'level name:'
    Note "  [$i] $($names.Count) 'level name:' line(s) in this run"
    $names | Select-Object -Last 4 | ForEach-Object { Note "  [$i] $_" }
}
Check 'stream' $H 'level union:' 3 'host level union is live' | Out-Null
Check 'stream' $P 'level union:' 3 'peer level union is live' | Out-Null
Check 'travel' $P 'travel: local destination|travel: followed' 5 'a travel was announced or followed' | Out-Null
Show-Log $P 'travel: ' 8
Show-Log $H 'travel: ' 8
# NOT a verdict: divergence only happens on a hard map switch, which this scenario does
# not perform. What matters is that the refusal, when it happens, is explicit.
Note '  (divergence/refusal lines, if any:)'
Show-Log $P 'WORLD DIVERGENCE|travel: cannot follow' 4

Step 'SESSION SURVIVAL' 'both instances still alive and still connected at the end'
foreach ($i in 1, 2) {
    $alive = [bool](Get-Process -Id $script:pids[$i] -ErrorAction SilentlyContinue)
    Note "  [$i] process alive=$alive"
    $conn = Get-Matches $i 'session established|relay transport up'
    Note "  [$i] last connection line: $(if ($conn.Count) { $conn[-1] } else { '(none)' })"
    $crash = Get-Matches $i 'scan: fault|god: fault|interact: fault|EXCEPTION'
    if ($crash.Count) { $crash | Select-Object -Last 3 | ForEach-Object { Note "  [$i] $_" } }
}

Note ''
Note '=== VERDICTS ==='
foreach ($v in $script:verdicts) {
    Note ("  {0,-10} inst{1}  {2,-6}  {3}" -f $v.Step, $v.Inst, $(if ($v.Ok) { 'PASS' } else { 'FAIL' }), $v.What)
}
}

# ---------------------------------------------------------------------------
# 6. teardown
# ---------------------------------------------------------------------------
if (-not $KeepRunning) {
    foreach ($i in 1, 2) {
        if ($script:pids.ContainsKey($i)) {
            Stop-Process -Id $script:pids[$i] -Force -ErrorAction SilentlyContinue
        }
    }
    Stop-Process -Id $rp.Id -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 1
    Say 'stopped both instances and the relay'
} else {
    Say "KEEPING RUNNING: inst1=$($script:pids[1]) inst2=$($script:pids[2]) relay=$($rp.Id)"
}

$script:lines | Set-Content -LiteralPath $Report
Say "report: $Report"
