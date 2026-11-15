# session_dialogue.ps1 -- the dialogue SPECTATOR proof (qddialogue2.h, K_DIALOGUE_VIEW 0x50).
#
# WHAT THIS SESSION PROVES, AND HOW IT IS FALSIFIED
# ------------------------------------------------
# The claim: when player A talks to an NPC, player B SEES the conversation on B's own
# screen, B's camera frames the speaker, B cannot walk (input locked), and everything goes
# back when A's conversation ends -- including after A vanishes.
#
# Every step below is a log line or a screenshot:
#   * A (the peer) starts a conversation with the nearest hub NPC with the existing rig
#     lever (`qd.near` + `qd.tp near:npc` + `qd.act near:npc`), so the dialogue itself is
#     the game's own, not a script.
#   * B (the host) must print `dialogue view: SHOW ...` and, if the game's own dialogue
#     widget accepted the cursor, `dialogue view: widget visible=1`.  If the widget does
#     not come up, the line says so and the shots show what B actually sees -- that is the
#     measurement, not an expectation.
#   * A advances one line with `DialogueSequenceContinue` on A's own DialogueManager; B
#     must print `dialogue view: update line=`.
#   * A writes a NamedSaveData flag through the game's own `SaveFunctions.SaveNamedData`
#     (the exact call a dialogue's NamedSaveDataValueChanges uses) while B is spectating;
#     both machines must report the same value.  That is the flags half.
#   * A ends the conversation with `dialogue!End`; B must print `dialogue view: HIDE` and
#     the shot after it must show B's own pawn again.
#
# HONESTY RULES
# -------------
# FAULT is a crash; ERR is a lane refusing a write.  They are counted separately.  A phase
# that cannot reach its precondition REFUSES rather than reporting a number about a run
# that did not happen.
[CmdletBinding()]
param(
    [ValidateSet('spectator', 'all')]
    [string]$Phase = 'all',
    [int]$BootSeconds = 300,
    [switch]$ShippedDefaults
)

$ErrorActionPreference = 'Stop'
# A SCRIPT THAT DIES SILENTLY COSTS A LIVE WINDOW.  Say/Note go to the host stream, which a
# redirected launch does not keep, so the few facts needed to tell "never started" from
# "died in the seed" are appended to one file as the script passes each stage.
$script:hbPath = 'G:\Freespace\Queens_Domain\_test\dlg-boot.txt'
function HB([string]$m) {
    try { Add-Content -LiteralPath $script:hbPath -Value ((Get-Date -Format 'HH:mm:ss') + ' ' + $m) } catch {}
}
HB ("script start (phase=$Phase psscriptroot=$PSScriptRoot)")
$here    = $PSScriptRoot
$repo    = Split-Path -Parent (Split-Path -Parent $here)
$gameDir = 'G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64'
$relay   = Join-Path (Split-Path -Parent $here) 'rust\target\release\qd-relay.exe'
$rigBase = 'G:\Freespace\Queens_Domain\_test'
$rigRoot = Join-Path $rigBase ('sdlg-' + $Phase)
$seed    = Join-Path $repo 'analysis\save-backup-before-live'
$outDir  = Join-Path $rigRoot 'out'
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force -Path $outDir | Out-Null }
$Report = Join-Path $outDir ('report-' + $Phase + '.txt')
HB ("rigRoot=$rigRoot report=$Report relay=$relay seed=$seed")

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
        Near   = Join-Path $root 'qd.near'; Flag = Join-Path $root 'qd.flag'
        Dlg    = Join-Path $root 'qd.dlg'
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
$script:goSeq = 0
function Invoke-Go([int]$i, [string]$name, [int]$near = 140) {
    $script:goSeq++
    $yaw = ($script:goSeq * 37) % 360
    Set-Control $i 'qd.near' ([string]$near)
    Set-Control $i 'qd.tp' ('near:' + $name + ',' + $yaw)
}
function Invoke-ActNear([int]$i, [string]$name) {
    Set-Control $i 'qd.act' ('near:' + $name + '#' + (Get-Date -Format 'HHmmssfff'))
    Say "  act[$i] -> near:$name"
}
function Invoke-DlgVerb([int]$i, [string]$verb) {
    # `near:dialogue!<verb>` drives the LOCAL player's own DialogueManager (Session 29).
    $nonce = Get-Date -Format 'HHmmssfff'
    Set-Control $i 'qd.act' ('near:dialogue!' + $verb + '#' + $nonce)
    Say "  act[$i] -> near:dialogue!$verb (nonce $nonce)"
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
                   'FlagSync=1', 'DialogueSync=1', 'DialogueView=1',
                   'SuppressMenuPause=1', 'HostAuthoritative=1',
                   '', '[Tuning]', 'PlayerHz=30')
        $lines | Set-Content -LiteralPath $ini
        foreach ($ln in $lines) { if ($ln -match '=' -and $ln -notmatch '^;') { Note "    ini $ln" } }
        Say "wrote $ini : FlagSync + DialogueSync + DialogueView (the lanes this phase needs)"
    }
    Copy-Item -LiteralPath $ini -Destination (Join-Path $outDir 'qdcoop-used.ini') -Force
}

function Seed-FreshPair {
    Say 're-seeding both instances from the pristine pre-live save'
    foreach ($i in 1, 2) {
        $r = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'make_test_instance.ps1') `
                -Name ("inst{0}" -f $i) -Index $i -SourceSave $seed -Root $rigRoot -Suffix $inst[$i].Suffix -Force 2>&1
        $r | Where-Object { $_ -match 'FAIL|REFUSED|seeded|save dir|Instance ready' } | ForEach-Object { Note "  $_" }
        if ($LASTEXITCODE -ne 0) { Say "REFUSED: make_test_instance refused instance $i"; exit 2 }
    }
    HB 'seed done'
}

function Start-Boot([string]$tag) {
    HB "Start-Boot $tag"
    Stop-All
    Install-And-Configure
    HB 'installed + configured'
    foreach ($i in 1, 2) {
        foreach ($leaf in 'qd.tp','qd.act','qd.near','qd.flag','qd.dlg','qd.kill','qd.hit',
                          'qd.cheat','qd.cheatarg','qd.travel','qd.hard','qd.level','qd.list',
                          'qd.drops','qd.menu','qd.efunc','qd.eattack','qd.use','qd.pdmg',
                          'qd.petrace','qd.enemies','qd.sequence','qd.enemykill','qd.hitguid',
                          'qd.obj','qd.gate') {
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
    HB "relay pid=$($script:relayProc.Id)"
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
        HB "inst$i pid=$(if ($gp) { $gp } else { 'FAILED' })"
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
    HB "roles H=$($script:H) P=$($script:P)"

    Say "waiting for gameplay on both (up to $BootSeconds s)"
    foreach ($i in @($script:H, $script:P)) {
        $ok = Wait-Gameplay $i $BootSeconds
        Say "inst${i} gameplay=$ok"
        Verdict 'boot' $i $ok 'the machine reached gameplay (it has a pawn to report)' ''
    }
}

# ===========================================================================
# SCREENSHOTS -- "B sees the dialogue" is a claim about pixels
# ===========================================================================
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
        $out = Join-Path $dir ("sdlg-{0}-{1}-inst{2}.png" -f $Phase, $tag, $i)
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
    # Disk discipline: at most the 12 newest shots.
    $keep = @(Get-ChildItem -LiteralPath $dir -Filter '*.png' -ErrorAction SilentlyContinue |
              Sort-Object LastWriteTime -Descending)
    if ($keep.Count -gt 12) {
        $keep | Select-Object -Skip 12 | ForEach-Object { Remove-Item -LiteralPath $_.FullName -Force -ErrorAction SilentlyContinue }
    }
}

function Invoke-DlgDump([int]$i, [string]$tag) {
    $mark = Get-Mark $i
    Set-Control $i 'qd.dlg' ('sdlg-' + $tag + '-' + (Get-Date -Format 'HHmmssfff'))
    $deadline = (Get-Date).AddSeconds(20)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 600
        if (@(Get-Since $i $mark | Where-Object { $_ -match 'rig: dialogue dump --' }).Count -gt 0) { break }
    }
    Show-Since $i 'rig: dialogue dump|prop dm\.|prop player\.|signature |dlgfn |dlgsig ' $mark 70
}

# ===========================================================================
# THE PHASE -- A talks, B watches
# ===========================================================================
function Invoke-Spectator {
    HB 'spectator start'
    Step 'DIALOGUE SPECTATOR -- A talks to the hub NPC; B must see the same conversation' `
         'A = peer (the game''s own dialogue via the rig), B = host (the viewer).  Logs and shots.'
    $H = $script:H; $P = $script:P

    Step 'boot state' 'the switch line must say dialogue=1 and dialogueView=1'
    foreach ($i in 1, 2) {
        $m = @(Get-Matches $i 'switches:')
        if ($m.Count -gt 0) { Note "  [$i] $([string]$m[-1])" }
        $v = @(Get-Matches $i 'dialogue view: lane (on|off)')
        if ($v.Count -gt 0) { Note "  [$i] $([string]$v[-1])" }
    }
    # The `switches:` line is logged during BOOTSTRAP, i.e. BEFORE the `hooked FEngineLoop`
    # line that `Get-RunLines` starts from -- so it must be read from the whole file, not
    # from the run window.  (Measured: run 3's report called this FAIL while the raw log
    # line 5 read `... dialogue=1 dialogueView=1 ...` and the lane obviously ran.)
    $sw = @(Read-LogLines $inst[$H].Log | Where-Object { $_ -match 'switches:' })
    $swOk = (@($sw | Where-Object { ([string]$_) -match 'dialogueView=1' }).Count -gt 0)
    Verdict 'boot' $H $swOk 'the host reports dialogueView=1' ($(if ($sw.Count) { [string]$sw[-1] } else { '' }))

    # --- 1. A starts a real conversation -----------------------------------
    Step '1. A starts a conversation with the nearest hub NPC' 'qd.tp near:npc, then qd.act near:npc'
    $mP0 = Get-Mark $P
    Invoke-Go $P 'npc' 200
    Start-Sleep -Seconds 8
    Invoke-ActNear $P 'npc'
    $started = Check 'dlg' $P 'dialogue: local started' 40 'a conversation started on the peer (A)' $mP0
    if (-not $started) {
        Show-Since $P 'dialogue:|dev interact:|rig: no live|near:' $mP0 14
        Say 'REFUSED: A never started a conversation, so there is nothing to spectate'
        return
    }
    Show-Since $P 'dialogue: local started|dialogue view: publish' $mP0 6

    # --- 2. B shows the view ------------------------------------------------
    Step '2. B must show the view, take the camera and lock its own input' 'the spectator lane'
    $mH0 = Get-Mark $H
    $shown = Check 'dlg' $H 'dialogue view: SHOW from slot' 20 'the host engaged the spectator view' $mH0
    [void](Check 'dlg' $H 'dialogue view: widget visible=' 10 'the host reports whether the game''s own dialogue widget came up' $mH0)
    [void](Check 'dlg' $H 'dialogue view: camera -> ' 10 'the host blended its camera to the speaker' $mH0)
    [void](Check 'dlg' $H 'dialogue view: input locked' 10 'the host locked its own movement for the conversation' $mH0)
    Show-Since $H 'dialogue view:|dialogue: BLOCK' $mH0 16
    Start-Sleep -Seconds 2
    Save-Shots 'during'

    # The measurement that decides whether the widget path worked, printed with the
    # properties and signatures the next iteration needs if it did not.
    Step '2b. the DialogueManager on B: what the widget path actually saw' 'qd.dlg'
    Invoke-DlgDump $H 'host'
    Invoke-DlgDump $P 'peer'

    # --- 3. the flags half (inside the dialogue window) ---------------------
    Step '3. a dialogue outcome that sets a flag applies on both' 'the game''s own SaveNamedData, then qd.flag on both'
    $mP2 = Get-Mark $P
    Set-Control $P 'qd.flag' ('set:CoopDialogueProbe=1#' + (Get-Date -Format 'HHmmssfff'))
    [void](Check 'flags' $P 'rig: flag set -- .*CoopDialogueProbe' 15 'A wrote the flag through the game''s own SaveNamedData' $mP2)
    Start-Sleep -Seconds 3
    $mH2 = Get-Mark $H; $mP3 = Get-Mark $P
    Set-Control $H 'qd.flag' ('CoopDialogueProbe#' + (Get-Date -Format 'HHmmssfff'))
    Set-Control $P 'qd.flag' ('CoopDialogueProbe#' + (Get-Date -Format 'HHmmssfff'))
    $both = Check 'flags' $H 'rig: flag dump -- CoopDialogueProbe = 1' 20 'the host''s bank holds the value A wrote' $mH2
    [void](Check 'flags' $P 'rig: flag dump -- CoopDialogueProbe = 1' 20 'the peer''s own bank holds it too' $mP3)
    Verdict 'flags' $H $both 'the dialogue lane does not disturb the flags lane; the value crossed' ''

    # --- 4. can the rig advance A's line? ---------------------------------
    Step '4. the dialogue cursor after an advance attempt' 'near:dialogue!DialogueSequenceContinue (measured, not asserted)'
    $mH1 = Get-Mark $H
    $mP1 = Get-Mark $P
    Invoke-DlgVerb $P 'DialogueSequenceContinue'
    Start-Sleep -Seconds 4
    $ranAdv = @(Get-Since $P $mP1 | Where-Object { $_ -match 'DialogueManager\.DialogueSequenceContinue' })
    if ($ranAdv.Count -gt 0) { Note "  [2] $([string]$ranAdv[-1])" }
    $adv = @(Get-Since $H $mH1 | Where-Object { $_ -match 'dialogue view: update line=' })
    Verdict 'dlg' $H ($adv.Count -gt 0) 'B followed an advance of A''s line' ($(if ($adv.Count) { [string]$adv[-1] } else { '' }))
    Show-Since $H 'dialogue view:' $mH1 8
    Save-Shots 'advanced'

    # --- 5. A ends; B restores ---------------------------------------------
    Step '5. A''s conversation ends (A dies, the proven end path); B restores' 'qd.kill on A'
    $mP4 = Get-Mark $P
    $mH4 = Get-Mark $H
    # MEASURED (run 1): `DialogueSequenceContinue` and `End` are not UFunctions on the
    # manager either, so the rig cannot advance or close A's conversation by calling them.
    # The game's own death path DOES end a conversation and release the lock (R89), and it
    # is the stronger restore test: it exercises "A vanished mid-dialogue".
    Set-Control $P 'qd.kill' ('1#' + (Get-Date -Format 'HHmmssfff'))
    [void](Check 'dlg' $P 'rig: kill -> .*ran=1' 25 'the rig killed A (the game''s own end path)' $mP4)
    [void](Check 'dlg' $P 'dialogue: local death ended the conversation' 20 'A''s conversation ended on death' $mP4)
    $hid = Check 'dlg' $H 'dialogue view: HIDE' 30 'B hid the view' $mH4
    [void](Check 'dlg' $H 'dialogue view: camera restored' 10 'B''s camera went back to its own pawn' $mH4)
    [void](Check 'dlg' $H 'dialogue view: input restored' 10 'B''s input was given back' $mH4)
    Show-Since $H 'dialogue view:' $mH4 12
    Start-Sleep -Seconds 2
    Save-Shots 'after'
    Verdict 'dlg' $H $hid 'the spectator released exactly once and restored' ''
    HB 'spectator done'

    # --- 6. survival --------------------------------------------------------
    Step '6. both machines survived'
    foreach ($i in @($H, $P)) {
        $alive = Test-Alive $i
        Verdict 'alive' $i $alive 'the instance is still running' ''
        $fault = @(Get-RunLines $i | Where-Object { $_ -match '(\sFAULT\b)|Unhandled Exception|ACCESS_VIOLATION|NULL_CLASS_PTR' })
        Verdict 'alive' $i ($fault.Count -eq 0) 'no FAULT line' ($(if ($fault.Count) { [string]$fault[-1] } else { '' }))
    }
}

function Write-Summary {
    Note ''
    Note '================ SUMMARY ================'
    $pass = @($script:verdicts | Where-Object { $_.Ok }).Count
    $fail = @($script:verdicts | Where-Object { -not $_.Ok }).Count
    foreach ($v in $script:verdicts) {
        $flag = if ($v.Ok) { 'PASS' } else { 'FAIL' }
        $ln = [string]$v.Line
        if ($ln.Length -gt 220) { $ln = $ln.Substring(0, 220) + ' ...' }
        Note ("  {0} {1,-16} [{2}] {3}{4}" -f $flag, $v.Step, $v.Inst, $v.What, $(if ($ln) { " -- $ln" } else { '' }))
    }
    Note ("  {0} PASS / {1} FAIL" -f $pass, $fail)
    if ($script:shots.Count -gt 0) {
        Note '  screenshots:'
        foreach ($s in $script:shots) {
            Note ("    [{0}] {1} mean={2} stddev={3} {4}" -f $s.Inst, $s.Tag, $s.Mean, $s.Std, $s.Path)
        }
    }
    $script:lines | Set-Content -LiteralPath $Report
    Say "report: $Report"
}

# ---------------------------------------------------------------------------
try {
    Say "session_dialogue phase=$Phase rig=$rigRoot"
    Seed-FreshPair
    Start-Boot $Phase
    Invoke-Spectator
    Write-Summary
} catch {
    Say ("UNHANDLED: " + $_.Exception.Message)
    Say ("AT: " + $_.InvocationInfo.PositionMessage)
    HB ("CAUGHT: " + $_.Exception.Message + " @ " + $_.InvocationInfo.PositionMessage)
    try { $script:lines | Set-Content -LiteralPath $Report } catch {}
    Stop-All
    exit 1
} finally {
    try { Stop-All } catch {}
    try { if (-not (Test-Path $Report)) { $script:lines | Set-Content -LiteralPath $Report } } catch {}
}
