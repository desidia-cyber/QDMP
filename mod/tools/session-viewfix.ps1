# session-viewfix.ps1 -- the dialogue spectator CAMERA and the far-apart streaming check.
#
# WHAT THIS RUN ANSWERS, ONE SCREENSHOT PAIR AT A TIME
# ----------------------------------------------------
# A. Is the spectator camera at the TALKER'S EYE instead of inside the NPC?  The user's
#    report (`_test/sdlg-spectator/shots/sdlg-spectator-during-inst1.png`) showed the
#    dialogue box readable, the NPC invisible and the ground clipping -- the POV was the
#    NPC actor's ORIGIN.  `-Mode fix` runs with `ViewCamera=1` (the temporary CameraActor);
#    `-Mode before` runs the same scene with `ViewCamera=0` (the old
#    `SetViewTargetWithBlend(npc)`) so the two shots are the same moment on one build.
#
# B. Do the two machines keep each other's levels when the players separate in NORMAL
#    MOVEMENT?  Step 1 walks the peer away with a held key (no teleport) and reads
#    `level union:` on both machines; the shots say whether either screen lost its world.
#
# Every step is a log line or a screenshot.  A step that cannot be reached says so and
# does not claim the rest.
param(
    [ValidateSet('fix', 'before')]
    [string]$Mode = 'fix',
    [int]$BootSeconds = 300,
    [int]$WalkSeconds = 25,
    [switch]$SkipWalk,
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
$rigRoot = Join-Path $rigBase ('sview-' + $Mode)
$seed    = Join-Path $repo 'analysis\save-backup-before-live'
$outDir  = Join-Path $rigRoot 'out'
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force -Path $outDir | Out-Null }
$Report = Join-Path $outDir ('report-view-' + $Mode + '.txt')
HB ("rigRoot=$rigRoot report=$Report")

$script:lines = New-Object System.Collections.Generic.List[string]
trap {
    Write-Output ("UNHANDLED: " + $_.Exception.Message)
    Write-Output ("AT: " + $_.InvocationInfo.PositionMessage)
    $script:lines.Add("UNHANDLED: " + $_.Exception.Message) | Out-Null
    try { Stop-All } catch {}
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

function Stop-All {
    Get-Process -Name 'QueensDomain-Win64-Shipping', 'qd-relay' -ErrorAction SilentlyContinue |
        ForEach-Object {
            Say "  stopping leftover $($_.ProcessName) pid=$($_.Id)"
            Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue
        }
    Start-Sleep -Seconds 2
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
    Copy-Item -LiteralPath $built -Destination (Join-Path $gameDir 'winmm.dll') -Force
    $pdb = Join-Path $repo 'mod\native\build\winmm.pdb'
    if (Test-Path -LiteralPath $pdb) { Copy-Item -LiteralPath $pdb -Destination (Join-Path $gameDir 'winmm.pdb') -Force }
    Say "installed $built -> $gameDir"
    $viewCam = if ($Mode -eq 'fix') { '1' } else { '0' }
    $lines = @('[Local]', 'Enabled=1', 'LogLevel=3', '', '[Net]',
               'RelayHost=127.0.0.1', 'RelayPort=47800', '', '[Rules]',
               'FlagSync=1', 'DialogueSync=1', 'DialogueView=1', "ViewCamera=$viewCam",
               'PuppetSync=1', 'StreamUnion=1',
               'SuppressMenuPause=1', 'HostAuthoritative=1',
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
# The run
# ===========================================================================
Say ("session_viewfix: mode=$Mode rig=$rigRoot")
Seed-FreshPair
Start-Boot ('view-' + $Mode)
$H = $script:H      # the viewer (B)
$P = $script:P      # the talker (A)

Step 'boot state' 'the switches line must say dialogueView=1, stream=1 and the mode''s ViewCamera'
$sw = @(Read-LogLines $inst[$H].Log | Where-Object { $_ -match 'switches:' })
$swLine = if ($sw.Count) { [string]$sw[-1] } else { '' }
if ($swLine) { Note "  [$H] $swLine" }
Verdict 'boot' $H ($swLine -match 'dialogueView=1') 'the host runs the dialogue spectator lane' $swLine
Verdict 'boot' $H ($swLine -match 'stream=1') 'the host runs the level union (defect B needs it)' ''
$wantCam = if ($Mode -eq 'fix') { 'viewCamera=1' } else { 'viewCamera=0' }
Verdict 'boot' $H ($swLine -match [regex]::Escape($wantCam)) ("the switches line reports $wantCam") ''
# A build that does not know the switch is a build where "before" and "after" are the same.
Verdict 'boot' $H ($swLine -match 'viewCamera=') 'this build HAS the ViewCamera switch' ''

# --- 1. the two players separate in NORMAL MOVEMENT -------------------------
if ($SkipWalk) {
    Step '1. (skipped) the walk-apart step' '-SkipWalk'
} else {
Step '1. the peer WALKS away (a held key, no teleport); both worlds must stay whole' `
     "hold W on inst$P for $WalkSeconds s, then read the position and the level union on both"
$posBefore = @(Get-Matches $P 'scan: local at') | Select-Object -Last 1
if ($posBefore) { Note "  [$P] BEFORE $([string]$posBefore)".Trim() }
[void](Post-Key $P 0x57 $true)          # 'W'
Start-Sleep -Seconds $WalkSeconds
[void](Post-Key $P 0x57 $false)
Start-Sleep -Seconds 2
$posAfter = @(Get-Matches $P 'scan: local at') | Select-Object -Last 1
if ($posAfter) { Note "  [$P] AFTER  $([string]$posAfter)".Trim() }
$moved = ($posBefore -and $posAfter -and $posBefore -ne $posAfter)
Verdict 'far' $P $moved 'the walk moved the peer (input, not a teleport)' ($(if ($posAfter) { [string]$posAfter } else { '' }))
$mH = @(Get-Matches $H 'level union: local set|level union size|level union: partner set')
$mP = @(Get-Matches $P 'level union: local set|level union size|level union: partner set')
Note "  [$H] union lines=$($mH.Count), [$P] union lines=$($mP.Count)"
foreach ($ln in ($mH | Select-Object -Last 3)) { Note "  [$H] $([string]$ln)".Trim() }
foreach ($ln in ($mP | Select-Object -Last 3)) { Note "  [$P] $([string]$ln)".Trim() }
Verdict 'far' $H ($mH.Count -gt 0) 'the host publishes and receives a level set while apart' ''
Verdict 'far' $P ($mP.Count -gt 0) 'the peer does too' ''
Save-Shots 'far'
}

# --- 2. A goes back to the NPC and starts a conversation ---------------------
Step '2. the peer returns to the hub NPC and starts the game''s own conversation' `
     'qd.tp near:npc then qd.act near:npc -- the TALKER is A, the VIEWER is B'
Set-Control $P 'qd.near' '200'
Set-Control $P 'qd.tp' ('near:npc,' + ((Get-Random) % 360))
Start-Sleep -Seconds 8
$mP0 = Get-Mark $P
Set-Control $P 'qd.act' ('near:npc#' + (Get-Date -Format 'HHmmssfff'))
$started = Check 'dlg' $P 'dialogue: local started' 40 'a conversation started on the peer' $mP0
if (-not $started) {
    Show-Since $P 'dialogue:|dev interact:|rig: no live|near:' $mP0 14
    Say 'REFUSED: the peer never started a conversation, so there is nothing to spectate'
} else {
    Show-Since $P 'dialogue: local started|dialogue view: publish' $mP0 6

    # --- 3. the viewer's camera ----------------------------------------------
    Step '3. the viewer must put its camera at the TALKER''s eye' 'the spectator camera lane'
    $mH0 = Get-Mark $H
    [void](Check 'cam' $H 'dialogue view: SHOW from slot' 25 'the host engaged the spectator view' $mH0)
    [void](Check 'cam' $H 'dialogue view: widget visible=' 15 'the dialogue widget came up' $mH0)
    Show-Since $H 'dialogue view:' $mH0 20
    if ($Mode -eq 'fix') {
        # The cut is HELD until the talker's place is on screen, so this is the line that
        # says the camera actually moved -- and the wait's reason is printed above it.
        $cut = Check 'cam' $H 'dialogue view: camera -> the talker' 30 'the host cut to the TALKER''s eye (not the NPC)' $mH0
        if ($cut) {
            $l = @(Get-Since $H $mH0 | Where-Object { $_ -match 'dialogue view: camera -> the talker' }) | Select-Object -Last 1
            Note "  [$H] $(([string]$l).Trim())"
        }
        [void](Check 'cam' $H 'dialogue view: NPC aim for ' 15 'the NPC''s head height was measured off the actor' $mH0)
        $wait = @(Get-Since $H $mH0 | Where-Object { $_ -match 'dialogue view: the talker''s place .* is on screen after|dialogue view: the talker''s place .* was NOT on screen' })
        Verdict 'cam' $H ($wait.Count -gt 0) 'the cut reports whether the place was on screen (the wait)' $(if ($wait.Count) { ([string]$wait[-1]).Trim() } else { '' })
    } else {
        [void](Check 'cam' $H 'dialogue view: camera -> .*ViewCamera=0' 25 'the host used the OLD lever (camera onto the NPC actor)' $mH0)
    }
    Start-Sleep -Seconds 2
    Save-Shots 'during'

    # --- 4. the viewer follows an ADVANCE ------------------------------------
    Step '4. the talker advances; the viewer follows the line' 'qd.dlg = advance#<nonce>'
    $mH1 = Get-Mark $H
    Set-Control $P 'qd.dlg' ('advance#' + (Get-Date -Format 'HHmmssfff'))
    $upd = @(Wait-Since $H 'dialogue view: update line=' $mH1 20)
    Verdict 'dlg' $H ($upd.Count -gt 0) 'the viewer followed the advance' $(if ($upd.Count) { ([string]$upd[-1]).Trim() } else { '' })
    Show-Since $H 'dialogue view:' $mH1 8
    Save-Shots 'advanced'

    # --- 5. the end: A dies (the game's own end path), B restores ------------
    Step '5. the conversation ends; the viewer gets its own camera and legs back' 'qd.kill on the talker'
    # THE MARK GOES BEFORE THE LEVER.  Measured in this script's first run: the kill ran and
    # B hid the view 80 ms later, and the mark taken AFTER the 4 s sleep could never see it.
    $mH2 = Get-Mark $H
    Set-Control $P 'qd.kill' ('1#' + (Get-Date -Format 'HHmmssfff'))
    Start-Sleep -Seconds 4
    $hid = @(Wait-Since $H 'dialogue view: HIDE' $mH2 30)
    Verdict 'end' $H ($hid.Count -gt 0) 'the viewer released the view' $(if ($hid.Count) { ([string]$hid[-1]).Trim() } else { '' })
    $rest = @(Wait-Since $H 'dialogue view: camera restored' $mH2 15)
    Verdict 'end' $H ($rest.Count -gt 0) 'the viewer''s camera went back to its own pawn' ''
    if ($Mode -eq 'fix') {
        $gone = @(Wait-Since $H 'dialogue view: temporary camera destroyed' $mH2 15)
        Verdict 'end' $H ($gone.Count -gt 0) 'the TEMPORARY camera was destroyed' ''
    }
    Show-Since $H 'dialogue view:' $mH2 10
    Start-Sleep -Seconds 2
    Save-Shots 'after'
}

Step '6. both machines survived' ''
foreach ($i in 1, 2) {
    Verdict 'alive' $i (Test-Alive $i) 'the instance is still running' ''
    # A word boundary: the first run's pattern 'FAULT' matched the word "default" in a
    # healthy log line and failed the survival check for a machine that was fine.
    $faults = @(Get-Matches $i '\bFAULT\b|page fault|Unhandled exception')
    Verdict 'alive' $i ($faults.Count -eq 0) 'no FAULT line' $(if ($faults.Count) { [string]$faults[-1] } else { '' })
}

# ===========================================================================
# Report
# ===========================================================================
$pass = @($script:verdicts | Where-Object { $_.Ok }).Count
$fail = @($script:verdicts | Where-Object { -not $_.Ok }).Count
Note ''
Note '================ SUMMARY ================'
foreach ($v in $script:verdicts) {
    $s = "  {0} {1,-12} [{2}] {3}" -f $(if ($v.Ok) { 'PASS' } else { 'FAIL' }), $v.Step, $v.Inst, $v.What
    if ($v.Line) {
        $l = [string]$v.Line
        if ($l.Length -gt 220) { $l = $l.Substring(0, 220) + ' ...' }
        $s += " -- $l"
    }
    Note $s
}
Note ("  {0} PASS / {1} FAIL" -f $pass, $fail)
if ($script:shots.Count -gt 0) {
    Note '  screenshots:'
    foreach ($s in $script:shots) { Note ("    [{0}] {1} mean={2} stddev={3} {4}" -f $s.Inst, $s.Tag, $s.Mean, $s.Std, $s.Path) }
}
$script:lines | Set-Content -LiteralPath $Report
Say "report: $Report"

if (-not $KeepRunning) { try { Stop-All } catch {} }
if ($fail -gt 0) { exit 1 }
exit 0
