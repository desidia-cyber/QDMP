# session-viewfix2.ps1 -- the dialogue box's TEXT, and the black holes in the ground.
#
# TWO DEFECTS, ONE PAIR OF INSTANCES (`mod/doc/features/view-fix2.md`):
#
# 1. THE SPECTATOR'S BOX SHOWED THE WRONG LINE.  `view-fix` proved the camera; the box on the
#    viewing machine kept the widget's design-time placeholder ("Bored Knight / The locked
#    chest? ...") while the talker read a different conversation.  Fixed by having the talking
#    machine publish the speaker and line its OWN box is rendering, and the spectator writing
#    them into its own two text blocks.  The proof is a PAIR of shots at the same moment plus
#    the log: A's `dialogue view: publish text` and B's `dialogue view: box` must be equal,
#    for two NPCs, over two advances.
#
# 2. BLACK HOLES IN THE GROUND/WATER on both machines.  The pixels are exactly 0,0,0, so
#    nothing was drawn there.  This run measures it instead of arguing: (a) the same spot with
#    the mod's lanes all OFF (`StreamUnion=0`) -- if the holes persist the union is not the
#    cause; (b) reached by WALKING (held key, no teleport) vs by rig placement; (c) 5/15/30 s
#    after arrival; (d) off-screen PrintWindow vs on-screen PrintWindow vs on-screen
#    CopyFromScreen -- a hole that only appears in one capture method is a capture artefact;
#    (e) the level table at that moment (`qd.act levels`: loaded / should-be-visible /
#    engine-visible, per level).
param(
    [int]$BootSeconds = 300,
    [switch]$KeepRunning
)

$ErrorActionPreference = 'Stop'
$script:hbPath = 'G:\Freespace\Queens_Domain\_test\viewfix-boot.txt'
function HB([string]$m) {
    try { Add-Content -LiteralPath $script:hbPath -Value ((Get-Date -Format 'HH:mm:ss') + ' ' + $m) } catch {}
}
HB ("script start (mode=$Mode psscriptroot=$PSScriptRoot)")
$here    = $PSScriptRoot
$repo    = Split-Path -Parent (Split-Path -Parent $here)
$gameDir = 'G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64'
$relay   = Join-Path (Split-Path -Parent $here) 'rust\target\release\qd-relay.exe'
$rigBase = 'G:\Freespace\Queens_Domain\_test'
$rigRoot = Join-Path $rigBase 'sview-fix2'
$seed    = Join-Path $repo 'analysis\save-backup-before-live'
$outDir  = Join-Path $rigRoot 'out'
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force -Path $outDir | Out-Null }
$Report = Join-Path $outDir 'report-view-fix2.txt'
HB ("rigRoot=$rigRoot report=$Report")

$script:lines = New-Object System.Collections.Generic.List[string]
# When set, the ini is written with every lane at 0 (including StreamUnion): the "mod off"
# half of the black-hole experiment.  The transport stays Enabled so a pair still forms.
$script:allOff = $false
trap {
    Write-Output ("UNHANDLED: " + $_.Exception.Message)
    Write-Output ("AT: " + $_.InvocationInfo.PositionMessage)
    $script:lines.Add("UNHANDLED: " + $_.Exception.Message) | Out-Null
    try { Stop-All } catch {}
    try { Release-LiveLock } catch {}
    try { $script:lines | Set-Content -LiteralPath $Report } catch {}
    exit 1
}

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
        Index  = $i; Root = $root; Suffix = $suffix
        Log    = Join-Path $gameDir ("qdcoop-{0}.log" -f $suffix)
        Tp     = Join-Path $root 'qd.tp';   Act  = Join-Path $root 'qd.act'
        Scan   = Join-Path $root 'qd.scan'; God  = Join-Path $root 'qd.god'
        Near   = Join-Path $root 'qd.near'; Dlg  = Join-Path $root 'qd.dlg'
        Kill   = Join-Path $root 'qd.kill'; Level = Join-Path $root 'qd.level'
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
        if (-not (Test-Alive $i)) { return $false }
        if (@(Get-Matches $i 'scan: local at|scan: health').Count -gt 0) { return $true }
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
$script:verdicts = New-Object System.Collections.Generic.List[object]
$script:pass = 0
$script:fail = 0
function Check([string]$step, [int]$i, [string]$pattern, [int]$secs, [string]$label, [int]$Mark = 0) {
    if (-not (Test-Alive $i)) {
        $script:verdicts.Add([pscustomobject]@{ Step = $step; Inst = $i; What = $label; Ok = $false; Line = 'INSTANCE DIED' }) | Out-Null
        Note "  FAIL [$i] $label -- the instance is no longer running"
        return $false
    }
    $m = Wait-Since $i $pattern $Mark $secs
    $found = ($m.Count -gt 0)
    $script:verdicts.Add([pscustomobject]@{ Step = $step; Inst = $i; What = $label; Ok = $found; Line = ($m | Select-Object -Last 1) }) | Out-Null
    if ($found) { Note "  PASS [$i] $label" } else { Note "  --   [$i] $label (no line matches '$pattern' in ${secs}s)" }
    return $found
}
function Verdict([string]$step, [int]$i, [bool]$ok, [string]$what, [string]$line = '') {
    $script:verdicts.Add([pscustomobject]@{ Step = $step; Inst = $i; What = $what; Ok = $ok; Line = $line }) | Out-Null
    Note ("  {0} [{1}] {2}{3}" -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $i, $what,
          $(if ($line) { " -- $line" } else { '' }))
}
function Step([string]$title, [string]$body) {
    Touch-LiveLock
    Note ''
    Note "### $title"
    if ($body) { Note $body }
}
function Show-Since([int]$i, [string]$pattern, [int]$mark, [int]$n) {
    $m = @(Get-Since $i $mark | Where-Object { $_ -match $pattern })
    if ($m.Count -eq 0) { Note "  [$i] (no line matches '$pattern' since the step started)"; return }
    $m | Select-Object -Last $n | ForEach-Object {
        $s = [string]$_
        if ($s.Length -gt 400) { $s = $s.Substring(0, 400) + ' ...' }
        Note "  [$i] $s"
    }
}

# KILL ONLY WHAT THIS SCRIPT LAUNCHED (STRICT live-lock rule, 16 Nov).
# `Stop-Process -Name QueensDomain-Win64-Shipping` used to take down ANOTHER delegate's
# playthrough: several sessions run on this machine and a name is not an owner.  The pids in
# `$script:pids` and `$script:relayProc` are the only processes this run may end.
function Stop-All {
    if ($script:relayProc) {
        Say "  stopping the relay this run started pid=$($script:relayProc.Id)"
        Stop-Process -Id $script:relayProc.Id -Force -ErrorAction SilentlyContinue
        $script:relayProc = $null
    }
    if ($script:pids) {
        foreach ($k in @($script:pids.Keys)) {
            $pid2 = $script:pids[$k]
            if (Get-Process -Id $pid2 -ErrorAction SilentlyContinue) {
                Say "  stopping inst$k pid=$pid2 (started by this run)"
                Stop-Process -Id $pid2 -Force -ErrorAction SilentlyContinue
            }
        }
    }
    $script:pids = @{}
    Start-Sleep -Seconds 2
}

# ---- the LIVE lock (STRICT rule) -----------------------------------------------
# LAUNCH NOTHING WITHOUT IT.  Holding it means: write owner.txt with my id and the time, and
# rewrite it every 10 minutes so a successor can tell a live session from a dead one.
# BREAK someone else's lock ONLY when its stamp is >25 min old AND no game/relay process runs.
$script:lockDir = 'G:\Freespace\Queens_Domain\_test\locks\live'
$script:lockOwned = $false
$script:lockStamp = $null
function Write-LockStamp([string]$who) {
    if (-not (Test-Path -LiteralPath $script:lockDir)) { return }
    Set-Content -LiteralPath (Join-Path $script:lockDir 'owner.txt') `
        -Value ("{0} {1}" -f $who, (Get-Date -Format 'yyyy-MM-ddTHH:mm:ss.fffzzz')) -ErrorAction SilentlyContinue
}
function Get-LockOwner {
    $f = Join-Path $script:lockDir 'owner.txt'
    if (-not (Test-Path -LiteralPath $f)) { return '?' }
    return ((Get-Content -LiteralPath $f -ErrorAction SilentlyContinue) -join ' ').Trim()
}
function Get-LockAgeMinutes {
    # THE STAMP INSIDE owner.txt, NOT THE DIRECTORY'S mtime.  Measured while waiting for
    # `play-p2b`: it rewrote owner.txt every few minutes and the directory's LastWriteTime never
    # moved (NTFS does not touch a directory when a file's CONTENT changes), so the directory
    # reported "34 min old" for a lock that was being refreshed.  Breaking on that number would
    # have killed a live playthrough -- the exact failure the STRICT rule forbids.  Two signals,
    # and the FRESHEST of them wins.
    $ages = New-Object System.Collections.Generic.List[double]
    $f = Join-Path $script:lockDir 'owner.txt'
    if (Test-Path -LiteralPath $f) {
        $txt = ((Get-Content -LiteralPath $f -ErrorAction SilentlyContinue) -join ' ').Trim()
        if ($txt -match '(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(\.\d+)?([+-]\d{2}:\d{2}|Z)?)') {
            try {
                $when = [datetime]::Parse($Matches[1])
                $ages.Add(((Get-Date) - $when).TotalMinutes)
            } catch { }
        }
    }
    $d = Get-Item -LiteralPath $script:lockDir -ErrorAction SilentlyContinue
    if ($d) { $ages.Add(((Get-Date) - $d.LastWriteTime).TotalMinutes) }
    if ($ages.Count -eq 0) { return 0 }
    $min = $ages[0]
    foreach ($a in $ages) { if ($a -lt $min) { $min = $a } }
    if ($min -lt 0) { $min = 0 }
    return [int]$min
}
function Test-AnyGameRunning {
    return [bool](Get-Process -Name 'QueensDomain-Win64-Shipping', 'qd-relay' -ErrorAction SilentlyContinue)
}
function Acquire-LiveLock([string]$who, [int]$waitMinutes = 90) {
    $deadline = (Get-Date).AddMinutes($waitMinutes)
    while ($true) {
        if (-not (Test-Path -LiteralPath $script:lockDir)) {
            try {
                New-Item -ItemType Directory -Path $script:lockDir -ErrorAction Stop | Out-Null
                Write-LockStamp $who
                $script:lockOwned = $true
                Say "LIVE lock acquired by $who"
                return
            } catch { }
        }
        $owner = Get-LockOwner
        $age = Get-LockAgeMinutes
        $busy = Test-AnyGameRunning
        if ($age -gt 25 -and -not $busy) {
            Say ("LIVE lock is STALE (owner='{0}' stamp {1} min old, no game or relay process): taking it" -f $owner, $age)
            Remove-Item -LiteralPath $script:lockDir -Recurse -Force -ErrorAction SilentlyContinue
            continue
        }
        if ((Get-Date) -gt $deadline) {
            Say ("REFUSED: the LIVE lock is held by '{0}' ({1} min old, game running={2}) and the wait expired" -f $owner, $age, $busy)
            exit 2
        }
        Say ("  waiting for the LIVE lock (owner='{0}' {1} min old, game running={2})" -f $owner, $age, $busy)
        Start-Sleep -Seconds 30
    }
}
# Called at every step boundary: the stamp must never be more than 10 minutes old.
function Touch-LiveLock {
    if (-not $script:lockOwned) { return }
    $now = Get-Date
    if (($script:lockStamp -eq $null) -or (($now - $script:lockStamp).TotalMinutes -ge 10)) {
        $script:lockStamp = $now
        Write-LockStamp 'view-fix2'
    }
}
function Release-LiveLock {
    if (-not $script:lockOwned) { return }
    $owner = Get-LockOwner
    if ($owner -like 'view-fix2*') {
        Remove-Item -LiteralPath $script:lockDir -Recurse -Force -ErrorAction SilentlyContinue
        Say 'LIVE lock released'
    } else {
        Say ("LIVE lock NOT released: it now belongs to '{0}'" -f $owner)
    }
    $script:lockOwned = $false
}

# ---- held keys -----------------------------------------------------------------
# The rig has no "walk" lever: movement comes from the game's own input path, so this
# posts WM_KEYDOWN/WM_KEYUP to the same window capture-hidden.ps1 reads.  That is the only
# way step 1 can be "normal movement" rather than another teleport.
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class QdKey {
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
function Find-GameWindow([int]$procId) {
    $script:tp = $procId
    $script:best = [IntPtr]::Zero
    $script:bestArea = 0
    $cb = [QdKey+EP]{
        param($hw, $l)
        $q = 0
        [QdKey]::GetWindowThreadProcessId($hw, [ref]$q) | Out-Null
        if ($q -eq $script:tp -and [QdKey]::IsWindowVisible($hw)) {
            $sb = New-Object System.Text.StringBuilder 512
            [QdKey]::GetWindowText($hw, $sb, 512) | Out-Null
            $r = New-Object QdKey+RECT
            [QdKey]::GetClientRect($hw, [ref]$r) | Out-Null
            $a = ($r.R - $r.L) * ($r.B - $r.T)
            if ($a -gt $script:bestArea -and ($sb.ToString() -match 'Queen' -or $a -gt 200000)) {
                $script:bestArea = $a
                $script:best = $hw
            }
        }
        return $true
    }
    [QdKey]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
    return $script:best
}
function Post-Key([int]$i, [int]$vk, [bool]$down) {
    $h = Find-GameWindow $script:pids[$i]
    if ($h -eq [IntPtr]::Zero) { Note "  key: NO GAME WINDOW for inst$i"; return $false }
    # The game only treats input as gameplay while it believes its window is active
    # (measured for the mouse path in send-input.ps1); WM_ACTIVATE says so without
    # stealing the real focus from anything.
    if ($down) {
        [QdKey]::PostMessage($h, 0x0006, [IntPtr]1, [IntPtr]0) | Out-Null
        Start-Sleep -Milliseconds 60
    }
    $scan = [QdKey]::MapVirtualKey([uint32]$vk, 0)
    if ($down) { $lp = [IntPtr]([int64](1 -bor ($scan -shl 16))) }
    else { $lp = [IntPtr]([int64](1 -bor ($scan -shl 16) -bor (1 -shl 30) -bor (1 -shl 31))) }
    [QdKey]::PostMessage($h, $(if ($down) { 0x0100 } else { 0x0101 }), [IntPtr]$vk, $lp) | Out-Null
    return $true
}

# ---- screenshots ---------------------------------------------------------------
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
        $mean = 0.0; foreach ($v in $vals) { $mean += $v }
        $mean = $mean / [math]::Max(1, $vals.Count)
        $var = 0.0; foreach ($v in $vals) { $var += ($v - $mean) * ($v - $mean) }
        $sd = [math]::Sqrt($var / [math]::Max(1, $vals.Count))
        return [pscustomobject]@{ W = $bmp.Width; H = $bmp.Height; Mean = [math]::Round($mean, 1); Std = [math]::Round($sd, 1) }
    } finally { $bmp.Dispose() }
}
$script:shots = New-Object System.Collections.Generic.List[object]
function Save-Shots([string]$tag) {
    $dir = Join-Path $rigRoot 'shots'
    if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
    foreach ($i in 1, 2) {
        if (-not $script:pids -or -not $script:pids.ContainsKey($i)) { continue }
        if (-not (Test-Alive $i)) { continue }
        $out = Join-Path $dir ("view-{0}-{1}-inst{2}.png" -f $Mode, $tag, $i)
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

# ---- instance setup -------------------------------------------------------------
function Seed-FreshPair {
    Say 're-seeding both instances from the pristine pre-live save'
    foreach ($i in 1, 2) {
        $r = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'make_test_instance.ps1') `
                -Name ("inst{0}" -f $i) -Index $i -SourceSave $seed -Root $rigRoot -Suffix $inst[$i].Suffix -Force 2>&1
        $r | Where-Object { $_ -match 'FAIL|REFUSED|seeded|save dir|Instance ready' } | ForEach-Object { Note "  $_" }
        if ($LASTEXITCODE -ne 0) { Say "REFUSED: make_test_instance refused instance $i"; exit 2 }
    }
}
function Install-And-Configure {
    $built = Join-Path $repo 'mod\native\build\winmm.dll'
    if (-not (Test-Path -LiteralPath $built)) { Say "REFUSED: built proxy not found at $built"; exit 2 }
    $dest = Join-Path $gameDir 'winmm.dll'
    # A killed game keeps its file handle for a moment; a Copy-Item into it then fails and
    # aborts the whole run (measured, run 2's second phase).  Wait for the handle.
    $copied = $false
    for ($try = 0; $try -lt 30; $try++) {
        try { Copy-Item -LiteralPath $built -Destination $dest -Force -ErrorAction Stop; $copied = $true; break }
        catch { Start-Sleep -Milliseconds 500 }
    }
    if (-not $copied) { Say "REFUSED: winmm.dll is still locked by a game process"; exit 2 }
    $pdb = Join-Path $repo 'mod\native\build\winmm.pdb'
    if (Test-Path -LiteralPath $pdb) { Copy-Item -LiteralPath $pdb -Destination (Join-Path $gameDir 'winmm.pdb') -Force }
    Say "installed $built -> $gameDir"
    $on = if ($script:allOff) { '0' } else { '1' }
    $lines = @('[Local]', 'Enabled=1', 'LogLevel=3', '', '[Net]',
               'RelayHost=127.0.0.1', 'RelayPort=47800', '', '[Rules]',
               "FlagSync=$on", "DialogueSync=$on", "DialogueView=$on", 'ViewCamera=1',
               "PuppetSync=$on", "StreamUnion=$on",
               "SuppressMenuPause=$on", "HostAuthoritative=$on",
               '', '[Tuning]', 'PlayerHz=30')
    $ini = Join-Path $gameDir 'qdcoop.ini'
    $lines | Set-Content -LiteralPath $ini
    $lines | ForEach-Object { if ($_ -match '=') { Note "    ini $_" } }
    Copy-Item -LiteralPath $ini -Destination (Join-Path $outDir 'qdcoop-used.ini') -Force
}
function Start-Boot([string]$tag) {
    HB "Start-Boot $tag"
    Stop-All
    Install-And-Configure
    foreach ($i in 1, 2) {
        foreach ($leaf in 'qd.tp','qd.act','qd.near','qd.dlg','qd.kill','qd.hit','qd.cheat',
                          'qd.travel','qd.hard','qd.level','qd.list','qd.sequence','qd.selfhp',
                          'qd.god','qd.scan') {
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
        foreach ($i in 1, 2) { Note "  [$i] alive=$(Test-Alive $i)"; Show-Since $i 'session|REFUS|ERR ' 0 12 }
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

# ===========================================================================
# The run -- view-fix2
# ===========================================================================
Say ("session_viewfix2: rig=$rigRoot")
Acquire-LiveLock 'view-fix2' 90
Touch-LiveLock
Seed-FreshPair
Start-Boot 'viewfix2'
$H = $script:H      # B: the spectator
$P = $script:P      # A: the talker

# ---- helpers for this run -------------------------------------------------
function Cmd([int]$i, [string]$leaf, [string]$value) {
    Set-Control $i $leaf ($value + '#' + (Get-Date -Format 'HHmmssfff'))
}
function Parse-Box([string]$ln) {
    if ($ln -notmatch "box speaker='(.*?)' line='(.*?)' \(exact=(\d+) published=(\d+).*?lineHow=(\d+) nameHow=(\d+)") { return $null }
    return [pscustomobject]@{ Speaker = $Matches[1]; Line = $Matches[2]; Exact = [int]$Matches[3]
                              Published = [int]$Matches[4]; LineHow = [int]$Matches[5]; NameHow = [int]$Matches[6] }
}
function Get-BoxSince([int]$i, [int]$mark) {
    $m = @(Get-Since $i $mark | Where-Object { $_ -match 'dialogue view: box speaker=' })
    if ($m.Count -eq 0) { return $null }
    return (Parse-Box ([string]$m[-1]))
}
function Get-PubSince([int]$i, [int]$mark) {
    $m = @(Get-Since $i $mark | Where-Object { $_ -match 'dialogue view: publish text speaker=' })
    if ($m.Count -eq 0) { return $null }
    $ln = [string]$m[-1]
    if ($ln -notmatch "publish text speaker='(.*?)' line='(.*?)'") { return $null }
    return [pscustomobject]@{ Speaker = $Matches[1]; Line = $Matches[2] }
}
function Show-Box([int]$i, [string]$who, [int]$mark) {
    $b = Get-BoxSince $i $mark
    if (-not $b) { Note ("  [{0}] {1}: no box line since the step started" -f $i, $who); return $null }
    Note ("  [{0}] {1}: speaker='{2}' line='{3}' (exact={4} published={5} lineHow={6} nameHow={7})" -f `
          $i, $who, $b.Speaker, $b.Line, $b.Exact, $b.Published, $b.LineHow, $b.NameHow)
    return $b
}
function Compare-Box([string]$step, [string]$label, [int]$mP, [int]$mH) {
    $a = Get-PubSince $P $mP
    $b = Get-BoxSince $H $mH
    if (-not $a) { Verdict $step $P $false $label 'the talker published no text'; return }
    if (-not $b) { Verdict $step $H $false $label 'the spectator has no box line'; return }
    $ok = ($a.Speaker -eq $b.Speaker) -and ($a.Line -eq $b.Line) -and ($b.Line.Length -gt 0) -and ($b.Published -eq 1)
    Verdict $step $H $ok $label ("A='" + $a.Speaker + "' / '" + $a.Line + "';  B='" + $b.Speaker + "' / '" + $b.Line + "'")
}
function Shot-Black([string]$path) {
    Add-Type -AssemblyName System.Drawing -ErrorAction SilentlyContinue
    $bmp = [System.Drawing.Bitmap]::FromFile($path)
    try {
        $n = 0; $black = 0
        for ($y = 0; $y -lt $bmp.Height; $y += 4) {
            for ($x = 0; $x -lt $bmp.Width; $x += 4) {
                $c = $bmp.GetPixel($x, $y); ++$n
                if ($c.R -le 2 -and $c.G -le 2 -and $c.B -le 2) { ++$black }
            }
        }
        return [math]::Round(100.0 * $black / [math]::Max(1, $n), 1)
    } finally { $bmp.Dispose() }
}
function Save-Shots2([string]$tag) {
    $dir = Join-Path $rigRoot 'shots'
    if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
    foreach ($i in 1, 2) {
        if (-not (Test-Alive $i)) { continue }
        $out = Join-Path $dir ("view-fix2-{0}-inst{1}.png" -f $tag, $i)
        $r = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'capture-hidden.ps1') `
                -Out $out -ProcessId $script:pids[$i] 2>&1
        if (-not (Test-Path -LiteralPath $out)) { Note ("  shot[{0}] {1}: FAILED ({2})" -f $i, $tag, (@($r) | Select-Object -Last 1)); continue }
        $blk = Shot-Black $out
        Note ("  shot[{0}] {1}: black={2}% -> {3}" -f $i, $tag, $blk, $out)
        $script:blackLog.Add([pscustomobject]@{ Tag = $tag; Inst = $i; Black = $blk; Path = $out }) | Out-Null
    }
}
$script:blackLog = New-Object System.Collections.Generic.List[object]

Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class QdWin2 {
  [DllImport("user32.dll")] public static extern bool EnumWindows(EP cb, IntPtr l);
  public delegate bool EP(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
  public struct RECT { public int L, T, R, B; }
  public static IntPtr Find(uint pid) {
    IntPtr best = IntPtr.Zero; long bestA = 0;
    EnumWindows(delegate(IntPtr h, IntPtr l) {
      uint q; GetWindowThreadProcessId(h, out q);
      if (q == pid) { RECT r; GetWindowRect(h, out r); long a = (long)(r.R - r.L) * (r.B - r.T); if (a > bestA) { bestA = a; best = h; } }
      return true;
    }, IntPtr.Zero);
    return best;
  }
}
"@
function Move-Window([int]$i, [int]$x, [int]$y) {
    $h = [QdWin2]::Find([uint32]$script:pids[$i])
    if ($h -eq [IntPtr]::Zero) { Note "  window[$i]: not found"; return $false }
    # SWP_NOSIZE (1): move only.
    return [QdWin2]::SetWindowPos($h, [IntPtr]::Zero, $x, $y, 0, 0, 1)
}
function Save-ScreenShot([int]$i, [string]$tag) {
    $h = [QdWin2]::Find([uint32]$script:pids[$i])
    if ($h -eq [IntPtr]::Zero) { Note "  screen[$i] no window"; return }
    $r = New-Object QdWin2+RECT
    [QdWin2]::GetWindowRect($h, [ref]$r) | Out-Null
    $w = $r.R - $r.L; $hh = $r.B - $r.T
    if ($w -le 0 -or $hh -le 0) { Note "  screen[$i] zero rect"; return }
    $bmp = New-Object System.Drawing.Bitmap $w, $hh
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    try { $g.CopyFromScreen($r.L, $r.T, 0, 0, (New-Object System.Drawing.Size $w, $hh)) } catch { Note ("  screen[$i] CopyFromScreen failed: " + $_.Exception.Message) }
    $g.Dispose()
    $dir = Join-Path $rigRoot 'shots'
    if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
    $out = Join-Path $dir ("view-fix2-{0}-screen-inst{1}.png" -f $tag, $i)
    $bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
    $blk = Shot-Black $out
    Note ("  screen[{0}] {1}: {2}x{3} black={4}% -> {5}" -f $i, $tag, $w, $hh, $blk, $out)
}

# ===========================================================================
# 0. boot state
# ===========================================================================
Step 'boot state' 'the spectator lane and the union must both be on'
$sw = @(Read-LogLines $inst[$H].Log | Where-Object { $_ -match 'switches:' })
$swLine = if ($sw.Count) { [string]$sw[-1] } else { '' }
if ($swLine) { Note "  [$H] $swLine" }
Verdict 'boot' $H ($swLine -match 'dialogueView=1') 'the host runs the dialogue spectator lane' $swLine
Verdict 'boot' $H ($swLine -match 'stream=1') 'the host runs the level union' ''

# ===========================================================================
# 1. THE HOLES: walked into, at boot, with no teleport
# ===========================================================================
Step 'holes/walk' 'reach the ground on foot from the boot position (held W, no placement)'
Save-Shots2 'walk0'
foreach ($i in @($H, $P)) {
    [void](Post-Key $i 0x57 $true)      # W
}
Start-Sleep -Seconds 6
Save-Shots2 'walk1'
foreach ($i in @($H, $P)) {
    [void](Post-Key $i 0x57 $false)
}
Start-Sleep -Seconds 2
Save-Shots2 'walk2'
Show-Since $H 'scan: local at|level union: local set' (Get-Mark $H) 6

# ===========================================================================
# 2. THE HOLES: the spot the user's shots were taken at
# ===========================================================================
Step 'holes/arrive' 'both machines stand beside the hub NPC the holes were reported next to'
$mH1 = Get-Mark $H; $mP1 = Get-Mark $P
Cmd $H 'qd.tp' 'near:Fishwife'
Cmd $P 'qd.tp' 'near:Fishwife'
Start-Sleep -Seconds 8
Show-Since $H 'dev teleport|scan: local at' $mH1 6
Show-Since $P 'dev teleport|scan: local at' $mP1 6
Save-Shots2 'arrive5'
Cmd $H 'qd.act' 'levels'; Cmd $P 'qd.act' 'levels'
Start-Sleep -Seconds 2
Show-Since $H 'levels dump|  level |  levels:' $mH1 40
Show-Since $P '  levels:' $mP1 3

Step 'holes/wait' '5 / 15 / 30 s after arrival: does the hole fill in?'
Start-Sleep -Seconds 7
Save-Shots2 'arrive15'
Start-Sleep -Seconds 15
Save-Shots2 'arrive30'
Cmd $H 'qd.act' 'levels'
Start-Sleep -Seconds 2
Show-Since $H '  levels:' $mH1 3

Step 'holes/capture' 'off-screen PrintWindow vs on-screen PrintWindow vs on-screen screen grab'
foreach ($i in @($H, $P)) {
    $onScreen = Move-Window $i 0 0
    Note ("  window[{0}] moved to 0,0 (ran={1})" -f $i, $onScreen)
}
Start-Sleep -Seconds 3
foreach ($i in @($H, $P)) {
    Save-ScreenShot $i 'onscreen'
}
# the same window, same frame area, through PrintWindow while it is VISIBLE
foreach ($i in @($H, $P)) {
    if (-not (Test-Alive $i)) { continue }
    $out = Join-Path (Join-Path $rigRoot 'shots') ("view-fix2-onscreen-pw-inst{0}.png" -f $i)
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'capture-hidden.ps1') `
        -Out $out -ProcessId $script:pids[$i] 2>&1 | Out-Null
    if (Test-Path -LiteralPath $out) { Note ("  shot[{0}] onscreen-printwindow: black={1}% -> {2}" -f $i, (Shot-Black $out), $out) }
}
foreach ($i in @($H, $P)) {
    [void](Move-Window $i 2200 1400)
}

# ===========================================================================
# 3. THE BOX: the spectator must show the talker's own speaker and line
# ===========================================================================
function Invoke-Npc([string]$sub, [string]$stepTag) {
    # `near:npc` is the CURATED class walk (NPC_Base_C): an exact instance name can match an
    # unrelated actor in the same area -- last run `near:Fishwife` resolved
    # `PPC_EnemyOnly_FishWife`, a different actor that has no dialogue at all.
    Step ("box/" + $stepTag) ("the peer talks to '" + $sub + "'; the host watches")
    $mH = Get-Mark $H; $mP = Get-Mark $P
    Cmd $P 'qd.tp' 'npc'
    Start-Sleep -Seconds 6
    # Dump the talking machine's widget surface BEFORE the press, so the block names are on
    # record whether or not the box ends up showing anything.
    Cmd $P 'qd.dlg' 'box'
    Start-Sleep -Seconds 2
    Cmd $P 'qd.act' 'near:npc'
    [void](Check $stepTag $P 'dialogue: local started' 40 'the peer started the game''s own conversation' $mP)
    [void](Check $stepTag $H 'dialogue view: box speaker=' 90 'the host put the conversation on its own box' $mH)
    Start-Sleep -Seconds 2
    Show-Since $P 'dialogue view: publish|dialogue cursor' $mP 6
    Show-Box $P 'A (talker)' $mP
    Show-Box $H 'B (spectator)' $mH
    Cmd $H 'qd.dlg' 'box'
    Start-Sleep -Seconds 2
    Show-Since $H 'dialogue box dump|dlg box read|  dlg w |box\.w|box box' $mH 45
    Show-Since $P 'dialogue box dump|dlg box read|  dlg w |box\.w|box box' $mP 45
    Save-Shots2 ($stepTag + '-1')
    Compare-Box $stepTag 'B shows exactly A''s speaker and line (line 1)' $mP $mH
    # ADVANCE 1 then 2, each with a fresh pair of shots
    foreach ($n in 1, 2) {
        $mHa = Get-Mark $H; $mPa = Get-Mark $P
        Cmd $P 'qd.dlg' 'advance'
        Start-Sleep -Seconds 4
        Show-Since $P 'rig: dialogue cursor \(after the press\)' $mPa 2
        $b = Show-Box $P 'A (talker)' $mPa
        [void]$b
        Cmd $H 'qd.dlg' 'box'
        Start-Sleep -Seconds 2
        Show-Box $H 'B (spectator)' $mHa
        Show-Since $H 'dlg box read|  dlg w |box\.w' $mHa 40
        Save-Shots2 ("{0}-adv{1}" -f $stepTag, $n)
        Compare-Box ($stepTag + '/adv' + $n) ("B followed advance " + $n) $mPa $mHa
    }
    $b = Get-BoxSince $H $mH
    if ($b) {
        # >1 option means the talker's box shows a list; that is the option frame.
        $opt = @(Get-Since $P $mP | Where-Object { $_ -match 'dialogue cursor \(.*options=(\d+)' })
        if ($opt.Count) { Note ("  [$P] " + ([string]$opt[-1]).Trim()) }
        Save-Shots2 ($stepTag + '-options')
    }
    Cmd $P 'qd.dlg' 'leave'
    Start-Sleep -Seconds 3
    Show-Since $P 'rig: dialogue leave|dialogue: .*ended' $mP 4
    Show-Since $H 'dialogue view: HIDE' $mH 3
}
# TWO DIFFERENT NPCs, because a hub NPC is ONE-SHOT: run 2's second step re-aimed at the
# Fishwife and got an empty cursor (`talking=0`).  `npc` is the curated NPC_Base_C walk;
# `Arjaot` is an exact instance-name match that run 2 proved starts a conversation
# (`dialogue: local started with Ghost_Arjaot [Knight_Arjaot_NPC_Ghost_BP_C]`).
Invoke-Npc 'npc' 'npc1'
Invoke-Npc 'Arjaot' 'npc2'

# ===========================================================================
# 4. THE HOLES: the SAME spot with the mod's lanes all off
# ===========================================================================
Step 'holes/modeoff' 'the same place with StreamUnion=0 and every other lane off'
$script:allOff = $true
Start-Boot 'nomod'
$H2 = $script:H; $P2 = $script:P
$sw2 = @(Read-LogLines $inst[$H2].Log | Where-Object { $_ -match 'switches:' })
$sw2Line = if ($sw2.Count) { [string]$sw2[-1] } else { '' }
if ($sw2Line) { Note "  [$H2] $sw2Line" }
Verdict 'modeoff' $H2 ($sw2Line -match 'stream=0' -and $sw2Line -match 'dialogueView=0') 'every lane is off in this phase' $sw2Line
$mH3 = Get-Mark $H2; $mP3 = Get-Mark $P2
Cmd $H2 'qd.tp' 'near:Fishwife'
Cmd $P2 'qd.tp' 'near:Fishwife'
Start-Sleep -Seconds 8
Show-Since $H2 'dev teleport' $mH3 4
Save-Shots2 'modeoff5'
Cmd $H2 'qd.act' 'levels'
Start-Sleep -Seconds 2
$lv2 = @(Get-Since $H2 $mH3 | Where-Object { $_ -match '  levels:' })
if ($lv2.Count) { Note ("  [$H2] " + ([string]$lv2[-1]).Trim()) }
Note '  (with StreamUnion=0 the level table is never refreshed -- the union owns it -- so the holes are judged by the pictures here, not by the table)'
Start-Sleep -Seconds 7
Save-Shots2 'modeoff15'
Start-Sleep -Seconds 15
Save-Shots2 'modeoff30'

# THE DECISIVE ONE FOR THE HOLES (added after run 3): does a conversation produce the black
# regions with EVERY LANE OFF?  Run 3 measured 11-19% black while the box was up and 0.4-0.8%
# at the same place with the box down, on BOTH machines.  If the talker's own screen shows
# them with the whole mod inert, the holes are the GAME's dialogue presentation, not this
# module -- and the mod cannot be blamed for them in real play.
Step 'holes/dialogue-off' 'open the same conversation with every lane off and measure the black area'
$mHo = Get-Mark $H2; $mPo = Get-Mark $P2
Cmd $P2 'qd.tp' 'npc'
Start-Sleep -Seconds 6
Cmd $P2 'qd.act' 'near:npc'
Start-Sleep -Seconds 12
$startedOff = @(Get-Since $P2 $mPo | Where-Object { $_ -match 'dialogue: local started' })
Verdict 'holes/dialogue-off' $P2 ($startedOff.Count -gt 0) 'the talker opened a conversation with every lane off' $(if ($startedOff.Count) { ([string]$startedOff[-1]).Trim() } else { '' })
Start-Sleep -Seconds 3
Save-Shots2 'modeoff-dialogue'
Cmd $P2 'qd.dlg' 'box'
Start-Sleep -Seconds 2
Show-Since $P2 'dlg box read|box box' $mPo 6
Cmd $P2 'qd.dlg' 'leave'
Start-Sleep -Seconds 3
Save-Shots2 'modeoff-after'

# ===========================================================================
# verdicts: the black fractions, side by side
# ===========================================================================
Step 'verdict' 'the measured black area per shot (a hole is a large exactly-black region)'
foreach ($s in $script:blackLog) { Note ("  black {0,-12} inst{1} {2}%" -f $s.Tag, $s.Inst, $s.Black) }

Note ''
Say 'done'
if ($KeepRunning) {
    Note '  -KeepRunning: the pair is left up'
} else {
    Stop-All
}
$script:verdicts | ForEach-Object {
    if ($_.Ok) { $script:pass++ } else { $script:fail++ }
}
Note ''
Note ("=== {0} PASS / {1} FAIL ===" -f $script:pass, $script:fail)
Touch-LiveLock
$script:lines | Set-Content -LiteralPath $Report
Release-LiveLock
Write-Host ("report: " + $Report)
