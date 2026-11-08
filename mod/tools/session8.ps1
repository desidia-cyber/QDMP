# session8.ps1 -- Session 8 of the live two-instance run, in ONE process.
#
# WHY IT IS ONE PROCESS
# ---------------------
# A launched game does not survive the command that started it (LIVE-TEST-LOG R14), so the
# relay, both instances, the driving, the collection and the teardown all happen inside
# this script.  It reads the roles from `session established` and never assumes which
# instance is the host.
#
# WHAT IT COVERS
# --------------
#   1. all switches on, load, 3 minutes idle, with the enemy-parity merge
#   2. enemy parity: identity sources, shared keys, montage read-back, bandwidth
#   3. G2 on a chest that is still CLOSED (the R60 pick was already open)
#   4. in-world teleport follow (the resolver keeps the actor RigFindNear found)
#   5. a drop's derived identity, taken on one machine, hidden on the other
#   6. menu time dilation: a local menu must not freeze the shared world
#   7. an enemy attack provoked on the partner body
#
# EVERY control file is created before launch because the environment is not inherited
# reliably through the launch chain (measured with QDCOOP_TEST_TPFILE).
[CmdletBinding()]
param(
    [ValidateSet('probe', 'full')]
    [string]$Phase = 'full',
    [int]$BootSeconds = 240,
    [int]$IdleSeconds = 180,
    [int]$AttackSeconds = 75,
    [int]$SuppressMenuPause = 1,
    [string]$Report = ''
)

$ErrorActionPreference = 'Stop'
$here    = $PSScriptRoot
$repo    = Split-Path -Parent (Split-Path -Parent $here)
$gameDir = 'G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64'
$relay   = Join-Path (Split-Path -Parent $here) 'rust\target\release\qd-relay.exe'
$rigRoot = 'G:\Freespace\Queens_Domain\_test'
$outDir  = Join-Path $rigRoot 'session8'
if (-not $Report) { $Report = Join-Path $outDir 'report.txt' }
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force -Path $outDir | Out-Null }

$script:lines = New-Object System.Collections.Generic.List[string]
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
        Menu   = Join-Path $root 'qd.menu'
        EFunc  = Join-Path $root 'qd.efunc'
        EAtk   = Join-Path $root 'qd.eattack'
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
    $p = Join-Path $inst[$i].Root $leaf
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
function Get-Unpaused([int]$i) {
    $m = Get-Matches $i 'clock:'
    if ($m.Count -eq 0) { return -1.0 }
    if ($m[-1] -match 'unpaused=(-?[0-9.]+)') { return [double]$Matches[1] }
    return -1.0
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
$script:goSeq = 0
function Invoke-Go([int]$i, [string]$name, [int]$near = -1) {
    $script:goSeq++
    $yaw = ($script:goSeq * 37) % 360
    if ($near -ge 0) { Set-Control $i 'qd.near' ([string]$near) }
    Set-Control $i 'qd.tp' ('near:' + $name + ',' + $yaw)
    Say "  tp[$i] -> near:$name (yaw $yaw, near=$(if ($near -ge 0) { $near } else { 'unchanged' }))"
}

# ===========================================================================
# 1. teardown, switches, install
# ===========================================================================
Say "session8 start: phase=$Phase"
Get-Process -Name 'QueensDomain-Win64-Shipping', 'qd-relay' -ErrorAction SilentlyContinue |
    ForEach-Object {
        Say "  stopping leftover $($_.ProcessName) pid=$($_.Id)"
        Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue
    }
Start-Sleep -Seconds 2

$built = Join-Path $repo 'mod\native\build\winmm.dll'
if (-not (Test-Path -LiteralPath $built)) { Say "REFUSED: built proxy not found at $built"; exit 2 }
Copy-Item -LiteralPath $built -Destination (Join-Path $gameDir 'winmm.dll') -Force
Say "installed $built -> $gameDir\winmm.dll"

$ini = Join-Path $gameDir 'qdcoop.ini'
@(
    '[Local]'
    'Enabled=1'
    'LogLevel=3'
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
    "SuppressMenuPause=$SuppressMenuPause"
    'HostAuthoritative=1'
    ''
    '[Tuning]'
    'PlayerHz=30'
    'EnemyRowsPerTick=20'
    'SpawnAttemptsMax=5'
) | Set-Content -LiteralPath $ini
Say "wrote $ini (every write path on, PlayerBody on, SuppressMenuPause on, LogLevel=3)"

foreach ($i in 1, 2) {
    foreach ($leaf in 'qd.tp','qd.act','qd.hit','qd.cheat','qd.cheatarg','qd.travel','qd.hard',
                      'qd.level','qd.near','qd.drops','qd.menu','qd.efunc','qd.eattack') {
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
$relayLog = Join-Path $outDir 'relay.log'
Remove-Item -LiteralPath $relayLog -ErrorAction SilentlyContinue
$rp = Start-Process -FilePath $relay -ArgumentList @('--bind', '127.0.0.1:47800') `
        -RedirectStandardOutput $relayLog -RedirectStandardError (Join-Path $outDir 'relay.err') `
        -WindowStyle Hidden -PassThru
Say "relay pid=$($rp.Id) -> $relayLog"
Start-Sleep -Seconds 2

$script:pids = @{}
foreach ($i in 1, 2) {
    $ueLog = Join-Path $outDir ("inst{0}-ue.log" -f $i)
    Remove-Item -LiteralPath $ueLog -ErrorAction SilentlyContinue
    $launch = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'run-hidden.ps1') `
                -UserDir $inst[$i].Root -Suffix $inst[$i].Suffix -AbsLog $ueLog 2>&1
    $gp = ($launch | Where-Object { $_ -match '^\d+$' } | Select-Object -Last 1)
    if (-not $gp) { Say "LAUNCH FAILED for instance $i"; $launch | ForEach-Object { Note $_ }; }
    else { $script:pids[$i] = [int]$gp; Set-Content -LiteralPath $inst[$i].Pid -Value $gp; Say "inst$i pid=$gp (off-screen)" }
    Start-Sleep -Seconds 3
}

# ===========================================================================
# 4. session + gameplay + roles
# ===========================================================================
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

# ===========================================================================
# 5. the load evidence (item 1)
# ===========================================================================
Step 'LOAD -- switches, oracle, containers, class, hooks, dilation hook'
foreach ($i in 1, 2) {
    Show-Log $i 'switches:|NAMED-SAVE ORACLE|container offsets:|enemy sync: FS_EnemyCharacterBase|hooked ULevelStreaming|hooked UGameplayStatics::SetGlobalTimeDilation|hooks installed|LoadMapWithLoadingScreen' 18
    Check 'load' $i 'switches: .*pause=1' 5 'SuppressMenuPause is on' 0 | Out-Null
    Check 'load' $i 'hooked UGameplayStatics::SetGlobalTimeDilation' 5 'the dilation setter is hooked' 0 | Out-Null
}

# ===========================================================================
# 6. item 1: idle
# ===========================================================================
Step "ITEM 1 -- IDLE ${IdleSeconds}s: both instances and the parity code must survive"
if ($IdleSeconds -gt 0) {
    $deadline = (Get-Date).AddSeconds($IdleSeconds)
    $next = (Get-Date).AddSeconds(30)
    while ((Get-Date) -lt $deadline) {
        if ((Get-Date) -ge $next) {
            $next = (Get-Date).AddSeconds(30)
            Say ("  idle: inst1 alive={0} inst2 alive={1} remaining={2}s" -f (Test-Alive 1), (Test-Alive 2), [int]($deadline - (Get-Date)).TotalSeconds)
        }
        Start-Sleep -Seconds 2
    }
}
foreach ($i in 1, 2) {
    $ok = [bool](Get-Process -Id $script:pids[$i] -ErrorAction SilentlyContinue)
    $script:verdicts.Add([pscustomobject]@{ Step = 'item1'; Inst = $i; What = "alive after ${IdleSeconds}s idle"; Ok = $ok; Line = '' }) | Out-Null
    Note "  $(if ($ok) { 'PASS' } else { 'FAIL' }) [$i] alive after ${IdleSeconds}s idle"
    $err = Get-Matches $i 'ERR |EXCEPTION|ACCESS_VIOLATION|fatal|fault'
    Note "  [$i] error/fault lines: $($err.Count)"
    $err | Select-Object -Last 6 | ForEach-Object { Note "  [$i] $_" }
}

# ===========================================================================
# 7. item 2: enemy parity
# ===========================================================================
Step 'ITEM 2 -- enemy parity: identity sources, shared keys, montage read-back, bandwidth'
$mPar = @{}
foreach ($i in 1, 2) {
    $mPar[$i] = Get-Mark $i
    # Bring each player next to an enemy so the identity/montage lanes have work.
    Invoke-Go $i 'enemy' 400
}
Start-Sleep -Seconds 25
foreach ($i in 1, 2) {
    Show-Since $i 'enemy id: (spawner|placed|summon|adopted|no summoner)' $mPar[$i] 12
    Note "  [$i] identity lines:"
    foreach ($kind in 'spawner','placed','summon','adopted','no summoner') {
        $c = @(Get-Since $i $mPar[$i] | Where-Object { $_ -match ("enemy id: " + $kind) }).Count
        Note "    $kind = $c"
    }
    Show-Since $i 'enemy anim: Montage_Play' $mPar[$i] 6
    Show-Since $i 'enemy detail: candidates=' $mPar[$i] 4
}
# The shared-key check: the same 32-bit lane of the key on both machines for the same
# source.  The full key is 128 bits but the INFO line names the first lane, which is
# enough to catch a per-machine mint.
$keys = @{}
foreach ($i in 1, 2) {
    $keys[$i] = New-Object System.Collections.Generic.HashSet[string]
    foreach ($line in (Get-Since $i $mPar[$i] | Where-Object { $_ -match 'enemy id: (spawner|placed|summon)' })) {
        if ($line -match 'enemy id: spawner ([0-9a-f]{8})') { [void]$keys[$i].Add("spawner:$($Matches[1])") }
        elseif ($line -match 'enemy id: placed .* key=([0-9a-f]{8})') { [void]$keys[$i].Add("placed:$($Matches[1])") }
        elseif ($line -match 'enemy id: summon .* key=([0-9a-f]{8})') { [void]$keys[$i].Add("summon:$($Matches[1])") }
    }
}
$common = @($keys[1] | Where-Object { $keys[2].Contains($_) })
Note "  shared enemy keys: inst1=$($keys[1].Count) inst2=$($keys[2].Count) common=$($common.Count)"
$common | Select-Object -First 10 | ForEach-Object { Note "    same key on both: $_" }
$script:verdicts.Add([pscustomobject]@{ Step = 'item2'; Inst = 0; What = 'at least one identical enemy key on both machines'; Ok = ($common.Count -gt 0); Line = ($common | Select-Object -First 1) }) | Out-Null
Note "  $(if ($common.Count -gt 0) { 'PASS' } else { 'FAIL' }) identical key on both: $($common.Count)"
foreach ($i in 1, 2) {
    Check 'item2' $i 'enemy detail: candidates=' 20 'the bandwidth line is present' $mPar[$i] | Out-Null
}

# ===========================================================================
# 8. item 7: provoke an enemy attack on the partner body
# ===========================================================================
Step 'ITEM 7 -- an enemy attack lands on the partner body (B7 + DAMAGE)'
$mAtk = @{}
foreach ($i in 1, 2) { $mAtk[$i] = Get-Mark $i }
# Both players stand close to the same enemy; the owner steers TargetPlayer to the other
# body, and the game's own swing is what the lane carries.
Invoke-Go $H 'enemy' 500
Invoke-Go $P 'enemy' 300
Start-Sleep -Seconds 5
foreach ($i in 1, 2) {
    Set-Control $i 'qd.efunc' '1'
    Set-Control $i 'qd.eattack' '1'
}
$deadline = (Get-Date).AddSeconds($AttackSeconds)
$hit = $false
while ((Get-Date) -lt $deadline) {
    foreach ($i in 1, 2) {
        if (@(Get-Since $i $mAtk[$i] | Where-Object { $_ -match 'player body hit .* -> sent' }).Count -gt 0) { $hit = $true }
    }
    if ($hit) { break }
    Start-Sleep -Seconds 2
}
foreach ($i in 1, 2) {
    Show-Since $i 'dev teleport near|TargetPlayer|player body hit|damage sent|damage from enemy|rig: efunc|rig: eattack|attack' $mAtk[$i] 14
}
foreach ($i in 1, 2) {
    Check 'item7' $i 'player body hit guid=.* -> sent to the machine that owns the player' 15 'the game hit is forwarded to the owner' $mAtk[$i] | Out-Null
}
foreach ($i in 1, 2) {
    Check 'item7' $i 'damage from enemy .* applied hp' 15 'the owner applies the hit (G4 effect)' $mAtk[$i] | Out-Null
}
foreach ($i in 1, 2) { Set-Control $i 'qd.eattack' $null; Set-Control $i 'qd.efunc' $null }

# ===========================================================================
# 9. item 3: G2 on a chest that is still closed
# ===========================================================================
if ($Phase -eq 'full') {
Step 'ITEM 3 -- G2: a CLOSED chest opens, the partner refreshes it, it cannot be reopened'
$mG2P = Get-Mark $P
$mG2H = Get-Mark $H
foreach ($i in 1, 2) { Show-Log $i 'scan: chest |scan: kind chest' 10 }
Say "  peer=inst$P stands beside a savable chest whose container value is still 0"
Set-Control $P 'qd.near' '140'
Invoke-Go $P 'closedchest'
Start-Sleep -Seconds 6
Invoke-ActNear $P 'closedchest'
Check 'item3' $P 'dev interact: resolved .closedchest. -> ' 20 'a closed chest resolved' $mG2P | Out-Null
Check 'item3' $P 'dev state: .* savable=yes' 10 'the pick is a SavableActor with a container entry' $mG2P | Out-Null
Check 'item3' $P 'dev interact: target=.*Chest.*verb=' 15 'the chest own verb ran' $mG2P | Out-Null
Check 'item3' $P 'actor-state local change' 20 'the container changed locally' $mG2P | Out-Null
Check 'item3' $P 'actor-state intent sent' 20 'the peer sends the state' $mG2P | Out-Null
Check 'item3' $H 'actor-state intent applied:' 20 'the host applies it' $mG2H | Out-Null
Check 'item3' $H 'actor-state refresh ok .*opened=1' 45 'the host refreshes its own actor' $mG2H | Out-Null
Check 'item3' $P 'actor states .*confirmed=1' 45 'the peer confirms the round trip' $mG2P | Out-Null
Check 'item3' $P 'actor-state refresh ok .*opened=1' 45 'the peer refreshes its own actor' $mG2P | Out-Null
Check 'item3' $P 'actor-state refresh INCOMPLETE|actor-state refresh failed' 5 'NO INCOMPLETE/failed refresh' $mG2P -Absent | Out-Null
Show-Since $P 'dev interact:|dev state:|actor-state|actor states' $mG2P 16
Show-Since $H 'actor-state refresh|actor-state intent' $mG2H 8
# Second interaction with the same actor: a different spelling so the act lever re-fires.
$mG2bP = Get-Mark $P
$mG2bH = Get-Mark $H
Say "  a SECOND interaction must not be a second open"
Invoke-ActNear $P 'ClosedChest'
Start-Sleep -Seconds 8
$againP = @(Get-Since $P $mG2bP | Where-Object { $_ -match 'actor-state intent sent' })
$againH = @(Get-Since $H $mG2bH | Where-Object { $_ -match 'actor-state intent applied' })
$ok2 = ($againP.Count -eq 0 -and $againH.Count -eq 0)
$script:verdicts.Add([pscustomobject]@{ Step = 'item3'; Inst = $P; What = 'a second interact is not a second open'; Ok = $ok2; Line = '' }) | Out-Null
Note "  $(if ($ok2) { 'PASS' } else { '--  ' }) [$P] second interact: intents sent=$($againP.Count) applied=$($againH.Count)"
Show-Since $P 'dev interact:|actor-state' $mG2bP 8

# ===========================================================================
# 10. item 4: in-world teleport follow
# ===========================================================================
Step 'ITEM 4 -- in-world teleport follow (the resolver keeps the actor it found)'
$mTravH = Get-Mark $H
$mTravP = Get-Mark $P
Say "  host=inst$H drives TeleportToLocation through the nearest teleporter"
Set-Control $H 'qd.travel' 'near:teleport'
Check 'item4' $H 'rig: travel lever -> .*TeleportToLocation ran=1' 25 'the host calls the game teleport entry' $mTravH | Out-Null
Check 'item4' $H 'travel: local destination ' 25 'the host announces the destination' $mTravH | Out-Null
Check 'item4' $P 'travel: followed ' 45 'the peer follows through its own TeleportToLocation' $mTravP | Out-Null
Show-Since $H 'rig: travel|travel: ' $mTravH 10
Show-Since $P 'travel: ' $mTravP 8
foreach ($i in 1, 2) { Note "  [$i] alive after the teleport: $(Test-Alive $i)" }

# ===========================================================================
# 11. item 5: loot identity
# ===========================================================================
Step 'ITEM 5 -- a drop child identity: taken on one machine, hidden on the other'
$mLootP = Get-Mark $P
$mLootH = Get-Mark $H
foreach ($i in 1, 2) { Set-Control $i 'qd.drops' '1' }
Say "  peer=inst$P kills the nearest locally-run enemy so the game rolls a drop"
Set-Control $P 'qd.near' '300'
Invoke-Go $P 'enemy'
Start-Sleep -Seconds 6
Set-Control $P 'qd.hit' '400'
Start-Sleep -Seconds 25
Set-Control $P 'qd.hit' $null
Show-Since $P 'rig: drop probe|loot: ' $mLootP 14
Show-Since $H 'rig: drop probe|loot: ' $mLootH 14
Check 'item5' $P 'loot: drop ' 30 'the killer ledger learns a drop' $mLootP | Out-Null
Check 'item5' $H 'loot: merged ' 30 'the host merges the drop set' $mLootH | Out-Null
Say "  peer=inst$P walks to the nearest drop and picks it up"
Invoke-Go $P 'drop' 40
Start-Sleep -Seconds 5
Invoke-ActNear $P 'drop'
Check 'item5' $P 'loot: local pickup .* -- tombstoned for the partner' 40 'the pickup is tombstoned' $mLootP | Out-Null
Check 'item5' $H 'loot: hiding local drop .* -- the partner took it' 40 'the other machine hides the drop' $mLootH | Out-Null
Show-Since $P 'dev interact: resolved .drop.|loot: ' $mLootP 10
Show-Since $H 'loot: ' $mLootH 10

# ===========================================================================
# 12. item 6: menu time dilation
# ===========================================================================
Step 'ITEM 6 -- a menu on one machine must not freeze the shared world'
foreach ($i in 1, 2) { Check 'item6' $i 'clock: dilation=' 15 'the world clock is observable' 0 | Out-Null }
$mMenuH = Get-Mark $H
$mMenuP = Get-Mark $P
$h0 = Get-Unpaused $H
$p0 = Get-Unpaused $P
Say "  host=inst$H opens the game's own menu-pause path (SetPauseTimeDilation true)"
Set-Control $H 'qd.menu' '1'
Start-Sleep -Seconds 14
$h1 = Get-Unpaused $H
$p1 = Get-Unpaused $P
foreach ($i in 1, 2) {
    Show-Since $i 'rig: menu lever|BLOCK global time dilation|restored global|clock:|BLOCK SetGamePaused' $mMenuH 12
}
Note "  host unpaused clock: $h0 -> $h1 ; peer: $p0 -> $p1"
$worldMoved = ($h1 -gt $h0) -and ($p1 -gt $p0)
$script:verdicts.Add([pscustomobject]@{ Step = 'item6'; Inst = 0; What = 'the host world clock keeps advancing with its menu open'; Ok = ($h1 -gt $h0); Line = "unpaused $h0 -> $h1" }) | Out-Null
$script:verdicts.Add([pscustomobject]@{ Step = 'item6'; Inst = 0; What = 'the peer world clock keeps advancing'; Ok = ($p1 -gt $p0); Line = "unpaused $p0 -> $p1" }) | Out-Null
Note "  $(if ($h1 -gt $h0) { 'PASS' } else { 'FAIL' }) host clock advanced; $(if ($p1 -gt $p0) { 'PASS' } else { 'FAIL' }) peer clock advanced"
# A direct SetGlobalTimeDilation below 0.5, exactly what a GameMenu_C does, with a
# pretend-menu flag so the suppression is exercised without a widget.
$mDilH = Get-Mark $H
Set-Control $H 'qd.menu' 'dilate'
Start-Sleep -Seconds 10
Check 'item6' $H 'rig: menu lever -> pretend menu open' 10 'the dilation lever fired' $mDilH | Out-Null
Check 'item6' $H 'BLOCK global time dilation|restored global time dilation' 12 'the low dilation is blocked or restored' $mDilH | Out-Null
$h2 = Get-Unpaused $H
$p2 = Get-Unpaused $P
Note "  after a 0.20 dilation request: host unpaused=$h2 peer=$p2"
$script:verdicts.Add([pscustomobject]@{ Step = 'item6'; Inst = 0; What = 'dilation below 0.5 did not stop the world'; Ok = ($h2 -gt $h1); Line = "unpaused $h1 -> $h2" }) | Out-Null
Note "  $(if ($h2 -gt $h1) { 'PASS' } else { 'FAIL' }) world advanced under a blocked dilation"
Show-Since $H 'rig: menu lever|BLOCK global|restored global|clock:' $mDilH 12
Set-Control $H 'qd.menu' '0'
# The peer side too: the same path on the other machine.
$mMenuP2 = Get-Mark $P
$p3 = Get-Unpaused $P
Set-Control $P 'qd.menu' '1'
Start-Sleep -Seconds 12
$p4 = Get-Unpaused $P
$h3 = Get-Unpaused $H
$script:verdicts.Add([pscustomobject]@{ Step = 'item6'; Inst = 0; What = 'the peer menu did not freeze either world'; Ok = (($p4 -gt $p3) -and ($h3 -gt $h2)); Line = "peer $p3 -> $p4 host $h2 -> $h3" }) | Out-Null
Note "  $(if (($p4 -gt $p3) -and ($h3 -gt $h2)) { 'PASS' } else { 'FAIL' }) peer menu: peer unpaused $p3 -> $p4, host $h2 -> $h3"
Show-Since $P 'rig: menu lever|BLOCK global|restored global|clock:' $mMenuP2 10
Set-Control $P 'qd.menu' '0'
}

# ===========================================================================
# 13. survival and teardown
# ===========================================================================
Step 'SESSION SURVIVAL'
foreach ($i in 1, 2) {
    $alive = Test-Alive $i
    $script:verdicts.Add([pscustomobject]@{ Step = 'survival'; Inst = $i; What = 'alive at the end'; Ok = $alive; Line = '' }) | Out-Null
    Note "  $(if ($alive) { 'PASS' } else { 'FAIL' }) [$i] process alive=$alive"
    $err = Get-Matches $i 'ERR |EXCEPTION|ACCESS_VIOLATION|fatal|fault'
    Note "  [$i] error/fault lines: $($err.Count)"
    $err | Select-Object -Last 6 | ForEach-Object { Note "  [$i] $_" }
    $body = Get-Matches $i 'player damage had NO effect|player body: the hit payload|player body: spawn of the player class failed|player body: the spawn took over'
    Note "  [$i] B7/damage failure lines: $($body.Count)"
    $body | Select-Object -Last 4 | ForEach-Object { Note "  [$i] $_" }
}

foreach ($i in 1, 2) {
    if ($script:pids.ContainsKey($i)) { Stop-Process -Id $script:pids[$i] -Force -ErrorAction SilentlyContinue }
}
Stop-Process -Id $rp.Id -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1
Say 'stopped both instances and the relay'

Note ''
Note '=== VERDICTS ==='
foreach ($v in $script:verdicts) {
    Note ("  {0,-8} inst{1}  {2,-6}  {3}" -f $v.Step, $v.Inst, $(if ($v.Ok) { 'PASS' } else { 'FAIL' }), $v.What)
}
$script:lines | Set-Content -LiteralPath $Report
Say "report: $Report"
