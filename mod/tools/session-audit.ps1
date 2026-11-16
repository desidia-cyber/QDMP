# session-audit.ps1 -- the desync auditor's live proof.
#
# WHAT THIS MEASURES
# ------------------
# 1. CLEAN STATE.  Two instances, the SHIPPED qdcoop.ini (all lanes on), no lever active.
#    After the session settles, each machine must log `audit: OK 12 categories, N items`
#    once a minute.  That line is the product's claim: the two games agree about the whole
#    synced world state of the current map.
# 2. ONE DELIBERATE DESYNC.  Exactly one flag value is made to exist on ONE machine's
#    auditor view (`qd.auditfork`, a TEST lever that writes no game state -- see
#    qdaudit.h's note: every real lane repairs itself, so a non-healing divergence has to be
#    injected), and the auditor must report EXACTLY that item by name and both values.
# 3. HEALING.  Clearing the fork must produce `audit: flags healed` and then go back to
#    `audit: OK`.
#
# HONESTY RULES (inherited from every session since 18)
# ----------------------------------------------------
# * A verdict prints the line it read.  No line, no pass.
# * A missing precondition says REFUSED, never a pass.
# * Only lines from THIS run are read (after the last `hooked FEngineLoop`).
[CmdletBinding()]
param(
    [int]$BootSeconds = 300,
    [int]$CleanSeconds = 100,
    [int]$ForkSeconds = 60,
    [string]$Report = ''
)

$ErrorActionPreference = 'Stop'
$here    = $PSScriptRoot
$repo    = Split-Path -Parent (Split-Path -Parent $here)
$gameDir = 'G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64'
$relay   = Join-Path (Split-Path -Parent $here) 'rust\target\release\qd-relay.exe'
$rigRoot = 'G:\Freespace\Queens_Domain\_test\auditrun'
$seed    = Join-Path $repo 'analysis\save-backup-before-live'
$srcIni  = Join-Path $repo 'mod\qdcoop.ini'
$outDir  = Join-Path $rigRoot 'out'
if (-not $Report) { $Report = Join-Path $outDir 'report-audit.txt' }
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

# ---------------------------------------------------------------------------
# install + seed + boot
# ---------------------------------------------------------------------------
$built = Join-Path $repo 'mod\native\build\winmm.dll'
if (-not (Test-Path -LiteralPath $built)) { Say "REFUSED: built proxy not found at $built"; exit 2 }
$h = (Get-FileHash -LiteralPath $built -Algorithm SHA256).Hash
Say ("installing proxy sha256={0}... built {1}" -f $h.Substring(0, 16), (Get-Item $built).LastWriteTime)
$dst = Join-Path $gameDir 'winmm.dll'
try { Copy-Item -LiteralPath $built -Destination $dst -Force }
catch {
    # The loader maps the proxy, so a previous run's copy can still be image-mapped.  Move it
    # aside rather than fail: a run that cannot install its own build measures nothing.
    $held = $dst + '.held-' + (Get-Date -Format 'HHmmss')
    Rename-Item -LiteralPath $dst -NewName (Split-Path -Leaf $held) -Force
    Copy-Item -LiteralPath $built -Destination $dst -Force
    Say "destination was image-mapped; moved it to $held and installed the new proxy"
}

$text = Get-Content -LiteralPath $srcIni -Raw
$text = $text -replace '(?m)^RelayHost=.*$', 'RelayHost=127.0.0.1'
$text = $text -replace '(?m)^RelayPort=.*$', 'RelayPort=47800'
$text = $text -replace '(?m)^LogLevel=.*$', 'LogLevel=3'
$ini = Join-Path $gameDir 'qdcoop.ini'
$text | Set-Content -LiteralPath $ini -NoNewline
$sw = @(Get-Content -LiteralPath $ini | Where-Object { $_ -match '=' -and $_ -notmatch '^;' })
Say ("wrote {0} from the SHIPPED qdcoop.ini verbatim ({1} switches; relay loopback, LogLevel=3)" -f $ini, $sw.Count)
Say ("  DesyncAudit in that file: " + ((@(Get-Content -LiteralPath $ini | Where-Object { $_ -match '^DesyncAudit=' })) -join ','))
Copy-Item -LiteralPath $ini -Destination (Join-Path $outDir 'qdcoop-used.ini') -Force

foreach ($i in 1, 2) {
    $null = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'make_test_instance.ps1') `
            -Name ("inst{0}" -f $i) -Index $i -SourceSave $seed -Root $rigRoot -Suffix $inst[$i].Suffix -Force 2>&1
    if ($LASTEXITCODE -ne 0) { Say "REFUSED: make_test_instance refused instance $i"; exit 2 }
}

Stop-All 'boot: replacing any process left over from an earlier run'
foreach ($i in 1, 2) {
    foreach ($leaf in 'qd.tp','qd.act','qd.hit','qd.cheat','qd.cheatarg','qd.travel','qd.hard','qd.level',
                      'qd.list','qd.near','qd.drops','qd.menu','qd.efunc','qd.eattack','qd.use','qd.pdmg',
                      'qd.kill','qd.petrace','qd.enemies','qd.sequence','qd.enemykill','qd.hitguid',
                      'qd.obj','qd.flag','qd.gate','qd.take','qd.trigger','qd.selfhp','qd.nohit','qd.dlg',
                      'qd.attack','qd.equip','qd.mover','qd.wsys','qd.proj','qd.atkcall','qd.atkinfo',
                      'qd.eprops','qd.auditfork') {
        Set-Control $i $leaf $null
    }
    # `qd.audit` arms the lever (and forces one early pass); it is NOT needed for the passive
    # 10 s lane, which is what the clean-state verdict measures.
    Set-Control $i 'qd.audit' 'boot'
    Set-Control $i 'qd.scan' '1'
    if (Test-Path -LiteralPath $inst[$i].Pid) { Remove-Item -LiteralPath $inst[$i].Pid -ErrorAction SilentlyContinue }
    $old = $inst[$i].Log
    if (Test-Path -LiteralPath $old) { Move-Item -LiteralPath $old -Destination ($old + '.prev') -Force }
    $sc = Join-Path $gameDir ("qdcoop-{0}-respawn.ini" -f $inst[$i].Suffix)
    if (Test-Path -LiteralPath $sc) { Remove-Item -LiteralPath $sc -Force -ErrorAction SilentlyContinue }
}
$env:QDCOOP_AUTOLOAD = '0'

if (-not (Test-Path -LiteralPath $relay)) { Say "REFUSED: relay not built at $relay"; exit 2 }
$relayLog = Join-Path $outDir 'relay-audit.log'
Remove-Item -LiteralPath $relayLog -ErrorAction SilentlyContinue
$script:relayProc = Start-Process -FilePath $relay -ArgumentList @('--bind', '127.0.0.1:47800') `
        -RedirectStandardOutput $relayLog -RedirectStandardError (Join-Path $outDir 'relay-audit.err') `
        -WindowStyle Hidden -PassThru
Say ("relay pid={0} -> {1}" -f $script:relayProc.Id, $relayLog)
Start-Sleep -Seconds 2

$script:pids = @{}
foreach ($i in 1, 2) {
    $ueLog = Join-Path $outDir ("inst{0}-ue.log" -f $i)
    Remove-Item -LiteralPath $ueLog -ErrorAction SilentlyContinue
    $launch = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'run-hidden.ps1') `
                -UserDir $inst[$i].Root -Suffix $inst[$i].Suffix -AbsLog $ueLog 2>&1
    $gp = ($launch | Where-Object { $_ -match '^\d+$' } | Select-Object -Last 1)
    if (-not $gp) { Say "LAUNCH FAILED for instance $i"; $launch | ForEach-Object { Note $_ } }
    else { $script:pids[$i] = [int]$gp; Set-Content -LiteralPath $inst[$i].Pid -Value $gp; Say "inst$i pid=$gp" }
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

# The auditor needs the players IN the world and the map known before its digests mean
# anything, so wait for the local pawn and then a full summary period.
Say 'waiting for both players in the world'
foreach ($i in 1, 2) {
    $null = Wait-Match $i 'INFO local pawn:' 120
}

# ---------------------------------------------------------------------------
# 1. clean state
# ---------------------------------------------------------------------------
Say ("measuring a CLEAN state for {0} s (the summary is once a minute)" -f $CleanSeconds)
Start-Sleep -Seconds $CleanSeconds
$okLines = @{}
foreach ($i in 1, 2) {
    $m = @(Get-Matches $i 'audit: OK \d+ categories, \d+ items')
    $okLines[$i] = if ($m.Count -gt 0) { ([string]$m[-1]).Trim() } else { '' }
    if ($okLines[$i]) { Note ("  [{0}] {1}" -f $i, $okLines[$i]) } else { Note ("  [{0}] (no `audit: OK` yet)" -f $i) }
}
$openNow = @{}
$desyncAny = @{}
foreach ($i in 1, 2) {
    $openNow[$i] = @(Get-Matches $i 'audit: DESYNC').Count
    $desyncAny[$i] = $openNow[$i]
}
Verdict 'clean state: the HOST logs audit: OK' ($okLines[$hostIdx] -ne '') $okLines[$hostIdx]
Verdict 'clean state: the PEER logs audit: OK' ($okLines[$peerIdx] -ne '') $okLines[$peerIdx]
Verdict 'clean state: no audit: DESYNC line on either machine' `
        (($desyncAny[1] + $desyncAny[2]) -eq 0) `
        ("{0} DESYNC line(s) total" -f ($desyncAny[1] + $desyncAny[2]))

# ---------------------------------------------------------------------------
# 2. one deliberate desync, on the PEER only
# ---------------------------------------------------------------------------
# A name the game cannot have created, so the fork is exactly one item and the two values
# are unambiguous.  Written to ONE machine's auditor overlay; no game state is touched.
$forkName = 'QDAuditProof'
Say ("injecting ONE item on inst{0} only: flags {1}=1 (qd.auditfork; no lane written)" -f $peerIdx, $forkName)
Set-Control $peerIdx 'qd.auditfork' ("{0}=1" -f $forkName)
$deskLine = ''
$deadline = (Get-Date).AddSeconds($ForkSeconds)
while ((Get-Date) -lt $deadline) {
    $h1 = @(Get-Matches $hostIdx 'audit: DESYNC flags')
    $h2 = @(Get-Matches $peerIdx 'audit: DESYNC flags')
    if ($h1.Count -gt 0 -or $h2.Count -gt 0) {
        $deskLine = ([string]($h1 + $h2)[-1]).Trim()
        break
    }
    Start-Sleep -Seconds 2
}
$reported = @{}
foreach ($i in 1, 2) {
    $m = @(Get-Matches $i ("audit: DESYNC flags {0}" -f $forkName))
    $reported[$i] = if ($m.Count -gt 0) { ([string]$m[-1]).Trim() } else { '' }
    if ($reported[$i]) { Note ("  [{0}] {1}" -f $i, $reported[$i]) }
}
Verdict 'the desync is reported, naming the item and both values' `
        (($reported[1] -ne '') -or ($reported[2] -ne '')) `
        (($reported[1] + ' | ' + $reported[2]).Trim(' |'))
$exact = (($reported[1] -match 'QDAuditProof host=-?\d+ peer=-?\d+') -or
          ($reported[2] -match 'QDAuditProof host=-?\d+ peer=-?\d+'))
Verdict 'the report carries host= and peer= for that item' $exact $deskLine
# And ONLY that item: the category must not also report the rest of the flag bank.
$bulk = 0
foreach ($i in 1, 2) {
    $bulk += @(Get-Matches $i 'audit: DESYNC flags ' | Where-Object { $_ -notmatch [regex]::Escape($forkName) }).Count
}
Verdict 'exactly ONE item differs (no other flags: row)' ($bulk -eq 0) ("{0} other flags row(s)" -f $bulk)

# ---------------------------------------------------------------------------
# 3. it heals when the fork is removed
# ---------------------------------------------------------------------------
Say 'clearing the fork; the auditor must report the category healed'
Set-Control $peerIdx 'qd.auditfork' 'clear'
$healed = ''
$deadline = (Get-Date).AddSeconds(45)
while ((Get-Date) -lt $deadline) {
    $m = @(Get-Matches $peerIdx 'audit: flags healed') + @(Get-Matches $hostIdx 'audit: flags healed')
    if ($m.Count -gt 0) { $healed = ([string]$m[-1]).Trim(); break }
    Start-Sleep -Seconds 2
}
Verdict 'the mismatch heals once the item is gone' ($healed -ne '') $healed

# Faults are counted separately from a lane verdict: a refused diagnostic is not a crash.
$faults = 0
foreach ($i in 1, 2) { $faults += @(Get-Matches $i '\bFAULT\b').Count }
Verdict 'no FAULT in either run' ($faults -eq 0) ("{0} FAULT line(s)" -f $faults)

# The counter the overhead claim is measured from.
foreach ($i in 1, 2) {
    $m = @(Get-Matches $i 'audit: OK ')
    Note ("  [{0}] audit summary lines this run: {1}" -f $i, $m.Count)
}

Stop-All 'end of the audit proof'

Note ''
Note '==================== VERDICTS ===================='
foreach ($v in $script:verdicts) {
    Note ("{0}  {1}" -f ($(if ($v.Ok) { 'PASS' } else { 'FAIL' }), $v.What))
}
$bad = @($script:verdicts | Where-Object { -not $_.Ok }).Count
Note ("{0} of {1} checks passed" -f ($script:verdicts.Count - $bad), $script:verdicts.Count)
$script:lines | Set-Content -LiteralPath $Report
Note ("report: {0}" -f $Report)
exit ($(if ($bad -eq 0) { 0 } else { 1 }))
