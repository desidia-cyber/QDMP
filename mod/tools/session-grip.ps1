# session-grip.ps1 -- round 3 for combat-vfx: GRIP + FINGERS, the WEAPON-anchored swing
# arc, and the weapon-specific ATTACKS (with a gun's projectile) on the partner.
#
# WHAT THIS SESSION PROVES, AND WHY EACH PROOF NEEDS A RUN
# --------------------------------------------------------
# 1. GRIP.  The FP arms and the full-body mesh put the weapon socket at the same place on
#    the hand but with a different ORIENTATION (measured on wip: ~100 deg apart), so a
#    socket-relative transform from the sender landed rotated on the partner's copy.  The
#    receiver now measures `inv(L_body) * L_fp` once (`body: grip calibrated`) and composes
#    it onto every item.
# 2. FINGERS.  The body rig has no grip pose, so a weapon sat in an open palm.  The finger
#    chains are calibrated once (`body: finger chains calibrated (N bones)`) and curled for
#    a hand that holds something.
# 3. ARC.  The swing arc used to be rebuilt at eye height (`{0,12,150}`), i.e. on the HEAD;
#    it is now anchored to Weapon_R and attached to that socket (`arc: shown ... at weapon
#    ...  socket 1`).
# 4. ATTACKS.  Rounds 1-2 could not fire one: `send-input.ps1 -Button left` posts
#    WM_LBUTTONDOWN and this build reads the mouse through RAW input, and WM_KEYDOWN for
#    R/Q did not deliver either.  The new `qd.attack` lever calls the pawn's compiled
#    `InpActEvt_<Action>_K2Node_InputActionEvent` directly with the `KeyEvent` a press and
#    a release deliver -- the input path minus the window -- and logs the montage the game
#    then plays.  That is what makes a weapon-specific attack (and the gun's projectile
#    behind it) reachable.
#
# The rig writes a control file per instance (`<UserDir>\qd.<leaf>`); the mod polls it.
[CmdletBinding()]
param([int]$BootSeconds = 300)
$ErrorActionPreference = 'Stop'
$here    = $PSScriptRoot
$repo    = 'G:\Freespace\Queens_Domain\.worktrees\sync-fix'
$gameDir = 'G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64'
$relay   = Join-Path $repo 'mod\rust\target\release\qd-relay.exe'
$rigRoot = 'G:\Freespace\Queens_Domain\_test'
$outDir  = Join-Path $rigRoot 'grip-proof'
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
function Test-Alive([int]$i) { if (-not $script:pids.ContainsKey($i)) { return $false }; return [bool](Get-Process -Id $script:pids[$i] -ErrorAction SilentlyContinue) }
function Get-Since([int]$i, [int]$mark) { $a = @(Get-RunLines $i); if ($a.Count -le $mark) { return @() }; return @($a[$mark..($a.Count-1)]) }
function Set-Control([int]$i, [string]$leaf, [string]$value) {
    $p = Join-Path $inst[$i].Root $leaf
    if ($null -eq $value) { Remove-Item -LiteralPath $p -ErrorAction SilentlyContinue }
    else { Set-Content -LiteralPath $p -Value $value -NoNewline }
}
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

# One weapon per family, plus the gun whose projectile the widened gate is for.  These are
# the paths round 2 proved EquipItem accepts (each `ran=1`).
$guns    = @('/Game/Items/Weapons/HeavyRifle/HeavyRifle_Item.HeavyRifle_Item')
$weapons = @(
    '/Game/Items/Weapons/RustedSword/RustySword_Item.RustySword_Item'
    '/Game/Items/Weapons/Axe/SimpleAxe_Item.SimpleAxe_Item'
    '/Game/Items/Weapons/CyberHammer/CyberHammer_Item.CyberHammer_Item'
    '/Game/Items/Weapons/BasicSpear/BasicSpear_Item.BasicSpear_Item'
    '/Game/Items/Weapons/FinalClaw/FinalClaw_Item.FinalClaw_Item'
    '/Game/Items/Weapons/RayneScythe/RayneScythe_Item.RayneScythe_Item'
    '/Game/Items/Weapons/HeavyRifle/HeavyRifle_Item.HeavyRifle_Item'
)

Say 'scenario start: round 3 (grip, fingers, weapon arc, weapon attacks)'
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
    foreach ($c in 'qd.tp','qd.act','qd.hard','qd.cheat','qd.use','qd.selfhp','qd.equip','qd.wsys','qd.mover','qd.attack') { Set-Control $i $c $null }
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
Start-Sleep -Seconds 6

# ===========================================================================
# GRIP + FINGERS -- what the RECEIVER calibrated, before any weapon
# ===========================================================================
Step 'GRIP/FINGERS -- the body measured its own rig on both machines' @'
Both instances hold a full-body mesh (Player_FullChar_2026) and the local first-person
arms, so BOTH calibrate.  `grip calibrated` is the frame difference; `finger chains
calibrated (N bones)` is the 5 chains x 2 hands.
'@
foreach ($i in 1, 2) {
    Start-Sleep -Seconds 2
    $g = @(Get-RunLines $i | Where-Object { $_ -match 'body: grip calibrated' })
    $f = @(Get-RunLines $i | Where-Object { $_ -match 'body: finger chains calibrated' })
    $r = @(Get-RunLines $i | Where-Object { $_ -match 'body: rest pose captured' })
    Verdict 'grip' $i 'rest pose captured' ($r.Count -gt 0) ($r | Select-Object -Last 1)
    Verdict 'grip' $i 'grip frame calibrated' ($g.Count -gt 0) ($g | Select-Object -Last 1)
    Verdict 'grip' $i 'finger chains calibrated' ($f.Count -gt 0) (@($f | Select-Object -Last 1) -join '; ')
    Show-Log $i 'body: built|body: grip|body: finger|engine API incomplete|NO SKELETAL' 8
}

# ===========================================================================
# EQUIP each family on the HOST; read what the PEER decided to draw and its grip
# ===========================================================================
Step 'EQUIP -- one of each family into the right hand, and the partner draws it' @'
`qd.equip <item>,0` is the game's own `FS_PlayerChar_C.EquipItem(Item, SlotIndex)`.  The
PEER's lines are the proof: what the OTHER machine chose to draw, and at what transform.
'@
$slot = 0
foreach ($w in $weapons) {
    $slot++
    $name = [System.IO.Path]::GetFileName($w)
    $mE = Get-Mark $H
    $mP = Get-Mark $P
    Set-Control $H 'qd.equip' ("{0},0#{1}" -f $w, $slot)
    Start-Sleep -Seconds 4
    $eq = @(Get-Since $H $mE | Where-Object { $_ -match 'rig: equip' })
    $heldS = @(Get-Since $H $mE | Where-Object { $_ -match 'actor held:' } | Select-Object -Last 1)
    $peerHeld = @(Get-Since $P $mP | Where-Object { $_ -match 'body: hand R now holds' } | Select-Object -Last 1)
    $peerHeldAll = @(Get-RunLines $P | Where-Object { $_ -match 'body: hand R now holds' } | Select-Object -Last 1)
    Note ''
    Note "  --- $name ---"
    Verdict 'equip' $H ("EquipItem " + $name) (@($eq | Where-Object { $_ -match 'ran=1' }).Count -gt 0) ($eq | Select-Object -Last 1)
    if ($heldS) { Note ("           sender: " + $heldS.Trim()) }
    Verdict 'equip' $P ("partner draws " + $name) ($peerHeld.Count -gt 0 -or $peerHeldAll.Count -gt 0) $(if ($peerHeld.Count) { $peerHeld | Select-Object -Last 1 } else { $peerHeldAll | Select-Object -Last 1 })
    Start-Sleep -Milliseconds 500
    Shoot $H ("equip{0}" -f $slot)
    Shoot $P ("equip{0}" -f $slot)

    # =======================================================================
    # ATTACK -- the game's own input event for this weapon's attack
    # =======================================================================
    Step ("ATTACK $name -- qd.attack (InpActEvt_*)")
    $mA = Get-Mark $H
    $mPA = Get-Mark $P
    # gap 300 ms, 2 fires: the opener montage, then the strike.  Measured: the pawn
    # declares the Attack event TWICE and only the second one plays anything.
    Set-Control $H 'qd.attack' ("Attack,300,2#{0}" -f $slot)
    Start-Sleep -Seconds 4
    $pressS = @(Get-Since $H $mA | Where-Object { $_ -match 'rig: attack .*fired' })
    $montS  = @(Get-Since $H $mA | Where-Object { $_ -match 'local montage' })
    $actP   = @(Get-Since $P $mPA | Where-Object { $_ -match 'peer action kind' })
    Verdict 'attack' $H ("input event(s) fired (" + $name + ")") ($pressS.Count -gt 0) ($pressS | Select-Object -Last 1)
    if ($montS.Count -gt 0) {
        Note '           sender montage(s):'
        foreach ($l in ($montS | Select-Object -Last 4)) { Note ("             " + $l.Trim()) }
    }
    Verdict 'attack' $H ("the game played a montage for " + $name) ($montS.Count -gt 0) ($montS | Select-Object -Last 1)
    Verdict 'attack' $P ("partner played the action kind") ($actP.Count -gt 0) ($actP | Select-Object -Last 1)
    Shoot $H ("atk{0}" -f $slot); Shoot $P ("atk{0}" -f $slot)

    # The ALT attack, on the families that have a distinct one.
    if ($name -match 'Hammer|Claw|Scythe|Spear') {
        Step ("ALT ATTACK $name -- secondary (InpActEvt_SecondaryAttack)")
        $mB = Get-Mark $H
        $mPB = Get-Mark $P
        Set-Control $H 'qd.attack' ("SecondaryAttack,300,2#{0}a" -f $slot)
        Start-Sleep -Seconds 4
        $pB = @(Get-Since $H $mB | Where-Object { $_ -match 'rig: attack .*fired|local montage' })
        $aB = @(Get-Since $P $mPB | Where-Object { $_ -match 'peer action kind' })
        foreach ($l in ($pB | Select-Object -Last 4)) { Note ("             " + $l.Trim()) }
        Verdict 'alt' $H ("alt input event fired (" + $name + ")") (@($pB | Where-Object { $_ -match 'rig: attack .*fired' }).Count -gt 0) ($pB | Select-Object -Last 1)
        Verdict 'alt' $H ("the game played an ALT montage (" + $name + ")") (@($pB | Where-Object { $_ -match 'local montage' }).Count -gt 0) (@($pB | Where-Object { $_ -match 'local montage' }) | Select-Object -Last 1)
        Verdict 'alt' $P ("partner played the ALT action kind") ($aB.Count -gt 0) ($aB | Select-Object -Last 1)
        Shoot $P ("alt{0}" -f $slot)
    }

    # The ARC: a swing spawns a melee arc, which must be shown at the WEAPON now.
    Step ("ARC $name -- the swing arc anchored to the weapon")
    $arc = @(Get-RunLines $P | Where-Object { $_ -match 'arc: shown' } | Select-Object -Last 2)
    $grip = @(Get-RunLines $P | Where-Object { $_ -match 'body: grip calibrated' })
    if ($arc.Count -gt 0) {
        foreach ($l in $arc) { Note ("             " + $l.Trim()) }
        Verdict 'arc' $P ("arc shown at the weapon, attached") (@($arc | Where-Object { $_ -match 'at weapon' }).Count -gt 0) ($arc | Select-Object -Last 1)
        Verdict 'arc' $P 'arc socket attach verified' (@($arc | Where-Object { $_ -match 'socket 1' }).Count -gt 0) ($arc | Select-Object -Last 1)
    } else {
        Note '             (no arc line on the partner for this family)'
    }
}

# ===========================================================================
# GUN -- the projectile the widened class gate exists for
# ===========================================================================
Step 'GUN PROJECTILE -- a gun fires through the game and the partner sees the shot' @'
The widened projectile gate (walking `ProjectileMovement` + class name up the super chain)
was unit-tested only because no gun could be fired.  `qd.attack Attack` on an equipped gun
runs the game`s own fire path, which spawns the projectile; the partner should copy it and
show its impact.
'@
foreach ($g in $guns) {
    $name = [System.IO.Path]::GetFileName($g)
    $mE = Get-Mark $H
    Set-Control $H 'qd.equip' ("{0},0#gun" -f $g)
    Start-Sleep -Seconds 4
    Show-Log $H 'rig: equip|actor held:' 3
    $mA = Get-Mark $H
    $mP = Get-Mark $P
    Set-Control $H 'qd.attack' 'Attack,150#gun'
    Start-Sleep -Seconds 4
    Show-Log $H 'rig: attack|local montage|effect: local' 8
    $projS = @(Get-Since $H $mA | Where-Object { $_ -match 'effect: local' })
    $projP = @(Get-Since $P $mP | Where-Object { $_ -match 'effect: .*shown|effect: impact' })
    Verdict 'gun' $H ("$name fired a projectile") ($projS.Count -gt 0) ($projS | Select-Object -Last 1)
    Verdict 'gun' $P ("partner shows the projectile") (@($projP | Where-Object { $_ -match 'projectile|shot|bolt' }).Count -gt 0) ($projP | Select-Object -Last 1)
    Verdict 'gun' $P ("partner shows an impact") (@($projP | Where-Object { $_ -match 'impact' }).Count -gt 0) (@($projP | Where-Object { $_ -match 'impact' }) | Select-Object -Last 1)
    Show-Log $P 'effect: .*shown|effect: impact|effect: no impact' 8
    Shoot $H 'gun'; Shoot $P 'gun'
}

# ===========================================================================
# SESSION SURVIVAL
# ===========================================================================
Step 'SESSION SURVIVAL'
foreach ($i in 1, 2) {
    Note ("  [{0}] alive={1}" -f $i, [bool](Get-Process -Id $script:pids[$i] -ErrorAction SilentlyContinue))
    $crash = @(Get-RunLines $i | Where-Object { $_ -match 'EXCEPTION_ACCESS_VIOLATION' })
    Note ("  [{0}] access violations: {1}" -f $i, $crash.Count)
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
Say 'stopped both instances and the relay'
$script:lines | Set-Content -LiteralPath $Report
Say "report: $Report"
