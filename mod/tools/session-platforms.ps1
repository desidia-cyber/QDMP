# session-platforms.ps1 -- the platforms lane, end to end: find a real lift, put a player on
# it, and show the PARTNER's machine drawing them on ITS OWN copy of it.
#
# WHY THE STEPS ARE SHAPED THIS WAY
# --------------------------------
# * The lane's whole failure before this was that the CLASS FILTER never matched a lift, so
#   no earlier session could say anything about a mover. The first verdict therefore is
#   `movers: tracking ...` for a `*_Lift_*` and `scan: loose lift <name>`.
# * A rider has to be ON the platform for the ride lane to engage, and `near:<name>` stands
#   the player BESIDE an actor (measured: 140 cm away). The `:on` spec stands them exactly on
#   it with a sweep, which is what R153 added.
# * The lift is triggered through the game's own verb (`qd.act near:<name>`), because a lift
#   that nobody pressed never moves. But nothing about that matters to the lane: the
#   follower follows the TRANSFORM, so the lever/plate/terminal that started it is irrelevant.
#
# ONE PROCESS, like every other session script: a launched game does not survive the command
# that started it.
[CmdletBinding()]
param(
    [ValidateSet('probe', 'ride', 'all')]
    [string]$Phase = 'ride',
    [int]$BootSeconds = 240,
    [int]$StepSeconds = 90,
    [string]$Report = ''
)

$ErrorActionPreference = 'Stop'
$here    = $PSScriptRoot
$repo    = 'G:\Freespace\Queens_Domain\.worktrees\sync-fix'
$gameDir = 'G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64'
$relay   = Join-Path $repo 'mod\rust\target\release\qd-relay.exe'
$rigRoot = 'G:\Freespace\Queens_Domain\_test'
$outDir  = Join-Path $rigRoot 's31-platforms'
if (-not $Report) { $Report = Join-Path $outDir 'report.txt' }
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force -Path $outDir | Out-Null }

$script:lines = New-Object System.Collections.Generic.List[string]
function Say($m) {
    $s = "[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $m
    Write-Output $s
    $script:lines.Add($s) | Out-Null
}
function Note($m) { Write-Output $m; $script:lines.Add($m) | Out-Null }

# ---------------------------------------------------------------------------
# 0. the LIVE lock is the contract: only one session on this machine, and it is mine.
#    Refusing here is what keeps this script from killing someone else's run.
# ---------------------------------------------------------------------------
$lockDir = Join-Path $rigRoot 'locks\live'
$owner = ''
if (Test-Path (Join-Path $lockDir 'owner.txt')) {
    $owner = (Get-Content (Join-Path $lockDir 'owner.txt') -ErrorAction SilentlyContinue | Select-Object -First 1)
}
if ($owner -ne 'platforms') {
    Say "REFUSED: the LIVE lock is not mine (owner='$owner'). Nothing was started."
    exit 2
}

function Get-RigPaths([int]$i) {
    $root   = Join-Path $rigRoot ("inst{0}" -f $i)
    [pscustomobject]@{
        Index = $i; Root = $root; Suffix = "qd$i"
        Log = Join-Path $gameDir ("qdcoop-{0}.log" -f "qd$i")
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
function Set-Control([int]$i, [string]$leaf, [string]$value) {
    $p = Join-Path $inst[$i].Root $leaf
    if ($null -eq $value) { Remove-Item -LiteralPath $p -ErrorAction SilentlyContinue }
    else { Set-Content -LiteralPath $p -Value $value -NoNewline }
}
$script:verdicts = New-Object System.Collections.Generic.List[object]
function Check([string]$step, [int]$i, [string]$pattern, [int]$secs, [string]$label,
               [int]$Mark = 0, [switch]$Absent) {
    if (-not (Test-Alive $i)) {
        $script:verdicts.Add([pscustomobject]@{ Step=$step; Inst=$i; What=$label; Ok=$false; Line='INSTANCE DIED' }) | Out-Null
        Note "  FAIL [$i] $label -- the instance is no longer running"
        return $false
    }
    $m = Wait-Since $i $pattern $Mark $secs
    $found = ($m.Count -gt 0)
    $ok = if ($Absent) { -not $found } else { $found }
    $script:verdicts.Add([pscustomobject]@{ Step=$step; Inst=$i; What=$label; Ok=$ok; Line=($m | Select-Object -Last 1) }) | Out-Null
    if ($ok) { Note "  PASS [$i] $label" }
    else { Note "  --   [$i] $label $(if ($Absent) { '(the line APPEARED)' } else { "(nothing matches '$pattern' in ${secs}s)" })" }
    return $ok
}
function Show-Log([int]$i, [string]$pattern, [int]$n) {
    $m = Get-Matches $i $pattern
    if ($m.Count -eq 0) { Note "  [$i] (no line matches '$pattern')"; return }
    $m | Select-Object -Last $n | ForEach-Object { Note "  [$i] $_" }
}
function Step([string]$t, [string]$b) { Note ''; Note "### $t"; if ($b) { Note $b } }

# The game boots to `title_screen` and waits for a key.  `run-hidden.ps1` only parks the
# window, so a session script has to press: measured here, the world sat on
# `world: local=title_screen partner=title_screen` for four minutes with no press.
function Press-Key([int]$i, [int]$vk = 0x0D) {
    if (-not (Test-Alive $i)) { return }
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'send-input.ps1') `
        -ProcessId $script:pids[$i] -Vk $vk 2>&1 | Out-Null
}

# ---------------------------------------------------------------------------
# 1. teardown of anything left over.  Safe because the LIVE lock is ours.
# ---------------------------------------------------------------------------
Say "scenario start: phase=$Phase"
Get-Process -Name 'QueensDomain-Win64-Shipping', 'qd-relay' -ErrorAction SilentlyContinue | ForEach-Object {
    Say "  stopping leftover $($_.ProcessName) pid=$($_.Id)"
    Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue
}
Start-Sleep -Seconds 2

$built = Join-Path $repo 'mod\native\build\winmm.dll'
if (-not (Test-Path -LiteralPath $built)) { Say "REFUSED: $built missing (run native\build.bat)"; exit 2 }
Copy-Item -LiteralPath $built -Destination (Join-Path $gameDir 'winmm.dll') -Force
Say "installed winmm.dll ($((Get-Item $built).Length) bytes) into $gameDir"

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
    'MoverSync=1'
    'PlatformRide=1'
    'PuppetSync=1'
    'PlayerBody=1'
    'EnemySync=0'
    'StreamUnion=1'
    'TravelSync=1'
    'SuppressMenuPause=0'
    'HostAuthoritative=1'
    ''
    '[Tuning]'
    'PlayerHz=30'
    'EnemyRowsPerTick=20'
) | Set-Content -LiteralPath $ini
Say "wrote $ini (MoverSync=1 PlatformRide=1 PuppetSync=1 PlayerBody=1, LogLevel=3)"

foreach ($i in 1, 2) {
    Set-Control $i 'qd.tp' $null
    Set-Control $i 'qd.act' $null
    Set-Control $i 'qd.hard' $null
    Set-Control $i 'qd.cheat' $null
    Set-Control $i 'qd.scan' '1'
    Set-Control $i 'qd.god' '400'
    Set-Control $i 'qd.hit' $null
    if (Test-Path -LiteralPath $inst[$i].Pid) { Remove-Item -LiteralPath $inst[$i].Pid -ErrorAction SilentlyContinue }
}

# ---------------------------------------------------------------------------
# 2. relay + both instances
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
    if (-not $gp) { Say "LAUNCH FAILED for instance $i"; $launch | ForEach-Object { Note $_ } }
    else { $script:pids[$i] = [int]$gp; Set-Content -LiteralPath $inst[$i].Pid -Value $gp; Say "inst$i pid=$gp (off-screen)" }
    Start-Sleep -Seconds 3
}
if (-not $script:pids[1] -or -not $script:pids[2]) { Say 'ABORT: both instances are required'; }

# ---------------------------------------------------------------------------
# 3. session + gameplay
# ---------------------------------------------------------------------------
Say "waiting for a session (up to $BootSeconds s)"
$deadline = (Get-Date).AddSeconds($BootSeconds)
$roles = @{}
while ((Get-Date) -lt $deadline) {
    foreach ($i in 1, 2) {
        if ($roles.ContainsKey($i) -or -not $script:pids.ContainsKey($i)) { continue }
        $m = Get-Matches $i 'session established'
        if ($m.Count -gt 0) { $roles[$i] = if ($m[-1] -match 'role=HOST') { 'HOST' } else { 'PEER' } }
    }
    if ($roles.Count -eq 2) { break }
    Start-Sleep -Seconds 1
}
foreach ($i in 1, 2) {
    if ($roles.ContainsKey($i)) { Say "inst$i role=$($roles[$i])" } else { Say "inst$i NEVER ESTABLISHED A SESSION" }
}
$hostI = @($roles.Keys | Where-Object { $roles[$_] -eq 'HOST' } | Select-Object -First 1)
$peerI = @($roles.Keys | Where-Object { $roles[$_] -eq 'PEER' } | Select-Object -First 1)
if ($hostI.Count -eq 0 -or $peerI.Count -eq 0) { Say 'ABORT: no host/peer pair'; $hostI = 1; $peerI = 2 }
$H = [int]$hostI[0]; $P = [int]$peerI[0]
Say "host=inst$H peer=inst$P"

foreach ($i in 1, 2) {
    $ok = $false
    $dl = (Get-Date).AddSeconds($BootSeconds)
    $pressed = 0
    while ((Get-Date) -lt $dl) {
        if ((Get-Matches $i 'scan: health|scan: local at|dev autoload: in game').Count -gt 0) { $ok = $true; break }
        # Enter (and then Space) walks the title/menu; press at most every 8 s so the game has
        # time to load between them.
        if (((Get-Date) -lt $dl) -and ($pressed -lt 12)) {
            Press-Key $i 0x0D
            Start-Sleep -Milliseconds 700
            Press-Key $i 0x20
            ++$pressed
        }
        Start-Sleep -Seconds 6
    }
    Say "inst${i} gameplay=$ok (keys pressed=$pressed)"
}
$mBootH = Get-Mark $H
$mBootP = Get-Mark $P

Step 'LOAD -- the classifier says what it is' 'the old filter matched a LIGHT and no lift; read the new one'
foreach ($i in 1, 2) { Show-Log $i 'platforms: mover class filter|platforms: K_PLATFORM_RIDE' 4 }

Step 'PROBE -- what this world actually contains' 'scan: loose lift / elevator / platform / tram / mover'
foreach ($i in 1, 2) {
    Note "  --- inst$i ---"
    Show-Log $i 'scan: loose (lift|elevator|platform|tram|mover)|movers: tracking' 14
}

if ($Phase -eq 'probe') {
    foreach ($i in 1, 2) {
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'capture-hidden.ps1') `
            -ProcessId $script:pids[$i] -Out (Join-Path $outDir "probe-inst$i.png") 2>&1 | Out-Null
    }
    Say 'probe phase: nothing further is driven'
} else {
    # -----------------------------------------------------------------------
    # 4. find a lift: try the areas the analysis corpus puts lifts in
    # -----------------------------------------------------------------------
    Step 'ENTER -- a level that has a lift' 'analysis/22-close-out.md section 3 puts *_Lift_* in the Mausoleum, the Lab, the Castle and OtherWorld/Lift_00'
    $areas = @('400_MAUS_entrance', '420_MAUS_mid', '450_MAUS_darkmaze', 'Lift_00',
               'OW_Lift_00', '600_Castle', '610_Castle_Ext', 'Lab', '200_AQ', '780_SZ_Depths')
    $found = $false
    $nonce = 0
    foreach ($area in $areas) {
        if ($found) { break }
        $nonce++
        $mH = Get-Mark $H
        Set-Control $H 'qd.hard' ($area + '#' + $nonce)
        Say "  hard[$H] -> $area (nonce $nonce)"
        Start-Sleep -Seconds 12
        $m = @(Get-Since $H $mH | Where-Object { $_ -match ('rig: hard lever -> ' + [regex]::Escape($area)) })
        if ($m.Count -gt 0) { Note "  [$H] $($m[-1])" }
        Start-Sleep -Seconds 10
        $lifts = @(Get-RunLines $H | Where-Object {
            ($_ -match 'scan: loose (lift|elevator|platform|tram|mover) ') -and
            ($_ -notmatch 'none in this world') })
        if ($lifts.Count -gt 0) { $found = $true }
    }

    Step 'COVERAGE -- the mover table' 'every tracked mover on both machines'
    foreach ($i in 1, 2) {
        Note "  --- inst$i movers ---"
        Show-Log $i 'movers: tracking|movers: .*not tracked' 14
    }

    $liftName = ''
    $liftX = 0; $liftY = 0; $liftZ = 0
    # NOTE: read the log lines directly here. `Get-Matches` returns the array WRAPPED
    # (`return ,$res`), so piping its result into Where-Object filters the wrapper, not the
    # lines -- which is why two earlier versions of this block found no candidate while the
    # log listed six lifts.
    $liftLines = @(Get-RunLines $H | Where-Object {
        ($_ -match 'scan: loose (lift|elevator|platform|tram|mover) ') -and
        ($_ -notmatch 'none in this world') })
    $liftLine = $liftLines | Select-Object -Last 1
    if ($liftLine -and $liftLine -match 'scan: loose (?:lift|elevator|platform|tram|mover) (\S+) \[') {
        $liftName = $Matches[1]
        if ($liftLine -match ' at (-?[0-9.]+),(-?[0-9.]+),(-?[0-9.]+)') {
            $liftX = [double]$Matches[1]; $liftY = [double]$Matches[2]; $liftZ = [double]$Matches[3]
        }
    }
    Note "  lift candidate: '$liftName' at $liftX,$liftY,$liftZ"

    # Marked 0, not the boot mark: the PEER streamed these levels BEFORE it reported gameplay
    # (measured), so a boot mark excluded the only lines this verdict is about.
    Check 'movers' $H 'movers: tracking \S*(Lift|Elevator|Tram|Mover|Scaffold|Bridge)' 10 `
          'the HOST tracks a real mover class' 0 | Out-Null
    Check 'movers' $P 'movers: tracking \S*(Lift|Elevator|Tram|Mover|Scaffold|Bridge)' 10 `
          'the PEER tracks the same class' 0 | Out-Null
    Check 'movers' $H 'movers: tracking \S*Light' 5 'NO light is tracked as a mover' 0 -Absent | Out-Null
    if ($liftName) {
        $sameIdH = @()
        $sameIdP = @()
        foreach ($c in @($liftName, 'Mausoleum_Lift_00_BP2')) {
            $h1 = @(Get-RunLines $H | Where-Object { $_ -match ('movers: tracking ' + [regex]::Escape($c) + '/') })
            $p1 = @(Get-RunLines $P | Where-Object { $_ -match ('movers: tracking ' + [regex]::Escape($c) + '/') })
            if ($h1.Count -gt 0 -and $p1.Count -gt 0) {
                $idH = ''; $idP = ''
                if ($h1[-1] -match 'id=([0-9A-Fa-f]{8})') { $idH = $Matches[1] }
                if ($p1[-1] -match 'id=([0-9A-Fa-f]{8})') { $idP = $Matches[1] }
                $same = ($idH -ne '' -and $idH -eq $idP)
                $script:verdicts.Add([pscustomobject]@{ Step='identity'; Inst=$H; What=("the same lift has the same stable id on both machines (" + $c + ")"); Ok=$same; Line=("host=$idH peer=$idP") }) | Out-Null
                Note ("  {0} same stable id for {1}: host={2} peer={3}" -f $(if ($same) { 'PASS' } else { '--  ' }), $c, $idH, $idP)
            }
        }
    }

    if ($liftName -or $true) {
        # -------------------------------------------------------------------
        # 5. stand ON it (host), stand BESIDE it and watch (peer), then press it
        # -------------------------------------------------------------------
        # The scanned lift comes with its own measured point. `Mausoleum_Lift_00_BP2` is added
        # because its authored placement is fixed and it is a real ELEVATOR (11 placements in
        # the census), and `IntroDungerLift_BP`'s own Interact resolves but starts nothing --
        # measured in run 3 of this script.
        $cands = New-Object System.Collections.Generic.List[object]
        if ($liftName) { $cands.Add([pscustomobject]@{ Name=$liftName; X=$liftX; Y=$liftY; Z=$liftZ }) }
        $cands.Add([pscustomobject]@{ Name='Mausoleum_Lift_00_BP2'; X=7758; Y=11111; Z=716 })

        $moved = $false
        foreach ($c in $cands) {
            if ($moved) { break }
            Step ("RIDE -- {0}" -f $c.Name) ("host stands ON it at {0},{1},{2}; peer stands BESIDE it and watches" -f $c.X, $c.Y, $c.Z)
            $mRide = Get-Mark $H
            $mPeer = Get-Mark $P
            Set-Control $H 'qd.tp' ("{0},{1},{2},0" -f $c.X, $c.Y, ($c.Z + 120))
            Start-Sleep -Seconds 4
            Set-Control $P 'qd.tp' ('near:' + $c.Name)     # the rig's safe "beside it" point
            Start-Sleep -Seconds 5
            Show-Log $H 'dev teleport|platforms: ride local' 6
            Check 'ride' $H 'platforms: ride local place' 20 ("the host has a place ON " + $c.Name) $mRide | Out-Null

            Step ("TRIGGER -- press {0}" -f $c.Name) 'the lift owns the verb; nothing about it is re-implemented'
            Set-Control $H 'qd.act' ('near:' + $c.Name)
            Start-Sleep -Seconds 3
            Set-Control $H 'qd.act' ('near:' + $c.Name + '#2')
            Start-Sleep -Seconds 12
            Show-Log $H 'dev interact: resolved|platforms: ride local' 10
            $moved = $false
            if ((@(Get-Since $H $mRide | Where-Object { $_ -match 'platforms: ride local place .*moving' })).Count -gt 0) {
                $moved = $true
            }
            Check 'ride' $H 'platforms: ride local place .*moving' 20 ("the platform is MOVING under the rider (" + $c.Name + ")") $mRide | Out-Null
            Check 'ride' $P 'platforms: partner is on a platform' 20 'the partner draws the rider on its own copy' $mPeer | Out-Null
            Show-Log $P 'platforms: partner' 4
            foreach ($i in 1, 2) {
                & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'capture-hidden.ps1') `
                    -ProcessId $script:pids[$i] -Out (Join-Path $outDir ("ride-{0}-inst{1}.png" -f ($c.Name -replace '[^A-Za-z0-9]','_'), $i)) 2>&1 | Out-Null
                Note ("  screenshot: " + (Join-Path $outDir ("ride-{0}-inst{1}.png" -f ($c.Name -replace '[^A-Za-z0-9]','_'), $i)))
            }
        }
    }

    Step 'LEVER -- the peer presses the elevator lever while the host rides' 'a plate/lever on the OTHER machine starts the platform: the same mover lane carries it, and the mover lane needs no lever logic at all'
    $mLevH = Get-Mark $H
    $mLevP = Get-Mark $P
    Set-Control $H 'qd.tp' '73,-1587,-1274,0'                 # the host stands ON IntroDungerLift_BP
    Start-Sleep -Seconds 4
    Set-Control $P 'qd.tp' 'near:AQR_ElevatorLever11'          # the peer stands at the call lever
    Start-Sleep -Seconds 5
    Set-Control $P 'qd.act' 'near:AQR_ElevatorLever11'
    Start-Sleep -Seconds 3
    Set-Control $P 'qd.act' 'near:AQR_ElevatorLever11#2'
    Start-Sleep -Seconds 15
    Show-Log $H 'platforms: ride local' 6
    Show-Log $P 'dev interact|dev teleport|platforms: partner' 6
    Check 'ride' $H 'platforms: ride local place .*moving' 25 'the platform MOVES under the host while the PEER pressed the lever' $mLevH | Out-Null
    Check 'ride' $P 'platforms: partner is on a platform' 25 'the peer still draws the rider while it moves' $mLevP | Out-Null
    foreach ($i in 1, 2) {
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'capture-hidden.ps1') `
            -ProcessId $script:pids[$i] -Out (Join-Path $outDir "lever-inst$i.png") 2>&1 | Out-Null
        Note ("  screenshot: " + (Join-Path $outDir "lever-inst$i.png"))
    }

    Step 'SESSION SURVIVAL' 'both instances alive, no FAULT'
    foreach ($i in 1, 2) {
        Note ("  [{0}] process alive={1}" -f $i, [bool](Get-Process -Id $script:pids[$i] -ErrorAction SilentlyContinue))
        $crash = Get-Matches $i 'FAULT|EXCEPTION_ACCESS_VIOLATION'
        Note ("  [{0}] FAULT lines: {1}" -f $i, $crash.Count)
    }
}

Note ''
Note '=== VERDICTS ==='
foreach ($v in $script:verdicts) {
    Note ("  {0,-8} inst{1}  {2,-6}  {3}" -f $v.Step, $v.Inst, $(if ($v.Ok) { 'PASS' } else { 'FAIL' }), $v.What)
    if ($v.Line -and $v.Line -ne 'INSTANCE DIED') { Note ("           {0}" -f $v.Line.Trim()) }
}

# ---------------------------------------------------------------------------
# 6. teardown
# ---------------------------------------------------------------------------
foreach ($i in 1, 2) {
    if ($script:pids.ContainsKey($i)) { Stop-Process -Id $script:pids[$i] -Force -ErrorAction SilentlyContinue }
}
Stop-Process -Id $rp.Id -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1
Say 'stopped both instances and the relay'

$script:lines | Set-Content -LiteralPath $Report
Say "report: $Report"
