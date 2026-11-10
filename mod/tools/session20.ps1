# session20.ps1 -- Session 20: the boss bar, the stray kill key, and the area follow.
#
# WHAT THIS SESSION IS FOR
# ------------------------
# Session 19 left three measurable defects and one claim that was not met:
#
#   R143  the two screens showed different boss keys.  The host repaired
#         `03FDBC21` (a boss only IT held) every 3 s while the peer logged
#         `no local copy`, and the peer's `17181166` was a key the host held as a
#         standing death.  Fixed by ASKING the peer before raising the one bar, and
#         by reading liveness from the machine that RUNS the enemy.
#   R137  one stray kill key per ~600 s (`FA2FC279` in the 601 s run), dead on one
#         machine and absent from the other because the death sweep only ran for
#         owned keys.  Fixed by broadcasting an observed death even when this
#         machine does not run the enemy.
#   R147  one 600 s run ended with the machines in different areas (780_SZ_Depths
#         vs the hub).  Entering an area was a LOCAL lever on each machine; the
#         area is now announced on the travel lane and the partner streams it or
#         refuses with a named error.
#   R137' the 3 x 600 s "all diff 0" claim was not met.  This run measures it.
#
# PHASES (one .ps1 invocation per phase; a launched game does not survive the
# command that started it -- R14):
#   boss       enter 780_SZ_Depths and fight the Green Dragon: both bars must name
#              the same key, carry equal HP, and both reach 0 on the kill.
#   soak-hub   three back-to-back 600 s soaks in the hub, with the kill diff, the
#              shared-HP comparison, the same-world check, and the boss sampling.
#   soak-780   the partner is made to FOLLOW the host into 780 (TP_AREA), then one
#              600 s soak there, where both machines hold the same authored keys.
#   all        boss, then the three hub runs, then soak-780.
#
# HONESTY RULES INHERITED FROM SESSION 18
# ---------------------------------------
# FAULT and ERR are counted separately (a lane's refused write is not a crash).
# A verdict is a measurement, not an expectation: every check prints the line it
# read, and a phase that cannot reach its precondition REFUSES to run rather than
# reporting a number about a run that did not happen.
[CmdletBinding()]
param(
    [ValidateSet('boss', 'soak-hub', 'soak-780', 'all')]
    [string]$Phase = 'all',
    [int]$BootSeconds = 300,
    [int]$SoakSeconds = 600,
    [int]$HubRuns = 3,
    [int]$BossSeconds = 180,
    [string]$Report = '',
    [switch]$ShippedDefaults
)

$ErrorActionPreference = 'Stop'
$here    = $PSScriptRoot
$repo    = Split-Path -Parent (Split-Path -Parent $here)
$gameDir = 'G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64'
$relay   = Join-Path (Split-Path -Parent $here) 'rust\target\release\qd-relay.exe'
$rigBase = 'G:\Freespace\Queens_Domain\_test'
$rigRoot = Join-Path $rigBase ('s20-' + $Phase)
$seed    = Join-Path $repo 'analysis\save-backup-before-live'
$outDir  = Join-Path $rigRoot 'out'
if (-not $Report) { $Report = Join-Path $outDir ('report-' + $Phase + '.txt') }
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force -Path $outDir | Out-Null }

$script:lines = New-Object System.Collections.Generic.List[string]
trap {
    Write-Output ("UNHANDLED: " + $_.Exception.Message)
    Write-Output ("AT: " + $_.InvocationInfo.PositionMessage)
    $script:lines.Add("UNHANDLED: " + $_.Exception.Message) | Out-Null
    $script:lines.Add("AT: " + $_.InvocationInfo.PositionMessage) | Out-Null
    try { Stop-All } catch {}
    try { $script:lines | Set-Content -LiteralPath $Report } catch {}
    exit 1
}
function Say($m) {
    $s = "[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $m
    Write-Output $s
    $script:lines.Add($s) | Out-Null
}
function Note($m) { Write-Output $m; $script:lines.Add($m) | Out-Null }

function Get-RigPaths([int]$i) {
    $root   = Join-Path $rigRoot ("inst{0}" -f $i)
    $suffix = "qd$i"
    [pscustomobject]@{
        Index  = $i; Root = $root; Suffix = $suffix
        Log    = Join-Path $gameDir ("qdcoop-{0}.log" -f $suffix)
        Tp     = Join-Path $root 'qd.tp';   Act  = Join-Path $root 'qd.act'
        Scan   = Join-Path $root 'qd.scan'; God  = Join-Path $root 'qd.god'
        Hit    = Join-Path $root 'qd.hit';  Cheat = Join-Path $root 'qd.cheat'
        CheatArg = Join-Path $root 'qd.cheatarg'; Travel = Join-Path $root 'qd.travel'
        Hard   = Join-Path $root 'qd.hard'; Level = Join-Path $root 'qd.level'
        List   = Join-Path $root 'qd.list'; Near = Join-Path $root 'qd.near'
        Drops  = Join-Path $root 'qd.drops'; Menu = Join-Path $root 'qd.menu'
        EFunc  = Join-Path $root 'qd.efunc'; EAtk = Join-Path $root 'qd.eattack'
        Use    = Join-Path $root 'qd.use';   Pdmg = Join-Path $root 'qd.pdmg'
        Kill   = Join-Path $root 'qd.kill';  PeTrace = Join-Path $root 'qd.petrace'
        Enemies = Join-Path $root 'qd.enemies'; Sequence = Join-Path $root 'qd.sequence'
        EnemyKill = Join-Path $root 'qd.enemykill'; HitGuid = Join-Path $root 'qd.hitguid'
        Obj    = Join-Path $root 'qd.obj';  Flag = Join-Path $root 'qd.flag'
        Gate   = Join-Path $root 'qd.gate'
        Pid = "$root.pid"
    }
}
$R1 = Get-RigPaths 1
$R2 = Get-RigPaths 2
$inst = @{ 1 = $R1; 2 = $R2 }

function Read-LogLines([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) { return @() }
    for ($try = 0; $try -lt 3; $try++) {
        try {
            $fs = [System.IO.File]::Open($path, [System.IO.FileMode]::Open,
                                         [System.IO.FileAccess]::Read,
                                         ([System.IO.FileShare]::ReadWrite -bor [System.IO.FileShare]::Delete))
            try {
                $sr = New-Object System.IO.StreamReader($fs, [System.Text.Encoding]::UTF8, $true)
                $txt = $sr.ReadToEnd()
                $sr.Dispose()
            } finally { $fs.Dispose() }
            return @($txt -split "`r?`n")
        } catch { Start-Sleep -Milliseconds 50 }
    }
    return @()
}
function Get-RunLines([int]$i) {
    $p = $inst[$i].Log
    if (-not (Test-Path -LiteralPath $p)) { return @() }
    $all = @(Read-LogLines $p)
    if ($all.Count -eq 0) { return @() }
    $start = 0
    for ($k = $all.Count - 1; $k -ge 0; $k--) {
        if ($all[$k] -match 'hooked FEngineLoop') { $start = $k; break }
    }
    return @($all[$start..($all.Count - 1)])
}
function Get-Matches([int]$i, [string]$pattern) {
    return @(Get-RunLines $i | Where-Object { $_ -match $pattern })
}
function Get-LoadMatches([int]$i, [string]$pattern) {
    return @(Read-LogLines $inst[$i].Log | Where-Object { $_ -match $pattern })
}
function Wait-LoadMatch([int]$i, [string]$pattern, [int]$seconds) {
    $deadline = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $deadline) {
        $m = @(Get-LoadMatches $i $pattern)
        if ($m.Count -gt 0) { return $m }
        Start-Sleep -Milliseconds 300
    }
    return @()
}
function Get-LineMs([string]$ln) {
    if ($ln -match '^\s*(\d{1,2}):(\d{2}):(\d{2})\.(\d{3})') {
        return ([int]$Matches[1] * 3600000 + [int]$Matches[2] * 60000 +
                [int]$Matches[3] * 1000 + [int]$Matches[4])
    }
    return -1
}
function Get-Mark([int]$i) {
    $now = Get-Date
    $ms = $now.Hour * 3600000 + $now.Minute * 60000 + $now.Second * 1000 + $now.Millisecond
    return $ms - 1000
}
function Test-Alive([int]$i) {
    if (-not $script:pids.ContainsKey($i)) { return $false }
    return [bool](Get-Process -Id $script:pids[$i] -ErrorAction SilentlyContinue)
}
function Get-Since([int]$i, [int]$markMs) {
    $out = @()
    foreach ($ln in @(Get-RunLines $i)) {
        $t = Get-LineMs ([string]$ln)
        if ($t -ge 0 -and $t -ge $markMs) { $out += $ln }
    }
    return $out
}
$script:sinceCache = @{}
function Get-Window([int]$i, [int]$markMs, [switch]$Fresh) {
    $key = "$i|$markMs"
    if (-not $Fresh -and $script:sinceCache.ContainsKey($key)) { return $script:sinceCache[$key] }
    $v = @(Get-Since $i $markMs)
    $script:sinceCache[$key] = $v
    return $v
}
function Get-RunWindow([int]$i) { return @(Get-Window $i 0) }
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
        $m = @(Get-Matches $i 'scan: local at|scan: health')
        if ($m.Count -gt 0) { return $true }
        Start-Sleep -Milliseconds 500
    }
    return $false
}
function Set-Control([int]$i, [string]$leaf, $value) {
    $p = Join-Path -Path ([string]$inst[$i].Root) -ChildPath ([string]$leaf)
    for ($try = 0; $try -lt 6; $try++) {
        try {
            if ($null -eq $value) { Remove-Item -LiteralPath $p -ErrorAction SilentlyContinue }
            else { Set-Content -LiteralPath $p -Value ([string]$value) -NoNewline -ErrorAction Stop }
            return
        } catch [System.IO.IOException], [System.UnauthorizedAccessException] {
            Start-Sleep -Milliseconds 120
        } catch {
            if ($try -ge 5) { throw }
            Start-Sleep -Milliseconds 120
        }
    }
    Say "  WARN: could not write the lever '$leaf' for instance $i after 6 tries"
}
$script:verdicts = New-Object System.Collections.Generic.List[object]
function Check([string]$step, [int]$i, [string]$pattern, [int]$secs, [string]$label,
               [int]$Mark = 0) {
    if (-not (Test-Alive $i)) {
        $script:verdicts.Add([pscustomobject]@{ Step = $step; Inst = $i; What = $label; Ok = $false; Line = 'INSTANCE DIED' }) | Out-Null
        Note "  FAIL [$i] $label -- the instance is no longer running"
        return $false
    }
    $m = Wait-Since $i $pattern $Mark $secs
    $found = ($m.Count -gt 0)
    $script:verdicts.Add([pscustomobject]@{ Step = $step; Inst = $i; What = $label; Ok = $found; Line = ($m | Select-Object -Last 1) }) | Out-Null
    if ($found) { $line = "  PASS [$i] $label" }
    else { $line = "  --   [$i] $label (no line matches '$pattern' in ${secs}s)" }
    Note $line
    return $found
}
function Verdict([string]$step, [int]$i, [bool]$ok, [string]$what, [string]$line = '') {
    $script:verdicts.Add([pscustomobject]@{ Step = $step; Inst = $i; What = $what; Ok = $ok; Line = $line }) | Out-Null
    Note ("  {0} [{1}] {2}{3}" -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $i, $what,
          $(if ($line) { " -- $line" } else { '' }))
}
function Step([string]$title, [string]$body) { Note ''; Note "### $title"; if ($body) { Note $body } }
function Show-Since([int]$i, [string]$pattern, [int]$mark, [int]$n) {
    $m = @(Get-Since $i $mark | Where-Object { $_ -match $pattern })
    if ($m.Count -eq 0) { Note "  [$i] (no line matches '$pattern' since the step started)"; return }
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
    $yaw = ($script:goSeq * 37) % 360
    Set-Control $i 'qd.near' ([string]$near)
    Set-Control $i 'qd.tp' ('near:' + $name + ',' + $yaw)
}
function Invoke-Hard([int]$i, [string]$map) {
    # A nonce re-arms an unchanged instruction: the first attempt can arrive before the
    # level table is ready, and the lever must be retryable (R147).
    $nonce = Get-Date -Format 'HHmmssfff'
    Set-Control $i 'qd.hard' ($map + '#' + $nonce)
    Say "  hard[$i] -> $map (nonce $nonce)"
}

function Stop-All {
    Get-Process -Name 'QueensDomain-Win64-Shipping', 'qd-relay' -ErrorAction SilentlyContinue |
        ForEach-Object {
            Say "  stopping leftover $($_.ProcessName) pid=$($_.Id)"
            Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue
        }
    Start-Sleep -Seconds 2
}

function Install-And-Configure {
    $built = Join-Path $repo 'mod\native\build\winmm.dll'
    if (-not (Test-Path -LiteralPath $built)) { Say "REFUSED: built proxy not found at $built"; exit 2 }
    $dst = Join-Path $gameDir 'winmm.dll'
    try { Copy-Item -LiteralPath $built -Destination $dst -Force }
    catch {
        $held = $dst + '.held-' + (Get-Date -Format 'HHmmss')
        Rename-Item -LiteralPath $dst -NewName (Split-Path -Leaf $held) -Force
        Copy-Item -LiteralPath $built -Destination $dst -Force
        Say "destination was image-mapped; moved it to $held and installed the new proxy"
    }
    $pdb = Join-Path $repo 'mod\native\build\winmm.pdb'
    if (Test-Path -LiteralPath $pdb) {
        Copy-Item -LiteralPath $pdb -Destination (Join-Path $gameDir 'winmm.pdb') -Force
        Say "installed winmm.pdb beside it (symbols for a dump)"
    }
    Say "installed $built -> $dst"

    $ini = Join-Path $gameDir 'qdcoop.ini'
    if ($ShippedDefaults) {
        $srcIni = Join-Path $repo 'mod\qdcoop.ini'
        $text = Get-Content -LiteralPath $srcIni -Raw
        $text = $text -replace '(?m)^RelayHost=.*$', 'RelayHost=127.0.0.1'
        $text = $text -replace '(?m)^RelayPort=.*$', 'RelayPort=47800'
        $text = $text -replace '(?m)^LogLevel=.*$', 'LogLevel=3'
        $text | Set-Content -LiteralPath $ini -NoNewline
        Say "wrote $ini from the SHIPPED qdcoop.ini (relay pinned to loopback, LogLevel=3)"
    } else {
        $lines = @('[Local]', 'Enabled=1', 'LogLevel=3', '', '[Net]',
                   'RelayHost=127.0.0.1', 'RelayPort=47800', '', '[Rules]',
                   'FlagSync=1', 'InventorySync=1', 'ActorStateSync=1', 'ObjectSync=1',
                   'EnemySync=1', 'PuppetSync=1', 'PlayerBody=1', 'DialogueSync=1',
                   'StreamUnion=1', 'TravelSync=1', 'CutsceneSync=1', 'MoverSync=1',
                   'LootSync=1', 'SuspendGuard=1', 'SuppressMenuPause=1', 'HostAuthoritative=1',
                   '', '[Tuning]', 'PlayerHz=30', 'EnemyRowsPerTick=20', 'SpawnAttemptsMax=5')
        $lines | Set-Content -LiteralPath $ini
        $sw = @(Get-Content -LiteralPath $ini | Where-Object { $_ -match '=' -and $_ -notmatch '^;' })
        Say ("wrote $ini from the documented PLAY set ({0} switches)" -f $sw.Count)
    }
    Copy-Item -LiteralPath (Join-Path $gameDir 'qdcoop.ini') -Destination (Join-Path $outDir 'qdcoop-used.ini') -Force
}

function Seed-FreshPair {
    Say 're-seeding both instances from the pristine pre-live save'
    foreach ($i in 1, 2) {
        $r = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'make_test_instance.ps1') `
                -Name ("inst{0}" -f $i) -Index $i -SourceSave $seed -Root $rigRoot -Suffix $inst[$i].Suffix -Force 2>&1
        $r | Where-Object { $_ -match 'FAIL|REFUSED|seeded|save dir|Instance ready' } | ForEach-Object { Note "  $_" }
        if ($LASTEXITCODE -ne 0) { Say "REFUSED: make_test_instance refused instance $i"; exit 2 }
    }
}

function Start-Boot([string]$tag) {
    Stop-All
    Install-And-Configure
    foreach ($i in 1, 2) {
        foreach ($leaf in 'qd.tp','qd.act','qd.hit','qd.cheat','qd.cheatarg','qd.travel','qd.hard',
                          'qd.level','qd.list','qd.near','qd.drops','qd.menu','qd.efunc','qd.eattack',
                          'qd.use','qd.pdmg','qd.kill','qd.petrace','qd.enemies','qd.sequence',
                          'qd.enemykill','qd.hitguid','qd.obj','qd.flag','qd.gate') {
            Set-Control $i $leaf $null
        }
        Set-Control $i 'qd.scan' '1'
        Set-Control $i 'qd.god' '400'
        if (Test-Path -LiteralPath $inst[$i].Pid) { Remove-Item -LiteralPath $inst[$i].Pid -ErrorAction SilentlyContinue }
        $old = $inst[$i].Log
        if (Test-Path -LiteralPath $old) { Move-Item -LiteralPath $old -Destination ($old + '.prev') -Force }
    }
    $env:QDCOOP_AUTOLOAD = '0'
    $env:QDCOOP_TEST_NEAR = '140'
    $env:QDCOOP_TEST_HITMS = '3000'

    if (-not (Test-Path -LiteralPath $relay)) { Say "REFUSED: relay not built at $relay"; exit 2 }
    $relayLog = Join-Path $outDir ("relay-$tag.log")
    Remove-Item -LiteralPath $relayLog -ErrorAction SilentlyContinue
    $script:relayProc = Start-Process -FilePath $relay -ArgumentList @('--bind', '127.0.0.1:47800') `
            -RedirectStandardOutput $relayLog -RedirectStandardError (Join-Path $outDir ("relay-$tag.err")) `
            -WindowStyle Hidden -PassThru
    Say "relay pid=$($script:relayProc.Id) -> $relayLog"
    Start-Sleep -Seconds 2

    $script:pids = @{}
    foreach ($i in 1, 2) {
        $ueLog = Join-Path $outDir ("$tag-inst{0}-ue.log" -f $i)
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
            if ($roles.ContainsKey($i)) { continue }
            if (-not $script:pids.ContainsKey($i)) { continue }
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
        foreach ($i in 1, 2) {
            Note "  [$i] alive=$(Test-Alive $i)"
            Show-Since $i 'session|REFUS|ERR |PROBE|substrate' 0 12
        }
        Stop-All
        exit 2
    }
    Say "roles: HOST=inst$($script:H) PEER=inst$($script:P)"

    Say "waiting for gameplay on both (up to $BootSeconds s)"
    foreach ($i in @($script:H, $script:P)) {
        $ok = Wait-Gameplay $i $BootSeconds
        Say "inst${i} gameplay=$ok"
        Verdict 'boot' $i $ok 'the machine reached gameplay (it has a pawn to report)' ''
    }
}

# ---------------------------------------------------------------------------
# The world / area a machine is in, read from the mod's own standing line.
# Session 18 read this from the LAST `level union` line, which names whichever
# sublevel that line happened to mention -- so two machines in the same UWorld
# could be reported as "different worlds" (R147's measurement was made with that
# reader).  `world:` is the mod's own agreement statement and `area=` the pin.
# ---------------------------------------------------------------------------
function Get-WorldState([int]$i) {
    $out = [pscustomobject]@{ World = ''; Partner = ''; Agree = ''; Area = '' }
    $rows = @(Get-Matches $i 'world: local=')
    if ($rows.Count -gt 0) {
        $s = [string]$rows[-1]
        if ($s -match 'world: local=(\S*) partner=(\S*) agree=(\d+)') {
            $out.World = $Matches[1]; $out.Partner = $Matches[2]; $out.Agree = $Matches[3]
        }
        if ($s -match 'area=(\S+)$') { $out.Area = $Matches[1] }
        return $out
    }
    # Fallback for a build without the standing line: the UWorld name the game logs.
    $lv = @(Get-Matches $i 'level name: (\S+) from ')
    if ($lv.Count -gt 0 -and [string]$lv[-1] -match 'level name: (\S+) from ') { $out.World = $Matches[1] }
    $pin = @(Get-Matches $i 'rig: hard lever -> (\S+) streamed')
    if ($pin.Count -gt 0 -and [string]$pin[-1] -match 'rig: hard lever -> (\S+) streamed') { $out.Area = $Matches[1] }
    return $out
}

# `hp=-1` is UNKNOWN (the mod prints it when the enemy's HP is unreadable), not zero.
function Get-BossFromLines($lines) {
    $rows = @($lines | Where-Object { $_ -match 'boss: bar key=([0-9A-Fa-f]{8}) hp=(-?\d+)' })
    if ($rows.Count -eq 0) { return $null }
    $s = [string]$rows[-1]
    if ($s -notmatch 'boss: bar key=([0-9A-Fa-f]{8}) hp=(-?\d+)') { return $null }
    $key = $Matches[1].ToUpperInvariant(); $hp = [int]$Matches[2]
    return [pscustomobject]@{ Key = $key; Hp = $hp; Ms = (Get-LineMs $s); Line = $s.Trim() }
}
function Get-BossSample([int]$i, [int]$mark = 0) {
    return Get-BossFromLines @(Get-Since $i $mark)
}

function Enter-Area([int]$H, [int]$P, [string]$area, [string]$anchor, [string]$step) {
    Step "$step -- the HOST enters $area; the PEER must follow" `
         'R147: entering an area is a streamed sublevel, so the signal is TP_AREA on the travel lane and a local refusal is a named error.'
    $mH = Get-Mark $H
    $mP = Get-Mark $P
    # Deliberately the HOST ONLY: this is the live proof of "the partner must always
    # follow, or travel must be refused", not of two independent local levers.
    Invoke-Hard $H $area
    # NOTE: `Check` writes its note to the pipeline, so `[bool](Check ...)` would be true
    # because it emitted a string.  The verdict list is the authoritative answer.
    [void](Check $step $H ('rig: hard lever -> ' + [regex]::Escape($area) + ' streamed=1') 90 'the HOST streamed the area locally' $mH)
    $hostOk = [bool]$script:verdicts[$script:verdicts.Count - 1].Ok
    if (-not $hostOk) {
        Verdict $step $P $false 'the PEER follows the host into the area' 'the host never entered it'
        return $false
    }
    # The peer's follow: either the TP_AREA instruction streamed it, or (if the peer's own
    # earlier state already pinned it) it says so.  Both are a measured answer.
    $follow = @(Wait-Since $P ('travel: entered the partner.s area ' + [regex]::Escape($area) + '|travel: area ' + [regex]::Escape($area) + ' is already pinned') $mP 90)
    Verdict $step $P ($follow.Count -gt 0) 'the PEER followed the host into the area' $(if ($follow.Count) { ([string]$follow[-1]).Trim() } else { '' })
    if ($follow.Count -eq 0) {
        Show-Since $P 'travel:|level union|rig: hard' $mP 8
        return $false
    }
    # Both machines must have the level ENGINE state, not merely the pin.
    [void](Check $step $H ('rig: level engine state ' + [regex]::Escape($area) + ' LoadedLevel=1 actuallyVisible=1') 90 'the area is LOADED and SHOWN on the host' $mH)
    [void](Check $step $P ('rig: level engine state ' + [regex]::Escape($area) + ' LoadedLevel=1 actuallyVisible=1') 90 'the area is LOADED and SHOWN on the peer' $mP)
    Start-Sleep -Seconds 6
    Invoke-Go $H $anchor 400
    Invoke-Go $P $anchor 400
    Start-Sleep -Seconds 10
    [void](Check $step $H ('dev teleport near .' + [regex]::Escape($anchor) + '. -> ' + [regex]::Escape($anchor) + ' at') 45 'the host stands at the anchor' $mH)
    [void](Check $step $P ('dev teleport near .' + [regex]::Escape($anchor) + '. -> ' + [regex]::Escape($anchor) + ' at') 45 'the peer stands at the same anchor' $mP)
    $pH = Get-LastPos $H $mH
    $pP = Get-LastPos $P $mP
    $dist = $null
    if ($pH -and $pP) {
        $dx = $pH[0] - $pP[0]; $dy = $pH[1] - $pP[1]; $dz = $pH[2] - $pP[2]
        $dist = [math]::Sqrt($dx*$dx + $dy*$dy + $dz*$dz)
    }
    Verdict $step $P ([bool]($dist -ne $null -and $dist -lt 600)) 'both players stand together in the area' $(if ($dist -ne $null) { ("distance={0:N0} cm" -f $dist) } else { 'a position is missing' })
    return [bool]($dist -ne $null -and $dist -lt 600)
}

# ---------------------------------------------------------------------------
# The soak body (Session 18's, unchanged in behaviour): hit the nearest locally-run
# enemy every 3 s and re-anchor every other iteration.  Added here: the boss bar is
# sampled every 6 s, and the world/area is read from the mod's standing line.
# ---------------------------------------------------------------------------
function Invoke-OneSoak([int]$H, [int]$P, [string]$step, [int]$run, [string]$anchor, [switch]$WithBoss) {
    $m0 = Get-Mark $H
    $swm = @(Read-LogLines $inst[$H].Log | Where-Object { $_ -match 'switches: ' })
    $swLine = if ($swm.Count -gt 0) { ([string]$swm[-1]).Trim() } else { '' }
    if ($swLine) { Note ("  [$H] " + $swLine) }
    $isPlay = $swLine -match 'enemy=1'
    Verdict $step $H $isPlay 'the switch set under test is the documented PLAY set' $swLine

    $wH0 = Get-WorldState $H; $wP0 = Get-WorldState $P
    Note ("  world at start: host='{0}' area='{1}' peer='{2}' area='{3}'" -f $wH0.World, $wH0.Area, $wP0.World, $wP0.Area)

    if ($anchor) {
        Invoke-Go $H $anchor 400
        Invoke-Go $P $anchor 400
        Start-Sleep -Seconds 10
    }

    $start = Get-Date
    $end = $start.AddSeconds($SoakSeconds)
    $n = 0
    $died = $false
    $bossSamples = New-Object System.Collections.Generic.List[object]
    while ((Get-Date) -lt $end) {
        if (-not (Test-Alive $H) -or -not (Test-Alive $P)) { $died = $true; break }
        $n++
        Set-Control $H 'qd.hit' ([string](200 + ($n % 3) * 100))
        Set-Control $P 'qd.hit' ([string](200 + ($n % 3) * 100))
        if (($n % 2) -eq 0) {
            Invoke-Go $H 'enemy' 200
            Invoke-Go $P 'enemy' 200
        }
        # Every 30 s (n increments every 3 s): enough to see the bar's key and HP settle
        # during a soak without re-reading a 10-20 MB log every few seconds.
        if ($WithBoss -and ($n % 10) -eq 0) {
            $bh = Get-BossSample $H $m0; $bp = Get-BossSample $P $m0
            $bossSamples.Add([pscustomobject]@{
                T = [int]((Get-Date) - $start).TotalSeconds
                HostKey = $(if ($bh) { $bh.Key } else { '-' }); HostHp = $(if ($bh) { $bh.Hp } else { -1 })
                PeerKey = $(if ($bp) { $bp.Key } else { '-' }); PeerHp = $(if ($bp) { $bp.Hp } else { -1 })
            }) | Out-Null
        }
        Start-Sleep -Seconds 3
    }
    $elapsed = [int]((Get-Date) - $start).TotalSeconds
    Note ("  soak run {0}: {1} s of {2} s, iterations={3}" -f $run, $elapsed, $SoakSeconds, $n)
    Verdict $step $H (-not $died) 'both processes survived the soak' $(if ($died) { "a process died after ${elapsed}s" } else { "${elapsed}s" })
    foreach ($i in @($H, $P)) {
        $alive = Test-Alive $i
        Verdict $step $i $alive 'the process is alive at the end of the soak' $(if ($alive) { "${elapsed}s" } else { '' })
    }
    Set-Control $H 'qd.hit' $null
    Set-Control $P 'qd.hit' $null

    # ---- the boss bar's own samples (R143) --------------------------------
    if ($WithBoss -and $bossSamples.Count -gt 0) {
        $same = 0; $paired = 0; $maxDelta = 0; $last = $null
        foreach ($s in $bossSamples) {
            if ($s.HostKey -eq '-' -or $s.PeerKey -eq '-') { continue }
            if ($s.HostHp -lt 0 -or $s.PeerHp -lt 0) { continue }   # unreadable, not zero
            $paired++
            if ($s.HostKey -eq $s.PeerKey) {
                $same++
                $d = [math]::Abs($s.HostHp - $s.PeerHp)
                if ($d -gt $maxDelta) { $maxDelta = $d }
                $last = $s
            }
        }
        $csv = Join-Path $outDir ("boss-samples-{0}.csv" -f $step)
        $bossSamples | Export-Csv -LiteralPath $csv -NoTypeInformation
        Note ("  boss samples: {0} rows, {1} paired, {2} same key, max hp delta {3}; csv={4}" -f
              $bossSamples.Count, $paired, $same, $maxDelta, $csv)
        Verdict $step $H ($paired -gt 0) 'a boss bar was sampled on both machines during the soak' ("paired=$paired")
        if ($last) {
            Verdict $step $H ($maxDelta -le 1200) 'the shared boss bar carries the SAME HP on both machines' ("max delta=$maxDelta of 65535")
        }
    }

    # ---- world agreement (R147) -------------------------------------------
    # The standing `world:` line is emitted every 10 s, and a load inside the last 3 s is
    # deliberately not yet a "divergence" (MapBoard's debounce).  Wait for a settled pass
    # so the last line on each machine describes the same instant, not a load in flight.
    Start-Sleep -Seconds 12
    $wH = Get-WorldState $H; $wP = Get-WorldState $P
    Note ("  world at end: host='{0}' partner='{1}' agree='{2}' area='{3}'" -f $wH.World, $wH.Partner, $wH.Agree, $wH.Area)
    Note ("                peer='{0}' partner='{1}' agree='{2}' area='{3}'" -f $wP.World, $wP.Partner, $wP.Agree, $wP.Area)
    $div = @(Get-Since $H $m0 | Where-Object { $_ -match 'WORLD DIVERGENCE' })
    Verdict $step $H ($div.Count -eq 0) 'no WORLD DIVERGENCE was reported during the run' ("lines=$($div.Count)")
    $sameWorld = ($wH.World -ne '' -and $wP.World -ne '' -and $wH.World -eq $wP.World -and
                  $wH.Partner -eq $wP.World -and $wP.Partner -eq $wH.World -and
                  $wH.Agree -eq '1' -and $wP.Agree -eq '1')
    Verdict $step $P $sameWorld 'both machines are in the SAME world (UWorld + agreement)' ("host=$($wH.World)/partner=$($wH.Partner)/agree=$($wH.Agree) peer=$($wP.World)/partner=$($wP.Partner)/agree=$($wP.Agree)")

    # ---- the per-GUID kill sets (R137) ------------------------------------
    if (-not (Test-Alive $H) -or -not (Test-Alive $P)) {
        Note '  (kill-set comparison skipped: a process is gone, so its dump cannot answer)'
        Verdict $step $H $false 'the per-GUID kill sets DIFF TO ZERO' 'a process died; the dump is unreadable'
        return
    }
    Set-Control $H 'qd.enemies' ('s20-h-' + (Get-Date -Format 'HHmmss'))
    Set-Control $P 'qd.enemies' ('s20-p-' + (Get-Date -Format 'HHmmss'))
    Start-Sleep -Seconds 4
    $rowsH = @(Get-Window $H 0 -Fresh)
    $rowsP = @(Get-Window $P 0 -Fresh)
    $keysH = @{}; $keysP = @{}; $lastH = @{}; $lastP = @{}
    foreach ($ln in $rowsH) {
        if ([string]$ln -match 'enemykey ([0-9A-Fa-f]{8}) (ALIVE|DEAD)') {
            $k = $Matches[1].ToUpperInvariant(); $keysH[$k] = $Matches[2]; $lastH[$k] = $ln
        }
    }
    foreach ($ln in $rowsP) {
        if ([string]$ln -match 'enemykey ([0-9A-Fa-f]{8}) (ALIVE|DEAD)') {
            $k = $Matches[1].ToUpperInvariant(); $keysP[$k] = $Matches[2]; $lastP[$k] = $ln
        }
    }
    $deadH = @($keysH.Keys | Where-Object { $keysH[$_] -eq 'DEAD' })
    $deadP = @($keysP.Keys | Where-Object { $keysP[$_] -eq 'DEAD' })
    $onlyH = @($deadH | Where-Object { -not $keysP.ContainsKey($_) -or $keysP[$_] -ne 'DEAD' })
    $onlyP = @($deadP | Where-Object { -not $keysH.ContainsKey($_) -or $keysH[$_] -ne 'DEAD' })
    Note ("  host keys={0} (dead {1}); peer keys={2} (dead {3})" -f $keysH.Count, $deadH.Count, $keysP.Count, $deadP.Count)
    Note ("  dead only on the host: {0} -> {1}" -f $onlyH.Count, ($onlyH -join ','))
    Note ("  dead only on the peer: {0} -> {1}" -f $onlyP.Count, ($onlyP -join ','))
    # R137's residual was ONE key with no explanation.  When a diff appears, print the
    # source of the identity that produced it (`src=0` spawner, `1` placed, `2` summon).
    foreach ($k in $onlyH) {
        Note ("    host-only {0}: {1}" -f $k, ([string]$lastH[$k]).Trim())
    }
    foreach ($k in $onlyP) {
        Note ("    peer-only {0}: {1}" -f $k, ([string]$lastP[$k]).Trim())
    }
    Verdict $step $H ($onlyH.Count -eq 0 -and $onlyP.Count -eq 0) 'the per-GUID kill sets DIFF TO ZERO' ("host-dead=$($deadH.Count) peer-dead=$($deadP.Count) diff=$($onlyH.Count + $onlyP.Count)")

    $common = @($keysH.Keys | Where-Object { $keysP.ContainsKey($_) -and $keysH[$_] -eq 'ALIVE' -and $keysP[$_] -eq 'ALIVE' })
    Note ("  keys alive on BOTH machines: {0}" -f $common.Count)
    $unequal = 0; $checked = 0
    foreach ($k in $common) {
        if (-not $lastH.ContainsKey($k) -or -not $lastP.ContainsKey($k)) { continue }
        $a = if ([string]$lastH[$k] -match 'hp=([-0-9.]+)') { [double]$Matches[1] } else { $null }
        $b = if ([string]$lastP[$k] -match 'hp=([-0-9.]+)') { [double]$Matches[1] } else { $null }
        if ($null -eq $a -or $null -eq $b) { continue }
        ++$checked
        if ([math]::Abs($a - $b) -ge 0.5) {
            ++$unequal
            Note ("    HP MISMATCH {0}: host={1} peer={2}" -f $k, $a, $b)
            Note ("      host: " + ([string]$lastH[$k]).Trim())
            Note ("      peer: " + ([string]$lastP[$k]).Trim())
        }
    }
    Verdict $step $P ($unequal -eq 0) 'the HP of shared enemies is EQUAL on both machines' ("checked=$checked unequal=$unequal")

    foreach ($i in @($H, $P)) {
        $fault = @(Get-Since $i $m0 | Where-Object { $_ -match 'ACCESS_VIOLATION|EXCEPTION|fatal|\bfault' })
        $err = @(Get-Since $i $m0 | Where-Object { $_ -match ' ERR ' })
        $warn = @(Get-Since $i $m0 | Where-Object { $_ -match ' WARN ' })
        $classes = @{}
        foreach ($w in ($warn + $err)) {
            $s = [string]$w; $c = '(unlabelled)'
            if ($s -match '(?:WARN|ERR)\s+([a-z\-]+):') { $c = $Matches[1] }
            if ($classes.ContainsKey($c)) { $classes[$c]++ } else { $classes[$c] = 1 }
        }
        Note ("  [{0}] FAULT lines (ACCESS_VIOLATION/EXCEPTION/fatal)={1}" -f $i, $fault.Count)
        Note ("  [{0}] ERR lines={1}  WARN lines={2}  classes={3}" -f $i, $err.Count, $warn.Count,
              (($classes.GetEnumerator() | Sort-Object Value -Descending | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join ' '))
        foreach ($e in ($fault | Select-Object -Last 4)) {
            $s = [string]$e
            if ($s.Length -gt 220) { $s = $s.Substring(0, 220) + ' ...' }
            Note "  [$i] $s"
        }
        Verdict $step $i ($fault.Count -eq 0) 'no FAULT lines during the soak (the crash claim)' ("faults=$($fault.Count)")
    }
}

# ---------------------------------------------------------------------------
# R143 -- the dragon fight: one key, equal HP, both to zero.
# ---------------------------------------------------------------------------
function Invoke-BossFight([int]$H, [int]$P, [string]$step) {
    Step 'R143 -- the Green Dragon: both bars, one key, equal HP, both to 0' `
         'The host owns the dragon, so it is the one hit; the peer follows through its rows. Sampling every 2 s.'
    $mH = Get-Mark $H
    Invoke-Go $H 'GreenDragon_Spawner' 400
    Invoke-Go $P 'GreenDragon_Spawner' 400
    Start-Sleep -Seconds 10
    [void](Check $step $H 'dev teleport near .GreenDragon_Spawner. -> ' 40 'the host stands at the dragon spawner' $mH)
    [void](Check $step $P 'dev teleport near .GreenDragon_Spawner. -> ' 40 'the peer stands at the dragon spawner' $mH)

    $mS = Get-Mark $H
    Set-Control $H 'qd.hit' '400'
    Set-Control $P 'qd.hit' '400'
    $samples = New-Object System.Collections.Generic.List[object]
    $paired = 0
    # The boss the fight is ABOUT: the same-key sample with the LOWEST HP.  The bar may
    # legitimately move to another full boss once this one dies, so "the last same-key
    # sample" is the wrong answer -- measured: it reported the post-kill key and made a
    # successful dragon fight look like a bar that never left full HP.
    $fightKey = $null; $fightMin = 70000; $missing = 0
    $deadline = (Get-Date).AddSeconds($BossSeconds)
    $i = 0
    while ((Get-Date) -lt $deadline) {
        if (-not (Test-Alive $H) -or -not (Test-Alive $P)) { break }
        Start-Sleep -Seconds 2
        $i++
        $winH = @(Get-Since $H $mS)
        $winP = @(Get-Since $P $mS)
        $bh = Get-BossFromLines $winH; $bp = Get-BossFromLines $winP
        $row = [pscustomobject]@{
            T = $i * 2
            HostKey = $(if ($bh) { $bh.Key } else { '-' }); HostHp = $(if ($bh) { $bh.Hp } else { -1 })
            PeerKey = $(if ($bp) { $bp.Key } else { '-' }); PeerHp = $(if ($bp) { $bp.Hp } else { -1 })
        }
        $samples.Add($row) | Out-Null
        Note ("  sample t+{0,4}s: host={1}/{2}  peer={3}/{4}" -f $row.T, $row.HostKey, $row.HostHp, $row.PeerKey, $row.PeerHp)
        if ($bh -and $bp -and $bh.Hp -ge 0 -and $bp.Hp -ge 0 -and $bh.Key -eq $bp.Key) {
            $paired++
            if ($row.HostHp -lt $fightMin) { $fightMin = $row.HostHp; $fightKey = $row.HostKey }
            if ($fightKey -eq $row.HostKey) { $missing = 0 } else { $missing++ }
            # The bar moved to another boss after this one died: the fight is over.
            if ($fightMin -lt 60000 -and $missing -ge 3) { break }
        }
    }
    Set-Control $H 'qd.hit' $null
    Set-Control $P 'qd.hit' $null
    Start-Sleep -Seconds 3

    $csv = Join-Path $outDir 'boss-fight.csv'
    $samples | Export-Csv -LiteralPath $csv -NoTypeInformation
    $fightRows = @($samples | Where-Object {
        $_.HostKey -eq $fightKey -and $_.PeerKey -eq $fightKey -and
        $_.HostHp -ge 0 -and $_.PeerHp -ge 0 })
    $maxDelta = 0
    foreach ($s in $fightRows) {
        $d = [math]::Abs($s.HostHp - $s.PeerHp)
        if ($d -gt $maxDelta) { $maxDelta = $d }
    }
    Note ("  boss fight samples: {0} rows, {1} paired, fight key={2} ({3} rows), max hp delta {4}; csv={5}" -f
          $samples.Count, $paired, $(if ($fightKey) { $fightKey } else { '-' }), $fightRows.Count, $maxDelta, $csv)

    $winH = @(Get-Since $H $mS)
    $winP = @(Get-Since $P $mS)
    $clearedH = $null; $clearedP = $null
    if ($fightKey) {
        $c = @($winH | Where-Object { $_ -match ('boss: (sent bar off|bar key=) key=' + $fightKey + '.*hp=0') })
        if ($c.Count -eq 0) {
            # The last read may be -1 because the actor was gone before it could be read.
            # The bar-off is still the event; the number is reported, never invented.
            $c = @($winH | Where-Object { $_ -match ('boss: sent bar off key=' + $fightKey) })
        }
        if ($c.Count -gt 0) { $clearedH = [string]$c[0] }
        $d = @($winP | Where-Object {
            $_ -match ('boss: bar lowered for key=' + $fightKey) -or
            $_ -match ('boss: bar key=' + $fightKey + '.*hp=0 \(lowered') })
        if ($d.Count -gt 0) { $clearedP = [string]$d[0] }
    }

    Note '  selection negotiation, host:'
    $winH | Where-Object { $_ -match 'boss: (active key ->|peer (ACK|NACK)|asked the partner|no answer for|sent bar off)' } |
        Select-Object -Last 8 | ForEach-Object { Note ("    " + ([string]$_).Trim()) }
    Note '  selection negotiation, peer:'
    $winP | Where-Object { $_ -match 'boss: (peer answer|follower raised|bar lowered|no local copy)' } |
        Select-Object -Last 8 | ForEach-Object { Note ("    " + ([string]$_).Trim()) }

    Verdict $step $H ($paired -gt 0) 'a boss bar sample exists on BOTH machines' ("paired=$paired")
    Verdict $step $H ($null -ne $fightKey) 'both bars name the SAME key during the fight' $(if ($fightKey) { ("key=" + $fightKey + " samples=" + $fightRows.Count) } else { '' })
    if ($fightKey) {
        Verdict $step $H ($maxDelta -le 1200) 'the bars carry the SAME HP (within 1200/65535)' ("max delta=$maxDelta over $($fightRows.Count) samples")
    }
    Verdict $step $H ($clearedH -ne $null -and $clearedH -match 'hp=0') 'the host bar reached 0 on the kill' $(if ($clearedH) { $clearedH.Trim() } else { '' })
    Verdict $step $P ($clearedP -ne $null -and $clearedP -match 'hp=0') 'the peer bar reached 0 on the kill' $(if ($clearedP) { $clearedP.Trim() } else { '' })
    if ($clearedH -and $clearedP) {
        $dh = Get-LineMs $clearedH; $dp = Get-LineMs $clearedP
        $gap = [math]::Abs($dh - $dp)
        Verdict $step $P ($gap -le 3000) 'both bars reached 0 TOGETHER (<= 3 s apart)' ("gap=${gap}ms")
    }
}

# ===========================================================================
# main
# ===========================================================================
Say ("session20 start: phase={0} switches={1} hubRuns={2} soak={3}s boss={4}s" -f $Phase,
     $(if ($ShippedDefaults) { 'SHIPPED' } else { 'DOCUMENTED PLAY' }), $HubRuns, $SoakSeconds, $BossSeconds)
Stop-All
Seed-FreshPair
Start-Boot $Phase
$H = $script:H
$P = $script:P

if ($Phase -eq 'boss' -or $Phase -eq 'all') {
    # Called as a STATEMENT: the notes go to the console/report, and the answer comes back
    # in the verdict list.  (`$ok = Enter-Area ...` would capture every note instead.)
    Enter-Area $H $P '780_SZ_Depths' 'SecretSword_Altar_BP' 'boss-area'
    $enterOk = [bool]$script:verdicts[-1].Ok
    $areaVerdicts = @($script:verdicts | Where-Object { $_.Step -eq 'boss-area' })
    foreach ($v in $areaVerdicts) { if (-not $v.Ok) { $enterOk = $false } }
    if ($enterOk) { Invoke-BossFight $H $P 'r143' }
    else { Verdict 'r143' $P $false 'the boss fight was run' 'area entry did not converge; refusing to measure a fight that did not happen' }
}

if ($Phase -eq 'soak-hub' -or $Phase -eq 'all') {
    Step 'SOAK-HUB -- three back-to-back 600 s runs' `
         'The R132 crash and the R137 kill-set convergence, measured three times in one pair of processes.'
    for ($run = 1; $run -le $HubRuns; $run++) {
        Step ("soak-hub run {0} of {1}" -f $run, $HubRuns)
        Invoke-OneSoak $H $P ("soakhub{0}" -f $run) $run 'GreenDragon_Spawner' -WithBoss
        if (-not (Test-Alive $H) -or -not (Test-Alive $P)) {
            Note ("  run {0} ended the process; the back-to-back claim cannot be met" -f $run)
            break
        }
    }
}

if ($Phase -eq 'soak-780' -or $Phase -eq 'all') {
    Enter-Area $H $P '780_SZ_Depths' 'SecretSword_Altar_BP' 'soak780'
    $enterOk = $true
    $areaVerdicts = @($script:verdicts | Where-Object { $_.Step -eq 'soak780' })
    foreach ($v in $areaVerdicts) { if (-not $v.Ok) { $enterOk = $false } }
    if ($enterOk) {
        Step 'SOAK-780 -- one 600 s run where both machines hold the same authored keys'
        Invoke-OneSoak $H $P 'soak780' 1 $null -WithBoss
    } else {
        Verdict 'soak780' $P $false 'the 780 soak was run' 'area entry did not converge; refusing to measure a run in two different places'
    }
}

Step 'SESSION SURVIVAL'
foreach ($i in @($H, $P)) {
    $alive = Test-Alive $i
    $script:verdicts.Add([pscustomobject]@{ Step = 'survival'; Inst = $i; What = 'alive at the end'; Ok = $alive; Line = '' }) | Out-Null
    Note "  $(if ($alive) { 'PASS' } else { 'FAIL' }) [$i] process alive=$alive"
    $fault = @(Get-Matches $i 'ACCESS_VIOLATION|EXCEPTION|fatal|\bfault')
    Note "  [$i] FAULT lines (ACCESS_VIOLATION/EXCEPTION/fatal): $($fault.Count)"
}
Stop-All

Note ''
Note '=== VERDICTS ==='
foreach ($v in $script:verdicts) {
    Note ("  {0,-10} inst{1}  {2,-6}  {3}  {4}" -f $v.Step, $v.Inst, $(if ($v.Ok) { 'PASS' } else { 'FAIL' }), $v.What, $v.Line)
}
$pass = @($script:verdicts | Where-Object { $_.Ok }).Count
$fail = @($script:verdicts | Where-Object { -not $_.Ok }).Count
Note ("=== {0} PASS, {1} FAIL ===" -f $pass, $fail)
$script:lines | Set-Content -LiteralPath $Report
