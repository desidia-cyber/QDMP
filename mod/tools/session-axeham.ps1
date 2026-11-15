# session-axeham.ps1 -- SHORT, single instance: do the AXE and the HAMMER play a montage?
#
# The round-3 proof run fired `Attack` on the axe and the hammer and both events ran while
# the arms played nothing in the sampled window.  That run also had a bug in the lever (the
# `#nonce` was stripped before the "have I handled this spec" comparison, so a repeat of the
# same action was skipped) which is now fixed.  The question is local -- does the game play a
# montage -- so ONE instance answers it, and the peer half is already proven for five
# families.
[CmdletBinding()]
param([int]$BootSeconds = 300)
$ErrorActionPreference = 'Stop'
$here    = $PSScriptRoot
$repo    = 'G:\Freespace\Queens_Domain\.worktrees\sync-fix'
$gameDir = 'G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64'
$relay   = Join-Path $repo 'mod\rust\target\release\qd-relay.exe'
$rigRoot = 'G:\Freespace\Queens_Domain\_test'
$outDir  = Join-Path $rigRoot 'axeham'
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force -Path $outDir | Out-Null }
$Report = Join-Path $outDir 'report.txt'
$script:lines = New-Object System.Collections.Generic.List[string]
function Say($m) { $s = "[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $m; Write-Output $s; $script:lines.Add($s) | Out-Null }
function Note($m) { Write-Output $m; $script:lines.Add($m) | Out-Null }

$lockDir = Join-Path $rigRoot 'locks\live'
$owner = ''
if (Test-Path (Join-Path $lockDir 'owner.txt')) { $owner = (Get-Content (Join-Path $lockDir 'owner.txt') -ErrorAction SilentlyContinue | Select-Object -First 1) }
if ($owner -ne 'combat-grip') { Say "REFUSED: the LIVE lock is not mine (owner='$owner')."; exit 2 }

$root = Join-Path $rigRoot 'inst1'
$log  = Join-Path $gameDir 'qdcoop-qd1.log'
function Get-RunLines {
    if (-not (Test-Path -LiteralPath $log)) { return @() }
    $all = @(Get-Content -LiteralPath $log -ErrorAction SilentlyContinue)
    if ($all.Count -eq 0) { return @() }
    $start = 0
    for ($k = $all.Count - 1; $k -ge 0; $k--) { if ($all[$k] -match 'hooked FEngineLoop') { $start = $k; break } }
    return @($all[$start..($all.Count - 1)])
}
function Get-Mark { return (Get-RunLines).Count }
function Get-Since([int]$mark) { $a = @(Get-RunLines); if ($a.Count -le $mark) { return @() }; return @($a[$mark..($a.Count-1)]) }
function Set-Control([string]$leaf, [string]$value) {
    $p = Join-Path $root $leaf
    if ($null -eq $value) { Remove-Item -LiteralPath $p -ErrorAction SilentlyContinue }
    else { Set-Content -LiteralPath $p -Value $value -NoNewline }
}
function Show-Log([string]$pattern, [int]$n) {
    $a = @(Get-RunLines | Where-Object { $_ -match $pattern } | Select-Object -Last $n)
    if ($a.Count -eq 0) { Note "  (no line matches '$pattern')"; return }
    foreach ($l in $a) { Note ("  " + $l.Trim()) }
}

Say 'axeham start'
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
foreach ($c in 'qd.tp','qd.act','qd.hard','qd.cheat','qd.use','qd.selfhp','qd.equip','qd.wsys','qd.mover','qd.attack','qd.proj') { Set-Control $c $null }
Set-Control 'qd.scan' '1'
Set-Control 'qd.god' '400'

$rp = Start-Process -FilePath $relay -ArgumentList @('--bind','127.0.0.1:47800') `
        -RedirectStandardOutput (Join-Path $outDir 'relay.log') -RedirectStandardError (Join-Path $outDir 'relay.err') `
        -WindowStyle Hidden -PassThru
Start-Sleep -Seconds 2
$env:QDCOOP_GIVE = '/Game/Items/Weapons/Axe/SimpleAxe_Item.SimpleAxe_Item;/Game/Items/Weapons/CyberHammer/CyberHammer_Item.CyberHammer_Item'
$launch = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'run-hidden.ps1') -UserDir $root -Suffix 'qd1' 2>&1
$gp = ($launch | Where-Object { $_ -match '^\d+$' } | Select-Object -Last 1)
$script:pid1 = if ($gp) { [int]$gp } else { 0 }
Say "inst1 pid=$($script:pid1)"
Remove-Item Env:\QDCOOP_GIVE -ErrorAction SilentlyContinue

$dl = (Get-Date).AddSeconds($BootSeconds); $pr = 0
while ((Get-Date) -lt $dl) {
    if ((@(Get-RunLines | Where-Object { $_ -match 'scan: health|scan: local at' })).Count -gt 0) { break }
    if ($pr -lt 14) {
        if (-not $script:pid1) { break }
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'send-input.ps1') -ProcessId $script:pid1 -Vk 0x0D 2>&1 | Out-Null
        Start-Sleep -Milliseconds 700
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'send-input.ps1') -ProcessId $script:pid1 -Vk 0x20 2>&1 | Out-Null
        ++$pr
    }
    Start-Sleep -Seconds 6
}
Say 'in game'
Start-Sleep -Seconds 6

$i = 0
foreach ($w in @(
    @{ N='axe';    P='/Game/Items/Weapons/Axe/SimpleAxe_Item.SimpleAxe_Item' }
    @{ N='hammer'; P='/Game/Items/Weapons/CyberHammer/CyberHammer_Item.CyberHammer_Item' }
)) {
    $i++
    Note ''
    Note ("### " + $w.N)
    Set-Control 'qd.equip' ("{0},0#e{1}" -f $w.P, $i)
    Start-Sleep -Seconds 4
    Show-Log 'rig: equip|cannot load|hand R now holds' 3
    # Three separate bursts, each with its own nonce, and a long settle window: the opener
    # is the FIRST montage after the swap, so a burst that lands while the swap montage is
    # still playing can be swallowed.
    foreach ($k in 1..3) {
        $m = Get-Mark
        Set-Control 'qd.attack' ("Attack,350,2#{0}-{1}" -f $i, $k)
        Start-Sleep -Seconds 5
        $rows = @(Get-Since $m | Where-Object { $_ -match 'rig: attack (fired|Attack)|local montage' })
        if ($rows.Count -gt 0) {
            Note ("  burst {0}:" -f $k)
            foreach ($l in $rows) { Note ("    " + $l.Trim()) }
        } else { Note ("  burst {0}: (no lines)" -f $k) }
    }
    # SecondaryAttack too -- the hammer's alt is the interesting one.
    $m = Get-Mark
    Set-Control 'qd.attack' ("SecondaryAttack,350,2#{0}-s" -f $i)
    Start-Sleep -Seconds 5
    Note '  alt:'
    foreach ($l in @(Get-Since $m | Where-Object { $_ -match 'rig: attack (fired|SecondaryAttack)|local montage' })) { Note ("    " + $l.Trim()) }
}

if ($script:pid1) { Stop-Process -Id $script:pid1 -Force -ErrorAction SilentlyContinue }
Stop-Process -Id $rp.Id -Force -ErrorAction SilentlyContinue
Say 'stopped'
$script:lines | Set-Content -LiteralPath $Report
Say "report: $Report"
