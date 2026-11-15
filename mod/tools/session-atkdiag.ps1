# session-atkdiag.ps1 -- SHORT diagnostic run: WHICH input event really starts an attack?
#
# The first round-3 run resolved the pawn's `InpActEvt_Attack_K2Node_InputActionEvent`,
# called it, and it ran (`called=1`) while the first-person arms played NOTHING
# (`montage=None`).  That is the same symptom the interaction lane hit when it pressed the
# wrong object's event -- so before another proof run, dump what the pawn declares and call
# each candidate, watching the arms' ACTIVE MONTAGE as the oracle.
#
# ONE instance is enough: the question is local (does the game play a montage), and the
# body/attack sync is already proven on two.
[CmdletBinding()]
param([int]$BootSeconds = 300)
$ErrorActionPreference = 'Stop'
$here    = $PSScriptRoot
$repo    = 'G:\Freespace\Queens_Domain\.worktrees\sync-fix'
$gameDir = 'G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64'
$relay   = Join-Path $repo 'mod\rust\target\release\qd-relay.exe'
$rigRoot = 'G:\Freespace\Queens_Domain\_test'
$outDir  = Join-Path $rigRoot 'atkdiag'
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
$inst = @{ 1 = (Get-RigPaths 1) }
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
    foreach ($l in $a) { Note ("  " + $l.Trim()) }
}
function Press-Key([int]$i, [int]$vk = 0x0D) {
    if (-not (Get-Process -Id $script:pids[$i] -ErrorAction SilentlyContinue)) { return }
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'send-input.ps1') -ProcessId $script:pids[$i] -Vk $vk 2>&1 | Out-Null
}

Say 'atkdiag start'
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
foreach ($c in 'qd.tp','qd.act','qd.hard','qd.cheat','qd.use','qd.selfhp','qd.equip','qd.wsys','qd.mover','qd.attack') { Set-Control 1 $c $null }
Set-Control 1 'qd.scan' '1'
Set-Control 1 'qd.god' '400'

$rp = Start-Process -FilePath $relay -ArgumentList @('--bind','127.0.0.1:47800') `
        -RedirectStandardOutput (Join-Path $outDir 'relay.log') -RedirectStandardError (Join-Path $outDir 'relay.err') `
        -WindowStyle Hidden -PassThru
Start-Sleep -Seconds 2
$env:QDCOOP_GIVE = '/Game/Items/Weapons/Axe/SimpleAxe_Item.SimpleAxe_Item'
$script:pids = @{}
$launch = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'run-hidden.ps1') -UserDir $inst[1].Root -Suffix $inst[1].Suffix 2>&1
$gp = ($launch | Where-Object { $_ -match '^\d+$' } | Select-Object -Last 1)
if ($gp) { $script:pids[1] = [int]$gp; Say "inst1 pid=$gp" }
Remove-Item Env:\QDCOOP_GIVE -ErrorAction SilentlyContinue

$dl = (Get-Date).AddSeconds($BootSeconds); $pr = 0
while ((Get-Date) -lt $dl) {
    if ((@(Get-RunLines 1 | Where-Object { $_ -match 'scan: health|scan: local at' })).Count -gt 0) { break }
    if ($pr -lt 14) { Press-Key 1 0x0D; Start-Sleep -Milliseconds 700; Press-Key 1 0x20; ++$pr }
    Start-Sleep -Seconds 6
}
Say 'inst1 in game'
Start-Sleep -Seconds 6

Note ''
Note '### 1. WHICH INPUT EVENTS DOES THE PAWN DECLARE?'
$m = Get-Mark 1
Set-Control 1 'qd.attack' 'Info:Attack#1'
Start-Sleep -Seconds 3
Show-Log 1 'signature InpActEvt|    (KeyEvent|InputActionName|InpActEvt)|renders|atkinfo' 60

Note ''
Note '### 2. CALL EACH Attack EVENT AND WATCH THE ARMS MONTAGE'
$m = Get-Mark 1
Set-Control 1 'qd.attack' 'Call:Attack#2'
Start-Sleep -Seconds 8
Show-Log 1 'rig: atkcall|local montage' 30

Note ''
Note '### 3. THE SAME FOR SecondaryAttack / UltraAttack / NormalDaggerThrow'
foreach ($a in 'SecondaryAttack','UltraAttack','NormalDaggerThrow') {
    $m = Get-Mark 1
    Set-Control 1 'qd.attack' ("Call:{0}#d{0}" -f $a)
    Start-Sleep -Seconds 6
    Show-Log 1 'rig: atkcall|local montage' 10
}

Note ''
Note '### 4. THE DIRECT VERBS (no input event at all)'
foreach ($v in 'UltraAttack','TickAttackKey','DoSimpleAttackBlockDuration') {
    $m = Get-Mark 1
    Set-Control 1 'qd.wsys' ("{0}#v{0}" -f $v)
    Start-Sleep -Seconds 4
    Show-Log 1 ("rig: wsys|local montage") 4
}

Note ''
Note '### 5. THE FUNCTIONS THE PAWN EXPOSES (attack/charge/montage words)'
Set-Control 1 'qd.attack' 'List#9'
Start-Sleep -Seconds 3
Show-Log 1 'Atk|Attack|Charge|Montage|Fire|Shoot' 40

foreach ($i in 1) { if ($script:pids.ContainsKey($i)) { Stop-Process -Id $script:pids[$i] -Force -ErrorAction SilentlyContinue } }
Stop-Process -Id $rp.Id -Force -ErrorAction SilentlyContinue
Say 'stopped'
$script:lines | Set-Content -LiteralPath $Report
Say "report: $Report"
