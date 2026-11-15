# session-s32b.ps1 -- run 2: the proofs run 1 could not reach, with the rig bugs fixed.
#
# WHAT RUN 1 (session-s32-proof.ps1) ESTABLISHED, SO THIS SCRIPT CAN BE SHORT
# -------------------------------------------------------------------------
# * the ELEMENT enumeration works live: `platforms: element 2/5 of id=38BFA7F4 is
#   ElevatorPlatformRoot` -- this game's elevators move a CHILD SCENE COMPONENT, which is
#   exactly the read the old actor-root lane could not see.
# * the host BOARDS `Mausoleum_Lift_00_BP2` standing on top of it (`ride local place
#   -21.9,-148.3,121.6`, i.e. 121.6 cm above its root).
# * the game's own `StartElevatorTowardsEnd` and `Activate` both return `ran=yes` on that
#   lift and it does not travel.  So no scripted trigger on this rig starts a lift, and the
#   moving half has to be driven to be measured.
# * `QDCOOP_GIVE` grants and AutoEquipItem FILLS THE EMPTY HAND: eight weapons granted gave
#   `local equipment rev 2: 2 item(s)`, the DAGGER in the left hand and RustySword still in
#   the right.  Reaching the other families needs `EquipItem(Item, Slot)` -- the `qd.equip`
#   lever added for this run.
#
# Therefore: placement is hard-coded from run 1's own measurement, the platform is made to
# travel by the `qd.mover` instrument (which moves ONE element of ONE mover; every row, the
# easing, the element index and the rider's place are the lane's own code), and the weapon
# walk is `qd.equip` per weapon.
[CmdletBinding()]
param([int]$BootSeconds = 240)
$ErrorActionPreference = 'Stop'
$here    = $PSScriptRoot
$repo    = 'G:\Freespace\Queens_Domain\.worktrees\sync-fix'
$gameDir = 'G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64'
$relay   = Join-Path $repo 'mod\rust\target\release\qd-relay.exe'
$rigRoot = 'G:\Freespace\Queens_Domain\_test'
$outDir  = Join-Path $rigRoot 's32b-proof'
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force -Path $outDir | Out-Null }
$Report = Join-Path $outDir 'report.txt'

$script:lines = New-Object System.Collections.Generic.List[string]
function Say($m) { $s = "[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $m; Write-Output $s; $script:lines.Add($s) | Out-Null }
function Note($m) { Write-Output $m; $script:lines.Add($m) | Out-Null }

$lockDir = Join-Path $rigRoot 'locks\live'
$owner = ''
if (Test-Path (Join-Path $lockDir 'owner.txt')) {
    $owner = (Get-Content (Join-Path $lockDir 'owner.txt') -ErrorAction SilentlyContinue | Select-Object -First 1)
}
if ($owner -ne 'live-pc') { Say "REFUSED: the LIVE lock is not mine (owner='$owner')."; exit 2 }

function Get-RigPaths([int]$i) {
    $root = Join-Path $rigRoot ("inst{0}" -f $i)
    [pscustomobject]@{ Index=$i; Root=$root; Suffix="qd$i"
                       Log = Join-Path $gameDir ("qdcoop-{0}.log" -f "qd$i"); Pid="$root.pid" }
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
function Wait-Since([int]$i, [string]$pattern, [int]$mark, [int]$seconds) {
    $deadline = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $deadline) {
        $m = @(Get-Since $i $mark | Where-Object { $_ -match $pattern })
        if ($m.Count -gt 0) { return $m }
        if (-not (Test-Alive $i)) { return @() }
        Start-Sleep -Milliseconds 400
    }
    return @()
}
function Set-Control([int]$i, [string]$leaf, [string]$value) {
    $p = Join-Path $inst[$i].Root $leaf
    if ($null -eq $value) { Remove-Item -LiteralPath $p -ErrorAction SilentlyContinue }
    else { Set-Content -LiteralPath $p -Value $value -NoNewline }
}
# Every match, trimmed to one readable line: the mod's log records can share a physical line.
function Show-Log([int]$i, [string]$pattern, [int]$n) {
    $a = @(Get-RunLines $i | Where-Object { $_ -match $pattern } | Select-Object -Last $n)
    if ($a.Count -eq 0) { Note "  [$i] (no line matches '$pattern')"; return }
    foreach ($l in $a) {
        $t = $l.Trim()
        if ($t.Length -gt 300) {
            $mm = [regex]::Matches($t, $pattern)
            $t = "[...]" + $t.Substring([Math]::Max(0, $mm[$mm.Count-1].Index - 40), [Math]::Min(260, $t.Length - [Math]::Max(0, $mm[$mm.Count-1].Index - 40)))
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
    if ($line) { Note ("           " + $line.Trim()) }
}

$weapons = @(
    '/Game/Items/Weapons/Axe/SimpleAxe_Item.SimpleAxe_Item'
    '/Game/Items/Weapons/CyberHammer/CyberHammer_Item.CyberHammer_Item'
    '/Game/Items/Weapons/BasicSpear/BasicSpear_Item.BasicSpear_Item'
    '/Game/Items/Weapons/WodasoGreatSword/WodasoGreatSword_Item.WodasoGreatSword_Item'
    '/Game/Items/Weapons/HeavyRifle/HeavyRifle_Item.HeavyRifle_Item'
    '/Game/Items/Weapons/Daggers/Default/DefaultDagger.DefaultDagger'
    '/Game/Items/Weapons/FinalClaw/FinalClaw_Item.FinalClaw_Item'
    '/Game/Items/Weapons/RayneScythe/RayneScythe_Item.RayneScythe_Item'
)

Say 'scenario start: run 2 (proofs)'
Get-Process -Name 'QueensDomain-Win64-Shipping','qd-relay' -ErrorAction SilentlyContinue | ForEach-Object { Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue }
Start-Sleep -Seconds 2
$built = Join-Path $repo 'mod\native\build\winmm.dll'
Copy-Item -LiteralPath $built -Destination (Join-Path $gameDir 'winmm.dll') -Force
Say "installed winmm.dll ($((Get-Item $built).Length) bytes)"
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
    Set-Control $i 'qd.scan' '1'
    Set-Control $i 'qd.god' '400'
    if (Test-Path -LiteralPath $inst[$i].Pid) { Remove-Item -LiteralPath $inst[$i].Pid -ErrorAction SilentlyContinue }
}

$rp = Start-Process -FilePath $relay -ArgumentList @('--bind','127.0.0.1:47800') `
        -RedirectStandardOutput (Join-Path $outDir 'relay.log') -RedirectStandardError (Join-Path $outDir 'relay.err') `
        -WindowStyle Hidden -PassThru
Say "relay pid=$($rp.Id)"
Start-Sleep -Seconds 2
$env:QDCOOP_GIVE = ($weapons -join ';')
$script:pids = @{}
foreach ($i in 1, 2) {
    $launch = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'run-hidden.ps1') -UserDir $inst[$i].Root -Suffix $inst[$i].Suffix 2>&1
    $gp = ($launch | Where-Object { $_ -match '^\d+$' } | Select-Object -Last 1)
    if (-not $gp) { Say "LAUNCH FAILED for instance $i" } else { $script:pids[$i] = [int]$gp; Set-Content -LiteralPath $inst[$i].Pid -Value $gp; Say "inst$i pid=$gp" }
    Start-Sleep -Seconds 3
}
Remove-Item Env:\QDCOOP_GIVE -ErrorAction SilentlyContinue

Say "waiting for a session (up to $BootSeconds s)"
$deadline = (Get-Date).AddSeconds($BootSeconds); $roles = @{}
while ((Get-Date) -lt $deadline) {
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
    $dl = (Get-Date).AddSeconds($BootSeconds); $pr = 0
    while ((Get-Date) -lt $dl) {
        if ((@(Get-RunLines $i | Where-Object { $_ -match 'scan: health|scan: local at' })).Count -gt 0) { break }
        if ($pr -lt 12) { Press-Key $i 0x0D; Start-Sleep -Milliseconds 700; Press-Key $i 0x20; ++$pr }
        Start-Sleep -Seconds 6
    }
    Say "inst${i} in game"
}

# ===========================================================================
# PLATFORMS -- a lift that travels, both directions
# ===========================================================================
$LIFT = 'Mausoleum_Lift_00_BP2'
$LX = 7757; $LY = 11111; $LZ = 716          # measured in run 1 by the game's own resolver
Step 'PLATFORMS -- reach the Mausoleum lift'
Set-Control $H 'qd.hard' '400_MAUS_entrance#r2'
Start-Sleep -Seconds 16
Show-Log $H 'rig: hard lever ->|streamed=' 2

function Get-ElemIndex([int]$i, [string]$elemName) {
    $a = @(Get-RunLines $i | Where-Object { $_ -match ("platforms: element \d+/\d+ of id=\w+ is " + [regex]::Escape($elemName)) })
    if ($a.Count -eq 0) { return -1 }
    $m = [regex]::Matches(($a[-1]), ('platforms: element (\d+)/\d+ of id=\w+ is ' + [regex]::Escape($elemName)))
    if ($m.Count -eq 0) { return -1 }
    return [int]$m[$m.Count-1].Groups[1].Value
}

function Ride-Once([int]$rider, [int]$watch, [float]$dz, [int]$beat, [string]$tag) {
    $mR = Get-Mark $rider
    Set-Control $rider 'qd.tp' ("{0},{1},{2},0" -f $LX, $LY, ($LZ + 120))
    Start-Sleep -Seconds 4
    Set-Control $watch 'qd.tp' ('near:' + $LIFT)
    Start-Sleep -Seconds 4
    Show-Log $rider 'platforms: ride local' 5
    $board = Wait-Since $rider 'platforms: ride local place .*on id=' $mR 14
    Verdict 'platforms' $rider ("the rider is ON " + $LIFT) ($board.Count -gt 0) ($board | Select-Object -Last 1)

    # Which element is the platform?  Read it from the mover's own log, do not guess.
    $el = Get-ElemIndex $rider 'ElevatorPlatformRoot'
    Note "  elevator platform element on inst${rider}: $el"
    if ($el -lt 0) { $el = 1 }

    $mN = Get-Mark $rider
    $mW = Get-Mark $watch
    Set-Control $rider 'qd.mover' ("{0},{1},{2},{3}#{4}" -f $LIFT, $dz, $beat, $el, (Get-Random))
    Start-Sleep -Seconds 2
    # Mid-travel: shots from BOTH machines while it moves.
    for ($k = 1; $k -le 3; ++$k) { Shoot $rider ("$tag-a$k"); Shoot $watch ("$tag-b$k"); Start-Sleep -Milliseconds 700 }
    Start-Sleep -Seconds 4
    Show-Log $rider 'rig: mover nudge|rig: mover --|platforms: mover id=|platforms: ride local place' 8
    Show-Log $watch 'movers: applied|platforms: partner is on a platform|platforms: mover id=' 8

    $own = @(Get-Since $rider $mN | Where-Object { $_ -match 'rig: mover nudge' })
    Verdict 'platforms' $rider 'the OWNER drove the platform element' ($own.Count -gt 0) ($own | Select-Object -Last 1)
    $rows = @(Get-Since $watch $mW | Where-Object { $_ -match 'movers: applied \d+ owner row' })
    Verdict 'platforms' $watch 'the FOLLOWER applied the owner rows' ($rows.Count -gt 0) ($rows | Select-Object -Last 1)
    $rideMoving = @(Get-Since $rider $mN | Where-Object { $_ -match 'ride local place .*moving' })
    Verdict 'platforms' $rider 'the rider place is carried while the platform MOVES' ($rideMoving.Count -gt 0) ($rideMoving | Select-Object -Last 1)
    $peer = @(Get-Since $watch $mW | Where-Object { $_ -match 'platforms: partner is on a platform' })
    Verdict 'platforms' $watch 'the partner machine draws the rider on ITS copy' ($peer.Count -gt 0) ($peer | Select-Object -Last 1)
}

Step 'PLATFORMS -- forward: the host rides, the peer watches' 'the peer stands beside the lift'
Ride-Once $H $P 400 20 'fwd'

Step 'PLATFORMS -- reverse: the peer rides, the host watches' 'the host must leave so the peer owns the mover'
Set-Control $H 'qd.tp' '4106,-1744,140,0'
Set-Control $P 'qd.mover' $null
Start-Sleep -Seconds 5
Ride-Once $P $H -400 20 'rev'

# ===========================================================================
# COMBAT-VFX -- every family in the hand, and its attack on the partner
# ===========================================================================
Step 'COMBAT -- the weapon system, as the game itself sees it'
Set-Control $H 'qd.wsys' 'List#1'
Start-Sleep -Seconds 3
Set-Control $H 'qd.wsys' 'Init+Weapons#1'
Start-Sleep -Seconds 4
Show-Log $H 'rig: wsys|WeaponSystem|AllWeapons' 20

Step 'COMBAT -- one weapon of each family into the RIGHT hand (qd.equip, slot 0)'
$slot = 0
foreach ($w in $weapons) {
    $slot++
    $mE = Get-Mark $H
    Set-Control $H 'qd.equip' ("{0},0#{1}" -f $w, $slot)
    Start-Sleep -Seconds 3
    $eq = @(Get-Since $H $mE | Where-Object { $_ -match 'rig: equip' })
    $held = @(Get-RunLines $H | Where-Object { $_ -match 'actor held:|local equipment rev' } | Select-Object -Last 1)
    Note "  --- $([System.IO.Path]::GetFileName($w)) ---"
    Verdict 'combat' $H ("equip " + [System.IO.Path]::GetFileName($w)) ($eq.Count -gt 0) ($eq | Select-Object -Last 1)
    Show-Log $H 'actor held|local equipment rev' 2
    $mP = Get-Mark $P
    Start-Sleep -Milliseconds 400
    # Attack: a short click is a swing, a long hold a charged attack.
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'send-input.ps1') -ProcessId $script:pids[$H] -Button left -ButtonHoldMs 120 2>&1 | Out-Null
    Start-Sleep -Seconds 2
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'send-input.ps1') -ProcessId $script:pids[$H] -Button left -ButtonHoldMs 800 2>&1 | Out-Null
    Start-Sleep -Seconds 2
    Show-Log $H 'local montage|local action' 4
    Show-Log $P 'peer action|body: swing variant|effect: .*shown|effect: system' 6
    Shoot $H ("w$slot-a"); Shoot $P ("w$slot-b")
}

Step 'SESSION SURVIVAL'
foreach ($i in 1, 2) {
    Note ("  [{0}] alive={1}" -f $i, [bool](Get-Process -Id $script:pids[$i] -ErrorAction SilentlyContinue))
    $crash = @(Get-RunLines $i | Where-Object { $_ -match 'FAULT|EXCEPTION_ACCESS_VIOLATION' })
    Note ("  [{0}] FAULT lines: {1}" -f $i, $crash.Count)
}

Note ''
Note '=== VERDICTS ==='
foreach ($v in $script:verdicts) {
    Note ("  {0,-10} inst{1}  {2,-4}  {3}" -f $v.Step, $v.Inst, $(if ($v.Ok) { 'PASS' } else { 'FAIL' }), $v.What)
    if ($v.Line) { Note ("           " + ([string]$v.Line).Trim()) }
}

foreach ($i in 1, 2) { if ($script:pids.ContainsKey($i)) { Stop-Process -Id $script:pids[$i] -Force -ErrorAction SilentlyContinue } }
Stop-Process -Id $rp.Id -Force -ErrorAction SilentlyContinue
Say 'stopped both instances and the relay'
$script:lines | Set-Content -LiteralPath $Report
Say "report: $Report"
