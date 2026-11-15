# session-gun.ps1 -- SHORT, focused: get a GUN/BOW/CROSSBOW shot to spawn a projectile, so
# the widened projectile class gate (combat round 1/2's one unproven half) is exercised
# live, and the partner shows the shot and its impact.
#
# WHY THE EARLIER ATTEMPT WAS EMPTY
# --------------------------------
# `Attack` on the HeavyRifle played no montage and spawned nothing.  The rifle is an
# ENERGY weapon (`GunPowerLogic`, `FullCellsConsumed`, `ItemTags.EnergyWeapon`), so the
# game refuses to fire it with no charge.  A CROSSBOW is not energy-gated, and the rig can
# give it AND its bolt item.  This run therefore tries, in order:
#   1. the crossbow through the game's own attack input (the lever calls every compiled
#      `InpActEvt_Attack*` event, which is what actually plays the weapon's montage);
#   2. the rifle with energy cells granted, in case the cells are the only gate;
#   3. `UltraAttack` on the rifle, the keyboard-bound attack.
# What the partner must show for a PASS: `effect: local #N .../X_Projectile_C` on the
# sender and `effect: .../X_Projectile_C shown at ...` (plus an impact) on the partner.
[CmdletBinding()]
param([int]$BootSeconds = 300)
$ErrorActionPreference = 'Stop'
$here    = $PSScriptRoot
$repo    = 'G:\Freespace\Queens_Domain\.worktrees\sync-fix'
$gameDir = 'G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64'
$relay   = Join-Path $repo 'mod\rust\target\release\qd-relay.exe'
$rigRoot = 'G:\Freespace\Queens_Domain\_test'
$outDir  = Join-Path $rigRoot 'gun-proof'
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force -Path $outDir | Out-Null }
$Report = Join-Path $outDir 'report.txt'
$script:lines = New-Object System.Collections.Generic.List[string]
function Say($m) { $s = "[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $m; Write-Output $s; $script:lines.Add($s) | Out-Null }
function Note($m) { Write-Output $m; $script:lines.Add($m) | Out-Null }

$lockDir = Join-Path $rigRoot 'locks\live'
$owner = ''
if (Test-Path (Join-Path $lockDir 'owner.txt')) { $owner = (Get-Content (Join-Path $lockDir 'owner.txt') -ErrorAction SilentlyContinue | Select-Object -First 1) }
if ($owner -ne 'combat-grip') { Say "REFUSED: the LIVE lock is not mine (owner='$owner')."; exit 2 }

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
function Get-Since([int]$i, [int]$mark) { $a = @(Get-RunLines $i); if ($a.Count -le $mark) { return @() }; return @($a[$mark..($a.Count-1)]) }
function Set-Control([int]$i, [string]$leaf, [string]$value) {
    $p = Join-Path $inst[$i].Root $leaf
    if ($null -eq $value) { Remove-Item -LiteralPath $p -ErrorAction SilentlyContinue }
    else { Set-Content -LiteralPath $p -Value $value -NoNewline }
}
function Show-Log([int]$i, [string]$pattern, [int]$n) {
    $a = @(Get-RunLines $i | Where-Object { $_ -match $pattern } | Select-Object -Last $n)
    if ($a.Count -eq 0) { Note "  [$i] (no line matches '$pattern')"; return }
    foreach ($l in $a) { Note ("  [$i] " + $l.Trim()) }
}
function Step([string]$t, [string]$b) { Note ''; Note "### $t"; if ($b) { Note $b } }
function Press-Key([int]$i, [int]$vk = 0x0D) {
    if (-not $script:pids.ContainsKey($i)) { return }
    if (-not (Get-Process -Id $script:pids[$i] -ErrorAction SilentlyContinue)) { return }
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'send-input.ps1') -ProcessId $script:pids[$i] -Vk $vk 2>&1 | Out-Null
}
function Shoot([int]$i, [string]$tag) {
    if (-not $script:pids.ContainsKey($i)) { return }
    if (-not (Get-Process -Id $script:pids[$i] -ErrorAction SilentlyContinue)) { return }
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

Say 'gun-proof start'
Get-Process -Name 'QueensDomain-Win64-Shipping','qd-relay' -ErrorAction SilentlyContinue | ForEach-Object { Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue }
Start-Sleep -Seconds 2
$built = Join-Path $repo 'mod\native\build\winmm.dll'
Copy-Item -LiteralPath $built -Destination (Join-Path $gameDir 'winmm.dll') -Force
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
    foreach ($c in 'qd.tp','qd.act','qd.hard','qd.cheat','qd.use','qd.selfhp','qd.equip','qd.wsys','qd.mover','qd.attack') { Set-Control $i $c $null }
    Set-Control $i 'qd.scan' '1'
    Set-Control $i 'qd.god' '400'
    if (Test-Path -LiteralPath $inst[$i].Pid) { Remove-Item -LiteralPath $inst[$i].Pid -ErrorAction SilentlyContinue }
}

$rp = Start-Process -FilePath $relay -ArgumentList @('--bind','127.0.0.1:47800') `
        -RedirectStandardOutput (Join-Path $outDir 'relay.log') -RedirectStandardError (Join-Path $outDir 'relay.err') `
        -WindowStyle Hidden -PassThru
Start-Sleep -Seconds 2
# Grant the crossbow, the energy-cell upgrade and a throwing spear: one of them has to fire.
$env:QDCOOP_GIVE = @(
    '/Game/Items/Weapons/Luudecrossbow/LuudeCrossbow_Item.LuudeCrossbow_Item'
    '/Game/Items/Upgrades/EnergyCell/EnergyCell_Item.EnergyCell_Item'
    '/Game/Items/Weapons/HeavyRifle/HeavyRifle_Item.HeavyRifle_Item'
) -join ';'
$script:pids = @{}
foreach ($i in 1, 2) {
    $launch = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'run-hidden.ps1') -UserDir $inst[$i].Root -Suffix $inst[$i].Suffix 2>&1
    $gp = ($launch | Where-Object { $_ -match '^\d+$' } | Select-Object -Last 1)
    if ($gp) { $script:pids[$i] = [int]$gp; Set-Content -LiteralPath $inst[$i].Pid -Value $gp; Say "inst$i pid=$gp" }
    Start-Sleep -Seconds 3
}
Remove-Item Env:\QDCOOP_GIVE -ErrorAction SilentlyContinue

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
        if ($pr -lt 14) { Press-Key $i 0x0D; Start-Sleep -Milliseconds 700; Press-Key $i 0x20; ++$pr }
        Start-Sleep -Seconds 6
    }
    Say "inst${i} in game"
}
Start-Sleep -Seconds 5

Step 'WHAT DID THE GRANT ACTUALLY GIVE?'
Show-Log $H 'dev give|local equipment rev|actor held:' 12

$tries = @(
    @{ Name='crossbow + Attack';        Item='/Game/Items/Weapons/Luudecrossbow/LuudeCrossbow_Item.LuudeCrossbow_Item'; Spec='Attack,400,3' }
    @{ Name='crossbow + UltraAttack';   Item='/Game/Items/Weapons/Luudecrossbow/LuudeCrossbow_Item.LuudeCrossbow_Item'; Spec='UltraAttack,400,3' }
    @{ Name='rifle + Attack';           Item='/Game/Items/Weapons/HeavyRifle/HeavyRifle_Item.HeavyRifle_Item';         Spec='Attack,400,3' }
    @{ Name='rifle + UltraAttack';      Item='/Game/Items/Weapons/HeavyRifle/HeavyRifle_Item.HeavyRifle_Item';         Spec='UltraAttack,400,3' }
    @{ Name='crossbow + SecondaryAttack'; Item='/Game/Items/Weapons/Luudecrossbow/LuudeCrossbow_Item.LuudeCrossbow_Item'; Spec='SecondaryAttack,400,3' }
)
$n = 0
foreach ($t in $tries) {
    $n++
    Step ("TRY: " + $t.Name)
    $mE = Get-Mark $H
    Set-Control $H 'qd.equip' ("{0},0#g{1}" -f $t.Item, $n)
    Start-Sleep -Seconds 3
    Show-Log $H 'rig: equip|cannot load|hand R now holds' 3
    $mA = Get-Mark $H
    $mP = Get-Mark $P
    Set-Control $H 'qd.attack' ("{0}#g{1}" -f $t.Spec, $n)
    Start-Sleep -Seconds 5
    Show-Log $H 'rig: attack|local montage|effect: local' 10
    $projS = @(Get-Since $H $mA | Where-Object { $_ -match 'effect: local' })
    $projP = @(Get-Since $P $mP | Where-Object { $_ -match 'effect: .*shown' })
    Verdict 'gun' $H ($t.Name + " -> a projectile spawned") ($projS.Count -gt 0) ($projS | Select-Object -Last 1)
    Verdict 'gun' $P ($t.Name + " -> the partner shows the shot") ($projP.Count -gt 0) ($projP | Select-Object -Last 1)
    Show-Log $P 'effect: .*shown|effect: impact|effect: no impact' 8
    Shoot $H ("gun{0}" -f $n); Shoot $P ("gun{0}" -f $n)
    if ($projS.Count -gt 0) { Note '  >>> this try fired; the rest are skipped'; break }
}

# ===========================================================================
# THE LANE ITSELF -- spawn a real gun-class projectile through the engine
# ===========================================================================
# The guns above are ENERGY weapons and the rig cannot charge them; if none of the tries
# fired, this exercises the exact thing round 1/2 left unit-tested: does the widened gate
# (ProjectileMovement or class name, walked up the super chain) carry a GUN projectile to
# the partner, with its impact?  The actor is the real class, spawned by the engine's own
# deferred-spawn pair with the pawn as Owner and Instigator -- what the lane would see
# after a shot.  This proves the LANE, not the trigger.
Step 'LANE: a real GUN-class projectile spawned as the player own shot' @'
`qd.proj <class>` spawns the projectile actor owned and instigated by the local pawn.  If
the effect lane carries it, the partner logs the copy and its impact -- which is the whole
of the widened gate's behaviour, with no dependence on the game being able to fire a gun.
'@
foreach ($proj in @(
    '/Game/Items/Weapons/Default_Gun_Projectile.Default_Gun_Projectile_C'
    '/Game/Items/Weapons/HeavyRifle/HeavyRifle_Projectile.HeavyRifle_Projectile_C'
    '/Game/Items/Weapons/Luudecrossbow/LuudeCrossbow_Projectile.LuudeCrossbow_Projectile_C'
)) {
    $n++
    $mA = Get-Mark $H
    $mP = Get-Mark $P
    Set-Control $H 'qd.proj' ("{0}#p{1}" -f $proj, $n)
    Start-Sleep -Seconds 3
    $spawned = @(Get-Since $H $mA | Where-Object { $_ -match 'rig: proj' })
    $sentS   = @(Get-Since $H $mA | Where-Object { $_ -match 'effect: local' })
    $shownP  = @(Get-Since $P $mP | Where-Object { $_ -match 'effect: .*shown|effect: impact|effect: no impact' })
    Note ("  --- " + $proj + " ---")
    foreach ($l in $spawned) { Note ("             " + $l.Trim()) }
    Verdict 'lane' $H ("spawn " + $proj) ($spawned -match 'spawned').Count -gt 0 ($spawned | Select-Object -Last 1)
    Verdict 'lane' $H "the effect lane published it" ($sentS.Count -gt 0) ($sentS | Select-Object -Last 1)
    Verdict 'lane' $P "the partner shows the gun projectile" ($shownP.Count -gt 0) ($shownP | Select-Object -Last 1)
    Show-Log $P 'effect: .*shown|effect: impact|effect: no impact' 6
    Shoot $P ("lane{0}" -f $n)
}

Step 'SESSION SURVIVAL'
foreach ($i in 1, 2) {
    $crash = @(Get-RunLines $i | Where-Object { $_ -match 'EXCEPTION_ACCESS_VIOLATION' })
    Verdict 'survive' $i 'no access violation' ($crash.Count -eq 0) $(if ($crash.Count) { $crash[0].Trim() } else { '' })
}

Note ''
Note '=== VERDICTS ==='
foreach ($v in $script:verdicts) {
    Note ("  {0,-10} inst{1}  {2,-4}  {3}" -f $v.Step, $v.Inst, $(if ($v.Ok) { 'PASS' } else { 'FAIL' }), $v.What)
    if ($v.Line) { Note ("           " + ([string]$v.Line).Trim()) }
}
foreach ($i in 1, 2) { if ($script:pids.ContainsKey($i)) { Stop-Process -Id $script:pids[$i] -Force -ErrorAction SilentlyContinue } }
Stop-Process -Id $rp.Id -Force -ErrorAction SilentlyContinue
Say 'stopped'
$script:lines | Set-Content -LiteralPath $Report
Say "report: $Report"
