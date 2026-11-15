# session-s32d.ps1 -- run 4: the MOVING platform, framed.
#
# Run 2 proved the lane (`now follows element N (Cube) -- the ACTOR does not move, this
# component does`, the rider's place carried while `(the platform is moving)`, the follower
# applying owner rows), but its screenshots were first-person frames that happened to be
# pointed at a wall, so they showed nothing about the platform.  This run frames the
# subject: the OBSERVER stands a fixed 520 cm from the lift along -X and faces +X (yaw 0),
# so the platform and the rider's body are in the middle of the frame, and the nudge is
# slowed from 133 cm/s to 66 cm/s so three screenshots fit inside one travel.
[CmdletBinding()]
param([int]$BootSeconds = 240)
$ErrorActionPreference = 'Stop'
$here    = $PSScriptRoot
$repo    = 'G:\Freespace\Queens_Domain\.worktrees\sync-fix'
$gameDir = 'G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64'
$relay   = Join-Path $repo 'mod\rust\target\release\qd-relay.exe'
$rigRoot = 'G:\Freespace\Queens_Domain\_test'
$outDir  = Join-Path $rigRoot 's32d-platforms'
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force -Path $outDir | Out-Null }
$script:lines = New-Object System.Collections.Generic.List[string]
function Say($m) { $s = "[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $m; Write-Output $s; $script:lines.Add($s) | Out-Null }
function Note($m) { Write-Output $m; $script:lines.Add($m) | Out-Null }

$lockDir = Join-Path $rigRoot 'locks\live'
$owner = ''
if (Test-Path (Join-Path $lockDir 'owner.txt')) { $owner = (Get-Content (Join-Path $lockDir 'owner.txt') -ErrorAction SilentlyContinue | Select-Object -First 1) }
if ($owner -ne 'live-pc') { Say "REFUSED: the LIVE lock is not mine (owner='$owner')."; exit 2 }

function Get-RigPaths([int]$i) {
    $root = Join-Path $rigRoot ("inst{0}" -f $i)
    [pscustomobject]@{ Index=$i; Root=$root; Suffix="qd$i"; Log = Join-Path $gameDir ("qdcoop-{0}.log" -f "qd$i"); Pid="$root.pid" }
}
$inst = @{ 1 = (Get-RigPaths 1); 2 = (Get-RigPaths 2) }
function Get-RunLines([int]$i) {
    $p = $inst[$i].Log
    if (-not (Test-Path -LiteralPath $p)) { return @() }
    $all = @(Get-Content -LiteralPath $p -ErrorAction SilentlyContinue)
    if ($all.Count -eq 0) { return @() }
    $start = 0
    for ($k = $all.Count - 1; $k -ge 0; $k--) { if ($all[$k] -match 'hooked FEngineLoop') { $start = $k; break } }
    return @($all[$start..($all.Count - 1)])
}
function Get-Mark([int]$i) { return (Get-RunLines $i).Count }
function Test-Alive([int]$i) { if (-not $script:pids.ContainsKey($i)) { return $false }; return [bool](Get-Process -Id $script:pids[$i] -ErrorAction SilentlyContinue) }
function Get-Since([int]$i, [int]$mark) { $a = @(Get-RunLines $i); if ($a.Count -le $mark) { return @() }; return @($a[$mark..($a.Count-1)]) }
function Set-Control([int]$i, [string]$leaf, [string]$value) {
    $p = Join-Path $inst[$i].Root $leaf
    if ($null -eq $value) { Remove-Item -LiteralPath $p -ErrorAction SilentlyContinue } else { Set-Content -LiteralPath $p -Value $value -NoNewline }
}
function Show-Log([int]$i, [string]$pattern, [int]$n) {
    $a = @(Get-RunLines $i | Where-Object { $_ -match $pattern } | Select-Object -Last $n)
    if ($a.Count -eq 0) { Note "  [$i] (no line matches '$pattern')"; return }
    foreach ($l in $a) {
        $t = $l.Trim()
        if ($t.Length -gt 300) {
            $mm = [regex]::Matches($t, $pattern); $ix = $mm[$mm.Count-1].Index
            $t = "[...]" + $t.Substring([Math]::Max(0,$ix-40), [Math]::Min(240, $t.Length-[Math]::Max(0,$ix-40)))
        }
        Note "  [$i] $t"
    }
}
function Step([string]$t, [string]$b) { Note ''; Note "### $t"; if ($b) { Note $b } }
function Press-Key([int]$i, [int]$vk = 0x0D) {
    if (-not (Test-Alive $i)) { return }
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'send-input.ps1') -ProcessId $script:pids[$i] -Vk $vk 2>&1 | Out-Null
}
function Shoot([int]$i, [string]$tag) {
    if (-not (Test-Alive $i)) { return }
    $p = Join-Path $outDir ("{0}-inst{1}.png" -f $tag, $i)
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'capture-hidden.ps1') -ProcessId $script:pids[$i] -Out $p 2>&1 | Out-Null
    Note ("  screenshot: " + $p)
}
$script:verdicts = New-Object System.Collections.Generic.List[object]
function Verdict([string]$step, [int]$i, [string]$what, [bool]$ok, [string]$line) {
    $script:verdicts.Add([pscustomobject]@{ Step=$step; Inst=$i; What=$what; Ok=$ok; Line=$line }) | Out-Null
    Note ("  {0} [{1}] {2}" -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $i, $what)
    if ($line) { Note ("           " + ([string]$line).Trim()) }
}

$LIFT = 'Mausoleum_Lift_00_BP2'
$LX = 7757; $LY = 11111; $LZ = 716          # measured by the game's own resolver (run 1)
$ONZ = $LZ + 120                            # where a capsule standing on it sits
$OBSX = $LX - 520                           # the observer's mark, facing +X = yaw 0

Say 'scenario start: run 4 (framed platform travel)'
Get-Process -Name 'QueensDomain-Win64-Shipping','qd-relay' -ErrorAction SilentlyContinue | ForEach-Object { Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue }
Start-Sleep -Seconds 2
Copy-Item -LiteralPath (Join-Path $repo 'mod\native\build\winmm.dll') -Destination (Join-Path $gameDir 'winmm.dll') -Force
@(
    '[Local]'; 'Enabled=1'; 'LogLevel=4'; ''
    '[Net]'; 'RelayHost=127.0.0.1'; 'RelayPort=47800'; ''
    '[Rules]'
    'FlagSync=1'; 'MoverSync=1'; 'PlatformRide=1'; 'PuppetSync=1'; 'PlayerBody=1'
    'CombatVfx=1'; 'EnemySync=0'; 'StreamUnion=1'; 'TravelSync=1'
    'SuppressMenuPause=0'; 'HostAuthoritative=1'; ''
    '[Tuning]'; 'PlayerHz=30'; 'EnemyRowsPerTick=20'
) | Set-Content -LiteralPath (Join-Path $gameDir 'qdcoop.ini')
foreach ($i in 1, 2) {
    foreach ($c in 'qd.tp','qd.act','qd.hard','qd.cheat','qd.use','qd.selfhp','qd.equip','qd.wsys','qd.mover') { Set-Control $i $c $null }
    Set-Control $i 'qd.scan' '1'; Set-Control $i 'qd.god' '400'
    if (Test-Path -LiteralPath $inst[$i].Pid) { Remove-Item -LiteralPath $inst[$i].Pid -ErrorAction SilentlyContinue }
}
$rp = Start-Process -FilePath $relay -ArgumentList @('--bind','127.0.0.1:47800') -RedirectStandardOutput (Join-Path $outDir 'relay.log') -RedirectStandardError (Join-Path $outDir 'relay.err') -WindowStyle Hidden -PassThru
Start-Sleep -Seconds 2
$script:pids = @{}
foreach ($i in 1, 2) {
    $launch = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'run-hidden.ps1') -UserDir $inst[$i].Root -Suffix $inst[$i].Suffix 2>&1
    $gp = ($launch | Where-Object { $_ -match '^\d+$' } | Select-Object -Last 1)
    if ($gp) { $script:pids[$i] = [int]$gp; Set-Content -LiteralPath $inst[$i].Pid -Value $gp; Say "inst$i pid=$gp" }
    Start-Sleep -Seconds 3
}
$dl = (Get-Date).AddSeconds($BootSeconds); $roles = @{}
while ((Get-Date) -lt $dl) {
    foreach ($i in 1, 2) {
        if ($roles.ContainsKey($i) -or -not $script:pids.ContainsKey($i)) { continue }
        $m = @(Get-RunLines $i | Where-Object { $_ -match 'session established' })
        if ($m.Count -gt 0) { $roles[$i] = if ($m[-1] -match 'role=HOST') { 'HOST' } else { 'PEER' } }
    }
    if ($roles.Count -eq 2) { break }
    Start-Sleep -Seconds 1
}
$hostI = @($roles.Keys | Where-Object { $roles[$_] -eq 'HOST' } | Select-Object -First 1)
$peerI = @($roles.Keys | Where-Object { $roles[$_] -eq 'PEER' } | Select-Object -First 1)
if ($hostI.Count -eq 0) { $hostI = @(1); $peerI = @(2) }
$H = [int]$hostI[0]; $P = [int]$peerI[0]
Say "host=inst$H peer=inst$P"
foreach ($i in 1, 2) {
    $d2 = (Get-Date).AddSeconds($BootSeconds); $pr = 0
    while ((Get-Date) -lt $d2) {
        if ((@(Get-RunLines $i | Where-Object { $_ -match 'scan: health|scan: local at' })).Count -gt 0) { break }
        if ($pr -lt 12) { Press-Key $i 0x0D; Start-Sleep -Milliseconds 700; Press-Key $i 0x20; ++$pr }
        Start-Sleep -Seconds 6
    }
    Say "inst${i} in game"
}
Step 'ENTER the Mausoleum'
Set-Control $H 'qd.hard' '400_MAUS_entrance#r4'
Start-Sleep -Seconds 16
Show-Log $H 'rig: hard lever ->' 1

function Get-ElemIndex([int]$i, [string]$elemName) {
    $a = @(Get-RunLines $i | Where-Object { $_ -match ("platforms: element \d+/\d+ of id=\w+ is " + [regex]::Escape($elemName)) })
    if ($a.Count -eq 0) { return -1 }
    $m = [regex]::Matches(($a[-1]), ('platforms: element (\d+)/\d+ of id=\w+ is ' + [regex]::Escape($elemName)))
    if ($m.Count -eq 0) { return -1 }
    return [int]$m[$m.Count-1].Groups[1].Value
}

function Travel-Framed([int]$rider, [int]$watch, [float]$dz, [string]$tag) {
    # The observer stands still and looks at the lift; the rider stands on it.
    Set-Control $watch 'qd.tp' ("{0},{1},{2},0" -f $OBSX, $LY, $ONZ)
    Start-Sleep -Seconds 3
    $mR = Get-Mark $rider
    Set-Control $rider 'qd.tp' ("{0},{1},{2},0" -f $LX, $LY, $ONZ)
    Start-Sleep -Seconds 5
    $board = @(Get-Since $rider $mR | Where-Object { $_ -match 'platforms: ride local place .*on id=' })
    Verdict 'platforms' $rider ("the rider is standing ON " + $LIFT) ($board.Count -gt 0) ($board | Select-Object -Last 1)
    Shoot $rider "$tag-rest-a"; Shoot $watch "$tag-rest-b"

    $el = Get-ElemIndex $rider 'ElevatorPlatformRoot'
    if ($el -lt 0) { $el = 1 }
    $mN = Get-Mark $rider
    $mW = Get-Mark $watch
    # 400 cm over 40 beats = 6 s at 66 cm/s.
    Set-Control $rider 'qd.mover' ("{0},{1},40,{2}#{3}" -f $LIFT, $dz, $el, (Get-Random))
    Start-Sleep -Milliseconds 900
    for ($k = 1; $k -le 3; ++$k) {
        Shoot $rider ("$tag-mid$k-a"); Shoot $watch ("$tag-mid$k-b")
        Start-Sleep -Milliseconds 300
    }
    Start-Sleep -Seconds 4
    Show-Log $rider 'rig: mover nudge|platforms: mover id=|platforms: ride local place' 10
    Show-Log $watch 'movers: applied|platforms: partner is on a platform|platforms: mover id=' 6

    $own = @(Get-Since $rider $mN | Where-Object { $_ -match 'rig: mover nudge' })
    Verdict 'platforms' $rider 'the OWNER drove the platform component' ($own.Count -gt 0) ($own | Select-Object -Last 1)
    $elem = @(Get-Since $rider $mN | Where-Object { $_ -match 'the ACTOR does not move, this component does' })
    Verdict 'platforms' $rider 'the lane named the COMPONENT as the thing that moves' ($elem.Count -gt 0) ($elem | Select-Object -Last 1)
    $rows = @(Get-Since $watch $mW | Where-Object { $_ -match 'movers: applied \d+ owner row' })
    Verdict 'platforms' $watch 'the FOLLOWER applied the owner rows' ($rows.Count -gt 0) ($rows | Select-Object -Last 1)
    $mv = @(Get-Since $rider $mN | Where-Object { $_ -match 'ride local place .*\(the platform is moving\)' })
    Verdict 'platforms' $rider 'the rider place is CARRIED while it moves' ($mv.Count -gt 0) ($mv | Select-Object -Last 1)
    $peersaw = @(Get-Since $watch $mW | Where-Object { $_ -match 'platforms: partner is on a platform' })
    if ($peersaw.Count -eq 0) {
        $peersaw = @(Get-RunLines $watch | Where-Object { $_ -match 'platforms: partner is on a platform' } | Select-Object -Last 1)
    }
    Verdict 'platforms' $watch 'the partner machine draws the rider on ITS copy' ($peersaw.Count -gt 0) ($peersaw | Select-Object -Last 1)

    # The no-jitter number: the spread of the rider's place while it rode.
    $places = @(Get-Since $rider $mR | Where-Object { $_ -match 'ride local place (-?[\d.]+),(-?[\d.]+),(-?[\d.]+)' } |
                ForEach-Object { $mm = [regex]::Match($_, 'ride local place (-?[\d.]+),(-?[\d.]+),(-?[\d.]+)'); [double]$mm.Groups[3].Value })
    if ($places.Count -ge 2) {
        $mn = ($places | Measure-Object -Minimum).Minimum
        $mx = ($places | Measure-Object -Maximum).Maximum
        Verdict 'platforms' $rider ("the rider's place on the platform held still while it travelled ({0:N1} cm spread over {1} samples)" -f ($mx-$mn), $places.Count) (($mx-$mn) -lt 40.0) ("z values: " + (($places | ForEach-Object { "{0:N1}" -f $_ }) -join " "))
    }
}

Step 'FORWARD -- the host rides, the peer watches, the peer looks at the lift'
Travel-Framed $H $P 400 'fwd'
Step 'REVERSE -- the peer rides, the host watches'
Set-Control $P 'qd.mover' $null
Travel-Framed $P $H -400 'rev'

Step 'SURVIVAL'
foreach ($i in 1, 2) {
    $c = @(Get-RunLines $i | Where-Object { $_ -match 'FAULT |EXCEPTION_ACCESS_VIOLATION' })
    Note ("  [{0}] alive={1} real faults={2}" -f $i, [bool](Get-Process -Id $script:pids[$i] -ErrorAction SilentlyContinue), $c.Count)
}
Note ''
Note '=== VERDICTS ==='
foreach ($v in $script:verdicts) {
    Note ("  {0,-10} inst{1}  {2,-4}  {3}" -f $v.Step, $v.Inst, $(if ($v.Ok) { 'PASS' } else { 'FAIL' }), $v.What)
    if ($v.Line) { Note ("           " + ([string]$v.Line).Trim()) }
}
foreach ($i in 1, 2) { if ($script:pids.ContainsKey($i)) { Stop-Process -Id $script:pids[$i] -Force -ErrorAction SilentlyContinue } }
Stop-Process -Id $rp.Id -Force -ErrorAction SilentlyContinue
$script:lines | Set-Content -LiteralPath (Join-Path $outDir 'report.txt')
Say ("report: " + (Join-Path $outDir 'report.txt'))
