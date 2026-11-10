# session21.ps1 -- Session 21: the loot proof, HP that can be measured, and a 30-minute run.
#
# WHAT THIS SESSION IS FOR
# ------------------------
# Session 20 closed the boss bar, the stray kill key and the area follow, and left three
# things that are measurable rather than mysterious:
#
#   R129  loot hiding.  The reconciliation defect is fixed (the tombstone is re-derived
#         from the ledger every pass, one Blueprint call per drop), but no run has ever
#         shown `loot: hiding local drop`.  Session 18 named why: `near:` addresses a
#         CLASS and parks the pawn `offset` cm away, so it walked to a PLACED pickup
#         instead of the SPAWNED drop the tombstone names.  That lever now exists:
#         `qd.tp near:drop:<8hex>:<index>` resolves the lane's own key
#         `{source_hash, drop_index}`.  This phase uses it, in BOTH directions.
#
#   shared HP.  In Session 20's hub runs 2 and 3 the end-of-run dump had one shared live
#         key whose HP printed `hp=?` on both machines, so "shared HP equal" had no pair
#         to compare and the report could only say "no mismatch was observable".  The
#         dump now prints WHY a live key's HP is unknown (`why=nofn|callfail|noout|zero|
#         unseen`) and the soak now SAMPLES the dump through the run instead of reading
#         only the last one.  An unreadable pair is never counted as equal.
#
#   30 min. One two-instance run on the documented play set: movement and fighting in the
#         hub, two TP_AREA entries, one hard travel, and an end state that is checked.
#
# PHASES (one .ps1 invocation per phase; a launched game does not survive the command that
# started it -- R14)
#   lootdrop  a drop both machines hold, taken on one, hidden on the other, and refused a
#             second time -- then the same the other way round.
#   soak30    the 1800 s play-set run described above.
#   repro     R138/R140: a long churn run (repeated area entries) looking for a fault, and
#             the crash folder is read if one appears.
#   all       lootdrop, then soak30, then repro.
#
# HONESTY RULES INHERITED FROM SESSION 18
# ---------------------------------------
# FAULT and ERR are counted separately (a lane's refused write is not a crash).  A verdict
# is a measurement, not an expectation: every check prints the line it read, and a phase
# that cannot reach its precondition REFUSES to run rather than reporting a number about a
# run that did not happen.  An HP that could not be read is reported as unreadable and is
# NEVER counted as equal.
[CmdletBinding()]
param(
    [ValidateSet('lootdrop', 'soak30', 'repro', 'all')]
    [string]$Phase = 'all',
    [int]$BootSeconds = 300,
    [int]$SoakSeconds = 1800,
    [int]$LootRounds = 8,
    [int]$ReproSeconds = 900,
    [string]$Report = '',
    [switch]$ShippedDefaults
)

$ErrorActionPreference = 'Stop'
$here    = $PSScriptRoot
$repo    = Split-Path -Parent (Split-Path -Parent $here)
$gameDir = 'G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64'
$relay   = Join-Path (Split-Path -Parent $here) 'rust\target\release\qd-relay.exe'
$rigBase = 'G:\Freespace\Queens_Domain\_test'
$rigRoot = Join-Path $rigBase ('s21-' + $Phase)
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
        Crashes = Join-Path $root ("Saved_{0}\Crashes" -f $suffix)
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
function Get-Window([int]$i, [int]$markMs, [switch]$Fresh) {
    return @(Get-Since $i $markMs)
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
# The LANE'S OWN key, not a class: `near:drop:<8hex>:<index>` is the lever Session 18 said
# the next session needed (R129), and it is what makes this proof possible at all.
function Invoke-GoDrop([int]$i, [string]$hash, [int]$index, [int]$near = 400) {
    $script:goSeq++
    $yaw = ($script:goSeq * 37) % 360
    Set-Control $i 'qd.near' ([string]$near)
    Set-Control $i 'qd.tp' ('near:drop:' + $hash + ':' + $index + ',' + $yaw + '#' + (Get-Date -Format 'HHmmssfff'))
}
function Invoke-PressDrop([int]$i, [string]$hash, [int]$index) {
    Set-Control $i 'qd.act' ('near:drop:' + $hash + ':' + $index + '#' + (Get-Date -Format 'HHmmssfff'))
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
    $lv = @(Get-Matches $i 'level name: (\S+) from ')
    if ($lv.Count -gt 0 -and [string]$lv[-1] -match 'level name: (\S+) from ') { $out.World = $Matches[1] }
    $pin = @(Get-Matches $i 'rig: hard lever -> (\S+) streamed')
    if ($pin.Count -gt 0 -and [string]$pin[-1] -match 'rig: hard lever -> (\S+) streamed') { $out.Area = $Matches[1] }
    return $out
}

# ---------------------------------------------------------------------------
# The per-key enemy dump, parsed (Session 21: `why=` is the new field)
# ---------------------------------------------------------------------------
# A dump is triggered by REWRITING the control file (its content is the trigger), and the
# lines it prints are `enemykey <key> <state> local=<0|1> src=<n> hp=<v>|? [why=<reason>
# seen=<0|1> inworld=<0|1>] [mirrored=1] cls=<class>`.  Only the dump prints `enemykey`,
# so a mark-to-now window is exactly one dump -- which is why the soak can now sample it
# repeatedly instead of reading one end-of-run answer.
function Invoke-EnemyDump([int]$i, [string]$tag) {
    $mark = Get-Mark $i
    Set-Control $i 'qd.enemies' ('s21-' + $tag + '-' + (Get-Date -Format 'HHmmssfff'))
    Start-Sleep -Seconds 3
    $rows = @(Get-Since $i $mark | Where-Object { $_ -match 'enemykey [0-9A-Fa-f]{8} (ALIVE|DEAD)' })
    $out = @{}
    foreach ($ln in $rows) {
        $s = [string]$ln
        if ($s -notmatch 'enemykey ([0-9A-Fa-f]{8}) (ALIVE|DEAD)') { continue }
        $k = $Matches[1].ToUpperInvariant(); $st = $Matches[2]
        $hp = $null
        if ($s -match '\shp=([-0-9.]+)') { $hp = [double]$Matches[1] }
        $why = ''
        if ($s -match ' why=(\w+)') { $why = $Matches[1] }
        $seen = ''; if ($s -match ' seen=(\d)') { $seen = $Matches[1] }
        $iw = ''; if ($s -match ' inworld=(\d)') { $iw = $Matches[1] }
        $cls = ''; if ($s -match ' cls=(\S+)') { $cls = $Matches[1] }
        $out[$k] = [pscustomobject]@{
            Key = $k; State = $st; Hp = $hp; Why = $why; Seen = $seen; InWorld = $iw
            Cls = $cls; Line = $s.Trim()
        }
    }
    return $out
}

# Compare the shared live keys.  An unreadable HP is NOT a comparison: it is counted
# separately, with the reason the mod printed.  This is the rule Session 20's end-of-run
# table could not state, because the end-of-run dump was the only sample it had.
function Compare-SharedHp([hashtable]$dh, [hashtable]$dp, [string]$tag) {
    $out = [pscustomobject]@{
        Tag = $tag; AliveBoth = 0; Compared = 0; Unequal = 0
        Unreadable = 0; Whys = @{}; Mismatches = @()
    }
    foreach ($k in @($dh.Keys)) {
        if (-not $dp.ContainsKey($k)) { continue }
        if ($dh[$k].State -ne 'ALIVE' -or $dp[$k].State -ne 'ALIVE') { continue }
        $out.AliveBoth++
        $a = $dh[$k].Hp; $b = $dp[$k].Hp
        if ($null -eq $a -or $null -eq $b) {
            $out.Unreadable++
            $w = ''
            if ($null -eq $a -and $null -eq $b) { $w = "$($dh[$k].Why)/$($dp[$k].Why)" }
            elseif ($null -eq $a) { $w = $dh[$k].Why }
            else { $w = $dp[$k].Why }
            if (-not $w) { $w = '?' }
            if ($out.Whys.ContainsKey($w)) { $out.Whys[$w]++ } else { $out.Whys[$w] = 1 }
            continue
        }
        $out.Compared++
        if ([math]::Abs($a - $b) -ge 0.5) {
            $out.Unequal++
            $out.Mismatches += ("{0}: host={1} peer={2}" -f $k, $a, $b)
        }
    }
    return $out
}
function Format-Whys($whys) {
    if (-not $whys -or $whys.Count -eq 0) { return '-' }
    return (($whys.GetEnumerator() | Sort-Object Value -Descending |
             ForEach-Object { "$($_.Key)=$($_.Value)" }) -join ' ')
}

# ===========================================================================
# R129 -- the loot drop: taken on one machine, hidden on the other, and refused
# ===========================================================================
function Get-DropKeys([int]$i, [int]$mark) {
    $out = New-Object System.Collections.Generic.List[object]
    foreach ($ln in @(Get-Since $i $mark)) {
        $s = [string]$ln
        if ($s -notmatch 'loot: drop ([0-9A-Fa-f]{8})#(\d+)') { continue }
        $h = $Matches[1].ToUpperInvariant(); $ix = [int]$Matches[2]
        $have = $false
        foreach ($e in $out) { if ($e.Hash -eq $h -and $e.Index -eq $ix) { $have = $true; break } }
        if (-not $have) { $out.Add([pscustomobject]@{ Hash = $h; Index = $ix }) | Out-Null }
    }
    return $out
}

# Does THIS machine hold a live copy of that key?  The answer is the lane's own resolver:
# `rig: drop <key> resolved to a live actor` versus `... is already gone on this machine`
# versus `rig: no live loot-ledger drop matches`.  Standing 400 cm away so the probe cannot
# be mistaken for a pickup.
function Test-DropHeld([int]$i, [string]$hash, [int]$index) {
    $mark = Get-Mark $i
    Invoke-GoDrop $i $hash $index 400
    Start-Sleep -Seconds 3
    $held = @(Get-Since $i $mark | Where-Object { $_ -match ('rig: drop ' + $hash + '#' + $index + ' resolved to a live actor') })
    $gone = @(Get-Since $i $mark | Where-Object { $_ -match ('rig: drop ' + $hash + '#' + $index + ' is already gone') })
    return [pscustomobject]@{
        Held = ($held.Count -gt 0); Gone = ($gone.Count -gt 0)
        Line = $(if ($held.Count) { ([string]$held[-1]).Trim() } elseif ($gone.Count) { ([string]$gone[-1]).Trim() } else { '' })
    }
}

# Stand ON the drop (a pickup is an overlap, not a verb) and then run the game's own
# Interact input event on that exact actor as the fallback the interaction lever documents.
function Invoke-TakeDrop([int]$i, [string]$hash, [int]$index) {
    Invoke-GoDrop $i $hash $index 60
    Start-Sleep -Seconds 4
    Invoke-PressDrop $i $hash $index
    Say ("  act[$i] -> near:drop:{0}:{1} (stood 60 cm away, then the real Interact event)" -f $hash, $index)
}

function Find-SharedDrop([int]$H, [int]$P, $skip, [int]$mark, [int]$maxProbe = 6) {
    $keys = @(Get-DropKeys $H $mark) + @(Get-DropKeys $P $mark)
    $seen = @{}
    $probed = 0
    foreach ($k in $keys) {
        $id = "$($k.Hash)#$($k.Index)"
        if ($seen.ContainsKey($id)) { continue }
        $seen[$id] = $true
        if (@($skip) -contains $id) { continue }
        if ($probed -ge $maxProbe) { Note ("    (stopped after {0} probes; the remaining keys were not tested)" -f $probed); break }
        $probed++
        $rh = Test-DropHeld $H $k.Hash $k.Index
        if (-not $rh.Held) { Note ("    key {0}: the HOST does not hold it ({1})" -f $id, $(if ($rh.Line) { $rh.Line } else { 'no resolver line' })); continue }
        $rp = Test-DropHeld $P $k.Hash $k.Index
        if (-not $rp.Held) { Note ("    key {0}: the PEER does not hold it ({1})" -f $id, $(if ($rp.Line) { $rp.Line } else { 'no resolver line' })); continue }
        Note ("    key {0}: BOTH machines hold it" -f $id)
        return $k
    }
    return $null
}

# Roll more drops by killing: the drop is the dying enemy's own Blueprint's, so both
# machines roll it locally and hold a copy -- which is exactly the precondition hiding
# needs.
function Invoke-KillRounds([int]$H, [int]$P, [int]$rounds, [int]$mark) {
    for ($r = 1; $r -le $rounds; $r++) {
        Set-Control $H 'qd.hit' '400'
        Set-Control $P 'qd.hit' '400'
        Note ("  kill round {0}/{1}: hitting the nearest enemy on both machines" -f $r, $rounds)
        Start-Sleep -Seconds 5
        Set-Control $H 'qd.hit' $null
        Set-Control $P 'qd.hit' $null
        Invoke-Go $H 'enemy' 200
        Invoke-Go $P 'enemy' 200
        Start-Sleep -Seconds 4
        $all = @(Get-DropKeys $H $mark) + @(Get-DropKeys $P $mark)
        $uniq = @{}
        foreach ($k in $all) { $uniq["$($k.Hash)#$($k.Index)"] = $true }
        Note ("    after round {0}: {1} distinct drop key(s); loot: drop lines host={2} peer={3}; merged host={4} peer={5}" -f
              $r, $uniq.Count,
              @(Get-Since $H $mark | Where-Object { $_ -match 'loot: drop ' }).Count,
              @(Get-Since $P $mark | Where-Object { $_ -match 'loot: drop ' }).Count,
              @(Get-Since $H $mark | Where-Object { $_ -match 'loot: merged ' }).Count,
              @(Get-Since $P $mark | Where-Object { $_ -match 'loot: merged ' }).Count)
    }
}

function Invoke-LootDirection([int]$H, [int]$P, [int]$taker, [int]$other, [string]$step, [string]$dir, $skip, [int]$mark) {
    Step ("R129 {0} -- the {1} takes a drop both machines hold; the {2} must hide its copy" -f
          $dir, $(if ($taker -eq $H) { 'HOST' } else { 'PEER' }), $(if ($other -eq $H) { 'HOST' } else { 'PEER' })) `
         'The tombstone is the lane''s own key {source_hash, drop_index}; the lever that addresses it is qd.tp near:drop:<8hex>:<index>.'
    $key = Find-SharedDrop $H $P $skip $mark
    if (-not $key) {
        # One retry with fresh kills: the first pass may simply have rolled no drop that
        # both machines hold at the same moment.
        Note '  no shared drop yet; killing again and retrying once'
        Invoke-KillRounds $H $P 3 $mark
        $key = Find-SharedDrop $H $P $skip $mark
    }
    if (-not $key) {
        Verdict $step $H $false 'a drop BOTH machines hold was found' 'no shared drop key exists; hiding cannot be measured'
        return $null
    }
    $id = "$($key.Hash)#$($key.Index)"
    Note ("  the drop under test is {0}" -f $id)

    $mTake = Get-Mark $taker
    $mOther = Get-Mark $other
    Invoke-TakeDrop $taker $key.Hash $key.Index
    $pick = Wait-Since $taker ('loot: local pickup ' + $key.Hash + '#' + [string]$key.Index) $mTake 30
    Verdict $step $taker ($pick.Count -gt 0) 'the taker took the drop (its ledger tombstoned it)' $(if ($pick.Count) { ([string]$pick[-1]).Trim() } else { '' })
    if ($pick.Count -eq 0) {
        Show-Since $taker 'loot:|dev interact|rig: drop|rig: near' $mTake 20
        return $id
    }

    $hide = Wait-Since $other ('loot: hiding local drop ' + $key.Hash + '#' + [string]$key.Index) $mOther 40
    Verdict $step $other ($hide.Count -gt 0) 'the OTHER machine hid its own copy (the measured hiding half)' $(if ($hide.Count) { ([string]$hide[-1]).Trim() } else { '' })
    if ($hide.Count -eq 0) {
        Show-Since $other 'loot:|rig: drop|rig: near|dev interact' $mOther 20
    }
    # The hiding half must be a HIDE, not a disappearance that happened anyway: the same
    # machine still resolves the key and now says it is gone.
    $after = Test-DropHeld $other $key.Hash $key.Index
    Verdict $step $other $after.Gone 'the other machine''s copy is GONE to its own resolver' $after.Line

    # ---- taken twice? -----------------------------------------------------
    # The claim is not "the line is missing": a second take must be REFUSED, and the
    # ledger must still hold one tombstone, not two entries.
    $m2 = Get-Mark $other
    $before = @(Get-Since $other $m2 | Where-Object { $_ -match ('loot: local pickup ' + $key.Hash + '#' + [string]$key.Index) }).Count
    Invoke-TakeDrop $other $key.Hash $key.Index
    Start-Sleep -Seconds 8
    $after2 = @(Get-Since $other $m2 | Where-Object { $_ -match ('loot: local pickup ' + $key.Hash + '#' + [string]$key.Index) }).Count
    $refused = @(Get-Since $other $m2 | Where-Object { $_ -match ('rig: drop ' + $key.Hash + '#' + [string]$key.Index + ' is already gone') })
    Verdict $step $other (($after2 - $before) -eq 0) 'the other machine CANNOT take the drop a second time' ("new pickup lines=$($after2 - $before)")
    Verdict $step $other ($refused.Count -gt 0) 'the second attempt was refused by name (already gone)' $(if ($refused.Count) { ([string]$refused[-1]).Trim() } else { '' })
    # And the drop was not resurrected by the taker's own 1 Hz body.
    $m3 = Get-Mark $other
    Start-Sleep -Seconds 6
    $reoff = @(Get-Since $other $m3 | Where-Object { $_ -match ('loot: drop ' + $key.Hash + '#' + [string]$key.Index) })
    Verdict $step $other ($reoff.Count -eq 0) 'the tombstone was not resurrected by the next state body' ("re-offer lines=$($reoff.Count)")
    return $id
}

function Invoke-LootDrop([int]$H, [int]$P) {
    Step 'R129 -- loot hiding, both directions' `
         'A drop both machines hold; one takes it; the other must hide its copy and refuse a second take.'
    # Anchor the pair on enemies and kill until drops exist.  The drop is rolled by the
    # dying enemy's own Blueprint, so both machines roll it locally and hold a copy --
    # which is exactly the precondition hiding needs.
    $mark = Get-Mark $H
    Invoke-Go $H 'enemy' 200
    Invoke-Go $P 'enemy' 200
    Start-Sleep -Seconds 8
    Invoke-KillRounds $H $P 3 $mark
    $all = @(Get-DropKeys $H $mark) + @(Get-DropKeys $P $mark)
    $uniq = @{}
    foreach ($k in $all) { $uniq["$($k.Hash)#$($k.Index)"] = $true }
    Note ("  drops rolled this run: {0} distinct key(s): {1}" -f $uniq.Count, (($uniq.Keys | Sort-Object) -join ' '))
    Verdict 'r129-roll' $H ($uniq.Count -gt 0) 'killing an enemy rolled a drop (the lane observed a key)' ("keys=$($uniq.Count)")
    if ($uniq.Count -eq 0) { return }

    $used = @()
    $id1 = Invoke-LootDirection $H $P $H $P 'r129-host' 'direction 1 (HOST takes)' $used $mark
    if ($id1) { $used += $id1 }
    $id2 = Invoke-LootDirection $H $P $P $H 'r129-peer' 'direction 2 (PEER takes)' $used $mark
    if ($id2) { $used += $id2 }
    Note ("  drops exercised: {0}" -f ($used -join ' '))
}

# ===========================================================================
# The soak body: fight, move, and SAMPLE the per-key dump through the run
# ===========================================================================
$script:hpSamples = New-Object System.Collections.Generic.List[object]

function Invoke-SoakSegment([int]$H, [int]$P, [string]$label, [int]$seconds, [string]$anchor, [int]$sampleEvery = 75) {
    $start = Get-Date
    $end = $start.AddSeconds($seconds)
    $m0 = Get-Mark $H
    $n = 0
    $died = $false
    $lastSample = Get-Date
    $anchors = @('enemy', 'enemy', 'chest', 'enemy', 'npc')
    while ((Get-Date) -lt $end) {
        if (-not (Test-Alive $H) -or -not (Test-Alive $P)) { $died = $true; break }
        $n++
        Set-Control $H 'qd.hit' ([string](200 + ($n % 3) * 100))
        Set-Control $P 'qd.hit' ([string](200 + ($n % 3) * 100))
        # MOVEMENT: a re-anchor every other iteration, cycling the kinds of thing the rig
        # can walk to, so the run is not a stationary turret test.
        if (($n % 2) -eq 0) {
            $kind = $anchors[[int](($n / 2) % $anchors.Count)]
            Invoke-Go $H $kind 200
            Invoke-Go $P $kind 200
        }
        if (((Get-Date) - $lastSample).TotalSeconds -ge $sampleEvery) {
            $lastSample = Get-Date
            $dh = Invoke-EnemyDump $H ($label + '-h-' + $n)
            $dp = Invoke-EnemyDump $P ($label + '-p-' + $n)
            $cmp = Compare-SharedHp $dh $dp ("{0} t+{1}s" -f $label, [int]((Get-Date) - $start).TotalSeconds)
            $script:hpSamples.Add($cmp) | Out-Null
            Note ("    hp sample t+{0,4}s: shared-alive={1} compared={2} unequal={3} unreadable={4} why={5}" -f
                  $cmp.Tag, [int]((Get-Date) - $start).TotalSeconds, $cmp.AliveBoth, $cmp.Compared,
                  $cmp.Unequal, $cmp.Unreadable, (Format-Whys $cmp.Whys))
            foreach ($mm in ($cmp.Mismatches | Select-Object -First 4)) { Note ("      HP MISMATCH " + $mm) }
        }
        Start-Sleep -Seconds 3
    }
    $elapsed = [int]((Get-Date) - $start).TotalSeconds
    Set-Control $H 'qd.hit' $null
    Set-Control $P 'qd.hit' $null
    Note ("  segment '{0}': {1} s of {2} s, iterations={3}" -f $label, $elapsed, $seconds, $n)
    Verdict ("seg-" + $label) $H (-not $died) 'both processes survived the segment' $(if ($died) { "a process died after ${elapsed}s" } else { "${elapsed}s" })
    if ($anchor) {
        Invoke-Go $H $anchor 400
        Invoke-Go $P $anchor 400
        Start-Sleep -Seconds 8
    }
}

function Enter-Area([int]$H, [int]$P, [string]$area, [string]$anchor, [string]$step) {
    Step "$step -- the HOST enters $area; the PEER must follow" `
         'R147: entering an area is a streamed sublevel, so the signal is TP_AREA on the travel lane and a local refusal is a named error.'
    $mH = Get-Mark $H
    $mP = Get-Mark $P
    Invoke-Hard $H $area
    [void](Check $step $H ('rig: hard lever -> ' + [regex]::Escape($area) + ' streamed=1') 90 'the HOST streamed the area locally' $mH)
    $hostOk = [bool]$script:verdicts[$script:verdicts.Count - 1].Ok
    if (-not $hostOk) {
        Verdict $step $P $false 'the PEER follows the host into the area' 'the host never entered it'
        return $false
    }
    $follow = @(Wait-Since $P ('travel: entered the partner.s area ' + [regex]::Escape($area) + '|travel: area ' + [regex]::Escape($area) + ' is already pinned') $mP 90)
    Verdict $step $P ($follow.Count -gt 0) 'the PEER followed the host into the area' $(if ($follow.Count) { ([string]$follow[-1]).Trim() } else { '' })
    if ($follow.Count -eq 0) {
        Show-Since $P 'travel:|level union|rig: hard' $mP 8
        return $false
    }
    [void](Check $step $H ('rig: level engine state ' + [regex]::Escape($area) + ' LoadedLevel=1 actuallyVisible=1') 90 'the area is LOADED and SHOWN on the host' $mH)
    [void](Check $step $P ('rig: level engine state ' + [regex]::Escape($area) + ' LoadedLevel=1 actuallyVisible=1') 90 'the area is LOADED and SHOWN on the peer' $mP)
    Start-Sleep -Seconds 6
    if ($anchor) {
        Invoke-Go $H $anchor 400
        Invoke-Go $P $anchor 400
        Start-Sleep -Seconds 8
    }
    return $true
}

function Invoke-HardTravel([int]$H, [int]$P, [string]$map, [string]$step) {
    Step "$step -- HARD travel to $map, and the partner must land in it too" `
         'G6: the sender announces HARD_BEGIN, the partner loads the same world through GameInstance.LoadMapWithLoadingScreen, and the host releases the pair only when both arrived.'
    $mH = Get-Mark $H
    $mP = Get-Mark $P
    Invoke-Hard $H $map
    $ann = @(Wait-Since $H ('travel: HARD transition to ' + [regex]::Escape($map)) $mH 90)
    Verdict $step $H ($ann.Count -gt 0) 'the host announced the hard transition' $(if ($ann.Count) { ([string]$ann[-1]).Trim() } else { '' })
    $loaded = @(Wait-Since $P ('travel: hard transition: loading ' + [regex]::Escape($map)) $mP 120)
    Verdict $step $P ($loaded.Count -gt 0) 'the peer loaded the same world' $(if ($loaded.Count) { ([string]$loaded[-1]).Trim() } else { '' })
    $both = @(Wait-Since $H ('travel: both machines arrived in ' + [regex]::Escape($map)) $mH 120)
    Verdict $step $H ($both.Count -gt 0) 'both machines arrived (the host released the pair)' $(if ($both.Count) { ([string]$both[-1]).Trim() } else { '' })
    if ($both.Count -eq 0) { Show-Since $H 'travel:|arrive:' $mH 12; Show-Since $P 'travel:|arrive:' $mP 12 }
    Start-Sleep -Seconds 12
    $wH = Get-WorldState $H; $wP = Get-WorldState $P
    Verdict $step $P ($wH.World -eq $map -and $wP.World -eq $map) 'both machines report the new world' ("host=$($wH.World) peer=$($wP.World)")
    return ($wH.World -eq $map -and $wP.World -eq $map)
}

# ---------------------------------------------------------------------------
# The end-of-run checks: kill sets, HP, world, area, faults and ERR
# ---------------------------------------------------------------------------
function Invoke-EndChecks([int]$H, [int]$P, [string]$step, [int]$m0) {
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
    Verdict $step $P ($wH.Area -eq $wP.Area) 'both machines hold the SAME area' ("host='$($wH.Area)' peer='$($wP.Area)'")

    if (-not (Test-Alive $H) -or -not (Test-Alive $P)) {
        Note '  (kill-set comparison skipped: a process is gone, so its dump cannot answer)'
        Verdict $step $H $false 'the per-GUID kill sets DIFF TO ZERO' 'a process died; the dump is unreadable'
        return
    }
    $dh = Invoke-EnemyDump $H 'end-h'
    $dp = Invoke-EnemyDump $P 'end-p'
    $deadH = @($dh.Keys | Where-Object { $dh[$_].State -eq 'DEAD' })
    $deadP = @($dp.Keys | Where-Object { $dp[$_].State -eq 'DEAD' })
    $onlyH = @($deadH | Where-Object { -not $dp.ContainsKey($_) -or $dp[$_].State -ne 'DEAD' })
    $onlyP = @($deadP | Where-Object { -not $dh.ContainsKey($_) -or $dh[$_].State -ne 'DEAD' })
    Note ("  host keys={0} (dead {1}); peer keys={2} (dead {3})" -f $dh.Count, $deadH.Count, $dp.Count, $deadP.Count)
    Note ("  dead only on the host: {0} -> {1}" -f $onlyH.Count, ($onlyH -join ','))
    Note ("  dead only on the peer: {0} -> {1}" -f $onlyP.Count, ($onlyP -join ','))
    foreach ($k in $onlyH) { Note ("    host-only {0}: {1}" -f $k, $dh[$k].Line) }
    foreach ($k in $onlyP) { Note ("    peer-only {0}: {1}" -f $k, $dp[$k].Line) }
    Verdict $step $H ($onlyH.Count -eq 0 -and $onlyP.Count -eq 0) 'the per-GUID kill sets DIFF TO ZERO' ("host-dead=$($deadH.Count) peer-dead=$($deadP.Count) diff=$($onlyH.Count + $onlyP.Count)")

    $end = Compare-SharedHp $dh $dp 'end'
    Note ("  shared live keys at the end: {0}; comparable={1} unequal={2} unreadable={3} why={4}" -f
          $end.AliveBoth, $end.Compared, $end.Unequal, $end.Unreadable, (Format-Whys $end.Whys))
    foreach ($mm in ($end.Mismatches | Select-Object -First 8)) { Note ("    HP MISMATCH " + $mm) }
    $script:hpSamples.Add($end) | Out-Null

    $cmpTotal = 0; $uneqTotal = 0; $unreadTotal = 0; $aliveTotal = 0
    $whys = @{}
    foreach ($s in $script:hpSamples) {
        $cmpTotal += $s.Compared; $uneqTotal += $s.Unequal
        $unreadTotal += $s.Unreadable; $aliveTotal += $s.AliveBoth
        foreach ($k in @($s.Whys.Keys)) {
            if ($whys.ContainsKey($k)) { $whys[$k] += $s.Whys[$k] } else { $whys[$k] = $s.Whys[$k] }
        }
    }
    Note ("  HP ACROSS THE RUN: {0} sample(s); shared-alive {1}; COMPARED {2}; unequal {3}; unreadable {4}; why {5}" -f
          $script:hpSamples.Count, $aliveTotal, $cmpTotal, $uneqTotal, $unreadTotal, (Format-Whys $whys))
    Verdict $step $P ($cmpTotal -gt 0) 'shared HP was actually MEASURED during the run' ("compared=$cmpTotal of $aliveTotal shared-alive observations, $unreadTotal unreadable (never counted as equal)")
    Verdict $step $P ($cmpTotal -gt 0 -and $uneqTotal -eq 0) 'the HP of shared enemies is EQUAL wherever it was readable' ("compared=$cmpTotal unequal=$uneqTotal")

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
        foreach ($e in ($fault | Select-Object -Last 4)) { Note ("  [$i] " + ([string]$e).Trim()) }
        foreach ($e in ($err | Select-Object -First 4)) { Note ("  [$i] " + ([string]$e).Trim()) }
        Verdict $step $i ($fault.Count -eq 0) 'no FAULT lines during the run (the crash claim)' ("faults=$($fault.Count)")
        Verdict $step $i ($err.Count -eq 0) 'no ERR lines during the run' ("err=$($err.Count)")
        $cr = Get-CrashFolders $i
        if ($cr.Count -gt 0) {
            Verdict $step $i $false 'no crash dump was written' ("crash folders: " + ($cr -join ' '))
            foreach ($f in $cr) { Show-CrashSummary $f }
        } else {
            Verdict $step $i $true 'no crash dump was written' ''
        }
    }
}

function Get-CrashFolders([int]$i) {
    $base = $inst[$i].Crashes
    if (-not (Test-Path -LiteralPath $base)) { return @() }
    return @(Get-ChildItem -LiteralPath $base -Directory -ErrorAction SilentlyContinue |
             Where-Object { $_.LastWriteTime -gt (Get-Date).AddHours(-3) } |
             ForEach-Object { $_.FullName })
}
function Show-CrashSummary([string]$folder) {
    $x = Join-Path $folder 'CrashContext.runtime-xml'
    if (Test-Path -LiteralPath $x) {
        $t = Get-Content -LiteralPath $x -Raw
        if ($t -match '<FaultingModule>([^<]*)</FaultingModule>') { Note ("    crash module: " + $Matches[1]) }
        if ($t -match '<ErrorMessage>([^<]*)</ErrorMessage>') { Note ("    crash error: " + $Matches[1]) }
        if ($t -match '<ExceptionAddress>([^<]*)</ExceptionAddress>') { Note ("    crash address: " + $Matches[1]) }
    }
}

# ===========================================================================
# Soak 30 -- one 1800 s run: hub, two TP_AREA entries, one hard travel, end state
# ===========================================================================
function Invoke-Soak30([int]$H, [int]$P) {
    Step ("SOAK-30 -- {0} s on the documented play set, sampled HP throughout" -f $SoakSeconds) `
         'Sessions 18-20 could only compare HP at the END of a run, which is when most shared keys are dormant. This run samples the dump through it.'
    $m0 = Get-Mark $H
    $swm = @(Read-LogLines $inst[$H].Log | Where-Object { $_ -match 'switches: ' })
    $swLine = if ($swm.Count -gt 0) { ([string]$swm[-1]).Trim() } else { '' }
    if ($swLine) { Note ("  [$H] " + $swLine) }
    Verdict 'soak30' $H ($swLine -match 'enemy=1') 'the switch set under test is the documented PLAY set' $swLine

    $wH0 = Get-WorldState $H; $wP0 = Get-WorldState $P
    Note ("  world at start: host='{0}' area='{1}' peer='{2}' area='{3}'" -f $wH0.World, $wH0.Area, $wP0.World, $wP0.Area)

    # The budget is the soak itself.  Transitions (area entry, hard travel) cost extra, and
    # that is why the total wall clock is longer than $SoakSeconds.
    $hub    = [int]($SoakSeconds * 0.333)
    $area1  = [int]($SoakSeconds * 0.167)
    $area2  = [int]($SoakSeconds * 0.167)
    $sipeha = [int]($SoakSeconds * 0.100)
    $back   = $SoakSeconds - $hub - $area1 - $area2 - $sipeha

    Invoke-SoakSegment $H $P 'hub' $hub 'enemy' 75
    $ok1 = Enter-Area $H $P '780_SZ_Depths' 'enemy' 'area1'
    if ($ok1) { Invoke-SoakSegment $H $P 'area780' $area1 'enemy' 75 }
    $ok2 = Enter-Area $H $P '770_SZ_Swamp' 'enemy' 'area2'
    if ($ok2) { Invoke-SoakSegment $H $P 'area770' $area2 'enemy' 75 }
    # The hard travel is the one world switch; the return to the hub is what puts the pair
    # back on the play set for the end-of-run measurement.
    $ok3 = Invoke-HardTravel $H $P '900_SipehaWorld' 'hard-out'
    if ($ok3) { Invoke-SoakSegment $H $P 'sipeha' $sipeha 'enemy' 60 }
    $ok4 = Invoke-HardTravel $H $P 'Ruins_Level_00' 'hard-back'
    if (-not $ok4) { Note '  (the return load did not confirm; the end checks run wherever the pair is)' }
    Invoke-SoakSegment $H $P 'hub-final' $back 'enemy' 60

    Invoke-EndChecks $H $P 'soak30' $m0
}

# ===========================================================================
# R138 / R140 -- try to reproduce a fault under the soak's own conditions
# ===========================================================================
function Invoke-Repro([int]$H, [int]$P) {
    Step ("R138/R140 -- {0} s of stream churn looking for a fault" -f $ReproSeconds) `
         'R140 reproduced ~250-260 s after process start, during a large area stream-in, right after a burst of "enemy death detected ... (actor gone)". R138 has not reproduced at all. This run reproduces those conditions deliberately: repeated area entries with fighting between them.'
    $m0 = Get-Mark $H
    $areas = @('780_SZ_Depths', '770_SZ_Swamp', '760_SZ_AUT', '790_SG', '775_JigekiTemple')
    $start = Get-Date
    $idx = 0
    while (((Get-Date) - $start).TotalSeconds -lt $ReproSeconds) {
        if (-not (Test-Alive $H) -or -not (Test-Alive $P)) { break }
        $area = $areas[$idx % $areas.Count]
        $idx++
        Say ("  churn {0}: entering {1}" -f $idx, $area)
        $mark = Get-Mark $H
        Invoke-Hard $H $area
        Start-Sleep -Seconds 8
        # Fight hard right after the stream-in: that is the frame R140's dump names.
        Set-Control $H 'qd.hit' '400'
        Set-Control $P 'qd.hit' '400'
        Invoke-Go $H 'enemy' 200
        Invoke-Go $P 'enemy' 200
        Start-Sleep -Seconds 35
        Set-Control $H 'qd.hit' $null
        Set-Control $P 'qd.hit' $null
        $gone = @(Get-Since $H $mark | Where-Object { $_ -match 'enemy death detected.*actor gone' })
        $fault = @(Get-Since $H $mark | Where-Object { $_ -match 'ACCESS_VIOLATION|EXCEPTION|fatal|\bfault' })
        Note ("    churn {0}: area lines={1} 'actor gone' deaths={2} faults={3}" -f
              $idx, @(Get-Since $H $mark | Where-Object { $_ -match ('rig: hard lever -> ' + [regex]::Escape($area)) }).Count,
              $gone.Count, $fault.Count)
    }
    $alive = (Test-Alive $H) -and (Test-Alive $P)
    Verdict 'repro' $H $alive 'both processes survived the churn run' ''
    foreach ($i in @($H, $P)) {
        $fault = @(Get-Since $i $m0 | Where-Object { $_ -match 'ACCESS_VIOLATION|EXCEPTION|fatal|\bfault' })
        $err = @(Get-Since $i $m0 | Where-Object { $_ -match ' ERR ' })
        Note ("  [{0}] FAULT lines={1} ERR lines={2}" -f $i, $fault.Count, $err.Count)
        foreach ($e in ($fault | Select-Object -Last 6)) { Note ("  [$i] " + ([string]$e).Trim()) }
        $cr = Get-CrashFolders $i
        if ($cr.Count -gt 0) {
            Note ("  [{0}] CRASH DUMPS: {1}" -f $i, ($cr -join ' '))
            foreach ($f in $cr) { Show-CrashSummary $f }
        } else { Note ("  [{0}] no crash dump" -f $i) }
    }
}

# ===========================================================================
# main
# ===========================================================================
Say ("session21 start: phase={0} switches={1} soak={2}s repro={3}s" -f $Phase,
     $(if ($ShippedDefaults) { 'SHIPPED' } else { 'DOCUMENTED PLAY' }), $SoakSeconds, $ReproSeconds)
Stop-All
Seed-FreshPair
Start-Boot $Phase
$H = $script:H
$P = $script:P

if ($Phase -eq 'lootdrop' -or $Phase -eq 'all') {
    Invoke-LootDrop $H $P
}

if ($Phase -eq 'soak30' -or $Phase -eq 'all') {
    Invoke-Soak30 $H $P
}

if ($Phase -eq 'repro' -or $Phase -eq 'all') {
    Invoke-Repro $H $P
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
    Note ("  {0,-12} inst{1}  {2,-6}  {3}  {4}" -f $v.Step, $v.Inst, $(if ($v.Ok) { 'PASS' } else { 'FAIL' }), $v.What, $v.Line)
}
$pass = @($script:verdicts | Where-Object { $_.Ok }).Count
$fail = @($script:verdicts | Where-Object { -not $_.Ok }).Count
Note ("=== {0} PASS, {1} FAIL ===" -f $pass, $fail)
$script:lines | Set-Content -LiteralPath $Report
