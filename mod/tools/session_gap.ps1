# session_gap.ps1 -- session_final.ps1's three gaps, each with that run's diagnosis applied.
# Raised from session_final.ps1; the header below describes what it NOW does.
#   WORLD-INTERACT: a door opened on A and CLOSED from B (both directions + bidirectional),
#                   which run 3 missed because its selector demanded one class name that this
#                   run's streamed set did not contain.
#   PROGRESSION:    the half that run 1 could not measure -- kill XP (after the counter moved
#                   to the save object) and the mirrored save (at LogLevel=4, where the
#                   ProcessEvent hook is unconditional).
#
# Everything else in both features is already proven (mod/doc/features/world-interact.md and
# progression.md, "Round 2 live proof").  The lanes are independent, so one boot runs both.
[CmdletBinding()]
param([string]$Phase = 'final', [int]$BootSeconds = 300)

$ErrorActionPreference = 'Stop'
$script:hbPath = 'G:\Freespace\Queens_Domain\_test\final-boot.txt'
function HB([string]$m) { try { Add-Content -LiteralPath $script:hbPath -Value ((Get-Date -Format 'HH:mm:ss') + ' ' + $m) } catch {} }
HB ("script start phase=$Phase")
$here    = $PSScriptRoot
$repo    = Split-Path -Parent (Split-Path -Parent $here)
$gameDir = 'G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64'
$relay   = Join-Path (Split-Path -Parent $here) 'rust\target\release\qd-relay.exe'
$rigRoot = Join-Path 'G:\Freespace\Queens_Domain\_test' ('sfinal-' + $Phase)
$seed    = Join-Path $repo 'analysis\save-backup-before-live'
$outDir  = Join-Path $rigRoot 'out'
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force -Path $outDir | Out-Null }
$Report = Join-Path $outDir ('report-' + $Phase + '.txt')

$script:lines = New-Object System.Collections.Generic.List[string]
trap {
    Write-Output ("UNHANDLED: " + $_.Exception.Message)
    $script:lines.Add("UNHANDLED: " + $_.Exception.Message) | Out-Null
    try { Stop-All } catch {}
    try { $script:lines | Set-Content -LiteralPath $Report } catch {}
    exit 1
}
function Say($m) { $s = "[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $m; Write-Host $s; $script:lines.Add($s) | Out-Null }
function Note($m) { Write-Host $m; $script:lines.Add($m) | Out-Null }

function Get-RigPaths([int]$i) {
    $root = Join-Path $rigRoot ("inst{0}" -f $i)
    [pscustomobject]@{ Index = $i; Root = $root; Suffix = "qd$i"
        Log = Join-Path $gameDir ("qdcoop-qd{0}.log" -f $i); Pid = "$root.pid"
        Saves = Join-Path $root ("Saved_{0}\SaveGames" -f "qd$i") }
}
$inst = @{ 1 = (Get-RigPaths 1); 2 = (Get-RigPaths 2) }

function Read-LogLines([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) { return @() }
    for ($t = 0; $t -lt 3; $t++) {
        try {
            $fs = [System.IO.File]::Open($path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read,
                                         ([System.IO.FileShare]::ReadWrite -bor [System.IO.FileShare]::Delete))
            try { $sr = New-Object System.IO.StreamReader($fs, [System.Text.Encoding]::UTF8, $true); $txt = $sr.ReadToEnd(); $sr.Dispose() }
            finally { $fs.Dispose() }
            return @($txt -split "`r?`n")
        } catch { Start-Sleep -Milliseconds 50 }
    }
    return @()
}
function Get-RunLines([int]$i) {
    $all = @(Read-LogLines $inst[$i].Log)
    if ($all.Count -eq 0) { return @() }
    $start = 0
    for ($k = $all.Count - 1; $k -ge 0; $k--) { if ($all[$k] -match 'hooked FEngineLoop') { $start = $k; break } }
    return @($all[$start..($all.Count - 1)])
}
function Get-Matches([int]$i, [string]$p) { return @(Get-RunLines $i | Where-Object { $_ -match $p }) }
function Get-LineMs([string]$ln) {
    if ($ln -match '^\s*(\d{1,2}):(\d{2}):(\d{2})\.(\d{3})') {
        return ([int]$Matches[1] * 3600000 + [int]$Matches[2] * 60000 + [int]$Matches[3] * 1000 + [int]$Matches[4])
    }
    return -1
}
function Get-Mark([int]$i) { $n = Get-Date; return ($n.Hour * 3600000 + $n.Minute * 60000 + $n.Second * 1000 + $n.Millisecond) - 1000 }
function Test-Alive([int]$i) { if (-not $script:pids.ContainsKey($i)) { return $false }; return [bool](Get-Process -Id $script:pids[$i] -ErrorAction SilentlyContinue) }
function Get-Since([int]$i, [int]$mark) {
    $out = @()
    foreach ($ln in @(Get-RunLines $i)) { $t = Get-LineMs ([string]$ln); if ($t -ge 0 -and $t -ge $mark) { $out += $ln } }
    return $out
}
function Wait-Since([int]$i, [string]$p, [int]$mark, [int]$secs) {
    $deadline = (Get-Date).AddSeconds($secs)
    while ((Get-Date) -lt $deadline) {
        $m = @(Get-Since $i $mark | Where-Object { $_ -match $p })
        if ($m.Count -gt 0) { return $m }
        if (-not (Test-Alive $i)) { return @() }
        Start-Sleep -Milliseconds 500
    }
    return @()
}
function Wait-Gameplay([int]$i, [int]$secs) {
    $deadline = (Get-Date).AddSeconds($secs)
    while ((Get-Date) -lt $deadline) {
        if (-not (Test-Alive $i)) { return $false }
        if (@(Get-Matches $i 'scan: local at').Count -gt 0) { return $true }
        Start-Sleep -Milliseconds 500
    }
    return $false
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
$script:verdicts = New-Object System.Collections.Generic.List[object]
function Check([string]$step, [int]$i, [string]$p, [int]$secs, [string]$label, [int]$Mark = 0) {
    if (-not (Test-Alive $i)) {
        $script:verdicts.Add([pscustomobject]@{ Step = $step; Inst = $i; What = $label; Ok = $false; Line = 'INSTANCE DIED' }) | Out-Null
        Note "  FAIL [$i] $label -- died"; return $false
    }
    $m = Wait-Since $i $p $Mark $secs
    $ok = ($m.Count -gt 0)
    $script:verdicts.Add([pscustomobject]@{ Step = $step; Inst = $i; What = $label; Ok = $ok; Line = ($m | Select-Object -Last 1) }) | Out-Null
    if ($ok) { Note "  PASS [$i] $label" } else { Note "  --   [$i] $label (no '$p' in ${secs}s)" }
    return $ok
}
function Verdict([string]$step, [int]$i, [bool]$ok, [string]$what, [string]$line = '') {
    $script:verdicts.Add([pscustomobject]@{ Step = $step; Inst = $i; What = $what; Ok = $ok; Line = $line }) | Out-Null
    Note ("  {0} [{1}] {2}{3}" -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $i, $what, $(if ($line) { " -- $line" } else { '' }))
}
function Step([string]$t, [string]$b) { Note ''; Note "### $t"; if ($b) { Note $b } }
function Show-Since([int]$i, [string]$p, [int]$mark, [int]$n) {
    $m = @(Get-Since $i $mark | Where-Object { $_ -match $p })
    if ($m.Count -eq 0) { Note "  [$i] (no '$p' since the step began)"; return }
    $m | Select-Object -Last $n | ForEach-Object { $s = [string]$_; if ($s.Length -gt 400) { $s = $s.Substring(0,400) + ' ...' }; Note "  [$i] $s" }
}
function Stop-All {
    Get-Process -Name 'QueensDomain-Win64-Shipping', 'qd-relay' -ErrorAction SilentlyContinue | ForEach-Object {
        Say "  stopping leftover $($_.ProcessName) pid=$($_.Id)"; Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue
    }
    Start-Sleep -Seconds 2
}
function Install-And-Configure {
    $built = Join-Path $repo 'mod\native\build\winmm.dll'
    if (-not (Test-Path -LiteralPath $built)) { Say 'REFUSED: no built proxy'; exit 2 }
    $h = (Get-FileHash -LiteralPath $built -Algorithm SHA256).Hash
    Say "installing proxy sha256=$($h.Substring(0,16))... built $((Get-Item $built).LastWriteTime)"
    $dst = Join-Path $gameDir 'winmm.dll'
    try { Copy-Item -LiteralPath $built -Destination $dst -Force }
    catch {
        $held = $dst + '.held-' + (Get-Date -Format 'HHmmss')
        Rename-Item -LiteralPath $dst -NewName (Split-Path -Leaf $held) -Force
        Copy-Item -LiteralPath $built -Destination $dst -Force
        Say "moved the image-mapped copy to $held"
    }
    $lines = @('[Local]','Enabled=1','LogLevel=4','','[Net]','RelayHost=127.0.0.1','RelayPort=47800',
               '','[Rules]','FlagSync=1','ObjectSync=1','ActorStateSync=1','InteractSync=1',
               'InventorySync=1','QuestItemSync=1','KillXPSync=1','PartnerSaveSync=1',
               'EnemySync=1','HostAuthoritative=1','','[Tuning]','PlayerHz=30')
    $ini = Join-Path $gameDir 'qdcoop.ini'
    $lines | Set-Content -LiteralPath $ini
    Say "wrote $ini : objects+actors+interact+inventory+quest+xp+save, LogLevel=4 (PE hook unconditional)"
    Copy-Item -LiteralPath $ini -Destination (Join-Path $outDir 'qdcoop-used.ini') -Force
}
function Seed-FreshPair {
    Say 're-seeding both instances'
    foreach ($i in 1, 2) {
        $null = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'make_test_instance.ps1') `
                -Name ("inst{0}" -f $i) -Index $i -SourceSave $seed -Root $rigRoot -Suffix $inst[$i].Suffix -Force 2>&1
        if ($LASTEXITCODE -ne 0) { Say "REFUSED: seed $i"; exit 2 }
    }
}
function Start-Boot([string]$tag) {
    Stop-All
    Install-And-Configure
    foreach ($i in 1, 2) {
        foreach ($leaf in 'qd.tp','qd.act','qd.near','qd.flag','qd.kill','qd.hit','qd.cheat','qd.cheatarg',
                          'qd.travel','qd.hard','qd.level','qd.list','qd.drops','qd.menu','qd.efunc','qd.eattack',
                          'qd.use','qd.pdmg','qd.sequence','qd.enemykill','qd.hitguid','qd.obj','qd.gate','qd.dlg') {
            Set-Control $i $leaf $null
        }
        Set-Control $i 'qd.scan' '1'
        Set-Control $i 'qd.god' '400'
        if (Test-Path -LiteralPath $inst[$i].Pid) { Remove-Item -LiteralPath $inst[$i].Pid -ErrorAction SilentlyContinue }
        $old = $inst[$i].Log
        if (Test-Path -LiteralPath $old) { Move-Item -LiteralPath $old -Destination ($old + '.prev') -Force }
    }
    $env:QDCOOP_AUTOLOAD = '0'
    $relayLog = Join-Path $outDir ("relay-$tag.log")
    Remove-Item -LiteralPath $relayLog -ErrorAction SilentlyContinue
    $script:relayProc = Start-Process -FilePath $relay -ArgumentList @('--bind','127.0.0.1:47800') `
        -RedirectStandardOutput $relayLog -RedirectStandardError (Join-Path $outDir "relay-$tag.err") -WindowStyle Hidden -PassThru
    Say "relay pid=$($script:relayProc.Id)"
    Start-Sleep -Seconds 2
    $script:pids = @{}
    foreach ($i in 1, 2) {
        $ueLog = Join-Path $outDir ("$tag-inst{0}-ue.log" -f $i)
        Remove-Item -LiteralPath $ueLog -ErrorAction SilentlyContinue
        $launch = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'run-hidden.ps1') `
                    -UserDir $inst[$i].Root -Suffix $inst[$i].Suffix -AbsLog $ueLog 2>&1
        $gp = ($launch | Where-Object { $_ -match '^\d+$' } | Select-Object -Last 1)
        if (-not $gp) { Say "LAUNCH FAILED inst$i" } else { $script:pids[$i] = [int]$gp; Set-Content -LiteralPath $inst[$i].Pid -Value $gp; Say "inst$i pid=$gp" }
        Start-Sleep -Seconds 3
    }
    Say "waiting for a session (up to $BootSeconds s)"
    $deadline = (Get-Date).AddSeconds($BootSeconds); $roles = @{}
    while ((Get-Date) -lt $deadline) {
        foreach ($i in 1, 2) {
            if ($roles.ContainsKey($i) -or -not $script:pids.ContainsKey($i)) { continue }
            $m = @(Get-Matches $i 'INFO session established: role=(HOST|PEER)')
            if ($m.Count -gt 0 -and $m[-1] -match 'role=(HOST|PEER)') { $roles[$i] = $Matches[1]; Note ("  [{0}] {1}" -f $i, ([string]$m[-1]).Trim()) }
        }
        if ($roles.Count -eq 2) { break }
        if (-not (Test-Alive 1) -and -not (Test-Alive 2)) { break }
        Start-Sleep -Seconds 2
    }
    $script:H = $null; $script:P = $null
    foreach ($i in 1, 2) {
        if ($roles.ContainsKey($i) -and $roles[$i] -eq 'HOST') { $script:H = $i }
        if ($roles.ContainsKey($i) -and $roles[$i] -eq 'PEER') { $script:P = $i }
    }
    if (-not $script:H -or -not $script:P) {
        Say 'REFUSED: no HOST/PEER pair'
        foreach ($i in 1, 2) { Show-Since $i 'session|REFUS|ERR ' 0 10 }
        Stop-All; exit 2
    }
    Say "roles: HOST=inst$($script:H) PEER=inst$($script:P)"
    foreach ($i in @($script:H, $script:P)) {
        $ok = Wait-Gameplay $i $BootSeconds
        Say "inst${i} gameplay=$ok"
        Verdict 'boot' $i $ok 'reached gameplay' ''
    }
}

# --- the world dump -------------------------------------------------------
function Invoke-ObjDump([int]$i, [string]$tag) {
    $mark = Get-Mark $i
    Set-Control $i 'qd.obj' ($tag + '#' + (Get-Date -Format 'HHmmssfff'))
    $deadline = (Get-Date).AddSeconds(40)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 600
        if (@(Get-Since $i $mark | Where-Object { $_ -match 'rig: savable object inventory --' }).Count -gt 0) { break }
    }
    return @(Get-Since $i $mark)
}
function Parse-SavKeys($rows) {
    $out = @()
    foreach ($r in $rows) {
        $s = [string]$r
        if ($s -notmatch 'savkey\s+([0-9A-Fa-f]{8})-[0-9A-Fa-f]{8}-[0-9A-Fa-f]{8}-[0-9A-Fa-f]{8}\s+(\w+)\s+(\S+)\s+\[([^\]]*)\]\s+value=(-?\d+)\s+isset=(\S+)\s+opened=(\S+)\s+world=(\d+)\s+dist=(-?[\d.]+)') { continue }
        $out += [pscustomobject]@{ Key = $Matches[1]; Shape = $Matches[2]; Name = $Matches[3]; Cls = $Matches[4]
            Value = [int]$Matches[5]; Opened = $Matches[7]; World = [int]$Matches[8]; Dist = [double]$Matches[9] }
    }
    return $out
}
function Wait-ObjScan([int]$i, [int]$seconds) {
    $deadline = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $deadline) {
        if (@(Get-Matches $i 'object sync: tracking').Count -gt 0) { return $true }
        if (-not (Test-Alive $i)) { break }
        Start-Sleep -Milliseconds 700
    }
    return $false
}
function Get-SaveStamps([int]$i) {
    $dir = $inst[$i].Saves
    if (-not (Test-Path -LiteralPath $dir)) { return @() }
    return @(Get-ChildItem -LiteralPath $dir -Filter '*.sav' -ErrorAction SilentlyContinue |
             Sort-Object Name | ForEach-Object { [pscustomobject]@{ Name = $_.Name; Stamp = $_.LastWriteTime.ToString('HH:mm:ss.fff') } })
}
function Show-Stamps([int]$i, [string]$when) {
    $s = Get-SaveStamps $i
    Note ("  [B=$i] save files $when : " + (($s | ForEach-Object { "$($_.Name)@$($_.Stamp)" }) -join '  '))
    return $s
}

# ===========================================================================
function Invoke-Final {
    HB 'final start'
    $H = $script:H; $P = $script:P

    Step '1. a door opened on A, then CLOSED from B' 'both directions and bidirectional, one door'
    # BY NAME, not by GUID: session_final picked a closed door out of the PEER's dump and the
    # host answered `no live actor matches 'guid:cf39fbf3'` -- `world=1` in the peer's dump is
    # not a fact about the host's world.  Runs 2 and 3 both reached `SwordShrineGate_BP` by
    # name on BOTH machines, so the close is driven the same way.
    $doorName = $null
    foreach ($dn in 'SwordShrineGate_BP', 'AquaGate_01_SM3', 'AquaGate_01_SM', 'Lib_Door_01_BP') {
        $markP = Get-Mark $P
        Set-Control $P 'qd.act' ('near:' + $dn + '!?Opened#' + (Get-Date -Format 'HHmmssfff'))
        Start-Sleep -Seconds 3
        $line = @(Get-Since $P $markP | Where-Object { $_ -match 'dev props:|no live actor' })
        if ($line.Count -eq 0) { Note "  -- $dn : no answer"; continue }
        Note "  [2] $([string]$line[-1])"
        if (([string]$line[-1]) -match 'Opened=0') { $doorName = $dn; break }
    }
    if (-not $doorName) { Say 'REFUSED: no door-shaped actor in this world reads Opened=0 on the peer' }
    else {
        Note "  door under test: $doorName"
        # OPEN on the peer
        $markP = Get-Mark $P; $markH = Get-Mark $H
        Set-Control $P 'qd.tp' ('near:' + $doorName)
        Start-Sleep -Seconds 7
        Set-Control $P 'qd.act' ('near:' + $doorName + '!Activate#' + (Get-Date -Format 'HHmmssfff'))
        Start-Sleep -Seconds 9
        Show-Since $P 'dev interact: forced verb|actor-state' $markP 8
        Show-Since $H 'actor-state|interact:' $markH 10
        $openH = @(Get-Since $H $markH | Where-Object { $_ -match 'interact: door anim .*state=1' })
        Verdict 'door' $H ($openH.Count -gt 0) 'A OPENED a door and B played its opening animation' ($(if ($openH.Count) { [string]$openH[-1] } else { '' }))
        # CLOSE from the host
        $markH2 = Get-Mark $H; $markP2 = Get-Mark $P
        Set-Control $H 'qd.tp' ('near:' + $doorName)
        Start-Sleep -Seconds 7
        Set-Control $H 'qd.act' ('near:' + $doorName + '!Deactivate#' + (Get-Date -Format 'HHmmssfff'))
        Start-Sleep -Seconds 9
        Show-Since $H 'dev interact: forced verb|actor-state|interact:' $markH2 10
        Show-Since $P 'actor-state|interact:' $markP2 10
        $closeP = @(Get-Since $P $markP2 | Where-Object { $_ -match 'interact: door anim .*state=0' })
        $closePany = @(Get-Since $P $markP2 | Where-Object { $_ -match 'actor states applied=[1-9]' })
        Verdict 'door' $H (@(Get-Since $H $markH2 | Where-Object { $_ -match 'actor-state host set|actor-state local change' }).Count -gt 0) 'B closed the door and published it' ''
        Verdict 'door' $P ($closeP.Count -gt 0) 'A played the door CLOSE animation (bidirectional)' ($(if ($closeP.Count) { [string]$closeP[-1] } else { '' }))
        if ($closeP.Count -eq 0 -and $closePany.Count) { Note "  [P] $([string]$closePany[-1])" }
    }

    Step '2. key items cross; normal items do not' 'GiveAllKeys then GiveAllConsumables on the host'
    $markH = Get-Mark $H; $markP = Get-Mark $P
    Set-Control $H 'qd.cheat' 'GiveAllKeys'
    [void](Check 'quest' $H 'rig: cheat GiveAllKeys' 20 'GiveAllKeys ran' $markH)
    Start-Sleep -Seconds 7
    [void](Check 'quest' $H 'inventory host set ' 30 'the host republished its inventory' $markH)
    [void](Check 'quest' $P 'inventory grant applied: ' 40 'the peer was granted a key item' $markP)
    Show-Since $P 'inventory grant applied' $markP 6
    $keyPath = $null
    foreach ($g in @(Get-Since $P $markP)) {
        $s = [string]$g
        if ($s -notmatch 'inventory grant applied: (\S+)') { continue }
        $cand = $Matches[1]
        if ($cand -match 'KeyItems') { $keyPath = $cand; break }
    }
    Verdict 'quest' $P ($null -ne $keyPath) 'the granted path is under Items/KeyItems' $(if ($keyPath) { $keyPath } else { 'none' })

    $markH2 = Get-Mark $H; $markP2 = Get-Mark $P
    Set-Control $H 'qd.cheat' 'GiveAllConsumables'
    [void](Check 'quest' $H 'rig: cheat GiveAllConsumables' 20 'GiveAllConsumables ran' $markH2)
    Start-Sleep -Seconds 10
    Show-Since $H 'inventory host set|inventory:' $markH2 4
    Show-Since $P 'inventory grant applied|inventory:' $markP2 8
    $nonKey = @(Get-Since $P $markP2 | Where-Object { $_ -match 'inventory grant applied: ' -and $_ -notmatch '/KeyItems/' })
    Verdict 'quest' $P ($nonKey.Count -eq 0) ("a NORMAL item did not cross (non-KeyItems grants: {0})" -f $nonKey.Count) ($(if ($nonKey.Count) { [string]$nonKey[0] } else { '' }))

    Step '3. the host uses the key: what the peer does' 'qd.use on the host'
    if (-not $keyPath) { Say 'REFUSED: no key path to use' }
    else {
        $markH = Get-Mark $H; $markP = Get-Mark $P
        Say "  use[$H] -> $keyPath"
        Set-Control $H 'qd.use' $keyPath
        [void](Check 'quest' $H 'rig: use lever ->' 20 'the host consumed one through the game''s own consumer' $markH)
        Show-Since $H 'rig: use lever|inventory host set|quest item' $markH 6
        Start-Sleep -Seconds 8
        Show-Since $P 'quest item removed|inventory' $markP 8
        $rm = @(Get-Since $P $markP | Where-Object { $_ -match 'quest item removed here' })
        Verdict 'quest' $P ($rm.Count -gt 0) 'the host''s use removed the peer''s copy' ($(if ($rm.Count) { [string]$rm[-1] } else { 'no removal (the host dropped to 0, which this lane cannot express)' }))
    }

    Step '4. boot: which XP counter resolved' 'the fix probes the pawn then the SAVE'
    foreach ($i in @($H, $P)) { $m = @(Get-Matches $i 'xp: the local player'); if ($m.Count) { Note "  [$i] $([string]$m[-1])" } }
    $xpProp = $null
    foreach ($i in @($H, $P)) {
        foreach ($l in @(Get-Matches $i 'xp: the local player.s counter is')) {
            if (([string]$l) -match "counter is (\w+)") { $xpProp = $Matches[1]; break }
        }
        if ($xpProp) { break }
    }

    Step '5. a kill on A pays BOTH players' 'FS_Cheats.KillAllEnemies'
    if (-not $xpProp) { Say 'REFUSED: no XP counter resolved (see the xp: line above)' }
    else {
        foreach ($killer in @($P, $H)) {
            $other = if ($killer -eq $P) { $H } else { $P }
            $markK = Get-Mark $killer; $markO = Get-Mark $other
            # MEASURED in session_final: `KillAllEnemies` ran and produced NO `xp:` line --
            # it kills without writing an enemy save state, and `ProgressionOnDeath` is only
            # reached from that funnel.  So the kill is made through the module's OWN damage
            # path, which IS the funnel that records.
            $ne = @(Get-Matches $killer 'scan: nearest enemy')
            $coord = $null
            if ($ne.Count -and ([string]$ne[-1]) -match 'at (-?[\d.]+),(-?[\d.]+),(-?[\d.]+)') {
                $coord = "$($Matches[1]),$($Matches[2]),$($Matches[3])"
            }
            if (-not $coord) { Say 'REFUSED: no enemy position for this machine'; continue }
            Say ("  tp[{0}] -> {1} (the nearest enemy)" -f $killer, $coord)
            Set-Control $killer 'qd.tp' $coord
            Start-Sleep -Seconds 8
            Say ("  hit[{0}] -> 400" -f $killer)
            Set-Control $killer 'qd.hit' '400'
            Start-Sleep -Seconds 22
            Set-Control $killer 'qd.hit' $null
            Show-Since $killer 'xp:' $markK 8
            Show-Since $other 'xp:' $markO 8
            $paidHere = @(Get-Since $killer $markK | Where-Object { $_ -match 'xp: this death paid' })
            $payThere = @(Get-Since $other $markO | Where-Object { $_ -match 'xp: the partner.s death paid' })
            $gave = @(Get-Since $other $markO | Where-Object { $_ -match 'xp: paid .*\(' })
            Verdict 'xp' $killer ($paidHere.Count -gt 0) 'the killer''s game paid and measured it' ($(if ($paidHere.Count) { [string]$paidHere[-1] } else { '' }))
            Verdict 'xp' $other ($payThere.Count -gt 0) 'the other machine decided to pay the shortfall' ($(if ($payThere.Count) { [string]$payThere[-1] } else { '' }))
            Verdict 'xp' $other ($gave.Count -gt 0) 'the other player''s counter rose (the read-back pair)' ($(if ($gave.Count) { [string]$gave[-1] } else { '' }))
        }
    }

    Step '6. a save on A writes B''s own slot' 'SaveHere, then the checkpoint if that dispatched nothing'
    $before = Show-Stamps $P 'before'
    $markH = Get-Mark $H; $markP = Get-Mark $P
    Set-Control $H 'qd.cheat' 'SaveHere'
    [void](Check 'save' $H 'rig: cheat SaveHere' 20 'SaveHere ran' $markH)
    $said = @(Wait-Since $H 'save: this machine saved' $markH 32).Count -gt 0
    Show-Since $H 'save:' $markH 6
    if (-not $said) {
        Say '  SaveHere dispatched no save; pressing the nearest checkpoint (the player route)'
        $markH = Get-Mark $H
        Set-Control $H 'qd.act' ('near:Checkpoint!Interact#' + (Get-Date -Format 'HHmmssfff'))
        Start-Sleep -Seconds 10
        Show-Since $H 'dev interact:|save:|checkpoint' $markH 10
        $said = @(Get-Since $H $markH | Where-Object { $_ -match 'save: this machine saved' }).Count -gt 0
    }
    Verdict 'save' $H $said 'a save dispatch was detected on A' ''
    [void](Check 'save' $P 'save: the partner saved|save: wrote this machine' 45 'B heard A''s save' $markP)
    Show-Since $P 'save:' $markP 8
    Start-Sleep -Seconds 4
    $after = Show-Stamps $P 'after'
    $moved = $false
    foreach ($a in @($after)) {
        $b = @($before | Where-Object { $_.Name -eq $a.Name })
        if ($b.Count -and $b[0].Stamp -ne $a.Stamp) { $moved = $true; Note ("    {0} moved {1} -> {2}" -f $a.Name, $b[0].Stamp, $a.Stamp) }
    }
    Verdict 'save' $P $moved 'B''s own save file moved on disk' ''
    $echo = @(Get-Since $P $markP | Where-Object { $_ -match 'save: this machine saved' })
    Verdict 'save' $P ($echo.Count -eq 0) 'the mirrored write did not bounce back' ($(if ($echo.Count) { [string]$echo[0] } else { '' }))

    Step '7. survival'
    foreach ($i in @($H, $P)) {
        Verdict 'alive' $i (Test-Alive $i) 'still running' ''
        $fault = @(Get-RunLines $i | Where-Object { $_ -match '(\sFAULT\b)|Unhandled Exception|ACCESS_VIOLATION|NULL_CLASS_PTR' })
        Verdict 'alive' $i ($fault.Count -eq 0) 'no FAULT line' ($(if ($fault.Count) { [string]$fault[-1] } else { '' }))
    }
    HB 'final done'
}

function Write-Summary {
    Note ''; Note '================ SUMMARY ================'
    $pass = @($script:verdicts | Where-Object { $_.Ok }).Count
    $fail = @($script:verdicts | Where-Object { -not $_.Ok }).Count
    foreach ($v in $script:verdicts) {
        $ln = [string]$v.Line; if ($ln.Length -gt 220) { $ln = $ln.Substring(0,220) + ' ...' }
        Note ("  {0} {1,-6} [{2}] {3}{4}" -f $(if ($v.Ok) { 'PASS' } else { 'FAIL' }), $v.Step, $v.Inst, $v.What, $(if ($ln) { " -- $ln" } else { '' }))
    }
    Note ("  {0} PASS / {1} FAIL" -f $pass, $fail)
    $script:lines | Set-Content -LiteralPath $Report
    Say "report: $Report"
}

try {
    Say "session_final phase=$Phase rig=$rigRoot"
    Seed-FreshPair
    Start-Boot $Phase
    Invoke-Final
    Write-Summary
} catch {
    Say ("UNHANDLED: " + $_.Exception.Message)
    Say ("AT: " + $_.InvocationInfo.PositionMessage)
    try { $script:lines | Set-Content -LiteralPath $Report } catch {}
    Stop-All
    exit 1
} finally {
    try { Stop-All } catch {}
    try { if (-not (Test-Path $Report)) { $script:lines | Set-Content -LiteralPath $Report } } catch {}
}
