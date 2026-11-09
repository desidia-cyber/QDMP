# session16.ps1 -- Session 16 of the live two-instance run, in ONE process per phase.
#
# WHAT THIS SESSION PROVES
# ------------------------
# Session 15 closed the content half of R109: 780_SZ_Depths' own actors arrive when the
# area's level list is pushed.  It then hit a wall with three names on it:
#
#   * the Green Dragon (`Dragon_Green_BP_C`, key 17181166) exists only on the machine whose
#     spawner activated, so the partner "holds no copy of this GUID" and can neither see nor
#     fight it;
#   * `TryApplyDamage` was called on that class and moved nothing (Session 15 read that as
#     "the class has no damage entry"), so two players could not reduce one HP;
#   * the boss bar never appeared, because the predicates it was asked for are not on the
#     enemy classes in this build.
#
# Session 16 answers all three by construction rather than by asking the same questions:
#
#   item1  the MIRROR.  The owner sends the enemy's class (K_ENEMY_SPAWN 0x2B), the machine
#          with no actor constructs a copy of it, AI off, driven by the owner's rows, never
#          persisted -- and the dragon becomes visible on BOTH machines.
#   boss   the one shared HP: the partner's hits on its mirrored copy reach the owner along
#          the existing enemy-hit intent and lower the ONE number both machines report; the
#          boss bar appears on both (the bar is raised through the game's own
#          `ActivateBossHPBar`, and boss membership now comes from the content's own
#          `EnemyData.Boss`); the death is recorded on both and the copy is removed.
#   cut    the joint resume: the sequence is ENDED on purpose (`Stop`) instead of waiting
#          out its length, and both machines are proven to leave the held state.
#
# ONE SCRIPT PER SCENARIO: -Phase item1|boss|cut|all, one boot per phase.
[CmdletBinding()]
param(
    [ValidateSet('item1', 'boss', 'cut', 'all')]
    [string]$Phase = 'all',
    [int]$BootSeconds = 300,
    [string]$Report = ''
)

$ErrorActionPreference = 'Stop'
$here    = $PSScriptRoot
$repo    = Split-Path -Parent (Split-Path -Parent $here)
$gameDir = 'G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64'
$relay   = Join-Path (Split-Path -Parent $here) 'rust\target\release\qd-relay.exe'
$rigBase = 'G:\Freespace\Queens_Domain\_test'
# ONE INSTANCE ROOT PER PHASE.  The instances are shared mutable state -- each run seeds both
# save directories and drives both control files -- so two runs at once corrupt each other,
# and the symptom is a run full of "INSTANCE DIED" that was in fact healthy.  A phase owns its
# own `instN` directories, which is also what lets a finished phase's logs be read back.
$rigRoot = Join-Path $rigBase ('s16-' + $Phase)
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
        if (-not (Get-Process -Id $script:pids[$i] -ErrorAction SilentlyContinue)) { return $false }
        $m = Get-Matches $i 'scan: local at|scan: health'
        if ($m.Count -gt 0) { return $true }
        Start-Sleep -Milliseconds 500
    }
    return $false
}
function Set-Control([int]$i, [string]$leaf, $value) {
    $p = Join-Path -Path ([string]$inst[$i].Root) -ChildPath ([string]$leaf)
    if ($null -eq $value) { Remove-Item -LiteralPath $p -ErrorAction SilentlyContinue }
    else { Set-Content -LiteralPath $p -Value ([string]$value) -NoNewline }
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
    Write-Host $line
    $script:lines.Add($line) | Out-Null
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
    Set-Control $i 'qd.hard' $map
    Say "  hard[$i] -> $map"
}
function Invoke-BossHit([int]$i, [string]$key, [int]$amount) {
    # The lever ignores a repeated string, so alternate a harmless suffix that the parser
    # stops at (a non-hex character) to make each hit a new instruction.
    Set-Control $i 'qd.hitguid' ($key + ':' + $amount + '#' + (Get-Date -Format 'HHmmssfff'))
    Say "  hitguid[$i] -> $key (amount $amount)"
}
function Invoke-Dump([int]$i, [string]$tag) {
    Set-Control $i 'qd.enemies' $tag
    Start-Sleep -Seconds 3
}
function Get-HpByKey([int]$i, [int]$markMs, [string]$key) {
    $rows = @(Get-Since $i $markMs | Where-Object { $_ -match ('enemykey ' + $key + ' ') })
    if ($rows.Count -eq 0) { return $null }
    $last = [string]$rows[-1]
    if ($last -match 'hp=([-0-9.]+)') { return [double]$Matches[1] }
    return $null
}
function Get-SinceKey([int]$i, [int]$markMs, [string]$key) {
    return @(Get-Since $i $markMs | Where-Object { $_ -match ('enemykey ' + $key + ' ') })
}
function Get-DeadByKey([int]$i, [int]$markMs, [string]$key) {
    $rows = Get-SinceKey $i $markMs $key
    if ($rows.Count -eq 0) { return $false }
    return ([string]$rows[-1] -match ' DEAD')
}
function Get-MirroredByKey([int]$i, [int]$markMs, [string]$key) {
    $rows = Get-SinceKey $i $markMs $key
    if ($rows.Count -eq 0) { return $false }
    # ANY dump of this key in the window that says mirrored=1.  Explicitly a bool: `Verdict`
    # binds Ok as [bool], and an ARRAY of matching lines is truthy but binds unpredictably
    # (Session 14's harness bug).
    foreach ($r in $rows) { if ([string]$r -match 'mirrored=1') { return $true } }
    return $false
}function Get-DragonKey([int]$i, [int]$markMs) {
    $rows = @(Get-Since $i $markMs | Where-Object { $_ -match 'scan: kind enemy Dragon_Green_BP_C guid=([0-9A-Fa-f]{8})' })
    if ($rows.Count -eq 0) { return '' }
    if ([string]$rows[-1] -match 'guid=([0-9A-Fa-f]{8})') { return $Matches[1].ToUpperInvariant() }
    return ''
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
    Say "installed $built -> $dst"

    $ini = Join-Path $gameDir 'qdcoop.ini'
    # MirrorSpawn is NOT written here: Session 16 proved it, so it is the compiled default
    # (qdconfig.h).  Leaving it out of the ini means these runs exercise the shipped default
    # rather than a rig-only setting -- which is what a real pair of players gets.
    $lines = @('[Local]', 'Enabled=1', 'LogLevel=3', '', '[Net]',
               'RelayHost=127.0.0.1', 'RelayPort=47800', '', '[Rules]',
               'FlagSync=1', 'InventorySync=1', 'ActorStateSync=1', 'EnemySync=1',
               'PuppetSync=1', 'PlayerBody=1', 'DialogueSync=1', 'StreamUnion=1',
               'TravelSync=1', 'CutsceneSync=1', 'MoverSync=1', 'LootSync=1',
               'SuspendGuard=1', 'ObjectSync=1', 'SuppressMenuPause=1', 'HostAuthoritative=1',
               '', '[Tuning]', 'PlayerHz=30', 'EnemyRowsPerTick=20', 'SpawnAttemptsMax=5')
    $lines | Set-Content -LiteralPath $ini
    Say "wrote $ini (every write path on, LogLevel=3; MirrorSpawn is the compiled default)"
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
                          'qd.enemykill','qd.hitguid') {
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
            if ($m.Count -gt 0) {
                $line = [string]$m[-1]
                $r = if ($line -match 'role=HOST') { 'HOST' } elseif ($line -match 'role=PEER') { 'PEER' } else { '' }
                if ($r) { $roles[$i] = $r }
            }
        }
        if ($roles.Count -eq 2) { break }
        Start-Sleep -Seconds 1
    }
    foreach ($i in 1, 2) {
        if ($roles.ContainsKey($i)) {
            $m = Get-Matches $i 'INFO session established: role=(HOST|PEER)'
            Say "inst$i role=$($roles[$i])  <- $($m[-1])"
        } else { Say "inst$i NEVER ESTABLISHED A SESSION" }
    }
    $hostI = @($roles.Keys | Where-Object { $roles[$_] -eq 'HOST' } | Select-Object -First 1)
    $peerI = @($roles.Keys | Where-Object { $roles[$_] -eq 'PEER' } | Select-Object -First 1)
    if ($hostI.Count -eq 0 -or $peerI.Count -eq 0) {
        foreach ($i in 1, 2) {
            $a = @(Get-RunLines $i)
            $last = if ($a.Count) { [string]$a[-1] } else { '(none)' }
            Say "inst$i log lines=$($a.Count) last=$last"
        }
        Say 'REFUSED: no host/peer pair; see the logs'
        Stop-Boot
        $script:lines | Set-Content -LiteralPath $Report
        exit 2
    }
    $script:H = [int]$hostI[0]; $script:P = [int]$peerI[0]
    Say "host=inst$($script:H) peer=inst$($script:P)"

    Say "waiting for gameplay on both (up to $BootSeconds s)"
    foreach ($i in 1, 2) {
        $ok = Wait-Gameplay $i $BootSeconds
        Say "inst${i} gameplay=$ok"
    }
}
function Stop-Boot {
    foreach ($i in 1, 2) {
        if ($script:pids -and $script:pids.ContainsKey($i)) { Stop-Process -Id $script:pids[$i] -Force -ErrorAction SilentlyContinue }
    }
    if ($script:relayProc) { Stop-Process -Id $script:relayProc.Id -Force -ErrorAction SilentlyContinue }
    Start-Sleep -Seconds 1
    Say 'stopped both instances and the relay'
}

# ===========================================================================
# Shared: get both players into 780_SZ_Depths with the area's actors present
# ===========================================================================
function Enter-780([int]$H, [int]$P, [string]$step) {
    Step "$step -- both players into 780_SZ_Depths through the game's own level list"
    $mH = Get-Mark $H
    $mP = Get-Mark $P
    Invoke-Hard $H '780_SZ_Depths'
    Invoke-Hard $P '780_SZ_Depths'
    Start-Sleep -Seconds 12
    [void](Check $step $H "level list 'SZ_Depths' added to the manager's stack" 60 'the host pushed the level list that carries 780' $mH)
    [void](Check $step $P "level list 'SZ_Depths' added to the manager's stack" 60 'the peer pushed the same list' $mP)
    Start-Sleep -Seconds 12
    Invoke-Go $H 'GreenDragon_Spawner' 400
    Invoke-Go $P 'GreenDragon_Spawner' 400
    Start-Sleep -Seconds 10
    [void](Check $step $H 'dev teleport near .GreenDragon_Spawner. -> GreenDragon_Spawner at' 40 'the host stands at the spawner' $mH)
    [void](Check $step $P 'dev teleport near .GreenDragon_Spawner. -> GreenDragon_Spawner at' 40 'the peer stands at the same spawner' $mP)
    $pH = Get-LastPos $H $mH
    $pP = Get-LastPos $P $mP
    Verdict $step $H ([bool]$pH) 'the host has a pawn in 780' $(if ($pH) { ("{0:N0},{1:N0},{2:N0}" -f $pH[0], $pH[1], $pH[2]) } else { '' })
    Verdict $step $P ([bool]$pP) 'the peer has a pawn in 780' $(if ($pP) { ("{0:N0},{1:N0},{2:N0}" -f $pP[0], $pP[1], $pP[2]) } else { '' })
}

# ===========================================================================
# ITEM 1 -- the dragon becomes visible on BOTH machines (the mirror)
# ===========================================================================
function Invoke-Mirror([int]$H, [int]$P, [string]$step) {
    Step 'ITEM 1 -- K_ENEMY_SPAWN: the machine that never spawned it constructs the enemy'
    Enter-780 $H $P $step
    # The dragon materialises at the spawner once a player is inside its activation range,
    # and the AREA itself arrives on the level manager's own schedule -- measured Session 16
    # first run: the dragon appeared 3.5 minutes after the teleport, well past a fixed
    # window.  So this polls for the owner to hold it, re-teleporting both players to the
    # spawner, rather than assuming one wait is enough.
    $key = ''
    $deadline = (Get-Date).AddSeconds(240)
    $tries = 0
    while ((Get-Date) -lt $deadline -and -not $key) {
        $tries++
        Invoke-Go $H 'GreenDragon_Spawner' 140
        Invoke-Go $P 'GreenDragon_Spawner' 140
        Start-Sleep -Seconds 20
        $mK = Get-Mark $H
        Set-Control $H 'qd.enemies' ('probe' + $tries)
        Start-Sleep -Seconds 3
        $key = Get-DragonKey $H (Get-Mark $H - 600000)
        Note ("  poll {0}: dragon key on the owner = '{1}'" -f $tries, $key)
    }
    if (-not $key) {
        foreach ($cand in '17181166') {
            $rows = @(Get-Since $H 0 | Where-Object { $_ -match ('enemykey ' + $cand + ' ') })
            if ($rows.Count) { $key = $cand }
        }
    }
    if (-not $key) { $key = '17181166'; Note '  falling back to the key the session record names' }
    Note ("  dragon key under test: '{0}'" -f $key)

    # The describe may already have gone out during the polling above (it is sent once per
    # key, on change), so the check scans the WHOLE run rather than a window that starts
    # after it.  Measured: the first version's window opened after the line had been written
    # and reported "never published" about a body the peer had received.
    $mH = 0
    $mP = 0
    Start-Sleep -Seconds 6
    $descH = @(Get-Since $H 0 | Where-Object { $_ -match ('enemy spawn: described guid=' + $key) })
    $conP  = @(Get-Since $P 0 | Where-Object { $_ -match ('enemy spawn: constructed guid=' + $key) })
    Verdict $step $H ($descH.Count -gt 0) 'the owner published the missing class (K_ENEMY_SPAWN)' $(if ($descH.Count) { ([string]$descH[-1]).Trim() } else { '' })
    if ($conP.Count -eq 0) {
        # Give a queued body the construction interval to be drained before judging it.
        $deadline2 = (Get-Date).AddSeconds(30)
        while ((Get-Date) -lt $deadline2) {
            Start-Sleep -Seconds 3
            $conP = @(Get-Since $P 0 | Where-Object { $_ -match ('enemy spawn: constructed guid=' + $key) })
            if ($conP.Count -gt 0) { break }
        }
    }
    Verdict $step $P ($conP.Count -gt 0) 'the machine with no actor CONSTRUCTED the copy' $(if ($conP.Count) { ([string]$conP[-1]).Trim() } else { '' })

    $dH = Get-Mark $H; $dP = Get-Mark $P
    Invoke-Dump $H 'mirror-h'
    Invoke-Dump $P 'mirror-p'
    $rowH = Get-SinceKey $H $dH $key
    $rowP = Get-SinceKey $P $dP $key
    Note ("  host enemykey: {0}" -f ($(if ($rowH.Count) { ([string]$rowH[-1]).Trim() } else { '(none)' })))
    Note ("  peer enemykey: {0}" -f ($(if ($rowP.Count) { ([string]$rowP[-1]).Trim() } else { '(none)' })))
    Verdict $step $H ($rowH.Count -gt 0 -and -not (Get-DeadByKey $H $dH $key)) 'the owner holds the dragon alive' (($rowH | Select-Object -Last 1) -join '')
    Verdict $step $P ($rowP.Count -gt 0 -and -not (Get-DeadByKey $P $dP $key)) 'the PARTNER now holds a copy of the same GUID' (($rowP | Select-Object -Last 1) -join '')
    Verdict $step $P (Get-MirroredByKey $P $dP $key) 'the partner''s copy is marked MIRRORED (never persisted, never its own AI)' 'mirrored=1'
    $hH = Get-HpByKey $H $dH $key
    $hP = Get-HpByKey $P $dP $key
    Verdict $step $P ($null -ne $hH -and $null -ne $hP -and [math]::Abs($hH - $hP) -lt 0.5) 'both machines report the SAME HP for the shared boss' ("host=$hH peer=$hP")

    # The spawner must not have adopted it: no `OwningSpawner` means the local spawner/save
    # path cannot mint a second identity or persist a record for it.  Scoped to THIS key --
    # ordinary enemies the partner ran still send records, and that is correct.
    $rec = @(Get-Since $P $mP | Where-Object { $_ -match ('enemy record sent to host guid=' + $key + ' ') })
    Verdict $step $P ($rec.Count -eq 0) 'the mirrored copy was never persisted by the partner' ("records for THIS key=$($rec.Count)")
    Show-Since $H 'enemy spawn:|enemykey 17181166|scan: kind enemy Dragon' $mH 12
    Show-Since $P 'enemy spawn:|enemy mirror:|enemykey 17181166' $mP 14
}

# ===========================================================================
# ITEM 2 -- one shared HP, the boss bar on both, the death on both
# ===========================================================================
function Invoke-BossFight([int]$H, [int]$P, [string]$step) {
    Step 'ITEM 2 -- the one shared HP, the bar on both, the death on both'
    if ($Phase -eq 'boss') { Enter-780 $H $P $step }
    # The dragon has to exist on the OWNER before anything else can be measured, and the
    # area arrives on the level manager's own schedule -- so poll rather than wait once.
    $key = ''
    $deadline = (Get-Date).AddSeconds(240)
    $tries = 0
    while ((Get-Date) -lt $deadline -and -not $key) {
        $tries++
        Invoke-Go $H 'GreenDragon_Spawner' 140
        Invoke-Go $P 'GreenDragon_Spawner' 140
        Start-Sleep -Seconds 20
        $key = Get-DragonKey $H (Get-Mark $H - 600000)
        Note ("  poll {0}: dragon key on the owner = '{1}'" -f $tries, $key)
    }
    if (-not $key) { $key = '17181166' }
    Note ("  dragon key: '{0}'" -f $key)

    $mH = Get-Mark $H
    $mP = Get-Mark $P
    Start-Sleep -Seconds 4

    # 2a. The bar.  Membership now comes from the content's own `EnemyData.Boss`.
    #
    # THE WHOLE RUN IS SCANNED, not a window opened here: the boss lane speaks ONCE, when
    # the dragon first materialises -- which the polling above has already driven -- so a
    # window that opens afterwards can only ever miss it.  Measured: the first version
    # reported "never identified" about a line written seven minutes earlier.
    function Seen([int]$i, [string]$pat) { return [bool](@(Get-Since $i 0 | Where-Object { $_ -match $pat }).Count) }
    function SeenLine([int]$i, [string]$pat) {
        $m = @(Get-Since $i 0 | Where-Object { $_ -match $pat })
        if ($m.Count -eq 0) { return '' }
        return ([string]$m[-1]).Trim()
    }
    Verdict $step $H (Seen $H 'boss probe: Dragon_Green_BP_C') 'the boss probe asked the dragon''s class' (SeenLine $H 'boss probe: Dragon_Green_BP_C')
    Verdict $step $H (Seen $H 'data\.Boss=1 -> boss=1') 'the CONTENT says this enemy is a boss (EnemyData.Boss)' (SeenLine $H 'data\.Boss=1 -> boss=1')
    Verdict $step $H (Seen $H ('boss: guid=' + $key + ' is a boss')) 'the owner identified the dragon as a boss' (SeenLine $H ('boss: guid=' + $key + ' is a boss'))
    Verdict $step $H (Seen $H 'boss: ActivateBossHPBar called|boss: ActivateBossHPbar on the player class called') 'the owner raised its own bar through the game''s own entry' (SeenLine $H 'boss: ActivateBossHPBar called')
    Verdict $step $H (Seen $H ('boss: sent guid=' + $key + '.*bar on')) 'the owner published the bar' (SeenLine $H ('boss: sent guid=' + $key))
    Verdict $step $P (Seen $P 'boss: follower raised its own bar for guid=') 'the PARTNER raised its OWN bar for its own copy' (SeenLine $P 'boss: follower raised')
    Verdict $step $P (Seen $P ('enemy spawn: constructed guid=' + $key)) 'the partner constructed its copy (the mirror item 1 proves)' (SeenLine $P ('enemy spawn: constructed guid=' + $key))

    # 2b. The one HP, hit from BOTH players.
    $dH0 = Get-Mark $H; $dP0 = Get-Mark $P
    Invoke-Dump $H 'hp-before-h'
    Invoke-Dump $P 'hp-before-p'
    $h0H = Get-HpByKey $H $dH0 $key
    $h0P = Get-HpByKey $P $dP0 $key
    Note ("  HP before: host={0} peer={1}" -f $h0H, $h0P)
    Verdict $step $P ($null -ne $h0P) 'the partner holds a copy whose HP it can read (the mirror from item 1)' ("peer hp=$h0P")

    # The OWNER's player hits it first.
    $mHit1 = Get-Mark $H
    Invoke-BossHit $H $key 200
    [void](Check $step $H ('rig: hitguid -> key=' + $key + ' LOCAL hit') 30 'the owner''s player landed a hit on the dragon' $mHit1)
    Start-Sleep -Seconds 4
    # Then the PARTNER hits its mirrored copy: the hit must travel to the owner as the
    # existing enemy-hit intent, and the OWNER must apply it to the one number.
    $mHit2 = Get-Mark $P
    $mOwn = Get-Mark $H
    Invoke-BossHit $P $key 200
    [void](Check $step $P ('rig: hitguid -> key=' + $key) 30 'the partner''s player landed a hit on its copy' $mHit2)
    [void](Check $step $H ('hit on enemy ' + $key + ' from the other player') 40 'the OWNER applied the partner''s hit to the one shared HP' $mOwn)
    Start-Sleep -Seconds 6

    $dH1 = Get-Mark $H; $dP1 = Get-Mark $P
    Invoke-Dump $H 'hp-after-h'
    Invoke-Dump $P 'hp-after-p'
    $h1H = Get-HpByKey $H $dH1 $key
    $h1P = Get-HpByKey $P $dP1 $key
    Note ("  HP after:  host={0} peer={1}" -f $h1H, $h1P)
    Verdict $step $H ($null -ne $h0H -and $null -ne $h1H -and $h1H -lt $h0H) 'the dragon''s HP FELL on the owner (damage from both players)' ("{0} -> {1}" -f $h0H, $h1H)
    Verdict $step $P ($null -ne $h1H -and $null -ne $h1P -and [math]::Abs($h1H - $h1P) -lt 0.5) 'both machines report the SAME HP after the hits' ("host=$h1H peer=$h1P")

    # 2c. Death recorded on both, and the copy removed.
    $mK = Get-Mark $H; $mKP = Get-Mark $P
    Set-Control $H 'qd.enemykill' $key
    Say "  enemykill[owner] -> $key"
    [void](Check $step $H ('rig: enemykill -> key=' + $key) 30 'the game''s own Kill ran on the dragon' $mK)
    Start-Sleep -Seconds 8
    [void](Check $step $H ('enemy death recorded guid=' + $key) 40 'the owner recorded the death' $mK)
    # The PARTNER holds a MIRROR, so it learns the death the same way it learns everything
    # else about that key: by the owner's row entering its DeadLedger and the pump applying it
    # to the copy.  That is the co-op requirement ("the death is recorded on both"), and it is
    # proved by the ledger line OR the ordinary record line, whichever the copy's state took.
    [void](Check $step $P ('the other machine recorded this death|enemy death recorded guid=' + $key) 70 'the PARTNER recorded the same death (its copy died too)' $mKP)
    # And the copy must be gone from the partner's world after that.
    $deadP = @(Get-Since $P $mKP | Where-Object { $_ -match ('enemykey ' + $key + ' DEAD') })
    if ($deadP.Count -eq 0) {
        $dl = (Get-Date).AddSeconds(30)
        while ((Get-Date) -lt $dl -and $deadP.Count -eq 0) {
            Start-Sleep -Seconds 3
            Set-Control $P 'qd.enemies' ('after-death-' + (Get-Date -Format 'HHmmss'))
            Start-Sleep -Seconds 2
            $deadP = @(Get-Since $P 0 | Where-Object { $_ -match ('enemykey ' + $key + ' DEAD') })
        }
    }
    Verdict $step $P ($deadP.Count -gt 0) 'the partner''s mirrored copy is DEAD in its own world' $(if ($deadP.Count) { ([string]$deadP[-1]).Trim() } else { '' })
    Show-Since $H 'boss:|hit on enemy|rig: hitguid|enemy death recorded' $mH 18
    Show-Since $P 'boss:|rig: hitguid|enemy death recorded|enemy mirror:|other machine recorded this death' $mP 18
}

# ===========================================================================
# ITEM 3 -- the cutscene: start on one, hold the other, resume BOTH together
# ===========================================================================
function Invoke-Cutscene([int]$H, [int]$P, [string]$step) {
    Step 'ITEM 3 -- SecretSwordGetSequence: start on one machine, HOLD and JOINT RESUME'
    if ($Phase -eq 'cut') { Enter-780 $H $P $step }
    $mH = Get-Mark $H
    $mP = Get-Mark $P
    Set-Control $P 'qd.sequence' ('SecretSwordGetSequence#' + (Get-Date -Format 'HHmmss'))
    Say '  sequence[peer] -> SecretSwordGetSequence (Play on the placed actor''s own player)'
    Start-Sleep -Seconds 8
    [void](Check $step $P 'rig: sequence lever -> .* Play ran' 30 'the sequencer''s own Play ran on the placed actor' $mP)
    [void](Check $step $P 'cutscene: local started ' 40 'one machine detected ITS OWN cutscene playing' $mP)
    [void](Check $step $H 'cutscene: (holding the local player|playing the partner.s sequence)' 40 'the OTHER machine HELD for the partner''s cutscene' $mH)

    # Session 15 could not prove the resume because the sequence was still playing when the
    # run ended.  End it on purpose through the sequencer's own Stop.
    Start-Sleep -Seconds 6
    $mEnd = Get-Mark $P
    $mEndH = Get-Mark $H
    Set-Control $P 'qd.sequence' ('stop#' + (Get-Date -Format 'HHmmss'))
    Say '  sequence[peer] -> stop (the sequencer''s own Stop)'
    [void](Check $step $P 'rig: sequence lever -> .* Stop ran' 30 'the sequencer''s Stop ran (the cutscene is ended)' $mEnd)
    [void](Check $step $P 'cutscene: local ended ' 40 'the machine that played it saw it END' $mEnd)
    [void](Check $step $H 'cutscene: partner.s cutscene ended; resuming together' 60 'the held machine RESUMED (exit + unlock)' $mEndH)
    $unlock = @(Get-Since $H $mEndH | Where-Object { $_ -match 'cutscene: partner.s cutscene ended; resuming together' })
    $okUnlock = $false
    if ($unlock.Count) { $okUnlock = ([string]$unlock[-1]) -match 'unlock=1' }
    Verdict $step $H $okUnlock 'the held partner was RELEASED (unlock=1)' $(if ($unlock.Count) { ([string]$unlock[-1]).Trim() } else { '' })
    Show-Since $P 'cutscene:|rig: sequence lever' $mP 12
    Show-Since $H 'cutscene:' $mH 10
}

# ===========================================================================
# main
# ===========================================================================
Say "session16 start: phase=$Phase"
Stop-All
Seed-FreshPair
Start-Boot $Phase
$H = $script:H
$P = $script:P

if ($Phase -eq 'item1' -or $Phase -eq 'all') { Invoke-Mirror $H $P 'item1' }if ($Phase -eq 'boss'  -or $Phase -eq 'all') { Invoke-BossFight $H $P 'boss' }
if ($Phase -eq 'cut'   -or $Phase -eq 'all') { Invoke-Cutscene $H $P 'cut' }

Step 'SESSION SURVIVAL'
foreach ($i in 1, 2) {
    $alive = Test-Alive $i
    $script:verdicts.Add([pscustomobject]@{ Step = 'survival'; Inst = $i; What = 'alive at the end'; Ok = $alive; Line = '' }) | Out-Null
    Note "  $(if ($alive) { 'PASS' } else { 'FAIL' }) [$i] process alive=$alive"
    $err = Get-Matches $i 'ERR |EXCEPTION|ACCESS_VIOLATION|fatal|\bfault'
    Note "  [$i] error/fault lines: $($err.Count)"
    $err | Select-Object -Last 4 | ForEach-Object {
        $s = [string]$_
        if ($s.Length -gt 220) { $s = $s.Substring(0, 220) + ' ...' }
        Note "  [$i] $s"
    }
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
Say "report: $Report"
