# session-finalsoak.ps1 -- the FINAL combined soak: every lane, ONE pair of processes.
#
# WHAT THIS IS
# ------------
# The r3/round-2/round-3 sessions each proved ONE lane in ONE boot.  Nothing yet has
# measured what happens when every shipped switch is on AT ONCE and the players keep
# playing: a lane that interferes with another (a spectator hijacking a valid respawn, a
# partner save landing while its owner is dead, a mover nudge fighting a ride body) would
# pass every per-lane run and still be broken in the hands of two people.
#
# So this is ONE boot, ONE pair, >= 15 minutes of measured body, exercising the whole
# shipped play set in the order a session actually happens:
#
#   0. boot + relay check (the relay must know 0x40/0x41 and report ZERO malformed)
#   1. a door opened AND closed, both directions
#   2. a barrel AND a pot broken
#   3. a secret wall opened
#   4. a key granted by the game's own cheat, then USED to zero
#   5. an enemy killed, XP paid on BOTH machines
#   6. a rest (which saves) -> the PARTNER writes its own save
#   7. a dialogue, with an ADVANCE the partner follows
#   8. a death -> respawn (penalty) -> rest (restore), with the partner watching
#   9. three weapon families equipped + attacked, and a crossbow shot
#  10. a lift ridden
#  11. end: alive, no FAULT, no malformed, feature-interference audit
#
# THE INI IS THE SHIPPED FILE, VERBATIM.  `mod/qdcoop.ini` is the play set: it is what a
# human tester is told to run.  Only two lines are rewritten -- RelayHost (two instances on
# one PC) and LogLevel=3 -- because neither is a rule switch.  A soak on a hand-written
# switch list would measure the list, not the product.
#
# HONESTY RULES (inherited from every session since 18)
# ----------------------------------------------------
# * A verdict prints the line it read.  No line, no pass.
# * A step whose PRECONDITION is missing says REFUSED and is counted as REFUSED, never as
#   a pass and never as a lane failure.
# * FAULT/ERR are counted separately from a lane's refusal: a refused write is not a crash.
# * An unreadable number is reported unreadable and is never counted as equal.
#
# ONE PROCESS.  A launched game does not survive the command that started it (R14), so the
# whole session -- launch, drive, measure, stop -- is this one script.  Run it in the
# background and read its output as it goes.
[CmdletBinding()]
param(
    [int]$BootSeconds = 300,
    [int]$SoakSeconds = 900,
    [string]$Report = '',
    # Which instance is which is READ from the log, never assumed.
    [switch]$KeepRunning
)

$ErrorActionPreference = 'Stop'
$here    = $PSScriptRoot
$repo    = Split-Path -Parent (Split-Path -Parent $here)
$gameDir = 'G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64'
$relay   = Join-Path (Split-Path -Parent $here) 'rust\target\release\qd-relay.exe'
$rigRoot = 'G:\Freespace\Queens_Domain\_test\finalsoak'
$seed    = Join-Path $repo 'analysis\save-backup-before-live'
$srcIni  = Join-Path $repo 'mod\qdcoop.ini'
$outDir  = Join-Path $rigRoot 'out'
if (-not $Report) { $Report = Join-Path $outDir 'report-finalsoak.txt' }
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force -Path $outDir | Out-Null }

$script:lines = New-Object System.Collections.Generic.List[string]
$script:runCache = @{}
function Say($m) {
    $s = "[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $m
    Write-Host $s
    $script:lines.Add($s) | Out-Null
}
function Note($m) { Write-Host $m; $script:lines.Add($m) | Out-Null }

function Get-RigPaths([int]$i) {
    $root   = Join-Path $rigRoot ("inst{0}" -f $i)
    $suffix = "qd$i"
    [pscustomobject]@{
        Index = $i; Root = $root; Suffix = $suffix
        Log   = Join-Path $gameDir ("qdcoop-{0}.log" -f $suffix)
        Saves = Join-Path $root ("Saved_{0}\SaveGames" -f $suffix)
        Crashes = Join-Path $root ("Saved_{0}\Crashes" -f $suffix)
        Pid   = "$root.pid"
    }
}
$inst = @{ 1 = (Get-RigPaths 1); 2 = (Get-RigPaths 2) }

function Read-LogLines([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) { return @() }
    for ($t = 0; $t -lt 3; $t++) {
        try {
            $fs = [System.IO.File]::Open($path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read,
                                         ([System.IO.FileShare]::ReadWrite -bor [System.IO.FileShare]::Delete))
            try {
                $sr = New-Object System.IO.StreamReader($fs, [System.Text.Encoding]::UTF8, $true)
                $txt = $sr.ReadToEnd(); $sr.Dispose()
            } finally { $fs.Dispose() }
            return @($txt -split "`r?`n")
        } catch { Start-Sleep -Milliseconds 50 }
    }
    return @()
}
function Get-RunLines([int]$i) {
    $p = $inst[$i].Log
    if (-not (Test-Path -LiteralPath $p)) { return @() }
    $len = (Get-Item -LiteralPath $p).Length
    if ($script:runCache.ContainsKey($i) -and $script:runCache[$i].Len -eq $len) {
        return $script:runCache[$i].Lines
    }
    $all = @(Read-LogLines $p)
    if ($all.Count -eq 0) { return @() }
    $start = 0
    for ($k = $all.Count - 1; $k -ge 0; $k--) {
        if ($all[$k] -match 'hooked FEngineLoop') { $start = $k; break }
    }
    $lines = @($all[$start..($all.Count - 1)])
    $script:runCache[$i] = [pscustomobject]@{ Len = $len; Lines = $lines }
    return $lines
}
function Get-Matches([int]$i, [string]$p) { return @(Get-RunLines $i | Where-Object { $_ -match $p }) }
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
    if (-not $script:pids -or -not $script:pids.ContainsKey($i)) { return $false }
    return [bool](Get-Process -Id $script:pids[$i] -ErrorAction SilentlyContinue)
}
function Get-Since([int]$i, [int]$mark) {
    $src = Get-RunLines $i
    $out = New-Object System.Collections.Generic.List[string]
    foreach ($ln in $src) {
        $t = Get-LineMs ([string]$ln)
        if ($t -ge 0 -and $t -ge $mark) { $out.Add([string]$ln) }
    }
    return $out.ToArray()
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
        if (@(Get-Matches $i 'scan: local at|scan: health').Count -gt 0) { return $true }
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
$script:refused = New-Object System.Collections.Generic.List[string]
function Check([string]$step, [int]$i, [string]$p, [int]$secs, [string]$label, [int]$Mark = 0) {
    if (-not (Test-Alive $i)) {
        $script:verdicts.Add([pscustomobject]@{ Step = $step; Inst = $i; What = $label; Ok = $false; Line = 'INSTANCE DIED' }) | Out-Null
        Note "  FAIL [$i] $label -- the instance is no longer running"
        return $false
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
function Refuse([string]$step, [string]$why) {
    $script:refused.Add("${step}: $why") | Out-Null
    Note "  REFUSED [$step] $why"
}
function Step([string]$t, [string]$b) { Note ''; Note "### $t"; if ($b) { Note $b } }
function Show-Since([int]$i, [string]$p, [int]$mark, [int]$n) {
    $m = @(Get-Since $i $mark | Where-Object { $_ -match $p })
    if ($m.Count -eq 0) { Note "  [$i] (no '$p' since the step began)"; return }
    $m | Select-Object -Last $n | ForEach-Object {
        $s = [string]$_
        if ($s.Length -gt 400) { $s = $s.Substring(0, 400) + ' ...' }
        Note "  [$i] $s"
    }
}
function Get-LastPos([int]$i, [int]$mark = 0) {
    $rows = @(Get-Since $i $mark | Where-Object { $_ -match 'scan: local at ' })
    if ($rows.Count -eq 0) { return $null }
    if ($rows[-1] -match 'local at (-?[0-9.]+),(-?[0-9.]+),(-?[0-9.]+)') {
        return @([double]$Matches[1], [double]$Matches[2], [double]$Matches[3])
    }
    return $null
}
$script:goSeq = 0
function Invoke-Go([int]$i, [string]$name, [int]$near = 140) {
    $script:goSeq++
    Set-Control $i 'qd.near' ([string]$near)
    Set-Control $i 'qd.tp' ('near:' + $name + ',' + (($script:goSeq * 37) % 360))
}
function Invoke-Act([int]$i, [string]$spec) {
    Set-Control $i 'qd.act' ($spec + '#' + (Get-Date -Format 'HHmmssfff'))
}
function Invoke-Hard([int]$i, [string]$map) {
    Set-Control $i 'qd.hard' ($map + '#' + (Get-Date -Format 'HHmmssfff'))
}
function Get-SaveStamps([int]$i) {
    $dir = $inst[$i].Saves
    if (-not (Test-Path -LiteralPath $dir)) { return @() }
    return @(Get-ChildItem -LiteralPath $dir -Filter '*.sav' -ErrorAction SilentlyContinue |
             Sort-Object Name | ForEach-Object { [pscustomobject]@{ Name = $_.Name; Stamp = $_.LastWriteTime.ToString('HH:mm:ss.fff') } })
}
function Show-Stamps([int]$i, [string]$when) {
    $s = Get-SaveStamps $i
    Note ("  [{0}] save files {1}: {2}" -f $i, $when, (($s | ForEach-Object { "$($_.Name)@$($_.Stamp)" }) -join '  '))
    return $s
}
function Get-WorldState([int]$i) {
    $out = [pscustomobject]@{ World = ''; Partner = ''; Agree = '' }
    $rows = @(Get-Matches $i 'world: local=')
    if ($rows.Count -gt 0 -and ([string]$rows[-1]) -match 'world: local=(\S*) partner=(\S*) agree=(\d+)') {
        $out.World = $Matches[1]; $out.Partner = $Matches[2]; $out.Agree = $Matches[3]
    }
    return $out
}
function Invoke-ObjDump([int]$i, [string]$tag) {
    $mark = Get-Mark $i
    Set-Control $i 'qd.obj' ($tag + '#' + (Get-Date -Format 'HHmmssfff'))
    $deadline = (Get-Date).AddSeconds(45)
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

function Stop-All([string]$Reason) {
    if (-not $Reason) { $Reason = 'NO REASON GIVEN (a harness defect, not a crash)' }
    Say "STOP-ALL reason=$Reason"
    foreach ($i in 1, 2) {
        if (-not (Test-Alive $i)) { continue }
        $last = ''
        try { $l = @(Get-RunLines $i); if ($l.Count -gt 0) { $last = ([string]$l[-1]).Trim() } } catch {}
        if ($last.Length -gt 260) { $last = $last.Substring(0, 260) + ' ...' }
        Say ("  killing inst{0} pid={1}; its last log line was: {2}" -f $i, $script:pids[$i], $last)
    }
    Get-Process -Name 'QueensDomain-Win64-Shipping', 'qd-relay' -ErrorAction SilentlyContinue |
        ForEach-Object { Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue }
    Start-Sleep -Seconds 2
}

function Install-And-Configure {
    $built = Join-Path $repo 'mod\native\build\winmm.dll'
    if (-not (Test-Path -LiteralPath $built)) { Say "REFUSED: built proxy not found at $built"; exit 2 }
    $h = (Get-FileHash -LiteralPath $built -Algorithm SHA256).Hash
    Say ("installing proxy sha256={0}... built {1}" -f $h.Substring(0, 16), (Get-Item $built).LastWriteTime)
    $dst = Join-Path $gameDir 'winmm.dll'
    try { Copy-Item -LiteralPath $built -Destination $dst -Force }
    catch {
        $held = $dst + '.held-' + (Get-Date -Format 'HHmmss')
        Rename-Item -LiteralPath $dst -NewName (Split-Path -Leaf $held) -Force
        Copy-Item -LiteralPath $built -Destination $dst -Force
        Say "destination was image-mapped; moved it to $held and installed the new proxy"
    }

    # THE SHIPPED INI, VERBATIM.  Only the relay address (two instances on one PC) and
    # LogLevel=3 are rewritten; every RULE switch is exactly what mod/qdcoop.ini says, so a
    # green run is a statement about the product rather than about a list in this script.
    if (-not (Test-Path -LiteralPath $srcIni)) { Say "REFUSED: shipped ini not found at $srcIni"; exit 2 }
    $text = Get-Content -LiteralPath $srcIni -Raw
    $text = $text -replace '(?m)^RelayHost=.*$', 'RelayHost=127.0.0.1'
    $text = $text -replace '(?m)^RelayPort=.*$', 'RelayPort=47800'
    $text = $text -replace '(?m)^LogLevel=.*$', 'LogLevel=3'
    $ini = Join-Path $gameDir 'qdcoop.ini'
    $text | Set-Content -LiteralPath $ini -NoNewline
    $sw = @(Get-Content -LiteralPath $ini | Where-Object { $_ -match '=' -and $_ -notmatch '^;' })
    Say ("wrote {0} from the SHIPPED qdcoop.ini verbatim ({1} switches; relay pinned to loopback, LogLevel=3)" -f $ini, $sw.Count)
    Copy-Item -LiteralPath $ini -Destination (Join-Path $outDir 'qdcoop-used.ini') -Force
}

function Seed-FreshPair {
    Say 're-seeding both instances from the pristine pre-live save'
    foreach ($i in 1, 2) {
        $null = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'make_test_instance.ps1') `
                -Name ("inst{0}" -f $i) -Index $i -SourceSave $seed -Root $rigRoot -Suffix $inst[$i].Suffix -Force 2>&1
        if ($LASTEXITCODE -ne 0) { Say "REFUSED: make_test_instance refused instance $i"; exit 2 }
    }
}

function Start-Boot {
    Stop-All 'boot: replacing any process left over from an earlier run'
    Install-And-Configure
    foreach ($i in 1, 2) {
        foreach ($leaf in 'qd.tp','qd.act','qd.hit','qd.cheat','qd.cheatarg','qd.travel','qd.hard',
                          'qd.level','qd.list','qd.near','qd.drops','qd.menu','qd.efunc','qd.eattack',
                          'qd.use','qd.pdmg','qd.kill','qd.petrace','qd.enemies','qd.sequence',
                          'qd.enemykill','qd.hitguid','qd.obj','qd.flag','qd.gate','qd.take',
                          'qd.trigger','qd.selfhp','qd.nohit','qd.dlg','qd.attack','qd.equip',
                          'qd.mover','qd.wsys','qd.proj','qd.atkcall','qd.atkinfo','qd.eprops') {
            Set-Control $i $leaf $null
        }
        Set-Control $i 'qd.scan' '1'
        # NO qd.god: the soak measures a REAL death in step 8, and the health floor is a latch
        # that renders the penalty unmeasurable (measured in the death lane's runs 5 and 6).
        Set-Control $i 'qd.near' '140'
        if (Test-Path -LiteralPath $inst[$i].Pid) { Remove-Item -LiteralPath $inst[$i].Pid -ErrorAction SilentlyContinue }
        $old = $inst[$i].Log
        if (Test-Path -LiteralPath $old) { Move-Item -LiteralPath $old -Destination ($old + '.prev') -Force }
        # The death lane's stack sidecar survives a save; a previous run's stacks would be
        # this run's baseline and the 90 % / 100 % checks would measure the wrong ladder.
        $sc = Join-Path $gameDir ("qdcoop-{0}-respawn.ini" -f $inst[$i].Suffix)
        if (Test-Path -LiteralPath $sc) { Remove-Item -LiteralPath $sc -Force -ErrorAction SilentlyContinue; Say "removed a previous run's $sc" }
    }
    $env:QDCOOP_AUTOLOAD = '0'
    $env:QDCOOP_TEST_NEAR = $null
    $env:QDCOOP_TEST_HITMS = '3000'

    if (-not (Test-Path -LiteralPath $relay)) { Say "REFUSED: relay not built at $relay"; exit 2 }
    $relayLog = Join-Path $outDir 'relay-finalsoak.log'
    Remove-Item -LiteralPath $relayLog -ErrorAction SilentlyContinue
    $script:relayLog = $relayLog
    $script:relayProc = Start-Process -FilePath $relay -ArgumentList @('--bind', '127.0.0.1:47800') `
            -RedirectStandardOutput $relayLog -RedirectStandardError (Join-Path $outDir 'relay-finalsoak.err') `
            -WindowStyle Hidden -PassThru
    $relayStamp = (Get-Item -LiteralPath $relay).LastWriteTime
    Say ("relay pid={0} built {1} -> {2}" -f $script:relayProc.Id, $relayStamp, $relayLog)
    Start-Sleep -Seconds 2

    $script:pids = @{}
    foreach ($i in 1, 2) {
        $ueLog = Join-Path $outDir ("inst{0}-ue.log" -f $i)
        Remove-Item -LiteralPath $ueLog -ErrorAction SilentlyContinue
        $launch = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'run-hidden.ps1') `
                    -UserDir $inst[$i].Root -Suffix $inst[$i].Suffix -AbsLog $ueLog 2>&1
        $gp = ($launch | Where-Object { $_ -match '^\d+$' } | Select-Object -Last 1)
        if (-not $gp) { Say "LAUNCH FAILED for instance $i"; $launch | ForEach-Object { Note $_ } }
        else { $script:pids[$i] = [int]$gp; Set-Content -LiteralPath $inst[$i].Pid -Value $gp; Say "inst$i pid=$gp (off-screen)" }
        Start-Sleep -Seconds 3
    }

    Say "waiting for a session (up to $BootSeconds s)"
    $deadline = (Get-Date).AddSeconds($BootSeconds)
    $roles = @{}
    while ((Get-Date) -lt $deadline) {
        foreach ($i in 1, 2) {
            if ($roles.ContainsKey($i) -or -not $script:pids.ContainsKey($i)) { continue }
            $m = @(Get-Matches $i 'INFO session established: role=(HOST|PEER)')
            if ($m.Count -gt 0 -and $m[-1] -match 'role=(HOST|PEER)') {
                $roles[$i] = $Matches[1]
                Note ("  [{0}] {1}" -f $i, ([string]$m[-1]).Trim())
            }
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
        Say 'REFUSED: no HOST/PEER pair was established'
        foreach ($i in 1, 2) { Note "  [$i] alive=$(Test-Alive $i)"; Show-Since $i 'session|REFUS|ERR |switches:' 0 12 }
        Stop-All ("boot refused: no HOST/PEER pair within $BootSeconds s (a timeout, not a crash)")
        $script:lines | Set-Content -LiteralPath $Report
        exit 2
    }
    Say "roles: HOST=inst$($script:H) PEER=inst$($script:P)"

    Say "waiting for gameplay on both (up to $BootSeconds s)"
    foreach ($i in @($script:H, $script:P)) {
        $ok = Wait-Gameplay $i $BootSeconds
        Say "inst${i} gameplay=$ok"
        Verdict 'boot' $i $ok 'the machine reached gameplay (it has a pawn to report)' ''
    }
    # THE SWITCH LINE.  This soak's whole claim is "the SHIPPED file", so the switches the
    # mod itself resolved are quoted rather than trusted from the file on disk.
    foreach ($i in @($script:H, $script:P)) {
        $sw = @(Read-LogLines $inst[$i].Log | Where-Object { $_ -match 'switches:' })
        if ($sw.Count -gt 0) { Note ("  [{0}] {1}" -f $i, ([string]$sw[-1]).Trim()) }
    }
}

# ===========================================================================
# 0. THE RELAY -- the stale-binary trap that silently drops kinds 0x40/0x41
# ===========================================================================
function Test-Relay {
    Step '0. THE RELAY knows every kind and dropped nothing' `
         'a relay built before kinds 0x40/0x41 answers "unknown packet kind" and every award/save datagram dies'
    Say ("  relay binary: {0} ({1:N0} bytes, built {2})" -f $relay, (Get-Item $relay).Length, (Get-Item $relay).LastWriteTime)
    if (-not (Test-Path -LiteralPath $script:relayLog)) { Refuse 'relay' 'no relay log was produced'; return }
    $bad = @(Select-String -LiteralPath $script:relayLog -Pattern 'malformed datagram' -ErrorAction SilentlyContinue)
    $k40 = @($bad | Where-Object { $_.Line -match '0x40' })
    $k41 = @($bad | Where-Object { $_.Line -match '0x41' })
    $k2a = @($bad | Where-Object { $_.Line -match '0x2a' })
    $k14 = @($bad | Where-Object { $_.Line -match '0x14' })
    $k48 = @($bad | Where-Object { $_.Line -match '0x48' })
    $k54 = @($bad | Where-Object { $_.Line -match '0x5[456]' })
    Note ("  relay malformed total={0} (0x2a={1}, 0x14={2}, 0x40={3}, 0x41={4}, 0x48={5}, 0x54-56={6})" -f `
          $bad.Count, $k2a.Count, $k14.Count, $k40.Count, $k41.Count, $k48.Count, $k54.Count)
    Verdict 'relay' $script:H ($bad.Count -eq 0) 'relay malformed total=0 (the relay forwarded every datagram)' `
            ($(if ($bad.Count) { [string]$bad[0].Line } else { 'malformed total=0' }))
    Verdict 'relay' $script:H ($k40.Count -eq 0 -and $k41.Count -eq 0) 'the relay knows KILL_XP (0x40) and SAVE (0x41)' `
            ($(if ($k40.Count -or $k41.Count) { [string]($bad | Select-Object -First 1).Line } else { 'no 0x40/0x41 complaint' }))
}

# ===========================================================================
# 1. DOORS -- open AND close, both directions
# ===========================================================================
function Test-Door([int]$H, [int]$P) {
    Step '1. DOOR: open and close, and the partner plays the door''s OWN visual' `
         'a door is animated on the receiver for both state 1 and state 0'
    $doorName = $null
    foreach ($dn in 'SwordShrineGate_BP', 'AquaGate_01_SM3', 'AquaGate_01_SM', 'Lib_Door_01_BP', 'SZ_BoringDoor') {
        # THE DOOR MUST BE A LIVE ACTOR ON **BOTH** MACHINES.  MEASURED (run 7): the probe
        # accepted `SZ_BoringDoor` from the peer alone, the peer's Activate reached the lane,
        # and the host's own leg was silent -- its copy of that door was not in a level it had
        # streamed, so there was no receiver-side actor to animate and the step reported a
        # lane failure that was really a targeting failure.  So the probe runs on both and
        # only a name that answers `Opened=0` on each is used.
        $okBoth = $true
        # NOT `$inst`: that is the GLOBAL per-machine table this script's readers index.  A
        # loop variable of that name shadows it and the next `$inst[$i].Root` is empty
        # (measured: "Cannot bind argument to parameter 'Path' because it is an empty
        # string" from inside Set-Control, one step after the probe).
        foreach ($mi in @($P, $H)) {
            $m = Get-Mark $mi
            Invoke-Act $mi ('near:' + $dn + '!?Opened')
            Start-Sleep -Seconds 3
            $line = @(Get-Since $mi $m | Where-Object { $_ -match 'dev props:|no live actor' })
            if ($line.Count -eq 0) { $okBoth = $false; break }
            Note ("  probe[{0}] {1} : {2}" -f $mi, $dn, [string]$line[-1])
            if (([string]$line[-1]) -match 'no live actor') { $okBoth = $false; break }
            # THE ACTOR THAT ANSWERED MUST BE THE ONE ASKED FOR.  MEASURED (run 11): the probe
            # asked for `Lib_Door_01_BP` and the mod resolved `Lib_Door_Frame` -- a FRAME
            # actor that shares the name prefix and has no open state at all, so every
            # subsequent verb drove the wrong object and the step failed four times about a
            # door nobody had opened.  A prefix match on the resolved name is the cheap gate.
            $resolved = ''
            if (([string]$line[-1]) -match 'dev props:\s+(\S+)') { $resolved = $Matches[1] }
            if (-not $resolved -or -not $resolved.StartsWith($dn)) {
                Note ("           resolved '{0}', not '{1}' -- skipped" -f $resolved, $dn)
                $okBoth = $false
                break
            }
            if (([string]$line[-1]) -notmatch 'Opened=0') { $okBoth = $false; break }
        }
        if ($okBoth) { $doorName = $dn; break }
    }
    if (-not $doorName) { Refuse 'door' 'no door reads Opened=0 on BOTH machines in this world'; return }
    Note "  door under test (live on both): $doorName"

    # OPEN on the peer -> the host plays its open visual.
    $mP = Get-Mark $P; $mH = Get-Mark $H
    Invoke-Go $P $doorName 160
    Start-Sleep -Seconds 7
    # A SECOND MARK for the verb: the teleport is what was measured first, and the intent
    # fires AFTER it.  MEASURED (this session's third run): the window opened at the mark
    # before the teleport, the peer's `actor-state intent sent` landed 9 s later than the
    # window's first sample, and the check reported FAIL about a lane whose host half then
    # logged the open at 1:30:48.  The mark is taken again here so the window brackets the
    # action rather than the walk.
    $mAct = Get-Mark $P
    Invoke-Act $P ('near:' + $doorName + '!Activate')
    Start-Sleep -Seconds 11
    Show-Since $P 'dev interact:|actor-state|interact:' $mAct 8
    Show-Since $H 'actor-state|interact:' $mH 10
    $openP = @(Get-Since $P $mAct | Where-Object { $_ -match 'actor-state intent sent|object intent sent|interact: door anim .*state=1|local change' })
    $openH = @(Get-Since $H $mH | Where-Object { $_ -match 'interact: door anim .*state=1|actor-state intent applied' })
    Verdict 'door' $P ($openP.Count -gt 0) 'the peer OPENED the door and the change reached a lane' ($(if ($openP.Count) { [string]$openP[-1] } else { '' }))
    Verdict 'door' $H ($openH.Count -gt 0) 'the host played the door''s own OPEN visual' ($(if ($openH.Count) { [string]$openH[-1] } else { '' }))

    # CLOSE, driven on the HOST -> the peer must animate the close.
    $mP2 = Get-Mark $P; $mH2 = Get-Mark $H
    Invoke-Go $H $doorName 160
    Start-Sleep -Seconds 7
    Invoke-Act $H ('near:' + $doorName + '!Deactivate')
    Start-Sleep -Seconds 11
    Show-Since $H 'dev interact:|actor-state|interact:' $mH2 10
    Show-Since $P 'actor-state|interact:' $mP2 12
    $closeH = @(Get-Since $H $mH2 | Where-Object { $_ -match 'forced verb Deactivate.*state 1 -> 0|interact: door anim .*state=0' })
    $closeP = @(Get-Since $P $mP2 | Where-Object { $_ -match 'interact: door anim .*state=0|actor states applied.*value=0' })
    Verdict 'door' $H ($closeH.Count -gt 0) 'the host CLOSED the door through the game''s own verb' ($(if ($closeH.Count) { [string]$closeH[-1] } else { '' }))
    Verdict 'door' $P ($closeP.Count -gt 0) 'THE PARTNER PLAYED THE CLOSE' ($(if ($closeP.Count) { [string]$closeP[-1] } else { '' }))
}

# ===========================================================================
# 2. BREAKABLES -- a barrel and a pot
# ===========================================================================
function Test-Breakables([int]$H, [int]$P) {
    Step '2. BREAKABLES: a barrel and a pot broken on A, broken on B' `
         'the class'' own Break visual, and NO drop spawned on the receiver'
    $dumpP = Invoke-ObjDump $P 'soak-break'
    $objP = Parse-ObjKeys $dumpP
    $savP = Parse-SavKeys $dumpP
    Note ("  peer sees {0} object rows, {1} savable rows" -f $objP.Count, $savP.Count)
    foreach ($grp in @($objP | Group-Object Shape)) { Note ("    shape {0} x{1}" -f $grp.Name, $grp.Count) }

    $targets = @()
    $barrel = @($objP | Where-Object { $_.Shape -eq 'barricade' -and $_.Cls -match 'Barrel' -and $_.State -eq 0 } | Select-Object -First 1)
    if ($barrel.Count) { $targets += [pscustomobject]@{ Kind = 'barrel'; Name = $barrel[0].Name; Cls = $barrel[0].Cls } }
    $pot = @($objP | Where-Object { $_.Shape -eq 'barricade' -and $_.Cls -match 'Pot' -and $_.State -eq 0 } | Select-Object -First 1)
    if ($pot.Count) { $targets += [pscustomobject]@{ Kind = 'pot'; Name = $pot[0].Name; Cls = $pot[0].Cls } }
    if ($targets.Count -eq 0) {
        Refuse 'break' ('no unbroken barrel/pot on the object lane here ({0} object rows)' -f $objP.Count)
        return
    }

    foreach ($t in $targets) {
        Note ("  breaking the {0}: {1} [{2}]" -f $t.Kind, $t.Name, $t.Cls)
        # THE ROW MUST BE A REACHABLE ACTOR, NOT JUST A PUBLISHED KEY.  MEASURED (run 8): the
        # table named a pot whose actor was in a level this machine had not streamed, so the
        # teleport answered `dev teleport near: no live actor matches 'Pot_Fat16'`, the break
        # verb ran nothing, and the step reported a lane failure that was really a targeting
        # failure.  So the teleport's own answer is checked before the verb is driven.
        $mP = Get-Mark $P; $mH = Get-Mark $H
        Invoke-Go $P $t.Name 200
        Start-Sleep -Seconds 7
        $reach = @(Get-Since $P $mP | Where-Object { $_ -match "dev teleport near '$($t.Name)'|no live actor matches" })
        if ($reach.Count -eq 0 -or ([string]$reach[-1]) -match 'no live actor matches') {
            Note ("    {0} '{1}' is not a live actor here ({2}) -- skipped" -f $t.Kind, $t.Name,
                  $(if ($reach.Count) { ([string]$reach[-1]).Trim() } else { 'no answer' }))
            Refuse 'break' ("the {0} '{1}' is in the object table but is not a live actor on this machine" -f $t.Kind, $t.Name)
            continue
        }
        Invoke-Act $P ('near:' + $t.Name + '!Break')
        Start-Sleep -Seconds 12
        Show-Since $P 'dev interact:|actor-state|object' $mP 8
        Show-Since $H 'actor-state|object' $mH 10
        $sent = @(Get-Since $P $mP | Where-Object { $_ -match 'actor-state intent sent|object intent sent|local change' })
        $applied = @(Get-Since $H $mH | Where-Object { $_ -match 'actor-state intent applied|object intent applied|object apply:' })
        $broke = @(Get-Since $H $mH | Where-Object { $_ -match 'visual=Break|visual=SetToFinalState' })
        Verdict 'break' $P ($sent.Count -gt 0) ("A's {0} break reached a lane" -f $t.Kind) ($(if ($sent.Count) { [string]$sent[-1] } else { '' }))
        Verdict 'break' $H ($applied.Count -gt 0) ("B applied the {0} break" -f $t.Kind) ($(if ($applied.Count) { [string]$applied[-1] } else { '' }))
        if ($broke.Count) { Note ("  [$H] " + [string]$broke[-1]) }
        # THE NO-DROP TRAP: the receiver must not roll its own loot for a break it was told about.
        $drop = @(Get-Since $H $mH | Where-Object { $_ -match 'loot: spawn|drop spawned' })
        Verdict 'break' $H ($drop.Count -eq 0) ("no loot drop was spawned on B for A's {0}" -f $t.Kind) ($(if ($drop.Count) { [string]$drop[-1] } else { 'none' }))
    }
}

# ===========================================================================
# 3. SECRET WALL
# ===========================================================================
function Test-SecretWall([int]$H, [int]$P) {
    Step '3. SECRET WALL: opened on A, opened on B' 'SecretWall_C carries only its own Opened bool'
    $dumpP = Invoke-ObjDump $P 'soak-wall'
    $objP = Parse-ObjKeys $dumpP
    $walls = @($objP | Where-Object { $_.Cls -match 'SecretWall|ScienceSecret|HiddenWall' -and $_.State -eq 0 })
    Note ("  secret-wall candidates on the object lane: {0}" -f $walls.Count)
    @($walls | Select-Object -First 8) | ForEach-Object { Note ("    {0} [{1}] key={2} state={3}" -f $_.Name, $_.Cls, $_.Key, $_.State) }
    if ($walls.Count -eq 0) { Refuse 'wall' 'no unopened secret wall is published in this world snapshot'; return }

    # A ROW IN THE TABLE IS NOT AN ACTOR IN REACH.  MEASURED (runs 1 and 8): the dump listed
    # `ScienceRoomDoor2`/`SecretWall14`, and driving them answered either
    # `dev interact: no live actor matches` or a teleport that left the player 21 km away
    # (`dev interact: resolved 'SecretWall14' -> ... dist=21415`) -- the row exists in the
    # published set but the actor is in a sublevel this machine has not streamed, so the verb
    # ran on nothing and the step reported a lane failure that was a targeting failure.
    #
    # THE GATE IS THE TELEPORT'S OWN ANSWER, WITH ITS DISTANCE.  The cheap `!?Opened` props
    # form resolves through the object table and can succeed where the teleport cannot, so
    # only a hop that LANDED INSIDE THE SAME ROOM (dist < 500 cm after the move) is accepted.
    $wall = $null
    $unreachable = 0
    foreach ($w in $walls) {
        $probeMark = Get-Mark $P
        Invoke-Go $P $w.Name 200
        Start-Sleep -Seconds 6
        $hop = @(Get-Since $P $probeMark | Where-Object { $_ -match "dev teleport near '$([regex]::Escape($w.Name))'|no live actor matches" })
        if ($hop.Count -eq 0 -or ([string]$hop[-1]) -match 'no live actor matches') { $unreachable++; continue }
        $dist = -1.0
        if (([string]$hop[-1]) -match 'dist=(-?[\d.]+)') { $dist = [double]$Matches[1] }
        if ($dist -lt 0 -or $dist -gt 500) {
            Note ("  {0} is in the table but the hop put the player {1} cm away -- not reachable" -f $w.Name, $dist)
            $unreachable++
            continue
        }
        $probeMark2 = Get-Mark $P
        Invoke-Act $P ('near:' + $w.Name + '!?Opened')
        Start-Sleep -Seconds 3
        $ans = @(Get-Since $P $probeMark2 | Where-Object { $_ -match 'dev props:|no live actor' })
        if ($ans.Count -eq 0) { $unreachable++; continue }
        if (([string]$ans[-1]) -match 'no live actor') { $unreachable++; continue }
        Note ("  reachable wall: {0} [{1}] dist={2} -- {3}" -f $w.Name, $w.Cls, $dist, [string]$ans[-1])
        if (([string]$ans[-1]) -match 'Opened=0') { $wall = $w; break }
    }
    if (-not $wall) {
        Refuse 'wall' ("no unopened secret wall is REACHABLE on this machine ({0} in the table, {1} not streamed in)" -f $walls.Count, $unreachable)
        return
    }
    Note ("  wall under test: {0} [{1}] key={2}" -f $wall.Name, $wall.Cls, $wall.Key)
    $mP = Get-Mark $P; $mH = Get-Mark $H
    Invoke-Go $P $wall.Name 160
    Start-Sleep -Seconds 7
    Invoke-Act $P ('near:' + $wall.Name + '!Activate')
    Start-Sleep -Seconds 10
    Show-Since $P 'dev interact:|object' $mP 8
    Show-Since $H 'object' $mH 10
    $sentP = @(Get-Since $P $mP | Where-Object { $_ -match 'object local change|object intent sent' })
    $appliedH = @(Get-Since $H $mH | Where-Object { $_ -match 'object intent applied|object apply:' })
    $visualH = @(Get-Since $H $mH | Where-Object { $_ -match 'visual=SetToFinalState|visual=SetToOpenState|visual=Activate|visual=ForceOpen' })
    Verdict 'wall' $P ($sentP.Count -gt 0) 'A''s secret-wall change reached the OBJECT lane' ($(if ($sentP.Count) { [string]$sentP[-1] } else { '' }))
    Verdict 'wall' $H ($appliedH.Count -gt 0) 'B applied A''s secret-wall change' ($(if ($appliedH.Count) { [string]$appliedH[-1] } else { '' }))
    Verdict 'wall' $H ($visualH.Count -gt 0) 'B ran the wall''s OWN open visual' ($(if ($visualH.Count) { [string]$visualH[-1] } else { '' }))
}

# ===========================================================================
# 4. KEY -- granted by the game's cheat, then USED to zero
# ===========================================================================
function Test-Key([int]$H, [int]$P) {
    Step '4. KEY: the game grants all keys on A, B receives them, then A USES its last copy' `
         'a count that falls to zero travels as a tombstone'
    $mH = Get-Mark $H; $mP = Get-Mark $P
    # NO `#nonce` HERE.  `qd.cheat` reads the WHOLE file as a UFunction name on
    # `FS_Cheats_C` (DevCheatCall), so a nonce would be part of the name: MEASURED in the
    # first run of this session as `rig: cheat GiveAllKeys#005305955 -- no such UFunction
    # on FS_Cheats_C`.  The lever re-arms on CONTENT change, so the function name alone is
    # the nonce and a re-run of the same cheat needs the file removed first.
    Set-Control $H 'qd.cheat' $null
    Start-Sleep -Milliseconds 700
    Set-Control $H 'qd.cheat' 'GiveAllKeys'
    [void](Check 'key' $H 'rig: cheat GiveAllKeys' 30 'GiveAllKeys ran on the host' $mH)
    Start-Sleep -Seconds 16
    $granted = @(Get-Since $P $mP | Where-Object { $_ -match 'inventory grant applied: /' })
    Note ("  the peer was granted {0} path(s)" -f $granted.Count)
    $keyPath = $null
    foreach ($g in $granted) {
        $line = [string]$g
        if ($line -notmatch 'inventory grant applied:\s+(\S+)') { continue }
        $path = $Matches[1]
        if ($path -and $path.Contains('KeyItems')) { $keyPath = $path; break }
    }
    Verdict 'key' $P ($null -ne $keyPath) 'a KEY path reached the peer through the inventory lane' $(if ($keyPath) { $keyPath } else { 'none' })
    if (-not $keyPath) { Refuse 'key' 'no KeyItems path crossed, so there is nothing to use'; return }

    $mH2 = Get-Mark $H; $mP2 = Get-Mark $P
    Note ("  use[$H] -> $keyPath")
    # `qd.use` takes a bare item PATH (the whole file is the path), so no nonce either.
    Set-Control $H 'qd.use' $null
    Start-Sleep -Milliseconds 700
    Set-Control $H 'qd.use' $keyPath
    [void](Check 'key' $H 'rig: use lever ->' 30 'the host consumed it through the game''s own consumer' $mH2)
    Start-Sleep -Seconds 14
    Show-Since $H 'rig: use lever|inventory host set|quest item' $mH2 8
    Show-Since $P 'quest item removed|inventory body' $mP2 10
    $rm = @(Get-Since $P $mP2 | Where-Object { $_ -match 'quest item removed here' })
    $zero = @(Get-Since $H $mH2 | Where-Object { $_ -match 'inventory host set' })
    Verdict 'key' $P ($rm.Count -gt 0) 'the host''s USE removed the peer''s copy (the zero travelled)' ($(if ($rm.Count) { [string]$rm[-1] } else { 'no removal line' }))
    Verdict 'key' $H ($zero.Count -gt 0) 'the host republished its set with the tombstone' ($(if ($zero.Count) { [string]$zero[-1] } else { '' }))
}

# ===========================================================================
# 5. KILL XP -- both machines pay
# ===========================================================================
function Test-KillXp([int]$H, [int]$P) {
    Step '5. KILL XP: a kill pays BOTH players, exactly once each' 'through the game''s real death funnel'
    # THE LANE'S OWN PREMISE IS A DEATH: `XpResolve()` runs from the death recorder, and a
    # machine that has never recorded a death has no counter to resolve (its boot line
    # `xp: the local player's counter is XPAmount` comes from the SAVE slot, and the award
    # path needs the PAWN's).  MEASURED in this session's first run: the step refused on "no
    # XP counter resolved" while the lane was healthy and paid both players later in the run.
    # So this step kills FIRST and reads the counters from the deaths it caused.
    $xpProp = $null

    foreach ($killer in @($H, $P)) {
        $other = if ($killer -eq $H) { $P } else { $H }
        $mK = Get-Mark $killer; $mO = Get-Mark $other
        $ne = @(Get-Matches $killer 'scan: nearest enemy guid=[0-9A-Fa-f]{8}')
        $k = $null; $coord = $null
        if ($ne.Count) {
            if (([string]$ne[-1]) -match 'guid=([0-9A-Fa-f]{8})') { $k = $Matches[1] }
            if (([string]$ne[-1]) -match 'at (-?[\d.]+),(-?[\d.]+),(-?[\d.]+)') { $coord = "$($Matches[1]),$($Matches[2]),$($Matches[3])" }
        }
        if (-not $k) { Refuse 'xp' ("no enemy key visible on inst$killer"); continue }
        Note ("  kill attempt by inst{0}: target guid={1}{2}" -f $killer, $k, $(if ($coord) { " at $coord" } else { '' }))
        if ($coord) { Set-Control $killer 'qd.tp' "$coord"; Start-Sleep -Seconds 8 }
        # `qd.hitguid` is the RELIABLE lever: it names the key the enemy lane itself runs.
        # `qd.hit` picks by DISTANCE and, once the engine has marked the nearby copies for
        # destruction, gives up with "no CALLABLE live enemy within 15 m ... skipped 573
        # mid-teardown" (MEASURED in this session's second run, ten times in a row).  So the
        # hit is repeated by KEY until a death line appears, and `qd.hit` is only a fallback
        # for the case where the scan had no guid at all.
        $died = $false
        for ($t = 1; $t -le 3 -and -not $died; ++$t) {
            # `:<amount>` is the lever's own damage argument.  WITHOUT IT the default is 50,
            # which is enough to wound and not to kill (MEASURED: `hp 260.0 -> 210.0` three
            # times, then the fallback brought it down); 400 kills any hub enemy outright.
            Set-Control $killer 'qd.hitguid' ("{0}:400#{1}" -f $k, $t)
            Start-Sleep -Seconds 12
            Set-Control $killer 'qd.hitguid' $null
            Start-Sleep -Milliseconds 600
            $d = @(Get-Since $killer $mK | Where-Object { $_ -match 'xp: this death paid|rig: hitguid|rig: hit enemy .*amount=' })
            $paidNow = @(Get-Since $killer $mK | Where-Object { $_ -match 'xp: this death paid|xp: paid ' })
            if ($paidNow.Count -gt 0) { $died = $true }
            else { Note ("    attempt {0}: no award yet ({1} hit line(s))" -f $t, $d.Count) }
        }
        if (-not $died -and $coord) {
            Set-Control $killer 'qd.hit' '400'
            Start-Sleep -Seconds 14
            Set-Control $killer 'qd.hit' $null
        }
        Start-Sleep -Seconds 8
        Show-Since $killer 'xp:|rig: hit' $mK 8
        Show-Since $other 'xp:' $mO 8
        $payK = @(Get-Since $killer $mK | Where-Object { $_ -match 'xp: (this death paid|paid |the partner.s death paid)' })
        $payO = @(Get-Since $other $mO | Where-Object { $_ -match 'xp: (this death paid|paid |the partner.s death paid)' })
        # THE COUNTER IS RESOLVED FROM THE DEATH, which is where the lane itself resolves it.
        if (-not $xpProp) {
            $c = @(Get-Since $killer $mK | Where-Object { $_ -match "xp: the local player's counter is" })
            if ($c.Count -and ([string]$c[-1]) -match 'counter is (\w+)') { $xpProp = $Matches[1]; Note ("  XP counter resolved on a death: {0}" -f $xpProp) }
        }
        Verdict 'xp' $killer ($payK.Count -gt 0) ("kill by inst{0}: inst{0}'s XP moved" -f $killer) ($(if ($payK.Count) { [string]$payK[-1] } else { '' }))
        Verdict 'xp' $other ($payO.Count -gt 0) ("kill by inst{0}: inst{1}'s XP moved too" -f $killer, $other) ($(if ($payO.Count) { [string]$payO[-1] } else { '' }))
        # THE UNIT OF "PAID TWICE" IS ONE ACTUAL PAYMENT, NOT ONE MENTION OF THE DEATH.  A
        # machine can legitimately log TWO SENTENCES about the same `guid#seq`: it announces
        # the partner's death ("the partner's death paid 384 there") and then, because its own
        # game paid nothing, awards it ("...and nothing here; awarding it to the local
        # player").  MEASURED (run 11): keying on `guid#seq` alone reported `3929AB69#2 x2`
        # about ONE death with one payment.  What must not happen twice is an AWARD to this
        # machine's player, so only lines that name this machine's own payment are counted.
        $awards = @()
        foreach ($set in @($payK, $payO)) {
            $awards += @($set | Where-Object { ([string]$_) -match 'xp: (this death paid|paid )' })
        }
        $dup = @($awards | ForEach-Object {
                    $s = [string]$_
                    if ($s -match 'guid=([0-9A-Fa-f]{8})(#(\d+))?') {
                        if ($Matches[3]) { "$($Matches[1])#$($Matches[3])" } else { $Matches[1] }
                    }
                  } | Group-Object | Where-Object { $_.Count -gt 1 })
        Verdict 'xp' $killer ($dup.Count -eq 0) 'no death was PAID twice on ONE machine' ($(if ($dup.Count) { ($dup | ForEach-Object { "$($_.Name) x$($_.Count)" }) -join ', ' } else { '' }))
        $sawK = @($awards | ForEach-Object { if (([string]$_) -match 'guid=([0-9A-Fa-f]{8})') { $Matches[1] } } | Sort-Object -Unique)
        Note ("    awards on inst{0}={1} inst{2}={3}; mentions inst{0}={4} inst{2}={5}" -f `
              $killer, $sawK.Count, $other,
              @($awards | Where-Object { ([string]$_) -match 'this death paid|paid \d+ to the local' }).Count,
              $payK.Count, $payO.Count)
    }
}

# ===========================================================================
# 6. REST + SAVE -- the partner writes its own
# ===========================================================================
function Test-RestSave([int]$H, [int]$P) {
    Step '6. REST/SAVE: a rest on A saves, and the PARTNER writes its own save' `
         'the request carries the route; the receiver runs its OWN matching writer'
    $beforeP = Show-Stamps $P 'before'
    $beforeH = Show-Stamps $H 'before'
    $mH = Get-Mark $H; $mP = Get-Mark $P
    Invoke-Go $H 'checkpoint' 140
    Start-Sleep -Seconds 9
    Invoke-Act $H 'near:checkpoint!Interact'
    Start-Sleep -Seconds 18
    Show-Since $H 'dev interact:|save:|checkpoint|respawn: rest' $mH 14
    Show-Since $P 'save:' $mP 14

    $restH = @(Get-Since $H $mH | Where-Object { $_ -match 'respawn: rest at |dev interact: forced verb Interact' })
    Verdict 'save' $H ($restH.Count -gt 0) 'A reached the checkpoint and its own rest/verb ran' ($(if ($restH.Count) { [string]$restH[-1] } else { '' }))

    # THE SAVE DETECTOR: the game's single write primitive, or its own timed suspend loop.
    $savedH = @()
    $deadline = (Get-Date).AddSeconds(150)
    while ((Get-Date) -lt $deadline) {
        $savedH = @(Get-Since $H $mH | Where-Object { $_ -match 'save: this machine saved|save: the game.s own writer ran' })
        if ($savedH.Count -gt 0) { break }
        if (-not (Test-Alive $H)) { break }
        Start-Sleep -Seconds 3
    }
    Show-Since $H 'save:' $mH 12
    Verdict 'save' $H ($savedH.Count -gt 0) 'A''s own save was DETECTED' ($(if ($savedH.Count) { [string]$savedH[-1] } else { 'no save: line' }))

    # THE PARTNER'S MIRROR.  Either it announced the partner's save, or it ran its own writer.
    $mirrorP = @(Wait-Since $P 'save: the partner saved|save: wrote this machine|save: the suspend writer returned' $mP 90)
    Show-Since $P 'save:' $mP 12
    Verdict 'save' $P ($mirrorP.Count -gt 0) 'THE PARTNER answered the save with its own writer' ($(if ($mirrorP.Count) { [string]$mirrorP[-1] } else { '' }))
    Start-Sleep -Seconds 8
    $afterP = Show-Stamps $P 'after'
    $moved = $false
    foreach ($a in @($afterP)) {
        $b = @($beforeP | Where-Object { $_.Name -eq $a.Name })
        if ($b.Count -and $b[0].Stamp -ne $a.Stamp) { $moved = $true; Note ("    [P] {0} moved {1} -> {2}" -f $a.Name, $b[0].Stamp, $a.Stamp) }
    }
    Verdict 'save' $P $moved 'B''s OWN save file timestamp moved on disk' ''

    # NO ECHO: the mirrored write must not be re-announced as a fresh save.
    $echo = @(Get-Since $P $mP | Where-Object { $_ -match 'save: swallowed the echo' })
    Note ("  [P] echo-bracket lines: {0}" -f $echo.Count)
    if ($echo.Count) { Note ("    " + [string]$echo[-1]) }
    return $mH
}

# ===========================================================================
# 7. DIALOGUE -- a conversation, and an ADVANCE the partner follows
# ===========================================================================
function Test-Dialogue([int]$H, [int]$P) {
    Step '7. DIALOGUE: A talks to a hub NPC, B shows the view, and B follows an ADVANCE' `
         'the advance lives on the DialogueScreen WIDGET (ConfirmPressed), not on the manager'
    # THE WATCHER'S MARK IS TAKEN BEFORE THE CONVERSATION STARTS.  MEASURED (run 12): the
    # host logged `dialogue view: SHOW from slot 1 asset=Ghost_Raider_2_Dialogue` one second
    # after the peer's publish -- i.e. BEFORE this step's old mark, which was taken after
    # `Check` had already waited for the conversation.  The spectator worked; the window
    # started after the event.  Every dialogue check below shares this one mark.
    $mH0 = Get-Mark $H
    $mP0 = Get-Mark $P
    Invoke-Go $P 'npc' 200
    Start-Sleep -Seconds 9
    Invoke-Act $P 'near:npc'
    $started = Check 'dlg' $P 'dialogue: local started' 45 'a conversation started on the peer (A)' $mP0
    if (-not $started) {
        Show-Since $P 'dialogue:|dev interact:|rig: no live|near:' $mP0 12
        Refuse 'dlg' 'A never started a conversation, so there is nothing to spectate'
        return
    }
    Show-Since $P 'dialogue: local started|dialogue view: publish' $mP0 6

    [void](Check 'dlg' $H 'dialogue view: SHOW from slot' 25 'B engaged the spectator view' $mH0)
    [void](Check 'dlg' $H 'dialogue view: widget visible=' 10 'B reports whether its own dialogue widget came up' $mH0)
    [void](Check 'dlg' $H 'dialogue view: camera -> ' 10 'B blended its camera to the speaker' $mH0)
    [void](Check 'dlg' $H 'dialogue view: input locked' 10 'B locked its own movement' $mH0)
    Show-Since $H 'dialogue view:|dialogue: BLOCK' $mH0 14
    # IF B DID NOT ENGAGE, SAY WHY.  The veto has five causes and only one of them is a bug,
    # so the reason the lane itself printed is the difference between "correctly declined"
    # and "stuck on a stale flag".  Without this a FAIL here is unattributable.
    $show = @(Get-Since $H $mH0 | Where-Object { $_ -match 'dialogue view: SHOW from slot' })
    if ($show.Count -eq 0) {
        $veto = @(Get-Since $H $mH0 | Where-Object { $_ -match 'dialogue view: not hijacking' })
        if ($veto.Count) {
            Verdict 'dlg' $H $false 'B engaged the spectator view' ([string]$veto[-1])
            Refuse 'dlg' ('the spectator lane declined and named its reason: ' + ([string]$veto[-1]).Trim())
        }
        return
    }

    # THE ADVANCE.
    $mH1 = Get-Mark $H; $mP1 = Get-Mark $P
    Set-Control $P 'qd.dlg' ('cursor#' + (Get-Date -Format 'HHmmssfff'))
    Start-Sleep -Seconds 2
    $curBefore = @(Get-Since $P $mP1 | Where-Object { $_ -match 'rig: dialogue cursor' })
    if ($curBefore.Count) { Note "  [P] BEFORE $([string]$curBefore[-1])" }
    Set-Control $P 'qd.dlg' ('advance#' + (Get-Date -Format 'HHmmssfff'))
    Start-Sleep -Seconds 4
    $curAfter = @(Get-Since $P $mP1 | Where-Object { $_ -match 'rig: dialogue cursor \(after the press\)' })
    if ($curAfter.Count) { Note "  [P] AFTER  $([string]$curAfter[-1])" }
    $press = @(Get-Since $P $mP1 | Where-Object { $_ -match 'rig: dialogue widget pressed' })
    Verdict 'dlg' $P ($press.Count -gt 0) 'the rig pressed the dialogue WIDGET''s own continue' ($(if ($press.Count) { [string]$press[-1] } else { 'no press line' }))
    $seqB = -1; $entB = -1
    if ($curBefore.Count -and ([string]$curBefore[-1]) -match 'sequenceIndex=(-?\d+) line=(\d+) entry=(\d+)') { $seqB = [int]$Matches[1]; $entB = [int]$Matches[3] }
    $seqA = -1; $entA = -1
    if ($curAfter.Count -and ([string]$curAfter[-1]) -match 'sequenceIndex=(-?\d+) line=(\d+) entry=(\d+)') { $seqA = [int]$Matches[1]; $entA = [int]$Matches[3] }
    Verdict 'dlg' $P ($seqB -ge 0 -and $seqA -ge 0 -and ($seqA -ne $seqB -or $entA -ne $entB)) 'A''s own cursor advanced' ("sequenceIndex {0}->{1} entry {2}->{3}" -f $seqB, $seqA, $entB, $entA)
    $upd = @(Get-Since $H $mH1 | Where-Object { $_ -match 'dialogue view: update line=' })
    Verdict 'dlg' $H ($upd.Count -gt 0) 'B followed the advance and re-drove its own widget' ($(if ($upd.Count) { [string]$upd[-1] } else { 'no update line' }))
    if ($upd.Count -and $seqA -ge 0 -and ([string]$upd[-1]) -match 'update line=(\d+)') {
        Verdict 'dlg' $H ([int]$Matches[1] -eq $seqA) 'B shows the same line A is on' ("B line=$($Matches[1]), A=$seqA")
    }

    # END THE CONVERSATION so the next step is not fighting a locked camera.
    Set-Control $P 'qd.dlg' ('leave#' + (Get-Date -Format 'HHmmssfff'))
    Start-Sleep -Seconds 5
    $hid = @(Get-Since $H $mH1 | Where-Object { $_ -match 'dialogue view: HIDE' })
    if ($hid.Count) { Note ("  [H] " + [string]$hid[-1]) }
}

# ===========================================================================
# 8. DEATH -> RESPAWN (penalty) -> REST (restore)
# ===========================================================================
function Test-DeathRestore([int]$H, [int]$P) {
    Step '8. DEATH: a death on A, the penalty, the respawn, and a rest that restores 100 %' `
         'the world reload is refused on BOTH machines; B only sees the body go down and come back'
    # A REAL death: the health floor is a latch and would silently undo every penalty.
    foreach ($i in @($H, $P)) { Set-Control $i 'qd.god' $null }
    foreach ($i in @($H, $P)) {
        $sc = Join-Path $gameDir ("qdcoop-{0}-respawn.ini" -f $inst[$i].Suffix)
        if (Test-Path -LiteralPath $sc) { Remove-Item -LiteralPath $sc -Force -ErrorAction SilentlyContinue }
    }
    Start-Sleep -Seconds 3
    $wH0 = Get-WorldState $H; $wP0 = Get-WorldState $P
    Note ("  world before: HOST='{0}' PEER='{1}'" -f $wH0.World, $wP0.World)

    # A rest point first, so the respawn point is one this session watched.
    #
    # THE CHECKPOINT MUST BE IN A GAMEPLAY LEVEL.  MEASURED (this session's second run): the
    # rig's "nearest checkpoint" resolved a `Checkpoint` placed in
    # `//Game/UI/Menus/OptionsMenu/VideoConfigMap/...`, a MENU sublevel of the same persistent
    # world.  The lane correctly refused it --
    #   respawn: a rest ran on a checkpoint in a MENU map (...); that is not a place a respawn
    #   may put a player, so no rest point is recorded
    # -- and the step then had nowhere to respawn.  The refusal is the lane working; the rig
    # was aiming at the wrong actor.  So the peer is put on the GR camp's own checkpoint floor
    # first (`11297,-2494,1120`, measured over several sessions) and the nearby checkpoint is
    # pressed from there, with a second attempt from the world's own checkpoint target.
    $rest = @()
    $menuRefusals = 0
    foreach ($attempt in 1, 2, 3) {
        if ($attempt -eq 1) {
            Set-Control $P 'qd.tp' ('11297,-2494,1120,0#' + (Get-Date -Format 'HHmmssfff'))
        } elseif ($attempt -eq 2) {
            Invoke-Go $P 'checkpoint' 140
        } else {
            # From the camp floor, walk to whatever checkpoint the world reports nearest.
            Set-Control $P 'qd.tp' ('11297,-2494,1120,0#' + (Get-Date -Format 'HHmmssfff'))
            Start-Sleep -Seconds 4
            Invoke-Go $P 'checkpoint' 220
        }
        Start-Sleep -Seconds 7
        $mP = Get-Mark $P
        Invoke-Act $P 'near:checkpoint'
        $rest = @(Wait-Since $P 'respawn: rest at ' $mP 25)
        if ($rest.Count -gt 0) { break }
        $menu = @(Get-Since $P $mP | Where-Object { $_ -match 'checkpoint in a MENU map' })
        if ($menu.Count) {
            $menuRefusals++
            Note ("    attempt {0}: the lane REFUSED this checkpoint (menu map): {1}" -f $attempt, [string]$menu[-1])
        } else {
            Note ("    attempt {0} recorded no rest point; re-targeting" -f $attempt)
        }
    }
    Verdict 'death' $P ($rest.Count -gt 0) 'the rest point was recorded' ($(if ($rest.Count) { [string]$rest[-1] } else { 'no respawn: rest line' }))
    if ($rest.Count -eq 0) {
        Refuse 'death' ("no rest point could be recorded ({0} attempt(s) hit a MENU-map checkpoint the lane correctly refuses), so a respawn has nowhere to land" -f $menuRefusals)
        return
    }

    # DEATH.
    $mP2 = Get-Mark $P; $mH2 = Get-Mark $H
    Set-Control $P 'qd.kill' ('1#' + (Get-Date -Format 'HHmmssfff'))
    $kill = @(Wait-Since $P 'rig: kill -> FS_PlayerChar_C.Kill ran=1' $mP2 25)
    Verdict 'death' $P ($kill.Count -gt 0) 'the rig drove the game''s own death entry' ($(if ($kill.Count) { [string]$kill[-1] } else { '' }))
    $d1 = @(Wait-Since $P 'respawn: DEATH #1' $mP2 20)
    Verdict 'death' $P ($d1.Count -gt 0) 'the lane saw the death and stacked 10 %' ($(if ($d1.Count) { [string]$d1[-1] } else { '' }))

    # THE CONTINUE PRESS DURING THE POSE (the documented risk), twice.
    $mPress = Get-Mark $P; $mPressH = Get-Mark $H
    Set-Control $P 'qd.dlg' ('death-continue#' + (Get-Date -Format 'HHmmssfff'))
    Start-Sleep -Milliseconds 1200
    Set-Control $P 'qd.dlg' ('death-continue#' + (Get-Date -Format 'HHmmssfff'))
    [void](Wait-Since $P 'respawn: respawned at ' $mP2 45)
    Show-Since $P 'rig: death Continue pressed|respawn: BLOCK ' $mPress 8
    $blk = @(Get-Since $P $mPress | Where-Object { $_ -match 'respawn: BLOCK the world reload|respawn: BLOCK the death screen|respawn: BLOCK the game.s death flow' })
    Verdict 'death' $P ($blk.Count -gt 0) 'a Continue press DURING the pose was refused' ($(if ($blk.Count) { [string]$blk[-1] } else { 'no BLOCK line' }))
    $loadP = @(Get-Since $P $mPress | Where-Object { $_ -match 'world: dropping every world-scoped handle|LoadMapWithLoadingScreen' })
    $loadH = @(Get-Since $H $mPressH | Where-Object { $_ -match 'world: dropping every world-scoped handle|LoadMapWithLoadingScreen' })
    Verdict 'death' $P ($loadP.Count -eq 0) 'the press did NOT reload the dying machine''s world' ($(if ($loadP.Count) { [string]$loadP[-1] } else { 'no teardown' }))
    Verdict 'death' $H ($loadH.Count -eq 0) "the PARTNER's world was not reloaded" ($(if ($loadH.Count) { [string]$loadH[-1] } else { 'no teardown' }))

    $up1 = @(Wait-Since $P 'respawn: respawned at ' $mP2 45)
    Verdict 'death' $P ($up1.Count -gt 0) 'A respawned at its rest point' ($(if ($up1.Count) { [string]$up1[-1] } else { '' }))
    $base1 = 0.0; $hp1 = -1.0
    if ($up1.Count -and ([string]$up1[-1]) -match 'Health -> ([0-9.]+) \(base ([0-9.]+) x ') { $hp1 = [double]$Matches[1]; $base1 = [double]$Matches[2] }
    Verdict 'death' $P ($base1 -gt 0 -and [math]::Abs($hp1 - ($base1 * 0.90)) -le 1.5) 'the restored HP is 90 % of the base' ("hp={0} want={1} (base {2})" -f $hp1, ($base1 * 0.90), $base1)
    $upLine = if ($up1.Count) { [string]$up1[-1] } else { '' }
    Verdict 'death' $P ($upLine -match 'input=1') 'the input gate was really enabled' ''
    Verdict 'death' $P ($upLine -match 'walking=1') 'the movement component was set back to Walking' ''

    # THE PARTNER'S SIDE.
    $down = @(Wait-Since $H 'respawn: partner went DOWN' $mH2 30)
    Verdict 'death' $H ($down.Count -gt 0) 'B saw the body go down' ($(if ($down.Count) { [string]$down[-1] } else { '' }))
    $pup = @(Wait-Since $H 'respawn: partner respawned at ' $mH2 45)
    Verdict 'death' $H ($pup.Count -gt 0) 'B teleported the puppet to the rest point' ($(if ($pup.Count) { [string]$pup[-1] } else { '' }))
    $wH2 = Get-WorldState $H
    Verdict 'death' $H ($wH2.World -eq $wH0.World -and $wH2.World -ne '') "B's world is unchanged" ("'{0}' -> '{1}'" -f $wH0.World, $wH2.World)

    # NO INTERFERENCE: the spectator must NOT have been engaged by a death.
    $spec = @(Get-Since $H $mH2 | Where-Object { $_ -match 'dialogue view: SHOW from slot' })
    Verdict 'death' $H ($spec.Count -eq 0) 'no dialogue spectator was engaged by the death (no cross-lane interference)' ($(if ($spec.Count) { [string]$spec[-1] } else { 'none' }))
    # NO PARTNER SAVE WHILE DEAD: a save must not be mirrored while its owner is down.
    $saveWhileDown = @(Get-Since $P $mP2 | Where-Object { $_ -match 'save: the partner saved|save: wrote this machine' } |
                       Where-Object { (Get-LineMs ([string]$_)) -le ((Get-LineMs ([string]$(if ($up1.Count) { $up1[-1] } else { '' }))) + 0) })
    if ($saveWhileDown.Count) { Note ("  [P] a save line landed inside the down window: " + [string]$saveWhileDown[-1]) }
    else { Note '  [P] no save line landed inside the down window' }

    # THE CAP: the penalty must hold while the player is alive.
    Start-Sleep -Seconds 18
    $hpLine = @(Get-Matches $P 'scan: health ') | Select-Object -Last 1
    $hpNow = -1.0
    if ($hpLine -and ([string]$hpLine) -match 'scan: health (-?[0-9.]+)') { $hpNow = [double]$Matches[1] }
    Note ("  peer health now: {0}" -f $(if ($hpNow -ge 0) { $hpNow } else { 'unreadable' }))
    Verdict 'death' $P ($base1 -le 0 -or ($hpNow -ge 0 -and $hpNow -le ($base1 * 0.90 + 1.5))) 'Health is held at or below the penalised maximum' ("health=$hpNow cap=$($base1 * 0.90)")

    # THE REST RESTORES 100 %.
    Step '8b. REST restores 100 % and clears the stack'
    $mP3 = Get-Mark $P
    Invoke-Go $P 'checkpoint' 140
    Start-Sleep -Seconds 9
    Invoke-Act $P 'near:checkpoint'
    $rest2 = @(Wait-Since $P 'respawn: rest at |respawn: stacks cleared|respawn: REST' $mP3 30)
    Show-Since $P 'respawn:|dev interact:' $mP3 10
    Verdict 'death' $P ($rest2.Count -gt 0) 'the rest cleared the penalty' ($(if ($rest2.Count) { [string]$rest2[-1] } else { '' }))
    $restoreLine = @(Get-Since $P $mP3 | Where-Object { $_ -match 'stack\(s\) cleared|max back to 100' })
    if ($restoreLine.Count) { Note ("  " + [string]$restoreLine[-1]) }
    # A REST IS ALSO A LIVE CHECK OF THE DEATH-STATE FIX: after a respawn the player must be
    # readable as ALIVE by every other lane, or the spectator/save/weapon lanes all refuse.
    $stillDead = @(Get-Since $P 0 | Where-Object { $_ -match 'STILL SET \(bDead/IsDead read back non-zero\)' })
    Verdict 'death' $P ($stillDead.Count -eq 0) 'the respawn left NO engine dead flag set (every lane reads IsDead)' ($(if ($stillDead.Count) { [string]$stillDead[-1] } else { 'no STILL SET line' }))
}

# ===========================================================================
# 9. WEAPONS -- three families equipped + attacked, and a crossbow shot
# ===========================================================================
function Test-Weapons([int]$H, [int]$P) {
    Step '9. WEAPONS: three families equipped and attacked, then a crossbow shot' `
         'the partner draws each weapon and receives each action kind'
    $weapons = @(
        '/Game/Items/Weapons/Axe/SimpleAxe_Item.SimpleAxe_Item',
        '/Game/Items/Weapons/CyberHammer/CyberHammer_Item.CyberHammer_Item',
        '/Game/Items/Weapons/BasicSpear/BasicSpear_Item.BasicSpear_Item'
    )
    $slot = 0
    foreach ($w in $weapons) {
        $slot++
        $name = [System.IO.Path]::GetFileName($w)
        $mE = Get-Mark $H; $mP = Get-Mark $P
        Set-Control $H 'qd.equip' ("{0},0#{1}" -f $w, $slot)
        Start-Sleep -Seconds 4
        $eq = @(Get-Since $H $mE | Where-Object { $_ -match 'rig: equip' })
        # THE PARTNER DRAWS ON THE NEXT EQUIPMENT BODY, NOT INSTANTLY.  MEASURED (run 7): the
        # peer's `body: hand R now holds` landed 5-10 s after the equip for the hammer and the
        # spear (the equipment lane republishes on change + 1 Hz, and the body re-dresses on
        # the next tick it is allowed to).  A fixed 4 s read therefore saw the axe and missed
        # the other two while all three had in fact crossed, so the peer line is WAITED for.
        $peerHeld = @(Wait-Since $P 'body: hand R now holds|body: hand L now holds' $mP 14)
        Verdict 'weapon' $H (@($eq | Where-Object { $_ -match 'ran=1' }).Count -gt 0) ("EquipItem " + $name) ($eq | Select-Object -Last 1)
        Verdict 'weapon' $P ($peerHeld.Count -gt 0) ("partner draws " + $name) ($peerHeld | Select-Object -Last 1)

        $mA = Get-Mark $H; $mPA = Get-Mark $P
        Set-Control $H 'qd.attack' ("Attack,300,2#{0}" -f $slot)
        Start-Sleep -Seconds 4
        $pressS = @(Get-Since $H $mA | Where-Object { $_ -match 'rig: attack .*fired' })
        $montS = @(Get-Since $H $mA | Where-Object { $_ -match 'local montage' })
        $actP = @(Get-Since $P $mPA | Where-Object { $_ -match 'peer action kind' })
        Verdict 'weapon' $H ($pressS.Count -gt 0) ("attack input fired (" + $name + ")") ($pressS | Select-Object -Last 1)
        Verdict 'weapon' $H ($montS.Count -gt 0) ("the game played a montage (" + $name + ")") ($montS | Select-Object -Last 1)
        Verdict 'weapon' $P ($actP.Count -gt 0) ("partner played the action kind (" + $name + ")") ($actP | Select-Object -Last 1)
        Set-Control $H 'qd.attack' $null
    }

    Step '9b. CROSSBOW: a projectile through the game''s own fire path'
    # THE ASSET PATH IS CASE-SENSITIVE AND THE DIRECTORY IS `Luudecrossbow` (lowercase c).
    # MEASURED (this session's fourth run): `Weapons/Crossbow/LuudeCrossbow_Item` answered
    # `rig: equip -- cannot load ...`, and the earlier gun-proof session had already
    # established the working path in `session-gun.ps1`.  Each candidate is tried against
    # what the mod LOGS, not against a guess about what should exist.
    $gun = $null
    foreach ($cand in @(
        '/Game/Items/Weapons/Luudecrossbow/LuudeCrossbow_Item.LuudeCrossbow_Item',
        '/Game/Items/Weapons/HeavyRifle/HeavyRifle_Item.HeavyRifle_Item'
    )) {
        $mTry = Get-Mark $H
        Set-Control $H 'qd.equip' ("{0},0#{1}" -f $cand, (Get-Date -Format 'HHmmssfff'))
        Start-Sleep -Seconds 4
        $eqTry = @(Get-Since $H $mTry | Where-Object { $_ -match 'rig: equip|rig: equip -- cannot load' })
        if ($eqTry.Count -and ([string]$eqTry[-1]) -match 'ran=1') { $gun = $cand; Note "  ranged weapon: $cand"; break }
        Note ("  {0} did not equip ({1})" -f $cand, $(if ($eqTry.Count) { ([string]$eqTry[-1]).Trim() } else { 'no answer' }))
    }
    $mE = Get-Mark $H; $mP = Get-Mark $P
    $eq = @(Get-Since $H $mE | Where-Object { $_ -match 'rig: equip' })
    Verdict 'gun' $H ($null -ne $gun) 'a ranged weapon equipped' ($(if ($gun) { $gun } else { 'no candidate loaded' }))
    if (-not $gun) { Refuse 'gun' 'no ranged weapon asset could be loaded, so there is nothing to fire'; return }
    # THE FIRE IS RETRIED.  MEASURED (run 7): the crossbow equipped and the attack event
    # fired, but the game answered `montage none => none` -- the pawn was still in the
    # previous weapon's recovery, so the fire was swallowed.  A gun that is not ready is not
    # a broken projectile lane, so the shot is taken again after the recovery.
    $projS = @(); $projP = @()
    for ($try = 1; $try -le 3 -and $projS.Count -eq 0; ++$try) {
        $mA = Get-Mark $H; $mPA = Get-Mark $P
        Set-Control $H 'qd.attack' ("Attack,150#gun{0}" -f $try)
        Start-Sleep -Seconds 6
        Show-Since $H 'rig: attack|local montage|effect: local' $mA 8
        Show-Since $P 'effect: .*shown|effect: impact|projectile' $mPA 10
        $projS = @(Get-Since $H $mA | Where-Object { $_ -match 'effect: local' })
        $projP = @(Get-Since $P $mPA | Where-Object { $_ -match 'effect: .*shown|effect: impact' })
        if ($projS.Count -eq 0) { Note ("    shot {0}: no projectile yet" -f $try) }
    }
    Verdict 'gun' $H ($projS.Count -gt 0) 'the shot spawned a projectile' ($(if ($projS.Count) { [string]$projS[-1] } else { 'no effect: local line' }))
    Verdict 'gun' $P ($projP.Count -gt 0) 'the partner showed the projectile/impact' ($(if ($projP.Count) { [string]$projP[-1] } else { '' }))
    Set-Control $H 'qd.attack' $null
}

# ===========================================================================
# 10. LIFT -- a rider on a moving platform
# ===========================================================================
function Test-Lift([int]$H, [int]$P) {
    Step '10. LIFT: a rider on a moving platform, and the partner draws it' `
         'PLATFORM_RIDE carries the place IN the platform''s axes, so a rider does not jitter'
    $LIFT = 'Mausoleum_Lift_00_BP2'
    $LX = 7757; $LY = 11111; $LZ = 716
    Invoke-Hard $H '400_MAUS_entrance'
    Start-Sleep -Seconds 18
    Show-Since $H 'rig: hard lever ->|streamed=' 3

    $mR = Get-Mark $H
    Set-Control $H 'qd.tp' ("{0},{1},{2},0#{3}" -f $LX, $LY, ($LZ + 120), (Get-Date -Format 'HHmmssfff'))
    Start-Sleep -Seconds 5
    Set-Control $P 'qd.tp' ('near:' + $LIFT + ',' + (Get-Date -Format 'HHmmssfff'))
    Start-Sleep -Seconds 5
    Show-Since $H 'platforms: ride local' $mR 6
    $board = @(Wait-Since $H 'platforms: ride local place .*on id=' $mR 16)
    Verdict 'lift' $H ($board.Count -gt 0) ("the rider is ON " + $LIFT) ($(if ($board.Count) { [string]$board[-1] } else { 'no ride line' }))
    if ($board.Count -eq 0) { Refuse 'lift' 'the rider never registered as being on the lift'; return }

    # WHICH ELEMENT IS THE PLATFORM, AND WHAT IS ITS INDEX?
    #
    # The module keeps a mover's elements SORTED BY NAME (`proxy.cpp:15521-15531` inserts each
    # new child in name order), and the log row it prints is a SNAPSHOT taken at insertion:
    #   platforms: element <at+1>/<count-at-that-moment> of id=<low word> is <name>
    # so the first number is NOT the element's final index.  MEASURED (runs 10-13): the table
    # printed `element 1/4 ... is ElevatorPlatformRoot` and `element 1/5 ... is Cube`, i.e. both
    # as "1", and driving element 1 moved `Cube` -- the rig's "the rider is carried while it
    # moves" then failed about a platform it had never moved.
    #
    # So the FINAL index is recomputed the way the module builds it: take the set of names for
    # THIS platform, sort them the same way, and read the position.  That is a reconstruction
    # of the module's own rule, not a guess about which number in the log to trust.
    #
    # The id in the log is the LOW WORD (`hex32`), so the filter is on that prefix.
    $el = -1
    $elemRows = @(Read-LogLines $inst[$H].Log | Where-Object { $_ -match 'platforms: element \d+/\d+ of id=(\w+) is (\S+)' })
    $liftId = $null
    if ($board.Count -and ([string]$board[-1]) -match 'on id=(\w+)') { $liftId = $Matches[1] }
    $names = @()
    foreach ($row in $elemRows) {
        $s = [string]$row
        if ($s -notmatch 'platforms: element \d+/\d+ of id=(\w+) is (\S+)') { continue }
        if ($liftId -and $Matches[1] -ne $liftId) { continue }
        if ($names -notcontains $Matches[2]) { $names += $Matches[2] }
    }
    if ($names.Count) {
        # The module orders by name (`strcmp < 0`), which for these ASCII names is the same as
        # PowerShell's case-sensitive ordinal sort.
        $sorted = @($names | Sort-Object -CaseSensitive)
        Note ("  element table for id={0} ({1} names, name-ordered as the module builds it): {2}" -f `
              $(if ($liftId) { $liftId } else { '?' }), $sorted.Count, ($sorted -join ', '))
        for ($k = 0; $k -lt $sorted.Count; ++$k) {
            if ($sorted[$k] -in @('ElevatorPlatformRoot', 'ElevatorRoot', 'PlatformRoot')) {
                $el = $k + 1
                Note ("  platform element: {0} -> {1} (name-sorted position)" -f $sorted[$k], $el)
                break
            }
        }
    }
    if ($el -lt 1) {
        # No named platform component: move the ACTOR ROOT (element 0), which every mover has.
        Note '  (no named platform component for this mover: the nudge will move the actor root)'
        $el = 0
    }
    Note "  elevator platform element on the rider's machine: $el"

    $mN = Get-Mark $H; $mW = Get-Mark $P
    Set-Control $H 'qd.mover' ("{0},400,20,{1}#{2}" -f $LIFT, $el, (Get-Random))
    Start-Sleep -Seconds 2
    for ($k = 1; $k -le 3; ++$k) { Start-Sleep -Milliseconds 700 }
    Start-Sleep -Seconds 5
    Show-Since $H 'rig: mover nudge|rig: mover --|platforms: mover id=|platforms: ride local place' $mN 8
    Show-Since $P 'movers: applied|platforms: partner is on a platform' $mW 8
    $own = @(Get-Since $H $mN | Where-Object { $_ -match 'rig: mover nudge' })
    $rows = @(Get-Since $P $mW | Where-Object { $_ -match 'movers: applied \d+ owner row' })
    $moving = @(Get-Since $H $mN | Where-Object { $_ -match 'ride local place .*moving' })
    $peerDraws = @(Get-Since $P $mW | Where-Object { $_ -match 'platforms: partner is on a platform' })
    Verdict 'lift' $H ($own.Count -gt 0) 'the OWNER drove the platform element' ($(if ($own.Count) { [string]$own[-1] } else { '' }))
    Verdict 'lift' $P ($rows.Count -gt 0) 'the FOLLOWER applied the owner rows' ($(if ($rows.Count) { [string]$rows[-1] } else { '' }))
    Verdict 'lift' $H ($moving.Count -gt 0) 'the rider place is carried while the platform MOVES' ($(if ($moving.Count) { [string]$moving[-1] } else { '' }))
    Verdict 'lift' $P ($peerDraws.Count -gt 0) 'the partner machine draws the rider on ITS copy' ($(if ($peerDraws.Count) { [string]$peerDraws[-1] } else { '' }))
    Set-Control $H 'qd.mover' $null
}

# ===========================================================================
# SOAK -- idle measured body between the features
# ===========================================================================
function Invoke-SoakWindow([int]$H, [int]$P, [int]$seconds, [string]$tag) {
    Step ("SOAK {0}s typical play -- both players moving, hitting, streaming" -f $seconds) `
         'no feature is being driven here: the point is that nothing falls over when they are all ON'
    $m0H = Get-Mark $H; $m0P = Get-Mark $P
    $t0 = Get-Date
    $iter = 0
    while (((Get-Date) - $t0).TotalSeconds -lt $seconds) {
        $iter++
        # A hop each, so the world keeps streaming and the players keep moving.
        Invoke-Go $H 'enemy' 500
        Start-Sleep -Seconds 2
        Invoke-Go $P 'npc' 500
        Start-Sleep -Seconds 2
        # A few hits, so the combat lanes tick under the same switches.
        Set-Control $H 'qd.hit' '60'
        Start-Sleep -Seconds 4
        Set-Control $H 'qd.hit' $null
        Set-Control $P 'qd.hit' '60'
        Start-Sleep -Seconds 4
        Set-Control $P 'qd.hit' $null
        Start-Sleep -Seconds 6
        if (-not (Test-Alive $H) -or -not (Test-Alive $P)) { Say "  INSTANCE DIED during the soak window (iteration $iter)"; break }
        if (($iter % 4) -eq 0) { Say ("  ... soak {0:N0}s / {1}s" -f ((Get-Date) - $t0).TotalSeconds, $seconds) }
    }
    $elapsed = ((Get-Date) - $t0).TotalSeconds
    Note ("  soak window {0} ran {1:N0} s in {2} iterations" -f $tag, $elapsed, $iter)
    Verdict 'soak' $H (Test-Alive $H) 'the host survived the soak window' ''
    Verdict 'soak' $P (Test-Alive $P) 'the peer survived the soak window' ''
    foreach ($i in @($H, $P)) {
        $div = @(Get-Since $i $m0H | Where-Object { $_ -match 'WORLD DIVERGENCE' })
        Note ("  [{0}] world-divergence lines in the window: {1}" -f $i, $div.Count)
    }
    return $elapsed
}

# ===========================================================================
# 11. END -- survival + the feature-interference audit
# ===========================================================================
function Test-End([int]$H, [int]$P, [int]$soakSeconds) {
    Step '11. SURVIVAL and the interference audit' `
         'a lane that breaks another lane passes every per-lane run and fails here'

    foreach ($i in @($H, $P)) {
        Verdict 'alive' $i (Test-Alive $i) 'still running at the end' ''
        $fault = @(Get-RunLines $i | Where-Object { $_ -match '(\sFAULT\b)|Unhandled Exception|EXCEPTION_ACCESS_VIOLATION|NULL_CLASS_PTR' })
        Verdict 'alive' $i ($fault.Count -eq 0) 'no FAULT / access violation' ($(if ($fault.Count) { [string]$fault[-1] } else { '' }))
        $err = @(Get-RunLines $i | Where-Object { $_ -match '\bERR\b' })
        Note ("  [{0}] ERR lines: {1} (a lane's refusal is counted separately from a crash)" -f $i, $err.Count)
        @($err | Select-Object -First 4) | ForEach-Object { Note ("     " + ([string]$_).Trim()) }
        $crash = @(Get-ChildItem -LiteralPath $inst[$i].Crashes -ErrorAction SilentlyContinue)
        Note ("  [{0}] crash dumps: {1}" -f $i, $crash.Count)
    }

    # THE RELAY, again -- a lane that only started being used later could still have tripped it.
    $bad = @(Select-String -LiteralPath $script:relayLog -Pattern 'malformed datagram' -ErrorAction SilentlyContinue)
    Verdict 'relay' $H ($bad.Count -eq 0) 'the relay still reports malformed total=0 at the end' `
            ($(if ($bad.Count) { ("{0} line(s), first: {1}" -f $bad.Count, [string]$bad[0].Line) } else { 'malformed total=0' }))

    # THE INTERFERENCE AUDIT.  Each pair below is a MEASURED cross-lane hazard from an
    # earlier session; here they are counted over the whole run rather than per step.
    Step '11b. Feature-interference audit' 'each pair is a hazard measured in an earlier session'
    $allH = Get-RunLines $H; $allP = Get-RunLines $P

    # (a) a spectator must not be engaged while a resapwn is in progress.  The lane's own
    #     veto exists for this ("a player being HIT is not hijacked"); a death is stronger.
    $hazardA = 0
    foreach ($i in @($H, $P)) {
        $lines = Get-RunLines $i
        $lastDown = -1
        foreach ($ln in $lines) {
            $s = [string]$ln
            if ($s -match 'respawn: DEATH #|respawn: partner went DOWN') { $lastDown = Get-LineMs $s }
            if ($lastDown -ge 0 -and $s -match 'dialogue view: SHOW from slot') {
                $t = Get-LineMs $s
                if ($t -ge 0 -and ($t - $lastDown) -ge 0 -and ($t - $lastDown) -le 5000) { $hazardA++ }
            }
        }
    }
    Verdict 'interfere' $H ($hazardA -eq 0) 'no dialogue spectator was engaged within 5 s of a death' ("$hazardA occurrence(s)")

    # (b) no save line inside a down window.
    $hazardB = 0
    foreach ($i in @($H, $P)) {
        $lines = Get-RunLines $i
        $downAt = $null; $upAt = $null
        foreach ($ln in $lines) {
            $s = [string]$ln; $t = Get-LineMs $s
            if ($s -match 'respawn: DEATH #') { $downAt = $t; $upAt = $null }
            if ($s -match 'respawn: respawned at ') { $upAt = $t }
            if ($null -ne $downAt -and $null -eq $upAt -and $t -ge 0 -and $s -match 'save: the partner saved|save: wrote this machine') { $hazardB++ }
        }
    }
    Verdict 'interfere' $H ($hazardB -eq 0) 'no save was mirrored while a player was dead' ("$hazardB occurrence(s)")

    # (c) a mover row must not fight a ride row: the ride must not be published for a
    #     platform whose owner rows disagree with it in the same second.
    $rideCount = @($allH | Where-Object { $_ -match 'platforms: ride local place' }).Count +
                 @($allP | Where-Object { $_ -match 'platforms: ride local place' }).Count
    $moverCount = @($allH | Where-Object { $_ -match 'movers: applied \d+ owner row' }).Count +
                  @($allP | Where-Object { $_ -match 'movers: applied \d+ owner row' }).Count
    Note ("  lift/ride lines: ride={0} mover-rows={1}" -f $rideCount, $moverCount)

    # (d) the switches still agree on both machines at the end.
    foreach ($i in @($H, $P)) {
        $w = Get-WorldState $i
        Note ("  [{0}] world: local='{1}' partner='{2}' agree={3}" -f $i, $w.World, $w.Partner, $w.Agree)
    }
    $divH = @($allH | Where-Object { $_ -match 'WORLD DIVERGENCE' }).Count
    $divP = @($allP | Where-Object { $_ -match 'WORLD DIVERGENCE' }).Count
    Note ("  world-divergence lines over the whole run: host={0} peer={1}" -f $divH, $divP)
}

function Write-Summary {
    Note ''; Note '================ SUMMARY ================'
    $pass = @($script:verdicts | Where-Object { $_.Ok }).Count
    $fail = @($script:verdicts | Where-Object { -not $_.Ok }).Count
    $byStep = @{}
    foreach ($v in $script:verdicts) {
        if (-not $byStep.ContainsKey($v.Step)) { $byStep[$v.Step] = [pscustomobject]@{ Pass = 0; Fail = 0 } }
        if ($v.Ok) { $byStep[$v.Step].Pass++ } else { $byStep[$v.Step].Fail++ }
    }
    foreach ($v in $script:verdicts) {
        $ln = [string]$v.Line
        if ($ln.Length -gt 220) { $ln = $ln.Substring(0, 220) + ' ...' }
        Note ("  {0} {1,-10} [{2}] {3}{4}" -f $(if ($v.Ok) { 'PASS' } else { 'FAIL' }), $v.Step, $v.Inst, $v.What, $(if ($ln) { " -- $ln" } else { '' }))
    }
    Note ''
    Note '  -- per step --'
    foreach ($k in ($byStep.Keys | Sort-Object)) {
        Note ("    {0,-12} {1} pass / {2} fail" -f $k, $byStep[$k].Pass, $byStep[$k].Fail)
    }
    if ($script:refused.Count) {
        Note ''
        Note '  -- REFUSED (precondition missing: neither a pass nor a lane failure) --'
        foreach ($r in $script:refused) { Note "    $r" }
    }
    Note ''
    Note ("  {0} PASS / {1} FAIL / {2} REFUSED" -f $pass, $fail, $script:refused.Count)
    $script:lines | Set-Content -LiteralPath $Report
    Say "report: $Report"
}

# ===========================================================================
# THE LIVE LOCK IS NOT OPTIONAL, AND SKIPPING IT CORRUPTS A MEASUREMENT.
# ===========================================================================
# WHY THIS CHECK EXISTS (measured, final soak run 9): a second delegate's session was live at
# the same time, on the same machine, with the same `-saveddirsuffix qd1` -- so BOTH sessions
# wrote `qdcoop-qd1.log`.  This script's reader anchors on the last `hooked FEngineLoop`, which
# is now the OTHER session's boot, and every verdict afterwards is about a run this process
# did not start: the weapon step reported the other session's control path
# (`equip levers enabled equip='...\gaprun\inst1\qd.equip'`), the object lane saw 0 rows, and
# both machines "crashed" because the other harness stopped them.  Forty minutes of
# measurements were unattributable, and nothing in the report said why.
#
# So the lock is checked BEFORE anything is launched, the owning session is named, and the
# run refuses rather than producing numbers about someone else's world.  A harness that can
# silently measure a different process is worse than one that fails.
function Assert-LiveLock {
    $lockDir = 'G:\Freespace\Queens_Domain\_test\locks\live'
    $me = 'final-soak'
    if (-not (Test-Path (Split-Path -Parent $lockDir))) {
        New-Item -ItemType Directory -Path (Split-Path -Parent $lockDir) -Force | Out-Null
    }
    if (Test-Path $lockDir) {
        $owner = ''
        $ownerFile = Join-Path $lockDir 'owner.txt'
        if (Test-Path $ownerFile) { $owner = (Get-Content -LiteralPath $ownerFile -EA SilentlyContinue | Select-Object -First 1) }
        if ($owner -and $owner.Split(' ')[0] -ne $me) {
            $ageMin = [math]::Round(((Get-Date) - (Get-Item -LiteralPath $ownerFile).LastWriteTime).TotalMinutes, 1)
            Say ("REFUSED: the LIVE lock is held by '{0}' ({1} min old).  Another session is" -f $owner, $ageMin)
            Say '          using the same game directory and the same qdcoop-qd1.log, so this'
            Say '          run would measure THAT session.  Re-run when the lock is free.'
            return $false
        }
    } else {
        New-Item -ItemType Directory -Path $lockDir -Force | Out-Null
        Set-Content -LiteralPath (Join-Path $lockDir 'owner.txt') -Value ("{0} {1}" -f $me, (Get-Date -Format o)) -NoNewline
        Say "acquired the LIVE lock as '$me'"
    }
    # A SESSION ALREADY WRITING THE LOG IS THE SAME HAZARD, lock or no lock: a previous
    # crashed harness can leave a game process behind, and its log is indistinguishable from
    # a fresh one's.
    $stale = @(Get-Process -Name 'QueensDomain-Win64-Shipping', 'qd-relay' -ErrorAction SilentlyContinue)
    if ($stale.Count) {
        Say ("REFUSED: {0} game/relay process(es) are already running ({1}); their logs and" -f $stale.Count,
             (($stale | ForEach-Object { "$($_.ProcessName)#$($_.Id)" }) -join ' '))
        Say '          their worlds would be measured as this run.  Stop them first.'
        return $false
    }
    return $true
}

# ===========================================================================
try {
    Say ("session-finalsoak start; rig=$rigRoot; soak>={0}s" -f $SoakSeconds)
    if (-not (Assert-LiveLock)) { exit 4 }
    # A HARNESS SELF-CHECK, because this class of bug costs a whole live window.  MEASURED
    # (runs 3-7): `Verdict 'step' $H 'a sentence' ($cond)` and its parenthesised twin
    # `Verdict 'step' $H ("a " + $s) ($cond)` both bind the SENTENCE to the [bool]$ok
    # parameter and kill the run with "Cannot convert value System.String to type
    # System.Boolean" -- after the expensive boot and half the features had run.
    #
    # THE `\$` IS LOAD-BEARING.  The first version of this check tested for `$` in a
    # double-quoted regex, where `$` is the END-OF-STRING ANCHOR, not a literal dollar: the
    # pattern could never match and the check silently passed while the bug it exists for
    # killed run 5.  Escaped, `\$` is a literal and the check actually sees a variable.
    $shapes = @(
        'Verdict\s+''[^'']+''\s+\$\w+\s+''',
        'Verdict\s+''[^'']+''\s+\$\w+\s+\("\x22'
    )
    $badCalls = @()
    foreach ($re in $shapes) {
        $badCalls += @(Select-String -LiteralPath $PSCommandPath -Pattern $re |
                       Where-Object { $_.Line -notmatch '^\s*#' })
    }
    if ($badCalls.Count) {
        Say ("HARNESS DEFECT: {0} Verdict call(s) pass a string where [bool] ok is expected:" -f $badCalls.Count)
        foreach ($b in $badCalls) { Say ("  line {0}: {1}" -f $b.LineNumber, $b.Line.Trim()) }
        exit 3
    }
    # … AND NO LOOP VARIABLE MAY SHADOW THIS SCRIPT'S OWN `$inst` TABLE.  MEASURED (run 8):
    # `foreach ($inst in @($P, $H))` shadowed the per-machine hashtable the log readers index,
    # so the very next `$inst[$i].Root` was $null and Set-Control threw "Cannot bind argument
    # to parameter 'Path' because it is an empty string" one step later, far from the cause.
    $shadow = @(Select-String -LiteralPath $PSCommandPath -Pattern 'foreach\s*\(\s*\$inst\s+in\b' |
                Where-Object { $_.Line -notmatch '^\s*#' })
    if ($shadow.Count) {
        Say ("HARNESS DEFECT: {0} loop variable shadows `$inst:" -f $shadow.Count)
        foreach ($s in $shadow) { Say ("  line {0}: {1}" -f $s.LineNumber, $s.Line.Trim()) }
        exit 3
    }
    Seed-FreshPair
    Start-Boot
    $H = $script:H; $P = $script:P

    Test-Relay
    Test-Door $H $P
    Test-Breakables $H $P
    Test-SecretWall $H $P
    Test-Key $H $P
    # THE ORDER IS NOT COSMETIC.  The XP step walks the player into a boss arena and fights,
    # and the hub's enemies are live, so a player CAN genuinely die there (measured: the host
    # took a ShrineBoss slam in run 3 and spent the rest of the run at 90 % with a dead flag).
    # The death test is therefore driven on the PEER first and the weapons/lift steps run
    # BEFORE it, so a real death in the XP step cannot invalidate the steps that follow.
    Test-Weapons $H $P
    Test-Lift $H $P
    $null = Test-RestSave $H $P
    Test-Dialogue $H $P
    Test-KillXp $H $P
    # The death step is LAST among the driven features: it needs a live player and a rest
    # point, and it deliberately leaves a penalty behind.
    Test-DeathRestore $H $P

    # The soak window is measured LAST but its length is the run's, so the total body covers
    # every lane with all switches on.
    $null = Invoke-SoakWindow $H $P $SoakSeconds 'final'

    Test-End $H $P $SoakSeconds
    Write-Summary
} catch {
    Say ("UNHANDLED: " + $_.Exception.Message)
    Say ("AT: " + $_.InvocationInfo.PositionMessage)
    try { $script:lines | Set-Content -LiteralPath $Report } catch {}
    try { Stop-All ("unhandled exception: " + $_.Exception.Message) } catch {}
    exit 1
} finally {
    if (-not $KeepRunning) { try { Stop-All 'run complete (this is the harness cleanup, not a crash)' } catch {} }
    try { if (-not (Test-Path $Report)) { $script:lines | Set-Content -LiteralPath $Report } } catch {}
}
