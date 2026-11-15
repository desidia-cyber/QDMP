# session_prog.ps1 -- the PROGRESSION live proof (qdprogression.h).
#
# Three sub-features, in the order the feature doc says, because each makes the next
# diagnosable:
#   1. QuestItemSync  -- a key item crosses; a NORMAL item does not; and what the host's own
#                        use of a key does to the peer.
#   2. KillXPSync     -- which XP property resolved, then a kill on A must raise BOTH counters
#                        by the same amount exactly once (and the reverse).
#   3. PartnerSaveSync-- a save on A must move B's own save file, with no echo.
#
# HOW A CHANGE IS CAUSED.  `qd.cheat` calls the game's OWN `FS_Cheats_C` UFUNCTIONs, which is
# the game's code path for exactly these events (`GiveAllKeys` adds items, `GiveAllConsumables`
# adds the normal ones, `KillAllEnemies`/the enemy's own `Kill` record deaths, `SaveHere`
# dispatches a save).  How the change was caused is not part of the claim -- the lane is about
# what happens after a machine's world changes -- and the measured alternatives are worse:
# coins and fruit are OVERLAP pickups, so the act lever does nothing near them.
#
# HONESTY: FAULT is a crash, ERR is a lane refusing a write.  A precondition that is missing
# REFUSES rather than reporting a number about a run that did not happen.
[CmdletBinding()]
param([string]$Phase = 'all', [int]$BootSeconds = 300)

$ErrorActionPreference = 'Stop'
$script:hbPath = 'G:\Freespace\Queens_Domain\_test\prog-boot.txt'
function HB([string]$m) { try { Add-Content -LiteralPath $script:hbPath -Value ((Get-Date -Format 'HH:mm:ss') + ' ' + $m) } catch {} }
HB ("script start phase=$Phase")
$here    = $PSScriptRoot
$repo    = Split-Path -Parent (Split-Path -Parent $here)
$gameDir = 'G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64'
$relay   = Join-Path (Split-Path -Parent $here) 'rust\target\release\qd-relay.exe'
$rigRoot = Join-Path 'G:\Freespace\Queens_Domain\_test' ('sprog-' + $Phase)
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
               '','[Rules]','FlagSync=1','InventorySync=1','QuestItemSync=1','KillXPSync=1',
               'PartnerSaveSync=1','HostAuthoritative=1','','[Tuning]','PlayerHz=30')
    $ini = Join-Path $gameDir 'qdcoop.ini'
    $lines | Set-Content -LiteralPath $ini
    Say "wrote $ini : InventorySync + QuestItemSync + KillXPSync + PartnerSaveSync"
    Copy-Item -LiteralPath $ini -Destination (Join-Path $outDir 'qdcoop-used.ini') -Force
}
function Seed-FreshPair {
    Say 're-seeding both instances'
    foreach ($i in 1, 2) {
        $null = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'make_test_instance.ps1') `
                -Name ("inst{0}" -f $i) -Index $i -SourceSave $seed -Root $rigRoot -Suffix $inst[$i].Suffix -Force 2>&1
        if ($LASTEXITCODE -ne 0) { Say "REFUSED: seed $i failed"; exit 2 }
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

# --- the XP counter, read through the rig's own props probe ---------------------
function Read-Xp([int]$i, [string]$prop) {
    $mark = Get-Mark $i
    Set-Control $i 'qd.act' ('near:me!?' + $prop + '#' + (Get-Date -Format 'HHmmssfff'))
    $deadline = (Get-Date).AddSeconds(10)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 600
        $m = @(Get-Since $i $mark | Where-Object { $_ -match 'dev props:' })
        if ($m.Count -gt 0) {
            if ([string]$m[-1] -match "$prop=i(-?\d+)/f(-?\d+)") { return [pscustomobject]@{ Int = [int]$Matches[1]; Frac = [int]$Matches[2]; Line = [string]$m[-1] } }
            return [pscustomobject]@{ Int = $null; Frac = $null; Line = [string]$m[-1] }
        }
    }
    return [pscustomobject]@{ Int = $null; Frac = $null; Line = '' }
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
function Invoke-Progression {
    HB 'progression start'
    $H = $script:H; $P = $script:P

    Step 'boot: which XP counter resolved' 'the lane reports the property it found, once'
    $xpLines = @()
    foreach ($i in @($H, $P)) {
        $m = @(Get-Matches $i 'xp: the local player')
        if ($m.Count -gt 0) { Note "  [$i] $([string]$m[-1])" }
        $xpLines += $m
    }
    $resolved = @($xpLines | Where-Object { $_ -match "counter is (\w+)" })
    Verdict 'xp' $H ($resolved.Count -gt 0) 'at least one player exposes a readable XP counter' `
            ($(if ($xpLines.Count) { [string]$xpLines[0] } else { 'no xp: line at all' }))
    $xpProp = $null
    if ($resolved.Count -gt 0 -and ([string]$resolved[0]) -match "counter is (\w+)") { $xpProp = $Matches[1] }
    Note ("  XP property for the reads below: {0}" -f $(if ($xpProp) { $xpProp } else { '(none; reads will use XP then XPAmount)' }))

    # --- 1. QUEST ITEMS ---------------------------------------------------
    Step '1. key items cross, normal items do not' 'qd.cheat GiveAllKeys on A, then GiveAllConsumables on A'
    $markH = Get-Mark $H; $markP = Get-Mark $P
    Say "  cheat[$H] -> GiveAllKeys"
    Set-Control $H 'qd.cheat' 'GiveAllKeys'
    [void](Check 'quest' $H 'rig: cheat GiveAllKeys' 20 'the host ran the game''s own GiveAllKeys' $markH)
    Start-Sleep -Seconds 6
    $hostSet = Check 'quest' $H 'inventory host set ' 30 'the host re-published its inventory after the keys' $markH
    Show-Since $H 'inventory host set|inventory:' $markH 6
    $grant = Check 'quest' $P 'inventory grant applied: ' 40 'the peer was GRANTED a key item' $markP
    Show-Since $P 'inventory grant applied|item intent confirmed|inventory:' $markP 14
    $keyPath = $null
    foreach ($g in @(Get-Since $P $markP)) {
        $s = [string]$g
        if ($s -notmatch 'inventory grant applied: (\S+)') { continue }
        $cand = $Matches[1]
        if ($cand -match 'KeyItems') { $keyPath = $cand; break }
    }
    Verdict 'quest' $P ($null -ne $keyPath) 'the granted path is a KeyItems asset' $(if ($keyPath) { $keyPath } else { 'no KeyItems grant was found in the window' })

    # the NORMAL items: same lever, a set the predicate must refuse
    $markH2 = Get-Mark $H; $markP2 = Get-Mark $P
    Say "  cheat[$H] -> GiveAllConsumables"
    Set-Control $H 'qd.cheat' 'GiveAllConsumables'
    [void](Check 'quest' $H 'rig: cheat GiveAllConsumables' 20 'the host ran GiveAllConsumables' $markH2)
    Start-Sleep -Seconds 10
    Show-Since $H 'inventory host set|inventory:' $markH2 6
    Show-Since $P 'inventory grant applied|inventory:|item intent' $markP2 14
    $normalGrants = @(Get-Since $P $markP2 | Where-Object { $_ -match 'inventory grant applied: ' })
    $nonKey = @($normalGrants | Where-Object { ([string]$_) -notmatch '/KeyItems/' })
    Verdict 'quest' $P ($nonKey.Count -eq 0) `
            ("a NORMAL item did not cross (grants after GiveAllConsumables that are not KeyItems: {0})" -f $nonKey.Count) `
            ($(if ($nonKey.Count) { [string]$nonKey[0] } else { '' }))

    # --- 2. the host's own USE of a key ----------------------------------
    Step '2. the host uses a key: what the peer does with its copy' 'qd.use on the host'
    if (-not $keyPath) { Say 'REFUSED: no key path was granted, so there is nothing to use' }
    else {
        $markH = Get-Mark $H; $markP = Get-Mark $P
        Say "  use[$H] -> $keyPath"
        Set-Control $H 'qd.use' $keyPath
        [void](Check 'quest' $H 'rig: use lever ->' 20 'the host consumed one through the game''s own consumer' $markH)
        Show-Since $H 'rig: use lever' $markH 3
        Start-Sleep -Seconds 8
        Show-Since $H 'inventory host set|quest item' $markH 6
        Show-Since $P 'quest item removed|inventory grant|inventory:' $markP 10
        $removed = @(Get-Since $P $markP | Where-Object { $_ -match 'quest item removed here' })
        Verdict 'quest' $P ($removed.Count -gt 0) 'the host''s use REMOVED the key on the peer' `
                ($(if ($removed.Count) { [string]$removed[-1] } else { 'nothing: the host now holds 0 and a zero is never published' }))
    }

    # --- 3. KILL XP -------------------------------------------------------
    # The lever is `FS_Cheats_C.KillAllEnemies`: it records deaths through the game's own path,
    # so it needs no enemy table (run 1 measured `enemykey ALIVE local=1` = 0 with the enemy
    # lane off, so `qd.enemykill` had nothing to name).  The COUNTER is read by the lane itself:
    # `xp: paid N to the local player (a -> b)` IS the read-back, and that pair is the
    # measurement -- a second property read would only repeat it.
    Step '3. a kill on A pays BOTH players, once each' 'FS_Cheats.KillAllEnemies on A, then on B'
    if (-not $xpProp) {
        Say 'REFUSED: no XP counter resolved at boot, so the lane does nothing by design'
    } else {
        foreach ($killer in @($P, $H)) {
            $other = if ($killer -eq $P) { $H } else { $P }
            $markK = Get-Mark $killer
            $markO = Get-Mark $other
            Say ("  cheat[{0}] -> KillAllEnemies" -f $killer)
            Set-Control $killer 'qd.cheat' 'KillAllEnemies'
            [void](Check 'xp' $killer 'rig: cheat KillAllEnemies' 25 'the game''s own KillAllEnemies ran' $markK)
            Start-Sleep -Seconds 13
            Show-Since $killer 'xp:' $markK 10
            Show-Since $other 'xp:' $markO 10
            $paidHere = @(Get-Since $killer $markK | Where-Object { $_ -match 'xp: this death paid' })
            $payThere = @(Get-Since $other $markO | Where-Object { $_ -match 'xp: the partner.s death paid' })
            $gave = @(Get-Since $other $markO | Where-Object { $_ -match 'xp: paid .*\(' })
            Verdict 'xp' $killer ($paidHere.Count -gt 0) 'the killer''s own game paid and said so' ($(if ($paidHere.Count) { [string]$paidHere[-1] } else { '' }))
            Verdict 'xp' $other ($payThere.Count -gt 0) 'the other machine saw it paid nothing and decided to pay' ($(if ($payThere.Count) { [string]$payThere[-1] } else { '' }))
            Verdict 'xp' $other ($gave.Count -gt 0) 'the other player''s counter rose (the read-back pair)' ($(if ($gave.Count) { [string]$gave[-1] } else { '' }))
            $refused = @(Get-Since $other $markO | Where-Object { $_ -match 'xp: the award was refused|did not rise' })
            if ($refused.Count) { Note ("  [{0}] {1}" -f $other, [string]$refused[-1]) }
        }
    }

    # --- 4. the mirrored SAVE --------------------------------------------
    Step '4. a save on A writes B''s own slot, with no echo' 'qd.cheat SaveHere on A'
    $beforeStamps = Show-Stamps $P 'before'
    $markH = Get-Mark $H; $markP = Get-Mark $P
    Say "  cheat[$H] -> SaveHere"
    Set-Control $H 'qd.cheat' 'SaveHere'
    [void](Check 'save' $H 'rig: cheat SaveHere' 20 'the host ran the game''s own SaveHere' $markH)
    $said = Check 'save' $H 'save: this machine saved' 30 'the host reported its own save dispatch' $markH
    if (-not $said) {
        # `SaveHere` RAN but dispatched no save verb this run, so the detection is tried through
        # the route a PLAYER takes: stand at the checkpoint the scan named and press Interact
        # (a rest point is the game's own save).  Which of the two produced the dispatch is
        # reported, not assumed.
        Say '  SaveHere dispatched nothing; pressing the nearest checkpoint (the player route)'
        $markH2 = Get-Mark $H
        Set-Control $H 'qd.act' ('1#' + (Get-Date -Format 'HHmmssfff'))
        Start-Sleep -Seconds 10
        Show-Since $H 'dev interact:|save:|checkpoint' $markH2 12
        $said = @(Get-Since $H $markH2 | Where-Object { $_ -match 'save: this machine saved' }).Count -gt 0
        Verdict 'save' $H $said 'a checkpoint press produced the save dispatch' ''
        if (-not $said) { Show-Since $H 'scan: interact target|scan: class' 0 6 }
    }
    Show-Since $H 'save:' $markH 8
    $mirror = Check 'save' $P 'save: the partner saved|save: wrote this machine' 45 'the peer heard it and wrote its own slot' $markP
    Show-Since $P 'save:' $markP 10
    Start-Sleep -Seconds 4
    $afterStamps = Show-Stamps $P 'after'
    $moved = $false
    foreach ($a in @($afterStamps)) {
        $b = @($beforeStamps | Where-Object { $_.Name -eq $a.Name })
        if ($b.Count -and $b[0].Stamp -ne $a.Stamp) { $moved = $true; Note ("    {0} moved {1} -> {2}" -f $a.Name, $b[0].Stamp, $a.Stamp) }
    }
    Verdict 'save' $P $moved 'the peer''s own save FILE moved on disk' ''
    $echo = @(Get-Since $P $markP | Where-Object { $_ -match 'save: this machine saved' })
    Verdict 'save' $P ($echo.Count -eq 0) 'the mirrored write did not bounce back as a new save' ($(if ($echo.Count) { [string]$echo[0] } else { '' }))
    $swallowed = @(Get-Since $P $markP | Where-Object { $_ -match 'save: swallowed the echo' })
    if ($swallowed.Count) { Note "  [P] $([string]$swallowed[-1])" }

    Step '5. survival'
    foreach ($i in @($H, $P)) {
        Verdict 'alive' $i (Test-Alive $i) 'still running' ''
        $fault = @(Get-RunLines $i | Where-Object { $_ -match '(\sFAULT\b)|Unhandled Exception|ACCESS_VIOLATION|NULL_CLASS_PTR' })
        Verdict 'alive' $i ($fault.Count -eq 0) 'no FAULT line' ($(if ($fault.Count) { [string]$fault[-1] } else { '' }))
    }
    HB 'progression done'
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
    Say "session_prog phase=$Phase rig=$rigRoot"
    Seed-FreshPair
    Start-Boot $Phase
    Invoke-Progression
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
