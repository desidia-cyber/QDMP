# session-s32-proof.ps1 -- round 2 of the `platforms` and `combat-vfx` lanes: prove the two
# things round 1 could not.
#
# PLATFORMS.  Round 1 proved the rider lane around a STATIONARY lift.  The missing half is a
# lift that actually travels: `Elevator_Base` (the parent of every lever elevator) moves its
# own `ElevatorPlatformRoot` SCENE COMPONENT, not the actor, so the mover lane's actor-root
# read reports a lift that never moves.  `qdplatforms.h` now follows an ELEMENT (root or a
# named child component) and carries its index in the spare bits of the `MOVERS` flags byte.
# This script therefore drives each candidate lift through the game's OWN start functions
# (`StartElevatorTowardsEnd`, `Activate`, the lever's `TryPullLever`, plain `Interact`) and
# watches for `platforms: mover id=... now follows element N`.
#
# COMBAT-VFX.  Round 1 could only equip the starting sword.  `QDCOOP_GIVE` is the game's own
# `AddItemToInventory`/`AutoEquipItem` path and needs no key input, so this run grants one
# weapon of each family at launch, then presses the weapon-slot keys and the attack button
# (`send-input.ps1 -Button left`) and reads what the PARTNER's machine says it is drawing.
#
# ONE PROCESS, like every other session script: a launched game does not survive the command
# that started it.
[CmdletBinding()]
param(
    [ValidateSet('platforms', 'combat', 'all')]
    [string]$Phase = 'all',
    [int]$BootSeconds = 240,
    [string]$Report = ''
)

$ErrorActionPreference = 'Stop'
$here    = $PSScriptRoot
$repo    = 'G:\Freespace\Queens_Domain\.worktrees\sync-fix'
$gameDir = 'G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64'
$relay   = Join-Path $repo 'mod\rust\target\release\qd-relay.exe'
$rigRoot = 'G:\Freespace\Queens_Domain\_test'
$outDir  = Join-Path $rigRoot 's32-proof'
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
# 0. the LIVE lock is the contract
# ---------------------------------------------------------------------------
$lockDir = Join-Path $rigRoot 'locks\live'
$owner = ''
if (Test-Path (Join-Path $lockDir 'owner.txt')) {
    $owner = (Get-Content (Join-Path $lockDir 'owner.txt') -ErrorAction SilentlyContinue | Select-Object -First 1)
}
if ($owner -ne 'live-pc') {
    Say "REFUSED: the LIVE lock is not mine (owner='$owner'). Nothing was started."
    exit 2
}

function Get-RigPaths([int]$i) {
    $root = Join-Path $rigRoot ("inst{0}" -f $i)
    [pscustomobject]@{
        Index = $i; Root = $root; Suffix = "qd$i"
        Log = Join-Path $gameDir ("qdcoop-{0}.log" -f "qd$i")
        Pid = "$root.pid"
    }
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
function Show-Log([int]$i, [string]$pattern, [int]$n) {
    $a = @(Get-RunLines $i | Where-Object { $_ -match $pattern })
    if ($a.Count -eq 0) { Note "  [$i] (no line matches '$pattern')"; return }
    $a | Select-Object -Last $n | ForEach-Object { Note "  [$i] $_" }
}
function Step([string]$t, [string]$b) { Note ''; Note "### $t"; if ($b) { Note $b } }

$script:verdicts = New-Object System.Collections.Generic.List[object]
function Verdict([string]$step, [int]$i, [string]$what, [bool]$ok, [string]$line) {
    $script:verdicts.Add([pscustomobject]@{ Step=$step; Inst=$i; What=$what; Ok=$ok; Line=$line }) | Out-Null
    Note ("  {0} [$i] {1}" -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $what)
    if ($line) { Note ("           {0}" -f $line.Trim()) }
}

function Press-Key([int]$i, [int]$vk = 0x0D) {
    if (-not (Test-Alive $i)) { return }
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'send-input.ps1') `
        -ProcessId $script:pids[$i] -Vk $vk 2>&1 | Out-Null
}
function Shoot([int]$i, [string]$tag) {
    if (-not (Test-Alive $i)) { return }
    $p = Join-Path $outDir ("{0}-inst{1}.png" -f $tag, $i)
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'capture-hidden.ps1') `
        -ProcessId $script:pids[$i] -Out $p 2>&1 | Out-Null
    Note ("  screenshot: " + $p)
}

# One weapon of each family the brief asks for.  Every path is verified present in
# `analysis/pak-files.txt`; `QDCOOP_GIVE` runs the game's own GiveItem + AutoEquipItem.
$weapons = @(
    '/Game/Items/Weapons/Axe/SimpleAxe_Item.SimpleAxe_Item'                                    # axe
    '/Game/Items/Weapons/CyberHammer/CyberHammer_Item.CyberHammer_Item'                        # hammer
    '/Game/Items/Weapons/BasicSpear/BasicSpear_Item.BasicSpear_Item'                           # spear / lance
    '/Game/Items/Weapons/WodasoGreatSword/WodasoGreatSword_Item.WodasoGreatSword_Item'         # two-handed
    '/Game/Items/Weapons/HeavyRifle/HeavyRifle_Item.HeavyRifle_Item'                          # gun (skeletal)
    '/Game/Items/Weapons/Daggers/Default/DefaultDagger.DefaultDagger'                         # dagger / off-hand
    '/Game/Items/Weapons/FinalClaw/FinalClaw_Item.FinalClaw_Item'                              # claw
    '/Game/Items/Weapons/RayneScythe/RayneScythe_Item.RayneScythe_Item'                        # scythe
)

# ---------------------------------------------------------------------------
# 1. teardown + deploy
# ---------------------------------------------------------------------------
Say "scenario start: phase=$Phase"
Get-Process -Name 'QueensDomain-Win64-Shipping', 'qd-relay' -ErrorAction SilentlyContinue | ForEach-Object {
    Say "  stopping leftover $($_.ProcessName) pid=$($_.Id)"
    Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue
}
Start-Sleep -Seconds 2

$built = Join-Path $repo 'mod\native\build\winmm.dll'
if (-not (Test-Path -LiteralPath $built)) { Say "REFUSED: $built missing"; exit 2 }
Copy-Item -LiteralPath $built -Destination (Join-Path $gameDir 'winmm.dll') -Force
Say "installed winmm.dll ($((Get-Item $built).Length) bytes)"

$ini = Join-Path $gameDir 'qdcoop.ini'
@(
    '[Local]'; 'Enabled=1'; 'LogLevel=4'; ''
    '[Net]'; 'RelayHost=127.0.0.1'; 'RelayPort=47800'; ''
    '[Rules]'
    'FlagSync=1'; 'MoverSync=1'; 'PlatformRide=1'; 'PuppetSync=1'; 'PlayerBody=1'
    'CombatVfx=1'; 'EnemySync=0'; 'StreamUnion=1'; 'TravelSync=1'
    'SuppressMenuPause=0'; 'HostAuthoritative=1'; ''
    '[Tuning]'; 'PlayerHz=30'; 'EnemyRowsPerTick=20'
) | Set-Content -LiteralPath $ini
Say "wrote $ini (MoverSync=1 PlatformRide=1 PuppetSync=1 PlayerBody=1 CombatVfx=1 LogLevel=4)"

foreach ($i in 1, 2) {
    foreach ($c in 'qd.tp','qd.act','qd.hard','qd.cheat','qd.use','qd.selfhp') { Set-Control $i $c $null }
    Set-Control $i 'qd.scan' '1'
    Set-Control $i 'qd.god' '400'
    if (Test-Path -LiteralPath $inst[$i].Pid) { Remove-Item -LiteralPath $inst[$i].Pid -ErrorAction SilentlyContinue }
}

# ---------------------------------------------------------------------------
# 2. relay + instances  (QDCOOP_GIVE is inherited by Start-Process)
# ---------------------------------------------------------------------------
if (-not (Test-Path -LiteralPath $relay)) { Say "REFUSED: relay not built at $relay"; exit 2 }
$relayLog = Join-Path $outDir 'relay.log'
Remove-Item -LiteralPath $relayLog -ErrorAction SilentlyContinue
$rp = Start-Process -FilePath $relay -ArgumentList @('--bind', '127.0.0.1:47800') `
        -RedirectStandardOutput $relayLog -RedirectStandardError (Join-Path $outDir 'relay.err') `
        -WindowStyle Hidden -PassThru
Say "relay pid=$($rp.Id)"
Start-Sleep -Seconds 2

$env:QDCOOP_GIVE = ($weapons -join ';')
Say "QDCOOP_GIVE = $($env:QDCOOP_GIVE)"

$script:pids = @{}
foreach ($i in 1, 2) {
    $launch = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'run-hidden.ps1') `
                -UserDir $inst[$i].Root -Suffix $inst[$i].Suffix 2>&1
    $gp = ($launch | Where-Object { $_ -match '^\d+$' } | Select-Object -Last 1)
    if (-not $gp) { Say "LAUNCH FAILED for instance $i"; $launch | ForEach-Object { Note $_ } }
    else { $script:pids[$i] = [int]$gp; Set-Content -LiteralPath $inst[$i].Pid -Value $gp; Say "inst$i pid=$gp" }
    Start-Sleep -Seconds 3
}
if (-not $script:pids[1] -or -not $script:pids[2]) { Say 'ABORT: both instances are required'; }
Remove-Item Env:\QDCOOP_GIVE -ErrorAction SilentlyContinue

# ---------------------------------------------------------------------------
# 3. session + gameplay
# ---------------------------------------------------------------------------
Say "waiting for a session (up to $BootSeconds s)"
$deadline = (Get-Date).AddSeconds($BootSeconds)
$roles = @{}
while ((Get-Date) -lt $deadline) {
    foreach ($i in 1, 2) {
        if ($roles.ContainsKey($i) -or -not $script:pids.ContainsKey($i)) { continue }
        $m = @(Get-RunLines $i | Where-Object { $_ -match 'session established' })
        if ($m.Count -gt 0) { $roles[$i] = if ($m[-1] -match 'role=HOST') { 'HOST' } else { 'PEER' } }
    }
    if ($roles.Count -eq 2) { break }
    Start-Sleep -Seconds 1
}
foreach ($i in 1, 2) {
    if ($roles.ContainsKey($i)) { Say "inst$i role=$($roles[$i])" } else { Say "inst$i NEVER ESTABLISHED" }
}
$hostI = @($roles.Keys | Where-Object { $roles[$_] -eq 'HOST' } | Select-Object -First 1)
$peerI = @($roles.Keys | Where-Object { $roles[$_] -eq 'PEER' } | Select-Object -First 1)
if ($hostI.Count -eq 0 -or $peerI.Count -eq 0) { Say 'ABORT: no host/peer pair'; $hostI = @(1); $peerI = @(2) }
$H = [int]$hostI[0]; $P = [int]$peerI[0]
Say "host=inst$H peer=inst$P"

foreach ($i in 1, 2) {
    $ok = $false
    $dl = (Get-Date).AddSeconds($BootSeconds)
    $pressed = 0
    while ((Get-Date) -lt $dl) {
        if ((@(Get-RunLines $i | Where-Object { $_ -match 'scan: health|scan: local at|dev autoload: in game' })).Count -gt 0) { $ok = $true; break }
        if (((Get-Date) -lt $dl) -and ($pressed -lt 12)) {
            Press-Key $i 0x0D
            Start-Sleep -Milliseconds 700
            Press-Key $i 0x20
            ++$pressed
        }
        Start-Sleep -Seconds 6
    }
    Say "inst${i} gameplay=$ok (presses=$pressed)"
}

# ---------------------------------------------------------------------------
# 4. PLATFORMS: make a lift travel
# ---------------------------------------------------------------------------
if ($Phase -in 'platforms', 'all') {
    Step 'PLATFORMS -- classify' 'every tracked mover, both machines'
    foreach ($i in 1, 2) {
        Note "  --- inst$i ---"
        Show-Log $i 'platforms: mover class filter|platforms: element |movers: tracking' 20
    }

    Step 'PLATFORMS -- reach a level with a lift'
    $areas = @('400_MAUS_entrance', 'Lift_00', 'OW_Lift_00', '420_MAUS_mid', '780_SZ_Depths',
               '600_Castle', 'Lab')
    $found = $false
    $nonce = 0
    foreach ($area in $areas) {
        if ($found) { break }
        $nonce++
        $mH = Get-Mark $H
        Set-Control $H 'qd.hard' ($area + '#' + $nonce)
        Say "  hard[$H] -> $area"
        Start-Sleep -Seconds 12
        $m = @(Get-Since $H $mH | Where-Object { $_ -match ('rig: hard lever -> ' + [regex]::Escape($area)) })
        if ($m.Count -gt 0) { Note "  [$H] $($m[-1])" }
        Start-Sleep -Seconds 10
        if ((@(Get-RunLines $H | Where-Object {
                ($_ -match 'scan: loose (lift|elevator|platform|tram|mover) ') -and
                ($_ -notmatch 'none in this world') })).Count -gt 0) { $found = $true }
    }
    Note "  a lift was seen: $found"

    Step 'PLATFORMS -- candidate lifts'
    foreach ($i in 1, 2) { Note "  --- inst$i ---"; Show-Log $i 'scan: loose (lift|elevator|platform|tram|mover) |movers: tracking' 16 }

    # The candidates the analysis corpus names, in the order most likely to TRAVEL: the
    # lever-driven OW lift (its BP exports StartElevatorTowardsEnd), the TimedLift (a timer
    # starts it), then the Mausoleum family that round 1 rode at 7758,11111,716.
    $cands = @(
        [pscustomobject]@{ Name='OW_Lift_00_BP';    X=$null; Y=$null; Z=$null }
        [pscustomobject]@{ Name='OW_TimedLift_00_BP'; X=$null; Y=$null; Z=$null }
        [pscustomobject]@{ Name='Mausoleum_Lift_00_BP2'; X=7758; Y=11111; Z=716 }
    )
    $travelled = $false
    foreach ($c in $cands) {
        if ($travelled) { break }
        # Learn the actor's own coordinates from the game's own resolver.
        $mLearn = Get-Mark $H
        Set-Control $H 'qd.act' ('near:' + $c.Name + '#l')
        Start-Sleep -Seconds 4
        $x = $c.X; $y = $c.Y; $z = $c.Z
        $res = @(Get-Since $H $mLearn | Where-Object { $_ -match 'dev interact: resolved' })
        if ($res.Count -gt 0) {
            Note "  [$H] $($res[-1])"
            if ($res[-1] -match ' at (-?[0-9.]+),(-?[0-9.]+),(-?[0-9.]+)') {
                $x = [double]$Matches[1]; $y = [double]$Matches[2]; $z = [double]$Matches[3]
            }
        }
        if ($null -eq $x) { Note "  $($c.Name): not in this world, skipping"; continue }
        Step ("PLATFORMS -- ride " + $c.Name) ("stand ON it at {0},{1},{2}" -f $x, $y, ($z + 120))

        $mRide = Get-Mark $H
        $mPeer = Get-Mark $P
        Set-Control $H 'qd.tp' ("{0},{1},{2},0" -f $x, $y, ($z + 120))
        Start-Sleep -Seconds 4
        Set-Control $P 'qd.tp' ('near:' + $c.Name)
        Start-Sleep -Seconds 5
        Show-Log $H 'dev teleport|platforms: ride local' 5

        # The game's OWN start functions, in order of how much of the game they use.
        foreach ($v in @('Interact', 'StartElevatorTowardsEnd', 'Activate', 'GoToNextNode')) {
            if ($travelled) { break }
            $mV = Get-Mark $H
            Set-Control $H 'qd.act' ('near:' + $c.Name + '!' + $v + '#r')
            Start-Sleep -Seconds 3
            $r = @(Get-Since $H $mV | Where-Object { $_ -match 'forced verb' })
            if ($r.Count -gt 0) { Note "  [$H] $($r[-1])" }
            # Watch for the lane itself saying the platform is travelling.
            $hit = Wait-Since $H 'platforms: mover id=\S+ now follows element|platforms: ride local place .*moving' $mRide 14
            if ($hit.Count -gt 0) { $travelled = $true }
        }
        # Mid-travel evidence: three shots a second apart, both machines.
        if ($travelled) {
            for ($k = 1; $k -le 3; ++$k) {
                Shoot $H ("travel-" + ($c.Name -replace '[^A-Za-z0-9]','_') + "-a$k")
                Shoot $P ("travel-" + ($c.Name -replace '[^A-Za-z0-9]','_') + "-b$k")
                Start-Sleep -Seconds 1
            }
        }
        Show-Log $H 'platforms: mover id=|platforms: ride local|platforms: element' 12
        Show-Log $P 'platforms: partner|platforms: mover id=' 8
        Verdict 'platforms' $H ("the platform on " + $c.Name + " TRAVELS under the rider") $travelled `
                (($(Get-Since $H $mRide) | Where-Object { $_ -match 'now follows element|place .*moving' } | Select-Object -Last 1))
        $peerSaw = @(Get-Since $P $mPeer | Where-Object { $_ -match 'platforms: partner is on a platform|following the OWNER on element' })
        Verdict 'platforms' $P 'the PEER draws the platform/rider on its own copy' ($peerSaw.Count -gt 0) ($peerSaw | Select-Object -Last 1)
    }
    if (-not $travelled) { Note '  NO candidate travelled: no lift on this rig starts from a scripted call.' }
}

# ---------------------------------------------------------------------------
# 5. COMBAT-VFX: each weapon family in the partner's hand, and its attack
# ---------------------------------------------------------------------------
if ($Phase -in 'combat', 'all') {
    Step 'COMBAT -- what was granted' 'the game own AddItemToInventory + AutoEquipItem path'
    Start-Sleep -Seconds 6
    foreach ($i in 1, 2) {
        Note "  --- inst$i ---"
        Show-Log $i 'dev give:|equip scan|actor held|local equipment rev' 18
    }

    Step 'COMBAT -- walk the weapon slots and attack with each'
    # Both machines act; each watches the other.  Slot keys are 1..4 (the game's own binds).
    foreach ($slot in 1, 2, 3, 4) {
        $vk = 0x30 + $slot
        Press-Key $H $vk
        Start-Sleep -Milliseconds 900
        Press-Key $P $vk
        Start-Sleep -Seconds 2
        Note "  --- slot $slot ---"
        Show-Log $H 'actor held|local equipment rev' 3
        Show-Log $P 'peer equipment rev|body: hand R now holds' 3
        # Attack: a held left button is a charged attack, a short one a swing.
        $mAtk = Get-Mark $H
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'send-input.ps1') `
            -ProcessId $script:pids[$H] -Button left -ButtonHoldMs 120 2>&1 | Out-Null
        Start-Sleep -Seconds 2
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'send-input.ps1') `
            -ProcessId $script:pids[$H] -Button left -ButtonHoldMs 900 2>&1 | Out-Null
        Start-Sleep -Seconds 3
        Show-Log $H 'local montage|local action' 6
        Show-Log $P 'peer action|body: swing variant|effect: system|effect: .* shown' 6
        Shoot $H ("wep$slot-a"); Shoot $P ("wep$slot-b")
        $eqH = Get-Mark $H
    }

    Step 'COMBAT -- a projectile and its impact'
    $mFx = Get-Mark $H
    # The game's own FireDagger path (the only projectile origin the rig can drive), plus
    # whatever the left click fired above.
    Set-Control $H 'qd.cheat' 'GiveAllWeapons'
    Start-Sleep -Seconds 3
    Show-Log $H 'rig: cheat|dev give' 4
    Show-Log $H 'effect: local|impact system|actor projectile' 10
    Show-Log $P 'effect: .* shown|effect: system' 8
    Shoot $H 'fx-a'; Shoot $P 'fx-b'
}

Step 'SESSION SURVIVAL' 'both instances alive, no FAULT'
foreach ($i in 1, 2) {
    Note ("  [{0}] alive={1}" -f $i, [bool](Get-Process -Id $script:pids[$i] -ErrorAction SilentlyContinue))
    $crash = @(Get-RunLines $i | Where-Object { $_ -match 'FAULT|EXCEPTION_ACCESS_VIOLATION' })
    Note ("  [{0}] FAULT lines: {1}" -f $i, $crash.Count)
    $crash | Select-Object -Last 3 | ForEach-Object { Note "     $_" }
}

Note ''
Note '=== VERDICTS ==='
foreach ($v in $script:verdicts) {
    Note ("  {0,-10} inst{1}  {2,-4}  {3}" -f $v.Step, $v.Inst, $(if ($v.Ok) { 'PASS' } else { 'FAIL' }), $v.What)
    if ($v.Line) { Note ("           " + $v.Line.Trim()) }
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
