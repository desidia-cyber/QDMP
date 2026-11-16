# session-p2b.ps1 -- [play-p2b] the boss/damage proof this pass exists for.
#
# WHY A NEW RIG AND NOT session-p2.ps1.  Three of play-p2's four open items were RIG defects,
# and each is fixed here rather than argued around:
#
#  1. THE ATTACKER WAS DEAD.  Test instances keep a death-penalty sidecar between runs
#     (`<gameDir>\qdcoop-p2*-respawn.ini`), so play-p2's run booted with 7 stacks restored and
#     spent the whole boss fight at 12 HP: every `qd.attack` played `Player_FPArms_Death_Montage`
#     (the DEATH arms) and no boss HP could ever move.  This rig deletes the sidecar before it
#     launches anything, and sets a survivable HP with `qd.selfhp` before each fight.
#  2. THE PLAYERS WERE 6 m FROM THE BOSS, and placed from each machine's OWN copy of a name.
#     `qd.tp near:DemonSpider` put the host 30 m away from the boss the peer was sent to (the
#     name resolves to a placed actor per machine).  This rig places both players by the ENEMY
#     KEY (`qd.tp guid:<8 hex>`, "the key both machines hold, so both players arrive at the SAME
#     enemy") and at 180 cm, inside melee reach.
#  3. THE HP PROPERTY WAS WRONG.  The pawn's health is `Health` (float), which is what the
#     vitals lane and `qd.selfhp` both use; play-p2 asked for `HP` and read `absent`.  Here:
#     `near:me!?Health` -> `Health=i<raw>/f<*100>`, so `f`/100 is the HP.
#
# Phases (one boot each):
#   bosses : B3 Necronomicon in 300_LIB, then B4 Demon Bosses + magma in 450_MAUS_darkmaze.
#   rest   : R17 fishwife, R24 310 teleporter, B2 Whale King, R27 Knight Nu.
#
# HONESTY RULES (inherited): a verdict prints the line it read; no line, no pass; the STATE
# (enemy table on BOTH machines, flag banks, HP) decides a step, never a lever's optimism; a
# step the rig cannot do with a player verb is reported NOT-REACHABLE with the reason.
[CmdletBinding()]
param(
    [ValidateSet('bosses', 'damage', 'rest', 'stop')]
    [string]$Phase = 'bosses',
    [int]$BootSeconds = 420,
    [int]$SettleSeconds = 30,
    [string]$Report = ''
)

$ErrorActionPreference = 'Stop'
$here    = $PSScriptRoot
$repo    = Split-Path -Parent (Split-Path -Parent $here)
$gameDir = 'G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64'
$relay   = Join-Path (Split-Path -Parent $here) 'rust\target\release\qd-relay.exe'
$rigRoot = 'G:\Freespace\Queens_Domain\_test\p2b'
$shotDir = 'G:\Freespace\Queens_Domain\_test\playthrough\p2b'
$seed    = Join-Path $repo 'analysis\save-backup-before-live'
$srcIni  = Join-Path $repo 'mod\qdcoop.ini'
$outDir  = Join-Path $rigRoot 'out'
if (-not $Report) { $Report = Join-Path $outDir ("report-{0}.txt" -f $Phase) }
foreach ($d in $outDir, $shotDir, $rigRoot) {
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
# THE LIVE LOCK (COMMON-BRIEF, strict form): nothing is launched unless this script
# holds `_test\locks\live`; another owner's lock is broken only when it is >25 min old
# AND no game/relay process runs; cleanup kills only the pids this script launched.
# ---------------------------------------------------------------------------
$script:RigId      = 'play-p2b'
$script:LockDir    = 'G:\Freespace\Queens_Domain\_test\locks\live'
$script:LockOwner  = Join-Path $script:LockDir 'owner.txt'
$script:LockWritten = [datetime]::MinValue

function Update-LockHeartbeat([switch]$Force) {
    if (-not (Test-Path $script:LockOwner)) { return }
    if (-not $Force -and ((Get-Date) - $script:LockWritten).TotalMinutes -lt 4) { return }
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
            Say ("live lock is already mine ('{0}') -- refreshing it" -f $owner)
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
        Suffix = "p2b$i"
        Log    = (Join-Path $gameDir ("qdcoop-p2b{0}.log" -f $i))
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
    foreach ($h in @(Get-Since $i $mark | Where-Object { $_ -match $pat } | Select-Object -Last $max)) {
        Note ("    [{0}] {1}" -f $i, ([string]$h).Trim())
    }
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
                      'qd.sequence','qd.enemykill','qd.auditfork','qd.god') {
        Set-Control $i $leaf $null
    }
}
function Wait-New([int]$i, [string]$pat, [int]$sec, [int]$mark) {
    $end = (Get-Date).AddSeconds($sec)
    while ((Get-Date) -lt $end) {
        Update-LockHeartbeat
        $hits = @(Get-Since $i $mark | Where-Object { $_ -match $pat })
        if ($hits.Count -gt 0) { return ([string]$hits[-1]).Trim() }
        if (-not (Test-Alive $i)) { return $null }
        Start-Sleep -Milliseconds 700
    }
    return $null
}
function Wait-Pat([int]$i, [string]$pat, [int]$sec) {
    $end = (Get-Date).AddSeconds($sec)
    while ((Get-Date) -lt $end) {
        Update-LockHeartbeat
        $hits = @(Get-Matches $i $pat)
        if ($hits.Count -gt 0) { return ([string]$hits[-1]).Trim() }
        Start-Sleep -Milliseconds 700
    }
    return $null
}

# ---------------------------------------------------------------------------
# Screenshots -- "the boss is on both screens" is a question about pixels
# ---------------------------------------------------------------------------
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
        return [pscustomobject]@{ W = $bmp.Width; H = $bmp.Height; Mean = [math]::Round($mean, 1); Std = [math]::Round([math]::Sqrt($var / [math]::Max(1, $vals.Count)), 1) }
    } finally { $bmp.Dispose() }
}
function Save-Shots([string]$tag) {
    $paths = @()
    foreach ($i in 1, 2) {
        if (-not (Test-Alive $i)) { Note ("  shot[{0}] {1}: instance gone" -f $i, $tag); continue }
        $out = Join-Path $shotDir ("p2b-{0}-{1}-inst{2}.png" -f $Phase, $tag, $i)
        Remove-Item -LiteralPath $out -Force -ErrorAction SilentlyContinue
        $r = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'capture-hidden.ps1') `
                -Out $out -ProcessId $script:pids[$i] 2>&1
        if (-not (Test-Path -LiteralPath $out)) {
            Note ("  shot[{0}] {1}: FAILED ({2})" -f $i, $tag, (@($r) | Select-Object -Last 1)); continue
        }
        $m = Measure-Shot $out
        Note ("  shot[{0}] {1}: {2}x{3} mean={4} stddev={5} -> {6}" -f $i, $tag, $m.W, $m.H, $m.Mean, $m.Std, $out)
        $paths += [pscustomobject]@{ Inst = $i; Tag = $tag; Path = $out; Mean = $m.Mean; Std = $m.Std }
    }
    if ($paths.Count -eq 2) {
        $ok = (@($paths | Where-Object { $_.Mean -gt 2 -and $_.Std -gt 3 }).Count -eq 2)
        Verdict ("shot pair '{0}': both scenes drawn" -f $tag) $ok `
                (($paths | ForEach-Object { "inst$($_.Inst) mean=$($_.Mean) std=$($_.Std)" }) -join '; ')
    }
    return $paths
}

# ---------------------------------------------------------------------------
# Real key input: the walk into a teleporter volume (a placement there is refused)
# ---------------------------------------------------------------------------
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
                $script:walkArea = $a; $script:walkBest = $hw
            }
        }
        return $true
    }
    [QdWalk]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
    return $script:walkBest
}
function Send-Key([int]$i, [int]$vk, [int]$holdMs) {
    $h = Get-GameWindow $i
    if ($h -eq [IntPtr]::Zero) { Note ("  [{0}] Send-Key: no game window" -f $i); return $false }
    [QdWalk]::PostMessage($h, 0x0006, [IntPtr]1, [IntPtr]0) | Out-Null
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
# Levers
# ---------------------------------------------------------------------------
function Invoke-Go([int]$i, [string]$spec, [int]$cm = 0) {
    if ($cm -gt 0) { Set-Control $i 'qd.near' $cm }
    $m = Get-Mark $i
    Set-Control $i 'qd.tp' ($spec + '#' + (Nonce))
    return (Wait-New $i 'dev teleport' 15 $m)
}
function Invoke-Act([int]$i, [string]$spec) {
    $m = Get-Mark $i
    Set-Control $i 'qd.act' ($spec + '#' + (Nonce))
    return (Wait-New $i 'dev interact: target=|dev interact: forced verb|dev interact: no live actor|dev props:' 15 $m)
}
function Invoke-Trigger([int]$i, [string]$spec) {
    $m = Get-Mark $i
    Set-Control $i 'qd.trigger' ($spec + '#' + (Nonce))
    return (Wait-New $i "rig: trigger '" 30 $m)
}
function Invoke-Dlg([int]$i, [string]$cmd) {
    $m = Get-Mark $i
    Set-Control $i 'qd.dlg' ($cmd + '#' + (Nonce))
    return (Wait-New $i 'dialogue|dlg' 20 $m)
}
function Invoke-Attack([int]$i, [string]$action, [int]$gap = 300, [int]$count = 2) {
    $m = Get-Mark $i
    Set-Control $i 'qd.attack' ("{0},{1},{2}#{3}" -f $action, $gap, $count, (Nonce))
    return (Wait-New $i 'rig: attack ' 25 $m)
}
function Read-Flag([int]$i, [string]$name) {
    $m = Get-Mark $i
    Set-Control $i 'qd.flag' ($name + '#' + (Nonce))
    $line = Wait-New $i ('rig: flag dump -- ' + [regex]::Escape($name) + '|flag ' + [regex]::Escape($name) + '=-?\d+') 15 $m
    if (-not $line) { return $null }
    if ($line -match '=\s*(-?\d+)') { return [int]$Matches[1] }
    return $null
}
# The pawn's health is `Health` (float), NOT `HP` -- play-p2's `HP` read was `absent`.
# `dev props` prints a 4-byte property as `i<raw int>/f<value*100>`, so f/100 is the value:
#   `dev props: local player Health=i1142947840/f64000`  ->  640.0
function Read-Hp([int]$i) {
    $m = Get-Mark $i
    Set-Control $i 'qd.act' ('near:me!?Health#' + (Nonce))
    $line = Wait-New $i 'dev props: ' 12 $m
    if ($line) { Note ("    [{0}] hp read: {1}" -f $i, $line) }
    if ($line -and $line -match 'Health=i-?\d+/f(-?\d+)') { return [math]::Round([double]$Matches[1] / 100.0, 2) }
    return $null
}
# THE SHARED BOSS BAR is the ONE number both screens are supposed to agree on: `boss: bar
# key=<k> hp=<fraction 0..65535>` is printed once a second while the bar is up, on BOTH
# machines.  Reading the local copy's `enemykey … hp=` instead compares two different things
# (the owner's simulation against a frozen follower's own body) and produced a false
# "the peer's reading did not drop" in this pass's first run.
function Read-BarHp([int]$i, [string]$key) {
    $m = Get-Mark $i
    $ln = Wait-New $i ('boss: bar key=' + $key + ' ') 15 $m
    if (-not $ln) { return $null }
    if ($ln -match 'hp=(-?\d+)') { return [int]$Matches[1] }
    return $null
}
function Set-SelfHp([int]$i, [int]$v) {
    Set-Control $i 'qd.selfhp' ([string]$v + '#' + (Nonce))
    $m = Get-Mark $i
    return (Wait-New $i 'rig: selfhp ->' 12 ($m - 1))
}
function Get-Enemies([int]$i) {
    $m = Get-Mark $i
    Set-Control $i 'qd.enemies' ('dump#' + (Nonce))
    [void](Wait-New $i 'enemykey |rig: enemies' 20 $m)
    Start-Sleep -Milliseconds 600
    $rows = @{}
    foreach ($ln in @(Get-Since $i $m)) {
        $s = [string]$ln
        if ($s -notmatch 'enemykey ([0-9A-F]{8}) (ALIVE|DEAD)') { continue }
        $k = $Matches[1]; $st = $Matches[2]
        $hp = -1.0; if ($s -match 'hp=([\d.]+)') { $hp = [double]$Matches[1] }
        $cls = '';  if ($s -match 'cls=(\S+)') { $cls = $Matches[1] }
        $mir = ($s -match 'mirrored=1')
        $rows[$k] = [pscustomobject]@{ Key = $k; State = $st; Hp = $hp; Cls = $cls; Mirror = $mir; Line = $s.Trim() }
    }
    return $rows
}
function Find-Enemy($rows, [string]$clsPattern) {
    foreach ($k in $rows.Keys) { if ($rows[$k].State -eq 'ALIVE' -and $rows[$k].Cls -match $clsPattern) { return $rows[$k] } }
    foreach ($k in $rows.Keys) { if ($rows[$k].Cls -match $clsPattern) { return $rows[$k] } }
    return $null
}
# The key the host's spawner minted, from `enemy id: spawner <8HEX> (<Class>)`.
# NOTE: `$Matches` is REASSIGNED by any later `-match`, so the two groups are copied into
# locals before the class name is tested -- the first version tested `$Matches[2] -match ...`
# and then read `$Matches[1]`, which was already gone (that is why play-p2b's first run
# reported "the host spawned no Necronomicon" for a key the peer was demonstrably told about).
function Read-SpawnKeys([int]$i, [int]$mark, [string]$clsPattern) {
    $out = @()
    foreach ($ln in @(Get-Since $i $mark)) {
        if ([string]$ln -notmatch 'enemy id: spawner ([0-9A-F]{8}) \(([^)]+)\)') { continue }
        $k = $Matches[1]; $c = $Matches[2]
        if ($c -match $clsPattern) { $out += [pscustomobject]@{ Key = $k; Cls = $c } }
    }
    return $out
}
function Enter-Area([int]$H, [int]$P, [string]$area) {
    Say ("entering area {0}" -f $area)
    $mP = Get-Mark $P     # the peer reacts BEFORE the host's own line is written
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
function Leave-Area([int]$H) { Set-Control $H 'qd.level' ('-#' + (Nonce)); Start-Sleep -Seconds 8 }

function Invoke-Audit([int]$i) {
    $m = Get-Mark $i
    Set-Control $i 'qd.audit' ('now#' + (Nonce))
    $line = Wait-New $i 'audit: (OK \d+ categories|DESYNC OPEN|pass \d+ us)' 45 $m
    if (-not $line) { $line = Wait-New $i 'audit: DESYNC|audit: OK' 20 $m }
    if (-not $line) { return '(no audit line)' }
    return $line
}
function Invoke-AuditBoth([int]$H, [int]$P, [string]$step) {
    $a = Invoke-Audit $H
    $b = Invoke-Audit $P
    if ($a -match 'DESYNC' -or $b -match 'DESYNC') {
        Note ("  audit[{0}]: DESYNC on the first pass -- re-asking after 12 s" -f $step)
        Start-Sleep -Seconds 12
        $a = Invoke-Audit $H; $b = Invoke-Audit $P
    }
    $bad = ($a -match 'DESYNC' -or $b -match 'DESYNC')
    Verdict ("{0}: qd.audit has no DESYNC on either machine" -f $step) (-not $bad) `
            ("host: {0} | peer: {1}" -f $a, $b)
}

# ---------------------------------------------------------------------------
# Boot / stop
# ---------------------------------------------------------------------------
function Stop-All([string]$why) {
    Say "STOP-ALL $why"
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

    # THE SHIPPED ini verbatim, with only the relay pinned to loopback, LogLevel=3, and the
    # switches this run exists to prove.  WorldSetSync is already 1 (play-p2's proof);
    # MirrorSpawn is written EXPLICITLY because item 1 is a mirror-spawn question.
    $text = Get-Content -LiteralPath $srcIni -Raw
    $text = $text -replace '(?m)^RelayHost=.*$', 'RelayHost=127.0.0.1'
    $text = $text -replace '(?m)^RelayPort=.*$', 'RelayPort=47800'
    $text = $text -replace '(?m)^LogLevel=.*$', 'LogLevel=3'
    if ($text -notmatch '(?m)^MirrorSpawn=') { $text = $text -replace '(?m)^(EnemySync=.*)$', "`$1`nMirrorSpawn=1" }
    $ini = Join-Path $gameDir 'qdcoop.ini'
    $text | Set-Content -LiteralPath $ini -NoNewline
    Say "wrote qdcoop.ini (shipped + loopback + LogLevel=3 + MirrorSpawn=1)"
    Copy-Item -LiteralPath $ini -Destination (Join-Path $outDir 'qdcoop-used.ini') -Force

    # THE DEATH-PENALTY CARRY-OVER (item 2's real cause): the per-instance respawn sidecar
    # survives between runs, so a pair booted at 30 % max HP and the attacker fired the DEATH
    # arms montage instead of an attack.  A fresh pair starts with no stacks.
    foreach ($i in 1, 2) {
        $side = Join-Path $gameDir ("qdcoop-{0}-respawn.ini" -f $inst[$i].Suffix)
        if (Test-Path -LiteralPath $side) {
            Remove-Item -LiteralPath $side -Force -ErrorAction SilentlyContinue
            Say ("removed the stale death-penalty sidecar {0} (a fresh pair has no death stacks)" -f (Split-Path -Leaf $side))
        }
    }

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
    $gives = @()
    foreach ($b in @('Alchemy','BoneMachines','CookBook','Dragons','Eldritch')) {
        $gives += ('+/Game/Items/KeyItems/BookKeys/{0}/BookKey_00_{0}_Item.BookKey_00_{0}_Item' -f $b)
    }
    $env:QDCOOP_GIVE = ($gives -join ';')
    Say ("QDCOOP_GIVE: {0} path(s)" -f $gives.Count)

    if (-not (Test-Path -LiteralPath $relay)) { Say "REFUSED: relay not built at $relay"; exit 2 }
    $relayPidFile = Join-Path $rigRoot 'relay.pid'
    if (Test-Path -LiteralPath $relayPidFile) {
        $old = Get-Content -LiteralPath $relayPidFile -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($old) { Stop-Process -Id ([int]$old) -Force -ErrorAction SilentlyContinue; Start-Sleep -Seconds 1 }
    }
    $relayLog = Join-Path $outDir 'relay-p2b.log'
    Remove-Item -LiteralPath $relayLog -ErrorAction SilentlyContinue
    $script:relayProc = Start-Process -FilePath $relay -ArgumentList @('--bind', '127.0.0.1:47800') `
            -RedirectStandardOutput $relayLog -RedirectStandardError (Join-Path $outDir 'relay-p2b.err') `
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

    # THE DEATH STACKS ARE REALLY GONE: measured, not assumed.
    $rl = @(Get-Matches $script:H 'respawn: restored \d+ death stack')
    Verdict 'the pair booted with NO carried-over death penalty' ($rl.Count -eq 0) `
            $(if ($rl.Count) { ([string]$rl[-1]).Trim() } else { '(no `restored N death stack(s)` line)' })

    $mal = @(Read-LogLines $relayLog | Where-Object { $_ -match 'malformed' })
    Verdict 'the relay reports no malformed datagrams' ($mal.Count -eq 0) `
            $(if ($mal.Count) { ([string]$mal[-1]).Trim() } else { '(0 malformed lines)' })

    Say "settling $SettleSeconds s"
    $settleEnd = (Get-Date).AddSeconds($SettleSeconds)
    while ((Get-Date) -lt $settleEnd) { Update-LockHeartbeat; Start-Sleep -Seconds 5 }
    Update-LockHeartbeat
}

# ---------------------------------------------------------------------------
# A BOSS FIGHT, generically -- the same body of checks for B3 and B4.
# ---------------------------------------------------------------------------
# `cls` matches the boss class in the enemy table; `keys` is what the host's spawner minted.
function Test-BossBothSides {
    param([int]$H, [int]$P, [string]$Tag, [string]$ClsPattern, [string[]]$Keys)

    Say ("{0}: waiting for the PEER to hold a body for every key the host spawned" -f $Tag)
    foreach ($k in $Keys) {
        $m = Get-Mark $P
        $got = Wait-New $P ('enemy spawn: constructed guid=' + $k + '|enemy spawn: received guid=' + $k) 45 $m
        Verdict ("{0}: the PEER was told about key {1}" -f $Tag, $k) ([bool]$got) `
                $(if ($got) { $got } else { '(the host never described this key to the peer)' })
    }
    foreach ($k in $Keys) {
        $m = Get-Mark $H
        $sent = Wait-New $H ('enemy spawn: described guid=' + $k) 15 $m
        if (-not $sent) {
            $already = @(Get-Matches $H ('enemy spawn: described guid=' + $k))
            $sent = $(if ($already.Count) { ([string]$already[-1]).Trim() } else { $null })
        }
        Verdict ("{0}: the HOST described key {1} to the peer" -f $Tag, $k) ([bool]$sent) `
                $(if ($sent) { $sent } else { '(no `enemy spawn: described` line for this key)' })
    }

    Start-Sleep -Seconds 6
    $eh = Get-Enemies $H; $ep = Get-Enemies $P
    $hH = @($eh.Values | Where-Object { $_.Cls -match $ClsPattern })
    $hP = @($ep.Values | Where-Object { $_.Cls -match $ClsPattern })
    Verdict ("{0}: the boss is in the HOST's enemy table" -f $Tag) ($hH.Count -gt 0) `
            $(($hH | ForEach-Object { $_.Line }) -join ' | ')
    Verdict ("{0}: the boss is in the PEER's enemy table TOO" -f $Tag) ($hP.Count -gt 0 -and @($hP | Where-Object { $_.Mirror }).Count -gt 0) `
            $(($hP | ForEach-Object { $_.Line }) -join ' | ')

    # THE SHARED BAR, on both screens, for the SAME key.
    # The bar is only raised for a boss IN THE FIGHT -- damaged, or within 60 m of a player --
    # so the players are placed beside it FIRST.  Skipping this made the first B4 run report
    # "no bar" for two bodies that were demonstrably present on both machines.
    Say ("{0}: placing both players 250 cm from key {1} so the boss counts as in the fight" -f $Tag, $Keys[0])
    foreach ($i in $H, $P) {
        $mp = Get-Mark $i
        [void](Invoke-Go $i ('guid:' + $Keys[0]) 250)
        [void](Wait-New $i ('dev teleport: resolved guid=' + $Keys[0]) 12 $mp)
    }
    Start-Sleep -Seconds 6
    $bh = Wait-Pat $H ('boss: bar key=(' + ($Keys -join '|') + ') ') 40
    $bp = Wait-Pat $P ('boss: bar key=(' + ($Keys -join '|') + ') ') 40
    $ack = @(Get-Matches $H ('boss: peer ACK key=(' + ($Keys -join '|') + ')'))
    Verdict ("{0}: the shared boss bar is raised on the HOST for a key of this fight" -f $Tag) ([bool]$bh) $(if ($bh) { $bh } else { '(no `boss: bar key=` line for these keys)' })
    Verdict ("{0}: the shared boss bar is raised on the PEER for the same key" -f $Tag) ([bool]$bp) $(if ($bp) { $bp } else { '(no `boss: bar key=` line for these keys)' })
    Verdict ("{0}: the PEER ACKed the boss (it has a live copy the bar can bind)" -f $Tag) ($ack.Count -gt 0) `
            $(if ($ack.Count) { ([string]$ack[-1]).Trim() } else { '(no `boss: peer ACK` line)' })
    if ($bh -and $bp) {
        $kh = $null; if ($bh -match 'key=([0-9A-F]{8})') { $kh = $Matches[1] }
        $kp = $null; if ($bp -match 'key=([0-9A-F]{8})') { $kp = $Matches[1] }
        Verdict ("{0}: both bars carry the SAME key" -f $Tag) ($kh -and $kp -and $kh -eq $kp) ("host={0} peer={1}" -f $kh, $kp)
    }
    return [pscustomobject]@{ HostRows = $eh; PeerRows = $ep; HostBoss = $hH; PeerBoss = $hP }
}

# REAL ATTACKS AT MELEE RANGE, both players, and the HP measured on BOTH machines.
function Test-RealAttacks {
    param([int]$H, [int]$P, [string]$Tag, [string]$Key, [string]$ClsPattern)
    Say ("{0}: both players placed 180 cm from key {1} by KEY (not by name)" -f $Tag, $Key)
    [void](Set-SelfHp $H 800)
    [void](Set-SelfHp $P 800)
    Start-Sleep -Seconds 2
    foreach ($i in $H, $P) {
        $m = Get-Mark $i
        [void](Invoke-Go $i ('guid:' + $Key) 180)
        $place = Wait-New $i ('dev teleport: resolved guid=' + $Key) 12 $m
        Verdict ("{0}: inst{1} stands beside key {2} by its enemy key" -f $Tag, $i, $Key) ([bool]$place) `
                $(if ($place) { $place } else { '(the guid lever did not resolve this key)' })
    }
    Start-Sleep -Seconds 2
    # THE MEASUREMENT IS THE SHARED BAR, not each machine's own copy: `boss: bar key=<k>
    # hp=<fraction>` is the one number both screens show, and it is what "the shared HP drops
    # on both screens" means.  Reading the local `enemykey … hp` compares the owner's live
    # simulation with a follower's frozen body and can never agree.
    $frac0H = Read-BarHp $H $Key
    $frac0P = Read-BarHp $P $Key
    $bH = Find-Enemy (Get-Enemies $H) $ClsPattern
    $bP = Find-Enemy (Get-Enemies $P) $ClsPattern
    $hp0H = $(if ($bH) { $bH.Hp } else { -1 })
    $hp0P = $(if ($bP) { $bP.Hp } else { -1 })
    Note ("  shared bar HP before the swings: host={0} peer={1} (owner's body {2} HP)" -f $frac0H, $frac0P, $hp0H)

    # WHICH MONTAGE DOES THE INPUT ACTUALLY PLAY?  A `MeleeResponse` montage is the arms'
    # reaction to being HIT, not an attack -- play-p2 read "montage changed" as "the attack
    # fired".  The line below names it, so the report cannot confuse the two.
    $mAtk = Get-Mark $H
    $a1 = Invoke-Attack $H 'Attack' 400 3
    $m1 = @(Get-Since $H $mAtk | Where-Object { $_ -match 'rig: attack .*montage' } | Select-Object -Last 1)
    if ($m1.Count) {
        $ln = [string]$m1[-1]
        $isResponse = ($ln -match 'MeleeResponse|Death_Montage|HitReact|Hurt')
        Verdict ("{0}: the Attack input can play a real ATTACK montage (not a hit response)" -f $Tag) (-not $isResponse) $ln
    } else {
        Verdict ("{0}: the Attack input produced a montage line" -f $Tag) $false '(none)'
    }

    # Many rounds on purpose: the swing only opens its hitbox when the pawn is not already in
    # the hit-response layer, so a real hit arrives every few attempts, not every attempt.
    for ($round = 0; $round -lt 10; $round++) {
        foreach ($i in @($H, $P)) {
            [void](Set-SelfHp $i 800)
            [void](Invoke-Attack $i 'Attack' 300 4)
            [void](Invoke-Attack $i 'SecondaryAttack' 300 2)
        }
        Start-Sleep -Seconds 1
    }
    Start-Sleep -Seconds 3
    $frac1H = Read-BarHp $H $Key
    $frac1P = Read-BarHp $P $Key
    $aH = Find-Enemy (Get-Enemies $H) $ClsPattern
    $hp1H = $(if ($aH) { $aH.Hp } else { -1 })
    Verdict ("{0}: real attacks dropped the HOST's shared boss bar" -f $Tag) ($null -ne $frac0H -and $null -ne $frac1H -and $frac1H -lt $frac0H) `
            ("bar hp {0} -> {1} (owner's body {2} -> {3} HP)" -f $frac0H, $frac1H, $hp0H, $hp1H)
    Verdict ("{0}: the PEER's bar shows the SAME drop (shared HP on both screens)" -f $Tag) ($null -ne $frac0P -and $null -ne $frac1P -and $frac1P -lt $frac0P) `
            ("bar hp {0} -> {1}" -f $frac0P, $frac1P)
    Verdict ("{0}: the two bars agree after the fight" -f $Tag) ($null -ne $frac1H -and $frac1H -eq $frac1P) `
            ("host={0} peer={1}" -f $frac1H, $frac1P)
    return [pscustomobject]@{ Hp0H = $hp0H; Hp1H = $hp1H; Frac0H = $frac0H; Frac1H = $frac1H; Frac0P = $frac0P; Frac1P = $frac1P }
}

# ---------------------------------------------------------------------------
# PHASE: bosses (B3 then B4 + magma)
# ---------------------------------------------------------------------------
function Invoke-BossPhase([int]$H, [int]$P) {
    # ---------------- B3: the Necronomicon, started from its lectern ----------------
    Say '===== B3 300_LIB: the Necronomicon ====='
    if (-not (Enter-Area $H $P '300_LIB')) { return }
    $mSpawn = Get-Mark $H
    # The lectern path: the spawner's own gate and then the volume a player walks into.
    foreach ($v in @('CanEverSpawn', 'CheckSpawnConditions', 'Try+Spawning+Enemy')) {
        [void](Invoke-Act $H ('near:Necronomicon_Boss_Spawner!' + $v))
    }
    $tr = Invoke-Trigger $H 'Necronomicon_Boss_Spawner@300_LIB'
    Note ("  trigger: " + $(if ($tr) { $tr } else { '(nothing)' }))
    # `triggered 0->0` is the TRIGGER VOLUME's counter, not the spawn result: the
    # follow-through line and the `enemy id: spawner` line are what decide it.
    $ft = Wait-New $H 'trigger: boss follow-through \(local\).*ActivateEnemy' 20 $mSpawn
    Verdict 'B3: the spawner ran (the trigger follow-through activated it)' ([bool]$ft) $(if ($ft) { $ft } else { '(no follow-through line)' })
    Start-Sleep -Seconds 6
    $keys = @(Read-SpawnKeys $H $mSpawn 'Necronomicon')
    $k = $null; if ($keys.Count) { $k = $keys[-1].Key }
    Verdict 'B3: the host spawned a Necronomicon and it has a key' ([bool]$k) $(if ($k) { "key=$k" } else { '(no `enemy id: spawner ... (Necronomicon_BP_C)` line)' })
    if (-not $k) { $b = Find-Enemy (Get-Enemies $H) 'Necronomicon'; if ($b) { $k = $b.Key } }
    if (-not $k) { Note '  no Necronomicon key on the host: the rest of B3 cannot be measured'; Leave-Area $H; return }

    $r = Test-BossBothSides -H $H -P $P -Tag 'B3' -ClsPattern 'Necronomicon' -Keys @($k)
    [void](Save-Shots 'B3-spawn')

    $hp = Test-RealAttacks -H $H -P $P -Tag 'B3' -Key $k -ClsPattern 'Necronomicon'
    [void](Save-Shots 'B3-midfight')

    # Finish it: real attacks first; if the HP stalls, say so and use the shared damage lane.
    Say 'B3: finishing the book with real attacks, then the shared damage lane'
    for ($round = 0; $round -lt 10; $round++) {
        $cur = Find-Enemy (Get-Enemies $H) 'Necronomicon'
        if (-not $cur -or $cur.State -eq 'DEAD' -or $cur.Hp -le 0) { break }
        [void](Invoke-Attack $H 'Attack' 300 4)
        [void](Invoke-Attack $P 'Attack' 300 4)
        Start-Sleep -Seconds 2
    }
    $mid = Find-Enemy (Get-Enemies $H) 'Necronomicon'
    $killedByAttack = ($mid -and ($mid.State -eq 'DEAD' -or $mid.Hp -le 0))
    Verdict 'B3: the book died to REAL attacks alone (no damage lane used)' $killedByAttack `
            ($(if ($mid) { $mid.Line } else { '(gone)' }))
    if (-not $killedByAttack) {
        for ($i2 = 0; $i2 -lt 40; $i2++) {
            $cur = Find-Enemy (Get-Enemies $H) 'Necronomicon'
            if (-not $cur -or $cur.State -eq 'DEAD' -or $cur.Hp -le 0) { break }
            $amt = [int][math]::Max(50, [math]::Floor([math]::Max(10, $cur.Hp) / 2))
            Set-Control $P 'qd.hitguid' ("{0}:{1}#{2}" -f $cur.Key, $amt, (Nonce))
            Start-Sleep -Seconds 2
        }
    }
    # The book's death is concluded by its own dialogue (Session 29).
    [void](Invoke-Act $H 'near:Necronomicon_BP!Interact')
    Start-Sleep -Seconds 2
    for ($i3 = 0; $i3 -lt 5; $i3++) { $d = Invoke-Dlg $H 'advance'; if (-not $d) { break }; Start-Sleep -Seconds 2 }
    [void](Invoke-Act $H 'near:dialogue!End')
    Start-Sleep -Seconds 6
    $dh = Find-Enemy (Get-Enemies $H) 'Necronomicon'
    $dp = Find-Enemy (Get-Enemies $P) 'Necronomicon'
    # A mirror the owner's death retired leaves NO row at all on the peer (the record is
    # dropped), so "not ALIVE on the peer" is the right test there -- but only because the
    # body was SEEN on the peer first (Test-BossBothSides proved `mirrored=1`).
    $peerAlive = ($dp -and $dp.State -eq 'ALIVE' -and $dp.Hp -gt 0)
    $peerDead  = (-not $peerAlive -and $r.PeerBoss.Count -gt 0)
    Verdict 'B3: the book is DEAD on both machines' `
            ($dh -and ($dh.State -eq 'DEAD' -or $dh.Hp -le 0) -and $peerDead) `
            ("host: {0} / peer: {1} (the peer had a mirrored body before the kill)" -f `
             $(if ($dh) { $dh.Line } else { 'gone' }), $(if ($dp) { $dp.Line } else { 'no row (retired)' }))
    $fh = Read-Flag $H 'NecronimiconDefeated'
    $fp = Read-Flag $P 'NecronimiconDefeated'
    Verdict 'B3: NecronimiconDefeated = 1 in BOTH banks' ($fh -eq 1 -and $fp -eq 1) ("host={0} peer={1}" -f $fh, $fp)
    [void](Save-Shots 'B3-dead')
    Invoke-AuditBoth $H $P 'B3'
    Leave-Area $H

    # ---------------- B4: the Demon Bosses + the magma they spawn ----------------
    Say '===== B4 450_MAUS_darkmaze: the Demon Bosses and the magma ====='
    if (-not (Enter-Area $H $P '450_MAUS_darkmaze')) { return }
    $mSpawn = Get-Mark $H
    $tr = Invoke-Trigger $H 'ActivationTrigger4@450_MAUS_darkmaze'
    Note ("  trigger: " + $(if ($tr) { $tr } else { '(nothing)' }))
    $ft = Wait-New $H 'trigger: boss follow-through \(local\).*DemonSpider_Spawner' 25 $mSpawn
    Verdict 'B4: ActivationTrigger4 activated DemonSpider_Spawner' ([bool]$ft) $(if ($ft) { $ft } else { '(no follow-through line naming DemonSpider_Spawner)' })
    Start-Sleep -Seconds 6
    $keys = @()
    for ($w = 0; $w -lt 8 -and ($keys | Where-Object { $_.Cls -match 'DemonSpider' }).Count -eq 0; $w++) {
        $keys += @(Read-SpawnKeys $H $mSpawn 'DemonSpider|Hellbat')
        if (($keys | Where-Object { $_.Cls -match 'DemonSpider' }).Count -eq 0) {
            # The spider is the deferred half of the pair (measured: the hell-bat appears at
            # once and the spider a little later, and in one run only the bat came out of the
            # trigger's follow-through).  Ask the spawner itself, the way the game does.
            if ($w -eq 2) { [void](Invoke-Act $H 'near:DemonSpider_Spawner!Activate') }
            if ($w -ge 4) { [void](Invoke-Act $H 'near:DemonSpider_Spawner!Try+Spawning+Enemy') }
            [void](Invoke-Act $H 'near:DemonSpider_Spawner!ActivateEnemy')
            Start-Sleep -Seconds 5
        }
    }
    $ks = @($keys | Where-Object { $_.Cls -match 'DemonSpider' } | Select-Object -Last 1)
    $kb = @($keys | Where-Object { $_.Cls -match 'Hellbat' } | Select-Object -Last 1)
    $kSpider = $(if ($ks.Count) { $ks[0].Key } else { $null })
    $kBat    = $(if ($kb.Count) { $kb[0].Key } else { $null })
    if (-not $kSpider) { $x = Find-Enemy (Get-Enemies $H) 'DemonSpider'; if ($x) { $kSpider = $x.Key } }
    if (-not $kBat)    { $x = Find-Enemy (Get-Enemies $H) 'Hellbat';    if ($x) { $kBat = $x.Key } }
    Verdict 'B4: the host spawned the Demon Spider (with a key)' ([bool]$kSpider) $(if ($kSpider) { "key=$kSpider" } else { '(none)' })
    Verdict 'B4: the host spawned the Hellbat (with a key)' ([bool]$kBat) $(if ($kBat) { "key=$kBat" } else { '(none)' })
    $allKeys = @(); if ($kSpider) { $allKeys += $kSpider }; if ($kBat) { $allKeys += $kBat }
    if ($allKeys.Count -eq 0) { Note '  no demon keys on the host: the rest of B4 cannot be measured'; Leave-Area $H; return }

    $r = Test-BossBothSides -H $H -P $P -Tag 'B4' -ClsPattern 'DemonSpider|Hellbat' -Keys $allKeys
    [void](Save-Shots 'B4-spawn')

    # MAGMA.  The hazard is the boss's own `DemonBoss_FightController_C` magma, and the damage
    # a hazard does to a player is applied BY THAT PLAYER'S MACHINE (qdhazard.h).  So the
    # measurement is each player's own `Health` on its own machine, before and after standing
    # in the fight -- and the `hazard:` lines say whether a puddle was ever mirrored.
    Say 'B4/magma: standing both players in the arena and reading each player''s own Health'
    $hazH0 = @(Get-Matches $H 'hazard: ').Count
    $hazP0 = @(Get-Matches $P 'hazard: ').Count
    $hpB = @{}
    foreach ($i in $H, $P) { [void](Set-SelfHp $i 800); $hpB[$i] = $null }
    Start-Sleep -Seconds 2
    foreach ($i in $H, $P) { $hpB[$i] = Read-Hp $i ; Start-Sleep -Milliseconds 400 }
    Note ("  Health before standing in the arena: host={0} peer={1}" -f $hpB[$H], $hpB[$P])
    foreach ($i in $H, $P) { [void](Invoke-Go $i ('guid:' + $kSpider) 400) }
    Start-Sleep -Seconds 3
    # The controller spawns puddles under the fight; give it time and move a little.
    for ($round = 0; $round -lt 8; $round++) {
        if ($round -eq 4) { foreach ($i in $H, $P) { [void](Invoke-Go $i ('guid:' + $kSpider) 250) } }
        Start-Sleep -Seconds 4
        Update-LockHeartbeat
    }
    $hpA = @{}
    foreach ($i in $H, $P) { $hpA[$i] = Read-Hp $i; Start-Sleep -Milliseconds 400 }
    Note ("  Health after standing in the arena: host={0} peer={1}" -f $hpA[$H], $hpA[$P])
    $hazH = @(Get-Since $H 0 | Where-Object { $_ -match 'hazard: ' } | Select-Object -Last 10)
    $hazP = @(Get-Since $P 0 | Where-Object { $_ -match 'hazard: ' } | Select-Object -Last 10)
    foreach ($l in $hazH) { Note ('    [H] ' + ([string]$l).Trim()) }
    foreach ($l in $hazP) { Note ('    [P] ' + ([string]$l).Trim()) }
    $hurtH = ($null -ne $hpB[$H] -and $null -ne $hpA[$H] -and $hpA[$H] -lt $hpB[$H])
    $hurtP = ($null -ne $hpB[$P] -and $null -ne $hpA[$P] -and $hpA[$P] -lt $hpB[$P])
    Verdict 'B4/magma: the hazard hurt the HOST''s own player' $hurtH ("Health {0} -> {1}" -f $hpB[$H], $hpA[$H])
    Verdict 'B4/magma: the hazard hurt the PEER''s own player (the partner in magma)' $hurtP ("Health {0} -> {1}" -f $hpB[$P], $hpA[$P])
    # THE MAGMA CLASS ITSELF.  `DemonSpider_MagmaPuddle` is what the fight controller spawns;
    # the hazard keyword list matches "magma" and "puddle".  If the class never appears, the
    # Health drop above is enemy damage, not the hazard, and the hazard claim is not measured.
    $mg = @(Get-Matches $H 'MagmaPuddle') + @(Get-Matches $P 'MagmaPuddle')
    Verdict 'B4/magma: the content actually spawned a magma puddle (the hazard lane had something to mirror)' ($mg.Count -gt 0) `
            $(if ($mg.Count) { ([string]$mg[-1]).Trim() } else { '(no MagmaPuddle anywhere on either machine: the Health drop above is enemy damage)' })
    $cp = @(Get-Matches $P 'hazard: (the partner spawned|copy of)')
    Verdict 'B4/magma: the PEER received a mirrored hazard copy from the partner' ($cp.Count -gt 0) `
            $(if ($cp.Count) { ([string]$cp[-1]).Trim() } else { '(no `hazard: the partner spawned` line on the peer)' })
    [void](Save-Shots 'B4-magma')

    # Real attacks on the spider, both players, at melee range.
    $hp = Test-RealAttacks -H $H -P $P -Tag 'B4' -Key $kSpider -ClsPattern 'DemonSpider'
    [void](Save-Shots 'B4-midfight')

    # Finish both bodies: real attacks, then the shared damage lane, and say which did it.
    Say 'B4: finishing both bodies'
    for ($round = 0; $round -lt 12; $round++) {
        $alive = @((Get-Enemies $H).Values | Where-Object { $_.Cls -match 'DemonSpider|Hellbat' -and $_.State -eq 'ALIVE' })
        if ($alive.Count -eq 0) { break }
        [void](Invoke-Attack $H 'Attack' 300 4)
        [void](Invoke-Attack $P 'Attack' 300 4)
        Start-Sleep -Seconds 2
    }
    $aliveNow = @((Get-Enemies $H).Values | Where-Object { $_.Cls -match 'DemonSpider|Hellbat' -and $_.State -eq 'ALIVE' })
    Verdict 'B4: both bodies died to REAL attacks alone' ($aliveNow.Count -eq 0) `
            (($aliveNow | ForEach-Object { $_.Line }) -join ' | ')
    if ($aliveNow.Count -gt 0) {
        for ($i4 = 0; $i4 -lt 60; $i4++) {
            $cur = @((Get-Enemies $H).Values | Where-Object { $_.Cls -match 'DemonSpider|Hellbat' -and $_.State -eq 'ALIVE' } | Select-Object -First 1)
            if ($cur.Count -eq 0) { break }
            $amt = [int][math]::Max(50, [math]::Floor([math]::Max(10, $cur[0].Hp) / 2))
            Set-Control $P 'qd.hitguid' ("{0}:{1}#{2}" -f $cur[0].Key, $amt, (Nonce))
            Start-Sleep -Seconds 2
        }
    }
    Start-Sleep -Seconds 8
    $rowsH = Get-Enemies $H; $rowsP = Get-Enemies $P
    $allH = @($rowsH.Values | Where-Object { $_.Cls -match 'DemonSpider|Hellbat' })
    $allP = @($rowsP.Values | Where-Object { $_.Cls -match 'DemonSpider|Hellbat' })
    $dh = @($allH | Where-Object { $_.State -eq 'ALIVE' })
    $dp = @($allP | Where-Object { $_.State -eq 'ALIVE' })
    # AN EMPTY SET IS NOT A DEATH (play-p2's own lesson, note 4): a row must have been SEEN.
    Verdict 'B4: both bodies DEAD on the HOST' ($allH.Count -gt 0 -and $dh.Count -eq 0) `
            ("{0} row(s), {1} alive: {2}" -f $allH.Count, $dh.Count, (($allH | ForEach-Object { $_.Line }) -join ' | '))
    Verdict 'B4: both bodies DEAD on the PEER' ($allP.Count -gt 0 -and $dp.Count -eq 0) `
            ("{0} row(s), {1} alive: {2}" -f $allP.Count, $dp.Count, (($allP | ForEach-Object { $_.Line }) -join ' | '))
    $fH = Read-Flag $H 'DemonBossesKilled'
    $fP = Read-Flag $P 'DemonBossesKilled'
    Verdict 'B4: DemonBossesKilled = 1 in BOTH banks' ($fH -eq 1 -and $fP -eq 1) ("host={0} peer={1}" -f $fH, $fP)
    [void](Save-Shots 'B4-dead')
    Invoke-AuditBoth $H $P 'B4'
    Leave-Area $H
}

# ---------------------------------------------------------------------------
# PHASE: damage -- CAN the rig's attack input land damage at all?
# ---------------------------------------------------------------------------
# This is the control experiment item 2 needs.  play-p2 concluded "real attacks moved no boss
# HP" and could not say whether that was the boss or the lever.  The answer has to be measured
# against an ORDINARY enemy in the same session: if a Crawler loses HP to the same input at the
# same range with the same weapon, the lever works and the boss is the special case; if nothing
# loses HP, the lever is the limit and no boss claim can ever be made with it.
function Invoke-DamagePhase([int]$H, [int]$P) {
    Say '===== the attack lever: a control enemy, then the boss ====='
    $mEq = Get-Mark $H
    # Which weapon is in hand?  `equip rev`/`hand R now holds` say so without trusting the rig.
    $eq = Wait-New $H 'body: hand R now holds|equip scan|local equipment rev' 20 $mEq
    Note ("  weapon in hand (host): " + $(if ($eq) { $eq } else { '(no line)' }))
    if (-not $Enter) { $Enter = $null }

    if (-not (Enter-Area $H $P '300_LIB')) { return }
    $rows = Get-Enemies $H
    # An ordinary enemy, far from being a boss: Crawler / BookBug / Slime / Boner.
    $ctl = $null
    foreach ($pat in @('Crawler_Candles|Crawler_Base', 'BookBug', 'Slimer', 'Boner_Standard')) {
        $c = @($rows.Values | Where-Object { $_.State -eq 'ALIVE' -and $_.Cls -match $pat })
        if ($c.Count) { $ctl = $c[0]; break }
    }
    Verdict 'control: an ordinary enemy is present to test the lever on' ([bool]$ctl) `
            $(if ($ctl) { $ctl.Line } else { '(no ordinary enemy in the host table)' })
    if ($ctl) {
        [void](Set-SelfHp $H 800)
        $m = Get-Mark $H
        [void](Invoke-Go $H ('guid:' + $ctl.Key) 180)
        $place = Wait-New $H ('dev teleport: resolved guid=' + $ctl.Key) 12 $m
        Note ("  placed beside the control enemy: " + $(if ($place) { $place } else { '(not resolved)' }))
        Start-Sleep -Seconds 2
        # WHAT IS IN THE HAND, AND IS IT DRAWN?  `WeaponsArePutAway` and `CurrentInHandWeapon`
        # are the pawn's own properties: an attack montage that plays without a drawn weapon
        # has no hitbox to trace, which is what "the montage changed but nothing lost HP" looks
        # like.  This read is the difference between "the lever is broken" and "the sword is on
        # the back".
        $props = Invoke-Act $H ('near:me!?CurrentInHandWeapon,WeaponsArePutAway,HurtBoxActive,CurrentMeleeAttackCharge' + '#' + (Nonce))
        Note ("  pawn state before drawing: " + $(if ($props) { $props } else { '(no line)' }))
        # A player presses 1 to draw; then swings.
        $mD = Get-Mark $H
        Set-Control $H 'qd.attack' ('EquipWeapon1,300,1#' + (Nonce))
        $dr = Wait-New $H 'rig: attack ' 20 $mD
        Note ("  draw (EquipWeapon1): " + $(if ($dr) { $dr } else { '(no line)' }))
        Start-Sleep -Seconds 2
        $props2 = Invoke-Act $H ('near:me!?CurrentInHandWeapon,WeaponsArePutAway,HurtBoxActive' + '#' + (Nonce))
        Note ("  pawn state after drawing: " + $(if ($props2) { $props2 } else { '(no line)' }))

        $c0 = Find-Enemy (Get-Enemies $H) ([regex]::Escape($ctl.Cls))
        $hp0 = $(if ($c0) { $c0.Hp } else { -1 })
        $montages = @()
        foreach ($act in @('Attack', 'Attack', 'SecondaryAttack', 'Attack', 'Attack')) {
            $a = Invoke-Attack $H $act 350 3
            if ($a) { $montages += $a }
            Start-Sleep -Milliseconds 900
        }
        foreach ($l in $montages) { Note ('    [H] ' + $l) }
        Start-Sleep -Seconds 2
        $c1 = Find-Enemy (Get-Enemies $H) ([regex]::Escape($ctl.Cls))
        $hp1 = $(if ($c1) { $c1.Hp } else { -1 })
        Verdict 'control: REAL attack input damaged an ORDINARY enemy' ($hp0 -gt 0 -and $hp1 -ge 0 -and $hp1 -lt $hp0) `
                ("{0} HP {1} -> {2}" -f $ctl.Cls, $hp0, $hp1)
        # THE LAST RESORT THAT IS STILL A REAL SWING: the game's own hitbox configuration, then
        # the same input.  `ConfigureNormalMeleeHitbox` is what the pawn's attack graph calls
        # before the trace; if only THIS makes damage land, the missing piece is the weapon in
        # hand, not the attack input.
        if (-not ($hp1 -ge 0 -and $hp1 -lt $hp0)) {
            $c = Invoke-Act $H ('near:me!?HurtBoxActive' + '#' + (Nonce))
            Note ("  HurtBoxActive before the forced configure: " + $(if ($c) { $c } else { '(no line)' }))
            $mC = Get-Mark $H
            Set-Control $H 'qd.equip' ('/Game/Items/Weapons/RustedSword/RustySword_Item.RustySword_Item,0#' + (Nonce))
            $eqr = Wait-New $H 'rig: equip ' 20 $mC
            Note ("  EquipItem(RustySword_Item, slot 0): " + $(if ($eqr) { $eqr } else { '(no line)' }))
            Start-Sleep -Seconds 2
            foreach ($act in @('Attack', 'Attack', 'SecondaryAttack')) {
                [void](Invoke-Attack $H $act 400 3)
                Start-Sleep -Seconds 1
            }
            Start-Sleep -Seconds 2
            $c3 = Find-Enemy (Get-Enemies $H) ([regex]::Escape($ctl.Cls))
            $hp3 = $(if ($c3) { $c3.Hp } else { -1 })
            Verdict 'control: with the sword item explicitly equipped, the real attack damages' ($hp3 -ge 0 -and $hp3 -lt $hp1) `
                    ("{0} HP {1} -> {2}" -f $ctl.Cls, $hp1, $hp3)
        }
        # A damage-intent lane hit on the same enemy, so the enemy's HP is KNOWN to be writable:
        # if this works and the attack does not, the gap is the melee hit, not the enemy.
        Set-Control $P 'qd.hitguid' ("{0}:{1}#{2}" -f $ctl.Key, [int][math]::Max(10, $hp1 / 4), (Nonce))
        Start-Sleep -Seconds 3
        $c2 = Find-Enemy (Get-Enemies $H) ([regex]::Escape($ctl.Cls))
        $hp2 = $(if ($c2) { $c2.Hp } else { -1 })
        Verdict 'control: the shared damage lane CAN lower that same enemy' ($hp2 -ge 0 -and $hp2 -lt $hp1) `
                ("{0} HP {1} -> {2}" -f $ctl.Cls, $hp1, $hp2)
    }

    # And the boss, if this area has one standing.
    $boss = Find-Enemy (Get-Enemies $H) 'Necronomicon'
    if ($boss -and $boss.State -eq 'ALIVE') {
        $m = Get-Mark $H
        [void](Invoke-Go $H ('guid:' + $boss.Key) 180)
        [void](Wait-New $H ('dev teleport: resolved guid=' + $boss.Key) 12 $m)
        Start-Sleep -Seconds 2
        $b0 = Find-Enemy (Get-Enemies $H) 'Necronomicon'
        $hp0 = $(if ($b0) { $b0.Hp } else { -1 })
        for ($r2 = 0; $r2 -lt 4; $r2++) {
            [void](Invoke-Attack $H 'Attack' 350 3)
            [void](Invoke-Attack $H 'SecondaryAttack' 350 3)
            Start-Sleep -Seconds 2
        }
        $b1 = Find-Enemy (Get-Enemies $H) 'Necronomicon'
        $hp1 = $(if ($b1) { $b1.Hp } else { -1 })
        Verdict 'boss: REAL attack input damaged the Necronomicon' ($hp0 -gt 0 -and $hp1 -ge 0 -and $hp1 -lt $hp0) `
                ("HP {0} -> {1} (state: {2})" -f $hp0, $hp1, $(if ($b1) { ($b1.Line -replace '.*cls=','cls=') } else { '?' }))
    }
    [void](Save-Shots 'damage-control')
    Invoke-AuditBoth $H $P 'damage'
}

# ---------------------------------------------------------------------------
# PHASE: rest (R17, R24, B2, R27)
# ---------------------------------------------------------------------------
function Invoke-RestPhase([int]$H, [int]$P) {
    # ---------------- R17: the fishwife's egg quest ----------------
    Say '===== R17 the fishwife: route says 210_AQ_underground, the quest table says 200_AQ ====='
    $done = $false
    foreach ($area in @('210_AQ_underground', '200_AQ')) {
        if ($done) { break }
        if (-not (Enter-Area $H $P $area)) { continue }
        # DISCOVER the placement instead of guessing: `qd.obj <substring>` lists placed actors
        # whose name matches.  play-p2 only ever tried one guessed name and reported
        # "no live actor", which is a statement about the guess, not about the content.
        $m = Get-Mark $H
        Set-Control $H 'qd.obj' ('fish#' + (Nonce))
        [void](Wait-New $H 'rig: object dump' 15 $m)
        Start-Sleep -Milliseconds 700
        $hits = @(Get-Since $H $m | Where-Object { $_ -match 'Fish|fish|Egg|egg' } | Select-Object -First 12)
        foreach ($l in $hits) { Note ('    ' + ([string]$l).Trim()) }
        $names = @()
        foreach ($l in $hits) { if ([string]$l -match '^\s*([A-Za-z0-9_]+)\s+\[') { $names += $Matches[1] } }
        # The route names BOTH `Fishwife_NPC_BP_C` and `Fishwife_Ghost_BP_C`.
        $cands = @('Fishwife_NPC_BP', 'Fishwife_Ghost_BP', 'Fishwife') + $names
        $tried = @(); $act = $null
        foreach ($nm in ($cands | Select-Object -Unique)) {
            if (-not $nm) { continue }
            [void](Invoke-Go $H ('near:' + $nm) 200)
            $a = Invoke-Act $H ('near:' + $nm + '!Interact')
            $tried += ("{0}: {1}" -f $nm, $a)
            if ($a -and $a -notmatch 'no live actor' -and $a -match 'ran=yes') { $act = $a; break }
        }
        Verdict ("R17: the HOST talks to the fishwife in {0}" -f $area) ([bool]$act) ("tried: " + ($tried -join ' ; '))
        if ($act) {
            $done = $true
            $mP = Get-Mark $P
            Start-Sleep -Seconds 3
            for ($k = 0; $k -lt 6; $k++) { $d = Invoke-Dlg $H 'advance'; if (-not $d) { break }; Start-Sleep -Seconds 2 }
            $view = Wait-New $P 'dialogue view|dialogue: .*(showing|spectator|BLOCK)' 15 $mP
            Verdict 'R17: the PEER sees the same conversation' ([bool]$view) $(if ($view) { $view } else { '(no dialogue-view line on the peer)' })
            [void](Invoke-Dlg $H 'leave')
            Start-Sleep -Seconds 3
            $eH = Read-Flag $H 'FishWifeTotalEggs'
            $eP = Read-Flag $P 'FishWifeTotalEggs'
            Verdict 'R17: FishWifeTotalEggs agrees on both machines' ($null -ne $eH -and $eH -eq $eP) `
                    ("host={0} peer={1} (>=50 needs fifty egg hand-ins; this rig granted none, so the COUNT is not the claim)" -f $eH, $eP)
            [void](Save-Shots 'R17-fishwife')
        }
        if ($done) {
            Invoke-AuditBoth $H $P 'R17'
        }
        Leave-Area $H
    }

    # ---------------- R24: the 310 teleporter, unlocked by WALKING in ----------------
    Say '===== R24 310_LIB_exterior: Teleporter_Lib (place at 450, then a real W walk) ====='
    if (Enter-Area $H $P '310_LIB_exterior') {
        $m = Get-Mark $H
        $mP = Get-Mark $P
        $unlock = $null
        for ($try = 0; $try -lt 3 -and -not $unlock; $try++) {
            $off = 450 + 150 * $try
            Set-Control $H 'qd.near' ([string]$off)
            $go = Invoke-Go $H 'near:Teleporter_Lib'
            Note ("    place {0} cm: {1}" -f $off, $go)
            if (-not $go -or $go -match 'refused|no live actor') { continue }
            Start-Sleep -Seconds 1
            [void](Send-Key $H 0x57 (900 + 400 * $try))    # a real W hold walks the pawn in
            Start-Sleep -Seconds 1
            $unlock = Wait-New $H 'worldset: teleporters holds \d+ entr' 12 $m
        }
        Verdict 'R24: the HOST unlocked Teleporter_Lib by walking into it' ([bool]$unlock) $(if ($unlock) { $unlock } else { '(no unlock line)' })
        if ($unlock) { [void](Send-Key $H 0x1B 30) }
        $sent = Wait-New $H 'worldset: sent teleporters rows' 20 $m
        Verdict 'R24: the HOST published the union' ([bool]$sent) $(if ($sent) { $sent } else { '(no `sent teleporters` line)' })
        $apply = Wait-New $P 'worldset: applied teleporters|worldset: teleported .* unlocked through the game.s own RegisterSphere' 40 $mP
        Verdict 'R24: the PEER has the same destination' ([bool]$apply) $(if ($apply) { $apply } else { '(no apply line)' })
        $ch = Read-Flag $H 'TeleporterUnlockCount'
        $cp = Read-Flag $P 'TeleporterUnlockCount'
        Verdict 'R24: TeleporterUnlockCount agrees on both' ($null -ne $ch -and $ch -eq $cp) ("host={0} peer={1}" -f $ch, $cp)
        [void](Save-Shots 'R24-teleporter')
        Invoke-AuditBoth $H $P 'R24'
        Leave-Area $H
    }

    # ---------------- B2: the Whale King (proximity trap) ----------------
    Say '===== B2 210_AQ_underground: the Whale King ====='
    if (Enter-Area $H $P '210_AQ_underground') {
        $m = Get-Mark $H
        $tr = Invoke-Trigger $H 'WhaleKingSpawner@210_AQ_underground'
        Note ("  trigger: " + $(if ($tr) { $tr } else { '(nothing)' }))
        # THE WHALE SPAWNS BY PROXIMITY, not by the trigger's dispatch (measured 2026-11-16:
        # `ActivateEnemy ran -- managed enemy none yet` then `no managed enemy appeared within
        # 30 s`).  A dispatched overlap is not a player STANDING in the trap volume, so the rig
        # does what the player does: place the pawn 4.5 m away FACING the spawner and post a
        # real W hold, twice if needed.
        $k = $null
        for ($try = 0; $try -lt 3 -and -not $k; $try++) {
            $off = 450 + 200 * $try
            Set-Control $H 'qd.near' ([string]$off)
            $go = Invoke-Go $H 'near:WhaleKingSpawner'
            Note ("    place {0} cm: {1}" -f $off, $go)
            if ($go -and $go -notmatch 'refused|no live actor') {
                [void](Send-Key $H 0x57 (900 + 500 * $try))
                Start-Sleep -Seconds 6
            }
            $keys = @(Read-SpawnKeys $H $m 'Whale|whale')
            if ($keys.Count) { $k = $keys[-1].Key }
            if (-not $k) { $b = Find-Enemy (Get-Enemies $H) 'Whale'; if ($b) { $k = $b.Key } }
        }
        $ft = Wait-New $H 'trigger: boss follow-through \(local\).*[Ww]hale' 10 $m
        Verdict 'B2: the trigger ran the Whale King spawner' ([bool]$ft) $(if ($ft) { $ft } else { '(no follow-through line naming the whale spawner)' })
        Verdict 'B2: the Whale King exists on the HOST with a key' ([bool]$k) $(if ($k) { "key=$k" } else { '(no whale in the enemy table after the trigger and three walk-ins)' })
        if ($k) {
            $r = Test-BossBothSides -H $H -P $P -Tag 'B2' -ClsPattern 'Whale' -Keys @($k)
            [void](Save-Shots 'B2-spawn')
            $hp = Test-RealAttacks -H $H -P $P -Tag 'B2' -Key $k -ClsPattern 'Whale'
            for ($round = 0; $round -lt 12; $round++) {
                $cur = Find-Enemy (Get-Enemies $H) 'Whale'
                if (-not $cur -or $cur.State -eq 'DEAD' -or $cur.Hp -le 0) { break }
                [void](Invoke-Attack $H 'Attack' 300 4)
                [void](Invoke-Attack $P 'Attack' 300 4)
                Start-Sleep -Seconds 2
            }
            $w = Find-Enemy (Get-Enemies $H) 'Whale'
            Verdict 'B2: the Whale King is DEAD on the host' ($w -and ($w.State -eq 'DEAD' -or $w.Hp -le 0)) $(if ($w) { $w.Line } else { '(gone)' })
            $fh = Read-Flag $H 'WhaleKingIsDead'
            $fp = Read-Flag $P 'WhaleKingIsDead'
            Verdict 'B2: WhaleKingIsDead agrees on both' ($null -ne $fh -and $fh -eq $fp) ("host={0} peer={1}" -f $fh, $fp)
            [void](Save-Shots 'B2-dead')
        }
        Invoke-AuditBoth $H $P 'B2'
        Leave-Area $H
    }

    # ---------------- R27: the vault relic and Knight Nu ----------------
    Say '===== R27 420_MAUS_mid: the vault relic and Knight Nu ====='
    if (Enter-Area $H $P '420_MAUS_mid') {
        # The pak has Content/NPCs/Knight/Knight_Nu/Knight_Nu_NPC_BP (+ Ghost_Knight_Nu_Dialogue);
        # `near:Knight_Nu_NPC` matched Knight_Ujji's ghost last time.  Resolve by the exact names
        # and refuse any actor whose own line does not say Nu.
        $tried = @(); $act = $null; $used = $null
        foreach ($nm in @('Knight_Nu_NPC_BP', 'Ghost_Knight_Nu', 'Knight_Nu_NPC', 'Knight_Nu')) {
            [void](Invoke-Go $H ('near:' + $nm) 200)
            $a = Invoke-Act $H ('near:' + $nm + '!Interact')
            $tried += ("{0}: {1}" -f $nm, $a)
            if ($a -and $a -match 'ran=yes' -and $a -notmatch 'Ujji') { $act = $a; $used = $nm; break }
        }
        Verdict 'R27: the HOST talks to the RIGHT Knight Nu (not Ghost_Ujji)' ([bool]$act) ("tried: " + ($tried -join ' ; '))
        if ($act) {
            Start-Sleep -Seconds 3
            for ($k = 0; $k -lt 6; $k++) { $d = Invoke-Dlg $H 'advance'; if (-not $d) { break }; Start-Sleep -Seconds 2 }
            [void](Invoke-Dlg $H 'leave')
            Start-Sleep -Seconds 4
        }
        $kH = Read-Flag $H 'GotKnightKey'
        $kP = Read-Flag $P 'GotKnightKey'
        Verdict 'R27: GotKnightKey agrees on both' ($null -ne $kH -and $kH -eq $kP) ("host={0} peer={1}" -f $kH, $kP)
        $vH = Read-Flag $H 'SR_RustedLeafBlade_Acquired'
        $vP = Read-Flag $P 'SR_RustedLeafBlade_Acquired'
        Verdict 'R27: the second relic flag agrees on both' ($null -ne $vH -and $vH -eq $vP) ("host={0} peer={1}" -f $vH, $vP)
        [void](Save-Shots 'R27-nu')
        Invoke-AuditBoth $H $P 'R27'
        Leave-Area $H
    }
}

# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------
Say ("session-p2b start: phase={0}" -f $Phase)
if ($Phase -eq 'stop') {
    Stop-All 'operator stop'
    $script:lines | Set-Content -LiteralPath $Report
    Say "report: $Report"
    exit 0
}

Assert-LiveLock
Stop-All 'phase start: clearing anything this rig left running'
Invoke-Boot
$H = $script:H
$P = $script:P

try {
    switch ($Phase) {
        'bosses' { Invoke-BossPhase $H $P }
        'damage' { Invoke-DamagePhase $H $P }
        'rest'   { Invoke-RestPhase $H $P }
    }
} catch {
    Say ("HARNESS EXCEPTION: {0}" -f $_.Exception.Message)
    Note ($_.ScriptStackTrace)
}

Note ''
Note '================ SUMMARY ================'
$fail = 0
foreach ($v in $script:verdicts) {
    $t = 'FAIL'; if ($v.Ok) { $t = 'PASS' } else { $fail++ }
    Note ("{0}  {1}" -f $t, $v.What)
}
Note ("{0} pass, {1} fail" -f (@($script:verdicts | Where-Object { $_.Ok }).Count, $fail))

Stop-All 'session complete'
$script:lines | Set-Content -LiteralPath $Report
Say "report: $Report"
if ($fail -gt 0) { exit 1 } else { exit 0 }
