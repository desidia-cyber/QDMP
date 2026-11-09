# session9.ps1 -- Session 9 of the live two-instance run, in ONE process.
#
# WHY IT IS ONE PROCESS
# ---------------------
# A launched game does not survive the command that started it (LIVE-TEST-LOG R14), so the
# relay, both instances, the driving, the collection and the teardown all happen inside
# this script.  It reads the roles from `session established` and never assumes which
# instance is the host.
#
# WHAT IT COVERS (Session 9's five items)
# ---------------------------------------
#   1. the R74 residual: the peer must APPLY the host's absolute K_ACTOR_STATES body
#      (`actor states applied=N` with `confirmed=1`) and refresh its own actor
#   2. in-world teleport follow: the follower's verb, by the signature that exists
#   3. an enemy aimed at the partner's body, and the hit arriving on the partner
#   4. a drop taken on one machine disappearing on the other
#   5. the never-run features: cutscene, movers, dialogue-on-death, suspend guard, boss bar
#
# EVERY control file is created before launch because the environment is not inherited
# reliably through the launch chain (measured with QDCOOP_TEST_TPFILE).
[CmdletBinding()]
param(
    [int]$BootSeconds = 300,
    [int]$IdleSeconds = 45,
    [int]$AttackSeconds = 90,
    [int]$SuppressMenuPause = 1,
    [string]$Report = ''
)

$ErrorActionPreference = 'Stop'
$here    = $PSScriptRoot
$repo    = Split-Path -Parent (Split-Path -Parent $here)
$gameDir = 'G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64'
$relay   = Join-Path (Split-Path -Parent $here) 'rust\target\release\qd-relay.exe'
$rigRoot = 'G:\Freespace\Queens_Domain\_test'
$outDir  = Join-Path $rigRoot 'session9'
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
        Pdmg   = Join-Path $root 'qd.pdmg'; Pid = "$root.pid"
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
function Check-Raw([string]$step, [int]$i, [string]$pattern, [string]$label) {
    $p = $inst[$i].Log
    $m = @()
    if (Test-Path -LiteralPath $p) {
        $m = @(Get-Content -LiteralPath $p -ErrorAction SilentlyContinue | Where-Object { $_ -match $pattern })
    }
    $ok = ($m.Count -gt 0)
    $script:verdicts.Add([pscustomobject]@{ Step = $step; Inst = $i; What = $label; Ok = $ok; Line = ($m | Select-Object -Last 1) }) | Out-Null
    if ($ok) { Note "  PASS [$i] $label" } else { Note "  --   [$i] $label (no raw line matches '$pattern')" }
}
function Step([string]$title, [string]$body) { Note ''; Note "### $title"; if ($body) { Note $body } }
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

# ===========================================================================
# 1. teardown, switches, install
# ===========================================================================
Say 'session9 start'
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
    'ObjectSync=1'
    "SuppressMenuPause=$SuppressMenuPause"
    'HostAuthoritative=1'
    ''
    '[Tuning]'
    'PlayerHz=30'
    'EnemyRowsPerTick=20'
    'SpawnAttemptsMax=5'
) | Set-Content -LiteralPath $ini
Say "wrote $ini (every write path on, PlayerBody on, LogLevel=3)"

foreach ($i in 1, 2) {
    foreach ($leaf in 'qd.tp','qd.act','qd.hit','qd.cheat','qd.cheatarg','qd.travel','qd.hard',
                      'qd.level','qd.near','qd.drops','qd.menu','qd.efunc','qd.eattack',
                      'qd.use','qd.pdmg') {
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
    if (-not $gp) { Say "LAUNCH FAILED for instance $i"; $launch | ForEach-Object { Note $_ } }
    else { $script:pids[$i] = [int]$gp; Set-Content -LiteralPath $inst[$i].Pid -Value $gp; Say "inst$i pid=$gp (off-screen)" }
    Start-Sleep -Seconds 3
}

# ===========================================================================
# 2. session + gameplay + roles
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
# 3. the load evidence
# ===========================================================================
Step 'LOAD -- switches, oracle, containers, class, hooks'
foreach ($i in 1, 2) {
    Show-Log $i 'switches:|NAMED-SAVE ORACLE|container offsets:|enemy sync: FS_EnemyCharacterBase|hooked ULevelStreaming|hooks installed|player body: class|player body: spawned' 16
    Check-Raw 'load' $i 'switches: .*actors=1' 'ActorStateSync is on' 0 | Out-Null
}

Step "IDLE ${IdleSeconds}s"
if ($IdleSeconds -gt 0) { Start-Sleep -Seconds $IdleSeconds }
foreach ($i in 1, 2) {
    $ok = [bool](Get-Process -Id $script:pids[$i] -ErrorAction SilentlyContinue)
    Note "  $(if ($ok) { 'PASS' } else { 'FAIL' }) [$i] alive after ${IdleSeconds}s idle"
    $err = Get-Matches $i 'ERR |EXCEPTION|ACCESS_VIOLATION|fatal|\bfault'
    Note "  [$i] error/fault lines: $($err.Count)"
    $err | Select-Object -Last 4 | ForEach-Object { Note "  [$i] $_" }
}

# ===========================================================================
# ITEM 1 -- the R74 residual: peer applies the host's absolute actor states
# ===========================================================================
Step 'ITEM 1 -- peer applies the host K_ACTOR_STATES body; confirmed=1; refresh ok'
Note '  The relay''s decoder treated the absolute-container header `n` as "entries in this'
Note '  datagram"; a 117-entry set is 3 datagrams, so every chunk failed validation and'
Note '  was dropped. Fixed in qd-core/protocol.rs (decode the prefix). This is the proof.'
# The host half FIRST, because a pure host->peer apply needs a value the peer has never
# seen: the host opens a savable the peer's container still holds at 0.  The peer then
# shows `actor states applied=1` (a value it did not produce) and its own actor refresh.
# The peer half SECOND, because `confirmed=1` needs a pending intent of the peer's own.
foreach ($i in 1, 2) { Show-Log $i 'actor states (sent|applied)|actor-state' 4 }
$m1H = Get-Mark $H
$m1P = Get-Mark $P
# The target is chosen from the PEER'S OWN scan: a savable it reports at value=0.  That is
# the only way the apply is guaranteed to be an apply -- two instances seeded from one
# save have converged over previous sessions, so "the host opened its nearest coin" can
# easily be a value the peer already holds, and then `applied=0` is correct and proves
# nothing.
$zeroName = ''
[void](Wait-Pattern $P 'scan: savable [0-9] ' 45)
$peerScans = @(Get-Matches $P 'scan: savable [0-9] ')
foreach ($line in $peerScans) {
    if ($line -match 'scan: savable \d+ (\S+) at .* value=0') { $zeroName = $Matches[1] }
}
if ($zeroName) { Say "  HOST=inst$H opens '$zeroName' (the peer's own scan says value=0)" }
else { $zeroName = 'coin'; Say "  HOST=inst$H opens the nearest coin (the peer's scan named no zero)" }
Invoke-Go $H $zeroName 140
Start-Sleep -Seconds 8
Invoke-ActNear $H $zeroName
Check 'item1' $H 'dev state: .* savable=yes' 20 'the host pick is a SavableActor (has a GUID)' $m1H | Out-Null
Check 'item1' $H 'actor-state host set ' 25 'the host container changed' $m1H | Out-Null
Check 'item1' $P 'actor states applied=[1-9]' 60 'the peer APPLIES the host absolute body' $m1P | Out-Null
Check 'item1' $P 'actor-state refresh ok' 60 'the peer refreshes its own actor' $m1P | Out-Null
Check 'item1' $P 'actor-state refresh INCOMPLETE|actor-state refresh failed' 5 'NO INCOMPLETE/failed refresh' $m1P -Absent | Out-Null
$m1bP = Get-Mark $P
$m1bH = Get-Mark $H
Say "  PEER=inst$P opens a savable that is still 0; the host applies and the peer confirms"
Invoke-Go $P 'coin' 200
Start-Sleep -Seconds 6
Invoke-ActNear $P 'coin'
Check 'item1' $P 'actor-state local change' 25 'the peer container changed locally' $m1bP | Out-Null
Check 'item1' $P 'actor-state intent sent' 25 'the peer offers the change' $m1bP | Out-Null
Check 'item1' $H 'actor-state intent applied' 25 'the host applies the intent' $m1bH | Out-Null
Check 'item1' $H 'actor-state refresh ok' 45 'the host refreshes its own actor' $m1bH | Out-Null
Check 'item1' $P 'actor states applied=.*confirmed=1' 60 'the peer CONFIRMS its own intent' $m1bP | Out-Null
Check 'item1' $P 'actor-state refresh ok' 60 'the peer refreshes its own actor' $m1bP | Out-Null
Show-Since $P 'dev interact:|dev state:|actor-state|actor states' $m1P 24
Show-Since $H 'dev interact:|dev state:|actor-state' $m1H 14
foreach ($i in 1, 2) { Show-Log $i 'actor states applied=|actor states sent' 3 }

# ===========================================================================
# ITEM 2 -- in-world teleport follow
# ===========================================================================
Step 'ITEM 2 -- in-world teleport follow (the follower verb, by its real signature)'
$m2P = Get-Mark $P
$m2H = Get-Mark $H
Say "  peer=inst$P drives the nearest teleporter; the host must follow"
Set-Control $P 'qd.travel' 'near:teleport'
Check 'item2' $P 'rig: travel near' 25 'a teleporter was found' $m2P | Out-Null
Check 'item2' $P 'travel: .*signature resolved' 25 'the travel entry signature was resolved' $m2P | Out-Null
Check 'item2' $P 'rig: travel lever -> .*TeleportToLocation ran=1|rig: travel lever -> .*ran=1' 25 'the origin drives the game entry' $m2P | Out-Null
Check 'item2' $P 'travel: local destination ' 25 'the origin announces a destination' $m2P | Out-Null
Check 'item2' $H 'travel: followed ' 60 'the partner follows (travel: followed)' $m2H | Out-Null
Show-Since $P 'rig: travel|travel: |signature travel' $m2P 14
Show-Since $H 'travel: ' $m2H 10
foreach ($i in 1, 2) { Note "  [$i] alive after the teleport: $(Test-Alive $i)" }

# ===========================================================================
# ITEM 3 -- an enemy attacks the partner body, and the hit reaches the partner
# ===========================================================================
Step 'ITEM 3 -- an enemy aimed at the partner body; the hit lands on the partner'
Note '  Both players stand by the SAME enemy, the peer nearer: ownership stays with the host'
Note '  (800 cm margin) while TargetPlayer flips to the proxy (200 cm margin).'
$m3H = Get-Mark $H
$m3P = Get-Mark $P
# `near:enemy` on each machine picks THAT machine's nearest enemy, so the two players can
# end up kilometres apart (measured: 4 km, and every swing refused on range).  The host is
# standing on ground known to be standable, so the peer is put on the host's own
# coordinates and the host then steps back -- which is exactly the geometry the lane wants:
# the proxy inside the enemy's reach, the owner's own player outside it.
Invoke-Go $H 'enemy' 400
Start-Sleep -Seconds 10
$hp = Get-LastPos $H
if ($hp) {
    Set-Control $P 'qd.near' 200
    Set-Control $P 'qd.tp' ("{0},{1},{2},90" -f $hp[0], $hp[1], $hp[2])
    Say ("  peer moved onto the host's ground: {0:N0},{1:N0},{2:N0}" -f $hp[0], $hp[1], $hp[2])
} else {
    Say '  -- host position unreadable; the peer stays where it is'
}
Start-Sleep -Seconds 10
Invoke-Go $H 'enemy' 900
Start-Sleep -Seconds 8
foreach ($i in 1, 2) {
    Set-Control $i 'qd.efunc' '1'
    Set-Control $i 'qd.eattack' '1'
}
$deadline = (Get-Date).AddSeconds($AttackSeconds)
$hit = $false
while ((Get-Date) -lt $deadline) {
    if (@(Get-Since $H $m3H | Where-Object { $_ -match 'player body hit .* -> sent' }).Count -gt 0) { $hit = $true; break }
    if (@(Get-Since $H $m3H | Where-Object { $_ -match 'damage sent guid=' }).Count -gt 0) { $hit = $true; break }
    Start-Sleep -Seconds 2
}
foreach ($i in 1, 2) { Set-Control $i 'qd.eattack' $null; Set-Control $i 'qd.efunc' $null }
Check 'item3' $H 'TargetPlayer local player -> the remote body \(read back\)' 20 'the enemy is steered at the partner body' $m3H | Out-Null
Check 'item3' $H 'player body hit guid=.* -> sent to the machine that owns the player|damage sent guid=' 20 'the swing reaches the victim machine' $m3H | Out-Null
Check 'item3' $P 'damage from enemy .* applied hp' 40 'the partner applies the hit (hp a -> b)' $m3P | Out-Null
Check 'item3' $P 'player damage had NO effect' 3 'NO player-damage no-effect line' $m3P -Absent | Out-Null
Show-Since $H 'TargetPlayer|player body hit|damage sent|damage: swing|damage: guid=|is aimed at the proxy' $m3H 20
Show-Since $P 'damage from enemy|damage: player hp' $m3P 8

# ===========================================================================
# ITEM 4 -- a drop taken on one machine disappears on the other
# ===========================================================================
Step 'ITEM 4 -- loot: a drop taken on one machine is tombstoned on the other'
$m4P = Get-Mark $P
$m4H = Get-Mark $H
foreach ($i in 1, 2) { Set-Control $i 'qd.drops' '1' }
Say '  BOTH machines kill the same enemy: each rolls its own drop at the same place, so the'
Say '  derived keys agree and each machine holds an actor to hide.'
foreach ($i in 1, 2) { Set-Control $i 'qd.near' '250'; Invoke-Go $i 'enemy' }
Start-Sleep -Seconds 8
foreach ($i in 1, 2) { Set-Control $i 'qd.hit' '400' }
Start-Sleep -Seconds 35
foreach ($i in 1, 2) { Set-Control $i 'qd.hit' $null }
Start-Sleep -Seconds 3
Show-Since $P 'rig: drop probe|loot: |rig: hit' $m4P 16
Show-Since $H 'rig: drop probe|loot: |rig: hit' $m4H 16
Check 'item4' $P 'loot: drop ' 30 'the killer ledger learns a drop' $m4P | Out-Null
Check 'item4' $H 'loot: merged ' 30 'the other machine merges the drop set' $m4H | Out-Null
Say "  peer=inst$P walks to the ledger drop and takes it"
Invoke-Go $P 'loot' 40
Start-Sleep -Seconds 5
Invoke-ActNear $P 'loot'
Check 'item4' $P 'loot: local pickup .* -- tombstoned for the partner' 45 'the pickup is tombstoned' $m4P | Out-Null
Check 'item4' $H 'loot: hiding local drop .* -- the partner took it' 45 'the other machine hides its copy' $m4H | Out-Null
Show-Since $P 'dev interact: resolved .drop.|loot: ' $m4P 12
Show-Since $H 'loot: ' $m4H 12

# ===========================================================================
# ITEM 5 -- the never-run features
# ===========================================================================
Step 'ITEM 5a -- cutscene sync (find one, trigger it, watch both machines)'
foreach ($i in 1, 2) { Show-Log $i 'scan: loose (cutscene|sequence)' 4 }
$m5aH = Get-Mark $H
$m5aP = Get-Mark $P
Invoke-Go $H 'cutscene' 200
Start-Sleep -Seconds 6
Invoke-ActNear $H 'cutscene'
Start-Sleep -Seconds 8
Invoke-Cheat $H 'RingLibraryBell'
Start-Sleep -Seconds 12
Check 'item5' $H 'cutscene: local started|cutscene: local ended|rig: cheat RingLibraryBell' 10 'a cutscene origin fired on the host' $m5aH | Out-Null
Check 'item5' $P 'cutscene: partner started|cutscene: partner ended|cutscene: playing the partner|cutscene: holding the local player' 30 'the partner reacts to it' $m5aP | Out-Null
Show-Since $H 'cutscene:|rig: cheat|dev interact' $m5aH 10
Show-Since $P 'cutscene:' $m5aP 8

Step 'ITEM 5b -- movers (an elevator or platform)'
foreach ($i in 1, 2) { Show-Log $i 'scan: loose (elevator|platform)' 4 }
$m5bH = Get-Mark $H
$m5bP = Get-Mark $P
Invoke-Go $H 'elevator' 100
Start-Sleep -Seconds 10
Show-Since $H 'scan: loose (elevator|platform)|movers:' $m5bH 10
Invoke-Go $P 'elevator' 100
Start-Sleep -Seconds 12
Check 'item5' $H 'movers: tracking ' 30 'a mover is tracked on one machine' $m5bH | Out-Null
Check 'item5' $P 'movers: applied [1-9]|movers: tracking ' 40 'the other machine applies or tracks it' $m5bP | Out-Null
Show-Since $H 'movers:' $m5bH 8
Show-Since $P 'movers:' $m5bP 8

Step 'ITEM 5c -- dialogue ends on death'
$m5cP = Get-Mark $P
Invoke-Go $P 'npc' 200
Start-Sleep -Seconds 6
Invoke-ActNear $P 'npc'
Check 'item5' $P 'dialogue: local started' 25 'a conversation started on the peer' $m5cP | Out-Null
# Take the god floor away so a lethal hit can actually land, then kill this machine's
# player through the game's own cheat object (the rig's floor would refill it otherwise).
Set-Control $P 'qd.god' $null
Start-Sleep -Seconds 2
Invoke-Cheat $P 'HurtMe' '5000'
Start-Sleep -Seconds 10
Check 'item5' $P 'dialogue: local death ended the conversation' 25 'the death ended the conversation' $m5cP | Out-Null
Show-Since $P 'dialogue:|rig: cheat|DIE|death' $m5cP 16
Set-Control $P 'qd.god' '400'

Step 'ITEM 5d -- suspend-save guard (suspend save on the host, then a load)'
$m5dH = Get-Mark $H
$m5dP = Get-Mark $P
Invoke-Cheat $H 'SuspendSave'
Start-Sleep -Seconds 12
Invoke-Cheat $H 'LoadSaveSlot'
Start-Sleep -Seconds 20
Check 'item5' $H 'suspend: host re-applying|suspend: peer loaded a suspend save|rig: cheat (SuspendSave|LoadSaveSlot)' 15 'the suspend path ran on the host' $m5dH | Out-Null
Check 'item5' $P 'suspend: peer holds BlockSuspendSaves|suspend: partner re-applied|suspend: partner loaded a suspend save' 25 'the peer took part in the guard' $m5dP | Out-Null
Show-Since $H 'suspend:|rig: cheat' $m5dH 14
Show-Since $P 'suspend:|rig: cheat' $m5dP 10

Step 'ITEM 5e -- boss bar (if a boss is reachable)'
foreach ($i in 1, 2) { Show-Log $i 'scan: loose (boss|whale)' 4 }
$m5eH = Get-Mark $H
Invoke-Go $H 'boss' 400
Start-Sleep -Seconds 15
Show-Since $H 'scan: loose (boss|whale)|boss:' $m5eH 14
Check 'item5' $H 'boss: guid=.* is a boss|scan: loose (boss|whale)' 20 'a boss was found (or the scan says none)' $m5eH | Out-Null

# ===========================================================================
# ITEM 6 -- the OBJECTS lane (0x2A): a placed chest, and a placed pickup
# ===========================================================================
Step 'ITEM 6 -- OBJECTS: a placed chest opens on the partner, and a placed pickup disappears'
Note '  The chest is not a SavableActor (R67: SG_Chest_BP2 savable=no guid=-), so it is the'
Note '  objects lane that has to carry it; the two lanes skip each other by the GUIDs array.'
foreach ($i in 1, 2) { Show-Log $i 'object sync: tracking|objects sent' 8 }
$m6H = Get-Mark $H
$m6P = Get-Mark $P
Invoke-Go $H 'closedchest' 200
Start-Sleep -Seconds 6
$haveChest = @(Get-Since $H $m6H | Where-Object { $_ -match 'dev interact: resolved' }).Count
if ($haveChest -eq 0) {
    Say '  no chest in reach of this world; streaming 200_AQ (where Session 8 found one)'
    foreach ($i in 1, 2) { Set-Control $i 'qd.level' '200_AQ' }
    Start-Sleep -Seconds 45
    Invoke-Go $H 'closedchest' 200
    Start-Sleep -Seconds 8
}
Invoke-ActNear $H 'closedchest'
Check 'item6' $H 'object sync: tracking chest ' 20 'the chest is classified by the objects lane' $m6H | Out-Null
Check 'item6' $P 'object sync: tracking chest ' 20 'the partner names the same chest' $m6P | Out-Null
Check 'item6' $H 'object local change' 25 'the host chest changed locally' $m6H | Out-Null
Check 'item6' $H 'object intent sent key=' 25 'the host announces the intent' $m6H | Out-Null
Check 'item6' $P 'object intent applied: ' 30 'the partner applies the intent' $m6P | Out-Null
Check 'item6' $P 'objects applied=[1-9].*confirmed=1' 30 'the partner confirms the round trip' $m6P | Out-Null
Check 'item6' $H 'object intent confirmed' 30 'the host settles the intent' $m6H | Out-Null
# The state and the visual on the partner, the same probe R67 used to show it CLOSED.
Say '  the partner now probes the same chest: it must read opened=1'
Invoke-Go $P 'closedchest' 200
Start-Sleep -Seconds 6
Invoke-ActNear $P 'ClosedChest'
Start-Sleep -Seconds 8
Check 'item6' $P 'dev state: .* opened=1|dev state: .* isOpen=1|object apply: chest .* prop=Opened 0->1' 25 'the partner chest reads OPEN' $m6P | Out-Null
$againH = @(Get-Since $H $m6H | Where-Object { $_ -match 'object intent sent key=' })
$againP = @(Get-Since $P $m6P | Where-Object { $_ -match 'object intent sent key=' })
$okAgain = ($againH.Count -le 1 -and $againP.Count -eq 0)
$script:verdicts.Add([pscustomobject]@{ Step = 'item6'; Inst = $P; What = 'a second open is not a second intent'; Ok = $okAgain; Line = "host open intents=$($againH.Count) peer second-act intents=$($againP.Count)" }) | Out-Null
Note "  $(if ($okAgain) { 'PASS' } else { '--  ' }) intents for the chest: host=$($againH.Count) peer=$($againP.Count) (one per side is the open itself)"
foreach ($i in 1, 2) {
    $dup = @(Get-Matches $i 'flag CHANGE TotalChestsOpened : ([0-9]+) -> ([0-9]+)' | Where-Object { $_ -match ': 2 -> 3|: 3 -> 4' })
    $script:verdicts.Add([pscustomobject]@{ Step = 'item6'; Inst = $i; What = 'TotalChestsOpened is not double-counted'; Ok = ($dup.Count -eq 0); Line = ($dup | Select-Object -First 1) }) | Out-Null
    Note "  $(if ($dup.Count -eq 0) { 'PASS' } else { 'FAIL' }) [$i] TotalChestsOpened not bumped twice"
}
Show-Since $H 'object sync: tracking|object local change|object intent|dev interact|dev state:|objects sent' $m6H 18
Show-Since $P 'object sync: tracking|object intent applied|objects applied|object apply:|dev state:' $m6P 18

Step 'ITEM 6b -- a placed pickup taken on the host disappears on the partner'
$m6bH = Get-Mark $H
$m6bP = Get-Mark $P
$pickName = ''
foreach ($line in @(Get-Matches $H 'object sync: tracking pickup ')) {
    if ($line -match 'tracking pickup (\S+) \[') { $pickName = $Matches[1] }
}
if ($pickName) {
    Say "  host walks to the objects-lane pickup '$pickName' and takes it"
    Invoke-Go $H $pickName 120
    Start-Sleep -Seconds 6
    Invoke-ActNear $H $pickName
    Check 'item6' $H 'object local change|object intent sent key=' 30 'the pickup change is announced' $m6bH | Out-Null
    Check 'item6' $P 'object apply: pickup |object intent applied: ' 35 'the partner applies the pickup' $m6bP | Out-Null
} else {
    Note '  -- item6b: the objects lane tracks no placed pickup in this world (NOT-REACHED)'
    $script:verdicts.Add([pscustomobject]@{ Step = 'item6'; Inst = $H; What = 'a placed pickup is tracked by the objects lane'; Ok = $false; Line = '' }) | Out-Null
}
Show-Since $H 'object sync: tracking pickup|object local change|object intent|dev interact' $m6bH 12
Show-Since $P 'object apply:|object intent applied|objects applied' $m6bP 9

# ===========================================================================
# survival and teardown
# ===========================================================================
Step 'SESSION SURVIVAL'
foreach ($i in 1, 2) {
    $alive = Test-Alive $i
    $script:verdicts.Add([pscustomobject]@{ Step = 'survival'; Inst = $i; What = 'alive at the end'; Ok = $alive; Line = '' }) | Out-Null
    Note "  $(if ($alive) { 'PASS' } else { 'FAIL' }) [$i] process alive=$alive"
    $err = Get-Matches $i 'ERR |EXCEPTION|ACCESS_VIOLATION|fatal|\bfault'
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
