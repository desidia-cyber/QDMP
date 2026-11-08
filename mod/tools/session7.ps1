# session7.ps1 -- Session 7 of the live two-instance run, in ONE process.
#
# WHY IT IS ONE PROCESS
# ---------------------
# A launched game does not survive the command that started it (LIVE-TEST-LOG R14), so
# the relay, both instances, the driving, the collection and the teardown all happen
# inside this script.  It reads the roles from `session established` and never assumes
# which instance is the host.
#
# PHASES
# ------
#   probe  -- all switches on, boot, 5 minutes of idle, collect what the world is.
#   full   -- the probe's switches, then the Session 7 steps 2..7.
#
# It is deliberately verbose: every verdict is a line that was actually printed, and
# every step marks the log before it starts so a boot-time line cannot pass a step.
[CmdletBinding()]
param(
    [ValidateSet('probe', 'switch', 'full')]
    [string]$Phase = 'probe',
    [int]$BootSeconds = 240,
    [int]$IdleSeconds = 300,
    [int]$StepSeconds = 60,
    [string]$Report = ''
)

$ErrorActionPreference = 'Stop'
$here    = $PSScriptRoot
$repo    = Split-Path -Parent (Split-Path -Parent $here)
$gameDir = 'G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64'
$exe     = Join-Path $gameDir 'QueensDomain-Win64-Shipping.exe'
$relay   = Join-Path (Split-Path -Parent $here) 'rust\target\release\qd-relay.exe'
$rigRoot = 'G:\Freespace\Queens_Domain\_test'
$outDir  = Join-Path $rigRoot 'session7'
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
        Cheat  = Join-Path $root 'qd.cheat'
        CheatArg = Join-Path $root 'qd.cheatarg'
        Travel = Join-Path $root 'qd.travel'
        Hard   = Join-Path $root 'qd.hard'
        Level  = Join-Path $root 'qd.level'
        Near   = Join-Path $root 'qd.near'
        Drops  = Join-Path $root 'qd.drops'
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
    $res = @(Get-RunLines $i | Where-Object { $_ -match $pattern })
    return ,$res
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
    $p = Join-Path $inst[$i].Root $leaf
    # UNTYPED `$value`, on purpose: a [string] parameter turns $null into "" and writes
    # an EMPTY file.  For qd.hit an empty file means "the default amount" in DevLever, so
    # a "clear" wrote an armed lever -- measured Session 7 as kills nobody asked for.
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

function Step([string]$title, [string]$body) {
    Note ''
    Note "### $title"
    if ($body) { Note $body }
}

function Show-Log([int]$i, [string]$pattern, [int]$n) {
    $m = Get-Matches $i $pattern
    if ($m.Count -eq 0) { Note "  [$i] (no line matches '$pattern')"; return }
    $m | Select-Object -Last $n | ForEach-Object { Note "  [$i] $_" }
}

function Show-Since([int]$i, [string]$pattern, [int]$mark, [int]$n) {
    $m = @(Get-Since $i $mark | Where-Object { $_ -match $pattern })
    if ($m.Count -eq 0) { Note "  [$i] (no line matches '$pattern' since the step started)"; return }
    $m | Select-Object -Last $n | ForEach-Object { Note "  [$i] $_" }
}

function Invoke-ActNear([int]$i, [string]$name) {
    Set-Control $i 'qd.act' ('near:' + $name)
    Say "  act[$i] -> near:$name"
}
# The tp/act levers apply an instruction only when its TEXT changes (a repeat would pin
# the pawn / re-run the verb every 200 ms).  A scenario that moves the same machine to
# the same kind twice must therefore vary the instruction: the yaw is the free field.
$script:goSeq = 0
function Invoke-Go([int]$i, [string]$name) {
    $script:goSeq++
    $yaw = ($script:goSeq * 37) % 360
    Set-Control $i 'qd.tp' ('near:' + $name + ',' + $yaw)
    Say "  tp[$i] -> near:$name (yaw $yaw)"
}

# ---------------------------------------------------------------------------
# 1. teardown of anything left over
# ---------------------------------------------------------------------------
Say "session7 start: phase=$Phase"
Get-Process -Name 'QueensDomain-Win64-Shipping', 'qd-relay' -ErrorAction SilentlyContinue |
    ForEach-Object {
        Say "  stopping leftover $($_.ProcessName) pid=$($_.Id)"
        Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue
    }
Start-Sleep -Seconds 2

# ---------------------------------------------------------------------------
# 2. switches, control files, log boundary
# ---------------------------------------------------------------------------
# EVERY write path on, including the new ones.  A switch that is off proves nothing.
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
    'PlayerBody=1'
    'DialogueSync=1'
    'StreamUnion=1'
    'TravelSync=1'
    'CutsceneSync=1'
    'MoverSync=1'
    'LootSync=1'
    'SuspendGuard=1'
    'SuppressMenuPause=0'
    'HostAuthoritative=1'
    ''
    '[Tuning]'
    'PlayerHz=30'
    'EnemyRowsPerTick=20'
    'SpawnAttemptsMax=5'
) | Set-Content -LiteralPath $ini
Say "wrote $ini (every write path on, PlayerBody on)"

foreach ($i in 1, 2) {
    Set-Control $i 'qd.tp' $null
    Set-Control $i 'qd.act' $null
    Set-Control $i 'qd.hit' $null
    Set-Control $i 'qd.cheat' $null
    Set-Control $i 'qd.cheatarg' $null
    Set-Control $i 'qd.travel' $null
    Set-Control $i 'qd.hard' $null
    Set-Control $i 'qd.level' $null
    Set-Control $i 'qd.near' $null
    Set-Control $i 'qd.drops' $null
    Set-Control $i 'qd.scan' '1'
    Set-Control $i 'qd.god' '400'
    if (Test-Path -LiteralPath $inst[$i].Pid) { Remove-Item -LiteralPath $inst[$i].Pid -ErrorAction SilentlyContinue }
    # A fresh log boundary: the mod appends, so each run has its own file.
    $old = $inst[$i].Log
    if (Test-Path -LiteralPath $old) {
        Move-Item -LiteralPath $old -Destination ($old + '.prev') -Force
    }
}

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
    $H = 1; $P = 2
} else { $H = [int]$hostI[0]; $P = [int]$peerI[0]; Say "host=inst$H peer=inst$P" }

Say "waiting for gameplay on both (up to $BootSeconds s)"
foreach ($i in 1, 2) {
    $ok = Wait-Gameplay $i $BootSeconds
    Say "inst${i} gameplay=$ok"
}

# ---------------------------------------------------------------------------
# 5. the load evidence
# ---------------------------------------------------------------------------
Step 'LOAD (5.1) -- switches, oracle, containers, class, hooks'
foreach ($i in 1, 2) {
    Show-Log $i 'switches:|NAMED-SAVE ORACLE|container offsets:|enemy sync: FS_EnemyCharacterBase|hooked ULevelStreaming|hooks installed|LoadMapWithLoadingScreen' 16
}
Step 'B7 -- the remote body as the player class (5.1 1-2)'
foreach ($i in 1, 2) {
    Show-Log $i 'player body:' 10
}
Step 'WORLD -- the map and the scan blocks'
foreach ($i in 1, 2) {
    $names = Get-Matches $i 'level name:'
    Note "  [$i] $($names.Count) 'level name:' line(s)"
    $names | Select-Object -Last 3 | ForEach-Object { Note "  [$i] $_" }
    $blocks = @()
    $rows = @(Get-RunLines $i | Where-Object { $_ -match 'scan: ' })
    if ($rows.Count) {
        $start = 0
        for ($k = $rows.Count - 1; $k -ge 0; $k--) { if ($rows[$k] -match 'scan: local at') { $start = $k; break } }
        $blocks = $rows[$start..($rows.Count - 1)]
    }
    $blocks | ForEach-Object { Note "  [$i] $_" }
}

# ---------------------------------------------------------------------------
# 6. idle: the load must survive five minutes on both
# ---------------------------------------------------------------------------
if ($IdleSeconds -gt 0) {
    Step "IDLE ${IdleSeconds}s -- both instances must stay alive"
    $aliveAt = @{}
    $deadline = (Get-Date).AddSeconds($IdleSeconds)
    $reportEvery = 30
    $next = (Get-Date).AddSeconds($reportEvery)
    while ((Get-Date) -lt $deadline) {
        foreach ($i in 1, 2) {
            if (-not $aliveAt.ContainsKey($i)) {
                if (Test-Alive $i) { $aliveAt[$i] = $true }
                else { $aliveAt[$i] = $false; Say "  inst$i DIED during idle" }
            }
        }
        if ((Get-Date) -ge $next) {
            $next = (Get-Date).AddSeconds($reportEvery)
            Say ("  idle: inst1 alive={0} inst2 alive={1} remaining={2}s" -f (Test-Alive 1), (Test-Alive 2), [int]($deadline - (Get-Date)).TotalSeconds)
        }
        Start-Sleep -Seconds 2
    }
    foreach ($i in 1, 2) {
        $ok = [bool](Get-Process -Id $script:pids[$i] -ErrorAction SilentlyContinue)
        $script:verdicts.Add([pscustomobject]@{ Step = 'idle'; Inst = $i; What = 'alive after idle'; Ok = $ok; Line = '' }) | Out-Null
        Note "  $(if ($ok) { 'PASS' } else { 'FAIL' }) [$i] alive after ${IdleSeconds}s idle"
    }
    # Errors and crash evidence, over the whole run.
    foreach ($i in 1, 2) {
        $err = Get-Matches $i 'ERR |EXCEPTION|ACCESS_VIOLATION|fatal|fault'
        Note "  [$i] error/fault lines: $($err.Count)"
        $err | Select-Object -Last 6 | ForEach-Object { Note "  [$i] $_" }
    }
}

$script:lines | Set-Content -LiteralPath $Report

# ---------------------------------------------------------------------------
# 6b. the driven steps (Session 7, items 2..7)
# ---------------------------------------------------------------------------
if ($Phase -ne 'probe') {

function Wait-GameplayAgain([int]$i, [int]$seconds) {
    $deadline = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $deadline) {
        if (-not (Test-Alive $i)) { return $false }
        $m = @(Get-RunLines $i | Where-Object { $_ -match 'scan: local at' })
        if ($m.Count -gt 0) { return $true }
        Start-Sleep -Milliseconds 500
    }
    return $false
}

function Set-Near([int]$i, [int]$cm) { Set-Control $i 'qd.near' ([string]$cm) }

# --- HARD MAP SWITCH: a real load, and back ----------------------------------------
# Only Ruins_Level_00 carries a PlayerStart; the area maps are sublevels, and loading one
# standalone lands in a DefaultPawn.  So the HARD handshake is proven by a round trip to
# a real world (990_B33sment) and back to the hub, and the area content is reached by
# STREAMING the sublevel into the running hub (the engine's own mechanism).
Step '5 (G6 hard) -- a real hard map switch to 990_B33sment, and back to the hub' 'HARD_BEGIN -> ARRIVED -> RESUME, twice'
$mHardH = Get-Mark $H
$mHardP = Get-Mark $P
Say "  host=inst$H takes LoadMapWithLoadingScreen(990_B33sment)"
Set-Control $H 'qd.hard' '990_B33sment'
Check 'travel' $H 'rig: hard lever -> 990_B33sment .*ran=1' 20 'the host calls the game''s hard-load entry' $mHardH | Out-Null
Check 'travel' $H 'travel: HARD transition to 990_B33sment' 60 'the host announces HARD_BEGIN' $mHardH | Out-Null
Check 'travel' $P 'travel: hard transition: loading 990_B33sment through GameInstance.LoadMapWithLoadingScreen' 60 'the peer loads the same map through the game' $mHardP | Out-Null
Check 'travel' $P 'signature LoadMapWithLoadingScreen' 20 'the one-time signature line' $mHardP | Out-Null
Check 'travel' $H 'travel: the partner arrived in 990_B33sment' 90 'the partner reports ARRIVED' $mHardH | Out-Null
Check 'travel' $H 'travel: both machines arrived in 990_B33sment; host releasing the pair to resume' 45 'the host RESUME' $mHardH | Out-Null
Check 'travel' $P 'travel: host released the pair; resuming in 990_B33sment' 30 'the peer is released in the new world' $mHardP | Out-Null
Show-Since $H 'rig: hard lever|travel: HARD|hard phase|both machines arrived|the partner arrived' $mHardH 10
Show-Since $P 'rig: hard|travel: hard|LoadMapWithLoadingScreen|arrived|released' $mHardP 10
foreach ($i in 1, 2) { Note "  [$i] alive after the hard switch: $(Test-Alive $i)" }

# Back to the hub, which has the PlayerStart the area maps do not.
Step '5 (G6 hard) -- back to Ruins_Level_00' 'the same handshake in the other direction'
$mBackH = Get-Mark $H
$mBackP = Get-Mark $P
Say "  host=inst$H takes LoadMapWithLoadingScreen(Ruins_Level_00)"
Set-Control $H 'qd.hard' 'Ruins_Level_00'
Check 'travel' $H 'rig: hard lever -> Ruins_Level_00 .*ran=1' 20 'the return call' $mBackH | Out-Null
Check 'travel' $H 'travel: HARD transition to Ruins_Level_00' 60 'the host announces the return' $mBackH | Out-Null
Check 'travel' $P 'travel: hard transition: loading Ruins_Level_00 through GameInstance.LoadMapWithLoadingScreen' 60 'the peer follows the return' $mBackP | Out-Null
Check 'travel' $H 'travel: both machines arrived in Ruins_Level_00; host releasing the pair to resume' 90 'the second RESUME' $mBackH | Out-Null
foreach ($i in 1, 2) { Say "  inst$i gameplay back in the hub: $(Wait-GameplayAgain $i 90)" }
Show-Since $H 'travel: HARD|both machines arrived' $mBackH 6

# --- reach the area content by streaming the sublevel ------------------------------
Step 'WORLD -- stream 200_AQ into the hub (chests, a teleporter, doors, enemies)'
$mLevH = Get-Mark $H
$mLevP = Get-Mark $P
foreach ($i in 1, 2) { Set-Control $i 'qd.level' '200_AQ' }
foreach ($i in 1, 2) {
    Check 'world' $i 'rig: level lever -> 200_AQ forced' 30 'the sublevel is forced loaded+visible' $(if ($i -eq $H) { $mLevH } else { $mLevP }) | Out-Null
}
Start-Sleep -Seconds 45
foreach ($i in 1, 2) {
    Show-Log $i 'scan: kind (chest|door|enemy|pickup|coin)|scan: class (NPC|Checkpoint)|scan: local at' 14
}
foreach ($i in 1, 2) { Note "  [$i] alive after the sublevel stream: $(Test-Alive $i)" }

# --- B7: the target lines, and the DAMAGE lane on the partner body -----------------
if ($Phase -eq 'full') {
Step '5.1 B7 -- TargetPlayer both ways, and a game hit on the partner body' 'peer nearer, then local nearer; host owns throughout (margin < 800 cm)'
$mB7P = Get-Mark $P
$mB7H = Get-Mark $H
Say "  peer=inst$P stands 300 cm from the nearest enemy"
Set-Near $P 300
Invoke-Go $P 'enemy'
Start-Sleep -Seconds 6
Say "  host=inst$H stands 800 cm from the same enemy (nearer than peer's body is not true, so the host keeps ownership)"
Set-Near $H 800
Invoke-Go $H 'enemy'
Start-Sleep -Seconds 4
Check 'b7' $H 'enemy .*: TargetPlayer local player -> the remote body \(read back\)' 40 'host re-targets the proxy (read back)' $mB7H | Out-Null
Show-Since $H 'dev teleport near|TargetPlayer|player body hit' $mB7H 8
# The enemy is chasing the proxy; the game's own hit is what the DAMAGE lane carries.
Check 'damage' $H 'player body hit guid=.* -> sent to the machine that owns the player' 90 'the game''s own hit is forwarded' $mB7H | Out-Null
Check 'damage' $P 'damage from enemy .*(applied|NO effect)' 20 'the owner applies the described hit' $mB7P | Out-Null
Check 'damage' $P 'damage from enemy .* applied hp' 20 'the hit moves the pawn''s HP (G4 effect half)' $mB7P | Out-Null
Show-Since $H 'player body hit|damage sent' $mB7H 6
Show-Since $P 'damage from enemy' $mB7P 6
# Back the other way: local nearer by > 200 cm, still inside the 800 cm ownership margin.
Say "  swap: peer 2000 cm, host 140 cm -> TargetPlayer back to the local player"
Set-Near $P 2000
Invoke-Go $P 'enemy'
Set-Near $H 140
Invoke-Go $H 'enemy'
Check 'b7' $H 'enemy .*: TargetPlayer remote body -> the local player \(read back\)' 30 'host re-targets its own player (read back)' $mB7H | Out-Null
Show-Since $H 'TargetPlayer' $mB7H 6
foreach ($i in 1, 2) { $alive = Test-Alive $i; Note "  [$i] alive after the target step: $alive" }

# --- item 3: a kill must produce a death record -----------------------------------
Step '5.3 enemies -- a kill produces a DEATH RECORD (the Session 6 gap)' 'the peer owns the enemy, kills it, and the host applies the record'
$mKillP = Get-Mark $P
$mKillH = Get-Mark $H
Say "  peer=inst$P takes ownership (peer 140 cm, host 2000 cm)"
Set-Near $H 2000
Invoke-Go $H 'enemy'
Set-Near $P 140
Invoke-Go $P 'enemy'
Start-Sleep -Seconds 8
Say "  arming the module's own hit on the peer (qd.hit=400)"
Set-Control $P 'qd.hit' '400'
Check 'enemies' $P 'rig: hit enemy guid=.* hp .* -> ' 40 'the peer lands a damaging hit' $mKillP | Out-Null
Check 'enemies' $P 'enemy death detected' 40 'the peer detects the death (HP/bDead/gone)' $mKillP | Out-Null
Check 'enemies' $P 'enemy death recorded' 20 'the death is recorded through StoreEnemySaveState' $mKillP | Out-Null
Check 'enemies' $P 'enemy record sent to host' 20 'the peer forwards the persistence record' $mKillP | Out-Null
Check 'enemies' $H 'enemy record applied guid=|intent applied' 30 'the host applies the record' $mKillH | Out-Null
Show-Since $P 'rig: hit enemy|enemy death|enemy record' $mKillP 10
Show-Since $H 'enemy death|enemy record|intent applied' $mKillH 6

# --- item 6: an enemy drop, taken on one machine, disappears on the other ---------
Step '5.12 loot (LootSync) -- a drop taken on one machine is hidden on the other' 'the drop is identified by the game''s own {source, index}'
$mLootP = Get-Mark $P
$mLootH = Get-Mark $H
foreach ($i in 1, 2) { Set-Control $i 'qd.drops' '1' }
Say "  letting the hit lever kill a few enemies; drops appear at their death spot"
Start-Sleep -Seconds 20
Set-Control $P 'qd.hit' $null
Set-Control $H 'qd.hit' $null
Show-Since $P 'rig: drop probe' $mLootP 10
Show-Since $H 'rig: drop probe' $mLootH 10
Check 'loot' $P 'loot: drop ' 30 'the killer''s ledger learns a drop' $mLootP | Out-Null
Check 'loot' $H 'loot: merged ' 30 'the host merges the drop set' $mLootH | Out-Null
Say "  peer=inst$P walks to the nearest drop and picks it up"
Set-Near $P 40
Invoke-Go $P 'drop'
Start-Sleep -Seconds 5
Invoke-ActNear $P 'drop'
Check 'loot' $P 'loot: local pickup .* -- tombstoned for the partner' 40 'the pickup is tombstoned' $mLootP | Out-Null
Check 'loot' $H 'loot: hiding local drop .* -- the partner took it' 40 'the other machine hides the drop' $mLootH | Out-Null
Show-Since $P 'dev interact: resolved .drop.|loot: ' $mLootP 8
Show-Since $H 'loot: ' $mLootH 8

# --- item 4: G2 on a real chest --------------------------------------------------
Step '5.2 G2 -- a real Chest_C opens, the partner refreshes it, and it cannot be reopened' 'actor-state intent -> applied -> refresh ok opened=1 on both'
$mG2P = Get-Mark $P
$mG2H = Get-Mark $H
Say "  peer=inst$P stands beside the nearest chest and calls its own verb"
Set-Near $P 140
Invoke-Go $P 'chest'
Start-Sleep -Seconds 5
Invoke-ActNear $P 'chest'
Check 'g2' $P 'dev interact: resolved .chest. -> ' 20 'the chest resolved' $mG2P | Out-Null
Check 'g2' $P 'dev interact: target=.*Chest.*verb=' 15 'the chest''s own Interact ran' $mG2P | Out-Null
Check 'g2' $P 'actor-state local change' 20 'the chest state changed locally' $mG2P | Out-Null
Check 'g2' $P 'actor-state intent sent' 20 'the peer sends the state' $mG2P | Out-Null
Check 'g2' $H 'actor-state intent applied:' 20 'the host applies it' $mG2H | Out-Null
Check 'g2' $H 'actor-state refresh ok .*opened=1' 45 'the host refreshes the in-level actor' $mG2H | Out-Null
Check 'g2' $P 'actor states .*confirmed=1' 45 'the peer confirms the round trip' $mG2P | Out-Null
Check 'g2' $P 'actor-state refresh ok .*opened=1' 45 'the peer refreshes its own actor' $mG2P | Out-Null
Check 'g2' $P 'actor-state refresh INCOMPLETE|actor-state refresh failed' 5 'NO INCOMPLETE/failed refresh' $mG2P -Absent | Out-Null
Show-Since $P 'dev interact:|actor-state|actor states' $mG2P 12
Show-Since $H 'actor-state refresh|actor-state intent' $mG2H 8
$mG2bP = Get-Mark $P
$mG2bH = Get-Mark $H
Say "  a SECOND interaction must not be a second open -- no new intent, container stays 1"
# Same actor, different text: the act lever re-runs only on a content change, and the
# name match is case-insensitive, so this is a real second call on the same chest.
Invoke-ActNear $P 'Chest'
Start-Sleep -Seconds 8
$againP = @(Get-Since $P $mG2bP | Where-Object { $_ -match 'actor-state intent sent' })
$againH = @(Get-Since $H $mG2bH | Where-Object { $_ -match 'actor-state intent applied' })
$ok2 = ($againP.Count -eq 0 -and $againH.Count -eq 0)
$script:verdicts.Add([pscustomobject]@{ Step = 'g2'; Inst = $P; What = 'a second interact is not a second open'; Ok = $ok2; Line = '' }) | Out-Null
Note "  $(if ($ok2) { 'PASS' } else { '--  ' }) [$P] second interact: intents sent=$($againP.Count) applied=$($againH.Count)"
Show-Since $P 'dev interact:|actor-state' $mG2bP 6

# --- item 5b: an in-world teleport, followed by the partner -----------------------
Step '5.8/5.9 travel follow -- a real Teleporter_C trip, followed' 'travel: local destination -> travel: followed'
$mTravH = Get-Mark $H
$mTravP = Get-Mark $P
Say "  host=inst$H drives FS_PlayerChar.TeleportToLocation through Teleporter_Aqr"
Set-Control $H 'qd.travel' 'near:teleport'
Check 'travel' $H 'rig: travel lever -> Teleporter_Aqr/' 25 'the host calls the game''s teleport entry' $mTravH | Out-Null
Check 'travel' $H 'travel: local destination Teleporter_Aqr/' 25 'the host announces the destination' $mTravH | Out-Null
Check 'travel' $P 'travel: followed Teleporter_Aqr/' 45 'the peer follows through its own TeleportToLocation' $mTravP | Out-Null
Show-Since $H 'rig: travel|travel: ' $mTravH 10
Show-Since $P 'travel: ' $mTravP 8
foreach ($i in 1, 2) { $alive = Test-Alive $i; Note "  [$i] alive after the teleport: $alive" }

# --- survival ----------------------------------------------------------------------
Step 'SESSION SURVIVAL' 'both instances still alive and still connected at the end'
foreach ($i in 1, 2) {
    $alive = Test-Alive $i
    $script:verdicts.Add([pscustomobject]@{ Step = 'survival'; Inst = $i; What = 'alive at the end'; Ok = $alive; Line = '' }) | Out-Null
    Note "  $(if ($alive) { 'PASS' } else { 'FAIL' }) [$i] process alive=$alive"
    # A crash is not a missing line.
    $err = Get-Matches $i 'ERR |EXCEPTION|ACCESS_VIOLATION|fatal|fault'
    Note "  [$i] error/fault lines: $($err.Count)"
    $err | Select-Object -Last 6 | ForEach-Object { Note "  [$i] $_" }
    $body = Get-Matches $i 'player damage had NO effect|player body: the hit payload|player body: spawn of the player class failed|player body: the spawn took over'
    Note "  [$i] B7/damage failure lines: $($body.Count)"
    $body | Select-Object -Last 4 | ForEach-Object { Note "  [$i] $_" }
}
}
}

# ---------------------------------------------------------------------------
# 7. teardown
# ---------------------------------------------------------------------------
foreach ($i in 1, 2) {
    if ($script:pids.ContainsKey($i)) { Stop-Process -Id $script:pids[$i] -Force -ErrorAction SilentlyContinue }
}
Stop-Process -Id $rp.Id -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1
Say 'stopped both instances and the relay'

Note ''
Note '=== VERDICTS ==='
foreach ($v in $script:verdicts) {
    Note ("  {0,-10} inst{1}  {2,-6}  {3}" -f $v.Step, $v.Inst, $(if ($v.Ok) { 'PASS' } else { 'FAIL' }), $v.What)
}
$script:lines | Set-Content -LiteralPath $Report
Say "report: $Report"

