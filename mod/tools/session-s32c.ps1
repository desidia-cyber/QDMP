# session-s32c.ps1 -- run 3: weapon-specific ATTACKS, thrown weapons and impacts on the
# partner.  Run 2 proved the equipment half (seven families in the right hand on the
# partner's screen, plus the off-hand dagger); the attack half failed there because
# `send-input.ps1 -Button left` posts WM_LBUTTONDOWN and this build reads the mouse through
# raw input, so the click never arrives.
#
# WHY KEYS WORK.  `QueensDomain/Config/DefaultInput.ini` (read out of the pak) binds:
#   Attack          = LeftMouseButton, Gamepad_RightTrigger     <- no keyboard binding
#   SecondaryAttack = RightMouseButton,  Gamepad_LeftTrigger    <- no keyboard binding
#   UltraAttack     = **R**,             Gamepad_FaceButton_Top <- a keyboard attack
#   NormalDaggerThrow = **Q**,           Gamepad_LeftShoulder   <- a keyboard throw
#   UseItem=F  Interact=E  LastWeapon=1  NextWeapon=2  LastDagger=3  NextDagger=4
# R and Q are the two the rig can press, and both run the weapon's own montage, which is
# what `cxf::ClassifyMontage` reads.
[CmdletBinding()]
param([int]$BootSeconds = 240)
$ErrorActionPreference = 'Stop'
$here    = $PSScriptRoot
$repo    = 'G:\Freespace\Queens_Domain\.worktrees\sync-fix'
$gameDir = 'G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64'
$relay   = Join-Path $repo 'mod\rust\target\release\qd-relay.exe'
$rigRoot = 'G:\Freespace\Queens_Domain\_test'
$outDir  = Join-Path $rigRoot 's32c-proof'
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force -Path $outDir | Out-Null }
$Report = Join-Path $outDir 'report.txt'
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
function Wait-Since([int]$i, [string]$pattern, [int]$mark, [int]$seconds) {
    $dl = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $dl) {
        $m = @(Get-Since $i $mark | Where-Object { $_ -match $pattern })
        if ($m.Count -gt 0) { return $m }
        if (-not (Test-Alive $i)) { return @() }
        Start-Sleep -Milliseconds 400
    }
    return @()
}
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
            $t = "[...]" + $t.Substring([Math]::Max(0,$ix-40), [Math]::Min(260, $t.Length-[Math]::Max(0,$ix-40)))
        }
        Note "  [$i] $t"
    }
}
function Step([string]$t, [string]$b) { Note ''; Note "### $t"; if ($b) { Note $b } }
function Press-Key([int]$i, [int]$vk) {
    if (-not (Test-Alive $i)) { return }
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'send-input.ps1') -ProcessId $script:pids[$i] -Vk $vk 2>&1 | Out-Null
}
function Shoot([int]$i, [string]$tag) {
    if (-not (Test-Alive $i)) { return }
    $p = Join-Path $outDir ("{0}-inst{1}.png" -f $tag, $i)
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'capture-hidden.ps1') -ProcessId $script:pids[$i] -Out $p 2>&1 | Out-Null
    Note ("  screenshot: " + $p)
}

$weapons = @(
    '/Game/Items/Weapons/Axe/SimpleAxe_Item.SimpleAxe_Item'
    '/Game/Items/Weapons/CyberHammer/CyberHammer_Item.CyberHammer_Item'
    '/Game/Items/Weapons/BasicSpear/BasicSpear_Item.BasicSpear_Item'
    '/Game/Items/Weapons/WodasoGreatSword/WodasoGreatSword_Item.WodasoGreatSword_Item'
    '/Game/Items/Weapons/HeavyRifle/HeavyRifle_Item.HeavyRifle_Item'
    '/Game/Items/Weapons/RayneScythe/RayneScythe_Item.RayneScythe_Item'
)
$VK_R = 0x52; $VK_Q = 0x51

Say 'scenario start: run 3 (attacks)'
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
$env:QDCOOP_GIVE = ($weapons -join ';')
$script:pids = @{}
foreach ($i in 1, 2) {
    $launch = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'run-hidden.ps1') -UserDir $inst[$i].Root -Suffix $inst[$i].Suffix 2>&1
    $gp = ($launch | Where-Object { $_ -match '^\d+$' } | Select-Object -Last 1)
    if ($gp) { $script:pids[$i] = [int]$gp; Set-Content -LiteralPath $inst[$i].Pid -Value $gp; Say "inst$i pid=$gp" }
    Start-Sleep -Seconds 3
}
Remove-Item Env:\QDCOOP_GIVE -ErrorAction SilentlyContinue
Say "waiting for a session"
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
# Both players in the same place so the partner body is drawn next to the attacker.
Set-Control $H 'qd.tp' '4106,-1744,140,0'
Set-Control $P 'qd.tp' '4400,-1744,140,0'
Start-Sleep -Seconds 4
Set-Control $H 'qd.wsys' 'Init+Weapons#1'
Start-Sleep -Seconds 3

$slot = 0
foreach ($w in $weapons) {
    $slot++
    Step ("ATTACK -- " + [System.IO.Path]::GetFileNameWithoutExtension([System.IO.Path]::GetFileName($w)))
    $mE = Get-Mark $H
    Set-Control $H 'qd.equip' ("{0},0#{1}" -f $w, $slot)
    Start-Sleep -Seconds 3
    Show-Log $H 'rig: equip EquipItem' 1
    $heldH = @(Get-RunLines $H | Where-Object { $_ -match 'hand R now holds|held: ' } | Select-Object -Last 1)
    if ($heldH.Count) { Note ("  held: " + $heldH[-1].Trim()) }
    Show-Log $P 'body: hand R now holds' 1

    # R = UltraAttack (a keyboard attack), tapped then held (a held attack charges).
    $mK = Get-Mark $H
    $mP = Get-Mark $P
    Press-Key $H $VK_R
    Start-Sleep -Seconds 2
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'send-input.ps1') -ProcessId $script:pids[$H] -Vk $VK_R -HoldMs 900 2>&1 | Out-Null
    Start-Sleep -Seconds 3
    Show-Log $H 'local montage|local action' 6
    Show-Log $P 'peer action|body: swing variant' 6
    # Q = NormalDaggerThrow (a keyboard throw): the projectile + impact lane.
    Press-Key $H $VK_Q
    Start-Sleep -Seconds 3
    Show-Log $H 'local montage|effect: local|impact system' 6
    Show-Log $P 'effect: .*shown|effect: system' 6
    Shoot $H ("atk$slot-a"); Shoot $P ("atk$slot-b")
}

Step 'NO REAL FAULTS'
foreach ($i in 1, 2) {
    $c = @(Get-RunLines $i | Where-Object { $_ -match 'FAULT |EXCEPTION_ACCESS_VIOLATION' })
    Note ("  [{0}] alive={1} real faults={2}" -f $i, [bool](Get-Process -Id $script:pids[$i] -ErrorAction SilentlyContinue), $c.Count)
}
foreach ($i in 1, 2) { if ($script:pids.ContainsKey($i)) { Stop-Process -Id $script:pids[$i] -Force -ErrorAction SilentlyContinue } }
Stop-Process -Id $rp.Id -Force -ErrorAction SilentlyContinue
$script:lines | Set-Content -LiteralPath $Report
Say "report: $Report"
