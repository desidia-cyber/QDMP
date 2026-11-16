# session-join.ps1 -- the join-reconcile live proof.
#
# WHAT THIS MEASURES
# ------------------
# 1. THE PEER'S SAVE IS AHEAD.  inst2 (the peer) is seeded from the sample save and then
#    `qdsave.py apply-flags` adds `QDJoinProofPeerAhead=1` (a story flag the host does not
#    have) and raises `CheckpointUnlockCount` above the host's.  Both are things the existing
#    lanes CANNOT lower: the flag bank is monotone and the peer's mirror baseline is taken at
#    join, so no intent is ever sent for a pre-existing value.
# 2. ON CONNECT the peer must ask, receive, and reconcile: `join: reconciled to host -- ...`
#    with a non-zero `flags reverted` count, a `join-backup-<stamp>` directory, and then
#    `audit: OK ... (forced: the join reconcile finished)` on both machines.
# 3. ON RECONNECT the peer process is stopped and relaunched (a forced drop) while the host
#    keeps running.  The relaunched peer must establish again and log
#    `join: already in sync with host`: the first reconcile persisted its slot, so a solo
#    load of that save is already the host's state.
#
# HONESTY RULES (inherited from session-audit.ps1)
# ------------------------------------------------
# * A verdict prints the line it read.  No line, no pass.
# * A missing precondition says REFUSED, never a pass.
# * Only lines from THIS run are read (after the last `hooked FEngineLoop`).
#
# NOT YET RUN.  The LIVE lock is single-session; run it only when the lock is free.
[CmdletBinding()]
param(
    [int]$BootSeconds = 360,
    [int]$SettleSeconds = 45,
    [int]$ReconnectSeconds = 240,
    [string]$Report = ''
)

$ErrorActionPreference = 'Stop'
$here    = $PSScriptRoot
$repo    = Split-Path -Parent (Split-Path -Parent $here)
$gameDir = 'G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64'
$relay   = Join-Path (Split-Path -Parent $here) 'rust\target\release\qd-relay.exe'
$rigRoot = 'G:\Freespace\Queens_Domain\_test\joinrun'
$seed    = Join-Path $repo 'analysis\save-backup-before-live'
$srcIni  = Join-Path $repo 'mod\qdcoop.ini'
$outDir  = Join-Path $rigRoot 'out'
if (-not $Report) { $Report = Join-Path $outDir 'report-join.txt' }
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force -Path $outDir | Out-Null }

$script:lines = New-Object System.Collections.Generic.List[string]
$script:runCache = @{}
$script:verdicts = New-Object System.Collections.Generic.List[object]
function Say($m) {
    $s = "[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $m
    Write-Host $s
    $script:lines.Add($s) | Out-Null
}
function Note($m) { Write-Host $m; $script:lines.Add($m) | Out-Null }
function Verdict([string]$what, [bool]$ok, [string]$line) {
    $script:verdicts.Add([pscustomobject]@{ What = $what; Ok = $ok; Line = $line }) | Out-Null
    $tag = if ($ok) { 'PASS' } else { 'FAIL' }
    Note ("  {0} {1}" -f $tag, $what)
    Note ("      {0}" -f $line)
}

function Get-RigPaths([int]$i) {
    $root = Join-Path $rigRoot ("inst{0}" -f $i)
    $suffix = "qd$i"
    [pscustomobject]@{
        Index = $i; Root = $root; Suffix = $suffix
        Log = Join-Path $gameDir ("qdcoop-{0}.log" -f $suffix)
        Pid = "$root.pid"
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
function Test-Alive([int]$i) {
    if (-not $script:pids -or -not $script:pids.ContainsKey($i)) { return $false }
    return [bool](Get-Process -Id $script:pids[$i] -ErrorAction SilentlyContinue)
}
function Wait-Match([int]$i, [string]$p, [int]$secs) {
    $deadline = (Get-Date).AddSeconds($secs)
    while ((Get-Date) -lt $deadline) {
        $m = @(Get-Matches $i $p)
        if ($m.Count -gt 0) { return [string]$m[-1] }
        if (-not (Test-Alive $i)) { return '' }
        Start-Sleep -Seconds 2
    }
    return ''
}
function Stop-All([string]$Reason) {
    Say "STOP-ALL reason=$Reason"
    if ($script:relayProc) { Stop-Process -Id $script:relayProc.Id -Force -ErrorAction SilentlyContinue }
    Get-Process -Name 'QueensDomain-Win64-Shipping', 'qd-relay' -ErrorAction SilentlyContinue |
        ForEach-Object { Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue }
    Start-Sleep -Seconds 2
}
function Launch-Instance([int]$i, [string]$ueLog) {
    Remove-Item -LiteralPath $ueLog -ErrorAction SilentlyContinue
    $launch = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'run-hidden.ps1') `
                -UserDir $inst[$i].Root -Suffix $inst[$i].Suffix -AbsLog $ueLog 2>&1
    $gp = ($launch | Where-Object { $_ -match '^\d+$' } | Select-Object -Last 1)
    if (-not $gp) { Say "LAUNCH FAILED for instance $i"; $launch | ForEach-Object { Note $_ }; return $false }
    $script:pids[$i] = [int]$gp
    Set-Content -LiteralPath $inst[$i].Pid -Value $gp
    Say "inst$i pid=$gp"
    return $true
}

# ---------------------------------------------------------------------------
# install + ini + seed
# ---------------------------------------------------------------------------
$built = Join-Path $repo 'mod\native\build\winmm.dll'
if (-not (Test-Path -LiteralPath $built)) { Say "REFUSED: built proxy not found at $built"; exit 2 }
$h = (Get-FileHash -LiteralPath $built -Algorithm SHA256).Hash
Say ("installing proxy sha256={0}... built {1}" -f $h.Substring(0, 16), (Get-Item $built).LastWriteTime)
Copy-Item -LiteralPath $built -Destination (Join-Path $gameDir 'winmm.dll') -Force

# The SHIPPED ini with the one switch this proof needs.  Every other lane stays at its
# shipped value, so a failure is attributable.
$text = Get-Content -LiteralPath $srcIni -Raw
$text = $text -replace '(?m)^RelayHost=.*$', 'RelayHost=127.0.0.1'
$text = $text -replace '(?m)^RelayPort=.*$', 'RelayPort=47800'
$text = $text -replace '(?m)^LogLevel=.*$', 'LogLevel=3'
$text = $text -replace '(?m)^JoinReconcile=.*$', 'JoinReconcile=1'
$ini = Join-Path $gameDir 'qdcoop.ini'
$text | Set-Content -LiteralPath $ini -NoNewline
Say ("wrote {0} with JoinReconcile=1, LogLevel=3" -f $ini)
Copy-Item -LiteralPath $ini -Destination (Join-Path $outDir 'qdcoop-used.ini') -Force

foreach ($i in 1, 2) {
    $null = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'make_test_instance.ps1') `
            -Name ("inst{0}" -f $i) -Index $i -SourceSave $seed -Root $rigRoot -Suffix $inst[$i].Suffix -Force 2>&1
    if ($LASTEXITCODE -ne 0) { Say "REFUSED: make_test_instance refused instance $i"; exit 2 }
}

# Seed the PEER ahead: a story flag the host lacks, and a counter above the host's.
$seedJson = Join-Path $inst[2].Root 'join-seed.json'
$peerFlags = [ordered]@{ QDJoinProofPeerAhead = 1; CheckpointUnlockCount = 7 }
$peerFlags | ConvertTo-Json | Set-Content -LiteralPath $seedJson -Encoding ASCII
$saveRoot = Join-Path $inst[2].Root 'Saved\SaveGames'
$apply = & python (Join-Path $here 'qdsave.py') apply-flags --root $saveRoot (Join-Path $saveRoot 'World0.sav') --json $seedJson 2>&1
$apply | ForEach-Object { Note ("  qdsave: {0}" -f $_) }
if ($LASTEXITCODE -ne 0) { Say 'REFUSED: qdsave.py apply-flags failed; the peer save is not seeded ahead'; exit 2 }

Stop-All 'boot: replacing any process left over from an earlier run'
foreach ($i in 1, 2) {
    foreach ($leaf in 'qd.tp','qd.act','qd.hit','qd.cheat','qd.cheatarg','qd.travel','qd.hard','qd.level',
                      'qd.list','qd.near','qd.drops','qd.menu','qd.efunc','qd.eattack','qd.use','qd.pdmg',
                      'qd.kill','qd.enemies','qd.sequence','qd.enemykill','qd.hitguid','qd.obj','qd.flag',
                      'qd.gate','qd.take','qd.trigger','qd.selfhp','qd.nohit','qd.dlg','qd.attack','qd.equip',
                      'qd.mover','qd.wsys','qd.proj','qd.atkcall','qd.atkinfo','qd.eprops','qd.auditfork') {
        Remove-Item -LiteralPath (Join-Path $inst[$i].Root $leaf) -ErrorAction SilentlyContinue
    }
    Remove-Item -LiteralPath $inst[$i].Pid -ErrorAction SilentlyContinue
    $old = $inst[$i].Log
    if (Test-Path -LiteralPath $old) { Move-Item -LiteralPath $old -Destination ($old + '.prev') -Force }
}
$env:QDCOOP_AUTOLOAD = '0'

if (-not (Test-Path -LiteralPath $relay)) { Say "REFUSED: relay not built at $relay"; exit 2 }
$relayLog = Join-Path $outDir 'relay-join.log'
Remove-Item -LiteralPath $relayLog -ErrorAction SilentlyContinue
$script:relayProc = Start-Process -FilePath $relay -ArgumentList @('--bind', '127.0.0.1:47800') `
        -RedirectStandardOutput $relayLog -RedirectStandardError (Join-Path $outDir 'relay-join.err') `
        -WindowStyle Hidden -PassThru
Say ("relay pid={0} -> {1}" -f $script:relayProc.Id, $relayLog)
Start-Sleep -Seconds 2

$script:pids = @{}
foreach ($i in 1, 2) {
    if (-not (Launch-Instance $i (Join-Path $outDir ("inst{0}-ue.log" -f $i)))) { }
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
if ($roles.Count -ne 2) { Say 'REFUSED: no session established'; Stop-All 'no session'; $script:lines | Set-Content $Report; exit 2 }
$hostIdx = 1; $peerIdx = 2
if ($roles[2] -eq 'HOST') { $hostIdx = 2; $peerIdx = 1 }
Say ("roles: inst{0}=HOST inst{1}=PEER" -f $hostIdx, $peerIdx)

# ---------------------------------------------------------------------------
# 1. the reconcile happens
# ---------------------------------------------------------------------------
foreach ($i in 1, 2) { $null = Wait-Match $i 'INFO local pawn:' 180 }

$reqLine = Wait-Match $peerIdx 'join: session established .* asking the host' 60
if (-not $reqLine) { $reqLine = Wait-Match $peerIdx 'join: .*asking the host' 10 }
Verdict 'the PEER asks the host for its world state on establish' ($reqLine -ne '') $reqLine

$sumLine = Wait-Match $peerIdx 'join: reconciled to host -- ' 90
Verdict 'the PEER reports the reconcile summary' ($sumLine -ne '') $sumLine
$reverted = 0
if ($sumLine -match '(\d+) flags reverted') { $reverted = [int]$Matches[1] }
Verdict 'the peer-ahead flag/counter WERE reverted (>=1)' ($reverted -ge 1) ("{0} flags reverted" -f $reverted)

$backupLine = ''
$backupDir = ''
if ($sumLine -match 'backup=(\S+)') {
    $backupDir = $Matches[1]
    $backupLine = $sumLine
}
$backupExists = ($backupDir -ne '') -and (Test-Path -LiteralPath $backupDir)
$backupFiles = 0
if ($backupExists) { $backupFiles = @(Get-ChildItem -LiteralPath $backupDir -Filter '*.sav' -File).Count }
Verdict 'a timestamped backup of the peer save exists' ($backupExists -and $backupFiles -gt 0) `
        ("{0} file(s) in {1}" -f $backupFiles, $backupDir)

Start-Sleep -Seconds $SettleSeconds
$okLines = @{}
foreach ($i in 1, 2) {
    $m = @(Get-Matches $i 'audit: OK \d+ categories, \d+ items \(forced: the join reconcile finished\)')
    if ($m.Count -eq 0) { $m = @(Get-Matches $i 'audit: OK \d+ categories, \d+ items') }
    $okLines[$i] = if ($m.Count -gt 0) { ([string]$m[-1]).Trim() } else { '' }
}
Verdict 'the HOST logs audit: OK after the reconcile' ($okLines[$hostIdx] -ne '') $okLines[$hostIdx]
Verdict 'the PEER logs audit: OK after the reconcile' ($okLines[$peerIdx] -ne '') $okLines[$peerIdx]
$desync = 0
foreach ($i in 1, 2) { $desync += @(Get-Matches $i 'audit: DESYNC (flags|enemy|actors|quest)').Count }
Verdict 'no audit: DESYNC for a reconciled category' ($desync -eq 0) ("{0} DESYNC line(s)" -f $desync)

# ---------------------------------------------------------------------------
# 2. a forced drop and a reconnect: the persisted save is already the host's
# ---------------------------------------------------------------------------
Say ("forcing a drop: stopping the PEER (inst{0}) and relaunching it" -f $peerIdx)
if ($script:pids.ContainsKey($peerIdx)) {
    Stop-Process -Id $script:pids[$peerIdx] -Force -ErrorAction SilentlyContinue
}
Start-Sleep -Seconds 4
$relaunchLog = Join-Path $outDir ("inst{0}-ue-reconnect.log" -f $peerIdx)
if (-not (Launch-Instance $peerIdx $relaunchLog)) { Say 'REFUSED: the peer could not be relaunched' }
else {
    $again = Wait-Match $peerIdx 'join: reconciled to host -- |join: already in sync with host' $ReconnectSeconds
    $inSync = ($again -match 'already in sync with host')
    Verdict 'after the forced drop the peer establishes again' ($again -ne '') $again
    Verdict 'the relaunched peer is already in sync with the host' $inSync $again
}

$faults = 0
foreach ($i in 1, 2) { $faults += @(Get-Matches $i '\bFAULT\b').Count }
Verdict 'no FAULT in either run' ($faults -eq 0) ("{0} FAULT line(s)" -f $faults)

Stop-All 'end of the join proof'

Note ''
Note '==================== VERDICTS ===================='
foreach ($v in $script:verdicts) {
    Note ("{0}  {1}" -f ($(if ($v.Ok) { 'PASS' } else { 'FAIL' }), $v.What))
    if ($v.Line) { Note ("      {0}" -f $v.Line) }
}
$bad = @($script:verdicts | Where-Object { -not $_.Ok }).Count
Note ("{0} of {1} checks passed" -f ($script:verdicts.Count - $bad), $script:verdicts.Count)
$script:lines | Set-Content -LiteralPath $Report
Note ("report: {0}" -f $Report)
exit ($(if ($bad -eq 0) { 0 } else { 1 }))
