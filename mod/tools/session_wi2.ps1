# session_wi2.ps1 -- WORLD-INTERACT live proof, run 2 (run 1 taught the script what the rig
# actually says, and this run is written against that).
#
# WHAT RUN 1 MEASURED (and why this file exists rather than an edit):
#   * the switch line reads `objects=1 actors=1 interact=1` -- the lanes are on;
#   * the object lane DISCOVERS INCREMENTALLY, so a dump taken the instant gameplay starts
#     reports `tracked 0` and the walk then adds 82 pots over the next two seconds.  This run
#     waits for `object sync: tracking` before it dumps, and dumps twice;
#   * a `savkey` guid is the game's FOUR-part form; the rig's `guid:` lever takes the FIRST
#     component.  Run 1's parser expected 8 hex and matched nothing;
#   * the boot world places NO savable door and NO savable barrel in this world -- only four
#     `SecretWall_C` in 700_SZ_GR and 82 pots on the object lane -- so the doors and the
#     barrel/crate families have to be PROBED by name and by level pin, not assumed.
#
# WHAT THIS RUN ASSERTS
#   * a pot broken on A breaks on B, and B DOES NOT SPAWN A DROP (the documented trap);
#   * a secret wall moved on A moves on B, and closes again (bidirectional);
#   * what the door-shaped classes actually expose, read off the class, and whether the
#     real interaction press or the class' own verb changes anything the lane can see.
#
# HONESTY: a step whose precondition is missing REFUSES.  FAULT is a crash; ERR is a lane
# refusing a write.  They are counted separately.
[CmdletBinding()]
param([string]$Phase = 'all', [int]$BootSeconds = 300)

$ErrorActionPreference = 'Stop'
$script:hbPath = 'G:\Freespace\Queens_Domain\_test\wi2-boot.txt'
function HB([string]$m) {
    try { Add-Content -LiteralPath $script:hbPath -Value ((Get-Date -Format 'HH:mm:ss') + ' ' + $m) } catch {}
}
HB ("script start (phase=$Phase)")
$here    = $PSScriptRoot
$repo    = Split-Path -Parent (Split-Path -Parent $here)
$gameDir = 'G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64'
$relay   = Join-Path (Split-Path -Parent $here) 'rust\target\release\qd-relay.exe'
$rigRoot = Join-Path 'G:\Freespace\Queens_Domain\_test' ('swi2-' + $Phase)
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
function Say($m) {
    $s = "[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $m
    Write-Host $s; $script:lines.Add($s) | Out-Null
}
function Note($m) { Write-Host $m; $script:lines.Add($m) | Out-Null }

$gameDirPaths = @{}
function Get-RigPaths([int]$i) {
    $root = Join-Path $rigRoot ("inst{0}" -f $i)
    [pscustomobject]@{ Index = $i; Root = $root; Suffix = "qd$i"
        Log = Join-Path $gameDir ("qdcoop-qd{0}.log" -f $i); Pid = "$root.pid" }
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
function Get-Matches([int]$i, [string]$pattern) { return @(Get-RunLines $i | Where-Object { $_ -match $pattern }) }
function Get-LineMs([string]$ln) {
    if ($ln -match '^\s*(\d{1,2}):(\d{2}):(\d{2})\.(\d{3})') {
        return ([int]$Matches[1] * 3600000 + [int]$Matches[2] * 60000 + [int]$Matches[3] * 1000 + [int]$Matches[4])
    }
    return -1
}
function Get-Mark([int]$i) {
    $n = Get-Date
    return ($n.Hour * 3600000 + $n.Minute * 60000 + $n.Second * 1000 + $n.Millisecond) - 1000
}
function Test-Alive([int]$i) {
    if (-not $script:pids.ContainsKey($i)) { return $false }
    return [bool](Get-Process -Id $script:pids[$i] -ErrorAction SilentlyContinue)
}
function Get-Since([int]$i, [int]$markMs) {
    $out = @()
    foreach ($ln in @(Get-RunLines $i)) { $t = Get-LineMs ([string]$ln); if ($t -ge 0 -and $t -ge $markMs) { $out += $ln } }
    return $out
}
function Wait-Since([int]$i, [string]$pattern, [int]$mark, [int]$seconds) {
    $deadline = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $deadline) {
        $m = @(Get-Since $i $mark | Where-Object { $_ -match $pattern })
        if ($m.Count -gt 0) { return $m }
        if (-not (Test-Alive $i)) { return @() }
        Start-Sleep -Milliseconds 500
    }
    return @()
}
function Wait-Gameplay([int]$i, [int]$seconds) {
    $deadline = (Get-Date).AddSeconds($seconds)
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
function Check([string]$step, [int]$i, [string]$pattern, [int]$secs, [string]$label, [int]$Mark = 0) {
    if (-not (Test-Alive $i)) {
        $script:verdicts.Add([pscustomobject]@{ Step = $step; Inst = $i; What = $label; Ok = $false; Line = 'INSTANCE DIED' }) | Out-Null
        Note "  FAIL [$i] $label -- the instance is no longer running"; return $false
    }
    $m = Wait-Since $i $pattern $Mark $secs
    $ok = ($m.Count -gt 0)
    $script:verdicts.Add([pscustomobject]@{ Step = $step; Inst = $i; What = $label; Ok = $ok; Line = ($m | Select-Object -Last 1) }) | Out-Null
    if ($ok) { Note "  PASS [$i] $label" } else { Note "  --   [$i] $label (no line matches '$pattern' in ${secs}s)" }
    return $ok
}
function Verdict([string]$step, [int]$i, [bool]$ok, [string]$what, [string]$line = '') {
    $script:verdicts.Add([pscustomobject]@{ Step = $step; Inst = $i; What = $what; Ok = $ok; Line = $line }) | Out-Null
    Note ("  {0} [{1}] {2}{3}" -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $i, $what, $(if ($line) { " -- $line" } else { '' }))
}
function Step([string]$title, [string]$body) { Note ''; Note "### $title"; if ($body) { Note $body } }
function Show-Since([int]$i, [string]$pattern, [int]$mark, [int]$n) {
    $m = @(Get-Since $i $mark | Where-Object { $_ -match $pattern })
    if ($m.Count -eq 0) { Note "  [$i] (no line matches '$pattern' since the step started)"; return }
    $m | Select-Object -Last $n | ForEach-Object { $s = [string]$_; if ($s.Length -gt 400) { $s = $s.Substring(0,400) + ' ...' }; Note "  [$i] $s" }
}
function Stop-All {
    Get-Process -Name 'QueensDomain-Win64-Shipping', 'qd-relay' -ErrorAction SilentlyContinue | ForEach-Object {
        Say "  stopping leftover $($_.ProcessName) pid=$($_.Id)"
        Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue
    }
    Start-Sleep -Seconds 2
}
function Install-And-Configure {
    $built = Join-Path $repo 'mod\native\build\winmm.dll'
    if (-not (Test-Path -LiteralPath $built)) { Say "REFUSED: built proxy not found"; exit 2 }
    $h = (Get-FileHash -LiteralPath $built -Algorithm SHA256).Hash
    Say "installing $built (sha256 $($h.Substring(0,16))..., $((Get-Item $built).LastWriteTime))"
    $dst = Join-Path $gameDir 'winmm.dll'
    try { Copy-Item -LiteralPath $built -Destination $dst -Force }
    catch {
        $held = $dst + '.held-' + (Get-Date -Format 'HHmmss')
        Rename-Item -LiteralPath $dst -NewName (Split-Path -Leaf $held) -Force
        Copy-Item -LiteralPath $built -Destination $dst -Force
        Say "destination was image-mapped; moved to $held and installed the new proxy"
    }
    $lines = @('[Local]','Enabled=1','LogLevel=3','','[Net]','RelayHost=127.0.0.1','RelayPort=47800',
               '','[Rules]','FlagSync=1','ObjectSync=1','ActorStateSync=1','InteractSync=1',
               'HostAuthoritative=1','','[Tuning]','PlayerHz=30')
    $ini = Join-Path $gameDir 'qdcoop.ini'
    $lines | Set-Content -LiteralPath $ini
    Say "wrote $ini : ObjectSync + ActorStateSync + InteractSync"
    Copy-Item -LiteralPath $ini -Destination (Join-Path $outDir 'qdcoop-used.ini') -Force
}
function Seed-FreshPair {
    Say 're-seeding both instances'
    foreach ($i in 1, 2) {
        $null = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'make_test_instance.ps1') `
                -Name ("inst{0}" -f $i) -Index $i -SourceSave $seed -Root $rigRoot -Suffix $inst[$i].Suffix -Force 2>&1
        if ($LASTEXITCODE -ne 0) { Say "REFUSED: make_test_instance refused instance $i"; exit 2 }
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
        if (-not $gp) { Say "LAUNCH FAILED for instance $i" } else { $script:pids[$i] = [int]$gp; Set-Content -LiteralPath $inst[$i].Pid -Value $gp; Say "inst$i pid=$gp" }
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

# --- the world dump, and what a row looks like ------------------------------
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
function Dump-World([string]$why) {
    $H = $script:H; $P = $script:P
    Note "  waiting for the incremental object walk on both ($why)"
    [void](Wait-ObjScan $P 60)
    [void](Wait-ObjScan $H 60)
    $dumpP = Invoke-ObjDump $P 'wi2-peer'
    $dumpH = Invoke-ObjDump $H 'wi2-host'
    $savP = Parse-SavKeys $dumpP; $objP = Parse-ObjKeys $dumpP
    $savH = Parse-SavKeys $dumpH; $objH = Parse-ObjKeys $dumpH
    Note ("  PEER  savable rows {0} (in world {1}), object rows {2}" -f $savP.Count,
          @($savP | Where-Object { $_.World -eq 1 }).Count, $objP.Count)
    Note ("  HOST  savable rows {0} (in world {1}), object rows {2}" -f $savH.Count,
          @($savH | Where-Object { $_.World -eq 1 }).Count, $objH.Count)
    foreach ($grp in @($savP | Group-Object Cls)) { Note ("     peer savable {0} x{1}" -f $grp.Name, $grp.Count) }
    foreach ($grp in @($objP  | Group-Object Cls)) { Note ("     peer object  {0} x{1}" -f $grp.Name, $grp.Count) }
    return [pscustomobject]@{ SavP = $savP; ObjP = $objP; SavH = $savH; ObjH = $objH }
}

# ===========================================================================
function Invoke-Interact2 {
    HB 'interact2 start'
    $H = $script:H; $P = $script:P
    Step 'WORLD-INTERACT run 2' 'A drives the game''s own verb; B is only told the state.'

    $sw = @(Read-LogLines $inst[$H].Log | Where-Object { $_ -match 'switches:' })
    Verdict 'boot' $H (@($sw | Where-Object { ([string]$_) -match 'interact=1' }).Count -gt 0) `
            'the host reports interact=1' ($(if ($sw.Count) { [string]$sw[-1] } else { 'none' }))

    Step '0. the world dump' 'qd.obj, after the incremental object walk has produced rows'
    $w = Dump-World 'initial'
    $pots  = @($w.ObjP | Where-Object { $_.Shape -eq 'barricade' -and $_.Cls -match 'Pot' -and $_.State -eq 0 })
    $walls = @($w.SavP | Where-Object { $_.Shape -eq 'door' -and $_.World -eq 1 -and $_.Cls -match 'SecretWall|ScienceSecret' -and $_.Opened -eq '0' })
    $brks  = @($w.SavP | Where-Object { $_.Shape -eq 'barricade' -and $_.World -eq 1 })
    $doors = @($w.SavP | Where-Object { $_.Shape -eq 'door' -and $_.World -eq 1 -and $_.Cls -notmatch 'SecretWall|ScienceSecret' })
    # A LEVER/SWITCH is the game's own secret mechanism (`Lever_C` and friends carry
    # Activated + Activate/Deactivate + Restore State From Save Data).  Run 2 measured that a
    # SecretWall's own Activate moves its `Opened` bool WITHOUT writing the SavableActorStates
    # container, and this lane's transport IS that container -- so the mechanism family is
    # proven on a lever, and the wall is reported as what it measurably is.
    $levers = @($w.SavP | Where-Object { $_.Cls -match 'Lever|Switch|Rune|Button' -and $_.World -eq 1 })
    Note ("  candidates: pots(state=0)={0} secretwalls(open?0)={1} savable breakables={2} doors={3} levers/mechanisms={4}" -f `
          $pots.Count, $walls.Count, $brks.Count, $doors.Count, $levers.Count)
    @($pots | Select-Object -First 6)  | ForEach-Object { Note ("    pot  {0} [{1}] key={2} state={3} prop={4}" -f $_.Name,$_.Cls,$_.Key,$_.State,$_.Prop) }
    @($walls | Select-Object -First 6) | ForEach-Object { Note ("    wall {0} [{1}] key={2} opened={3} dist={4}" -f $_.Name,$_.Cls,$_.Key,$_.Opened,$_.Dist) }
    @($doors | Select-Object -First 6) | ForEach-Object { Note ("    door {0} [{1}] key={2} opened={3} dist={4}" -f $_.Name,$_.Cls,$_.Key,$_.Opened,$_.Dist) }

    # --- 1. the POT, and the drop trap ---------------------------------
    Step '1. a pot broken on A breaks on B -- AND B MUST NOT SPAWN A DROP' 'Pot_Base_C lives on the object lane'
    if ($pots.Count -eq 0) { Say 'REFUSED: no unbroken pot on the object lane' }
    else {
        $pot = $pots | Select-Object -First 1
        Note ("  pot under test: {0} [{1}] key={2} prop={3}" -f $pot.Name, $pot.Cls, $pot.Key, $pot.Prop)
        # Both machines stand beside THEIR copy first, so a drop the receiver spawned would be
        # within the probe's 30 m and the count is about the pot, not about distance.
        Set-Control $H 'qd.tp' ('near:' + $pot.Name)
        Start-Sleep -Seconds 6
        Set-Control $H 'qd.drops' ('1#' + (Get-Date -Format 'HHmmssfff'))
        Set-Control $P 'qd.tp' ('near:' + $pot.Name)
        Start-Sleep -Seconds 6
        Show-Since $H "rig: near '$($pot.Name)' matched|dev teleport" (Get-Mark $H) 3
        $markH0 = Get-Mark $H
        Start-Sleep -Seconds 4
        $dropsBefore = @(Get-Since $H $markH0 | Where-Object { $_ -match 'rig: drop probe ' })
        Note ("  [B] 'rig: drop probe' lines before the pot: {0}" -f $dropsBefore.Count)

        $markP = Get-Mark $P; $markH = Get-Mark $H
        Say "  act[$P] -> near:$($pot.Name)!Break"
        Set-Control $P 'qd.act' ('near:' + $pot.Name + '!Break#' + (Get-Date -Format 'HHmmssfff'))
        Start-Sleep -Seconds 4
        Show-Since $P 'dev interact: forced verb|object|actor-state' $markP 10
        Start-Sleep -Seconds 6
        Show-Since $P 'object|actor-state|interact:' $markP 12
        Show-Since $H 'object|actor-state|interact:' $markH 14

        $pApplied = @(Get-Since $H $markH | Where-Object { $_ -match 'object intent applied|object apply:' })
        $pIntented = @(Get-Since $P $markP | Where-Object { $_ -match 'object intent sent|object sync: tracking|object local change' })
        Verdict 'pot' $P ($pIntented.Count -gt 0) 'the pot break on A reached the object lane' ($(if ($pIntented.Count) { [string]$pIntented[-1] } else { '' }))
        Verdict 'pot' $H ($pApplied.Count -gt 0) 'the pot break applied on B' ($(if ($pApplied.Count) { [string]$pApplied[-1] } else { '' }))
        [void](Check 'pot' $H 'visual=' 20 'B named the visual it ran' $markH)

        Start-Sleep -Seconds 5
        $markH1 = Get-Mark $H
        Start-Sleep -Seconds 7
        $dropsAfter = @(Get-Since $H $markH1 | Where-Object { $_ -match 'rig: drop probe ' })
        Note ("  [B] 'rig: drop probe' lines after the pot: {0}" -f $dropsAfter.Count)
        $dropsAfter | ForEach-Object { Note "    $_" }
        # A DROP IS ITS ACTOR NAME + CLASS, not its whole log line.  The line carries a
        # timestamp and a distance, so a whole-line comparison calls every repeated probe a new
        # drop -- run 2's FAIL was exactly that mistake, on a before=20/after=20 measurement
        # whose actor set had not changed.  (The player stands ~1.5 m from the pot, well inside
        # the probe's 30 m, so the window really is the pot's surroundings.)
        $keysBefore = @($dropsBefore | ForEach-Object { if (([string]$_) -match 'rig: drop probe (\S+) \[([^\]]+)\]') { "$($Matches[1])|$($Matches[2])" } } | Sort-Object -Unique)
        $keysAfter  = @($dropsAfter  | ForEach-Object { if (([string]$_) -match 'rig: drop probe (\S+) \[([^\]]+)\]') { "$($Matches[1])|$($Matches[2])" } } | Sort-Object -Unique)
        Note ("  [B] distinct drop actors before={0} after={1}" -f @($keysBefore).Count, @($keysAfter).Count)
        $newOnes = @($keysAfter | Where-Object { $keysBefore -notcontains $_ })
        Verdict 'pot' $H ($newOnes.Count -eq 0) `
                ("B spawned NO new drop for a pot broken on A (distinct drops before={0} after={1})" -f @($keysBefore).Count, @($keysAfter).Count) `
                ($(if ($newOnes.Count) { 'NEW: ' + ($newOnes -join ', ') } else { '' }))
    }

    # --- 2. a SECRET MECHANISM: a lever, then the secret wall ------------
    Step '2. a secret mechanism moved on A moves on B' 'a LEVER first (its own verb writes the container), then the wall'
    $lever = $levers | Select-Object -First 1
    if (-not $lever) { Say 'REFUSED: this world places no savable lever/switch' }
    else {
        Note ("  lever under test: {0} [{1}] key={2} value={3} dist={4}" -f $lever.Name, $lever.Cls, $lever.Key, $lever.Value, $lever.Dist)
        $markP = Get-Mark $P; $markH = Get-Mark $H
        Set-Control $P 'qd.tp' ('guid:' + $lever.Key)
        Start-Sleep -Seconds 7
        Set-Control $P 'qd.gate' ('near:guid:' + $lever.Key + '#' + (Get-Date -Format 'HHmmssfff'))
        Start-Sleep -Seconds 3
        Show-Since $P 'rig: gate|dev interact: standing|matched the actor' $markP 6
        Set-Control $P 'qd.act' ('near:guid:' + $lever.Key + '!Activate#' + (Get-Date -Format 'HHmmssfff'))
        Start-Sleep -Seconds 8
        Show-Since $P 'dev interact:|actor-state' $markP 12
        Show-Since $H 'actor-state|interact:' $markH 14
        $lP = @(Get-Since $P $markP | Where-Object { $_ -match 'actor-state local change|actor-state intent sent' })
        $lH = @(Get-Since $H $markH | Where-Object { $_ -match 'actor-state intent applied|actor states applied=[1-9]' })
        Verdict 'lever' $P ($lP.Count -gt 0) 'the lever change on A reached the lane' ($(if ($lP.Count) { [string]$lP[-1] } else { '' }))
        Verdict 'lever' $H ($lH.Count -gt 0) 'the lever change applied on B' ($(if ($lH.Count) { [string]$lH[-1] } else { '' }))
        [void](Check 'lever' $H 'interact: door anim |actor-state refresh ok' 15 'B ran the mechanism''s own visual' $markH)
        $markP2 = Get-Mark $P; $markH2 = Get-Mark $H
        Set-Control $P 'qd.act' ('near:guid:' + $lever.Key + '!Deactivate#' + (Get-Date -Format 'HHmmssfff'))
        Start-Sleep -Seconds 8
        Show-Since $P 'dev interact:|actor-state' $markP2 10
        Show-Since $H 'actor-state|interact:' $markH2 10
        $lP2 = @(Get-Since $P $markP2 | Where-Object { $_ -match 'actor-state local change|actor-state intent sent' })
        $lH2 = @(Get-Since $H $markH2 | Where-Object { $_ -match 'actor-state intent applied|interact: door anim .*state=0' })
        Verdict 'lever' $P ($lP2.Count -gt 0) 'the lever switched OFF reached the lane (bidirectional)' ($(if ($lP2.Count) { [string]$lP2[-1] } else { '' }))
        Verdict 'lever' $H ($lH2.Count -gt 0) 'the lever switched OFF applied on B' ($(if ($lH2.Count) { [string]$lH2[-1] } else { '' }))
    }

    Step '2b. a secret wall: what its own Activate does to the container' 'run 2 measured this; recorded again with the fixed lane'
    if ($walls.Count -eq 0) { Say 'REFUSED: no SecretWall_C in this world with opened=0' }
    else {
        $wall = $walls | Select-Object -First 1
        Note ("  secret wall: {0} [{1}] key={2} opened={3} dist={4}" -f $wall.Name, $wall.Cls, $wall.Key, $wall.Opened, $wall.Dist)
        $markP = Get-Mark $P; $markH = Get-Mark $H
        Set-Control $P 'qd.tp' ('guid:' + $wall.Key)
        Start-Sleep -Seconds 7
        Set-Control $P 'qd.act' ('near:guid:' + $wall.Key + '!Activate#' + (Get-Date -Format 'HHmmssfff'))
        Start-Sleep -Seconds 8
        Show-Since $P 'dev interact:|actor-state' $markP 10
        Show-Since $H 'actor-state|interact:' $markH 10
        $wP = @(Get-Since $P $markP | Where-Object { $_ -match 'actor-state local change|actor-state intent sent' })
        $wH = @(Get-Since $H $markH | Where-Object { $_ -match 'actor-state intent applied|actor states applied=[1-9]' })
        Verdict 'wall' $P ($wP.Count -gt 0) 'the secret wall change on A reached the lane' ($(if ($wP.Count) { [string]$wP[-1] } else { 'its own Activate moves Opened but never writes the container' }))
        Verdict 'wall' $H ($wH.Count -gt 0) 'the secret wall applied on B' ($(if ($wH.Count) { [string]$wH[-1] } else { '' }))
    }

    # --- 3. what a DOOR-shaped class actually exposes --------------------
    Step '3. the door-shaped classes in this world: what they expose, and what drives them' 'read, then try'
    foreach ($dn in 'AquaGate_01_SM3', 'SwordShrineGate_BP', 'SwordShrineGate_BP2') {
        $markP = Get-Mark $P
        Set-Control $P 'qd.act' ('near:' + $dn + '!?Opened,DoorClosed,PreviewOpen#' + (Get-Date -Format 'HHmmssfff'))
        Start-Sleep -Seconds 3
        Set-Control $P 'qd.gate' ('near:' + $dn + '#' + (Get-Date -Format 'HHmmssfff'))
        Start-Sleep -Seconds 3
        Note "  -- $dn"
        Show-Since $P "dev props:|rig: gate|dev interact: resolved|no live actor" $markP 5
    }
    Verdict 'door' $P (@(Get-Matches $P 'dev props: .*Opened=').Count -gt 0) 'at least one door-shaped class exposes Opened on the peer' ''

    # --- 4. the real press vs the class' own verb on a gate -------------
    Step '4. driving a gate on A: the real press, then the class'' own verb' 'measured, not asserted'
    foreach ($dn in 'AquaGate_01_SM3', 'SwordShrineGate_BP') {
        Note "  -- $dn : the REAL interaction press"
        $markP = Get-Mark $P; $markH = Get-Mark $H
        Set-Control $P 'qd.tp' ('near:' + $dn)
        Start-Sleep -Seconds 6
        Set-Control $P 'qd.act' ('near:' + $dn + '#' + (Get-Date -Format 'HHmmssfff'))
        Start-Sleep -Seconds 6
        Show-Since $P 'dev interact:|actor-state|gate says' $markP 10
        Show-Since $H 'actor-state|object|interact:' $markH 8
        $a1 = @(Get-Since $P $markP | Where-Object { $_ -match 'actor-state local change|actor-state intent sent|object local change' })
        Note ("    press -> lane change lines: {0}" -f $a1.Count)
        if ($a1.Count -gt 0) { Verdict 'door' $P $true ("the REAL press on " + $dn + " reached the lane") ([string]$a1[-1]) }

        Note "  -- $dn : the class' own Activate"
        $markP = Get-Mark $P; $markH = Get-Mark $H
        Set-Control $P 'qd.act' ('near:' + $dn + '!Activate#' + (Get-Date -Format 'HHmmssfff'))
        Start-Sleep -Seconds 7
        Show-Since $P 'dev interact:|actor-state|object' $markP 10
        Show-Since $H 'actor-state|object|interact:' $markH 8
        $a2 = @(Get-Since $P $markP | Where-Object { $_ -match 'actor-state local change|actor-state intent sent|object local change' })
        Note ("    Activate -> lane change lines: {0}" -f $a2.Count)
        if ($a2.Count -gt 0) { Verdict 'door' $P $true ("the class' own Activate on " + $dn + " reached the lane") ([string]$a2[-1]) }
    }

    # --- 4b. the SAME door CLOSED from the host: the other direction and the close path ---
    Step '4b. the door the peer opened is closed from B (the host): the reverse direction' 'bidirectional + host authority'
    $shrine = @($w.SavP | Where-Object { $_.Cls -eq 'SwordShrineGate_BP_C' -and $_.World -eq 1 } | Select-Object -First 1)
    if (-not $shrine) { Say 'REFUSED: no SwordShrineGate_BP_C in this world to close' }
    else {
        $markH = Get-Mark $H; $markP = Get-Mark $P
        Set-Control $H 'qd.tp' ('guid:' + $shrine.Key)
        Start-Sleep -Seconds 7
        Set-Control $H 'qd.act' ('near:guid:' + $shrine.Key + '!Deactivate#' + (Get-Date -Format 'HHmmssfff'))
        Start-Sleep -Seconds 9
        Show-Since $H 'dev interact:|actor-state|interact:' $markH 12
        Show-Since $P 'actor-state|interact:' $markP 12
        $cH = @(Get-Since $H $markH | Where-Object { $_ -match 'actor-state host set|actor-state local change' })
        $cP = @(Get-Since $P $markP | Where-Object { $_ -match 'actor states applied=[1-9]' })
        $cP2 = @(Get-Since $P $markP | Where-Object { $_ -match 'interact: door anim .*state=0' })
        Verdict 'door' $H ($cH.Count -gt 0) 'the host published its own CLOSE' ($(if ($cH.Count) { [string]$cH[-1] } else { '' }))
        Verdict 'door' $P ($cP.Count -gt 0) 'the peer applied the host''s CLOSE' ($(if ($cP.Count) { [string]$cP[-1] } else { '' }))
        Verdict 'door' $P ($cP2.Count -gt 0) 'the peer played the door''s CLOSE animation' ($(if ($cP2.Count) { [string]$cP2[-1] } else { '' }))
    }

    # --- 5. a BARREL / CRATE: which lane owns it -----------------------
    Step '5. a barrel/crate: does either lane see it, and does a break cross?' 'the breakable coverage question'
    foreach ($bn in 'Barrel', 'Crate') {
        $markP = Get-Mark $P
        Set-Control $P 'qd.act' ('near:' + $bn + '!?Broken,AlreadyBroke,BrokeBarricade#' + (Get-Date -Format 'HHmmssfff'))
        Start-Sleep -Seconds 3
        Note "  -- $bn properties:"
        Show-Since $P 'dev props:|no live actor' $markP 4
    }
    # Two DIFFERENT breakable families, because "several breakable types" is the requirement:
    # `Barrel_C`/`Crate_C` are `Permabreakable_base_C` (savable, GUID lane, their state is the
    # container and they carry NO bool of their own -- measured in run 2, all three probes
    # absent), while the pot above is a plain Actor on the object lane.
    foreach ($bn in 'Barrel', 'Crate', 'Crate_Mossy') {
        $markP = Get-Mark $P; $markH = Get-Mark $H
        Set-Control $P 'qd.tp' ('near:' + $bn)
        Start-Sleep -Seconds 6
        Set-Control $P 'qd.act' ('near:' + $bn + '!Break#' + (Get-Date -Format 'HHmmssfff'))
        Start-Sleep -Seconds 8
        Show-Since $P 'dev interact:|actor-state|object' $markP 10
        Show-Since $H 'actor-state|object|interact:' $markH 10
        $bP = @(Get-Since $P $markP | Where-Object { $_ -match 'actor-state local change|actor-state intent sent|object local change' })
        $bH = @(Get-Since $H $markH | Where-Object { $_ -match 'actor-state intent applied|object intent applied|actor states applied=[1-9]' })
        Verdict 'break' $P ($bP.Count -gt 0) ("the $bn break on A reached A's lane") ($(if ($bP.Count) { [string]$bP[-1] } else { 'no lane line' }))
        Verdict 'break' $H ($bH.Count -gt 0) ("the $bn break applied on B") ($(if ($bH.Count) { [string]$bH[-1] } else { 'no lane line' }))
    }

    Step '6. survival'
    foreach ($i in @($H, $P)) {
        Verdict 'alive' $i (Test-Alive $i) 'still running' ''
        $fault = @(Get-RunLines $i | Where-Object { $_ -match '(\sFAULT\b)|Unhandled Exception|ACCESS_VIOLATION|NULL_CLASS_PTR' })
        Verdict 'alive' $i ($fault.Count -eq 0) 'no FAULT line' ($(if ($fault.Count) { [string]$fault[-1] } else { '' }))
    }
    HB 'interact2 done'
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
    Say "session_wi2 phase=$Phase rig=$rigRoot"
    Seed-FreshPair
    Start-Boot $Phase
    Invoke-Interact2
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
