# session-gapcombat.ps1 -- the combat-gap delegate's live proof.
#
# WHAT THIS MEASURES (COVERAGE-MATRIX gaps 5, 13, 14)
# ---------------------------------------------------
# 1. ENEMY ATTACK ANIMATIONS (gap 14).  Both players are put in a real fight; the
#    follower's copy must PLAY the owner's montages, not merely receive them.  The proof is
#    the lane's own scoreboard (`animApplied=` on `enemy detail:`), which counts verified
#    playbacks, plus at least one `enemy anim: Montage_Play verified by read-back` line.
# 2. DAMAGE BOTH WAYS (gap 13).  K_DAMAGE used to be host -> peer, so an enemy the PEER runs
#    could not hurt the host at all.  `qd.pdmg` on the PEER describes one hit from an enemy
#    that machine runs; the HOST must apply it to its own pawn.  Before the fix the host
#    refused the packet.
# 3. SPAWNED HAZARDS (gap 5).  `qd.hazard` spawns a REAL hazard class (a dragon fire puddle,
#    the same class the dragons leave) owned by a REAL enemy, on the HOST.  The PEER must
#    draw a copy AND take damage from it on its own player, because a player's HP is written
#    only by that player's machine.  The HOST must NOT apply anything to itself from the
#    lane (its own game owns that), which is the no-double-damage half.
#
# HONESTY RULES (inherited from every session since 18)
# -----------------------------------------------------
# * A verdict prints the line it read.  No line, no pass.
# * A missing precondition says REFUSED, never a pass.
# * Only lines from THIS run are read (after the last `hooked FEngineLoop`).
[CmdletBinding()]
param(
    [int]$BootSeconds = 360,
    [int]$FightSeconds = 70,
    [int]$HzSeconds = 30,
    [int]$DmgSeconds = 45,
    [string]$Report = ''
)

$ErrorActionPreference = 'Stop'
$here    = $PSScriptRoot
$repo    = Split-Path -Parent (Split-Path -Parent $here)
$gameDir = 'G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64'
$relay   = Join-Path (Split-Path -Parent $here) 'rust\target\release\qd-relay.exe'
$rigRoot = 'G:\Freespace\Queens_Domain\_test\combatrun'
$seed    = Join-Path $repo 'analysis\save-backup-before-live'
$srcIni  = Join-Path $repo 'mod\qdcoop.ini'
$outDir  = Join-Path $rigRoot 'out'
if (-not $Report) { $Report = Join-Path $outDir 'report-gapcombat.txt' }
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force -Path $outDir | Out-Null }

$script:lines = New-Object System.Collections.Generic.List[string]
$script:runCache = @{}
$script:verdicts = New-Object System.Collections.Generic.List[object]
function Say($m) {
    $s = "[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $m
    Write-Host $s
    $script:lines.Add($s) | Out-Null
}
function Note($m) { Write-Host $m; $script:lines.Add($m) | Out-Null }
function Verdict([string]$what, [bool]$ok, [string]$line) {
    $script:verdicts.Add([pscustomobject]@{ What = $what; Ok = $ok; Line = $line }) | Out-Null
    $tag = if ($ok) { 'PASS' } else { 'FAIL' }
    Note ("  {0} {1}" -f $tag, $what)
    Note ("      {0}" -f $line)
}

function Get-RigPaths([int]$i) {
    $root = Join-Path $rigRoot ("inst{0}" -f $i)
    $suffix = "qc$i"
    [pscustomobject]@{
        Index = $i; Root = $root; Suffix = $suffix
        Log = Join-Path $gameDir ("qdcoop-{0}.log" -f $suffix)
        Pid = "$root.pid"
    }
}
$inst = @{ 1 = (Get-RigPaths 1); 2 = (Get-RigPaths 2) }

function Read-LogLines([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) { return @() }
    for ($t = 0; $t -lt 3; $t++) {
        try {
            $fs = [System.IO.File]::Open($path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read,
                                         ([System.IO.FileShare]::ReadWrite -bor [System.IO.FileShare]::Delete))
            try {
                $sr = New-Object System.IO.StreamReader($fs, [System.Text.Encoding]::UTF8, $true)
                $txt = $sr.ReadToEnd(); $sr.Dispose()
            } finally { $fs.Dispose() }
            return @($txt -split "`r?`n")
        } catch { Start-Sleep -Milliseconds 50 }
    }
    return @()
}
function Get-RunLines([int]$i) {
    $p = $inst[$i].Log
    if (-not (Test-Path -LiteralPath $p)) { return @() }
    $len = (Get-Item -LiteralPath $p).Length
    if ($script:runCache.ContainsKey($i) -and $script:runCache[$i].Len -eq $len) {
        return $script:runCache[$i].Lines
    }
    $all = @(Read-LogLines $p)
    if ($all.Count -eq 0) { return @() }
    $start = 0
    for ($k = $all.Count - 1; $k -ge 0; $k--) {
        if ($all[$k] -match 'hooked FEngineLoop') { $start = $k; break }
    }
    $lines = @($all[$start..($all.Count - 1)])
    $script:runCache[$i] = [pscustomobject]@{ Len = $len; Lines = $lines }
    return $lines
}
function Get-Matches([int]$i, [string]$p) { return @(Get-RunLines $i | Where-Object { $_ -match $p }) }
function Test-Alive([int]$i) {
    if (-not $script:pids -or -not $script:pids.ContainsKey($i)) { return $false }
    return [bool](Get-Process -Id $script:pids[$i] -ErrorAction SilentlyContinue)
}
function Set-Control([int]$i, [string]$leaf, $value) {
    $p = Join-Path ([string]$inst[$i].Root) $leaf
    for ($t = 0; $t -lt 6; $t++) {
        try {
            if ($null -eq $value) { Remove-Item -LiteralPath $p -ErrorAction SilentlyContinue }
            else { Set-Content -LiteralPath $p -Value ([string]$value) -NoNewline -ErrorAction Stop }
            return
        } catch { if ($t -ge 5) { throw }; Start-Sleep -Milliseconds 120 }
    }
}
function Wait-Match([int]$i, [string]$p, [int]$secs) {
    $deadline = (Get-Date).AddSeconds($secs)
    while ((Get-Date) -lt $deadline) {
        $m = @(Get-Matches $i $p)
        if ($m.Count -gt 0) { return [string]$m[-1] }
        if (-not (Test-Alive $i)) { return '' }
        Start-Sleep -Seconds 2
    }
    return ''
}
function Stop-All([string]$Reason) {
    Say "STOP-ALL reason=$Reason"
    if ($script:relayProc) { Stop-Process -Id $script:relayProc.Id -Force -ErrorAction SilentlyContinue }
    Get-Process -Name 'QueensDomain-Win64-Shipping', 'qd-relay' -ErrorAction SilentlyContinue |
        ForEach-Object { Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue }
    Start-Sleep -Seconds 2
}

function Reset-Controls([int]$i) {
    foreach ($leaf in 'qd.tp','qd.act','qd.hit','qd.cheat','qd.cheatarg','qd.travel','qd.hard','qd.level',
                      'qd.list','qd.near','qd.drops','qd.menu','qd.efunc','qd.eattack','qd.use','qd.pdmg',
                      'qd.kill','qd.petrace','qd.enemies','qd.sequence','qd.enemykill','qd.hitguid',
                      'qd.obj','qd.flag','qd.gate','qd.take','qd.trigger','qd.selfhp','qd.nohit','qd.dlg',
                      'qd.attack','qd.equip','qd.mover','qd.wsys','qd.proj','qd.atkcall','qd.atkinfo',
                      'qd.eprops','qd.auditfork','qd.hazard','qd.audit') {
        Set-Control $i $leaf $null
    }
    Set-Control $i 'qd.scan' '1'
}

# ---------------------------------------------------------------------------
# install + seed + boot
# ---------------------------------------------------------------------------
$built = Join-Path $repo 'mod\native\build\winmm.dll'
if (-not (Test-Path -LiteralPath $built)) { Say "REFUSED: built proxy not found at $built"; exit 2 }
$h = (Get-FileHash -LiteralPath $built -Algorithm SHA256).Hash
Say ("installing proxy sha256={0}... built {1}" -f $h.Substring(0, 16), (Get-Item $built).LastWriteTime)
$dst = Join-Path $gameDir 'winmm.dll'
try { Copy-Item -LiteralPath $built -Destination $dst -Force }
catch {
    $held = $dst + '.held-' + (Get-Date -Format 'HHmmss')
    Rename-Item -LiteralPath $dst -NewName (Split-Path -Leaf $held) -Force
    Copy-Item -LiteralPath $built -Destination $dst -Force
    Say "destination was image-mapped; moved it to $held and installed the new proxy"
}

$text = Get-Content -LiteralPath $srcIni -Raw
$text = $text -replace '(?m)^RelayHost=.*$', 'RelayHost=127.0.0.1'
$text = $text -replace '(?m)^RelayPort=.*$', 'RelayPort=47810'
$text = $text -replace '(?m)^LogLevel=.*$', 'LogLevel=3'
$ini = Join-Path $gameDir 'qdcoop.ini'
$text | Set-Content -LiteralPath $ini -NoNewline
Say ("wrote {0} from the SHIPPED qdcoop.ini (relay loopback 47810, LogLevel=3)" -f $ini)
foreach ($k in 'HazardSync','DamageBothWays','EnemySync','PlayerBody','PuppetSync') {
    Say ("  {0}: {1}" -f $k, ((@(Get-Content -LiteralPath $ini | Where-Object { $_ -match "^$k=" })) -join ','))
}
Copy-Item -LiteralPath $ini -Destination (Join-Path $outDir 'qdcoop-used.ini') -Force

foreach ($i in 1, 2) {
    $null = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'make_test_instance.ps1') `
            -Name ("inst{0}" -f $i) -Index $i -SourceSave $seed -Root $rigRoot -Suffix $inst[$i].Suffix -Force 2>&1
    if ($LASTEXITCODE -ne 0) { Say "REFUSED: make_test_instance refused instance $i"; exit 2 }
}

Stop-All 'boot: replacing any process left over from an earlier run'
foreach ($i in 1, 2) {
    Reset-Controls $i
    if (Test-Path -LiteralPath $inst[$i].Pid) { Remove-Item -LiteralPath $inst[$i].Pid -ErrorAction SilentlyContinue }
    $old = $inst[$i].Log
    if (Test-Path -LiteralPath $old) { Move-Item -LiteralPath $old -Destination ($old + '.prev') -Force }
    $sc = Join-Path $gameDir ("qdcoop-{0}-respawn.ini" -f $inst[$i].Suffix)
    if (Test-Path -LiteralPath $sc) { Remove-Item -LiteralPath $sc -Force -ErrorAction SilentlyContinue }
}
$env:QDCOOP_AUTOLOAD = '0'

if (-not (Test-Path -LiteralPath $relay)) { Say "REFUSED: relay not built at $relay"; exit 2 }
$relayLog = Join-Path $outDir 'relay-gapcombat.log'
Remove-Item -LiteralPath $relayLog -ErrorAction SilentlyContinue
$script:relayProc = Start-Process -FilePath $relay -ArgumentList @('--bind', '127.0.0.1:47810') `
        -RedirectStandardOutput $relayLog -RedirectStandardError (Join-Path $outDir 'relay-gapcombat.err') `
        -WindowStyle Hidden -PassThru
Say ("relay pid={0} -> {1}" -f $script:relayProc.Id, $relayLog)
Start-Sleep -Seconds 2

$script:pids = @{}
foreach ($i in 1, 2) {
    $ueLog = Join-Path $outDir ("inst{0}-ue.log" -f $i)
    Remove-Item -LiteralPath $ueLog -ErrorAction SilentlyContinue
    $launch = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'run-hidden.ps1') `
                -UserDir $inst[$i].Root -Suffix $inst[$i].Suffix -AbsLog $ueLog 2>&1
    $gp = ($launch | Where-Object { $_ -match '^\d+$' } | Select-Object -Last 1)
    if (-not $gp) { Say "LAUNCH FAILED for instance $i"; $launch | ForEach-Object { Note $_ } }
    else { $script:pids[$i] = [int]$gp; Set-Content -LiteralPath $inst[$i].Pid -Value $gp; Say "inst$i pid=$gp" }
    Start-Sleep -Seconds 3
}

Say "waiting for a session (up to $BootSeconds s)"
$deadline = (Get-Date).AddSeconds($BootSeconds)
$roles = @{}
while ((Get-Date) -lt $deadline) {
    foreach ($i in 1, 2) {
        if ($roles.ContainsKey($i) -or -not $script:pids.ContainsKey($i)) { continue }
        $m = @(Get-Matches $i 'INFO session established: role=(HOST|PEER)')
        if ($m.Count -gt 0 -and $m[-1] -match 'role=(HOST|PEER)') {
            $roles[$i] = $Matches[1]
            Note ("  [{0}] {1}" -f $i, ([string]$m[-1]).Trim())
        }
    }
    if ($roles.Count -eq 2) { break }
    if (-not (Test-Alive 1) -and -not (Test-Alive 2)) { break }
    Start-Sleep -Seconds 2
}
if ($roles.Count -ne 2) { Say 'REFUSED: no session established'; Stop-All 'no session'; $script:lines | Set-Content $Report; exit 2 }
$hostIdx = 1; $peerIdx = 2
if ($roles[2] -eq 'HOST') { $hostIdx = 2; $peerIdx = 1 }
Say ("roles: inst{0}=HOST inst{1}=PEER" -f $hostIdx, $peerIdx)

Say 'waiting for both players in the world'
foreach ($i in 1, 2) { $null = Wait-Match $i 'INFO local pawn:' 180 }

# ---------------------------------------------------------------------------
# 0. The switches must be ON, and the lane must say so.
# ---------------------------------------------------------------------------
foreach ($i in 1, 2) {
    $m = @(Get-Matches $i 'hazard: |enemy detail: candidates=')
    Note ("  [{0}] lane lines so far: {1}" -f $i, $m.Count)
}

# ---------------------------------------------------------------------------
# 1. A REAL FIGHT: both players on an enemy, so attack montages are exchanged.
# ---------------------------------------------------------------------------
# `qd.tp` beside a named enemy kind puts this machine's player into the fight the other
# machine is already in.  The hub's Ribber/Fishman/Robot populations are what the earlier
# sessions used; the lever is only a taxi.
Say ("putting both players into a fight for {0} s" -f $FightSeconds)
foreach ($i in 1, 2) { Set-Control $i 'qd.tp' ('near:Ribber') }
Start-Sleep -Seconds 6
foreach ($i in 1, 2) {
    $m = @(Get-Matches $i 'dev tp:|rig: tp|tp near')
    if ($m.Count -gt 0) { Note ("  [{0}] {1}" -f $i, ([string]$m[-1]).Trim()) }
}
# Provoke: the game's own attack input on each machine, so the enemies answer.
foreach ($i in 1, 2) { Set-Control $i 'qd.attack' ("1#" + (Get-Date -Format 'HHmmss')) }
Start-Sleep -Seconds $FightSeconds

$animLines = @{}
$animReports = @{}
$animVerified = @{}
foreach ($i in 1, 2) {
    # The scoreboard is its own line (reported every second, whether or not a packet went
    # out); the older per-send line also carries it.
    $m = @(Get-Matches $i 'enemy anim: applied=|enemy detail: candidates=.*animApplied=')
    $animReports[$i] = if ($m.Count -gt 0) { ([string]$m[-1]).Trim() } else { '' }
    if ($animReports[$i]) { Note ("  [{0}] {1}" -f $i, $animReports[$i]) }
    $v = @(Get-Matches $i 'enemy anim: Montage_Play verified by read-back')
    $animVerified[$i] = $v.Count
}
# The highest `applied=` reported on either machine is the follower's playback count.
$appliedMax = 0
foreach ($i in 1, 2) {
    if ($animReports[$i] -match 'applied=(\d+)') {
        $n = [int]$Matches[1]
        if ($n -gt $appliedMax) { $appliedMax = $n }
    }
}
Verdict 'GAP 14: the follower PLAYED enemy montages in a real fight (animApplied > 0)' `
        ($appliedMax -gt 0) ($animReports[1] + ' | ' + $animReports[2]).Trim(' |')
Verdict 'GAP 14: at least one read-back-verified playback line' `
        (($animVerified[1] + $animVerified[2]) -gt 0) `
        ("{0} verified line(s) on inst1, {1} on inst2" -f $animVerified[1], $animVerified[2])

# ---------------------------------------------------------------------------
# 2. DAMAGE BOTH WAYS: the PEER describes a hit, the HOST applies it.
# ---------------------------------------------------------------------------
Say ("damage both ways: qd.pdmg on the PEER (inst{0}) -- the host must apply it" -f $peerIdx)
# The victim must be ALIVE and above the rig floor: the first run measured a host that had
# already been beaten to 0 HP by the fight, and the lane correctly answered
# `damage dropped: the local player is dead` -- a REFUSED measurement, not a lane failure.
Set-Control $hostIdx 'qd.selfhp' ('400#' + (Get-Date -Format 'HHmmss'))
Start-Sleep -Seconds 4
$hostHp = @(Get-Matches $hostIdx 'rig: selfhp')
if ($hostHp.Count -gt 0) { Note ("  [{0}] {1}" -f $hostIdx, ([string]$hostHp[-1]).Trim()) }
$dropsBefore = @(Get-Matches $hostIdx 'damage dropped:').Count
$pdmgNonce = Get-Date -Format 'HHmmssfff'
Set-Control $peerIdx 'qd.pdmg' ("25#" + $pdmgNonce)
$peerSent = Wait-Match $peerIdx 'rig: pdmg -> guid=' 40
$pdmgGuid = ''
if ($peerSent -match 'guid=([0-9A-F]{8})') { $pdmgGuid = $Matches[1] }
Note ("  [{0}] {1}" -f $peerIdx, $peerSent)
# The applied line must name the guid the peer described, not any earlier hit.
$hostApplied = ''
if ($pdmgGuid) {
    $deadline = (Get-Date).AddSeconds($DmgSeconds)
    while ((Get-Date) -lt $deadline) {
        $m = @(Get-Matches $hostIdx ("damage from enemy " + $pdmgGuid))
        if ($m.Count -gt 0) { $hostApplied = ([string]$m[-1]).Trim(); break }
        Start-Sleep -Seconds 2
    }
}
Note ("  [{0}] {1}" -f $hostIdx, $hostApplied)
Verdict 'GAP 13: the PEER described a hit from an enemy IT runs' ($peerSent -ne '') $peerSent
Verdict 'GAP 13: the HOST received it and applied it to its own pawn' `
        ($hostApplied -match 'applied hp \d+(\.\d+)? -> ') $hostApplied
# And the honest half: no NEW refusal line while that packet was in flight.
$dropsAfter = @(Get-Matches $hostIdx 'damage dropped:').Count
Verdict 'GAP 13: the host did not DROP the packet' (($dropsAfter - $dropsBefore) -eq 0) `
        ("{0} new dropped line(s)" -f ($dropsAfter - $dropsBefore))

# ---------------------------------------------------------------------------
# 3. SPAWNED HAZARD: the HOST spawns a real puddle; the PEER must draw it AND burn.
# ---------------------------------------------------------------------------
# Both players are put on the SAME placed actor first, so they are co-located: the hazard's
# containment test is horizontal and 300 cm wide, and "the partner happened to be 4 m away"
# would be a REFUSED measurement rather than a failure.  The class is the dragon's own fire
# puddle, owned by a real enemy -- the shape `DemonBoss_FightController_C` produces.
Say 'co-locating both players on the same placed actor before the hazard step'
foreach ($i in 1, 2) { Set-Control $i 'qd.tp' ('near:Checkpoint#' + (Get-Date -Format 'HHmmss')) }
Start-Sleep -Seconds 12
foreach ($i in 1, 2) {
    $m = @(Get-Matches $i 'puppet at |local pawn:')
    if ($m.Count -gt 0) { Note ("  [{0}] {1}" -f $i, ([string]$m[-1]).Trim()) }
}
$hazSpec = '/Game/Enemies/Dragon/Dragon_FirePuddle.Dragon_FirePuddle_C'
# BOTH players must be ALIVE and above the rig floor for the burn to be measurable: the
# second run's peer had already been beaten to 0 HP by the fight, and the lane then honestly
# logged `NO effect hp 0.0 -> 0.0`, which proves the application attempt and NOT the damage.
foreach ($i in 1, 2) { Set-Control $i 'qd.selfhp' ('400#' + (Get-Date -Format 'HHmmss')) }
Start-Sleep -Seconds 4
foreach ($i in 1, 2) {
    $m = @(Get-Matches $i 'rig: selfhp')
    if ($m.Count -gt 0) { Note ("  [{0}] {1}" -f $i, ([string]$m[-1]).Trim()) }
}
Say ("hazard: the HOST (inst{0}) spawns {1} on the local player" -f $hostIdx, $hazSpec)
Set-Control $hostIdx 'qd.hazard' ($hazSpec + '#' + (Get-Date -Format 'HHmmss'))
$spawned = Wait-Match $hostIdx 'rig: hazard spawned' 30
Note ("  [{0}] {1}" -f $hostIdx, $spawned)
$published = Wait-Match $hostIdx 'hazard: published' 20
$peerSaw = Wait-Match $peerIdx "hazard: the partner spawned" 20
$peerDrew = Wait-Match $peerIdx "hazard: copy of" 20
Note ("  [{0}] {1}" -f $hostIdx, $published)
Note ("  [{0}] {1}" -f $peerIdx, $peerSaw)
Note ("  [{0}] {1}" -f $peerIdx, $peerDrew)
Verdict 'GAP 5: the spawner published the hazard' ($published -ne '') $published
Verdict 'GAP 5: the partner HEARD about it' ($peerSaw -ne '') $peerSaw
Verdict 'GAP 5: the partner DREW a copy (visible on both screens)' ($peerDrew -ne '') $peerDrew

$peerBurns = @{}
$hostBurns = @{}
# THE VICTIM MUST BE INSIDE THE HAZARD.  The hazard lands on the HOST's own player, and the
# peer's player is wherever its own machine left it -- in the first run they were 40 km
# apart, so the peer correctly applied nothing (`0 applications`) and the verdict was a
# REFUSED measurement rather than a pass.  So: read the position the lane itself published
# and put the peer's player on it.
$hazAt = ''
if ($published -match ' at (-?\d+(?:\.\d+)?),(-?\d+(?:\.\d+)?),(-?\d+(?:\.\d+)?)') { $hazAt = $Matches[0].Trim() }
if ($hazAt -match ' at (.+)$') { $hazAt = $Matches[1] }
if ($hazAt) {
    Say ("standing the PEER (inst{0}) in the hazard at {1}" -f $peerIdx, $hazAt)
    Set-Control $peerIdx 'qd.tp' ($hazAt + '#' + (Get-Date -Format 'HHmmss'))
    Start-Sleep -Seconds 6
}
$deadline = (Get-Date).AddSeconds($HzSeconds)
while ((Get-Date) -lt $deadline) {
    $peerBurns = @(Get-Matches $peerIdx 'hazard: standing in')
    if ($peerBurns.Count -ge 2) { break }
    Start-Sleep -Seconds 2
}
foreach ($i in 1, 2) {
    $b = @(Get-Matches $i 'hazard: standing in')
    if ($b.Count -gt 0) { Note ("  [{0}] {1}" -f $i, ([string]$b[-1]).Trim()) }
}
$peerBurns = @(Get-Matches $peerIdx 'hazard: standing in')
$hostBurns = @(Get-Matches $hostIdx 'hazard: standing in')
# The verdict is HP MOVEMENT, not an attempt: `NO effect hp 0.0 -> 0.0` means the pawn was
# dead and the lane applied nothing measurable.
$peerBurnsApplied = @($peerBurns | Where-Object { $_ -match 'applied hp \d+(\.\d+)? -> ' })
Verdict 'GAP 5: the partner took damage from the mirrored hazard on ITS OWN player' `
        ($peerBurnsApplied.Count -gt 0) `
        (("{0} application(s), {1} with measured HP movement" -f $peerBurns.Count, $peerBurnsApplied.Count))
Verdict 'GAP 5: no double damage -- the spawner did NOT apply it to itself from the lane' `
        ($hostBurns.Count -eq 0) ("{0} application(s) on the host (expected 0)" -f $hostBurns.Count)
# And it must be at the hazard's cadence, not once per frame: over HzSeconds a 500 ms
# hazard is ~2 Hz, so a per-frame burn would be an order of magnitude more.
$rate = if ($HzSeconds -gt 0) { [double]$peerBurns.Count / [double]$HzSeconds } else { 0 }
Verdict 'GAP 5: the burn rate is the hazard cadence, not per frame (<= 6/s)' `
        ($rate -le 6.0) ("{0:N1} applications/s" -f $rate)

# ---------------------------------------------------------------------------
# Faults are counted separately from a lane verdict.
# ---------------------------------------------------------------------------
$faults = 0
foreach ($i in 1, 2) { $faults += @(Get-Matches $i '\bFAULT\b').Count }
Verdict 'no FAULT in either run' ($faults -eq 0) ("{0} FAULT line(s)" -f $faults)
foreach ($i in 1, 2) { Say ("inst{0} alive: {1}" -f $i, (Test-Alive $i)) }

Stop-All 'session complete'

Note ''
Note '=== verdicts ==='
foreach ($v in $script:verdicts) {
    Note ("{0} {1}" -f ($(if ($v.Ok) { 'PASS' } else { 'FAIL' })), $v.What)
}
$script:lines | Set-Content $Report
Say ("report: {0}" -f $Report)
$fail = @($script:verdicts | Where-Object { -not $_.Ok }).Count
exit ($(if ($fail -eq 0) { 0 } else { 1 }))
