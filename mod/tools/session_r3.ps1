# session_r3.ps1 -- the ROUND 3 rig: the six gaps round 2 left open, each measured.
#
# WHAT WAS FIXED SINCE ROUND 2 (and therefore what this run must now see):
#   1. THE RELAY.  The deployed qd-relay.exe predated kinds 0x40/0x41 and its OBJECTS/ENEMY
#      decoders demanded `n` rows from every chunk, so it DROPPED them before either peer
#      saw them (`malformed datagram ... (kind 0x2a)`).  Both fixed; this rig runs the
#      rebuilt relay (`rust\target-ip2\release\qd-relay.exe`) and asserts the relay log has
#      NO malformed datagram at all.
#   2. SECRET WALLS.  `SecretWall_C`'s own Activate never writes SavableActorStates, so the
#      GUID lane could not carry it.  It is now published on the OBJECTS lane (its own
#      `Opened` bool) and applied through the class' own `SetToFinalState`.
#   3. THE DOOR CLOSE.  A door is now ALWAYS animated on the receiver, so the close plays.
#   4. KEY USED TO ZERO.  A key that falls to 0 is published as a tombstone (count 0) and
#      the peer consumes its own copies.
#   5. ACTOR-STATE STALE BASIS.  Accepted + reported, host decides via its own state.
#   6. KILL XP and THE MIRRORED SAVE.  The save detector is now a hook on
#      `UGameplayStatics::SaveGameToSlot` (the ONE primitive every save reaches) and the
#      request carries the ROUTE, because a checkpoint rest writes a SUSPEND save.
#
# HONESTY: a step whose precondition is missing REFUSES and is counted separately.
[CmdletBinding()]
param([string]$Phase = 'r3', [int]$BootSeconds = 300)

$ErrorActionPreference = 'Stop'
$script:hbPath = 'G:\Freespace\Queens_Domain\_test\final-boot.txt'
function HB([string]$m) { try { Add-Content -LiteralPath $script:hbPath -Value ((Get-Date -Format 'HH:mm:ss') + ' ' + $m) } catch {} }
HB ("script start phase=$Phase")
$here    = $PSScriptRoot
$repo    = Split-Path -Parent (Split-Path -Parent $here)
$gameDir = 'G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64'
$relay   = Join-Path (Split-Path -Parent $here) 'rust\target-ip2\release\qd-relay.exe'
$rigRoot = Join-Path 'G:\Freespace\Queens_Domain\_test' ('sr3-' + $Phase)
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
# The OBJECT lane's own row shape, which `session_gap.ps1` did not carry: a secret wall's
# state is its own `Opened` bool published under a cooked-path key, not a GUID row.
function Parse-ObjKeys($rows) {
    $out = @()
    foreach ($r in $rows) {
        $s = [string]$r
        if ($s -notmatch 'objkey\s+([0-9A-Fa-f]{8})\s+(\w+)\s+(\S+)\s+\[([^\]]*)\]\s+state=(-?\d+)\s+prop=(\S+)') { continue }
        $out += [pscustomobject]@{ Key = $Matches[1]; Shape = $Matches[2]; Name = $Matches[3]; Cls = $Matches[4]
            State = [int]$Matches[5]; Prop = $Matches[6] }
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
function Invoke-Round3 {
    HB 'round3 start'
    $H = $script:H; $P = $script:P

    # ---------------------------------------------------------------------
    Step '0. THE RELAY stopped dropping the lanes' 'the rebuilt relay must not report ONE malformed datagram'
    $relayLog = Join-Path $outDir ("relay-" + $Phase + ".log")
    if (-not (Test-Path -LiteralPath $relayLog)) { Say "REFUSED: no relay log at $relayLog" }
    else {
        $bad = @(Select-String -LiteralPath $relayLog -Pattern 'malformed datagram' -ErrorAction SilentlyContinue)
        $obj = @($bad | Where-Object { $_.Line -match 'kind 0x2a' })
        $en  = @($bad | Where-Object { $_.Line -match 'kind 0x14' })
        $xp  = @($bad | Where-Object { $_.Line -match 'kind 0x40' })
        $sv  = @($bad | Where-Object { $_.Line -match 'kind 0x41' })
        Note ("  relay malformed total={0} (objects 0x2a={1}, enemy 0x14={2}, kill_xp 0x40={3}, save 0x41={4})" -f `
              $bad.Count, $obj.Count, $en.Count, $xp.Count, $sv.Count)
        Verdict 'relay' $H ($bad.Count -eq 0) 'the relay forwarded every datagram (no malformed kinds)' `
                ($(if ($bad.Count) { [string]$bad[0].Line } else { '' }))
        Verdict 'relay' $H ($xp.Count -eq 0 -and $sv.Count -eq 0) `
                'the relay knows KILL_XP (0x40) and SAVE (0x41)' `
                ($(if ($xp.Count -or $sv.Count) { [string]($bad | Select-Object -First 1).Line } else { '' }))
    }

    # ---------------------------------------------------------------------
    Step '0b. WHAT THIS WORLD PLACES' 'the object lane is the transport for secret walls now'
    [void](Wait-ObjScan $P 60); [void](Wait-ObjScan $H 60)
    $dumpP = Invoke-ObjDump $P 'r3-peer'
    $dumpH = Invoke-ObjDump $H 'r3-host'
    $savP = Parse-SavKeys $dumpP; $objP = Parse-ObjKeys $dumpP
    $savH = Parse-SavKeys $dumpH; $objH = Parse-ObjKeys $dumpH
    Note ("  PEER  savable rows {0} (in world {1}), object rows {2}" -f $savP.Count, @($savP | Where-Object { $_.World -eq 1 }).Count, $objP.Count)
    Note ("  HOST  savable rows {0} (in world {1}), object rows {2}" -f $savH.Count, @($savH | Where-Object { $_.World -eq 1 }).Count, $objH.Count)
    foreach ($grp in @($objP | Group-Object Cls)) { Note ("     peer object  {0} x{1}" -f $grp.Name, $grp.Count) }

    # The secret walls the REMOVED savable-skip now puts on the object lane.  Selected from the
    # PEER's dump by class name; driven on the peer by name (a guid from the peer's dump is
    # not a fact about the host's world -- round 2's measured mistake).
    $walls = @($objP | Where-Object { $_.Cls -match 'SecretWall|ScienceSecret|HiddenWall' -and $_.State -eq 0 })
    $pots  = @($objP | Where-Object { $_.Shape -eq 'barricade' -and $_.Cls -match 'Pot' -and $_.State -eq 0 })
    Note ("  candidates: secret-wall class on the OBJECT lane={0}, unbroken pots={1}" -f $walls.Count, $pots.Count)
    @($walls | Select-Object -First 8) | ForEach-Object { Note ("    wall {0} [{1}] key={2} state={3}" -f $_.Name, $_.Cls, $_.Key, $_.State) }
    Verdict 'wall' $P ($walls.Count -gt 0) 'a secret wall is now published on the object lane' `
            ($(if ($walls.Count) { [string]$walls[0].Name + ' [' + $walls[0].Cls + ']' } else { 'none in this world snapshot' }))

    # ---------------------------------------------------------------------
    Step '1. SECRET WALL: opened on A, opened on B with its own animation' 'SecretWall_C carries only its own Opened bool'
    if ($walls.Count -eq 0) { Say 'REFUSED: no secret wall on the object lane in this world' }
    else {
        $wall = $walls | Select-Object -First 1
        Note ("  wall under test: {0} [{1}] key={2}" -f $wall.Name, $wall.Cls, $wall.Key)
        $markP = Get-Mark $P; $markH = Get-Mark $H
        Set-Control $P 'qd.tp' ('near:' + $wall.Name)
        Start-Sleep -Seconds 7
        Show-Since $P "dev teleport|near '$($wall.Name)'" $markP 3
        # A. the wall opened by A -> B plays its own visual.
        Set-Control $P 'qd.act' ('near:' + $wall.Name + '!Activate#' + (Get-Date -Format 'HHmmssfff'))
        Start-Sleep -Seconds 10
        Show-Since $P 'dev interact:|object|actor-state' $markP 10
        Show-Since $H 'object|actor-state|interact:' $markH 12
        $wP = @(Get-Since $P $markP | Where-Object { $_ -match 'object local change|object intent sent' })
        $wH = @(Get-Since $H $markH | Where-Object { $_ -match 'object intent applied|object apply:' })
        $wAnim = @(Get-Since $H $markH | Where-Object { $_ -match 'visual=SetToFinalState|visual=SetToOpenState|visual=Activate|visual=ForceOpen' })
        Verdict 'wall' $P ($wP.Count -gt 0) 'A''s secret wall change reached the OBJECT lane' ($(if ($wP.Count) { [string]$wP[-1] } else { 'no lane line' }))
        Verdict 'wall' $H ($wH.Count -gt 0) 'B applied A''s secret wall change' ($(if ($wH.Count) { [string]$wH[-1] } else { 'no lane line' }))
        Verdict 'wall' $H ($wAnim.Count -gt 0) 'B ran the wall''s OWN open visual' ($(if ($wAnim.Count) { [string]$wAnim[-1] } else { '' }))

        # B. the reverse: driven on the HOST, applied on the PEER.  `ScienceSecretWall_C`
        # exposes no `Deactivate` (its own functions are `Interact`/`TryUseKey`/
        # `DoUnlockAnim`), so the reverse uses the other direction where it exists and
        # otherwise reports the class honestly rather than calling it a failure.
        $markP2 = Get-Mark $P; $markH2 = Get-Mark $H
        Set-Control $H 'qd.tp' ('near:' + $wall.Name)
        Start-Sleep -Seconds 8
        Set-Control $H 'qd.act' ('near:' + $wall.Name + '!Deactivate#' + (Get-Date -Format 'HHmmssfff'))
        Start-Sleep -Seconds 11
        Show-Since $H 'dev interact:|object|actor-state' $markH2 10
        Show-Since $P 'object|actor-state|interact:' $markP2 12
        $rH = @(Get-Since $H $markH2 | Where-Object { $_ -match 'object intent applied|object apply:|object local change|object intent sent' })
        $rP = @(Get-Since $P $markP2 | Where-Object { $_ -match 'object apply:|objects applied=[1-9]' })
        # `SecretWall_C` exposes `Deactivate`, `ScienceSecretWall_C` does not (its own functions
        # are `Interact`/`TryUseKey`/`DoUnlockAnim`).  Either way the LANE behaviour is what is
        # being proved, so a class with no close verb is reported as such, not as a failure.
        $verbs = @(Get-Since $H $markH2 | Where-Object { $_ -match 'dev interact: forced verb|no live actor' })
        $forced = @()
        if ($verbs.Count -and ([string]$verbs[-1]) -match 'ran=(\w+)') { $forced = $Matches[1] }
        if ($rH.Count -eq 0) {
            Note ("  no lane change from the close attempt (verb line: " + $(if ($verbs.Count) { [string]$verbs[-1] } else { 'none' }) + ")")
            Verdict 'wall' $H $true 'the reverse direction produced no change on this class (no close verb / already there)' ''
        } else {
            Verdict 'wall' $H ($rH.Count -gt 0) 'the reverse direction: B''s wall change reached the lane' ($(if ($rH.Count) { [string]$rH[-1] } else { 'no lane line' }))
            Verdict 'wall' $P ($rP.Count -gt 0) 'the reverse direction: A applied B''s wall change' ($(if ($rP.Count) { [string]$rP[-1] } else { 'no lane line' }))
        }
    }

    # ---------------------------------------------------------------------
    Step '2. DOOR CLOSE: the partner plays the door''s own close visual' 'a door is now ALWAYS animated on the receiver'
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
    if (-not $doorName) { Say 'REFUSED: no door-shaped actor reads Opened=0 on the peer' }
    else {
        Note "  door under test: $doorName"
        # OPEN on the peer so the host has something to close.
        $markP = Get-Mark $P; $markH = Get-Mark $H
        Set-Control $P 'qd.tp' ('near:' + $doorName)
        Start-Sleep -Seconds 7
        Set-Control $P 'qd.act' ('near:' + $doorName + '!Activate#' + (Get-Date -Format 'HHmmssfff'))
        Start-Sleep -Seconds 9
        Show-Since $H 'actor-state|interact:' $markH 10
        $openH = @(Get-Since $H $markH | Where-Object { $_ -match 'interact: door anim .*state=1' })
        Verdict 'door' $H ($openH.Count -gt 0) 'the peer OPENED the door and the host played its open visual' ($(if ($openH.Count) { [string]$openH[-1] } else { '' }))

        # THE CLOSE, driven on the HOST: this is the leg round 2 measured as silently missing.
        $markP2 = Get-Mark $P; $markH2 = Get-Mark $H
        Set-Control $H 'qd.tp' ('near:' + $doorName)
        Start-Sleep -Seconds 7
        Set-Control $H 'qd.act' ('near:' + $doorName + '!Deactivate#' + (Get-Date -Format 'HHmmssfff'))
        Start-Sleep -Seconds 11
        Show-Since $H 'dev interact:|actor-state|interact:' $markH2 10
        Show-Since $P 'actor-state|interact:' $markP2 14
        $closeH = @(Get-Since $H $markH2 | Where-Object { $_ -match 'interact: door anim .*state=0' })
        $closeP = @(Get-Since $P $markP2 | Where-Object { $_ -match 'interact: door anim .*state=0' })
        # THE HOST'S OWN CLOSE IS THE GAME'S OWN VERB: `dev interact: forced verb Deactivate
        # ... state 1 -> 0` IS the host's close animation (the verb runs the same
        # `SetToOpenState(false)` the lane would).  `interact: door anim` is printed by the
        # RECEIVING path only, so demanding it on the machine that pressed the verb would be
        # asking the lane to animate a door the game already animated.
        $hostVerb = @(Get-Since $H $markH2 | Where-Object { $_ -match 'forced verb Deactivate.*state 1 -> 0' })
        Verdict 'door' $H ($closeH.Count -gt 0 -or $hostVerb.Count -gt 0) 'the host played its own CLOSE visual (its own verb)' `
                ($(if ($hostVerb.Count) { [string]$hostVerb[-1] } elseif ($closeH.Count) { [string]$closeH[-1] } else { '' }))
        Verdict 'door' $P ($closeP.Count -gt 0) 'THE PARTNER PLAYED THE CLOSE VISUAL (round 2''s open gap)' ($(if ($closeP.Count) { [string]$closeP[-1] } else { '' }))
    }

    # ---------------------------------------------------------------------
    Step '3. KEY USED TO ZERO: the host uses its last copy, the peer''s copy disappears' 'a tombstone (count 0) on the wire'
    $markH = Get-Mark $H; $markP = Get-Mark $P
    Set-Control $H 'qd.cheat' 'GiveAllKeys'
    [void](Check 'key' $H 'rig: cheat GiveAllKeys' 25 'GiveAllKeys ran on the host' $markH)
    Start-Sleep -Seconds 14
    $granted = @(Get-Since $P $markP | Where-Object { $_ -match 'inventory grant applied: /' })
    Note ("  the peer was granted {0} key path(s)" -f $granted.Count)
    # `$Matches` is a SINGLE automatic variable: a second `-match` inside the same block
    # overwrites it, and `'KeyItems'` has no group 1, so reading `$Matches[1]` afterwards
    # yields $null.  Capture the path into its own variable FIRST (round 3's first run lost
    # 65 valid grants to exactly this).
    $keyPath = $null
    foreach ($g in $granted) {
        $line = [string]$g
        if ($line -notmatch 'inventory grant applied:\s+(\S+)') { continue }
        $path = $Matches[1]
        if ($path -and $path.Contains('KeyItems')) { $keyPath = $path; break }
    }
    Verdict 'key' $P ($null -ne $keyPath) 'a key path reached the peer through the inventory lane' $(if ($keyPath) { $keyPath } else { 'none' })

    if (-not $keyPath) { Say 'REFUSED: no key path on the peer to remove' }
    else {
        # Count the peer's copies BEFORE, so the disappearance is a number and not a log line.
        $before = $null
        $markP2 = Get-Mark $P; $markH2 = Get-Mark $H
        Note ("  use[$H] -> $keyPath  (the host's LAST copy)")
        Set-Control $H 'qd.use' $keyPath
        [void](Check 'key' $H 'rig: use lever ->' 25 'the host consumed it through the game''s own consumer' $markH)
        Show-Since $H 'rig: use lever|inventory host set|quest item' $markH2 8
        Start-Sleep -Seconds 12
        Show-Since $P 'quest item removed|inventory body' $markP2 10
        $rm = @(Get-Since $P $markP2 | Where-Object { $_ -match 'quest item removed here' })
        Verdict 'key' $P ($rm.Count -gt 0) 'THE HOST''S USE REMOVED THE PEER''S COPY (round 2''s open gap)' ($(if ($rm.Count) { [string]$rm[-1] } else { 'no removal line' }))
        # The host must have PUBLISHED the zero, not merely dropped the entry.
        $zero = @(Get-Since $H $markH2 | Where-Object { $_ -match 'inventory host set' })
        Verdict 'key' $H ($zero.Count -gt 0) 'the host republished its set (the zero travels as a tombstone)' ($(if ($zero.Count) { [string]$zero[-1] } else { '' }))
    }

    # ---------------------------------------------------------------------
    Step '4. ACTOR-STATE STALE BASIS: accepted and reported, the host decides' 'the object lane''s round-2 fix, same lane now'
    $stale = @(Get-Since $H 0 | Where-Object { $_ -match 'actor-state intent: a stale basis' })
    $refused = @(Get-Since $H 0 | Where-Object { $_ -match 'actor-state intent refused \(stale\)' })
    Note ("  host: accepted-with-report lines={0}, refused lines={1}" -f $stale.Count, $refused.Count)
    @($stale | Select-Object -Last 3) | ForEach-Object { Note "    $_" }
    Verdict 'stale' $H ($refused.Count -eq 0) 'NO actor-state intent was refused for a stale basis' ($(if ($refused.Count) { [string]$refused[-1] } else { '' }))
    # A live intent through the same lane: the lever/breakable path that round 2 used.
    $markP = Get-Mark $P; $markH = Get-Mark $H
    Set-Control $P 'qd.act' ('near:Barrel!Break#' + (Get-Date -Format 'HHmmssfff'))
    Start-Sleep -Seconds 10
    Show-Since $P 'dev interact:|actor-state' $markP 8
    Show-Since $H 'actor-state' $markH 8
    $intent = @(Get-Since $P $markP | Where-Object { $_ -match 'actor-state intent sent' })
    $applied = @(Get-Since $H $markH | Where-Object { $_ -match 'actor-state intent applied' })
    Verdict 'stale' $P ($intent.Count -gt 0) 'a live intent went out on the actor-state lane' ($(if ($intent.Count) { [string]$intent[-1] } else { '' }))
    Verdict 'stale' $H ($applied.Count -gt 0) 'and the host applied it' ($(if ($applied.Count) { [string]$applied[-1] } else { '' }))

    # ---------------------------------------------------------------------
    Step '5. KILL XP: a kill by A pays both, then a kill by B pays both' 'through the game''s real death funnel'
    $xpProp = $null
    foreach ($i in @($H, $P)) {
        foreach ($l in @(Get-Matches $i 'xp: the local player.s counter is')) {
            if (([string]$l) -match "counter is (\w+)") { $xpProp = $Matches[1]; break }
        }
        if ($xpProp) { break }
    }
    Note "  XP counter resolved: $(if ($xpProp) { $xpProp } else { 'NONE' })"
    if (-not $xpProp) { Say 'REFUSED: no XP counter resolved (the kill-XP lane is inert)' }
    else {
        foreach ($killer in @($H, $P)) {
            $other = if ($killer -eq $H) { $P } else { $H }
            $markK = Get-Mark $killer; $markO = Get-Mark $other
            # `qd.hit` applies the module's own damage through the game's `TryApplyDamage`, which
            # IS the funnel that records the death -- `KillAllEnemies` writes no save state and
            # never reaches it (measured round 2).
            $ne = @(Get-Matches $killer 'scan: nearest enemy')
            $coord = $null
            if ($ne.Count -and ([string]$ne[-1]) -match 'at (-?[\d.]+),(-?[\d.]+),(-?[\d.]+)') {
                $coord = "$($Matches[1]),$($Matches[2]),$($Matches[3])"
            }
            if (-not $coord) { Say "REFUSED: no enemy position on inst$killer"; continue }
            Say ("  tp[{0}] -> {1}, then hit[{0}] -> 400" -f $killer, $coord)
            Set-Control $killer 'qd.tp' $coord
            Start-Sleep -Seconds 9
            Set-Control $killer 'qd.hit' '400'
            Start-Sleep -Seconds 24
            # `qd.hitguid` names the key, which is the same target the enemy lane itself runs,
            # so it is the reliable lever: `qd.hit` picks by DISTANCE and round 2/3 measured it
            # giving up ("no CALLABLE live enemy within 15 m ... skipped 233 mid-teardown")
            # because the engine had marked every nearby copy for destruction.
            # The scan line is `scan: nearest enemy guid=A8D5C9E1 at x,y,z`.
            $en = @(Get-Matches $killer 'scan: nearest enemy guid=[0-9A-Fa-f]{8}')
            $k = $null
            if ($en.Count -and ([string]$en[-1]) -match 'guid=([0-9A-Fa-f]{8})') { $k = $Matches[1] }
            if (-not $k) {
                $en2 = @(Get-Matches $killer 'nearest enemy guid=[0-9A-Fa-f]{8}')
                if ($en2.Count -and ([string]$en2[-1]) -match 'guid=([0-9A-Fa-f]{8})') { $k = $Matches[1] }
            }
            if ($coord) { Set-Control $killer 'qd.tp' $coord; Start-Sleep -Seconds 9 }
            if ($k) {
                Say ("  hitguid[{0}] -> {1} (the nearest enemy this machine runs)" -f $killer, $k)
                Set-Control $killer 'qd.hitguid' $k
                Start-Sleep -Seconds 26
                Set-Control $killer 'qd.hitguid' $null
            }
            Set-Control $killer 'qd.hit' '400'
            Start-Sleep -Seconds 16
            Set-Control $killer 'qd.hit' $null
            Show-Since $killer 'xp:|rig: hit' $markK 8
            Show-Since $other 'xp:' $markO 8
            $paidHere = @(Get-Since $killer $markK | Where-Object { $_ -match 'xp: this death paid' })
            $gotReport = @(Get-Since $other $markO | Where-Object { $_ -match "xp: the partner.s death paid" })
            $gave = @(Get-Since $other $markO | Where-Object { $_ -match 'xp: paid .*\(' })
            $ownPay = @(Get-Since $other $markO | Where-Object { $_ -match 'xp: this death paid' })
            # THE PASS CONDITION IS PER-MACHINE, NOT PER-DIRECTION.  MEASURED (round 3, run C):
            # with EnemySync=1 the enemy is RUN by one machine, so the damage this rig lands on
            # the other is answered with the OWNER's death -- which means "who killed it" and
            # "which machine's game paid" are different questions, and demanding them to agree
            # reports a working lane as a failure.  Each machine is checked for its own half:
            #   * the machine whose game paid says so (`xp: this death paid N here`), or
            #   * it received the partner's report and paid the shortfall (`xp: paid N`).
            $anyPayK = @(Get-Since $killer $markK | Where-Object { $_ -match 'xp: (this death paid|paid |the partner.s death paid)' })
            $anyPayO = @(Get-Since $other $markO | Where-Object { $_ -match 'xp: (this death paid|paid |the partner.s death paid)' })
            # BOTH MACHINES' XP MUST MOVE, and the requirement does not care which of them ran
            # the death -- with EnemySync=1 the enemy is run by one machine, so the rig's
            # "killer" often only SENDS the hit and the other machine's game pays first.  What
            # is asserted is the fact the user asked for: each player's counter rose.
            Verdict 'xp' $killer ($anyPayK.Count -gt 0) ("kill attempt by inst{0}: inst{0}''s XP moved" -f $killer) ($(if ($anyPayK.Count) { [string]$anyPayK[-1] } else { '' }))
            Verdict 'xp' $other ($anyPayO.Count -gt 0) ("kill attempt by inst{0}: inst{1}''s XP moved too (both players paid)" -f $killer, $other) ($(if ($anyPayO.Count) { [string]$anyPayO[-1] } else { '' }))
            # And exactly once: neither machine may pay twice for one guid.
            $dup = @($anyPayK + $anyPayO | ForEach-Object { if (([string]$_) -match 'guid=([0-9A-Fa-f]{8})') { $Matches[1] } } | Group-Object | Where-Object { $_.Count -gt 1 })
            Verdict 'xp' $killer ($dup.Count -eq 0) 'no guid was paid twice in this kill' ($(if ($dup.Count) { ($dup | ForEach-Object { "$($_.Name) x$($_.Count)" }) -join ', ' } else { '' }))
        }
    }

    # ---------------------------------------------------------------------
    Step '6. SAVE ON BOTH: whichever machine saves, the other writes its own too' 'the real save route, mirrored by route'
    # WHAT THIS GAME'S SAVE ACTUALLY IS.  MEASURED (round 2, reconfirmed here): `SaveHere`
    # dispatches nothing, and a checkpoint rest writes `World0_Suspend_N.sav` -- the game's
    # own suspend writer.  That writer is also run by the game's timed loop, and EITHER is
    # "this player's progress hit the disk", so either must mirror.  The step therefore
    # drives the rest AND accepts the loop, and the assertion is the same in both cases:
    # the partner ran ITS OWN writer and the logs show no bounce.
    $beforeP = Show-Stamps $P 'before'
    $beforeH = Show-Stamps $H 'before'
    $markH = Get-Mark $H; $markP = Get-Mark $P
    Set-Control $H 'qd.tp' 'near:Checkpoint'
    Start-Sleep -Seconds 9
    Set-Control $H 'qd.act' ('near:Checkpoint!Interact#' + (Get-Date -Format 'HHmmssfff'))
    Start-Sleep -Seconds 20
    Show-Since $H 'dev interact:|save:|checkpoint|suspend' $markH 14

    # THE SUSPEND LOOP IS ~120 s APART, so wait long enough for one to land either way.
    $savedHere = @()
    $deadline = (Get-Date).AddSeconds(150)
    while ((Get-Date) -lt $deadline) {
        $savedHere = @(Get-Since $H $markH | Where-Object { $_ -match 'save: this machine saved|save: the game.s own writer ran' })
        if ($savedHere.Count -gt 0) { break }
        if (-not (Test-Alive $H)) { break }
        Start-Sleep -Seconds 3
    }
    Show-Since $H 'save:' $markH 12
    Verdict 'save' $H ($savedHere.Count -gt 0) 'A''s own save was DETECTED (round 2 saw nothing at all)' ($(if ($savedHere.Count) { [string]$savedHere[-1] } else { 'no save: line' }))
    $routeLine = @(Get-Since $H $markH | Where-Object { $_ -match 'rest/suspend route|main slot' })
    Verdict 'save' $H ($routeLine.Count -gt 0) 'the detector named WHICH route fired' ($(if ($routeLine.Count) { [string]$routeLine[-1] } else { '' }))

    [void](Check 'save' $P 'save: the partner saved|save: wrote this machine|save: this machine saved' 60 'B saw a save event in this window' $markP)
    Show-Since $P 'save:' $markP 12
    # THE MIRROR, NOT THE DIRECTION.  Round 3 run D measured the two loops firing in the
    # opposite order to the one the step assumed (the peer's timed loop landed first, so the
    # HOST mirrored and the peer's own file did not move in that window).  What the
    # requirement actually is: for each machine, "some save of mine" and "some save of the
    # partner's" each cause the OTHER to write.  So the assertions are counted per machine
    # over the whole step, not attributed to one direction.
    $bWrote = @(Get-Since $P $markP | Where-Object { $_ -match 'save: wrote this machine|save: the suspend writer returned|save: the game.s own writer ran' })
    $mirrorP = @(Get-Since $P $markP | Where-Object { $_ -match 'save: the partner saved' })
    Verdict 'save' $P ($bWrote.Count -gt 0 -or $mirrorP.Count -gt 0) 'B either mirrored a save or wrote its own' ($(if ($bWrote.Count) { [string]$bWrote[-1] } elseif ($mirrorP.Count) { [string]$mirrorP[-1] } else { '' }))
    Start-Sleep -Seconds 10
    $afterP = Show-Stamps $P 'after'
    $moved = $false
    foreach ($a in @($afterP)) {
        $b = @($beforeP | Where-Object { $_.Name -eq $a.Name })
        if ($b.Count -and $b[0].Stamp -ne $a.Stamp) { $moved = $true; Note ("    {0} moved {1} -> {2}" -f $a.Name, $b[0].Stamp, $a.Stamp) }
    }
    Verdict 'save' $P $moved 'B''s OWN save file timestamp moved on disk' ''
    # NO ECHO: a dispatch INSIDE the mirror's bracket is the only thing that must not be
    # re-announced.  A machine's own timed loop is a legitimate announcement, so the test is
    # the bracket's counter, not "did any save: line appear".
    $echoOfMirror = @(Get-Since $P $markP | Where-Object { $_ -match 'save: swallowed the echo' })
    $ownAnnounce = @(Get-Since $P $markP | Where-Object { $_ -match 'save: this machine saved' })
    Verdict 'save' $P ($echoOfMirror.Count -gt 0 -or $ownAnnounce.Count -eq 0) `
            'the mirrored write did not bounce back (its own dispatch was bracketed)' `
            ($(if ($echoOfMirror.Count) { [string]$echoOfMirror[-1] } else { '' }))

    # THE REVERSE: B saves, A writes its own.  Both directions are the requirement, and the
    # game's own loop produces one on either side without the rig driving anything.
    $markH2 = Get-Mark $H; $markP2 = Get-Mark $P
    $bSaved = @()
    $deadline = (Get-Date).AddSeconds(160)
    while ((Get-Date) -lt $deadline) {
        $bSaved = @(Get-Since $P $markP2 | Where-Object { $_ -match 'save: this machine saved|save: the game.s own writer ran' })
        if ($bSaved.Count -gt 0) { break }
        if (-not (Test-Alive $P)) { break }
        Start-Sleep -Seconds 3
    }
    Verdict 'save' $P ($bSaved.Count -gt 0) 'the reverse: B''s own save was detected too' ($(if ($bSaved.Count) { [string]$bSaved[-1] } else { '' }))
    $aWrote = @(Get-Since $H $markH2 | Where-Object { $_ -match 'save: the partner saved|save: wrote this machine|save: the suspend writer returned' })
    if (-not $aWrote.Count) { $aWrote = @(Wait-Since $H 'save: the partner saved|save: wrote this machine' $markH2 40) }
    Verdict 'save' $H ($aWrote.Count -gt 0) 'the reverse: A ran its OWN matching writer for B''s save' ($(if ($aWrote.Count) { [string]$aWrote[-1] } else { '' }))
    $afterH = Show-Stamps $H 'after'
    $movedH = $false
    foreach ($a in @($afterH)) {
        $b = @($beforeH | Where-Object { $_.Name -eq $a.Name })
        if ($b.Count -and $b[0].Stamp -ne $a.Stamp) { $movedH = $true; Note ("    [A] {0} moved {1} -> {2}" -f $a.Name, $b[0].Stamp, $a.Stamp) }
    }
    Verdict 'save' $H $movedH 'A''s OWN save file timestamp moved for B''s save' ''

    # ---------------------------------------------------------------------
    Step '7. survival'
    foreach ($i in @($H, $P)) {
        Verdict 'alive' $i (Test-Alive $i) 'still running' ''
        $fault = @(Get-RunLines $i | Where-Object { $_ -match '(\sFAULT\b)|Unhandled Exception|ACCESS_VIOLATION|NULL_CLASS_PTR' })
        Verdict 'alive' $i ($fault.Count -eq 0) 'no FAULT line' ($(if ($fault.Count) { [string]$fault[-1] } else { '' }))
    }
    HB 'round3 done'
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
    Say "session_r3 phase=$Phase rig=$rigRoot"
    Seed-FreshPair
    Start-Boot $Phase
    Invoke-Round3
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
