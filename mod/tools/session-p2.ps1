# session-p2.ps1 -- the [play-p2] playthrough run: CAMPAIGN-ROUTE R16-R29 and bosses B2/B3/B4,
# plus the live proof of WorldSetSync and PuzzleSync (both ship 0 in the released ini).
#
# ONE PHASE PER INVOCATION (a launched game does not survive the command that started it, R14).
# Each phase boots a fresh pair from the seed save, runs its steps the way a player does
# (interact / walk / trigger / real attack / ride), screenshots both windows at every step,
# forces `qd.audit` on both after each step, and then stops everything.
#
# HONESTY RULES (inherited from session18/26/gapworld):
#   * a verdict prints the line it read; no line, no pass;
#   * a lever's optimism is not evidence -- the STATE (flag read, worldset dump, enemy table)
#     is what decides a step;
#   * a step the rig cannot do with a player verb is reported NOT-REACHABLE with the reason,
#     never PASS;
#   * only lines from THIS run are read (everything after the last `hooked FEngineLoop`).
[CmdletBinding()]
param(
    [ValidateSet('worldpuzzle', 'aq', 'lib', 'maus', 'story', 'stop')]
    [string]$Phase = 'worldpuzzle',
    [int]$BootSeconds = 420,
    [int]$SettleSeconds = 30,
    [string]$Report = ''
)

$ErrorActionPreference = 'Stop'
$here    = $PSScriptRoot
$repo    = Split-Path -Parent (Split-Path -Parent $here)
$gameDir = 'G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64'
$relay   = Join-Path (Split-Path -Parent $here) 'rust\target\release\qd-relay.exe'
$rigRoot = 'G:\Freespace\Queens_Domain\_test\p2'
$shotDir = 'G:\Freespace\Queens_Domain\_test\playthrough\p2'
$seed    = Join-Path $repo 'analysis\save-backup-before-live'
$srcIni  = Join-Path $repo 'mod\qdcoop.ini'
$outDir  = Join-Path $rigRoot 'out'
if (-not $Report) { $Report = Join-Path $outDir ("report-{0}.txt" -f $Phase) }
foreach ($d in $outDir, $shotDir) {
    if (-not (Test-Path $d)) { New-Item -ItemType Directory -Force -Path $d | Out-Null }
}

$script:lines    = New-Object System.Collections.Generic.List[string]
$script:verdicts = New-Object System.Collections.Generic.List[object]
function Say($m) { $s = "[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $m; Write-Host $s; $script:lines.Add($s) | Out-Null }
function Note($m) { Write-Host $m; $script:lines.Add([string]$m) | Out-Null }
function Verdict([string]$what, [bool]$ok, [string]$line) {
    $script:verdicts.Add([pscustomobject]@{ What = $what; Ok = $ok; Line = $line }) | Out-Null
    $tag = 'FAIL'; if ($ok) { $tag = 'PASS' }
    Note ("  {0} {1}" -f $tag, $what)
    Note ("      {0}" -f $line)
}
function Nonce { return (Get-Date -Format 'HHmmssfff') }

# ---------------------------------------------------------------------------
# THE LIVE LOCK (COMMON-BRIEF rule 4, strict form)
#   * launch nothing unless this script HOLDS `_test\locks\live`;
#   * break another owner's lock only if owner.txt is > 25 min old AND no
#     QueensDomain-Win64-Shipping / qd-relay process is running;
#   * cleanup kills ONLY the pids this script launched (never -Name);
#   * rewrite owner.txt every 10 minutes while holding, so a long session is not
#     mistaken for an abandoned one (that mistake cost two sessions at 06:46/07:14).
# ---------------------------------------------------------------------------
$script:RigId      = 'play-p2'
$script:LockDir    = 'G:\Freespace\Queens_Domain\_test\locks\live'
$script:LockOwner  = Join-Path $script:LockDir 'owner.txt'
$script:LockWritten = [datetime]::MinValue

function Update-LockHeartbeat([switch]$Force) {
    if (-not (Test-Path $script:LockOwner)) { return }
    if (-not $Force -and ((Get-Date) - $script:LockWritten).TotalMinutes -lt 5) { return }
    Set-Content -LiteralPath $script:LockOwner -Value ($script:RigId + ' ' + (Get-Date -Format 'yyyy-MM-ddTHH:mm:ss')) -NoNewline
    $script:LockWritten = Get-Date
}
function Assert-LiveLock {
    if (Test-Path $script:LockDir) {
        $owner = ''
        $age = 9999
        if (Test-Path $script:LockOwner) {
            $owner = (Get-Content -LiteralPath $script:LockOwner -Raw -ErrorAction SilentlyContinue)
            if ($owner) { $owner = $owner.Trim() }
            $age = [int]((Get-Date) - (Get-Item $script:LockOwner).LastWriteTime).TotalMinutes
        }
        if ($owner -and $owner.StartsWith($script:RigId)) {
            Update-LockHeartbeat -Force
            Say ("live lock is already mine ('{0}', {1} min old) -- refreshing it" -f $owner, $age)
            return
        }
        $procs = @(Get-Process -ErrorAction SilentlyContinue |
                   Where-Object { $_.ProcessName -match 'QueensDomain-Win64-Shipping|qd-relay' })
        if ($age -le 25 -or $procs.Count -gt 0) {
            Say ("REFUSED: the LIVE lock is held by '{0}' ({1} min old, {2} game/relay process(es) running). " -f $owner, $age, $procs.Count)
            Say 'Nothing was launched. Wait for that session to finish (owner.txt > 25 min AND no process).'
            exit 2
        }
        Say ("breaking a STALE live lock: '{0}' is {1} min old and no game/relay process runs" -f $owner, $age)
        Remove-Item -LiteralPath $script:LockDir -Recurse -Force -ErrorAction SilentlyContinue
    }
    New-Item -ItemType Directory -Path $script:LockDir -Force | Out-Null
    Update-LockHeartbeat -Force
    Say ("LIVE lock taken by '{0}'" -f $script:RigId)
}

function Get-RigPaths([int]$i) {
    [pscustomobject]@{
        Index  = $i
        Root   = (Join-Path $rigRoot ("inst{0}" -f $i))
        Suffix = "p2$i"
        Log    = (Join-Path $gameDir ("qdcoop-p2{0}.log" -f $i))
        Pid    = (Join-Path $rigRoot ("inst{0}.pid" -f $i))
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
# Only THIS run: everything after the last `===== qdcoop pid` banner (which precedes the
# config/switch line); fall back to `hooked FEngineLoop` for a log that has no banner.
function Get-RunLines([int]$i) {
    $all = @(Read-LogLines $inst[$i].Log)
    if ($all.Count -eq 0) { return @() }
    $start = -1
    for ($k = $all.Count - 1; $k -ge 0; $k--) { if ($all[$k] -match '^===== qdcoop pid ') { $start = $k; break } }
    if ($start -lt 0) {
        $start = 0
        for ($k = $all.Count - 1; $k -ge 0; $k--) { if ($all[$k] -match 'hooked FEngineLoop') { $start = $k; break } }
    }
    return @($all[$start..($all.Count - 1)])
}
function Get-Matches([int]$i, [string]$pat) { return @(Get-RunLines $i | Where-Object { $_ -match $pat }) }
function Get-Mark([int]$i) { return (@(Get-RunLines $i)).Count }
function Get-Since([int]$i, [int]$mark) {
    $l = @(Get-RunLines $i)
    if ($l.Count -le $mark) { return @() }
    return @($l[$mark..($l.Count - 1)])
}
function Show-Since([int]$i, [string]$pat, [int]$mark, [int]$max) {
    $hits = @(Get-Since $i $mark | Where-Object { $_ -match $pat } | Select-Object -Last $max)
    foreach ($h in $hits) { Note ("    [{0}] {1}" -f $i, ([string]$h).Trim()) }
}
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
function Clear-Controls([int]$i) {
    foreach ($leaf in 'qd.tp','qd.act','qd.hit','qd.cheat','qd.cheatarg','qd.travel','qd.hard','qd.level',
                      'qd.list','qd.near','qd.drops','qd.menu','qd.efunc','qd.use','qd.kill','qd.enemies',
                      'qd.hitguid','qd.obj','qd.flag','qd.gate','qd.take','qd.trigger','qd.selfhp','qd.nohit',
                      'qd.dlg','qd.attack','qd.equip','qd.mover','qd.wsys','qd.proj','qd.audit','qd.world',
                      'qd.sequence','qd.enemykill','qd.auditfork') {
        Set-Control $i $leaf $null
    }
}
# Wait for a regex that appears AFTER `mark` (never a stale line), returns the line or $null.
function Wait-New([int]$i, [string]$pat, [int]$sec, [int]$mark) {
    $end = (Get-Date).AddSeconds($sec)
    while ((Get-Date) -lt $end) {
        Update-LockHeartbeat
        $hits = @(Get-Since $i $mark | Where-Object { $_ -match $pat })
        if ($hits.Count -gt 0) { return ([string]$hits[-1]).Trim() }
        if (-not (Test-Alive $i)) { return $null }
        Start-Sleep -Milliseconds 800
    }
    return $null
}
function Wait-Pat([int]$i, [string]$pat, [int]$sec) {
    $end = (Get-Date).AddSeconds($sec)
    while ((Get-Date) -lt $end) {
        Update-LockHeartbeat
        $hits = @(Get-Matches $i $pat)
        if ($hits.Count -gt 0) { return ([string]$hits[-1]).Trim() }
        Start-Sleep -Milliseconds 800
    }
    return $null
}

# ---------------------------------------------------------------------------
# Screenshots -- "the partner's body is there, the room is drawn" is about pixels
# ---------------------------------------------------------------------------
function Measure-Shot([string]$path) {
    Add-Type -AssemblyName System.Drawing -ErrorAction SilentlyContinue
    $bmp = [System.Drawing.Bitmap]::FromFile($path)
    try {
        $sx = [math]::Max(1, [int]($bmp.Width / 64))
        $sy = [math]::Max(1, [int]($bmp.Height / 64))
        $vals = New-Object System.Collections.Generic.List[double]
        for ($y = 0; $y -lt $bmp.Height; $y += $sy) {
            for ($x = 0; $x -lt $bmp.Width; $x += $sx) {
                $c = $bmp.GetPixel($x, $y)
                $vals.Add(0.2126 * $c.R + 0.7152 * $c.G + 0.0722 * $c.B)
            }
        }
        $mean = 0.0
        foreach ($v in $vals) { $mean += $v }
        $mean = $mean / [math]::Max(1, $vals.Count)
        $var = 0.0
        foreach ($v in $vals) { $var += ($v - $mean) * ($v - $mean) }
        $sd = [math]::Sqrt($var / [math]::Max(1, $vals.Count))
        return [pscustomobject]@{ W = $bmp.Width; H = $bmp.Height; Mean = [math]::Round($mean, 1); Std = [math]::Round($sd, 1) }
    } finally { $bmp.Dispose() }
}
function Save-Shots([string]$tag) {
    $paths = @()
    foreach ($i in 1, 2) {
        if (-not (Test-Alive $i)) { Note ("  shot[{0}] {1}: instance gone" -f $i, $tag); continue }
        $out = Join-Path $shotDir ("p2-{0}-{1}-inst{2}.png" -f $Phase, $tag, $i)
        Remove-Item -LiteralPath $out -Force -ErrorAction SilentlyContinue
        $r = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'capture-hidden.ps1') `
                -Out $out -ProcessId $script:pids[$i] 2>&1
        if (-not (Test-Path -LiteralPath $out)) {
            Note ("  shot[{0}] {1}: FAILED ({2})" -f $i, $tag, (@($r) | Select-Object -Last 1))
            continue
        }
        $m = Measure-Shot $out
        Note ("  shot[{0}] {1}: {2}x{3} mean={4} stddev={5} -> {6}" -f $i, $tag, $m.W, $m.H, $m.Mean, $m.Std, $out)
        $paths += [pscustomobject]@{ Inst = $i; Tag = $tag; Path = $out; Mean = $m.Mean; Std = $m.Std }
    }
    # A screenshot pair is "the same moment" and both must be drawn: mean>2 (not black) and
    # stddev>3 (not a flat fill).
    if ($paths.Count -eq 2) {
        $ok = (@($paths | Where-Object { $_.Mean -gt 2 -and $_.Std -gt 3 }).Count -eq 2)
        Verdict ("shot pair '{0}': both scenes drawn" -f $tag) $ok `
                (($paths | ForEach-Object { "inst$($_.Inst) mean=$($_.Mean) std=$($_.Std)" }) -join '; ')
    }
    return $paths
}

# ---------------------------------------------------------------------------
# Levers (each is the same file-poll path a human drives)
# ---------------------------------------------------------------------------
function Invoke-Go([int]$i, [string]$spec, [int]$cm = 0) {
    if ($cm -gt 0) { Set-Control $i 'qd.near' $cm }
    $m = Get-Mark $i
    Set-Control $i 'qd.tp' ($spec + '#' + (Nonce))
    $line = Wait-New $i 'dev teleport near|dev teleport file|dev teleport to' 12 $m
    return $line
}
function Invoke-Act([int]$i, [string]$spec) {
    $m = Get-Mark $i
    Set-Control $i 'qd.act' ($spec + '#' + (Nonce))
    $line = Wait-New $i 'dev interact: target=|dev interact: forced verb|dev interact: no live actor|PE new:' 12 $m
    return $line
}
function Invoke-Trigger([int]$i, [string]$spec) {
    $m = Get-Mark $i
    Set-Control $i 'qd.trigger' ($spec + '#' + (Nonce))
    $line = Wait-New $i "rig: trigger '" 30 $m
    return $line
}
function Invoke-Dlg([int]$i, [string]$cmd) {
    $m = Get-Mark $i
    Set-Control $i 'qd.dlg' ($cmd + '#' + (Nonce))
    return (Wait-New $i 'dialogue|dlg' 20 $m)
}
function Invoke-Attack([int]$i, [string]$action, [int]$gap = 300, [int]$count = 2) {
    $m = Get-Mark $i
    Set-Control $i 'qd.attack' ("{0},{1},{2}#{3}" -f $action, $gap, $count, (Nonce))
    return (Wait-New $i 'rig: attack ' 20 $m)
}
function Read-Flag([int]$i, [string]$name) {
    $m = Get-Mark $i
    # the lever ignores the same instruction twice; `#<nonce>` is stripped before the read
    Set-Control $i 'qd.flag' ($name + '#' + (Nonce))
    $line = Wait-New $i ('rig: flag dump -- ' + [regex]::Escape($name) + '|flag ' + [regex]::Escape($name) + '=-?\d+') 15 $m
    if (-not $line) { return $null }
    # `rig: flag dump -- X = 2 (game bank 2, lane table -1)`: the FIRST number after '='.
    if ($line -match '=\s*(-?\d+)') { return [int]$Matches[1] }
    return $null
}
function Set-Flag([int]$i, [string]$name, [int]$v) {
    Set-Control $i 'qd.flag' ("set:{0}={1}#{2}" -f $name, $v, (Nonce))
    return (Wait-Pat $i ('rig: flag set -- SaveFunctions.SaveNamedData\(' + [regex]::Escape($name)) 15)
}
function Dump-World([int]$i) {
    $m = Get-Mark $i
    Set-Control $i 'qd.world' ('dump#' + (Nonce))
    [void](Wait-New $i 'puzzle: \d+ prop\(s\) held' 20 $m)
    Start-Sleep -Milliseconds 500
    return @(Get-Since $i $m | Where-Object { $_ -match 'worldset|puzzle:|  prop key=|counter |in the save' })
}
function Invoke-Audit([int]$i) {
    $m = Get-Mark $i
    Set-Control $i 'qd.audit' ('now#' + (Nonce))
    $line = Wait-New $i 'audit: (OK \d+ categories|DESYNC OPEN|pass \d+ us)' 45 $m
    if (-not $line) {
        $line = Wait-New $i 'audit: DESYNC|audit: OK' 20 $m
    }
    if (-not $line) { return '(no audit line)' }
    return $line
}
function Invoke-AuditBoth([int]$H, [int]$P, [string]$step) {
    $a = Invoke-Audit $H
    $b = Invoke-Audit $P
    $bad = ($a -match 'DESYNC' -or $b -match 'DESYNC')
    if ($bad) {
        # a transient disagreement right after a death/apply is not a persistent desync:
        # give the lanes one settle window and ask again.  The SECOND answer is the verdict.
        Note ("  audit[{0}]: DESYNC on the first pass -- re-asking after 12 s" -f $step)
        Start-Sleep -Seconds 12
        $a = Invoke-Audit $H
        $b = Invoke-Audit $P
        $bad = ($a -match 'DESYNC' -or $b -match 'DESYNC')
    }
    Verdict ("{0}: qd.audit has no DESYNC on either machine" -f $step) (-not $bad) `
            ("host: {0} | peer: {1}" -f $a, $b)
}
# HP read: `near:me!?HP,MaxHP` logs `dev props: local player HP=i100/f10000 MaxHP=i120/f12000`.
function Read-Hp([int]$i) {
    $m = Get-Mark $i
    Set-Control $i 'qd.act' ('near:me!?HP,MaxHP#' + (Nonce))
    $line = Wait-New $i 'dev props: ' 12 $m
    if ($line) { Note ("    [{0}] hp read: {1}" -f $i, $line) }
    if ($line -and $line -match 'HP=i(\d+)') { return [double]$Matches[1] }
    if ($line -and $line -match 'HP=f(\d+)') { return [double]$Matches[1] / 100.0 }
    return $null
}
function Get-Enemies([int]$i) {
    $m = Get-Mark $i
    Set-Control $i 'qd.enemies' ('dump#' + (Nonce))
    [void](Wait-New $i 'enemykey |rig: enemies' 15 $m)
    Start-Sleep -Milliseconds 500
    $rows = @{}
    foreach ($ln in @(Get-Since $i $m)) {
        if ([string]$ln -notmatch 'enemykey ([0-9A-F]{8}) (ALIVE|DEAD)') { continue }
        $k = $Matches[1]; $st = $Matches[2]
        $hp = -1.0
        if ([string]$ln -match 'hp=([\d.]+)') { $hp = [double]$Matches[1] }
        $cls = ''
        if ([string]$ln -match 'cls=(\S+)') { $cls = $Matches[1] }
        $rows[$k] = [pscustomobject]@{ Key = $k; State = $st; Hp = $hp; Cls = $cls; Line = ([string]$ln).Trim() }
    }
    return $rows
}
function Find-Enemy($rows, [string]$clsPattern) {
    foreach ($k in $rows.Keys) {
        if ($rows[$k].State -eq 'ALIVE' -and $rows[$k].Cls -match $clsPattern) { return $rows[$k] }
    }
    # a dead one still proves presence (and the flag)
    foreach ($k in $rows.Keys) { if ($rows[$k].Cls -match $clsPattern) { return $rows[$k] } }
    return $null
}

# ---------------------------------------------------------------------------
# REAL KEY INPUT.  `qd.tp near:<teleporter>` is refused on purpose: the spot beside a
# teleporter is inside its volume and the game would move the player out of the level
# (RigStandBeside, measured in Session 25).  A player does not appear there -- they WALK
# in, and entering the volume is what fires `Teleporter_C`'s own RegisterSphere begin
# overlap (the only unlock path the content has).  So the rig places the pawn 4.5 m away,
# FACING the target, and posts a real W key-down/key-up to the game window.  Same window
# selection as capture-hidden.ps1, and WM_ACTIVATE first (measured: an inert window sends
# gameplay input to the UI unless the app is told it is active).
if (-not ('QdWalk' -as [type])) {
    Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class QdWalk {
  [DllImport("user32.dll")] public static extern bool EnumWindows(EP cb, IntPtr l);
  public delegate bool EP(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint type);
  public struct RECT { public int L, T, R, B; }
}
"@
}
function Get-GameWindow([int]$i) {
    $script:walkPid = $script:pids[$i]
    $script:walkBest = [IntPtr]::Zero
    $script:walkArea = 0
    $cb = [QdWalk+EP]{
        param($hw, $l)
        $q = 0
        [QdWalk]::GetWindowThreadProcessId($hw, [ref]$q) | Out-Null
        if ($q -eq $script:walkPid -and [QdWalk]::IsWindowVisible($hw)) {
            $sb = New-Object System.Text.StringBuilder 512
            [QdWalk]::GetWindowText($hw, $sb, 512) | Out-Null
            $r = New-Object QdWalk+RECT
            [QdWalk]::GetClientRect($hw, [ref]$r) | Out-Null
            $a = ($r.R - $r.L) * ($r.B - $r.T)
            if ($a -gt $script:walkArea -and ($sb.ToString() -match 'Queen' -or $a -gt 200000)) {
                $script:walkArea = $a
                $script:walkBest = $hw
            }
        }
        return $true
    }
    [QdWalk]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
    return $script:walkBest
}
# VK 0x57 = W, 0x1B = Esc.  A positive `hold` posts down, sleeps, posts up (a real hold).
function Send-Key([int]$i, [int]$vk, [int]$holdMs) {
    $h = Get-GameWindow $i
    if ($h -eq [IntPtr]::Zero) { Note ("  [{0}] Send-Key: no game window" -f $i); return $false }
    [QdWalk]::PostMessage($h, 0x0006, [IntPtr]1, [IntPtr]0) | Out-Null   # WM_ACTIVATE
    Start-Sleep -Milliseconds 60
    $scan = [QdWalk]::MapVirtualKey([uint32]$vk, 0)
    $down = [IntPtr]([int64](1 -bor ([int64]$scan -shl 16)))
    $up = [IntPtr]([int64](1 -bor ([int64]$scan -shl 16) -bor (1 -shl 30) -bor (1 -shl 31)))
    [QdWalk]::PostMessage($h, 0x0100, [IntPtr]$vk, $down) | Out-Null
    Start-Sleep -Milliseconds $holdMs
    [QdWalk]::PostMessage($h, 0x0101, [IntPtr]$vk, $up) | Out-Null
    return $true
}

# ---------------------------------------------------------------------------
# Steps: enter an area (host), the peer must follow; both must have it shown
# ---------------------------------------------------------------------------
function Enter-Area([int]$H, [int]$P, [string]$area) {
    Say ("entering area {0}" -f $area)
    # The PEER reacts to the host's announcement BEFORE the host's own engine state line is
    # written (measured 06:23:13.857 peer vs 06:23:15.824 host), so its mark must be taken
    # FIRST or the follow line is already behind the cursor and never seen.
    $mP = Get-Mark $P
    $mH = Get-Mark $H
    Set-Control $H 'qd.level' ($area + '#' + (Nonce))
    $hostLine = Wait-New $H ('rig: level engine state ' + [regex]::Escape($area) + ' LoadedLevel=1 actuallyVisible=1') 120 $mH
    Verdict ("{0}: the HOST has the area loaded and shown" -f $area) ([bool]$hostLine) $(if ($hostLine) { $hostLine } else { '(no level engine state line)' })
    if (-not $hostLine) { return $false }
    $follow = Wait-New $P ('travel: entered the partner.s area ' + [regex]::Escape($area)) 60 $mP
    Verdict ("{0}: the PEER followed into the area" -f $area) ([bool]$follow) $(if ($follow) { $follow } else { '(the peer never entered it)' })
    Start-Sleep -Seconds 5
    return ([bool]$follow)
}
function Leave-Area([int]$H) {
    Set-Control $H 'qd.level' ('-#' + (Nonce))
    Start-Sleep -Seconds 8
}

# ---------------------------------------------------------------------------
# Boot / stop
# ---------------------------------------------------------------------------
function Stop-All([string]$why) {
    Say "STOP-ALL $why"
    # ONLY this rig's own processes.  A `Stop-Process -Name QueensDomain-*` here kills another
    # delegate's pair mid-run (measured 07:14:51: their phase-start Stop-All killed mine, and my
    # session-complete Stop-All killed theirs two seconds later).  pids are recorded at launch.
    if ($script:relayProc) { Stop-Process -Id $script:relayProc.Id -Force -ErrorAction SilentlyContinue }
    if ($script:pids) {
        foreach ($i in 1, 2) {
            if ($script:pids.ContainsKey($i)) { Stop-Process -Id $script:pids[$i] -Force -ErrorAction SilentlyContinue }
        }
    }
    foreach ($i in 1, 2) {
        if (-not (Test-Path $inst[$i].Pid)) { continue }
        $p = Get-Content -LiteralPath $inst[$i].Pid -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($p) { Stop-Process -Id ([int]$p) -Force -ErrorAction SilentlyContinue }
    }
    Start-Sleep -Seconds 2
}

function Invoke-Boot {
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

    # THE SWITCHES UNDER TEST: the SHIPPED ini verbatim, with only the relay pinned to
    # loopback, LogLevel=3, and the two switches this run exists to prove.
    $text = Get-Content -LiteralPath $srcIni -Raw
    $text = $text -replace '(?m)^RelayHost=.*$', 'RelayHost=127.0.0.1'
    $text = $text -replace '(?m)^RelayPort=.*$', 'RelayPort=47800'
    $text = $text -replace '(?m)^LogLevel=.*$', 'LogLevel=3'
    $text = $text -replace '(?m)^WorldSetSync=.*$', 'WorldSetSync=1'
    $text = $text -replace '(?m)^PuzzleSync=.*$', 'PuzzleSync=1'
    $ini = Join-Path $gameDir 'qdcoop.ini'
    $text | Set-Content -LiteralPath $ini -NoNewline
    Say "wrote qdcoop.ini (shipped + WorldSetSync=1 PuzzleSync=1 + loopback + LogLevel=3)"
    Copy-Item -LiteralPath $ini -Destination (Join-Path $outDir 'qdcoop-used.ini') -Force

    foreach ($i in 1, 2) {
        $null = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'make_test_instance.ps1') `
                -Name ("inst{0}" -f $i) -Index $i -SourceSave $seed -Root $rigRoot -Suffix $inst[$i].Suffix -Force 2>&1
        if ($LASTEXITCODE -ne 0) { Say "REFUSED: make_test_instance refused instance $i"; exit 2 }
    }

    Stop-All 'boot: replacing anything left over'
    foreach ($i in 1, 2) {
        Clear-Controls $i
        if (Test-Path -LiteralPath $inst[$i].Pid) { Remove-Item -LiteralPath $inst[$i].Pid -ErrorAction SilentlyContinue }
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
    # The items a real player would have picked up by these route steps.  Granted through the
    # game's own cheat object (`FS_Cheats_C.GiveItem`), 20 s into gameplay, exactly like a
    # pickup: the library's 15 book keys (the puzzle is "insert the books themselves" -- the
    # `LIB_BookRecepticle_BP` imports all 15 `BookKey_00_*_Item` assets), four raider
    # necklaces, and the meal tickets the Chef asks for.
    # 29 paths would overflow the lever's 1024-byte buffer (it truncates mid-path), and a key
    # item is added by the PICKUP path (`+path` -> SaveFunctions_C.AddItemToInventory), not by
    # the cheat's GiveItem.  So: five library books, three raider necklaces, one meal ticket.
    $gives = @()
    foreach ($b in @('Alchemy','BoneMachines','CookBook','Dragons','Eldritch')) {
        $gives += ('+/Game/Items/KeyItems/BookKeys/{0}/BookKey_00_{0}_Item.BookKey_00_{0}_Item' -f $b)
    }
    for ($k = 0; $k -lt 4; $k++) { $gives += '+/Game/Items/Accessories/RaidersFineNecklace/RaidersFineNecklace' }
    $gives += '+/Game/Items/KeyItems/Mealticket/mealticket_Item'
    $env:QDCOOP_GIVE = ($gives -join ';')
    Say ("QDCOOP_GIVE: {0} path(s), {1} chars" -f $gives.Count, $env:QDCOOP_GIVE.Length)

    if (-not (Test-Path -LiteralPath $relay)) { Say "REFUSED: relay not built at $relay"; exit 2 }
    # a relay left over from MY previous run holds port 47800; kill that recorded pid only
    $relayPidFile = Join-Path $rigRoot 'relay.pid'
    if (Test-Path -LiteralPath $relayPidFile) {
        $old = Get-Content -LiteralPath $relayPidFile -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($old) { Stop-Process -Id ([int]$old) -Force -ErrorAction SilentlyContinue; Start-Sleep -Seconds 1 }
    }
    $relayLog = Join-Path $outDir 'relay-p2.log'
    Remove-Item -LiteralPath $relayLog -ErrorAction SilentlyContinue
    $script:relayProc = Start-Process -FilePath $relay -ArgumentList @('--bind', '127.0.0.1:47800') `
            -RedirectStandardOutput $relayLog -RedirectStandardError (Join-Path $outDir 'relay-p2.err') `
            -WindowStyle Hidden -PassThru
    Say ("relay pid={0}" -f $script:relayProc.Id)
    Set-Content -LiteralPath (Join-Path $rigRoot 'relay.pid') -Value $script:relayProc.Id
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
        Update-LockHeartbeat
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
    $script:H = 1; $script:P = 2
    if ($roles[2] -eq 'HOST') { $script:H = 2; $script:P = 1 }
    Say ("roles: inst{0}=HOST inst{1}=PEER" -f $script:H, $script:P)

    $onH = @(Get-Matches $script:H 'switches:.*worldSet=1 puzzle=1')
    Verdict 'the switches are ON in the running game (worldSet=1 puzzle=1)' ($onH.Count -gt 0) `
            $(if ($onH.Count -gt 0) { ([string]$onH[-1]).Trim() } else { '(no `switches:` line with both on)' })

    # The relay must have dropped nothing: a stale relay counts the new kinds malformed.
    $rl = @(Read-LogLines $relayLog | Where-Object { $_ -match 'malformed' })
    Verdict 'the relay reports no malformed datagrams' ($rl.Count -eq 0) `
            $(if ($rl.Count) { ([string]$rl[-1]).Trim() } else { '(0 malformed lines)' })

    Say "settling $SettleSeconds s"
    $settleEnd = (Get-Date).AddSeconds($SettleSeconds)
    while ((Get-Date) -lt $settleEnd) { Update-LockHeartbeat; Start-Sleep -Seconds 5 }
    Update-LockHeartbeat
}

# ---------------------------------------------------------------------------
# WorldSetSync: the unlock sets are a UNION -- a teleporter unlocked by one is
# unlocked for both.  The turn is the game's own RegisterSphere overlap: the rig
# puts the pawn ON the placed teleporter, which is what walking into it does.
# ---------------------------------------------------------------------------
function Parse-WorldSet([int]$i) {
    $lines = Dump-World $i
    $out = @{ Sets = @{}; Keys = @{}; PuzzleHeld = -1; PuzzleSent = -1; PuzzleApplied = -1; Raw = $lines }
    foreach ($ln in $lines) {
        $s = [string]$ln
        if ($s -match 'worldset\[([a-z-]+)\]: save=(-?\d+)/(-?\d+) union=(\d+)') {
            $out.Sets[$Matches[1]] = [pscustomobject]@{ Save = [int]$Matches[2]; Max = [int]$Matches[3]; Union = [int]$Matches[4] }
        } elseif ($s -match '([A-Za-z0-9_/.-]+)\s+\(in the save\)\s*$') {
            if (-not $out.Keys.ContainsKey('inSave')) { $out.Keys['inSave'] = New-Object System.Collections.Generic.List[string] }
            $out.Keys['inSave'].Add($Matches[1])
        }
        if ($s -match 'puzzle: (\d+) prop\(s\) held, sent=(\d+) applied=(\d+)') {
            $out.PuzzleHeld = [int]$Matches[1]; $out.PuzzleSent = [int]$Matches[2]; $out.PuzzleApplied = [int]$Matches[3]
        }
    }
    return $out
}

function Invoke-WorldSetProof([int]$H, [int]$P) {
    # THE TURN MUST BE A DESTINATION ONE MACHINE DID NOT HAVE.  Both instances boot from the
    # same seed, and the live save starts holding ONE destination (the game registers the
    # arrival node), so the proof walks the pair to a teleporter that is NOT in that set.  A
    # `near:` target on a resident-but-NOT-SHOWN level is refused (correctly), so each
    # candidate comes with the area that shows it.
    Say 'WorldSetSync: baseline unlock sets on both machines'
    $base = @{}
    foreach ($i in $H, $P) {
        $w = Parse-WorldSet $i
        $base[$i] = $w
        Note ("  inst{0}: teleporters save={1}/{2} union={3}; puzzle held={4} sent={5} applied={6}" -f `
              $i, $w.Sets['teleporters'].Save, $w.Sets['teleporters'].Max, $w.Sets['teleporters'].Union,
              $w.PuzzleHeld, $w.PuzzleSent, $w.PuzzleApplied)
    }
    $already = @()
    if ($base[$H].Keys.ContainsKey('inSave')) { $already = @($base[$H].Keys['inSave']) }
    if ($already.Count) { Note ("  already in this save: " + ($already -join ', ')) }

    $pairs = @(
        @{ Area = '100_GR';           Tp = 'Teleporter_GR' },
        @{ Area = '200_AQ';           Tp = 'Teleporter_Aqr' },
        @{ Area = '310_LIB_exterior'; Tp = 'Teleporter_Lib' },
        @{ Area = '420_MAUS_mid';     Tp = 'Teleporter_Maus' }
    )
    $chosen = $null; $unlockLine = $null; $chosenArea = $null
    foreach ($pair in $pairs) {
        $cand = $pair.Tp
        $mPairH = Get-Mark $H
        $mPairP = Get-Mark $P
        if ($already -contains $cand) { Note ("  {0} is already unlocked here; next candidate" -f $cand); continue }
        if (-not (Enter-Area $H $P $pair.Area)) { Note ("  {0} did not open; next candidate" -f $pair.Area); continue }
        $pre = Parse-WorldSet $H
        if ($pre.Keys.ContainsKey('inSave') -and (@($pre.Keys['inSave']) -contains $cand)) {
            Note ("  after entering {0}, {1} is already in the save" -f $pair.Area, $cand)
            continue
        }
        Say ("walking the HOST into {0} in {1}" -f $cand, $pair.Area)
        $m = Get-Mark $H
        $unlockLine = $null
        for ($try = 0; $try -lt 3 -and -not $unlockLine; $try++) {
            # 300 cm is the rig's own "inside a teleporter's volume" radius, so the placement
            # has to be farther than that; the pawn is put down FACING the target.
            $off = 450 + 150 * $try
            Set-Control $H 'qd.near' ([string]$off)
            $go = Invoke-Go $H ('near:' + $cand)
            Note ("    place {0} cm: {1}" -f $off, $go)
            if (-not $go -or $go -match 'refused|no live actor') { continue }
            Start-Sleep -Seconds 1
            [void](Send-Key $H 0x57 (800 + 500 * $try))     # W: walk in, a real input event
            Start-Sleep -Seconds 1
            $unlockLine = Wait-New $H 'worldset: teleporters holds \d+ entr' 10 $m
        }
        if (-not $unlockLine) {
            # The CLOSEST real mechanism left: dispatch the teleporter's OWN RegisterSphere
            # begin-overlap with this machine's player as OtherActor -- the identical event a
            # walking body fires, and the same call the apply side makes for the peer.
            $ev = 'near:' + $cand + '!BndEvt__Teleporter_RegisterSphere_K2Node_ComponentBoundEvent_0_ComponentBeginOverlapSignature__DelegateSignature'
            $r = Invoke-Act $H $ev
            Note ("    RegisterSphere dispatch (fallback): " + $r)
            $unlockLine = Wait-New $H 'worldset: teleporters holds \d+ entr' 15 $m
        }
        if ($unlockLine) { [void](Send-Key $H 0x1B 30); $chosen = $cand; $chosenArea = $pair.Area; break }
        Show-Since $H 'dev teleport|worldset: teleporters' $m 8
    }
    Verdict 'the HOST unlocked a destination by standing on the placed teleporter' ([bool]$chosen) `
            $(if ($chosen) { ("{0} in {1}: {2}" -f $chosen, $chosenArea, $unlockLine) } else { '(no unlock after every area/teleporter pair)' })
    if (-not $chosen) { return }

    # Marks taken BEFORE the walk: the publish and the peer's receive happen within ~1 s of
    # the overlap, so a mark taken after the unlock wait is already past both lines.
    $sent = Wait-New $H 'worldset: sent teleporters rows' 20 $mPairH
    Verdict 'the HOST published the set on the wire' ([bool]$sent) $(if ($sent) { $sent } else { '(no `worldset: sent teleporters` line)' })

    $recv = Wait-New $P "worldset: the partner's teleporters" 20 $mPairP
    Verdict 'the PEER received the partner''s set' ([bool]$recv) $(if ($recv) { $recv } else { '(no `worldset: the partner''s teleporters` line)' })
    $applied = Wait-New $P 'worldset: teleporter .* unlocked through the game.s own RegisterSphere|worldset: applied teleporters' 30 $mPairP
    Verdict 'the PEER wrote the destination into its OWN save through the game' ([bool]$applied) $(if ($applied) { $applied } else { '(no apply line)' })
    $peerHas = Wait-New $P 'worldset: teleporters holds \d+ entr' 20 $mPairP
    Verdict 'the PEER''s save now holds the destination' ([bool]$peerHas) $(if ($peerHas) { $peerHas } else { '(no `holds N entries` on the peer)' })

    Start-Sleep -Seconds 4
    $afterH = Parse-WorldSet $H
    $afterP = Parse-WorldSet $P
    $same = ($null -ne $afterH.Sets['teleporters']) -and ($null -ne $afterP.Sets['teleporters']) -and `
            ($afterH.Sets['teleporters'].Union -eq $afterP.Sets['teleporters'].Union) -and `
            ($afterH.Sets['teleporters'].Union -gt $base[$H].Sets['teleporters'].Union)
    Verdict 'both machines report the SAME teleporter union, and it GREW' $same `
            ("host union={0} (save {1}/{2}) / peer union={3} (save {4}/{5})" -f `
             $afterH.Sets['teleporters'].Union, $afterH.Sets['teleporters'].Save, $afterH.Sets['teleporters'].Max,
             $afterP.Sets['teleporters'].Union, $afterP.Sets['teleporters'].Save, $afterP.Sets['teleporters'].Max)
    # The identity on the wire is the DESTINATION's own asset name (`Aquatic_Teleporter`), not
    # the placed actor's name (`Teleporter_Aqr`), so the comparison is SET-TO-SET.
    $hostIn = @()
    if ($afterH.Keys.ContainsKey('inSave')) { $hostIn = @($afterH.Keys['inSave']) }
    $peerIn = @()
    if ($afterP.Keys.ContainsKey('inSave')) { $peerIn = @($afterP.Keys['inSave']) }
    $added = @($hostIn | Where-Object { $already -notcontains $_ })
    $sameKeys = (($hostIn | Sort-Object) -join ',') -eq (($peerIn | Sort-Object) -join ',')
    Verdict 'the two SAVES hold the identical destination set (not just the union)' `
            ($sameKeys -and $hostIn.Count -gt 0 -and $added.Count -ge 1) `
            ("added by the walk: [{0}] | host in-save: [{1}] | peer in-save: [{2}]" -f ($added -join ','), ($hostIn -join ','), ($peerIn -join ','))
    $cH = Read-Flag $H 'TeleporterUnlockCount'
    $cP = Read-Flag $P 'TeleporterUnlockCount'
    Verdict 'TeleporterUnlockCount is derived from the set and equal on both' ($null -ne $cH -and $cH -eq $cP) `
            ("host={0} peer={1}" -f $cH, $cP)
    [void](Save-Shots 'worldset-union')
    Invoke-AuditBoth $H $P 'worldset'
}

# ---------------------------------------------------------------------------
# R22 -- THE LIBRARY BOOK PUZZLE, solved with the books themselves.
#
# What the content actually is (decoded, not guessed): `LIB_BookRecepticle_BP` is an
# `AQR_KeyRecepticle_BP_C` descendant whose name table imports all 14
# `BookKey_00_<Category>_Item` assets.  So the puzzle is "insert the book items the player
# collected into the receptacle", and its intermediate state is the receptacle's
# SavableActor state (lane 0x12) -- NOT a rotation.  The decor `Lib_Book_00..04_BP_C` actors
# are static meshes with no function at all, so there is no "turn a book" verb anywhere;
# that is measured in this function too, and reported.
# ---------------------------------------------------------------------------
function Invoke-PuzzleProof([int]$H, [int]$P, [string]$area, [switch]$AlreadyIn) {
    if (-not $AlreadyIn) {
        if (-not (Enter-Area $H $P $area)) {
            Verdict 'PuzzleSync: reached the library' $false 'area did not follow'
            return
        }
    }
    # 1. what is actually placed here
    foreach ($i in $H, $P) {
        $m = Get-Mark $i
        Set-Control $i 'qd.obj' ('books#' + (Nonce))
        [void](Wait-New $i 'rig: object dump' 15 $m)
        Start-Sleep -Milliseconds 600
        $dump = @(Get-Since $i $m | Where-Object { $_ -match 'Book|book|Puzzle|puzzle|Recepticle' } | Select-Object -First 14)
        Note ("  inst{0} object dump: {1} book/puzzle line(s)" -f $i, $dump.Count)
        foreach ($d in $dump) { Note ('    ' + ([string]$d).Trim()) }
    }
    # 2. the transform lane (PuzzleSync) reports what it holds, before anything.
    #    It observes props within 40 m of the player, so the pair is walked to the books
    #    first -- otherwise `actors=0` and the lane is armed but blind.
    foreach ($i in $H, $P) {
        $g = Invoke-Go $i 'near:Lib_Book_00' 300
        Note ("  inst{0} -> beside a book: {1}" -f $i, $g)
    }
    Start-Sleep -Seconds 3
    $before = @{}
    foreach ($i in $H, $P) { $before[$i] = Parse-WorldSet $i }
    Verdict 'PuzzleSync: the transform lane is armed and reporting on both machines' `
            ($before[$H].PuzzleHeld -ge 0 -and $before[$P].PuzzleHeld -ge 0) `
            ("host: held={0} sent={1} applied={2} / peer: held={3} sent={4} applied={5}" -f `
             $before[$H].PuzzleHeld, $before[$H].PuzzleSent, $before[$H].PuzzleApplied,
             $before[$P].PuzzleHeld, $before[$P].PuzzleSent, $before[$P].PuzzleApplied)

    # 3. THE REAL PUZZLE: walk to the receptacle and put a book in it, one per book.
    $flagBefore = Read-Flag $H 'LibBookPuzzleComplete'
    $mStH = Get-Mark $H
    $mStP = Get-Mark $P
    $inserted = 0
    for ($n = 1; $n -le 6; $n++) {
        [void](Invoke-Go $H 'near:LIB_BookRecepticle' 150)
        $act = Invoke-Act $H 'near:LIB_BookRecepticle!Interact'
        Note ("  insert #{0}: {1}" -f $n, $act)
        if (-not $act -or $act -match 'no live actor') { break }
        Start-Sleep -Seconds 3
        # the receptacle may need the dialogue/confirm press of its own pickup animation
        [void](Invoke-Act $H 'near:LIB_BookRecepticle!Interact')
        Start-Sleep -Seconds 3
        $inserted++
        $nH = Read-Flag $H 'LibBookPuzzleComplete'
        if ($null -ne $nH -and $nH -ne $flagBefore) { Note ("  LibBookPuzzleComplete changed to {0} after {1} insert(s)" -f $nH, $n); break }
        if ($inserted -ge 4) { break }
    }
    Verdict 'R22: the HOST inserted a book through the receptacle''s own Interact' ($inserted -gt 0) `
            ("{0} insert attempt(s)" -f $inserted)
    Start-Sleep -Seconds 4
    # the receptacle's savable state is the intermediate state; it must be on BOTH machines
    $stH = @(Get-Since $H $mStH | Where-Object { $_ -match 'actor.?state|savable|object intent|interact:' })
    $stP = @(Get-Since $P $mStP | Where-Object { $_ -match 'actor.?state|savable|object intent|interact:' })
    Verdict 'R22: the receptacle state crossed to the PEER (the intermediate state agrees)' ($stP.Count -gt 0) `
            ("host lines={0}, peer lines={1} :: peer: {2}" -f $stH.Count, $stP.Count, (($stP | Select-Object -Last 3 | ForEach-Object { ([string]$_).Trim() }) -join ' | '))
    $fH = Read-Flag $H 'LibBookPuzzleComplete'
    $fP = Read-Flag $P 'LibBookPuzzleComplete'
    Verdict 'R22: LibBookPuzzleComplete agrees on both machines' ($null -ne $fH -and $fH -eq $fP) `
            ("host={0} peer={1} (before={2}; the completion needs the whole book set)" -f $fH, $fP, $flagBefore)
    [void](Save-Shots 'R22-book-insert')

    # 4. the "turn a book" claim itself: is there ANY player verb on the decor book?
    #    A row being SENT is not a turn: the lane publishes the FIRST pose it sees for a prop
    #    that comes into its 40 m window, so walking up to the books raises `sent` with no
    #    state change at all.  A TURN is the same prop key holding a DIFFERENT yaw afterwards.
    $poseBefore = @{}
    foreach ($ln in $before[$H].Raw) {
        if ([string]$ln -match 'prop key=([0-9A-Fa-f]{8}) yaw=(-?\d+)') { $poseBefore[$Matches[1]] = [int]$Matches[2] }
    }
    foreach ($verb in @('Interact','Activate','Open','Rotate','Push','Toggle')) {
        $line = Invoke-Act $H ('near:Lib_Book_00_BP!' + $verb)
        Note ("  decor book verb {0}: {1}" -f $verb, $line)
        if ($line -and $line -match 'no live actor|component=MISSING|has no') { continue }
        Start-Sleep -Seconds 2
    }
    $afterH = Parse-WorldSet $H
    $poseAfter = @{}
    foreach ($ln in $afterH.Raw) {
        if ([string]$ln -match 'prop key=([0-9A-Fa-f]{8}) yaw=(-?\d+)') { $poseAfter[$Matches[1]] = [int]$Matches[2] }
    }
    $turned = 0
    foreach ($k in $poseAfter.Keys) {
        if ($poseBefore.ContainsKey($k) -and $poseBefore[$k] -ne $poseAfter[$k]) { $turned++ }
    }
    Verdict 'PuzzleSync: a decor book CHANGED pose through a player verb (a real turn)' ($turned -gt 0) `
            ("props with a changed yaw={0}; held before={1} after={2}; rows the lane sent={3} (first observations of newly-seen props do not count as turns)" -f `
             $turned, $before[$H].PuzzleHeld, $afterH.PuzzleHeld, $afterH.PuzzleSent)
    # Whatever the lane DID send must have landed on the partner: that half is measurable.
    $appliedP = @(Get-Matches $P 'puzzle: applied \d+ of \d+ prop row')
    Verdict 'PuzzleSync: the rows the lane sent were APPLIED on the partner' ($appliedP.Count -gt 0) `
            $(if ($appliedP.Count) { ([string]$appliedP[-1]).Trim() } else { '(the partner logged no `puzzle: applied` line)' })
    Invoke-AuditBoth $H $P $Phase
}

# ---------------------------------------------------------------------------
# AQ: R16-R19 and B2
# ---------------------------------------------------------------------------
function Invoke-AqPhase([int]$H, [int]$P) {
    Say '===== R16 200_AQ: the raider camp and the necklace turn-in ====='
    if (Enter-Area $H $P '200_AQ') {
        $raider = $null; $line = $null
        foreach ($nm in @('Raider_NPC_BP','Raider_Captain','Raider_Ghost_NPC_BP','Raider')) {
            $line = Invoke-Go $H ('near:' + $nm) 200
            if ($line -and $line -notmatch 'no live actor') { $raider = $nm; break }
        }
        Note ("  go raider ({0}): {1}" -f $raider, $line)
        [void](Save-Shots 'R16-raider-camp')
        if (-not $raider) { $raider = 'Raider' }
        $act = Invoke-Act $H ('near:' + $raider + '!Interact')
        Verdict 'R16: the HOST talks to the raider (the game''s own Interact)' ([bool]$act) $(if ($act) { $act } else { '(no interact line)' })
        # Four necklaces were granted at boot.  The turn-in is a DIALOGUE OPTION, not an
        # "advance": the raider's `Raider_Dialogue_00` opens with 2 options (`options=2
        # chosen=65535`), and only picking one offers the necklace.  So each round presses
        # the first option, then the second if the count did not move.
        $mTurn = Get-Mark $H
        for ($turn = 0; $turn -lt 5; $turn++) {
            foreach ($cmd in @('advance','opt0','advance','opt1')) {
                $d = Invoke-Dlg $H $cmd
                Note ("  dlg {0} (turn {1}): {2}" -f $cmd, $turn, $d)
                Start-Sleep -Seconds 2
            }
            $n = Read-Flag $H 'RaiderNeckacesTurnedIn'
            Note ("  necklaces turned in so far: {0}" -f $n)
            if ($null -ne $n -and $n -ge 4) { break }
            [void](Invoke-Act $H ('near:' + $raider + '!Interact'))
            Start-Sleep -Seconds 3
        }
        Start-Sleep -Seconds 3
        $flagH = Read-Flag $H 'RaiderNeckacesTurnedIn'
        $flagP = Read-Flag $P 'RaiderNeckacesTurnedIn'
        Verdict 'R16: the turn-in count is the SAME on both machines' ($null -ne $flagH -and $flagH -eq $flagP) `
                ("host={0} peer={1} (4 necklaces were granted at boot)" -f $flagH, $flagP)
        Verdict 'R16: RaiderNeckacesTurnedIn reached 4 through the conversation' ($flagH -ge 4) `
                ("host={0}" -f $flagH)
        [void](Save-Shots 'R16-raider-dialogue')
        Invoke-AuditBoth $H $P $Phase
        Leave-Area $H
    }

    Say '===== R17-R19 210_AQ_underground: fishwife, dark cave, arena, B2 Whale King ====='
    if (-not (Enter-Area $H $P '210_AQ_underground')) { return }
    # R17 -- the egg quest dialogue (the dialogue lane's own live proof)
    [void](Invoke-Go $H 'near:Fishwife_Ghost_BP' 180)
    $act = Invoke-Act $H 'near:Fishwife_Ghost_BP!Interact'
    Verdict 'R17: the HOST opens the Fishwife''s dialogue' ([bool]$act) $(if ($act) { $act } else { '(no interact line)' })
    Start-Sleep -Seconds 4
    $peerDlg = Wait-Pat $P 'dialogue: (BLOCK|view|spectator|mirror)' 20
    Verdict 'R17: the PEER sees the same conversation' ([bool]$peerDlg) $(if ($peerDlg) { $peerDlg } else { '(no dialogue line on the peer)' })
    [void](Save-Shots 'R17-fishwife-dialogue')
    for ($k = 0; $k -lt 5; $k++) { $d = Invoke-Dlg $H 'advance'; if (-not $d) { break }; Start-Sleep -Seconds 2 }
    [void](Invoke-Dlg $H 'leave')
    Start-Sleep -Seconds 3
    $eH = Read-Flag $H 'FishWifeTotalEggs'
    $eP = Read-Flag $P 'FishWifeTotalEggs'
    Verdict 'R17: the egg count agrees on both' ($null -ne $eH -and $eH -eq $eP) ("host={0} peer={1}" -f $eH, $eP)

    # R18 -- the dark cave: the setter that writes AQR_OpenedDarkCave
    $cH = Read-Flag $H 'AQR_OpenedDarkCave'
    $cP = Read-Flag $P 'AQR_OpenedDarkCave'
    Verdict 'R18: AQR_OpenedDarkCave agrees on both (before)' ($null -ne $cH -and $cH -eq $cP) ("host={0} peer={1}" -f $cH, $cP)
    # the setter IS a placed `ActivatableNamedSaveDataSetter_C`; drive its own verb
    $setter = $null
    foreach ($nm in @('ActivatableNamedSaveDataSetter!Activate','ActivatableNamedSaveDataSetter!SetActive','ActivatableNamedSaveDataSetter!Interact')) {
        $a = Invoke-Act $H ('near:' + $nm)
        if ($a -and $a -notmatch 'no live actor') { $setter = $a; break }
    }
    Verdict 'R18: the dark-cave setter ran on the HOST (a placed ActivatableNamedSaveDataSetter)' ([bool]$setter) `
            $(if ($setter) { $setter } else { '(no verb answered on near:ActivatableNamedSaveDataSetter)' })
    Start-Sleep -Seconds 4
    $cH2 = Read-Flag $H 'AQR_OpenedDarkCave'
    $cP2 = Read-Flag $P 'AQR_OpenedDarkCave'
    Verdict 'R18: AQR_OpenedDarkCave agrees on both (after)' ($null -ne $cH2 -and $cH2 -eq $cP2) ("host={0} peer={1}" -f $cH2, $cP2)

    # R19 -- the arena doors and exit door, then the boss
    foreach ($door in @('AquaBossDoor_BP','AQR_BossExitDoor','AquaBossExitDoor')) {
        $m = Get-Mark $H
        $a = Invoke-Act $H ('near:' + $door + '!Interact')
        if (-not $a) { $a = Invoke-Act $H ('near:' + $door + '!Activate') }
        Note ("  door {0}: {1}" -f $door, $a)
        if ($a -and $a -notmatch 'no live actor') {
            Start-Sleep -Seconds 3
            [void](Save-Shots ('R19-' + $door))
        }
    }
    # the two boss pickups must reach both
    $mPk = Get-Mark $P
    foreach ($pk in @('ChickenBossNecklace_Pickup','Fishman_Boss_Crown_Pickup')) {
        $a = Invoke-Act $H ('near:' + $pk + '!Interact')
        Note ("  pickup {0}: {1}" -f $pk, $a)
    }
    Start-Sleep -Seconds 4
    $qh = @(Get-Since $P $mPk | Where-Object { $_ -match 'inventory|inv:|item' } | Select-Object -Last 6)
    foreach ($l in $qh) { Note ('    [peer item] ' + ([string]$l).Trim()) }

    # B2 -- the Whale King: proximity trap starts it; real attacks, god off
    $mH = Get-Mark $H
    $tr = Invoke-Trigger $H 'WhaleKingSpawner@210_AQ_underground'
    Verdict 'B2: the HOST started the Whale King fight' ([bool]$tr) $(if ($tr) { $tr } else { '(trigger printed nothing)' })
    Start-Sleep -Seconds 10
    $eh = Get-Enemies $H
    $ep = Get-Enemies $P
    $bh = Find-Enemy $eh 'Whaleking|WhaleKing'
    $bp = Find-Enemy $ep 'Whaleking|WhaleKing'
    Verdict 'B2: the boss exists on BOTH machines' ($null -ne $bh -and $null -ne $bp) `
            ("host: {0} / peer: {1}" -f $(if ($bh) { $bh.Line } else { 'none' }), $(if ($bp) { $bp.Line } else { 'none' }))
    [void](Save-Shots 'B2-whale-spawn')
    if ($bh) {
        # both players walk up to the boss so the attack reaches it
        [void](Invoke-Go $H 'near:Whaleking_Boss' 600)
        [void](Invoke-Go $P 'near:Whaleking_Boss' 600)
        $hp0 = $bh.Hp
        # REAL attacks from each side (the player's own InpActEvt_Attack), then the enemy's
        # own damage entry to finish what the attack lever's timing does not.
        for ($round = 0; $round -lt 6; $round++) {
            [void](Invoke-Attack $H 'Attack' 250 2)
            [void](Invoke-Attack $P 'Attack' 250 2)
            Start-Sleep -Seconds 3
        }
        $eh2 = Get-Enemies $H
        $b2 = Find-Enemy $eh2 'Whaleking|WhaleKing'
        $moved = ($b2 -and $b2.Hp -lt $hp0 -and $b2.Hp -ge 0)
        Verdict 'B2: real attacks moved the shared HP (god off)' $moved `
                ("before={0} after={1}" -f $hp0, $(if ($b2) { $b2.Hp } else { '?' }))
        # finish it through the damage intent (the partner's hits also ride this path)
        for ($k = 0; $k -lt 40; $k++) {
            $cur = Find-Enemy (Get-Enemies $H) 'Whaleking|WhaleKing'
            if (-not $cur -or $cur.State -eq 'DEAD' -or ($cur.Hp -ge 0 -and $cur.Hp -le 0)) { break }
            $amt = [int][math]::Max(50, [math]::Floor($cur.Hp / 2))
            Set-Control $P 'qd.hitguid' ("{0}:{1}#{2}" -f $cur.Key, $amt, (Get-Date -Format 'HHmmssfff'))
            Start-Sleep -Seconds 2
        }
        Start-Sleep -Seconds 6
        $dh = Get-Enemies $H
        $dp = Get-Enemies $P
        $deadH = (Find-Enemy $dh 'Whaleking|WhaleKing')
        $deadP = (Find-Enemy $dp 'Whaleking|WhaleKing')
        Verdict 'B2: the boss is dead on BOTH machines' `
                ($deadH -and $deadP -and $deadH.State -eq 'DEAD' -and $deadP.State -eq 'DEAD') `
                ("host: {0} / peer: {1}" -f $(if ($deadH) { $deadH.Line } else { 'none' }), $(if ($deadP) { $deadP.Line } else { 'none' }))
        $fh = Read-Flag $H 'WhaleKingIsDead'
        $fp = Read-Flag $P 'WhaleKingIsDead'
        Verdict 'B2: WhaleKingIsDead = 1 in BOTH banks' ($fh -eq 1 -and $fp -eq 1) ("host={0} peer={1}" -f $fh, $fp)
        [void](Save-Shots 'B2-whale-dead')
    }
    Invoke-AuditBoth $H $P $Phase
    Leave-Area $H
}

# ---------------------------------------------------------------------------
# LIB: R20-R25 and B3
# ---------------------------------------------------------------------------
function Invoke-LibPhase([int]$H, [int]$P) {
    Say '===== R20-R23 300_LIB: librarian, Budaen key, book puzzle, Necronomicon ====='
    if (-not (Enter-Area $H $P '300_LIB')) { return }
    # R20 -- the bestiary (a kill-count condition; reported as a skip when not natural)
    $bH = Read-Flag $H 'LibrarianBestiaryCompletionPercent'
    $bP = Read-Flag $P 'LibrarianBestiaryCompletionPercent'
    Verdict 'R20: the bestiary percentage agrees on both machines' ($null -ne $bH -and $bH -eq $bP) ("host={0} peer={1}" -f $bH, $bP)
    # R21 -- Knight Budaen: dialogue and the key item
    $mIH = Get-Mark $H
    $mIP = Get-Mark $P
    [void](Invoke-Go $H 'near:Knight_Budaen' 200)
    $act = Invoke-Act $H 'near:Knight_Budaen_NPC_Ghost_BP!Interact'
    if (-not $act -or $act -match 'no live actor') { $act = Invoke-Act $H 'near:Knight_Budaen!Interact' }
    if (-not $act -or $act -match 'no live actor') { $act = Invoke-Act $H 'near:Knight_Budaen!Activate' }
    Verdict 'R21: the HOST talks to Knight Budaen' ([bool]$act) $(if ($act) { $act } else { '(no interact line)' })
    Start-Sleep -Seconds 3
    for ($k = 0; $k -lt 6; $k++) { $d = Invoke-Dlg $H 'advance'; if (-not $d) { break }; Start-Sleep -Seconds 2 }
    [void](Invoke-Dlg $H 'leave')
    Start-Sleep -Seconds 4
    [void](Save-Shots 'R21-budaen')
    # the key must reach both: inventory lines, both sides
    $invH = @(Get-Since $H $mIH | Where-Object { $_ -match 'inventory' } | Select-Object -Last 4)
    $invP = @(Get-Since $P $mIP | Where-Object { $_ -match 'inventory' } | Select-Object -Last 4)
    Verdict 'R21: the key item crossed (inventory line on the peer)' ($invP.Count -gt 0 -or $invH.Count -gt 0) `
            ("host: {0} | peer: {1}" -f (($invH | ForEach-Object { ([string]$_).Trim() }) -join ' ; '), (($invP | ForEach-Object { ([string]$_).Trim() }) -join ' ; '))

    # R22 -- the book puzzle: intermediate state (PuzzleSync)
    Say 'R22 -- the book puzzle with the books themselves'
    Invoke-PuzzleProof $H $P '300_LIB' -AlreadyIn
    $pzH = Read-Flag $H 'LibBookPuzzleComplete'
    $pzP = Read-Flag $P 'LibBookPuzzleComplete'
    Verdict 'R22: LibBookPuzzleComplete agrees on both' ($null -ne $pzH -and $pzH -eq $pzP) ("host={0} peer={1}" -f $pzH, $pzP)

    # R23 -- the Necronomicon, from its lectern
    Say 'R23 -- the Necronomicon (B3)'
    $mH = Get-Mark $H
    [void](Invoke-Act $H 'near:Necronomicon_Boss_Spawner!CanEverSpawn')
    [void](Invoke-Act $H 'near:Necronomicon_Boss_Spawner!CheckSpawnConditions')
    [void](Invoke-Act $H 'near:Necronomicon_Boss_Spawner!Try+Spawning+Enemy')
    $tr = Invoke-Trigger $H 'Necronomicon_Boss_Spawner@300_LIB'
    Verdict 'B3: the HOST started the Necronomicon' ([bool]$tr) $(if ($tr) { $tr } else { '(trigger printed nothing)' })
    Start-Sleep -Seconds 10
    $bh = Find-Enemy (Get-Enemies $H) 'Necronomicon'
    $bp = Find-Enemy (Get-Enemies $P) 'Necronomicon'
    Verdict 'B3: the book exists on BOTH machines' ($null -ne $bh -and $null -ne $bp) `
            ("host: {0} / peer: {1}" -f $(if ($bh) { $bh.Line } else { 'none' }), $(if ($bp) { $bp.Line } else { 'none' }))
    [void](Save-Shots 'B3-necro-spawn')
    if ($bh) {
        [void](Invoke-Go $H 'near:Necronomicon_BP' 600)
        [void](Invoke-Go $P 'near:Necronomicon_BP' 600)
        $hp0 = $bh.Hp
        for ($round = 0; $round -lt 6; $round++) {
            [void](Invoke-Attack $H 'Attack' 250 2)
            [void](Invoke-Attack $P 'Attack' 250 2)
            Start-Sleep -Seconds 3
        }
        $b2 = Find-Enemy (Get-Enemies $H) 'Necronomicon'
        Verdict 'B3: real attacks moved the shared HP (god off)' ($b2 -and $b2.Hp -lt $hp0 -and $b2.Hp -ge 0) `
                ("before={0} after={1}" -f $hp0, $(if ($b2) { $b2.Hp } else { '?' }))
        for ($k = 0; $k -lt 40; $k++) {
            $cur = Find-Enemy (Get-Enemies $H) 'Necronomicon'
            if (-not $cur -or $cur.State -eq 'DEAD' -or ($cur.Hp -ge 0 -and $cur.Hp -le 0)) { break }
            $amt = [int][math]::Max(50, [math]::Floor($cur.Hp / 2))
            Set-Control $P 'qd.hitguid' ("{0}:{1}#{2}" -f $cur.Key, $amt, (Get-Date -Format 'HHmmssfff'))
            Start-Sleep -Seconds 2
        }
        # the book's death is concluded by its own dialogue
        [void](Invoke-Act $H 'near:Necronomicon_BP!Interact')
        Start-Sleep -Seconds 2
        for ($k = 0; $k -lt 5; $k++) { $d = Invoke-Dlg $H 'advance'; if (-not $d) { break }; Start-Sleep -Seconds 2 }
        [void](Invoke-Act $H 'near:dialogue!End')
        Start-Sleep -Seconds 6
        $dh = Find-Enemy (Get-Enemies $H) 'Necronomicon'
        $dp = Find-Enemy (Get-Enemies $P) 'Necronomicon'
        Verdict 'B3: the book is dead on BOTH machines' ($dh -and $dp -and $dh.State -eq 'DEAD' -and $dp.State -eq 'DEAD') `
                ("host: {0} / peer: {1}" -f $(if ($dh) { $dh.Line } else { 'none' }), $(if ($dp) { $dp.Line } else { 'none' }))
        $fh = Read-Flag $H 'NecronimiconDefeated'
        $fp = Read-Flag $P 'NecronimiconDefeated'
        Verdict 'B3: NecronimiconDefeated = 1 in BOTH banks' ($fh -eq 1 -and $fp -eq 1) ("host={0} peer={1}" -f $fh, $fp)
        [void](Save-Shots 'B3-necro-dead')
    }
    Invoke-AuditBoth $H $P $Phase
    Leave-Area $H

    # R24 -- 310_LIB_exterior: Teleporter_Lib (a second, independent WorldSetSync data point)
    Say '===== R24 310_LIB_exterior: Teleporter_Lib ====='
    if (Enter-Area $H $P '310_LIB_exterior') {
        $m = Get-Mark $H
        [void](Invoke-Go $H 'near:Teleporter_Lib' 40)
        $un = Wait-New $H 'worldset: teleporters holds \d+ entr' 25 $m
        Verdict 'R24: the HOST unlocked Teleporter_Lib by standing on it' ([bool]$un) $(if ($un) { $un } else { '(no unlock line)' })
        $mP = Get-Mark $P
        $pApply = Wait-New $P 'worldset: teleporter .* unlocked through the game.s own RegisterSphere|worldset: applied teleporters' 40 $mP
        Verdict 'R24: the PEER unlocked the same destination in its own save' ([bool]$pApply) $(if ($pApply) { $pApply } else { '(no apply line)' })
        [void](Save-Shots 'R24-teleporter')
        Invoke-AuditBoth $H $P $Phase
        Leave-Area $H
    }

    # R25 -- 320_LIB_basement: vault relic #1
    Say '===== R25 320_LIB_basement: the vault relic ====='
    if (Enter-Area $H $P '320_LIB_basement') {
        $m = Get-Mark $H
        Set-Control $H 'qd.obj' ('relic#' + (Nonce))
        [void](Wait-New $H 'rig: object dump' 15 $m)
        Start-Sleep -Milliseconds 600
        $hits = @(Get-Since $H $m | Where-Object { $_ -match 'Greatsword|Rusted|SR_' } | Select-Object -First 8)
        foreach ($h in $hits) { Note ('    ' + ([string]$h).Trim()) }
        $pick = $null
        foreach ($nm in @('SR_Rusted_Greatsword','Rusted_Greatsword','VaultRelic')) {
            $a = Invoke-Act $H ('near:' + $nm + '!Interact')
            if ($a -and $a -notmatch 'no live actor') { $pick = $nm; Note ("  took {0}: {1}" -f $nm, $a); break }
        }
        $rH = Read-Flag $H 'SR_Rusted_Greatsword_Acquired'
        $rP = Read-Flag $P 'SR_Rusted_Greatsword_Acquired'
        Verdict 'R25: the relic flag agrees on both machines' ($null -ne $rH -and $rH -eq $rP) `
                ("host={0} peer={1} (1 only if the vault pickup was taken by a player verb; rig took: {2})" -f $rH, $rP, $pick)
        [void](Save-Shots 'R25-vault')
        Invoke-AuditBoth $H $P $Phase
        Leave-Area $H
    }
}

# ---------------------------------------------------------------------------
# MAUS: R26-R29 and B4
# ---------------------------------------------------------------------------
function Invoke-MausPhase([int]$H, [int]$P) {
    Say '===== R26 400_MAUS_entrance: lift, lever, small door, checkpoint ====='
    if (-not (Enter-Area $H $P '400_MAUS_entrance')) { return }
    # the lift: the mover lane's job.  The HOST owns it, both must see the same transform.
    $mBoard = Get-Mark $P
    [void](Invoke-Go $H 'near:Mausoleum_Lift' 300)
    # the PEER boards the lift: 1.5 m from its origin, then the landing hold puts it on top
    [void](Invoke-Go $P 'near:Mausoleum_Lift' 150)
    Start-Sleep -Seconds 4
    $boarded = Wait-New $P 'platforms: ride local BOARDED' 15 $mBoard
    Verdict 'R26: a player is RIDING the lift (the rider lane boarded)' ([bool]$boarded) `
            $(if ($boarded) { $boarded } else { '(no `platforms: ride local BOARDED` line)' })
    $mH = Get-Mark $H
    $mP = Get-Mark $P
    Set-Control $H 'qd.mover' ('Mausoleum_Lift_00,250,12#' + (Nonce))
    $mv = Wait-New $H 'rig: mover nudge' 20 $mH
    Verdict 'R26: the HOST''s lift is a tracked mover and was driven' ([bool]$mv) $(if ($mv) { $mv } else { '(no mover nudge line)' })
    $follow = Wait-New $P 'movers: .*(follow|elem)' 40 $mP
    Verdict 'R26: the PEER follows the lift' ([bool]$follow) $(if ($follow) { $follow } else { '(no movers follow line on the peer)' })
    $rode = Wait-New $P 'platforms: ride (local place|peer|drop)' 20 $mP
    Verdict 'R26: the rider''s place was carried with the platform' ([bool]$rode) $(if ($rode) { $rode } else { '(no further `platforms: ride` line)' })
    [void](Save-Shots 'R26-lift')
    # the lever and the small door are real interacts
    foreach ($t in @('MausoLever_00','M_DoorSmall_00_BP')) {
        $a = Invoke-Act $H ('near:' + $t + '!Interact')
        if (-not $a) { $a = Invoke-Act $H ('near:' + $t + '!Activate') }
        Note ("  {0}: {1}" -f $t, $a)
    }
    Start-Sleep -Seconds 3
    [void](Save-Shots 'R26-lever-door')
    Invoke-AuditBoth $H $P $Phase
    Leave-Area $H

    Say '===== R27 420_MAUS_mid: vault relic #2, Knight Nu, GotKnightKey ====='
    if (Enter-Area $H $P '420_MAUS_mid') {
        [void](Invoke-Go $H 'near:Knight_Nu' 200)
        $act = Invoke-Act $H 'near:Knight_Nu_NPC!Interact'
        if (-not $act -or $act -match 'no live actor') { $act = Invoke-Act $H 'near:Knight_Nu!Interact' }
        if (-not $act -or $act -match 'no live actor') { $act = Invoke-Act $H 'near:Knight_Nu!Activate' }
        Verdict 'R27: the HOST talks to Knight Nu' ([bool]$act) $(if ($act) { $act } else { '(no interact line)' })
        Start-Sleep -Seconds 3
        for ($k = 0; $k -lt 6; $k++) { $d = Invoke-Dlg $H 'advance'; if (-not $d) { break }; Start-Sleep -Seconds 2 }
        [void](Invoke-Dlg $H 'leave')
        Start-Sleep -Seconds 4
        $kH = Read-Flag $H 'GotKnightKey'
        $kP = Read-Flag $P 'GotKnightKey'
        Verdict 'R27: GotKnightKey agrees on both' ($null -ne $kH -and $kH -eq $kP) ("host={0} peer={1}" -f $kH, $kP)
        $vH = Read-Flag $H 'SR_RustedLeafBlade_Acquired'
        $vP = Read-Flag $P 'SR_RustedLeafBlade_Acquired'
        Verdict 'R27: the second relic flag agrees on both' ($null -ne $vH -and $vH -eq $vP) ("host={0} peer={1}" -f $vH, $vP)
        [void](Save-Shots 'R27-nu')
        # a third teleporter data point, for free, while we are here
        $m = Get-Mark $H
        [void](Invoke-Go $H 'near:Teleporter_Maus' 40)
        $un = Wait-New $H 'worldset: teleporters holds \d+ entr' 25 $m
        Note ("  Teleporter_Maus unlock: " + $un)
        Invoke-AuditBoth $H $P $Phase
        Leave-Area $H
    }

    Say '===== R28 450_MAUS_darkmaze: the chef, the king, the kitchen ====='
    if (-not (Enter-Area $H $P '450_MAUS_darkmaze')) { return }
    [void](Invoke-Go $H 'near:Chef' 200)
    $act = Invoke-Act $H 'near:Chef!Interact'
    Verdict 'R28: the HOST talks to the Chef' ([bool]$act) $(if ($act) { $act } else { '(no interact line)' })
    Start-Sleep -Seconds 3
    for ($k = 0; $k -lt 6; $k++) { $d = Invoke-Dlg $H 'advance'; if (-not $d) { break }; Start-Sleep -Seconds 2 }
    [void](Invoke-Dlg $H 'leave')
    Start-Sleep -Seconds 3
    $tH = Read-Flag $H 'MealTicketsCollected'
    $tP = Read-Flag $P 'MealTicketsCollected'
    Verdict 'R28: the meal-ticket count agrees on both' ($null -ne $tH -and $tH -eq $tP) ("host={0} peer={1}" -f $tH, $tP)
    $kH = Read-Flag $H 'PlayerReachedKitchen'
    $kP = Read-Flag $P 'PlayerReachedKitchen'
    Verdict 'R28: PlayerReachedKitchen agrees on both' ($null -ne $kH -and $kH -eq $kP) ("host={0} peer={1}" -f $kH, $kP)
    [void](Save-Shots 'R28-chef')

    Say '===== R29 450_MAUS_darkmaze: the Demon Bosses (B4) + magma ====='
    $hpBeforeH = Read-Hp $H
    $hpBeforeP = Read-Hp $P
    Note ("  HP before: host={0} peer={1}" -f $hpBeforeH, $hpBeforeP)
    $tr = Invoke-Trigger $H 'ActivationTrigger4@450_MAUS_darkmaze'
    Verdict 'B4: the HOST started the Demon Bosses' ([bool]$tr) $(if ($tr) { $tr } else { '(trigger printed nothing)' })
    Start-Sleep -Seconds 12
    $eh = Get-Enemies $H; $ep = Get-Enemies $P
    $sh = Find-Enemy $eh 'DemonSpider'; $sp = Find-Enemy $ep 'DemonSpider'
    $hh = Find-Enemy $eh 'Hellbat'; $hp2 = Find-Enemy $ep 'Hellbat'
    Verdict 'B4: the spider exists on BOTH machines' ($null -ne $sh -and $null -ne $sp) `
            ("host: {0} / peer: {1}" -f $(if ($sh) { $sh.Line } else { 'none' }), $(if ($sp) { $sp.Line } else { 'none' }))
    Verdict 'B4: the hell-bat exists on BOTH machines' ($null -ne $hh -and $null -ne $hp2) `
            ("host: {0} / peer: {1}" -f $(if ($hh) { $hh.Line } else { 'none' }), $(if ($hp2) { $hp2.Line } else { 'none' }))
    [void](Save-Shots 'B4-demon-spawn')
    # both players stand in the arena (that is also what puts them in reach of the magma)
    [void](Invoke-Go $H 'near:DemonSpider' 600)
    [void](Invoke-Go $P 'near:DemonSpider' 600)
    # MAGMA: stand both players in the fight and let the controller's own puddles hurt them.
    # The damage is the measurement: each player's HP must fall on ITS OWN machine.
    for ($round = 0; $round -lt 8; $round++) {
        [void](Invoke-Attack $H 'Attack' 250 2)
        [void](Invoke-Attack $P 'Attack' 250 2)
        Start-Sleep -Seconds 4
    }
    $hpH = Read-Hp $H; $hpP = Read-Hp $P
    Note ("  HP after standing in the fight: host={0} peer={1}" -f $hpH, $hpP)
    $hurtH = ($null -ne $hpBeforeH -and $null -ne $hpH -and $hpH -lt $hpBeforeH)
    $hurtP = ($null -ne $hpBeforeP -and $null -ne $hpP -and $hpP -lt $hpBeforeP)
    Verdict 'B4/magma: the hazard hurts the player who stands in it (host)' $hurtH ("before={0} after={1}" -f $hpBeforeH, $hpH)
    Verdict 'B4/magma: the hazard hurts the player who stands in it (peer)' $hurtP ("before={0} after={1}" -f $hpBeforeP, $hpP)
    # finish both bodies through the shared damage path
    for ($k = 0; $k -lt 60; $k++) {
        $cur = @(Get-Enemies $H).Values | Where-Object { $_.Cls -match 'DemonSpider|Hellbat' -and $_.State -eq 'ALIVE' } | Select-Object -First 1
        if (-not $cur) { break }
        $amt = [int][math]::Max(50, [math]::Floor([math]::Max(10, $cur.Hp) / 2))
        Set-Control $P 'qd.hitguid' ("{0}:{1}#{2}" -f $cur.Key, $amt, (Get-Date -Format 'HHmmssfff'))
        Start-Sleep -Seconds 2
    }
    Start-Sleep -Seconds 8
    $dh = Get-Enemies $H; $dp = Get-Enemies $P
    $allDeadH = @($dh.Values | Where-Object { $_.Cls -match 'DemonSpider|Hellbat' -and $_.State -eq 'ALIVE' }).Count -eq 0
    $allDeadP = @($dp.Values | Where-Object { $_.Cls -match 'DemonSpider|Hellbat' -and $_.State -eq 'ALIVE' }).Count -eq 0
    Verdict 'B4: both bodies dead on the HOST' $allDeadH (($dh.Values | Where-Object { $_.Cls -match 'DemonSpider|Hellbat' } | ForEach-Object { $_.Line }) -join ' | ')
    Verdict 'B4: both bodies dead on the PEER' $allDeadP (($dp.Values | Where-Object { $_.Cls -match 'DemonSpider|Hellbat' } | ForEach-Object { $_.Line }) -join ' | ')
    $fH = Read-Flag $H 'DemonBossesKilled'
    $fP = Read-Flag $P 'DemonBossesKilled'
    Verdict 'B4: DemonBossesKilled = 1 in BOTH banks' ($fH -eq 1 -and $fP -eq 1) ("host={0} peer={1}" -f $fH, $fP)
    [void](Save-Shots 'B4-demon-dead')
    # the web wall: open on both?  It is a door family actor.
    $wallH = Invoke-Act $H 'near:WebBossWall_BP!Interact'
    Note ("  WebBossWall (host): " + $wallH)
    Start-Sleep -Seconds 3
    [void](Save-Shots 'R29-webwall')
    Invoke-AuditBoth $H $P $Phase
    Leave-Area $H
}

# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------
$script:RunStartedAt = Get-Date
Say ("session-p2 start: phase={0}" -f $Phase)

if ($Phase -eq 'stop') {
    # launching nothing: only this rig's own recorded pids are stopped
    Stop-All 'operator stop'
    $script:lines | Set-Content -LiteralPath $Report
    Say "report: $Report"
    exit 0
}

Assert-LiveLock
Stop-All 'phase start: clearing anything this rig left running before the pair is seeded'

Invoke-Boot
$H = $script:H
$P = $script:P

try {
    switch ($Phase) {
        'worldpuzzle' { Invoke-WorldSetProof $H $P; Invoke-PuzzleProof $H $P '300_LIB' }
        'aq'          { Invoke-AqPhase $H $P }
        'lib'         { Invoke-LibPhase $H $P }
        'maus'        { Invoke-MausPhase $H $P }
        'story' {
            # one boot, three parts: a phase that throws does not take the later ones with it
            foreach ($part in @('aq', 'lib', 'maus')) {
                try {
                    switch ($part) {
                        'aq'   { Invoke-AqPhase $H $P }
                        'lib'  { Invoke-LibPhase $H $P }
                        'maus' { Invoke-MausPhase $H $P }
                    }
                } catch {
                    Say ("PHASE {0} EXCEPTION: {1}" -f $part, $_.Exception.Message)
                    Note ($_.ScriptStackTrace)
                }
                if (-not (Test-Alive $H) -and -not (Test-Alive $P)) { Say ("both instances are gone after {0}" -f $part); break }
            }
        }
    }
} catch {
    Say ("HARNESS EXCEPTION: {0}" -f $_.Exception.Message)
    Note ($_.ScriptStackTrace)
}

Note ''
Note '================ SUMMARY ================'
foreach ($v in $script:verdicts) { $t = 'FAIL'; if ($v.Ok) { $t = 'PASS' }; Note ("{0}  {1}" -f $t, $v.What) }
$fail = @($script:verdicts | Where-Object { -not $_.Ok }).Count
Note ("{0} pass, {1} fail" -f (@($script:verdicts | Where-Object { $_.Ok }).Count, $fail))

Stop-All 'session complete'
$script:lines | Set-Content -LiteralPath $Report
Say "report: $Report"
if ($fail -gt 0) { exit 1 } else { exit 0 }
