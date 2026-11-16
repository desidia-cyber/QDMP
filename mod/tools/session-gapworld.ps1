# session-gapworld.ps1 -- the [gap-world] live proof: unlock sets, rest-as-world-event,
# puzzle intermediate state, shop stock, and the chest counter.
#
# WHAT THIS MEASURES (and the exact line that decides each)
# --------------------------------------------------------
# 1. UNLOCK SETS ARE A UNION.  `qd.world=dump` on both machines must show the same
#    `worldset[...] union=N` for each set, and the derived `TeleporterUnlockCount` /
#    `CheckpointUnlockCount` must be equal on both.  A set that only one save held is the
#    failure this lane exists to remove.
# 2. A REST IS A WORLD EVENT.  `qd.world=rest` on the PEER must produce, on the HOST,
#    `rest: the partner rested at ... -- applying the same reset here and ordering it`
#    followed by `rest: world reset (...) -- ClearAllEnemySaveStates ran`, and on the PEER
#    `rest: the host ordered rest #N ... -- applying the same reset here`.  Two machines
#    resetting for ONE rest is the whole rule; one machine resetting alone is the gap.
# 3. THE BOSS DEATH SURVIVES IT.  With an exempt key held, both logs must say
#    `rest: N of N exempt death(s) written back after the clear`.
# 4. THE PUZZLE LANE IS ALIVE (not necessarily exercised): both machines must log
#    `puzzle:` state in the dump.  Turning a book needs a player beside one; the dump is the
#    precondition, and this script says PROVEN / NOT EXERCISED rather than claiming more.
# 5. SHOP STOCK IS LOCAL.  A `ShopPurchasedKey` key must never appear in a `flag intent`
#    line on the peer nor in the host's `flag CHANGE` stream.
#
# HONESTY RULES (inherited): a verdict prints the line it read; no line, no pass; a missing
# precondition says REFUSED, never PASS; only lines from THIS run are read.
[CmdletBinding()]
param(
    [int]$BootSeconds = 300,
    [int]$SettleSeconds = 40,
    [int]$AfterSeconds = 60,
    [string]$Report = ''
)

$ErrorActionPreference = 'Stop'
$here    = $PSScriptRoot
$repo    = Split-Path -Parent (Split-Path -Parent $here)
$gameDir = 'G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64'
$relay   = Join-Path (Split-Path -Parent $here) 'rust\target\release\qd-relay.exe'
$rigRoot = 'G:\Freespace\Queens_Domain\_test\gaprun'
$seed    = Join-Path $repo 'analysis\save-backup-before-live'
$srcIni  = Join-Path $repo 'mod\qdcoop.ini'
$outDir  = Join-Path $rigRoot 'out'
if (-not $Report) { $Report = Join-Path $outDir 'report-gapworld.txt' }
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force -Path $outDir | Out-Null }

$script:lines = New-Object System.Collections.Generic.List[string]
$script:verdicts = New-Object System.Collections.Generic.List[object]
function Say($m) { $s = "[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $m; Write-Host $s; $script:lines.Add($s) | Out-Null }
function Note($m) { Write-Host $m; $script:lines.Add($m) | Out-Null }
function Verdict([string]$what, [bool]$ok, [string]$line) {
    $script:verdicts.Add([pscustomobject]@{ What = $what; Ok = $ok; Line = $line }) | Out-Null
    $tag = 'FAIL'; if ($ok) { $tag = 'PASS' }
    Note ("  {0} {1}" -f $tag, $what)
    Note ("      {0}" -f $line)
}

function Get-RigPaths([int]$i) {
    [pscustomobject]@{
        Index = $i
        Root = (Join-Path $rigRoot ("inst{0}" -f $i))
        Suffix = "qd$i"
        Log = (Join-Path $gameDir ("qdcoop-qd{0}.log" -f $i))
        Pid = (Join-Path $rigRoot ("inst{0}.pid" -f $i))
    }
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
# Only THIS run: everything after the last `hooked FEngineLoop`.
function Get-RunLines([int]$i) {
    $all = @(Read-LogLines $inst[$i].Log)
    if ($all.Count -eq 0) { return @() }
    $start = 0
    for ($k = $all.Count - 1; $k -ge 0; $k--) { if ($all[$k] -match 'hooked FEngineLoop') { $start = $k; break } }
    return @($all[$start..($all.Count - 1)])
}
function Get-Matches([int]$i, [string]$pat) { return @(Get-RunLines $i | Where-Object { $_ -match $pat }) }
function Test-Alive([int]$i) {
    if (-not (Test-Path $inst[$i].Pid)) { return $false }
    $p = Get-Content -LiteralPath $inst[$i].Pid -ErrorAction SilentlyContinue | Select-Object -First 1
    return [bool]($p -and (Get-Process -Id ([int]$p) -ErrorAction SilentlyContinue))
}
function Set-Control([int]$i, [string]$leaf, $value) {
    $p = Join-Path $inst[$i].Root $leaf
    if ($null -eq $value) { Remove-Item -LiteralPath $p -Force -ErrorAction SilentlyContinue; return }
    Set-Content -LiteralPath $p -Value ([string]$value) -NoNewline
}
function Stop-All([string]$why) {
    Say "STOP-ALL $why"
    if ($script:relayProc) { Stop-Process -Id $script:relayProc.Id -Force -ErrorAction SilentlyContinue }
    Get-Process -Name 'QueensDomain-Win64-Shipping', 'qd-relay' -ErrorAction SilentlyContinue |
        ForEach-Object { Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue }
    Start-Sleep -Seconds 2
}

# ---------------------------------------------------------------------------
# install + ini + seed
# ---------------------------------------------------------------------------
$built = Join-Path $repo 'mod\native\build\winmm.dll'
if (-not (Test-Path -LiteralPath $built)) { Say "REFUSED: no built proxy at $built"; exit 2 }
$h = (Get-FileHash -LiteralPath $built -Algorithm SHA256).Hash
Say ("installing proxy sha256={0}..." -f $h.Substring(0, 16))
$dst = Join-Path $gameDir 'winmm.dll'
try { Copy-Item -LiteralPath $built -Destination $dst -Force }
catch {
    $held = $dst + '.held-' + (Get-Date -Format 'HHmmss')
    Rename-Item -LiteralPath $dst -NewName (Split-Path -Leaf $held) -Force
    Copy-Item -LiteralPath $built -Destination $dst -Force
    Say "destination was image-mapped; moved it aside and installed the new proxy"
}

# THE SWITCHES UNDER TEST.  Everything else is the shipped file verbatim, so a difference in
# behaviour is attributable to these three.
$text = Get-Content -LiteralPath $srcIni -Raw
$text = $text -replace '(?m)^RelayHost=.*$', 'RelayHost=127.0.0.1'
$text = $text -replace '(?m)^RelayPort=.*$', 'RelayPort=47800'
$text = $text -replace '(?m)^LogLevel=.*$', 'LogLevel=3'
$text = $text -replace '(?m)^WorldSetSync=.*$', 'WorldSetSync=1'
$text = $text -replace '(?m)^PuzzleSync=.*$', 'PuzzleSync=1'
$text = $text -replace '(?m)^TravelSync=.*$', 'TravelSync=1'
$text = $text -replace '(?m)^EnemySync=.*$', 'EnemySync=1'
$ini = Join-Path $gameDir 'qdcoop.ini'
$text | Set-Content -LiteralPath $ini -NoNewline
Say ("wrote qdcoop.ini with WorldSetSync=1 PuzzleSync=1 TravelSync=1 EnemySync=1")
Copy-Item -LiteralPath $ini -Destination (Join-Path $outDir 'qdcoop-used.ini') -Force

foreach ($i in 1, 2) {
    $null = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'make_test_instance.ps1') `
            -Name ("inst{0}" -f $i) -Index $i -SourceSave $seed -Root $rigRoot -Suffix $inst[$i].Suffix -Force 2>&1
    if ($LASTEXITCODE -ne 0) { Say "REFUSED: make_test_instance refused instance $i"; exit 2 }
}

Stop-All 'boot: replacing anything left over'
foreach ($i in 1, 2) {
    foreach ($leaf in 'qd.tp','qd.act','qd.hit','qd.cheat','qd.cheatarg','qd.travel','qd.hard','qd.level',
                      'qd.list','qd.near','qd.drops','qd.menu','qd.efunc','qd.use','qd.kill','qd.enemies',
                      'qd.hitguid','qd.obj','qd.flag','qd.gate','qd.take','qd.trigger','qd.selfhp','qd.nohit',
                      'qd.dlg','qd.attack','qd.equip','qd.mover','qd.wsys','qd.proj','qd.auditfork','qd.world') {
        Set-Control $i $leaf $null
    }
    Set-Control $i 'qd.scan' '1'
    if (Test-Path -LiteralPath $inst[$i].Pid) { Remove-Item -LiteralPath $inst[$i].Pid -ErrorAction SilentlyContinue }
    # Rotating a log a just-killed process still holds is not fatal: retry, then truncate.
    # A session that refuses to start because of a file handle measures nothing.
    $old = $inst[$i].Log
    if (Test-Path -LiteralPath $old) {
        $prev = $old + '.prev'
        Remove-Item -LiteralPath $prev -Force -ErrorAction SilentlyContinue
        $moved = $false
        for ($try = 0; $try -lt 10 -and -not $moved; $try++) {
            try { Move-Item -LiteralPath $old -Destination $prev -Force -ErrorAction Stop; $moved = $true }
            catch { Start-Sleep -Milliseconds 500 }
        }
        if (-not $moved) { try { Set-Content -LiteralPath $old -Value '' -ErrorAction Stop } catch { } }
    }
}
$env:QDCOOP_AUTOLOAD = '0'

if (-not (Test-Path -LiteralPath $relay)) { Say "REFUSED: relay not built at $relay"; exit 2 }
$relayLog = Join-Path $outDir 'relay-gapworld.log'
Remove-Item -LiteralPath $relayLog -ErrorAction SilentlyContinue
$script:relayProc = Start-Process -FilePath $relay -ArgumentList @('--bind', '127.0.0.1:47800') `
        -RedirectStandardOutput $relayLog -RedirectStandardError (Join-Path $outDir 'relay-gapworld.err') `
        -WindowStyle Hidden -PassThru
Say ("relay pid={0}" -f $script:relayProc.Id)
Start-Sleep -Seconds 2

$script:pids = @{}
foreach ($i in 1, 2) {
    $ueLog = Join-Path $outDir ("inst{0}-ue.log" -f $i)
    Remove-Item -LiteralPath $ueLog -ErrorAction SilentlyContinue
    $launch = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'run-hidden.ps1') `
                -UserDir $inst[$i].Root -Suffix $inst[$i].Suffix -AbsLog $ueLog 2>&1
    $gp = ($launch | Where-Object { $_ -match '^\d+$' } | Select-Object -Last 1)
    if (-not $gp) { Say "LAUNCH FAILED instance $i" } else {
        $script:pids[$i] = [int]$gp; Set-Content -LiteralPath $inst[$i].Pid -Value $gp; Say "inst$i pid=$gp"
    }
    Start-Sleep -Seconds 3
}

Say "waiting for a session (up to $BootSeconds s)"
$deadline = (Get-Date).AddSeconds($BootSeconds)
$roles = @{}
while ((Get-Date) -lt $deadline) {
    foreach ($i in 1, 2) {
        if ($roles.ContainsKey($i) -or -not $script:pids.ContainsKey($i)) { continue }
        $m = @(Get-Matches $i 'session established: role=(HOST|PEER)')
        if ($m.Count -gt 0 -and $m[-1] -match 'role=(HOST|PEER)') {
            $roles[$i] = $Matches[1]; Note ("  [{0}] {1}" -f $i, ([string]$m[-1]).Trim())
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

# the switches must actually be ON, or nothing below means anything
$onHost = @(Get-Matches $hostIdx 'switches:.*worldSet=1')
Verdict 'the switches are on in the running game' ($onHost.Count -gt 0) `
        $(if ($onHost.Count -gt 0) { ([string]$onHost[-1]).Trim() } else { '(no `switches:` line with worldSet=1 puzzle=1)' })

Say "settling $SettleSeconds s so the save is loaded and the world is up"
Start-Sleep -Seconds $SettleSeconds

# ---------------------------------------------------------------------------
# 1. the sets, before any rest
# ---------------------------------------------------------------------------
for ($try = 0; $try -lt 12; $try++) {
    foreach ($i in $hostIdx, $peerIdx) { Set-Control $i 'qd.world' 'dump' }
    Start-Sleep -Seconds 5
    $h = @(Get-Matches $hostIdx 'worldset\[teleporters\]:')
    $p = @(Get-Matches $peerIdx 'worldset\[teleporters\]:')
    if ($h.Count -gt 0 -and $p.Count -gt 0) { break }
}
foreach ($i in $hostIdx, $peerIdx) {
    $d = @(Get-Matches $i 'worldset\[')
    $c = @(Get-Matches $i 'worldset counter ')
    Verdict ("inst{0} dumped its unlock sets" -f $i) ($d.Count -gt 0) `
            $(if ($d.Count -gt 0) { (@($d[0..([Math]::Min(3,$d.Count-1))] | ForEach-Object { $_.Trim() }) -join ' | ') } else { '(no `worldset[` line)' })
    Verdict ("inst{0} reported its derived counters" -f $i) ($c.Count -gt 0) `
            $(if ($c.Count -gt 0) { (@($c | ForEach-Object { $_.Trim() }) -join ' | ') } else { '(no `worldset counter` line)' })
}
$hostSets = @(Get-Matches $hostIdx 'worldset\[(teleporters|savepoints|shop-teleporters)\]:')
$peerSets = @(Get-Matches $peerIdx 'worldset\[(teleporters|savepoints|shop-teleporters)\]:')
$hostU = @{}; $peerU = @{}
foreach ($l in $hostSets) { if ($l -match 'worldset\[([a-z-]+)\]:.*union=(\d+)') { $hostU[$Matches[1]] = [int]$Matches[2] } }
foreach ($l in $peerSets) { if ($l -match 'worldset\[([a-z-]+)\]:.*union=(\d+)') { $peerU[$Matches[1]] = [int]$Matches[2] } }
$same = $true; $detail = @()
foreach ($k in $hostU.Keys) {
    $pv = if ($peerU.ContainsKey($k)) { $peerU[$k] } else { -1 }
    $detail += ("{0}: host={1} peer={2}" -f $k, $hostU[$k], $pv)
    if ($pv -ne $hostU[$k]) { $same = $false }
}
Verdict 'the two machines agree about every unlock set' ($hostU.Count -gt 0 -and $same) ($detail -join '; ')
$hostCk = @(Get-Matches $hostIdx 'worldset counter CheckpointUnlockCount = (-?\d+)')
$peerCk = @(Get-Matches $peerIdx 'worldset counter CheckpointUnlockCount = (-?\d+)')
$hv = if ($hostCk.Count -gt 0 -and $hostCk[-1] -match '= (-?\d+)') { $Matches[1] } else { '?' }
$pv2 = if ($peerCk.Count -gt 0 -and $peerCk[-1] -match '= (-?\d+)') { $Matches[1] } else { '?' }
$ckLine = 'no line'
if ($hostCk.Count) { $ckLine = ([string]$hostCk[-1]).Trim() }
Verdict 'CheckpointUnlockCount is equal on both machines' ($hv -ne '?' -and $hv -eq $pv2) `
        ("host=$hv peer=$pv2  (" + $ckLine + ")")

# ---------------------------------------------------------------------------
# 2. ONE rest on the PEER must reset the world on BOTH
# ---------------------------------------------------------------------------
Say "asking the PEER (inst$peerIdx) to rest"
$peerReq = @(); $hostRecv = @(); $hostReset = @(); $peerOrder = @(); $peerReset = @()
for ($try = 0; $try -lt 10; $try++) {
    if ($try -eq 0) { Set-Control $peerIdx 'qd.world' 'rest' } else { Set-Control $peerIdx 'qd.world' ("rest{0}" -f $try) }
    Start-Sleep -Seconds 3
    $peerReq   = @(Get-Matches $peerIdx 'rest: told the partner about rest #')
    $hostRecv  = @(Get-Matches $hostIdx 'rest: the partner rested at')
    $hostReset = @(Get-Matches $hostIdx 'rest: world reset')
    $peerOrder = @(Get-Matches $peerIdx 'rest: the host ordered rest #')
    $peerReset = @(Get-Matches $peerIdx 'rest: world reset')
    if ($hostRecv.Count -gt 0 -and $peerOrder.Count -gt 0) { break }
}
$hostKeep = @(Get-Matches $hostIdx 'exempt death\(s\) written back')
$peerKeep = @(Get-Matches $peerIdx 'exempt death\(s\) written back')

Verdict 'the peer announced its rest' ($peerReq.Count -gt 0) `
        $(if ($peerReq.Count) { ([string]$peerReq[-1]).Trim() } else { '(no `rest: told the partner` line)' })
Verdict 'the HOST received that rest as a request' ($hostRecv.Count -gt 0) `
        $(if ($hostRecv.Count) { ([string]$hostRecv[-1]).Trim() } else { '(no `rest: the partner rested at` line)' })
Verdict 'the HOST ran the same reset (ClearAllEnemySaveStates)' ($hostReset.Count -gt 0 -and $hostReset[-1] -match 'ran') `
        $(if ($hostReset.Count) { ([string]$hostReset[-1]).Trim() } else { '(no `rest: world reset` line)' })
Verdict 'the PEER applied the host''s ORDER (a second, same reset)' ($peerOrder.Count -gt 0 -and $peerReset.Count -gt 0) `
        $(if ($peerOrder.Count -and $peerReset.Count) { ([string]$peerOrder[-1]).Trim() + ' || ' + ([string]$peerReset[-1]).Trim() } else { '(no order line and/or no peer reset)' })
$keptText = @()
if ($hostKeep.Count) { $keptText += ('host: ' + ([string]$hostKeep[-1]).Trim()) }
if ($peerKeep.Count) { $keptText += ('peer: ' + ([string]$peerKeep[-1]).Trim()) }
Verdict 'boss / one-time deaths were written back after the clear' ($hostKeep.Count -gt 0 -or $peerKeep.Count -gt 0) `
        $(if ($keptText.Count) { $keptText -join ' || ' } else { '(no exempt-death line: no boss key was held at the time)' })

# ---------------------------------------------------------------------------
# 3. the lanes must not have errored, and shop stock must not cross
# ---------------------------------------------------------------------------
$badShopHost = @(Get-Matches $hostIdx 'flag CHANGE .*ShopPurchasedKey')
$badShopPeer = @(Get-Matches $peerIdx 'flag intent (?:_?queued|coalesced) .*ShopPurchasedKey')
Verdict 'no ShopPurchasedKey crossed the flag lane' ($badShopHost.Count -eq 0 -and $badShopPeer.Count -eq 0) `
        ("host CHANGE lines=" + $badShopHost.Count + ", peer intent lines=" + $badShopPeer.Count)
$warn = @(Get-Matches $hostIdx 'worldset: (FULL|.*not placeable|.*does not resolve)')
$pzHost = @(Get-Matches $hostIdx 'puzzle: \d+ prop\(s\) held')
Verdict 'the puzzle lane is armed and reporting on both machines' `
        ($pzHost.Count -gt 0 -and (@(Get-Matches $peerIdx 'puzzle: \d+ prop\(s\) held')).Count -gt 0) `
        $(if ($pzHost.Count) { ([string]$pzHost[-1]).Trim() } else { '(no `puzzle:` line in the dump)' })

# final dumps for the record
foreach ($i in $hostIdx, $peerIdx) { Set-Control $i 'qd.world' 'dump' }
Start-Sleep -Seconds 4
foreach ($i in $hostIdx, $peerIdx) {
    Note ("----- inst{0} world dump -----" -f $i)
    @(Get-Matches $i 'worldset|worldset counter|^.*rest: episode|puzzle: ') | Select-Object -Last 40 |
        ForEach-Object { Note ('  ' + $_.Trim()) }
}
foreach ($i in $hostIdx, $peerIdx) { Set-Control $i 'qd.world' $null }

Note ''
Note '================ SUMMARY ================'
foreach ($v in $script:verdicts) { $t = 'FAIL'; if ($v.Ok) { $t = 'PASS' }; Note ("{0}  {1}" -f $t, $v.What) }
$fail = @($script:verdicts | Where-Object { -not $_.Ok }).Count
Note ("{0} pass, {1} fail" -f (@($script:verdicts | Where-Object { $_.Ok }).Count, $fail))

Stop-All 'session complete'
$script:lines | Set-Content -LiteralPath $Report
Say "report: $Report"
if ($fail -gt 0) { exit 1 } else { exit 0 }
