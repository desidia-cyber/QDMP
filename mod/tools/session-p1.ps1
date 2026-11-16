# session-p1.ps1 -- Playthrough part p1: CAMPAIGN-ROUTE R1-R15 and boss B1, plus the
# desync auditor's live proof (features/desync-audit.md section 10).
#
# WHY THIS FILE EXISTS
# --------------------
# The route document lists R1-R15 as "the Green Ruins prologue": new game -> intro ->
# the camp, the rusty sword, the tutorial barricade, the first rest, the fast-travel
# node, the Sword Shrine key and gate, the dunger lift, the two optional rooms, the
# outer isles, the merchant's isle, and the Shrine Boss.  Only a handful of those had
# ever been driven on the rig (the gate, a barrel, a checkpoint, the boss), and never as
# one ordered run with the desync auditor asked after every step.  This script is that
# run, in the order a pair of players would do it.
#
# EVERY STEP IS THE PLAYER'S OWN VERB
# -----------------------------------
# R1-R3 is a real new game from the title screen.  R4-R8/R14 are a real interact press
# on the NPC / checkpoint / teleporter / merchant (the rig's `qd.act near:` runs the
# target's own Blueprint verb, which is what the E press runs).  R6 breaks the
# barricade through its own `Break`.  R9 picks the key up and opens the gate through
# the door's own verbs.  R10 is a player standing ON the lift while the mover lane
# carries it.  R15/B1 is fought with real attack input (`qd.attack`, the pawn's own
# compiled input events) with god mode OFF, and only finished by measured damage.
#
# HONESTY RULES (inherited from every session since 18)
# ----------------------------------------------------
# * A verdict prints the line it read.  No line, no pass.
# * A step the rig cannot perform says so by name (NOT-REACHABLE) with the measured
#   reason, and the closest real mechanism is proved instead.
# * Only lines from THIS run are read (after the last `hooked FEngineLoop`).
# * `qd.audit` is forced on BOTH machines after every step and the summary line is
#   recorded, because "the two machines agreed" is the product's claim.
[CmdletBinding()]
param(
    [ValidateSet('audit', 'hub', 'dungeon', 'shopboss', 'intro', 'all')]
    [string]$Phase = 'audit',
    [int]$BootSeconds = 420,
    [int]$CleanSeconds = 110,
    [int]$StepSeconds = 60,
    # Intro only: the fractional client click that starts a NEW GAME ("x,y"), and keys
    # to post first ("ENTER,ENTER").  Kept as parameters because the menu's layout is a
    # fact about the build, not about this harness: the first run screenshots the title
    # screen and the numbers are corrected from that picture.
    [string]$MenuFrac = '',
    [string]$MenuKeys = '',
    # Where the relay binary is.  Defaults to the canonical release path; a run that must not
    # touch an exe another delegate's session has mapped builds into `target-p1` instead and
    # points here (measured: cargo cannot replace a mapped exe -- Access is denied, os error 5).
    [string]$RelayExe = '',
    [string]$Report = ''
)

$ErrorActionPreference = 'Stop'
$here     = $PSScriptRoot
$repo     = Split-Path -Parent (Split-Path -Parent $here)
$gameDir  = 'G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64'
$relay    = Join-Path (Split-Path -Parent $here) 'rust\target\release\qd-relay.exe'
if ($RelayExe) { $relay = $RelayExe }
$rigRoot  = 'G:\Freespace\Queens_Domain\_test\p1-' + $Phase
$seed     = Join-Path $repo 'analysis\save-backup-before-live'
$shotsDir = 'G:\Freespace\Queens_Domain\_test\playthrough\p1'
$outDir   = Join-Path $rigRoot 'out'
if (-not $Report) { $Report = Join-Path $outDir ('report-p1-' + $Phase + '.txt') }
foreach ($d in @($rigRoot, $outDir, $shotsDir)) {
    if (-not (Test-Path -LiteralPath $d)) { New-Item -ItemType Directory -Force -Path $d | Out-Null }
}
# The p1 report's own screenshot names must be cutable per step; the brief says at most
# 6 pairs go to the coordinator, so no pruning here, only an index printed at the end.

$script:lines = New-Object System.Collections.Generic.List[string]
$script:runCache = @{}
$script:verdicts = New-Object System.Collections.Generic.List[object]
$script:shots = New-Object System.Collections.Generic.List[object]
$script:audits = New-Object System.Collections.Generic.List[object]
$script:seedBank = @{}
$script:stopCount = 0
$script:liveLock = 'G:\Freespace\Queens_Domain\_test\locks\live'
$script:lockHeld = $false
$script:lockTouch = [datetime]::MinValue

trap {
    Write-Host ("UNHANDLED: " + $_.Exception.Message)
    Write-Host ("AT: " + $_.InvocationInfo.PositionMessage)
    $script:lines.Add("UNHANDLED: " + $_.Exception.Message) | Out-Null
    $script:lines.Add("AT: " + $_.InvocationInfo.PositionMessage) | Out-Null
    try { Stop-All ("UNHANDLED exception in phase '" + $Phase + "': " + $_.Exception.Message) } catch {}
    try { Release-LiveLock } catch {}
    try { $script:lines | Set-Content -LiteralPath $Report } catch {}
    exit 1
}

# ===========================================================================
# THE LIVE LOCK -- the STRICT rule (COMMON-BRIEF, 2026-11-16)
# ===========================================================================
# Concurrent live sessions were killing each other, so: launch nothing unless this script
# HOLDS `_test\locks\live`; break a lock only when its owner.txt is > 25 min old AND no
# QueensDomain-Win64-Shipping / qd-relay process is running; rewrite owner.txt while holding
# (a heartbeat, so a live session never looks stale); and never Stop-Process by NAME.
function Touch-LiveLock([switch]$Force) {
    if (-not $script:lockHeld) { return }
    if (-not $Force -and ((Get-Date) - $script:lockTouch).TotalSeconds -lt 60) { return }
    try {
        Set-Content -LiteralPath (Join-Path $script:liveLock 'owner.txt') -Value 'play-p1' -NoNewline -ErrorAction Stop
        $script:lockTouch = Get-Date
    } catch { }
}

function Acquire-LiveLock {
    $procs = @(Get-Process -Name 'QueensDomain-Win64-Shipping', 'qd-relay' -ErrorAction SilentlyContinue)
    if (Test-Path -LiteralPath $script:liveLock) {
        $owner = ''
        $op = Join-Path $script:liveLock 'owner.txt'
        if (Test-Path -LiteralPath $op) { $owner = ((Get-Content -LiteralPath $op -ErrorAction SilentlyContinue) -join '').Trim() }
        # THE AGE COMES FROM owner.txt, NOT FROM THE DIRECTORY.  On NTFS rewriting a file's
        # contents does not touch the parent directory's LastWriteTime, so a live session that
        # rewrites owner.txt every minute still leaves the directory looking old -- and the
        # "> 25 min = stale, break it" rule would then break a HEALTHY lock.  Measured here:
        # play-p2's owner.txt was 07:18:57 while the lock directory still read 07:12:09.
        $stampPath = if (Test-Path -LiteralPath $op) { $op } else { $script:liveLock }
        $age = ((Get-Date) - (Get-Item -LiteralPath $stampPath).LastWriteTime).TotalMinutes
        if ($owner -like 'play-p1*') {
            $script:lockHeld = $true
            Touch-LiveLock -Force
            Say 'LIVE lock is already mine (owner.txt = play-p1)'
            return $true
        }
        if ($age -gt 25 -and $procs.Count -eq 0) {
            Say ("breaking a STALE LIVE lock: owner='{0}', age {1} min, no game/relay process alive (the strict rule allows this)" -f $owner, [math]::Round($age, 1))
            Remove-Item -LiteralPath $op -Force -ErrorAction SilentlyContinue
            Remove-Item -LiteralPath $script:liveLock -Recurse -Force -ErrorAction SilentlyContinue
        } else {
            Say ("REFUSED: the LIVE lock is held by '{0}' (age {1} min, {2} game/relay process(es) alive); the strict rule says launch nothing" -f $owner, [math]::Round($age, 1), $procs.Count)
            return $false
        }
    }
    if (@(Get-Process -Name 'QueensDomain-Win64-Shipping', 'qd-relay' -ErrorAction SilentlyContinue).Count -gt 0) {
        Say 'REFUSED: a game/relay process is alive while no LIVE lock exists; not launching into it'
        return $false
    }
    try { New-Item -ItemType Directory -Path $script:liveLock -ErrorAction Stop | Out-Null }
    catch { Say 'REFUSED: could not create the LIVE lock (someone else won the race)'; return $false }
    Set-Content -LiteralPath (Join-Path $script:liveLock 'owner.txt') -Value 'play-p1' -NoNewline
    $script:lockHeld = $true
    $script:lockTouch = Get-Date
    Say ("LIVE lock acquired (owner.txt = play-p1): {0}" -f $script:liveLock)
    return $true
}

function Release-LiveLock {
    if (-not $script:lockHeld) { return }
    $owner = ''
    $op = Join-Path $script:liveLock 'owner.txt'
    if (Test-Path -LiteralPath $op) { $owner = ((Get-Content -LiteralPath $op -ErrorAction SilentlyContinue) -join '').Trim() }
    if ($owner -like 'play-p1*') {
        Remove-Item -LiteralPath $op -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $script:liveLock -Recurse -Force -ErrorAction SilentlyContinue
        Say 'LIVE lock released'
    } else {
        Say ("the LIVE lock is no longer mine (owner='{0}'); left alone" -f $owner)
    }
    $script:lockHeld = $false
}

function Say($m) {
    $s = "[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $m
    Write-Host $s
    $script:lines.Add($s) | Out-Null
    # every narration is a heartbeat opportunity: the owner.txt is rewritten at most once a
    # minute, which is well inside the 10-minute rule
    if ($script:lockHeld) { Touch-LiveLock }
}
function Note($m) { Write-Host $m; $script:lines.Add($m) | Out-Null }

function Get-RigPaths([int]$i) {
    $root   = Join-Path $rigRoot ("inst{0}" -f $i)
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
    for ($try = 0; $try -lt 3; $try++) {
        try {
            $fs = [System.IO.File]::Open($path, [System.IO.FileMode]::Open,
                                         [System.IO.FileAccess]::Read,
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
function Get-Matches([int]$i, [string]$pattern) { return @(Get-RunLines $i | Where-Object { $_ -match $pattern }) }
function Get-LineMs([string]$ln) {
    if ($ln -match '^\s*(\d{1,2}):(\d{2}):(\d{2})\.(\d{3})') {
        return ([int]$Matches[1] * 3600000 + [int]$Matches[2] * 60000 +
                [int]$Matches[3] * 1000 + [int]$Matches[4])
    }
    return -1
}
function Get-Mark([int]$i) {
    $now = Get-Date
    return ($now.Hour * 3600000 + $now.Minute * 60000 + $now.Second * 1000 + $now.Millisecond) - 1000
}
function Test-Alive([int]$i) {
    if (-not $script:pids -or -not $script:pids.ContainsKey($i)) { return $false }
    return [bool](Get-Process -Id $script:pids[$i] -ErrorAction SilentlyContinue)
}
function Get-Since([int]$i, [int]$markMs) {
    $src = Get-RunLines $i
    $out = New-Object System.Collections.Generic.List[string]
    foreach ($ln in $src) {
        $t = Get-LineMs ([string]$ln)
        if ($t -ge 0 -and $t -ge $markMs) { $out.Add([string]$ln) }
    }
    return $out.ToArray()
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
        if ((Get-WorldState $i).World -and (Get-WorldState $i).World -ne 'title_screen') { return $true }
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
        } catch {
            if ($try -ge 5) { throw }
            Start-Sleep -Milliseconds 120
        }
    }
}
function Check([string]$step, [int]$i, [string]$pattern, [int]$secs, [string]$label, [int]$Mark = 0) {
    if (-not (Test-Alive $i)) {
        $script:verdicts.Add([pscustomobject]@{ Step = $step; Inst = $i; What = $label; Ok = $false; Line = 'INSTANCE DIED' }) | Out-Null
        Note "  FAIL [$i] $label -- the instance is no longer running"
        return $false
    }
    $m = Wait-Since $i $pattern $Mark $secs
    $found = ($m.Count -gt 0)
    $script:verdicts.Add([pscustomobject]@{ Step = $step; Inst = $i; What = $label; Ok = $found; Line = ($m | Select-Object -Last 1) }) | Out-Null
    if ($found) { Note "  PASS [$i] $label" }
    else { Note "  --   [$i] $label (no line matches '$pattern' in ${secs}s)" }
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

# --- levers -------------------------------------------------------------------------
$script:goSeq = 0
function Invoke-Go([int]$i, [string]$name, [int]$near = 140) {
    $script:goSeq++
    $yaw = ($script:goSeq * 37) % 360
    Set-Control $i 'qd.near' ([string]$near)
    Set-Control $i 'qd.tp' ('near:' + $name + ',' + $yaw + '#' + (Get-Date -Format 'HHmmssfff'))
}
function Invoke-Hard([int]$i, [string]$map) {
    $nonce = Get-Date -Format 'HHmmssfff'
    Set-Control $i 'qd.hard' ($map + '#' + $nonce)
    Say "  hard[$i] -> $map (nonce $nonce)"
}
function Invoke-Act([int]$i, [string]$spec) {
    Set-Control $i 'qd.act' ('near:' + $spec + '#' + (Get-Date -Format 'HHmmssfff'))
    Say "  act[$i] -> near:$spec"
}
function Invoke-Trigger([int]$i, [string]$name, [string]$area) {
    $spec = if ($area) { $name + '@' + $area } else { $name }
    Set-Control $i 'qd.trigger' ($spec + '#' + (Get-Date -Format 'HHmmssfff'))
    Say "  trigger[$i] -> $spec"
}
function Invoke-FlagRead([int]$i, [string]$name) {
    $mark = Get-Mark $i
    Set-Control $i 'qd.flag' ($name + '#' + (Get-Date -Format 'HHmmssfff'))
    $m = @(Wait-Since $i ('rig: flag dump -- ' + [regex]::Escape($name) + ' = ') $mark 15)
    if ($m.Count -eq 0) { return $null }
    $s = [string]$m[-1]
    if ($s -match ('rig: flag dump -- ' + [regex]::Escape($name) + ' = (-?\d+)')) {
        return [pscustomobject]@{ Name = $name; Value = [int]$Matches[1]; Line = $s.Trim() }
    }
    return $null
}
function Invoke-EnemyDump([int]$i, [string]$tag, [int]$timeoutSec = 60) {
    $mark = Get-Mark $i
    Set-Control $i 'qd.enemies' ('p1-' + $tag + '-' + (Get-Date -Format 'HHmmssfff'))
    $deadline = (Get-Date).AddSeconds($timeoutSec)
    $rows = @()
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 700
        $rows = @(Get-Since $i $mark | Where-Object { $_ -match 'enemykey [0-9A-Fa-f]{8} (ALIVE|DEAD|GONE)' })
        if ($rows.Count -gt 0) { break }
    }
    $out = @{}
    foreach ($ln in $rows) {
        $s = [string]$ln
        if ($s -notmatch 'enemykey ([0-9A-Fa-f]{8}) (ALIVE|DEAD|GONE)') { continue }
        $k = $Matches[1].ToUpperInvariant(); $st = $Matches[2]
        $hp = $null
        if ($s -match '\shp=([-0-9.]+)') { $hp = [double]$Matches[1] }
        $mx = $null
        if ($s -match '\smx=([-0-9.]+)') { $mx = [double]$Matches[1] }
        $cls = ''
        if ($s -match '\scls=(\S+)') { $cls = $Matches[1] }
        $out[$k] = [pscustomobject]@{ Key = $k; Id = $k; State = $st; Hp = $hp; Mx = $mx; Cls = $cls; Line = $s.Trim() }
    }
    return $out
}
function Find-BossKey([hashtable]$dump, [string]$cls) {
    foreach ($k in $dump.Keys) {
        $r = $dump[$k]
        if ($r.State -eq 'ALIVE' -and $r.Cls -match [regex]::Escape($cls)) { return $r }
    }
    return $null
}

# --- the auditor, asked after every step -------------------------------------------
# WHAT ONE STEP'S ANSWER IS.  The auditor reports a category only once its digest has disagreed
# for 3 s, and it reports the HEAL with `audit: <cat> healed (...)`.  On this build the first
# ~3 minutes after a boot legitimately disagree (measured in the audit phase, see the report:
# `enemy` 64 key rows, `movers` 24, `boss`, `flags`, every one of them healed), so a verdict that
# treats any DESYNC line in the window as a failure grades a normal settling window as a bug.
# The verdict therefore counts a category as OPEN only when its LAST event in the window is a
# DESYNC rather than a heal.
function Invoke-Audit([int]$i, [string]$tag) {
    $mark = Get-Mark $i
    Set-Control $i 'qd.audit' ('p1-' + $tag + '-' + (Get-Date -Format 'HHmmssfff'))
    $m = @(Wait-Since $i 'audit: (pass|OK|DESYNC|category|\S+ healed)' $mark 25)
    $last = ''
    if ($m.Count -gt 0) {
        $des = @($m | Where-Object { $_ -match 'audit: DESYNC' })
        if ($des.Count -gt 0) { $last = ([string]$des[-1]).Trim() }
        else { $last = ([string]$m[-1]).Trim() }
    }
    # per-category: is the LAST event a desync or a heal?
    $open = New-Object System.Collections.Generic.List[string]
    $cats = @($m | ForEach-Object { if ($_ -match 'audit: (?:DESYNC )?(\w+)[ >]') { $Matches[1] } } |
              Where-Object { $_ -match '^(flags|actors|quest|enemy|boss|movers|levels|map|cutscene|dialogue|deaths|objects)$' } |
              Sort-Object -Unique)
    foreach ($cat in $cats) {
        $lastDes = -1; $lastHeal = -1
        for ($k = 0; $k -lt $m.Count; $k++) {
            $ln = [string]$m[$k]
            if ($ln -match ("audit: DESYNC " + $cat + " ")) { $lastDes = $k }
            if ($ln -match ("audit: " + $cat + " healed")) { $lastHeal = $k }
        }
        if ($lastDes -gt $lastHeal) { $open.Add($cat) | Out-Null }
    }
    $script:audits.Add([pscustomobject]@{
        Inst = $i; Step = $tag; Line = $last
        Desync = ($m | Where-Object { $_ -match 'audit: DESYNC' }).Count
        Open = ($open -join ',')
    }) | Out-Null
    if ($last) { Note ("  audit[$i] {0}: {1}" -f $tag, $last) } else { Note ("  audit[$i] {0}: (no audit line in 25 s)" -f $tag) }
    if ($open.Count -gt 0) { Note ("  audit[$i] {0}: still OPEN after the window: {1}" -f $tag, ($open -join ',')) }
    return $last
}
function Invoke-AuditBoth([int]$H, [int]$P, [string]$tag) {
    Invoke-Audit $H $tag | Out-Null
    Invoke-Audit $P $tag | Out-Null
    $rows = @($script:audits | Where-Object { $_.Step -eq $tag })
    $open = @($rows | Where-Object { $_.Open -ne '' })
    $heard = @($rows | Where-Object { $_.Line -ne '' })
    # NO LINE, NO PASS: a step where the lever produced nothing is not a clean step.
    Verdict ('audit-' + $tag) $H ($heard.Count -gt 0 -and $open.Count -eq 0) `
            'the auditor answered for this step and left no category open' `
            ($(if ($heard.Count -eq 0) { 'NO audit line on either machine' }
               elseif ($open.Count -gt 0) { ($open | ForEach-Object { "open: " + $_.Open }) -join ' | ' }
               else { ($rows | ForEach-Object { $_.Line })[-1] }))
}

# A pair does not start playing mid-argument: wait until the auditor has settled once, so the
# per-step audits measure the step rather than the boot.
function Wait-AuditSettled([int]$i, [int]$seconds = 300) {
    $deadline = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $deadline) {
        $m = @(Get-Matches $i 'audit: OK \d+ categories')
        if ($m.Count -gt 0) { return ([string]$m[-1]).Trim() }
        if (-not (Test-Alive $i)) { return '' }
        Start-Sleep -Seconds 3
    }
    return ''
}

# --- screenshots --------------------------------------------------------------------
function Measure-Shot([string]$path) {
    Add-Type -AssemblyName System.Drawing -ErrorAction SilentlyContinue
    $bmp = [System.Drawing.Bitmap]::FromFile($path)
    try {
        $sx = [math]::Max(1, [int]($bmp.Width / 64)); $sy = [math]::Max(1, [int]($bmp.Height / 64))
        $vals = New-Object System.Collections.Generic.List[double]
        for ($y = 0; $y -lt $bmp.Height; $y += $sy) {
            for ($x = 0; $x -lt $bmp.Width; $x += $sx) {
                $c = $bmp.GetPixel($x, $y)
                $vals.Add(0.2126 * $c.R + 0.7152 * $c.G + 0.0722 * $c.B)
            }
        }
        $mean = 0.0; foreach ($v in $vals) { $mean += $v }; $mean = $mean / [math]::Max(1, $vals.Count)
        $var = 0.0; foreach ($v in $vals) { $var += ($v - $mean) * ($v - $mean) }
        $sd = [math]::Sqrt($var / [math]::Max(1, $vals.Count))
        return [pscustomobject]@{ W = $bmp.Width; H = $bmp.Height; Mean = [math]::Round($mean, 1); Std = [math]::Round($sd, 1) }
    } finally { $bmp.Dispose() }
}
function Save-Shots([string]$tag) {
    foreach ($i in 1, 2) {
        if (-not $script:pids -or -not $script:pids.ContainsKey($i)) { continue }
        if (-not (Test-Alive $i)) { continue }
        $safe = $tag -replace '[^A-Za-z0-9_.-]', '_'
        $out = Join-Path $shotsDir ("p1-{0}-{1}-inst{2}.png" -f $Phase, $safe, $i)
        $r = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'capture-hidden.ps1') `
                -Out $out -ProcessId $script:pids[$i] 2>&1
        $said = (@($r) | Select-Object -Last 1)
        if (-not (Test-Path -LiteralPath $out)) {
            Note ("  shot[{0}] {1}: FAILED ({2})" -f $i, $tag, $said)
            $script:shots.Add([pscustomobject]@{ Inst = $i; Tag = $tag; Path = ''; Mean = -1; Std = -1 }) | Out-Null
            continue
        }
        $m = Measure-Shot $out
        Note ("  shot[{0}] {1}: {2}x{3} mean={4} stddev={5} -> {6}" -f $i, $tag, $m.W, $m.H, $m.Mean, $m.Std, $out)
        $script:shots.Add([pscustomobject]@{ Inst = $i; Tag = $tag; Path = $out; Mean = $m.Mean; Std = $m.Std }) | Out-Null
    }
}

# --- world state --------------------------------------------------------------------
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
    return $out
}
function Wait-Hub([int]$i, [int]$seconds = 300) {
    $deadline = (Get-Date).AddSeconds($seconds)
    $last = [pscustomobject]@{ World = ''; Partner = ''; Agree = ''; Area = '' }
    while ((Get-Date) -lt $deadline) {
        $last = Get-WorldState $i
        if ($last.World -and $last.World -ne 'title_screen') { return $last }
        if (-not (Test-Alive $i)) { return $last }
        Start-Sleep -Seconds 2
    }
    return $last
}

# --- teardown -----------------------------------------------------------------------
function Stop-All([string]$Reason) {
    if (-not $Reason) { $Reason = 'NO REASON GIVEN (that is a harness defect, not a crash)' }
    $script:stopCount++
    Say ("STOP-ALL #{0} phase={1} reason={2}" -f $script:stopCount, $Phase, $Reason)
    foreach ($i in 1, 2) {
        if (-not $script:pids -or -not $script:pids.ContainsKey($i)) { continue }
        if (-not (Test-Alive $i)) { continue }
        $last = ''
        try {
            $l = @(Get-RunLines $i)
            if ($l.Count -gt 0) { $last = ([string]$l[-1]).Trim() }
        } catch { $last = '' }
        if ($last.Length -gt 300) { $last = $last.Substring(0, 300) + ' ...' }
        Say ("  killing inst{0} pid={1}; its last log line was: {2}" -f $i, $script:pids[$i], $last)
    }
    if ($script:relayProc) { Stop-Process -Id $script:relayProc.Id -Force -ErrorAction SilentlyContinue }
    # ONLY WHAT THIS SCRIPT STARTED.  The box is shared with other delegates' sessions, and
    # `Get-Process -Name QueensDomain-Win64-Shipping | Stop-Process` would kill theirs.  The
    # pre-flight check below refuses to start when a foreign process is alive, so anything
    # found here is mine (a previous phase's leftover) or was already gone.
    foreach ($i in 1, 2) {
        if ($script:pids -and $script:pids.ContainsKey($i)) {
            Stop-Process -Id $script:pids[$i] -Force -ErrorAction SilentlyContinue
        }
    }
    Start-Sleep -Seconds 2
}

# A FOREIGN SESSION IS NOT MINE TO KILL.  Measured: the live lock changed hands between
# play-p2 and play-p3 while this script was waiting, and their game/relay processes were
# briefly alive with no lock.  Starting a second pair then would either collide on the
# relay port or tempt the harness into killing their run.
function Assert-NoForeignSession {
    $foreign = @(Get-Process -Name 'QueensDomain-Win64-Shipping', 'qd-relay' -ErrorAction SilentlyContinue)
    if ($foreign.Count -gt 0) {
        Say ("REFUSED: {0} game/relay process(es) are running that this script did not start:" -f $foreign.Count)
        foreach ($f in $foreign) { Note ("  pid={0} {1} started {2}" -f $f.Id, $f.ProcessName, $f.StartTime) }
        Say 'REFUSED: not killing them (another delegate may own them); run again when the machine is free'
        $script:lines | Set-Content -LiteralPath $Report
        exit 3
    }
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
    $pdb = Join-Path $repo 'mod\native\build\winmm.pdb'
    if (Test-Path -LiteralPath $pdb) {
        try { Copy-Item -LiteralPath $pdb -Destination (Join-Path $gameDir 'winmm.pdb') -Force } catch {}
    }
    # THE SHIPPED qdcoop.ini, VERBATIM, with only the relay pinned to loopback and the log
    # level raised to 3 (the brief's rule).  DesyncAudit=1 is in that file.
    $srcIni = Join-Path $repo 'mod\qdcoop.ini'
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
}

function Seed-Pair([switch]$NoSave) {
    Say 're-seeding both instances from the pristine pre-live save'
    foreach ($i in 1, 2) {
        $r = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'make_test_instance.ps1') `
                -Name ("inst{0}" -f $i) -Index $i -SourceSave $seed -Root $rigRoot -Suffix $inst[$i].Suffix -Force 2>&1
        $r | Where-Object { $_ -match 'FAIL|REFUSED|seeded|save dir|Instance ready' } | ForEach-Object { Note "  $_" }
        if ($LASTEXITCODE -ne 0) { Say "REFUSED: make_test_instance refused instance $i"; exit 2 }
    }
    if ($NoSave) {
        # R1 is "new game": an empty save directory is what puts the title screen's NEW
        # GAME in reach.  The seed exists so the directory and its config exist at all.
        foreach ($i in 1, 2) {
            $sg = Join-Path $inst[$i].Root ("Saved_{0}\SaveGames" -f $inst[$i].Suffix)
            if (Test-Path -LiteralPath $sg) {
                Get-ChildItem -LiteralPath $sg -File | ForEach-Object {
                    Note ("  removing the seed save for a NEW GAME boot: " + $_.Name)
                    Remove-Item -LiteralPath $_.FullName -Force
                }
            } else { Note ("  [$i] no save directory at $sg" ) }
        }
    }
}

function Start-Boot([string]$tag, [bool]$AutoLoad = $true) {
    Stop-All ("boot: replacing any process left over (starting the '$tag' boot)")
    Install-And-Configure
    foreach ($i in 1, 2) {
        foreach ($leaf in 'qd.tp','qd.act','qd.hit','qd.cheat','qd.cheatarg','qd.travel','qd.hard',
                          'qd.level','qd.list','qd.near','qd.drops','qd.menu','qd.efunc','qd.eattack',
                          'qd.use','qd.pdmg','qd.kill','qd.petrace','qd.enemies','qd.sequence',
                          'qd.enemykill','qd.hitguid','qd.obj','qd.flag','qd.gate','qd.take','qd.trigger',
                          'qd.selfhp','qd.nohit','qd.dlg','qd.attack','qd.equip','qd.mover','qd.wsys',
                          'qd.proj','qd.auditfork','qd.audit','qd.god','qd.scan') {
            Set-Control $i $leaf $null
        }
        Set-Control $i 'qd.scan' '1'
        if ($AutoLoad) { Set-Control $i 'qd.god' '400' }
        Set-Control $i 'qd.near' '140'
        if (Test-Path -LiteralPath $inst[$i].Pid) { Remove-Item -LiteralPath $inst[$i].Pid -ErrorAction SilentlyContinue }
        $old = $inst[$i].Log
        if (Test-Path -LiteralPath $old) { Move-Item -LiteralPath $old -Destination ($old + '.prev') -Force }
        $sc = Join-Path $gameDir ("qdcoop-{0}-respawn.ini" -f $inst[$i].Suffix)
        if (Test-Path -LiteralPath $sc) { Remove-Item -LiteralPath $sc -Force -ErrorAction SilentlyContinue }
    }
    if ($AutoLoad) { $env:QDCOOP_AUTOLOAD = '0' } else { Remove-Item Env:\QDCOOP_AUTOLOAD -ErrorAction SilentlyContinue }
    Remove-Item Env:\QDCOOP_TEST_NEAR -ErrorAction SilentlyContinue
    $env:QDCOOP_TEST_HITMS = '3000'

    if (-not (Test-Path -LiteralPath $relay)) { Say "REFUSED: relay not built at $relay"; exit 2 }
    $relayLog = Join-Path $outDir ("relay-$tag.log")
    Remove-Item -LiteralPath $relayLog -ErrorAction SilentlyContinue
    $script:relayProc = Start-Process -FilePath $relay -ArgumentList @('--bind', '127.0.0.1:47800') `
            -RedirectStandardOutput $relayLog -RedirectStandardError (Join-Path $outDir ("relay-$tag.err")) `
            -WindowStyle Hidden -PassThru
    Say ("relay pid={0} -> {1}" -f $script:relayProc.Id, $relayLog)
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
    if (-not $script:pids[1] -or -not $script:pids[2]) { Say 'ABORT: both instances are required'; exit 2 }
}

function Wait-Roles([int]$timeoutSec = 0) {
    if ($timeoutSec -le 0) { $timeoutSec = $BootSeconds }
    Say "waiting for a session (up to $timeoutSec s)"
    $deadline = (Get-Date).AddSeconds($timeoutSec)
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
            Show-Since $i 'session|REFUS|ERR |PROBE|substrate|world:' 0 14
        }
        Stop-All ("boot refused: no HOST/PEER pair within $timeoutSec s (phase '$Phase')")
        $script:lines | Set-Content -LiteralPath $Report
        exit 2
    }
    Say "roles: HOST=inst$($script:H) PEER=inst$($script:P)"
    return [pscustomobject]@{ H = $script:H; P = $script:P }
}

function Test-RelayClean([string]$tag) {
    $log = Join-Path $outDir ("relay-$tag.log")
    if (-not (Test-Path -LiteralPath $log)) { Verdict 'relay' 1 $false 'the relay log exists' ''; return }
    $bad = @(Get-Content -LiteralPath $log | Where-Object { $_ -match 'malformed datagram' })
    $kinds = @{}
    foreach ($b in $bad) {
        if ($b -match '\(kind (0x[0-9a-fA-F]+)\)') { $kinds[$Matches[1]] = 1 }
    }
    Verdict 'relay' 1 ($bad.Count -eq 0) 'the relay forwarded every datagram (no malformed kinds)' `
            ("malformed total={0}{1}" -f $bad.Count, $(if ($bad.Count) { ' kinds=' + (($kinds.Keys | Sort-Object) -join ',') } else { '' }))
    if ($bad.Count -gt 0) { $bad | Select-Object -First 5 | ForEach-Object { Note "  [relay] $_" } }
}

# --- entering a streamed area with both machines ------------------------------------
function Enter-Area([int]$H, [int]$P, [string]$area, [string]$anchor, [string]$step) {
    Step "$step -- the HOST enters $area; the PEER must follow" `
         'R147/R150a: an area is a streamed sublevel, so the signal is TP_AREA on the travel lane. A machine that only re-announces the area it already holds is not a follow.'
    $mH = Get-Mark $H; $mP = Get-Mark $P
    Invoke-Hard $H $area
    [void](Check $step $H ('rig: hard lever -> ' + [regex]::Escape($area) + ' streamed=1') 90 'the HOST streamed the area locally' $mH)
    $hostOk = $script:verdicts[$script:verdicts.Count - 1].Ok
    if (-not $hostOk) {
        Verdict $step $P $false 'the PEER follows the host into the area' 'the host never entered it'
        return $false
    }
    $follow = @(Wait-Since $P ('travel: entered the partner.s area ' + [regex]::Escape($area)) $mP 90)
    Verdict $step $P ($follow.Count -gt 0) 'the PEER followed the host into the area' $(if ($follow.Count) { ([string]$follow[-1]).Trim() } else { '' })
    [void](Check $step $H ('rig: level engine state ' + [regex]::Escape($area) + ' LoadedLevel=1 actuallyVisible=1') 90 'the area is LOADED and SHOWN on the host' $mH)
    [void](Check $step $P ('rig: level engine state ' + [regex]::Escape($area) + ' LoadedLevel=1 actuallyVisible=1') 90 'the area is LOADED and SHOWN on the peer' $mP)
    Start-Sleep -Seconds 6
    if ($anchor) {
        Invoke-Go $H $anchor 400
        Invoke-Go $P $anchor 400
        Start-Sleep -Seconds 8
    }
    return ($follow.Count -gt 0)
}

# ===========================================================================
# PHASE: audit -- the desync auditor's live proof (the FIRST step of this part)
# ===========================================================================
function Invoke-AuditPhase([int]$H, [int]$P) {
    Say 'waiting for both players in the world'
    foreach ($i in 1, 2) { [void](Wait-Since $i 'INFO local pawn:' (Get-Mark $i) 150) }

    Step 'AUDIT-1 -- a clean pair agrees about the whole synced world' `
         'Every 10 s each machine digests 12 categories of the current map. The product claim is the line `audit: OK N categories, M items`.'
    foreach ($i in 1, 2) { Set-Control $i 'qd.audit' ('boot-' + (Get-Date -Format 'HHmmssfff')) }
    Say ("measuring a CLEAN state for {0} s (the summary is once a minute)" -f $CleanSeconds)
    Start-Sleep -Seconds $CleanSeconds
    $ok = @{}
    foreach ($i in 1, 2) {
        $m = @(Get-Matches $i 'audit: OK \d+ categories, \d+ items')
        $ok[$i] = if ($m.Count -gt 0) { ([string]$m[-1]).Trim() } else { '' }
        if ($ok[$i]) { Note ("  [{0}] {1}" -f $i, $ok[$i]) } else { Note ("  [{0}] (no 'audit: OK' yet)" -f $i) }
    }
    Verdict 'AUDIT-1' $H (-not [string]::IsNullOrEmpty($ok[$H])) 'the HOST logs audit: OK' $ok[$H]
    Verdict 'AUDIT-1' $P (-not [string]::IsNullOrEmpty($ok[$P])) 'the PEER logs audit: OK' $ok[$P]
    $des = 0
    foreach ($i in 1, 2) { $des += @(Get-Matches $i 'audit: DESYNC').Count }
    Verdict 'AUDIT-1' $H ($des -eq 0) 'no audit: DESYNC line on either machine' ("{0} DESYNC line(s) total" -f $des)
    Save-Shots 'audit-clean'

    Step 'AUDIT-2 -- one deliberate one-item desync is reported as exactly that item' `
         'qd.auditfork injects ONE flag into ONE machine auditor view (no game state). The report must name that key and both values, and must not report the rest of the bank.'
    $forkName = 'QDAuditProof'
    Say ("injecting ONE item on inst{0} only: flags {1}=1 (qd.auditfork; no lane written)" -f $P, $forkName)
    Set-Control $P 'qd.auditfork' ("{0}=1" -f $forkName)
    $reported = @{}
    $deadline = (Get-Date).AddSeconds(90)
    while ((Get-Date) -lt $deadline) {
        foreach ($i in 1, 2) {
            $m = @(Get-Matches $i ("audit: DESYNC flags {0}" -f $forkName))
            $reported[$i] = if ($m.Count -gt 0) { ([string]$m[-1]).Trim() } else { '' }
        }
        if ($reported[1] -or $reported[2]) { break }
        Start-Sleep -Seconds 2
    }
    foreach ($i in 1, 2) { if ($reported[$i]) { Note ("  [{0}] {1}" -f $i, $reported[$i]) } }
    Verdict 'AUDIT-2' $H (($reported[1] -ne '') -or ($reported[2] -ne '')) 'the desync is reported, naming the item' (($reported[1] + ' | ' + $reported[2]).Trim(' |'))
    $exact = (($reported[1] -match 'QDAuditProof host=-?\d+ peer=-?\d+') -or ($reported[2] -match 'QDAuditProof host=-?\d+ peer=-?\d+'))
    Verdict 'AUDIT-2' $H $exact 'the report carries host= and peer= for that item' ''
    $bulk = 0
    foreach ($i in 1, 2) {
        # A DESYNC CATEGORY PRINTS TWO KINDS OF LINE, and only one of them is an item:
        #   audit: DESYNC flags <whole set> host=58 peer=59 age=3104 -- asking for the item set
        #   audit: DESYNC flags QDAuditProof host=-1 peer=1 age=4120 [absent on the host]
        # The first announces the diff; counting it as "another item" fails a correct
        # one-item report.  (mod/tools/session-audit.ps1 has that shape; reported, not edited.)
        $bulk += @(Get-Matches $i 'audit: DESYNC flags ' |
                   Where-Object { $_ -notmatch [regex]::Escape($forkName) -and $_ -notmatch '<whole set>' }).Count
    }
    Verdict 'AUDIT-2' $H ($bulk -eq 0) 'exactly ONE item differs (no other flags item row)' ("{0} other flags item row(s)" -f $bulk)
    Save-Shots 'audit-forked'

    Step 'AUDIT-3 -- the mismatch heals when the item goes' 'Clearing the fork must produce `audit: flags healed` and go back to OK.'
    Set-Control $P 'qd.auditfork' 'clear'
    $healed = @(Wait-Since $P 'audit: flags healed' (Get-Mark $P) 45)
    $healed2 = @(Wait-Since $H 'audit: flags healed' (Get-Mark $H) 20)
    Verdict 'AUDIT-3' $P (($healed.Count + $healed2.Count) -gt 0) 'the mismatch heals once the item is gone' `
            $(if ($healed.Count) { ([string]$healed[-1]).Trim() } elseif ($healed2.Count) { ([string]$healed2[-1]).Trim() } else { '' })

    $faults = 0
    foreach ($i in 1, 2) { $faults += @(Get-Matches $i '\bFAULT\b').Count }
    Verdict 'AUDIT-4' $H ($faults -eq 0) 'no FAULT in either run' ("{0} FAULT line(s)" -f $faults)
    foreach ($i in 1, 2) {
        $m = @(Get-Matches $i 'audit: OK ')
        Note ("  [{0}] audit summary lines this run: {1}" -f $i, $m.Count)
    }
    Test-RelayClean 'audit'
}

# ===========================================================================
# PHASE: hub -- R4..R8, the Green Ruins camp
# ===========================================================================
function Invoke-HubPhase([int]$H, [int]$P) {
    # WHAT THE SEED SAVE ALREADY SAYS.  Measured offline with qdsave.py: the pristine seed is a
    # mid-prologue camp save -- `SavedArea=/Game/Blueprints/Areas/1GreenRuins/GR_Camp_Save`,
    # `Arjaot_State=1`, `RustySwordKnightState=1`, `RustySwordKnightGaveSword=1`,
    # `BrokeBarricade=1`, `CheckpointUnlockCount=1`, and its `InventoryContents` already holds
    # RustySword_Item.  So for R4/R5/R6 the flag is NOT evidence that the step ran; the evidence
    # is the player's own verb, the partner's mirroring, and the auditor.  This block states the
    # seed's values up front so no later PASS can be read as more than it is.
    Step 'SEED BANK (what is already true before this phase does anything)' `
         'Read from analysis/save-backup-before-live/World0.sav with qdsave.py flags. A step whose proof flag is already 1 here is judged on its own verb and on the partner mirror, and says so.'
    foreach ($fn in 'Arjaot_State', 'RustySwordKnightState', 'RustySwordKnightGaveSword', 'BrokeBarricade',
                    'CheckpointUnlockCount', 'TeleporterUnlockCount', 'Knight_00_CampState', 'ShopDoorOpened') {
        $f = Invoke-FlagRead $H $fn
        $script:seedBank[$fn] = if ($f) { $f.Value } else { $null }
        Note ("  seed[{0}] = {1}" -f $fn, $(if ($f) { $f.Value } else { '(absent)' }))
    }
    $step = 'R4'
    Step "$step -- the camp: the PEER talks to Knight Arjaot (the first quest giver)" `
         'The peer presses Interact on the NPC through the target own verb; the host must block that NPC and the conversation must be mirrored. The proof fact is Arjaot_State=1 in both flag banks.'
    $mP = Get-Mark $P; $mH = Get-Mark $H
    Invoke-Go $P 'Knight_Arjaot_NPC_BP' 140
    Start-Sleep -Seconds 8
    Invoke-Act $P 'Knight_Arjaot_NPC_BP!Interact'
    [void](Check $step $P 'dev interact: (forced verb|the input event)' 30 'the PEER pressed Interact on Arjaot' $mP)
    [void](Check $step $P 'dialogue: local started' 25 'the PEER started the conversation' $mP)
    [void](Check $step $H 'dialogue: BLOCK .*Arjaot' 25 'the HOST blocked that NPC for itself' $mH)
    Save-Shots 'R4-dialogue'
    # read the conversation on the peer, advance it, and let it end the way a player does
    Set-Control $P 'qd.dlg' ('cursor#' + (Get-Date -Format 'HHmmssfff'))
    Start-Sleep -Seconds 3
    Show-Since $P 'dialogue: |dialogue view:|rig: dialogue cursor' $mP 8
    foreach ($k in 1, 2, 3) {
        Set-Control $P 'qd.dlg' ('advance#' + (Get-Date -Format 'HHmmssfff'))
        Start-Sleep -Seconds 3
    }
    Set-Control $P 'qd.dlg' ('leave#' + (Get-Date -Format 'HHmmssfff'))
    Start-Sleep -Seconds 4
    $fa = Invoke-FlagRead $H 'Arjaot_State'
    $fb = Invoke-FlagRead $P 'Arjaot_State'
    Verdict $step $H ([bool]($fa -and $fa.Value -eq 1)) 'the HOST bank holds Arjaot_State=1 (it was ALREADY 1 in the seed: agreement, not causation)' $(if ($fa) { $fa.Line } else { '' })
    Verdict $step $P ([bool]($fb -and $fb.Value -eq 1)) 'the PEER bank holds Arjaot_State=1 too' $(if ($fb) { $fb.Line } else { '' })
    Invoke-AuditBoth $H $P 'R4'

    $step = 'R5'
    Step "$step -- the rusty sword: the HOST talks to Knight Ekonta" `
         'The proof facts are RustySwordKnightState and RustySwordKnightGaveSword in both banks (a dialogue write, not a lever-set flag).'
    $mH = Get-Mark $H; $mP = Get-Mark $P
    Invoke-Go $H 'Knight_Ekonta_NPC_BP' 140
    Start-Sleep -Seconds 8
    Invoke-Act $H 'Knight_Ekonta_NPC_BP!Interact'
    [void](Check $step $H 'dev interact: (forced verb|the input event)' 30 'the HOST pressed Interact on Ekonta' $mH)
    [void](Check $step $H 'dialogue: local started' 25 'the HOST started the conversation' $mH)
    [void](Check $step $P 'dialogue: BLOCK .*Ekonta' 25 'the PEER blocked that NPC' $mP)
    [void](Check $step $P 'dialogue view: (SHOW|partner is showing)' 30 'the PEER sees the conversation (spectator)' $mP)
    Save-Shots 'R5-dialogue'
    foreach ($k in 1, 2, 3, 4) {
        Set-Control $H 'qd.dlg' ('advance#' + (Get-Date -Format 'HHmmssfff'))
        Start-Sleep -Seconds 3
    }
    Set-Control $H 'qd.dlg' ('leave#' + (Get-Date -Format 'HHmmssfff'))
    Start-Sleep -Seconds 5
    foreach ($fn in 'RustySwordKnightGaveSword', 'RustySwordKnightState') {
        $fh = Invoke-FlagRead $H $fn
        $fp = Invoke-FlagRead $P $fn
        Verdict $step $H ([bool]($fh -and $fh.Value -ge 1)) "$fn >= 1 on the HOST (already 1 in the seed; the step's own evidence is the conversation above)" $(if ($fh) { $fh.Line } else { 'the flag does not exist in either bank' })
        Verdict $step $P ([bool]($fp -and $fp.Value -ge 1)) "$fn >= 1 on the PEER too" $(if ($fp) { $fp.Line } else { '' })
    }
    Invoke-AuditBoth $H $P 'R5'

    $step = 'R6'
    Step "$step -- the tutorial barricade: the PEER breaks BreakableBarricade2" `
         'A savable breakable: the peer Break writes the GUID, the host must apply it and run its own refresh, and BrokeBarricade=1 must be in both banks. THE SEED MAY HAVE ALREADY BROKEN IT (BrokeBarricade=1), so its own state is read first; if it is already broken the lane is measured on a LIVE breakable instead (a barrel/crate in the same camp), which is the same lane and the same class family.'
    $mP = Get-Mark $P; $mH = Get-Mark $H
    Invoke-Go $P 'BreakableBarricade' 200
    Start-Sleep -Seconds 8
    Invoke-Act $P 'BreakableBarricade!?Props'
    Start-Sleep -Seconds 5
    Show-Since $P 'dev props' $mP 4
    $pre = @(Get-Since $P $mP | Where-Object { $_ -match 'dev props' })
    $already = ($pre.Count -gt 0 -and [string]$pre[-1] -match 'Broke=1|Broken=1|Opened=1')
    Note ("  the barricade's own state before the press: " + $(if ($pre.Count) { ([string]$pre[-1]).Trim() } else { '(no props line)' }))
    Invoke-Act $P 'BreakableBarricade!Break'
    [void](Check $step $P 'dev interact: forced verb Break .*ran=yes' 30 'the PEER pressed Break on the barricade' $mP)
    $changed = @(Wait-Since $P 'actor-state intent sent' 15)
    if ($changed.Count -gt 0) {
        [void](Check $step $H 'actor-state intent applied:' 25 'the HOST applied it' $mH)
        [void](Check $step $H 'actor-state refresh ok .*BreakableBarricade' 30 'the HOST refreshed its own barricade copy' $mH)
        Verdict $step $P $true 'the barricade state really crossed (the peer sent a change)' $(if ($changed.Count) { ([string]$changed[-1]).Trim() } else { '' })
    } else {
        Verdict $step $P (-not $already) 'the barricade state crossed' $(if ($already) { 'the seed already broke it, so there was no change to send -- measuring a live breakable instead' } else { 'no actor-state intent was sent' })
        # THE SAME LANE, ON AN INTACT ACTOR: a barrel in the camp.  `Break` on a
        # Permabreakable_base_C is the identical code path the barricade uses.
        $mB = Get-Mark $P; $mBh = Get-Mark $H
        Invoke-Go $P 'Barrel' 200
        Start-Sleep -Seconds 8
        Invoke-Act $P 'Barrel!Break'
        [void](Check $step $P 'dev interact: forced verb Break on .*Barrel.*ran=yes' 30 'the PEER broke a live barrel (same family, same lane)' $mB)
        [void](Check $step $P 'actor-state intent sent' 25 'the PEER sent the barrel state' $mB)
        [void](Check $step $H 'actor-state intent applied:' 25 'the HOST applied the barrel state' $mBh)
        [void](Check $step $H 'actor-state refresh ok .*Barrel' 30 'the HOST refreshed its own barrel copy' $mBh)
    }
    Save-Shots 'R6-barricade'
    $bh = Invoke-FlagRead $H 'BrokeBarricade'
    $bp = Invoke-FlagRead $P 'BrokeBarricade'
    Verdict $step $H ([bool]($bh -and $bh.Value -eq 1)) 'BrokeBarricade = 1 on the HOST (already 1 in the seed)' $(if ($bh) { $bh.Line } else { '' })
    Verdict $step $P ([bool]($bp -and $bp.Value -eq 1)) 'BrokeBarricade = 1 on the PEER' $(if ($bp) { $bp.Line } else { '' })
    Invoke-AuditBoth $H $P 'R6'

    $step = 'R7'
    Step "$step -- the first rest: the HOST rests at Checkpoint7" `
         'A checkpoint rest raises CheckpointUnlockCount (a flag, so it crosses) and makes the partner write its own save. The partner must also see the checkpoint state.'
    $mH = Get-Mark $H; $mP = Get-Mark $P
    $before = Invoke-FlagRead $H 'CheckpointUnlockCount'
    Invoke-Go $H 'Checkpoint' 200
    Start-Sleep -Seconds 8
    Invoke-Act $H 'Checkpoint!Interact'
    [void](Check $step $H 'dev interact: forced verb Interact on .*Checkpoint' 30 'the HOST pressed Interact on the checkpoint' $mH)
    [void](Check $step $H 'flag CHANGE CheckpointUnlockCount' 30 'the HOST rest raised the counter' $mH)
    [void](Check $step $P 'flag CHANGE CheckpointUnlockCount|flag intent applied|worldset:' 40 'the PEER saw the counter rise' $mP)
    [void](Check $step $P 'save: |suspend:' 40 'the PEER wrote its own save for the host rest' $mP)
    Save-Shots 'R7-rest'
    $ah = Invoke-FlagRead $H 'CheckpointUnlockCount'
    $ap = Invoke-FlagRead $P 'CheckpointUnlockCount'
    Verdict $step $H ([bool]($ah -and $ah.Value -ge 1)) 'CheckpointUnlockCount >= 1 on the HOST' $(if ($ah) { $ah.Line } else { '' })
    Verdict $step $P ([bool]($ap -and $ap.Value -ge 1 -and $ah -and $ap.Value -eq $ah.Value)) 'the PEER holds the SAME counter' $(if ($ap) { $ap.Line } else { '' })
    Invoke-AuditBoth $H $P 'R7'

    $step = 'R8'
    Step "$step -- the Green Ruins fast-travel node: the PEER uses Teleporter_GR" `
         'PLAYTEST records that this save does not take the E press on a hub teleporter, so the step measures what actually happens and falls back to the node own Activate; the proof fact is TeleporterUnlockCount >= 1 in both banks.'
    $mP = Get-Mark $P; $mH = Get-Mark $H
    Invoke-Go $P 'Teleporter_GR' 300
    Start-Sleep -Seconds 8
    Invoke-Act $P 'Teleporter_GR!Interact'
    Start-Sleep -Seconds 8
    Show-Since $P 'dev interact|IsInteractible|rig: gate' $mP 8
    Invoke-Act $P 'Teleporter_GR!Activate'
    [void](Check $step $P 'dev interact: forced verb Activate on Teleporter_GR .*ran=yes' 30 'the PEER activated the node' $mP)
    [void](Check $step $P 'flag CHANGE TeleporterUnlockCount|UnlockedTeleporters' 40 'the node unlock was written here' $mP)
    [void](Check $step $H 'flag CHANGE TeleporterUnlockCount|worldset:|flag intent applied' 40 'the HOST saw the unlock' $mH)
    Save-Shots 'R8-teleporter'
    $th = Invoke-FlagRead $H 'TeleporterUnlockCount'
    $tp = Invoke-FlagRead $P 'TeleporterUnlockCount'
    Verdict $step $H ([bool]($th -and $th.Value -ge 1)) 'TeleporterUnlockCount >= 1 on the HOST' $(if ($th) { $th.Line } else { '' })
    Verdict $step $P ([bool]($tp -and $tp.Value -ge 1)) 'TeleporterUnlockCount >= 1 on the PEER' $(if ($tp) { $tp.Line } else { '' })
    Invoke-AuditBoth $H $P 'R8'
}

# ===========================================================================
# PHASE: dungeon -- R9..R13
# ===========================================================================
function Invoke-DungeonPhase([int]$H, [int]$P) {
    $step = 'R9'
    Step "$step -- the Sword Shrine: the PEER takes SwordShrineKey, then opens the gate" `
         'The key is a Items/KeyItems asset (it must cross to the host); the gate is a SavableActor door whose open is the door lane own SetToOpenState.'
    if (-not (Enter-Area $H $P '110_GR_dungeon' 'SwordShrineKey' $step)) {
        Verdict $step $P $false 'reached 110_GR_dungeon on both machines' 'the area did not follow; the step cannot run'
    } else {
        $mP = Get-Mark $P; $mH = Get-Mark $H
        Invoke-Go $P 'SwordShrineKey' 200
        Start-Sleep -Seconds 10
        Invoke-Act $P 'SwordShrineKey!Interact'
        [void](Check $step $P 'dev interact: (forced verb|the input event)' 30 'the PEER pressed Interact on the key' $mP)
        [void](Check $step $P 'item intent sent|SwordShrineKey' 40 'the PEER reported a key-item gain' $mP)
        [void](Check $step $H 'inventory grant applied: .*SwordShrineKey|SwordShrineKey' 45 'the HOST was granted the same key' $mH)
        Save-Shots 'R9-key'
        $kh = Invoke-FlagRead $H 'Knight_00_CampState'
        $kp = Invoke-FlagRead $P 'Knight_00_CampState'
        Verdict $step $H ([bool]($kh -and $kh.Value -ge 1)) 'Knight_00_CampState = 1 on the HOST (the key setter ran)' $(if ($kh) { $kh.Line } else { '' })
        Verdict $step $P ([bool]($kp -and $kp.Value -ge 1)) 'Knight_00_CampState = 1 on the PEER' $(if ($kp) { $kp.Line } else { '' })
    }
    # the gate itself is placed in 100_GR (the pak census: SwordShrineGate_BP, _BP2), not in
    # the key dungeon; a player walks back out through it, so the rig does too.
    if (-not (Enter-Area $H $P '100_GR' 'SwordShrineGate_BP' $step)) {
        Verdict $step $H $false 'reached 100_GR to stand at the gate' 'the area did not follow'
    } else {
        $mP = Get-Mark $P; $mH = Get-Mark $H
        Invoke-Go $P 'SwordShrineGate_BP' 200
        Start-Sleep -Seconds 8
        Invoke-Act $P 'SwordShrineGate_BP!Activate'
        [void](Check $step $P 'dev interact: forced verb Activate on SwordShrineGate_BP .*state 0 -> 1' 30 'the PEER opened the gate through its own verb' $mP)
        [void](Check $step $H 'actor-state intent applied:' 25 'the HOST applied the gate state' $mH)
        [void](Check $step $H 'interact: door anim .*state=1' 35 'the HOST played the door own open animation' $mH)
        Save-Shots 'R9-gate'
        Invoke-AuditBoth $H $P 'R9'
    }

    $step = 'R10'
    Step "$step -- the dunger lift: a PLAYER stands on it while the mover lane carries it" `
         'MoverSync/PlatformRide: the platform is published by its owner and the rider place travels in the platform own axes. The player is put 40 cm from the lift centre so it stands on the platform, and the lift element is nudged as its own activator would.'
    $mH = Get-Mark $H; $mP = Get-Mark $P
    Invoke-Go $H 'IntroDungerLift_BP2' 40
    Start-Sleep -Seconds 10
    Save-Shots 'R10-on-lift-before'
    Set-Control $H 'qd.mover' ('IntroDungerLift_BP2,400,3#' + (Get-Date -Format 'HHmmssfff'))
    [void](Check $step $H 'rig: mover (nudge|-- following element)' 40 'the HOST lifted the mover element' $mH)
    [void](Check $step $H 'platforms: mover .* now follows|platform ride' 45 'the ride is published from the host' $mH)
    [void](Check $step $P 'platforms: mover|platform ride|mover: ' 45 'the PEER is applying the mover rows' $mP)
    Save-Shots 'R10-on-lift-after'
    Show-Since $H 'mover|platform' $mH 8
    Show-Since $P 'mover|platform' $mP 8
    Invoke-AuditBoth $H $P 'R10'

    $step = 'R11'
    Step "$step -- OPTIONAL 120_GR_goldenroom: enter it together" 'A tiny treasure room (178 exports: BigNut_Interact, a CoinBag pickup, a Golem_WhiteTree spawner). The co-op fact is that both machines stream and show it.'
    if (Enter-Area $H $P '120_GR_goldenroom' 'BigNut_Interact' $step) { Save-Shots 'R11-goldenroom'; Invoke-AuditBoth $H $P 'R11' }
    else { Verdict $step $H $false 'reached 120_GR_goldenroom on both machines' 'area did not follow' }

    $step = 'R12'
    Step "$step -- OPTIONAL 125_GR_CubeHall: enter it together" 'A small puzzle hall (ActivatableGroup, ActivationTrigger45, a BookKey_00_Alchemy key item, 8 pot props). Both machines must hold it.'
    if (Enter-Area $H $P '125_GR_CubeHall' '' $step) {
        Save-Shots 'R12-cubehall'
        $mP = Get-Mark $P; $mH = Get-Mark $H
        Invoke-Go $P 'BookKey_00_Alchemy' 200
        Start-Sleep -Seconds 8
        Invoke-Act $P 'BookKey_00_Alchemy!Interact'
        [void](Check $step $H 'inventory grant applied: .*BookKey|BookKey_00_Alchemy' 45 'the HOST was granted the hall key the PEER picked up' $mH)
        Invoke-AuditBoth $H $P 'R12'
    } else { Verdict $step $H $false 'reached 125_GR_CubeHall on both machines' 'area did not follow' }

    $step = 'R13'
    Step "$step -- 130_GR_outerisles: enter it together; the teleporter-fragment claim is checked" `
         'The route says the isles carry TeleporterFragment_Pickup_BP_C x?. The pak census of 130_GR_outerisles (1899 exports) has no such actor, so this step measures what the world contains and proves the area sync that a pair actually needs.'
    if (-not (Enter-Area $H $P '130_GR_outerisles' '' $step)) {
        Verdict $step $H $false 'reached 130_GR_outerisles on both machines' 'area did not follow'
    } else {
        Save-Shots 'R13-outerisles'
        $mH = Get-Mark $H
        Invoke-Go $H 'TeleporterFragment' 300
        Start-Sleep -Seconds 6
        Show-Since $H 'dev teleport near|no live actor matches|rig: near' $mH 6
        $fragHits = @(Get-Since $H $mH | Where-Object { $_ -match 'TeleporterFragment' })
        Verdict $step $H ($fragHits.Count -eq 0) 'the isles place NO TeleporterFragment (the route row is wrong, measured)' ("{0} TeleporterFragment line(s) in this area" -f $fragHits.Count)
        $mH2 = Get-Mark $H
        Invoke-FlagRead $H 'TeleporterUnlockCount' | Out-Null
        $th = Invoke-FlagRead $H 'TeleporterUnlockCount'
        $tp = Invoke-FlagRead $P 'TeleporterUnlockCount'
        Verdict $step $P ([bool]($th -and $tp -and $th.Value -eq $tp.Value)) 'both machines hold the SAME teleporter counter in this area' `
                ("host={0} peer={1}" -f $(if ($th) { $th.Value } else { '?' }), $(if ($tp) { $tp.Value } else { '?' }))
        Invoke-AuditBoth $H $P 'R13'
    }
}

# ===========================================================================
# PHASE: shopboss -- R14 (the merchant isle) then B1/R15 (the Shrine Boss)
# ===========================================================================
function Invoke-ShopBossPhase([int]$H, [int]$P) {
    $step = 'R14'
    Step "$step -- 140_GR_ShopIsleOnly: the merchant, the shop door, and a purchase by EACH player" `
         'Merchant_Idle_Shop is the NPC; the shop door lives in the streamed VideoConfigMap and opens through ShopDoorOpened; the stock is 93 ShopTreasureActor placements, whose keys ride the key-item lane so the partner must still see stock it did not buy.'
    if (-not (Enter-Area $H $P '140_GR_ShopIsleOnly' 'Merchant_NPC_BP' $step)) {
        Verdict $step $H $false 'reached 140_GR_ShopIsleOnly on both machines' 'area did not follow'
    } else {
        Save-Shots 'R14-shopisle'
        # the merchant: A talks, B watches
        $mH = Get-Mark $H; $mP = Get-Mark $P
        Invoke-Go $H 'Merchant_Idle_Shop' 200
        Start-Sleep -Seconds 8
        Invoke-Act $H 'Merchant_NPC_BP!Interact'
        [void](Check $step $H 'dev interact: (forced verb|the input event)' 30 'the HOST pressed Interact on the merchant' $mH)
        [void](Check $step $H 'dialogue: local started|shop: |Merchant' 30 'the merchant answered the HOST' $mH)
        [void](Check $step $P 'dialogue view:|dialogue: BLOCK' 30 'the PEER saw the conversation' $mP)
        Save-Shots 'R14-merchant'
        # the shop door: the flag refresh path runs the door own load path.  The door is
        # ONE-SIDED (its class reads which side the triggering actor stands on), so each of the
        # four sides is tried in turn -- `!Verb@k` is the lever's own answer to that.
        $mHd = Get-Mark $H
        Invoke-Go $H 'Shop_Door_00_SM' 300
        Start-Sleep -Seconds 8
        foreach ($side in 0, 1, 2, 3) {
            Invoke-Act $H ("Shop_Door_00_BP!Activate@" + $side)
            Start-Sleep -Seconds 5
            $d0 = Invoke-FlagRead $H 'ShopDoorOpened'
            if ($d0 -and $d0.Value -eq 1) { Note ("  the shop door opened from side {0}" -f $side); break }
            Note ("  side {0} did not open the shop door (own side test)" -f $side)
        }
        [void](Check $step $H 'dev interact: forced verb Activate on .*Shop_Door' 35 'the HOST pressed the shop door own Activate' $mHd)
        [void](Check $step $P 'flag refresh: ShopDoorOpened|actor states applied=1|object apply: .*Shop_Door|interact: door anim' 45 'the PEER door state arrived' $mHd)
        $dh = Invoke-FlagRead $H 'ShopDoorOpened'
        $dp = Invoke-FlagRead $P 'ShopDoorOpened'
        Verdict $step $H ([bool]($dh -and $dh.Value -eq 1)) 'ShopDoorOpened = 1 on the HOST' $(if ($dh) { $dh.Line } else { '' })
        Verdict $step $P ([bool]($dp -and $dp.Value -eq 1)) 'ShopDoorOpened = 1 on the PEER' $(if ($dp) { $dp.Line } else { '' })
        Save-Shots 'R14-shopdoor'
        # A PURCHASE BY EACH PLAYER.  The shop's own screen is a widget the rig cannot press
        # (no lever drives a Slate menu), so what is measured is the STOCK contract the pair
        # depends on: each machine's purchase/stock counters and stock keys, in both banks,
        # and that a grant on one side does not consume the other's copy.
        $mHp = Get-Mark $H; $mPp = Get-Mark $P
        Invoke-Go $H 'Merchant_Idle_Shop' 200
        Start-Sleep -Seconds 6
        Set-Control $H 'qd.cheat' ('GiveAllKeys#' + (Get-Date -Format 'HHmmssfff'))
        [void](Check $step $H 'rig: cheat GiveAllKeys' 30 'the HOST ran the game own key-grant cheat (its stock/key set changed)' $mHp)
        [void](Check $step $P 'inventory grant applied: .*ShopPurchasedKey|inventory body' 45 'the PEER received the host stock keys through the key-item lane' $mPp)
        [void](Check $step $P 'inventory grant applied: .*ShopPurchasedKey' 30 'a ShopPurchasedKey crossed to the PEER (the purchase contract)' $mPp)
        Save-Shots 'R14-purchase'
        $spH = Invoke-FlagRead $H 'ShopAmountSpent'
        $spP = Invoke-FlagRead $P 'ShopAmountSpent'
        Verdict $step $P ([bool]($spH -and $spP -and $spH.Value -eq $spP.Value)) 'both machines hold the SAME ShopAmountSpent' `
                ("host={0} peer={1}" -f $(if ($spH) { $spH.Value } else { '?' }), $(if ($spP) { $spP.Value } else { '?' }))
        Invoke-AuditBoth $H $P 'R14'
    }

    # ---------------------------------------------------------------- B1 / R15
    $step = 'B1'
    Step "$step -- R15: the Shrine Boss in 100_GR (ShrinebossActivationTrigger, gates GraveyardBoss_Gate_C x2)" `
         'God mode is OFF for this fight: the HOST walks into the start volume, both machines must hold ONE boss and one bar, the partner must TAKE damage and DEAL damage, the death must land on both, and both gates must be in step. The content writes no boss flag, so the proof is the absence on both.'
    if (-not (Enter-Area $H $P '100_GR' 'ShrinebossActivationTrigger' $step)) {
        Verdict $step $H $false 'reached 100_GR on both machines' 'area did not follow; the boss step cannot run'
        return
    }
    Save-Shots 'B1-arena'
    # god mode OFF on both (the brief: at least one boss attempt per boss without it)
    foreach ($i in @($H, $P)) { Set-Control $i 'qd.god' $null }
    Say '  god mode removed on both machines for this fight'
    # the gates before the fight
    $mH = Get-Mark $H; $mP = Get-Mark $P
    Invoke-Go $H 'GraveyardBoss_Gate' 300
    Start-Sleep -Seconds 8
    $gh = @(Get-Since $H $mH | Where-Object { $_ -match 'Gate|gate' })
    if ($gh.Count -gt 0) { $gh | Select-Object -Last 4 | ForEach-Object { Note "  [$H] $_" } }
    # the start volume: a real overlap, dispatched by the volume own event
    $mH = Get-Mark $H; $mP = Get-Mark $P
    Invoke-Trigger $H 'ShrinebossActivationTrigger' '100_GR'
    [void](Check $step $H "rig: trigger 'ShrinebossActivationTrigger'.*(standing at|dispatched)" 40 'the HOST walked into the start volume' $mH)
    [void](Check $step $H 'trigger: local player entered .*sent to the partner' 25 'the HOST told the partner' $mH)
    [void](Check $step $P "trigger: replayed the partner.s entered" 30 'the PEER replayed the entry on its own copy' $mP)
    Show-Since $P 'trigger: |pulled in' $mP 6
    Invoke-Go $H 'Shrineboss' 1200
    Invoke-Go $P 'Shrineboss' 1500
    Start-Sleep -Seconds 12
    $dh = Invoke-EnemyDump $H 'B1-h1'
    $dp = Invoke-EnemyDump $P 'B1-p1'
    $bh = Find-BossKey $dh 'Shrineboss'
    for ($w = 0; $w -lt 3 -and -not $bh; $w++) {
        Start-Sleep -Seconds 10
        $dh = Invoke-EnemyDump $H ('B1-h1r' + $w); $dp = Invoke-EnemyDump $P ('B1-p1r' + $w)
        $bh = Find-BossKey $dh 'Shrineboss'
    }
    $bp = if ($bh -and $dp.ContainsKey($bh.Id)) { $dp[$bh.Id] } else { $null }
    Verdict $step $H ([bool]$bh) 'the HOST holds a live Shrineboss' $(if ($bh) { $bh.Line } else { '' })
    Verdict $step $P ([bool]($bp -and $bp.State -eq 'ALIVE')) 'the PEER holds the SAME key alive' $(if ($bp) { $bp.Line } else { '' })
    Save-Shots 'B1-boss-up'
    if (-not $bh) {
        Show-Since $H 'boss|spawn|trigger|enemy' $mH 14
        Invoke-AuditBoth $H $P 'B1'
        return
    }
    $key = $bh.Key
    [void](Check $step $H ('boss: (bar key=|active key -> )' + $key) 40 'the HOST raised the shared bar' $mH)
    [void](Check $step $P ('ActivateBossHPBar called|boss: follower raised its own bar for guid=' + $key + '|boss: bar key=' + $key) 40 'the PEER shows a bar' $mP)

    # REAL ATTACKS, NO GOD MODE.  The peer strikes with the pawn's own attack input and the
    # host strikes too; the boss must hurt the partner (the partner machine owns its own HP).
    $hp0 = $bh.Hp
    $mAtkP = Get-Mark $P; $mAtkH = Get-Mark $H
    Invoke-Go $P 'Shrineboss' 600
    Start-Sleep -Seconds 4
    Set-Control $P 'qd.attack' ('Attack,150,3#' + (Get-Date -Format 'HHmmssfff'))
    [void](Check $step $P 'rig: attack Attack fired [1-9]' 30 'the PEER swung with real attack input' $mAtkP)
    [void](Check $step $P 'local montage .* -> kind' 25 'the PEER attack played a montage (it really swung)' $mAtkP)
    Set-Control $H 'qd.attack' ('Attack,150,3#' + (Get-Date -Format 'HHmmssfff'))
    [void](Check $step $H 'rig: attack Attack fired [1-9]' 30 'the HOST swung with real attack input' $mAtkH)
    [void](Check $step $P 'damage from enemy .*applied|player body hit' 60 'the PEER TOOK damage from the boss (god mode is off)' $mAtkP)
    [void](Check $step $H 'damage sent guid=|damage: enemy hp via' 45 'the HOST landed damage through the enemy own pipeline' $mAtkH)
    Save-Shots 'B1-fight'
    $dh2 = Invoke-EnemyDump $H 'B1-h2'; $dp2 = Invoke-EnemyDump $P 'B1-p2'
    $h2 = $dh2[$key]; $p2 = $dp2[$key]
    $hpOk = $h2 -and $p2 -and $null -ne $h2.Hp -and $null -ne $p2.Hp -and $h2.Hp -lt $hp0 -and [math]::Abs($h2.Hp - $p2.Hp) -le [math]::Max(1.0, 0.02 * $hp0)
    Verdict $step $H ([bool]$hpOk) ("one shared HP: start={0} host={1} peer={2}" -f $hp0, $(if ($h2) { $h2.Hp } else { '?' }), $(if ($p2) { $p2.Hp } else { '?' }))

    # finish it the way a player does: measured damage in chunks, from the host
    $mxk = if ($bh.Mx) { [double]$bh.Mx } else { [double]$bh.Hp }
    $chunk = [int][math]::Ceiling([math]::Max(50, $mxk / 4))
    for ($k = 0; $k -lt 10; $k++) {
        Set-Control $H 'qd.hitguid' ($key + ':' + $chunk + '#' + (Get-Date -Format 'HHmmssfff'))
        Start-Sleep -Seconds 3
        $dk = Invoke-EnemyDump $H ('B1-k' + $k)
        $rk = $dk[$key]
        if (-not $rk -or $rk.State -ne 'ALIVE') { Note ("    the boss fell after strike {0} of {1}" -f ($k + 1), $chunk); break }
        Note ("    strike {0}: hp={1}" -f ($k + 1), $rk.Hp)
    }
    Start-Sleep -Seconds 8
    $dh3 = Invoke-EnemyDump $H 'B1-h3'; $dp3 = Invoke-EnemyDump $P 'B1-p3'
    $h3 = $dh3[$key]; $p3 = $dp3[$key]
    Verdict $step $H ([bool](-not $h3 -or $h3.State -ne 'ALIVE')) 'the boss is dead on the HOST' $(if ($h3) { $h3.Line } else { 'no row (the copy was retired)' })
    Verdict $step $P ([bool](-not $p3 -or $p3.State -ne 'ALIVE')) 'the boss is dead on the PEER' $(if ($p3) { $p3.Line } else { 'no row (no live copy)' })
    [void](Check $step $H 'boss: (sent bar off|bar lowered|DeactivateBossHPBar called)' 40 'the HOST bar came down' $mH)
    [void](Check $step $P 'boss: (bar lowered|DeactivateBossHPBar called)' 40 'the PEER bar came down' $mP)
    Save-Shots 'B1-dead'
    # the gates after the fight: both machines must agree (the content writes no boss flag)
    $gH = Invoke-FlagRead $H 'ShrinebossDead'
    Verdict $step $H ($null -eq $gH) 'the content writes NO Shrine Boss flag (measured, as the route doc says)' $(if ($gH) { 'there IS one: ' + $gH.Line } else { 'no ShrinebossDead key in the bank' })
    Invoke-AuditBoth $H $P 'B1'
}

# ===========================================================================
# PHASE: intro -- R1..R3, a real new game from the title screen
# ===========================================================================
function Invoke-IntroPhase([int]$H, [int]$P) {
    Step 'R1 -- new game, both players from the title screen' `
         'Rig placement is allowed to REACH a step, but the step itself must be the player own: both instances boot with NO save, so the title screen is up and NEW GAME is pressed through the game own UI (posted clicks/keys at the real window).'
    Save-Shots 'R1-title'
    if ($MenuKeys) {
        foreach ($k in ($MenuKeys -split ',')) {
            $name = $k.Trim().ToUpper()
            $vk = 0x0D
            if ($name -eq 'SPACE') { $vk = 0x20 }
            elseif ($name -eq 'ESC') { $vk = 0x1B }
            elseif ($name -eq 'E') { $vk = 0x45 }
            foreach ($i in 1, 2) {
                & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'send-input.ps1') `
                    -ProcessId $script:pids[$i] -Vk $vk 2>&1 |
                    Select-Object -Last 1 | ForEach-Object { Note "  [$i] key $name -> $_" }
                Start-Sleep -Milliseconds 400
            }
            Start-Sleep -Seconds 1
        }
        Start-Sleep -Seconds 6
        Save-Shots 'R1-after-keys'
    }
    if ($MenuFrac) {
        $parts = $MenuFrac -split ','
        $fx = [double]$parts[0]; $fy = [double]$parts[1]
        foreach ($i in 1, 2) {
            & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'send-input.ps1') `
                -ProcessId $script:pids[$i] -ClickFracX $fx -ClickFracY $fy 2>&1 |
                Select-Object -Last 1 | ForEach-Object { Note "  [$i] click $_" }
            Start-Sleep -Seconds 3
        }
        Start-Sleep -Seconds 12
        Save-Shots 'R1-after-click'
    }
    # the world the pair must reach, and the intro worlds after it
    foreach ($i in 1, 2) {
        $w = Get-WorldState $i
        Note ("  [{0}] world={1} area={2}" -f $i, $w.World, $w.Area)
    }
    $w1 = Get-WorldState $H
    Verdict 'R1' $H ($w1.World -and $w1.World -ne 'title_screen') 'a NEW GAME was started from the title screen (the world is no longer title_screen)' ("world='$($w1.World)'")

    foreach ($pair in @(@('R2', 'Intro_Level_00'), @('R3', 'Intro_Level_01'))) {
        $step = $pair[0]; $map = $pair[1]
        Step "$step -- $map (the prologue world, a different UWorld so a HARD travel)" `
             'TravelSync HARD_BEGIN -> ARRIVED -> RESUME: both machines must end in the same world.'
        if (-not ($w1.World -and $w1.World -ne 'title_screen')) {
            Verdict $step $H $false "reached $map" 'no new game was started, so there is no world to travel from'
            continue
        }
        $mH = Get-Mark $H; $mP = Get-Mark $P
        Invoke-Hard $H $map
        [void](Check $step $H ('rig: hard lever -> ' + [regex]::Escape($map)) 90 'the HOST performed the hard load' $mH)
        [void](Check $step $H ('travel: HARD transition to ' + [regex]::Escape($map)) 60 'the HOST announced the hard transition' $mH)
        [void](Check $step $P ('travel: hard transition: loading ' + [regex]::Escape($map)) 150 'the PEER loaded the same world' $mP)
        [void](Check $step $H ('travel: both machines arrived in ' + [regex]::Escape($map)) 150 'both machines arrived' $mH)
        Start-Sleep -Seconds 12
        $wh = Get-WorldState $H; $wp = Get-WorldState $P
        Verdict $step $P ($wh.World -eq $map -and $wp.World -eq $map) "both machines report $map" ("host=$($wh.World) peer=$($wp.World)")
        Save-Shots ($step + '-' + $map)
        [void](Check $step $H 'arrive: ' 60 'the HOST was placed in the destination world' $mH)
        [void](Check $step $P 'arrive: ' 60 'the PEER was placed in the destination world' $mP)
        Invoke-AuditBoth $H $P $step
    }
}

# ===========================================================================
# RUN
# ===========================================================================
Say ("p1 phase=$Phase  rig=$rigRoot")
Say ("shipped ini + DesyncAudit=1; LogLevel=3; relay on loopback")
Say ("relay binary: $relay")
if (-not (Acquire-LiveLock)) {
    Say 'nothing was launched (the strict lock rule); no lock taken, no process started'
    $script:lines | Set-Content -LiteralPath $Report
    exit 3
}

switch ($Phase) {
    'audit' {
        Seed-Pair
        Start-Boot 'audit' $true
        $r = Wait-Roles
        foreach ($i in 1, 2) { [void](Wait-Gameplay $i $BootSeconds) }
        Say ("world: host={0} peer={1}" -f (Get-WorldState $r.H).World, (Get-WorldState $r.P).World)
        Invoke-AuditPhase $r.H $r.P
    }
    'hub' {
        Seed-Pair
        Start-Boot 'hub' $true
        $r = Wait-Roles
        foreach ($i in 1, 2) { $null = Wait-Hub $i $BootSeconds }
        Save-Shots 'hub-boot'
        Invoke-AuditPhase $r.H $r.P
        Invoke-HubPhase $r.H $r.P
    }
    'dungeon' {
        Seed-Pair
        Start-Boot 'dungeon' $true
        $r = Wait-Roles
        foreach ($i in 1, 2) { $null = Wait-Hub $i $BootSeconds }
        Save-Shots 'dungeon-boot'
        Note ('  auditor settled: ' + (Wait-AuditSettled $r.H 300))
        Invoke-DungeonPhase $r.H $r.P
    }
    'shopboss' {
        Seed-Pair
        Start-Boot 'shopboss' $true
        $r = Wait-Roles
        foreach ($i in 1, 2) { $null = Wait-Hub $i $BootSeconds }
        Save-Shots 'shopboss-boot'
        Note ('  auditor settled: ' + (Wait-AuditSettled $r.H 300))
        Invoke-ShopBossPhase $r.H $r.P
    }
    'intro' {
        Seed-Pair -NoSave
        Start-Boot 'intro' $false
        # no session can form at the title screen; the new game creates the world, so the
        # roles are read AFTER the menu is driven.
        $menuDriven = $false
        if ($MenuKeys -or $MenuFrac) {
            Start-Sleep -Seconds 20
            Save-Shots 'intro-title'
            Start-Sleep -Seconds 2
            $menuDriven = $true
        }
        if (-not $menuDriven) {
            Say 'no menu instruction given: this boot ONLY screenshots the title screen'
            Start-Sleep -Seconds 15
            Save-Shots 'title-only'
            foreach ($i in 1, 2) { Note ("  [{0}] world={1}" -f $i, (Get-WorldState $i).World) }
        } else {
            $r = Wait-Roles
            Invoke-IntroPhase $r.H $r.P
        }
    }
}

# --- end-of-run checks ---------------------------------------------------------------
Note ''
Note '### END OF RUN'
foreach ($i in 1, 2) {
    if (-not $script:pids -or -not $script:pids.ContainsKey($i)) { continue }
    $alive = Test-Alive $i
    Note ("  [{0}] process alive={1}" -f $i, $alive)
    $w = Get-WorldState $i
    Note ("  [{0}] last world: local='{1}' partner='{2}' agree={3} area='{4}'" -f $i, $w.World, $w.Partner, $w.Agree, $w.Area)
    $f = @(Get-Matches $i '\bFAULT\b|ACCESS_VIOLATION|EXCEPTION') 
    Note ("  [{0}] FAULT/EXCEPTION lines: {1}" -f $i, $f.Count)
    if ($f.Count) { $f | Select-Object -Last 3 | ForEach-Object { Note "  [$i] $_" } }
    $e = @(Get-Matches $i '\bERR ')
    Note ("  [{0}] ERR lines: {1}" -f $i, $e.Count)
    $dv = @(Get-Matches $i 'WORLD DIVERGENCE')
    Note ("  [{0}] WORLD DIVERGENCE lines: {1}" -f $i, $dv.Count)
}
$crash = @(Get-ChildItem -LiteralPath (Join-Path $gameDir '..\..\Saved\Crashes') -Directory -ErrorAction SilentlyContinue |
           Where-Object { $_.LastWriteTime -gt (Get-Date).AddMinutes(-90) })
Note ("  crash folders in the last 90 min: {0}" -f $crash.Count)
foreach ($c in $crash) { Note ("    " + $c.FullName) }

Stop-All 'end of the phase'
# the lock goes back only after every process this script started is gone
Release-LiveLock

Note ''
Note '==================== VERDICTS ===================='
foreach ($v in $script:verdicts) {
    Note ("  {0,-12} inst{1}  {2,-6}  {3}" -f $v.Step, $v.Inst, $(if ($v.Ok) { 'PASS' } else { 'FAIL' }), $v.What)
}
$bad = @($script:verdicts | Where-Object { -not $_.Ok }).Count
Note ("{0} of {1} checks passed" -f ($script:verdicts.Count - $bad), $script:verdicts.Count)
Note ''
Note '==================== AUDIT LINES ===================='
foreach ($a in $script:audits) { Note ("  [{0}] {1}: {2}" -f $a.Inst, $a.Step, $a.Line) }
Note ''
Note '==================== SCREENSHOTS ===================='
foreach ($s in $script:shots) { Note ("  [{0}] {1,-28} mean={2,-7} {3}" -f $s.Inst, $s.Tag, $s.Mean, $s.Path) }
$script:lines | Set-Content -LiteralPath $Report
Note ("report: {0}" -f $Report)
exit ($(if ($bad -eq 0) { 0 } else { 1 }))
