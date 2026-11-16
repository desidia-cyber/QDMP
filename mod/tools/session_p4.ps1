# session_p4.ps1 -- playthrough part p4: R35-R46 (secret zones, bosses B6-B9, puzzles).
#
# WHAT THIS MEASURES (one row per CAMPAIGN-ROUTE step; the line that decides each)
# ------------------------------------------------------------------------------
# R35 700_SZ_GR   : area follow, SecretZoneDoor_Blue / SZ_Door_02 (savable doors, BOTH
#                   directions), SipehaRune_GR_SZ (PTSeal_SipehaRune = savable), SecretWall
#                   (object lane), vault relic setter ActivatableNamedSaveDataSetter2 ->
#                   `SR_GMS_Acquired = 1` in BOTH banks, SkullKeyRelic pickup -> key item on both.
# R36 710/720     : same for SecretWall / SipehaRune_SZ_Aq / SipehaRune_SZ_Lib / TeleporterFragment.
# R37 730_HOD     : the relic room's statue set + SkullKeyRelic + SpectralSword pickup.
# R38 740_SZ_Prison: 9x SZ_JailDoor, JosamiPrisonDoor (arena gate), Prison_key_1F (key item),
#                   SipehaRune_Prison; B6 Red Lords via ActivationTrigger4 -- both sisters,
#                   the revive rule, RedLordsDead in both banks, real attack input.
# R39 750_SZ_MAUS2: BookKey_00_Puwema (key item), SipehaRune_SZ_Maus2, SkullRelic.
# R40 760_Dekanima: OPTIONAL -- reached only if the cost is low; otherwise declared.
# R41 760_SZ_AUT : B8 ROS Dragon (ActivationTrigger11), Ros_Cage_Gate, ROS_Door_BP,
#                   Teleporter_ROS, and the ROS book set (LIB_BookRecepticle + Lib_Book_*).
# R42 770_SZ_Swamp: B7 Giant Fish + vault relic setter3 (SR_GreenKey_Acquired) + SipehaRune_Swamp.
# R43 775_Jigeki   : JigekiDoor_BP, SZ_BoringDoor_00_BP9, FishWifeEvil spawner.
# R44 780_SZ_Depths: B9 Dragon Lord (both bodies), DesBoss_Gate, SZDoorKey_Blue,
#                   vault relic setter5 (SR_Key_Blue_Acquired).
# R45 790_SG      : StoneGardenPuzzleScript, WhaleKing_SZ_Spawner + WhaleKing_Env_PPC,
#                   SipehaRune_BP/Jigeki/Puwema/Wodaso runes, SG_Door_Beepee, Checkpoint16.
# R46 800_FinalZone: SkullDoorQuestStateManager/State0/1, SecretSkullDoor NPC, SecretSkullRitual,
#                   SecretWall4, SkullKeyRelic_TP.
#
# RULES (inherited from the playthrough brief): the SHIPPED qdcoop.ini verbatim apart from
# RelayHost/RelayPort/LogLevel; DesyncAudit=1 (it already is).  No switch is flipped here:
# BossPhaseSync / WorldSetSync / PuzzleSync are shipped 0 and this run MEASURES what that
# costs instead of turning them on.  Every step ends with `qd.audit` on both + a screenshot
# pair.  A verdict prints the line it read; no line, no pass.
[CmdletBinding()]
param(
    [ValidateSet('secrets', 'prison', 'swamp', 'final', 'all')]
    [string]$Part = 'secrets',
    [int]$BootSeconds = 420,
    [int]$SettleSeconds = 45,
    [string]$Only = '',
    [string]$Report = ''
)

$ErrorActionPreference = 'Stop'
$here    = $PSScriptRoot
$repo    = Split-Path -Parent (Split-Path -Parent $here)
$gameDir = 'G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64'
$relay   = Join-Path (Split-Path -Parent $here) 'rust\target\release\qd-relay.exe'
$rigRoot = 'G:\Freespace\Queens_Domain\_test\p4run'
$seed    = Join-Path $repo 'analysis\save-backup-before-live'
$srcIni  = Join-Path $repo 'mod\qdcoop.ini'
$outDir  = Join-Path $rigRoot 'out'
$shotDir = 'G:\Freespace\Queens_Domain\_test\playthrough\play-p4'
$locks   = 'G:\Freespace\Queens_Domain\_test\locks'
$lockDir = Join-Path $locks 'live'
$buildLk = Join-Path $locks 'build'
if (-not $Report) { $Report = Join-Path $outDir ("report-p4-{0}.txt" -f $Part) }
foreach ($d in @($outDir, $shotDir, $locks)) { if (-not (Test-Path $d)) { New-Item -ItemType Directory -Force -Path $d | Out-Null } }

$script:lines    = New-Object System.Collections.Generic.List[string]
$script:verdicts = New-Object System.Collections.Generic.List[object]
$script:pids     = @{}
$script:relayProc = $null
$script:step = ''
function Say($m) { $s = "[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $m; Write-Host $s; $script:lines.Add($s) | Out-Null }
function Note($m) { Write-Host $m; $script:lines.Add($m) | Out-Null }
function Verdict([int]$i, [bool]$ok, [string]$what, [string]$line = '') {
    $script:verdicts.Add([pscustomobject]@{ Step = $script:step; Inst = $i; What = $what; Ok = $ok; Line = $line }) | Out-Null
    $tag = 'FAIL'; if ($ok) { $tag = 'PASS' }
    Note ("  {0} [{1}] {2}{3}" -f $tag, $i, $what, $(if ($line) { ' -- ' + $line } else { '' }))
}
function Step([string]$title, [string]$body = '') {
    $script:step = $title
    Note ''
    Note ("### {0}" -f $title)
    if ($body) { Note $body }
}

# ---------------------------------------------------------------- locks (LIVE, whole run)
# STRICT RULE (2026-11-16): launch nothing unless this script holds _test\locks\live; break
# another owner's lock ONLY if its owner.txt is >25 min old AND no game/relay process runs;
# cleanup kills only the pids this script launched; owner.txt is rewritten every ~20 s so the
# hold's age is never stale.
function Get-LockOwner([string]$p) {
    $o = Join-Path $p 'owner.txt'
    if (Test-Path $o) { return (Get-Content $o -Raw).Trim() }
    return ''
}
function Get-ForeignLive {
    $n = 0
    foreach ($p in @(Get-Process -Name 'QueensDomain-Win64-Shipping', 'qd-relay' -ErrorAction SilentlyContinue)) {
        if ($script:pids.ContainsValue($p.Id)) { continue }
        if ($script:relayProc -and $script:relayProc.Id -eq $p.Id) { continue }
        $n++
    }
    return $n
}
function Touch-Lock {
    if (-not (Test-Path $lockDir)) { return }
    if ((Get-LockOwner $lockDir) -notlike 'play-p4*') { return }
    Set-Content -LiteralPath (Join-Path $lockDir 'owner.txt') -Value ('play-p4 ' + (Get-Date -Format 'HH:mm:ss')) -NoNewline
    try { (Get-Item $lockDir).LastWriteTime = Get-Date } catch { }
}
function Rest([double]$secs) {
    if ($secs -le 0) { return }
    $end = (Get-Date).AddSeconds($secs)
    while ((Get-Date) -lt $end) {
        $slice = [Math]::Min(20.0, ($end - (Get-Date)).TotalSeconds)
        if ($slice -gt 0) { Start-Sleep -Milliseconds ([int]($slice * 1000)) }
        Touch-Lock
    }
    Touch-Lock
}
function Acquire-Live {
    for ($t = 0; $t -lt 300; $t++) {
        if (-not (Test-Path $lockDir)) {
            try {
                New-Item -ItemType Directory -Path $lockDir -ErrorAction Stop | Out-Null
                Set-Content -LiteralPath (Join-Path $lockDir 'owner.txt') -Value ('play-p4 ' + (Get-Date -Format 'HH:mm:ss')) -NoNewline
                Say 'LIVE lock acquired'
                return
            } catch { }
        }
        $own = Get-LockOwner $lockDir
        if ($own -like 'play-p4*') { return }
        $ageMin = 999
        $of = Join-Path $lockDir 'owner.txt'
        if (Test-Path $of) { $ageMin = [int]((Get-Date) - (Get-Item $of).LastWriteTime).TotalMinutes }
        $foreign = Get-ForeignLive
        if ($ageMin -gt 25 -and $foreign -eq 0) {
            Say ("LIVE lock owner '{0}' is {1} min stale and NO game/relay process runs -- taking it" -f $own, $ageMin)
            Remove-Item -LiteralPath $lockDir -Recurse -Force -ErrorAction SilentlyContinue
            continue
        }
        Say ("LIVE lock held by '{0}' (owner.txt {1} min old, {2} foreign game/relay process(es)) -- waiting" -f $own, $ageMin, $foreign)
        Rest 25
    }
    throw 'could not acquire the LIVE lock'
}
function Release-Live {
    if ((Test-Path $lockDir) -and ((Get-LockOwner $lockDir) -like 'play-p4*')) {
        Remove-Item -LiteralPath $lockDir -Recurse -Force -ErrorAction SilentlyContinue
        Say 'LIVE lock released'
    }
}
# ---------------------------------------------------------------- log helpers
function Get-RigPaths([int]$i) {
    [pscustomobject]@{
        Index = $i
        Root  = (Join-Path $rigRoot ("inst{0}" -f $i))
        Suffix = ("qd{0}" -f $i)
        Log   = (Join-Path $gameDir ("qdcoop-qd{0}.log" -f $i))
        Pid   = (Join-Path $rigRoot ("inst{0}.pid" -f $i))
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
        } catch { Start-Sleep -Milliseconds 60 }
    }
    return @()
}
function Get-RunLines([int]$i) {
    $all = @(Read-LogLines $inst[$i].Log)
    if ($all.Count -eq 0) { return @() }
    $start = 0
    for ($k = $all.Count - 1; $k -ge 0; $k--) { if ($all[$k] -match 'hooked FEngineLoop') { $start = $k; break } }
    return @($all[$start..($all.Count - 1)])
}
function Get-Matches([int]$i, [string]$pat) { return @(Get-RunLines $i | Where-Object { $_ -match $pat }) }
function Get-LineMs([string]$ln) {
    if ($ln -match '^\s*(\d{1,2}):(\d{2}):(\d{2})\.(\d{3})') {
        return ([int]$Matches[1] * 3600000 + [int]$Matches[2] * 60000 + [int]$Matches[3] * 1000 + [int]$Matches[4])
    }
    return -1
}
function Get-Mark([int]$i) {
    $n = Get-Date
    return ($n.Hour * 3600000 + $n.Minute * 60000 + $n.Second * 1000 + $n.Millisecond) - 800
}
function Test-Alive([int]$i) {
    if (-not $script:pids.ContainsKey($i)) { return $false }
    return [bool](Get-Process -Id $script:pids[$i] -ErrorAction SilentlyContinue)
}
function Get-Since([int]$i, [int]$markMs) {
    $out = New-Object System.Collections.Generic.List[string]
    foreach ($ln in @(Get-RunLines $i)) {
        $t = Get-LineMs ([string]$ln)
        if ($t -ge 0 -and $t -ge $markMs) { $out.Add([string]$ln) }
    }
    return $out.ToArray()
}
function Wait-Since([int]$i, [string]$pat, [int]$mark, [int]$secs) {
    $deadline = (Get-Date).AddSeconds($secs)
    while ((Get-Date) -lt $deadline) {
        $m = @(Get-Since $i $mark | Where-Object { $_ -match $pat })
        if ($m.Count -gt 0) { return $m }
        if (-not (Test-Alive $i)) { return @() }
        Start-Sleep -Milliseconds 600
        Touch-Lock
    }
    return @()
}
function Show-Since([int]$i, [string]$pat, [int]$mark, [int]$n) {
    $m = @(Get-Since $i $mark | Where-Object { $_ -match $pat })
    if ($m.Count -eq 0) { Note ("  [{0}] (no line matches '{1}')" -f $i, $pat); return }
    $m | Select-Object -Last $n | ForEach-Object {
        $s = [string]$_
        if ($s.Length -gt 340) { $s = $s.Substring(0, 340) + ' ...' }
        Note ("  [{0}] {1}" -f $i, $s.Trim())
    }
}
function Set-Control([int]$i, [string]$leaf, $value) {
    $p = Join-Path -Path ([string]$inst[$i].Root) -ChildPath ([string]$leaf)
    for ($t = 0; $t -lt 6; $t++) {
        try {
            if ($null -eq $value) { Remove-Item -LiteralPath $p -ErrorAction SilentlyContinue }
            else { Set-Content -LiteralPath $p -Value ([string]$value) -NoNewline -ErrorAction Stop }
            return
        } catch { Start-Sleep -Milliseconds 120 }
    }
    Say ("WARN: could not write lever '{0}' for inst{1}" -f $leaf, $i)
}
function Check([int]$i, [string]$pat, [int]$secs, [string]$label, [int]$Mark = 0) {
    if (-not (Test-Alive $i)) { Verdict $i $false $label 'INSTANCE DIED'; return $false }
    $m = Wait-Since $i $pat $Mark $secs
    $ok = ($m.Count -gt 0)
    Verdict $i $ok $label $(if ($ok) { ([string]$m[-1]).Trim() } elseif ($m.Count -eq 0) { "no line matches '$pat' in ${secs}s" } else { '' })
    return $ok
}

# ---------------------------------------------------------------- rig verbs
$script:seq = 0
function Invoke-Act([int]$i, [string]$instr) {
    $script:seq++
    Set-Control $i 'qd.act' ($instr + '#' + (Get-Date -Format 'HHmmssfff') + 'p4' + $script:seq)
}
function Invoke-Go([int]$i, [string]$name, [int]$near = 160) {
    $script:seq++
    Set-Control $i 'qd.near' ([string]$near)
    Set-Control $i 'qd.tp' ('near:' + $name + ',' + (($script:seq * 37) % 360))
}
function Get-Props([int]$i, [string]$actor, [string]$props, [int]$secs = 12) {
    $m = Get-Mark $i
    Invoke-Act $i ('near:' + $actor + '!?' + $props)
    $ln = @(Wait-Since $i 'dev props: ' $m $secs)
    if ($ln.Count -eq 0) { return @{} }
    $h = @{}
    foreach ($p in @(([string]$ln[-1] -replace '^.*dev props:\s*', '') -split '\s+')) {
        if ($p -match '^([A-Za-z0-9_]+)=(.+)$') { $h[$Matches[1]] = $Matches[2] }
    }
    return $h
}
function Get-Bank([int]$i, [string]$name, [int]$secs = 14) {
    $m = Get-Mark $i
    Set-Control $i 'qd.flag' ($name + '#' + (Get-Date -Format 'HHmmssfff'))
    $ln = @(Wait-Since $i ('rig: flag dump -- ' + $name + ' = ') $m $secs)
    if ($ln.Count -eq 0) { return 'no answer' }
    $s = [string]$ln[-1]
    if ($s -match ' = (-?\d+)') { return $Matches[1] }
    return $s.Trim()
}
function Get-Enemies([int]$i, [string]$tag, [int]$secs = 45) {
    $m = Get-Mark $i
    Set-Control $i 'qd.enemies' ('p4-' + $tag + '-' + (Get-Date -Format 'HHmmssfff'))
    $rows = @()
    $deadline = (Get-Date).AddSeconds($secs)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 700
        $rows = @(Get-Since $i $m | Where-Object { $_ -match 'enemykey [0-9A-Fa-f]{8} (ALIVE|DEAD|GONE)' })
        if ($rows.Count -gt 0) { break }
    }
    $out = @{}
    foreach ($ln in $rows) {
        $s = [string]$ln
        if ($s -notmatch 'enemykey ([0-9A-Fa-f]{8}) (ALIVE|DEAD|GONE)') { continue }
        $k = $Matches[1].ToUpperInvariant(); $st = $Matches[2]
        $hp = $null; if ($s -match '\shp=([-0-9.]+)') { $hp = [double]$Matches[1] }
        $cls = '';  if ($s -match ' cls=(\S+)') { $cls = $Matches[1] }
        $id = $k;   if ($s -match '\sid=([0-9A-Fa-f]{32})') { $id = $Matches[1].ToUpperInvariant() }
        $out[$id] = [pscustomobject]@{ Id = $id; Key = $k; State = $st; Hp = $hp; Cls = $cls; Line = $s.Trim() }
    }
    return $out
}
function Save-Shots([string]$tag) {
    $ok = @()
    foreach ($i in 1, 2) {
        if (-not (Test-Alive $i)) { continue }
        $out = Join-Path $shotDir ("p4-{0}-{1}-inst{2}.png" -f $Part, $tag, $i)
        $null = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'capture-hidden.ps1') `
                 -Out $out -ProcessId $script:pids[$i] 2>&1
        if (Test-Path -LiteralPath $out) { $ok += $out; Note ("  shot[{0}] {1}" -f $i, $out) }
        else { Note ("  shot[{0}] {1}: FAILED" -f $i, $tag) }
    }
    return $ok
}
function Invoke-Audit([string]$why) {
    $res = @{}
    foreach ($i in 1, 2) { Set-Control $i 'qd.audit' ('p4-' + $why + '-' + (Get-Date -Format 'HHmmssfff')) }
    Rest 14
    foreach ($i in 1, 2) {
        $m = @(Get-Matches $i 'audit: (OK|DESYNC)')
        if ($m.Count -eq 0) { $res[$i] = 'no audit line'; continue }
        $res[$i] = ([string]$m[-1]).Trim()
    }
    foreach ($i in 1, 2) {
        $ok = $res[$i] -match 'audit: OK'
        Verdict $i $ok ("audit after $why") $res[$i]
    }
    return $res
}
function Enter-Area([int]$i, [string]$area, [int]$role, [int]$other) {
    Set-Control $i 'qd.level' $area
    $m = Get-Mark $i
    $okA = Check $i ('rig: level engine state ' + [regex]::Escape($area) + ' LoadedLevel=1 actuallyVisible=1') 90 ("inst{0}: {1} loaded and visible" -f $i, $area) $m
    $okB = Check $other ('travel: (entered the partner.s area ' + [regex]::Escape($area) + ' by streaming it|REFUSED the partner.s area)') 90 ("inst{0}: followed the partner into {1}" -f $other, $area) ($m - 3000)
    if ($okB) {
        $okC = Check $other ('rig: level engine state ' + [regex]::Escape($area) + ' LoadedLevel=1 actuallyVisible=1') 60 ("inst{0}: own engine state for {1}" -f $other, $area) ($m - 3000)
    }
    Rest 4
    return ($okA -and $okB)
}
function Leave-Area([int]$i, [int]$other) {
    $m = Get-Mark $i
    Set-Control $i 'qd.level' ('-#' + (Get-Date -Format 'HHmmssfff'))
    Rest 8
    Check $other 'travel: left the area .* \(the partner left it\)' 40 ("inst{0}: left the area with the partner" -f $other) ($m - 3000)
}

# ---------------------------------------------------------------- install / seed / launch
function Stop-All([string]$why) {
    Say "STOP-ALL $why (kills ONLY the pids this script launched)"
    if ($script:relayProc) { Stop-Process -Id $script:relayProc.Id -Force -ErrorAction SilentlyContinue }
    foreach ($k in @($script:pids.Keys)) {
        $p = $script:pids[$k]
        if ($p) { Stop-Process -Id $p -Force -ErrorAction SilentlyContinue }
    }
    $script:pids = @{}
    Start-Sleep -Seconds 2
}

Acquire-Live
try {
    if ((Get-LockOwner $lockDir) -notlike 'play-p4*') {
        Say 'REFUSED: the LIVE lock is not held by play-p4'
        exit 2
    }
    $foreign = Get-ForeignLive
    if ($foreign -gt 0) {
        Say ("REFUSED: {0} game/relay process(es) from another session are running. Not killing them; aborting so two sessions never overlap." -f $foreign)
        throw 'foreign live processes are running'
    }
    $built = Join-Path $repo 'mod\native\build\winmm.dll'
    if (-not (Test-Path -LiteralPath $built)) { Say "REFUSED: no built proxy at $built"; exit 2 }
    $h = (Get-FileHash -LiteralPath $built -Algorithm SHA256).Hash
    Say ("installing proxy sha256={0}..." -f $h.Substring(0, 16))
    $dst = Join-Path $gameDir 'winmm.dll'
    try { Copy-Item -LiteralPath $built -Destination $dst -Force }
    catch {
        Rename-Item -LiteralPath $dst -NewName ('winmm.dll.held-' + (Get-Date -Format 'HHmmss')) -Force
        Copy-Item -LiteralPath $built -Destination $dst -Force
        Say 'the destination was image-mapped; moved it aside and installed the new proxy'
    }

    # SHIPPED ini verbatim, except the loopback pin and the log level.
    $text = Get-Content -LiteralPath $srcIni -Raw
    $text = $text -replace '(?m)^RelayHost=.*$', 'RelayHost=127.0.0.1'
    $text = $text -replace '(?m)^RelayPort=.*$', 'RelayPort=47800'
    $text = $text -replace '(?m)^LogLevel=.*$', 'LogLevel=3'
    $ini = Join-Path $gameDir 'qdcoop.ini'
    $text | Set-Content -LiteralPath $ini -NoNewline
    Copy-Item -LiteralPath $ini -Destination (Join-Path $outDir ('qdcoop-used-{0}.ini' -f $Part)) -Force
    foreach ($sw in 'BossPhaseSync', 'WorldSetSync', 'PuzzleSync') {
        $v = ([regex]::Match($text, '(?m)^' + $sw + '=(\d+)')).Groups[1].Value
        Say ("switch {0}={1} (shipped value, not changed by this rig)" -f $sw, $v)
    }

    foreach ($i in 1, 2) {
        $null = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'make_test_instance.ps1') `
                -Name ("inst{0}" -f $i) -Index $i -SourceSave $seed -Root $rigRoot -Suffix $inst[$i].Suffix -Force 2>&1
        if ($LASTEXITCODE -ne 0) { Say "REFUSED: make_test_instance refused instance $i"; exit 2 }
    }

    Stop-All 'boot: replacing anything left over'
    foreach ($i in 1, 2) {
        foreach ($leaf in 'qd.tp', 'qd.act', 'qd.hit', 'qd.cheat', 'qd.cheatarg', 'qd.travel', 'qd.hard', 'qd.level',
                          'qd.list', 'qd.near', 'qd.drops', 'qd.menu', 'qd.efunc', 'qd.use', 'qd.kill', 'qd.enemies',
                          'qd.hitguid', 'qd.obj', 'qd.flag', 'qd.gate', 'qd.take', 'qd.trigger', 'qd.selfhp', 'qd.nohit',
                          'qd.dlg', 'qd.attack', 'qd.equip', 'qd.mover', 'qd.wsys', 'qd.proj', 'qd.auditfork', 'qd.world',
                          'qd.audit', 'qd.god', 'qd.bphase', 'qd.enemykill', 'qd.eattack', 'qd.eprops', 'qd.hazard',
                          'qd.pdmg', 'qd.sequence') {
            Set-Control $i $leaf $null
        }
        Set-Control $i 'qd.scan' '1'
        if (Test-Path -LiteralPath $inst[$i].Pid) { Remove-Item -LiteralPath $inst[$i].Pid -ErrorAction SilentlyContinue }
        $old = $inst[$i].Log
        if (Test-Path -LiteralPath $old) {
            $prev = $old + '.prev'
            Remove-Item -LiteralPath $prev -Force -ErrorAction SilentlyContinue
            $moved = $false
            for ($t = 0; $t -lt 10 -and -not $moved; $t++) {
                try { Move-Item -LiteralPath $old -Destination $prev -Force -ErrorAction Stop; $moved = $true }
                catch { Start-Sleep -Milliseconds 500 }
            }
            if (-not $moved) { try { Set-Content -LiteralPath $old -Value '' -ErrorAction Stop } catch { } }
        }
    }
    $env:QDCOOP_AUTOLOAD = '0'

    if (-not (Test-Path -LiteralPath $relay)) { Say "REFUSED: relay not built at $relay"; exit 2 }
    $relayLog = Join-Path $outDir ("relay-{0}.log" -f $Part)
    Remove-Item -LiteralPath $relayLog -ErrorAction SilentlyContinue
    $script:relayProc = Start-Process -FilePath $relay -ArgumentList @('--bind', '127.0.0.1:47800') `
            -RedirectStandardOutput $relayLog -RedirectStandardError (Join-Path $outDir ("relay-{0}.err" -f $Part)) `
            -WindowStyle Hidden -PassThru
    Say ("relay pid={0}" -f $script:relayProc.Id)
    Rest 2

    foreach ($i in 1, 2) {
        $ueLog = Join-Path $outDir ("inst{0}-ue-{1}.log" -f $i, $Part)
        Remove-Item -LiteralPath $ueLog -ErrorAction SilentlyContinue
        $launch = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'run-hidden.ps1') `
                    -UserDir $inst[$i].Root -Suffix $inst[$i].Suffix -AbsLog $ueLog 2>&1
        $gp = ($launch | Where-Object { $_ -match '^\d+$' } | Select-Object -Last 1)
        if (-not $gp) { Say "LAUNCH FAILED instance $i" } else {
            $script:pids[$i] = [int]$gp; Set-Content -LiteralPath $inst[$i].Pid -Value $gp; Say "inst$i pid=$gp"
        }
        Rest 4
    }

    Say "waiting for a session (up to $BootSeconds s)"
    $deadline = (Get-Date).AddSeconds($BootSeconds)
    $roles = @{}
    while ((Get-Date) -lt $deadline) {
        foreach ($i in 1, 2) {
            if ($roles.ContainsKey($i) -or -not (Test-Alive $i)) { continue }
            $m = @(Get-Matches $i 'session established: role=(HOST|PEER)')
            if ($m.Count -gt 0 -and ([string]$m[-1]) -match 'role=(HOST|PEER)') {
                $roles[$i] = $Matches[1]; Note ("  [{0}] {1}" -f $i, ([string]$m[-1]).Trim())
            }
        }
        if ($roles.Count -eq 2) { break }
        if (-not (Test-Alive 1) -and -not (Test-Alive 2)) { break }
        Touch-Lock
        Rest 2
    }
    if ($roles.Count -ne 2) { Say 'REFUSED: no session established'; Stop-All 'no session'; $script:lines | Set-Content $Report; exit 2 }
    $H = 1; $P = 2
    if ($roles[2] -eq 'HOST') { $H = 2; $P = 1 }
    Say ("roles: inst{0}=HOST inst{1}=PEER" -f $H, $P)

    # the relay must have forwarded everything: a stale relay drops new kinds
    $mal = @(Read-LogLines $relayLog) | Where-Object { $_ -match 'malformed datagram' }
    $kill = @(Read-LogLines $relayLog) | Where-Object { $_ -match 'unknown packet kind' }
    Verdict $H ($mal.Count -eq 0 -and $kill.Count -eq 0) 'the relay forwarded every datagram (malformed total=0)' `
            ("malformed datagram={0}, unknown kind={1}" -f $mal.Count, $kill.Count)

    $swAll = @(Read-LogLines $inst[$H].Log) | Where-Object { $_ -match 'switches: ' }
    $onHost = @($swAll | Where-Object { $_ -match 'flags=1' -and $_ -match 'actors=1' -and $_ -match 'objects=1' })
    Verdict $H ($onHost.Count -gt 0) 'the switches are on in the running game' $(if ($onHost.Count) { ([string]$onHost[-1]).Trim() } else { '(no switches: line in the whole log)' })
    Check $P 'session established: role=PEER' 20 'the PEER joined' 0 | Out-Null

    Say "settling $SettleSeconds s so the save is loaded and the world is up"
    Rest $SettleSeconds

    # =================================================================================
    # R35 -- 700_SZ_GR: the Green Ruins secret zone
    # =================================================================================
    if ($Part -in @('secrets', 'all') -and (-not $Only -or $Only -match 'R35')) {
        Step 'R35 700_SZ_GR -- the SZ entrance, its rune, a secret wall, the vault relic'
        $ok = Enter-Area $H '700_SZ_GR' 1 $P
        Rest 6
        Save-Shots 'R35-arrive'

        # --- BUG-1 (play-p4b): the SECRET ZONE DOOR, driven through its REAL player verb.
        # `SecretZoneDoor_Blue_BP_C` holds no actor-state row (`savable=no index=-1` while
        # `guids=1`), so until this round the OBJECT lane skipped it and the GUID lane had
        # nothing to publish: it was carried by NO lane, and one player's key opened nothing on
        # the other machine.  The real open path is `InsertKey`; the RECEIVER is put into the
        # state through the object lane's visual ladder (`SetToOpenState`), which refuses
        # `InsertKey`/`OpenDoor`/`TryUseKey` by name, so the partner's key is never spent.
        $mH = Get-Mark $H
        Set-Control $P 'qd.cheat' 'GiveAllKeys'
        Rest 2
        Invoke-Go $P 'SecretZoneDoor_Blue_BP' 220
        Rest 3
        Invoke-Act $P 'near:SecretZoneDoor_Blue_BP!InsertKey'
        Check $P 'dev interact: forced verb InsertKey on SecretZoneDoor_Blue_BP' 20 'peer: ran the SZ door''s real verb InsertKey' $mH | Out-Null
        Rest 4
        $pP = Get-Props $P 'SecretZoneDoor_Blue_BP' 'Opened,DoorClosed'
        $tP = (@($pP.GetEnumerator() | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join ' ')
        Verdict $P ($pP['Opened'] -eq '1') 'peer: its own SZ door reads OPEN after InsertKey' $tP
        Check $P 'object sync: tracking door SecretZoneDoor_Blue_BP' 20 'peer: the OBJECT lane tracks the SZ door (BUG-1)' $mH | Out-Null
        Check $H 'object intent applied: SecretZoneDoor_Blue_BP' 30 'host: applied the peer''s SZ door state' $mH | Out-Null
        Check $H 'interact: door anim SetToOpenState \(intent\) key=[0-9A-Fa-f]{8} state=1' 25 'host: played the SZ door''s own open visual' $mH | Out-Null
        Rest 3
        $pH = Get-Props $H 'SecretZoneDoor_Blue_BP' 'Opened,DoorClosed'
        $tH = (@($pH.GetEnumerator() | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join ' ')
        Verdict $H ($pH['Opened'] -eq '1') 'host: its SZ door OPEN from the peer''s key' $tH
        # "without consuming the partner's key": the receiver adds nothing to its inventory
        $spend = @(Get-Since $H $mH | Where-Object { $_ -match 'inventory grant applied|write api: AddItemToInventory' })
        Verdict $H ($spend.Count -eq 0) 'host: the receiver spent/added no key (visual path only)' ([string]($spend | Select-Object -Last 1))

        # the other direction: the HOST opens the second SZ door, the PEER must apply it
        $mP = Get-Mark $P
        Set-Control $H 'qd.cheat' 'GiveAllKeys'
        Rest 2
        Invoke-Go $H 'SecretZoneDoor_Green_BP' 220
        Rest 3
        Invoke-Act $H 'near:SecretZoneDoor_Green_BP!InsertKey'
        Check $H 'dev interact: forced verb InsertKey on SecretZoneDoor_Green_BP' 20 'host: ran the Green SZ door''s real verb InsertKey' $mP | Out-Null
        Rest 4
        Check $P 'object intent applied: SecretZoneDoor_Green_BP' 30 'peer: applied the host''s SZ door state' $mP | Out-Null
        Check $P 'interact: door anim SetToOpenState \(intent\) key=[0-9A-Fa-f]{8} state=1' 25 'peer: played that SZ door''s own open visual' $mP | Out-Null
        $gH = Get-Props $H 'SecretZoneDoor_Green_BP' 'Opened,DoorClosed'
        $gP = Get-Props $P 'SecretZoneDoor_Green_BP' 'Opened,DoorClosed'
        Verdict $H ($gH['Opened'] -eq '1') 'host: the Green SZ door reads OPEN' ((@($gH.GetEnumerator() | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join ' '))
        Verdict $P ($gP['Opened'] -eq '1') 'peer: the Green SZ door reads OPEN' ((@($gP.GetEnumerator() | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join ' '))

        # both lanes' own object inventories, for the record
        Set-Control $H 'qd.obj' ('p4-R35-obj-' + (Get-Date -Format 'HHmmssfff'))
        Set-Control $P 'qd.obj' ('p4-R35-obj-' + (Get-Date -Format 'HHmmssfff'))
        Rest 2

        # the other direction: the HOST opens SZ_Door_02 and the PEER must apply it
        $mP = Get-Mark $P
        Invoke-Go $H 'SZ_Door_02_BP' 220
        Rest 3
        Invoke-Act $H 'near:SZ_Door_02_BP!Activate'
        Check $P 'actor states applied=' 30 'the PEER applied the host''s SZ_Door_02 state' $mP | Out-Null
        Check $P 'interact: door anim' 25 'the PEER played SZ_Door_02''s own visual' $mP | Out-Null

        # the rune (PTSeal_SipehaRune_BP_C is a SavableActor: GUID lane)
        $mH2 = Get-Mark $H
        Invoke-Go $P 'SipehaRune_GR_SZ' 200
        Rest 3
        Invoke-Act $P 'near:SipehaRune_GR_SZ!Activate'
        Check $H 'actor-state intent applied' 25 'the HOST received the rune activation' $mH2 | Out-Null
        Check $H 'actor-state refresh ok .*SipehaRune' 25 'the HOST''s own rune copy re-applied from the save' $mH2 | Out-Null

        # a secret WALL (non-savable: the OBJECT lane, SetToFinalState)
        $mH3 = Get-Mark $H
        Invoke-Go $P 'SecretWall25' 200
        Rest 3
        Invoke-Act $P 'near:SecretWall25!SetToFinalState'
        Check $H 'object intent applied: SecretWall25 .*visual=SetToFinalState' 30 'the HOST''s SecretWall25 slid out' $mH3 | Out-Null

        # the VAULT RELIC: the setter writes SR_GMS_Acquired (a flag, the flag lane carries it)
        $mH4 = Get-Mark $H
        Invoke-Go $P 'ActivatableNamedSaveDataSetter2' 200
        Rest 3
        Invoke-Act $P 'near:ActivatableNamedSaveDataSetter2!Activate'
        Check $P 'flag (NEW |CHANGE )?SR_GMS_Acquired' 30 'the PEER wrote SR_GMS_Acquired locally' $mH4 | Out-Null
        Check $H 'flag (NEW |CHANGE )?SR_GMS_Acquired' 30 'the HOST received SR_GMS_Acquired' $mH4 | Out-Null
        $vH = Get-Bank $H 'SR_GMS_Acquired'
        $vP = Get-Bank $P 'SR_GMS_Acquired'
        Verdict $H ($vH -eq '1') 'SR_GMS_Acquired = 1 in the host bank' ("host=$vH peer=$vP")

        # a skull relic PICKUP (key item) taken by the peer
        $mH5 = Get-Mark $H
        Invoke-Go $P 'SkullKeyRelic_BP12' 200
        Rest 3
        Invoke-Act $P 'near:SkullKeyRelic_BP12!Interact'
        $got = Check $H 'inventory grant applied: /Game/Items/KeyItems/.*SkullKeyRelic' 35 'the HOST was granted the skull relic the peer took' $mH5
        if (-not $got) { Check $H 'object intent applied: SkullKeyRelic_BP12' 15 'the HOST''s own skull relic pickup closed' $mH5 | Out-Null }

        Invoke-Audit 'R35'
        Save-Shots 'R35-after'
    }

    # =================================================================================
    # R36 -- 710_SZ_AQ and 720_SZ_LIB
    # =================================================================================
    if ($Part -in @('secrets', 'all') -and (-not $Only -or $Only -match 'R36')) {
        Step 'R36 710_SZ_AQ + 720_SZ_LIB -- the two sub-areas between'
        [void](Enter-Area $H '710_SZ_AQ' 1 $P)
        Rest 5
        Save-Shots 'R36-710'

        $mA = Get-Mark $H
        Invoke-Go $P 'SecretWall' 200
        Rest 3
        Invoke-Act $P 'near:SecretWall!SetToFinalState'
        Check $H 'object intent applied: SecretWall .*visual=SetToFinalState' 30 '710: the HOST''s SecretWall slid out' $mA | Out-Null

        $mB = Get-Mark $H
        Invoke-Go $P 'SipehaRune_SZ_Aq' 200
        Rest 3
        Invoke-Act $P 'near:SipehaRune_SZ_Aq!Activate'
        Check $H 'actor-state intent applied' 25 '710: the HOST received the aqua rune' $mB | Out-Null

        $mC = Get-Mark $H
        Invoke-Go $P 'TeleporterFragment_Pickup_BP2' 200
        Rest 3
        Invoke-Act $P 'near:TeleporterFragment_Pickup_BP2!Interact'
        Check $H '(object intent applied: TeleporterFragment_Pickup_BP2|inventory grant applied)' 30 '710: the HOST saw the teleporter fragment the peer took' $mC | Out-Null

        [void](Enter-Area $H '720_SZ_LIB' 1 $P)
        Rest 5
        Save-Shots 'R36-720'

        $mD = Get-Mark $H
        Invoke-Go $P 'SecretWall23' 200
        Rest 3
        Invoke-Act $P 'near:SecretWall23!SetToFinalState'
        Check $H 'object intent applied: SecretWall23' 30 '720: the HOST''s SecretWall23 slid out' $mD | Out-Null

        $mE = Get-Mark $H
        Invoke-Go $P 'SipehaRune_SZ_Lib' 200
        Rest 3
        Invoke-Act $P 'near:SipehaRune_SZ_Lib!Activate'
        Check $H 'actor-state intent applied' 25 '720: the HOST received the library rune' $mE | Out-Null

        # the route calls this `SecretZoneDoor_BP2`; the map places an SZFrame_NoDoor_BP_C under
        # that name, i.e. a door FRAME with no door.  Measure and report, do not assume.
        $fr = Get-Props $P 'SecretZoneDoor_BP2' 'Opened,DoorClosed'
        Note ("  720 SecretZoneDoor_BP2 props: " + (($fr.GetEnumerator() | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join ' '))

        Invoke-Audit 'R36'
        Save-Shots 'R36-after'
    }

    # =================================================================================
    # R37 -- 730_HOD (optional/uncertain)
    # =================================================================================
    if ($Part -in @('secrets', 'all') -and (-not $Only -or $Only -match 'R37')) {
        Step 'R37 730_HOD -- the optional relic room (4 statues + SkullKeyRelic + SpectralSword)'
        [void](Enter-Area $H '730_HOD' 1 $P)
        Rest 5
        Save-Shots 'R37-arrive'
        $mF = Get-Mark $H
        Invoke-Go $P 'SkullKeyRelic_BP' 200
        Rest 3
        Invoke-Act $P 'near:SkullKeyRelic_BP!Interact'
        Check $H '(inventory grant applied: /Game/Items/KeyItems/|object intent applied: SkullKeyRelic_BP)' 35 'HOD: the HOST saw the skull relic the peer took' $mF | Out-Null
        $mG = Get-Mark $H
        Invoke-Go $P 'SpectralSword_Pickup' 200
        Rest 3
        Invoke-Act $P 'near:SpectralSword_Pickup!Interact'
        Check $H '(object intent applied: SpectralSword_Pickup|inventory grant applied)' 30 'HOD: the HOST saw the SpectralSword pickup' $mG | Out-Null
        # what the room is for: does ANY flag move?
        $bankA = Get-Bank $H 'SkullKeyRelic_Data'
        Note ("  730_HOD SkullKeyRelic_Data bank read: " + $bankA)
        Invoke-Audit 'R37'
        Save-Shots 'R37-after'
    }

    # =================================================================================
    # R38 -- 740_SZ_Prison: Red Lords, jail doors, the prison key
    # =================================================================================
    if ($Part -in @('prison', 'all') -and (-not $Only -or $Only -match 'R38')) {
        Step 'R38 740_SZ_Prison -- Red Lords (2 sisters), 9 jail doors, JosamiPrisonDoor, prison key'
        [void](Enter-Area $H '740_SZ_Prison' 1 $P)
        Rest 6
        Save-Shots 'R38-arrive'

        # jail doors, one each way
        $mP1 = Get-Mark $P
        Invoke-Go $H 'SZ_JailDoor_BP' 220
        Rest 3
        Invoke-Act $H 'near:SZ_JailDoor_BP!Activate'
        Check $P 'actor states applied=' 30 'the PEER applied the host''s jail-door state' $mP1 | Out-Null
        $mH1 = Get-Mark $H
        Invoke-Go $P 'SZ_JailDoor_BP9' 220
        Rest 3
        Invoke-Act $P 'near:SZ_JailDoor_BP9!Activate'
        Check $H 'actor-state intent applied' 25 'the HOST received the peer''s jail door (a different instance)' $mH1 | Out-Null

        # the prison key is a real key ITEM
        $mH2 = Get-Mark $H
        Invoke-Go $P 'Prison_key_1F_BP' 200
        Rest 3
        Invoke-Act $P 'near:Prison_key_1F_BP!Interact'
        Check $H 'inventory grant applied: /Game/Items/KeyItems/.*Prison' 35 'the HOST was granted the prison key' $mH2 | Out-Null

        # the arena gate + the boss
        $mH3 = Get-Mark $H
        Invoke-Go $H 'ActivationTrigger4' 300
        Rest 2
        Set-Control $H 'qd.trigger' ('ActivationTrigger4@740_SZ_Prison#' + (Get-Date -Format 'HHmmssfff'))
        Check $H 'trigger: .*ActivationTrigger4' 25 'the HOST dispatched the Red Lords trigger' $mH3 | Out-Null
        Check $P 'trigger: replayed the partner' 30 'the PEER replayed the same trigger' $mH3 | Out-Null
        Rest 6
        Check $H 'interact: door anim|JosamiPrisonDoor' 25 'the HOST''s prison gate moved with the fight start' $mH3 | Out-Null
        Rest 4
        Save-Shots 'R38-boss-start'

        $dh = Get-Enemies $H 'prison-a' 70
        $dp = Get-Enemies $P 'prison-a' 70
        $bh = @($dh.Values | Where-Object { $_.Cls -match 'JosamiBloodLord' -and $_.State -eq 'ALIVE' })
        $bp = @($dp.Values | Where-Object { $_.Cls -match 'JosamiBloodLord' -and $_.State -eq 'ALIVE' })
        Verdict $H ($bh.Count -ge 2) 'both Red Lords are ALIVE on the HOST' (($bh | ForEach-Object { "$($_.Key) $($_.Cls) hp=$($_.Hp)" }) -join ' | ')
        Verdict $P ($bp.Count -ge 2) 'both Red Lords are ALIVE on the PEER' (($bp | ForEach-Object { "$($_.Key) $($_.Cls) hp=$($_.Hp)" }) -join ' | ')

        # REAL ATTACKS, god OFF (the playthrough rule): the peer equips and swings.
        foreach ($i in @($H, $P)) { Set-Control $i 'qd.god' $null }
        Invoke-Go $P 'JosamiBloodLord' 260
        Rest 3
        $mAtk = Get-Mark $P
        Set-Control $P 'qd.attack' ('Attack,400,8#' + (Get-Date -Format 'HHmmssfff'))
        Rest 6
        Check $P 'rig: atkcall|local montage .*kind' 25 'the PEER swung the game''s own attack input' $mAtk | Out-Null
        Check $H '(damage: enemy hp|player body hit|damage: .*->)' 30 'the HOST saw the peer''s damage land' $mAtk | Out-Null
        Save-Shots 'R38-fight'

        # the REVIVE rule: one sister alone stands up again
        $dh = Get-Enemies $H 'prison-b' 60
        $first = @($dh.Values | Where-Object { $_.Cls -match 'JosamiBloodLord' -and $_.State -eq 'ALIVE' } | Select-Object -First 1)
        if ($first.Count -gt 0) {
            $k1 = $first[0].Key
            Set-Control $H 'qd.hitguid' ($k1 + ':1000000#' + (Get-Date -Format 'HHmmssfff'))
            Rest 6
            $d1 = Get-Enemies $H 'prison-revive' 60
            $row = $d1[$first[0].Id]
            $up = $row -and $row.State -eq 'ALIVE'
            Verdict $H $up 'one Red Lord alone is revived by her sister (the rule the route states)' $(if ($row) { $row.Line } else { 'no row' })
        }
        # both down: strike every live member in the same second
        $dh = Get-Enemies $H 'prison-c' 60
        $live = @($dh.Values | Where-Object { $_.Cls -match 'JosamiBloodLord' -and $_.State -eq 'ALIVE' })
        foreach ($e in $live) {
            Set-Control $H 'qd.hitguid' ($e.Key + ':1000000#' + (Get-Date -Format 'HHmmssfff'))
            Start-Sleep -Milliseconds 400
        }
        Rest 10
        $dh2 = Get-Enemies $H 'prison-d' 60
        $dp2 = Get-Enemies $P 'prison-d' 60
        $liveH = @($dh2.Values | Where-Object { $_.Cls -match 'JosamiBloodLord' -and $_.State -eq 'ALIVE' })
        $liveP = @($dp2.Values | Where-Object { $_.Cls -match 'JosamiBloodLord' -and $_.State -eq 'ALIVE' })
        Verdict $H ($liveH.Count -eq 0) 'both Red Lords are DOWN on the HOST' (($liveH | ForEach-Object { "$($_.Key) hp=$($_.Hp)" }) -join ' ')
        Verdict $P ($liveP.Count -eq 0) 'both Red Lords are DOWN on the PEER' (($liveP | ForEach-Object { "$($_.Key) hp=$($_.Hp)" }) -join ' ')
        $vH = Get-Bank $H 'RedLordsDead'
        $vP = Get-Bank $P 'RedLordsDead'
        Verdict $H ($vH -eq '1') 'RedLordsDead = 1 in the host bank' ("host=$vH peer=$vP")
        Verdict $P ($vP -eq '1') 'RedLordsDead = 1 in the peer bank' ("host=$vH peer=$vP")
        Save-Shots 'R38-dead'
        Invoke-Audit 'R38'
    }

    # =================================================================================
    # R39 / R43 -- 750_SZ_MAUS2, 775_JigekiTemple
    # =================================================================================
    if ($Part -in @('swamp', 'all') -and (-not $Only -or $Only -match 'R39')) {
        Step 'R39 750_SZ_MAUS2 -- the deep mausoleum (no boss, no flag of its own)'
        [void](Enter-Area $H '750_SZ_MAUS2' 1 $P)
        Rest 5
        Save-Shots 'R39-arrive'
        $m1 = Get-Mark $H
        Invoke-Go $P 'BookKey_00_Puwema_BP' 200
        Rest 3
        Invoke-Act $P 'near:BookKey_00_Puwema_BP!Interact'
        Check $H 'inventory grant applied: /Game/Items/KeyItems/' 35 '750: the HOST was granted the Puwema book key' $m1 | Out-Null
        $m2 = Get-Mark $H
        Invoke-Go $P 'SipehaRune_SZ_Maus2' 200
        Rest 3
        Invoke-Act $P 'near:SipehaRune_SZ_Maus2!Activate'
        Check $H 'actor-state intent applied' 25 '750: the HOST received the mausoleum rune' $m2 | Out-Null
        Invoke-Audit 'R39'
        Save-Shots 'R39-after'
    }
    if ($Part -in @('swamp', 'all') -and (-not $Only -or $Only -match 'R43')) {
        Step 'R43 775_JigekiTemple -- JigekiDoor, SZ_BoringDoor, FishWifeEvil'
        [void](Enter-Area $H '775_JigekiTemple' 1 $P)
        Rest 5
        Save-Shots 'R43-arrive'
        $m3 = Get-Mark $P
        Invoke-Go $H 'JigekiDoorR_SM' 220
        Rest 3
        Invoke-Act $H 'near:JigekiDoorR_SM!Activate'
        Check $P 'actor states applied=|interact: door anim' 30 'the PEER applied the Jigeki door' $m3 | Out-Null
        $m4 = Get-Mark $H
        Invoke-Go $P 'SZ_BoringDoor_00_BP9' 220
        Rest 3
        Invoke-Act $P 'near:SZ_BoringDoor_00_BP9!Activate'
        Check $H 'actor-state intent applied' 25 'the HOST received the SZ boring door' $m4 | Out-Null
        Invoke-Audit 'R43'
        Save-Shots 'R43-after'
    }

    # =================================================================================
    # R42 / B7 -- 770_SZ_Swamp: Giant Fish + vault relic #4
    # =================================================================================
    if ($Part -in @('swamp', 'all') -and (-not $Only -or $Only -match 'R42')) {
        Step 'R42 770_SZ_Swamp -- Giant Fish (B7) and SR_GreenKey_Acquired'
        [void](Enter-Area $H '770_SZ_Swamp' 1 $P)
        Rest 6
        Save-Shots 'R42-arrive'
        # the vault relic first (setter3 writes SR_GreenKey_Acquired)
        $m5 = Get-Mark $H
        Invoke-Go $P 'ActivatableNamedSaveDataSetter3' 200
        Rest 3
        Invoke-Act $P 'near:ActivatableNamedSaveDataSetter3!Activate'
        Check $P 'flag (NEW |CHANGE )?SR_GreenKey_Acquired' 30 'the PEER wrote SR_GreenKey_Acquired locally' $m5 | Out-Null
        Check $H 'flag (NEW |CHANGE )?SR_GreenKey_Acquired' 30 'the HOST received SR_GreenKey_Acquired' $m5 | Out-Null
        $gH = Get-Bank $H 'SR_GreenKey_Acquired'
        $gP = Get-Bank $P 'SR_GreenKey_Acquired'
        Verdict $H ($gH -eq '1') 'SR_GreenKey_Acquired = 1 in the host bank' ("host=$gH peer=$gP")

        # the fish: proximity spawner; the route's lever is the spawner itself
        $m6 = Get-Mark $H
        Invoke-Go $H 'GiantFish_Spawner_BP' 600
        Rest 3
        Invoke-Act $H 'near:GiantFish_Spawner_BP!Activate'
        Rest 10
        $dh = Get-Enemies $H 'fish-a' 70
        $dp = Get-Enemies $P 'fish-a' 70
        $fh = @($dh.Values | Where-Object { $_.Cls -match 'GiantFish' -and $_.State -eq 'ALIVE' })
        $fp = @($dp.Values | Where-Object { $_.Cls -match 'GiantFish' -and $_.State -eq 'ALIVE' })
        Verdict $H ($fh.Count -ge 1) 'the Giant Fish is ALIVE on the HOST' (($fh | ForEach-Object { "$($_.Key) hp=$($_.Hp) mx=$($_.Hp)" }) -join ' ')
        Verdict $P ($fp.Count -ge 1) 'the Giant Fish is ALIVE on the PEER' (($fp | ForEach-Object { "$($_.Key) hp=$($_.Hp)" }) -join ' ')
        Save-Shots 'R42-boss'
        # god off on the peer, take a hit, then kill it by damage
        Set-Control $P 'qd.god' $null
        Invoke-Go $P 'GiantFish' 400
        Rest 3
        $mDmg = Get-Mark $P
        Rest 12
        Check $P '(player body hit|damage from enemy|hp .*->|vitals: partner)' 30 'the PEER took boss damage with god off' $mDmg | Out-Null
        $dh = Get-Enemies $H 'fish-b' 70
        $fx = @($dh.Values | Where-Object { $_.Cls -match 'GiantFish' -and $_.State -eq 'ALIVE' } | Select-Object -First 1)
        if ($fx.Count -gt 0) {
            $mx = if ($fx[0].Hp) { [double]$fx[0].Hp } else { 1800 }
            $chunk = [int][math]::Ceiling([math]::Max(50, $mx / 4))
            for ($k = 0; $k -lt 8; $k++) {
                Set-Control $H 'qd.hitguid' ($fx[0].Key + ':' + $chunk + '#' + (Get-Date -Format 'HHmmssfff'))
                Rest 3
                $dk = Get-Enemies $H ('fish-k' + $k) 60
                $rk = $dk[$fx[0].Id]
                if (-not $rk -or $rk.State -ne 'ALIVE') { break }
            }
        }
        Rest 8
        $dh2 = Get-Enemies $H 'fish-c' 60
        $dp2 = Get-Enemies $P 'fish-c' 60
        $aliveH = @($dh2.Values | Where-Object { $_.Cls -match 'GiantFish' -and $_.State -eq 'ALIVE' })
        $aliveP = @($dp2.Values | Where-Object { $_.Cls -match 'GiantFish' -and $_.State -eq 'ALIVE' })
        Verdict $H ($aliveH.Count -eq 0) 'the Giant Fish is DEAD on the HOST' (($aliveH | ForEach-Object { "$($_.Key) hp=$($_.Hp)" }) -join ' ')
        Verdict $P ($aliveP.Count -eq 0) 'the Giant Fish is DEAD on the PEER' (($aliveP | ForEach-Object { "$($_.Key) hp=$($_.Hp)" }) -join ' ')
        $fH = Get-Bank $H 'GiantSwampFish_IsDead'
        $fP = Get-Bank $P 'GiantSwampFish_IsDead'
        Verdict $H ($fH -eq '1') 'GiantSwampFish_IsDead = 1 in the host bank' ("host=$fH peer=$fP")
        Verdict $P ($fP -eq '1') 'GiantSwampFish_IsDead = 1 in the peer bank' ("host=$fH peer=$fP")
        Save-Shots 'R42-dead'
        Invoke-Audit 'R42'
    }

    # =================================================================================
    # R41 / B8 -- 760_SZ_AUT: ROS Dragon, cage gate, ROS door, the book puzzle
    # =================================================================================
    if ($Part -in @('final', 'all') -and (-not $Only -or $Only -match 'R41')) {
        Step 'R41 760_SZ_AUT -- ROS Dragon (B8), Ros_Cage_Gate, ROS_Door, the ROS book puzzle'
        [void](Enter-Area $H '760_SZ_AUT' 1 $P)
        Rest 7
        Save-Shots 'R41-arrive'

        # the book puzzle: books carry NO state property; only the completion flag crosses.
        $bk = Get-Props $P 'Lib_Book_02_BP' 'Opened,Broken,DoorClosed,Phase'
        Note ("  Lib_Book_02 props on the peer: " + (($bk.GetEnumerator() | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join ' '))
        $rec = Get-Props $P 'LIB_BookRecepticle_BP' 'Opened,Activated,Broken'
        Note ("  LIB_BookRecepticle props on the peer: " + (($rec.GetEnumerator() | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join ' '))
        # THE PUZZLE'S REAL VERB: insert a book through the receptacle's own Interact
        # (part p2 measured the decor `Lib_Book_*` props have NO verb that runs at all:
        # Interact/Activate/Open/Rotate/Push/Toggle all `ran=no`).
        $rbBefore = Get-Bank $H 'ROSBookPuzzleComplete'
        $mBk = Get-Mark $P
        foreach ($rn in 'LIB_BookRecepticle_BP','LIB_BookRecepticle_BP2','LIB_BookRecepticle_BP3','LIB_BookRecepticle_BP4') {
            Invoke-Go $P $rn 220
            Rest 2
            Invoke-Act $P ('near:' + $rn + '!Interact')
            Rest 3
        }
        Check $P 'forced verb Interact on LIB_BookRecepticle' 20 'the PEER inserted a book through the receptacle' $mBk | Out-Null
        $rbH = Get-Bank $H 'ROSBookPuzzleComplete'
        $rbP = Get-Bank $P 'ROSBookPuzzleComplete'
        Verdict $H ($rbH -eq $rbP) 'the ROS book-puzzle flag agrees on both machines' ("host=$rbH peer=$rbP (before=$rbBefore)")

        # the ROS door and the cage gate (both savable doors)
        $m7 = Get-Mark $P
        Invoke-Go $H 'ROS_Door_BP' 220
        Rest 3
        Invoke-Act $H 'near:ROS_Door_BP!Activate'
        Check $P '(actor states applied=|interact: door anim)' 30 'the PEER applied the ROS door' $m7 | Out-Null
        $m8 = Get-Mark $H
        Invoke-Go $P 'Ros_Cage_Gate_BP' 220
        Rest 3
        Invoke-Act $P 'near:Ros_Cage_Gate_BP!Activate'
        Check $H '(actor-state intent applied|interact: door anim)' 30 'the HOST received the cage gate' $m8 | Out-Null

        # the fight
        $m9 = Get-Mark $H
        Invoke-Go $H 'ActivationTrigger11' 300
        Rest 2
        Set-Control $H 'qd.trigger' ('ActivationTrigger11@760_SZ_AUT#' + (Get-Date -Format 'HHmmssfff'))
        Check $P 'trigger: replayed the partner' 30 'the PEER replayed the ROS trigger' $m9 | Out-Null
        Rest 8
        $dh = Get-Enemies $H 'ros-a' 70
        $dp = Get-Enemies $P 'ros-a' 70
        $rh = @($dh.Values | Where-Object { $_.Cls -match 'Dragon_Ros|ROSDragon' -and $_.State -eq 'ALIVE' })
        $rp = @($dp.Values | Where-Object { $_.Cls -match 'Dragon_Ros|ROSDragon' -and $_.State -eq 'ALIVE' })
        Verdict $H ($rh.Count -ge 1) 'the ROS Dragon is ALIVE on the HOST' (($rh | ForEach-Object { "$($_.Key) hp=$($_.Hp)" }) -join ' ')
        Verdict $P ($rp.Count -ge 1) 'the ROS Dragon is ALIVE on the PEER' (($rp | ForEach-Object { "$($_.Key) hp=$($_.Hp)" }) -join ' ')
        # the arena's post-process is per-machine (GAP 9): report what each machine says
        $ppcH = @(Get-Matches $H 'ROS_BossArenaPPC|PPC.*(on|off)')
        Note ("  arena PPC lines on the host: " + $ppcH.Count)
        Save-Shots 'R41-boss'
        foreach ($i in @($H, $P)) { Set-Control $i 'qd.god' $null }
        Invoke-Go $P 'Dragon_Ros' 500
        Rest 12
        $mDmg = Get-Mark $P
        Rest 10
        Check $P '(player body hit|damage from enemy|hp .*->|vitals: partner)' 30 'the PEER took dragon damage with god off' $mDmg | Out-Null
        $dh = Get-Enemies $H 'ros-b' 70
        $rx = @($dh.Values | Where-Object { $_.Cls -match 'Dragon_Ros|ROSDragon' -and $_.State -eq 'ALIVE' } | Select-Object -First 1)
        if ($rx.Count -gt 0) {
            $mx = if ($rx[0].Hp) { [double]$rx[0].Hp } else { 9999 }
            $chunk = [int][math]::Ceiling([math]::Max(50, $mx / 4))
            for ($k = 0; $k -lt 12; $k++) {
                Set-Control $H 'qd.hitguid' ($rx[0].Key + ':' + $chunk + '#' + (Get-Date -Format 'HHmmssfff'))
                Rest 3
                $dk = Get-Enemies $H ('ros-k' + $k) 60
                $rk = $dk[$rx[0].Id]
                if (-not $rk -or $rk.State -ne 'ALIVE') { break }
            }
        }
        Rest 10
        $dh2 = Get-Enemies $H 'ros-c' 60
        $dp2 = Get-Enemies $P 'ros-c' 60
        $alH = @($dh2.Values | Where-Object { $_.Cls -match 'Dragon_Ros|ROSDragon' -and $_.State -eq 'ALIVE' })
        $alP = @($dp2.Values | Where-Object { $_.Cls -match 'Dragon_Ros|ROSDragon' -and $_.State -eq 'ALIVE' })
        Verdict $H ($alH.Count -eq 0) 'the ROS Dragon is DEAD on the HOST' (($alH | ForEach-Object { "$($_.Key) hp=$($_.Hp)" }) -join ' ')
        Verdict $P ($alP.Count -eq 0) 'the ROS Dragon is DEAD on the PEER' (($alP | ForEach-Object { "$($_.Key) hp=$($_.Hp)" }) -join ' ')
        $rH = Get-Bank $H 'ROSDragonDead'
        $rP = Get-Bank $P 'ROSDragonDead'
        Verdict $H ($rH -eq '1') 'ROSDragonDead = 1 in the host bank' ("host=$rH peer=$rP")
        Verdict $P ($rP -eq '1') 'ROSDragonDead = 1 in the peer bank' ("host=$rH peer=$rP")
        Save-Shots 'R41-dead'
        Invoke-Audit 'R41'
    }

    # =================================================================================
    # R44 / B9 -- 780_SZ_Depths: Dragon Lord (both bodies), DesBoss_Gate, vault relic #5
    # =================================================================================
    if ($Part -in @('final', 'all') -and (-not $Only -or $Only -match 'R44')) {
        Step 'R44 780_SZ_Depths -- Dragon Lord (green dragon + Josami rider), DesBoss_Gate'
        [void](Enter-Area $H '780_SZ_Depths' 1 $P)
        Rest 7
        Save-Shots 'R44-arrive'
        $m10 = Get-Mark $H
        Invoke-Go $P 'ActivatableNamedSaveDataSetter5' 200
        Rest 3
        Invoke-Act $P 'near:ActivatableNamedSaveDataSetter5!Activate'
        Check $P 'flag (NEW |CHANGE )?SR_Key_Blue_Acquired' 30 'the PEER wrote SR_Key_Blue_Acquired locally' $m10 | Out-Null
        Check $H 'flag (NEW |CHANGE )?SR_Key_Blue_Acquired' 30 'the HOST received SR_Key_Blue_Acquired' $m10 | Out-Null
        $bH = Get-Bank $H 'SR_Key_Blue_Acquired'
        $bP = Get-Bank $P 'SR_Key_Blue_Acquired'
        Verdict $H ($bH -eq '1') 'SR_Key_Blue_Acquired = 1 in the host bank' ("host=$bH peer=$bP")

        $m11 = Get-Mark $H
        Invoke-Go $H 'ActivationTrigger9' 300
        Rest 2
        Set-Control $H 'qd.trigger' ('ActivationTrigger9@780_SZ_Depths#' + (Get-Date -Format 'HHmmssfff'))
        Check $P 'trigger: replayed the partner' 30 'the PEER replayed the Dragon Lord trigger' $m11 | Out-Null
        Rest 8
        Check $H 'DesBoss_Gate|interact: door anim' 25 'the HOST''s DesBoss gate moved with the fight' $m11 | Out-Null
        $dh = Get-Enemies $H 'dl-a' 70
        $dp = Get-Enemies $P 'dl-a' 70
        $gh = @($dh.Values | Where-Object { $_.Cls -match 'Dragon_Green|GreenDragon' -and $_.State -eq 'ALIVE' })
        $gp = @($dp.Values | Where-Object { $_.Cls -match 'Dragon_Green|GreenDragon' -and $_.State -eq 'ALIVE' })
        $jh = @($dh.Values | Where-Object { $_.Cls -match 'JosamiLord_Green|GreenDragon' -and $_.State -eq 'ALIVE' })
        Verdict $H ($gh.Count -ge 1) 'the green dragon is ALIVE on the HOST' (($gh | ForEach-Object { "$($_.Key) $($_.Cls) hp=$($_.Hp)" }) -join ' ')
        Verdict $P ($gp.Count -ge 1) 'the green dragon is ALIVE on the PEER' (($gp | ForEach-Object { "$($_.Key) hp=$($_.Hp)" }) -join ' ')
        Save-Shots 'R44-boss'
        foreach ($i in @($H, $P)) { Set-Control $i 'qd.god' $null }
        Invoke-Go $P 'Dragon_Green' 500
        Rest 12
        $mDmg = Get-Mark $P
        Rest 10
        Check $P '(player body hit|damage from enemy|hp .*->|vitals: partner)' 30 'the PEER took the dragon lord''s damage with god off' $mDmg | Out-Null
        # kill every live body of the group, the rider included
        for ($round = 0; $round -lt 4; $round++) {
            $dx = Get-Enemies $H ('dl-g' + $round) 60
            $left = @($dx.Values | Where-Object { $_.State -eq 'ALIVE' -and ($_.Cls -match 'Dragon_Green|JosamiLord_Green') })
            if ($left.Count -eq 0) { break }
            foreach ($e in $left) {
                Set-Control $H 'qd.hitguid' ($e.Key + ':1000000#' + (Get-Date -Format 'HHmmssfff'))
                Rest 2
            }
            Rest 6
        }
        Rest 8
        $dh2 = Get-Enemies $H 'dl-c' 60
        $dp2 = Get-Enemies $P 'dl-c' 60
        $alH = @($dh2.Values | Where-Object { $_.State -eq 'ALIVE' -and ($_.Cls -match 'Dragon_Green|JosamiLord_Green') })
        $alP = @($dp2.Values | Where-Object { $_.State -eq 'ALIVE' -and ($_.Cls -match 'Dragon_Green|JosamiLord_Green') })
        Verdict $H ($alH.Count -eq 0) 'both Dragon Lord bodies are DOWN on the HOST' (($alH | ForEach-Object { "$($_.Key) $($_.Cls) hp=$($_.Hp)" }) -join ' ')
        Verdict $P ($alP.Count -eq 0) 'both Dragon Lord bodies are DOWN on the PEER' (($alP | ForEach-Object { "$($_.Key) $($_.Cls) hp=$($_.Hp)" }) -join ' ')
        $dH = Get-Bank $H 'DragonLordDead'
        $dP = Get-Bank $P 'DragonLordDead'
        Verdict $H ($dH -eq '1') 'DragonLordDead = 1 in the host bank' ("host=$dH peer=$dP")
        Verdict $P ($dP -eq '1') 'DragonLordDead = 1 in the peer bank' ("host=$dH peer=$dP")
        Save-Shots 'R44-dead'
        Invoke-Audit 'R44'
    }

    # =================================================================================
    # R45 -- 790_SG: the Stone Garden puzzle + the second Whale King
    # =================================================================================
    if ($Part -in @('final', 'all') -and (-not $Only -or $Only -match 'R45')) {
        Step 'R45 790_SG -- Stone Garden puzzle, Whale King_SZ, runes, Checkpoint16'
        [void](Enter-Area $H '790_SG' 1 $P)
        Rest 7
        Save-Shots 'R45-arrive'
        # the puzzle script: read whether it exposes any state at all
        $ps = Get-Props $P 'StoneGardenPuzzleScript' 'Solved,Finished,Complete,Phase,Opened,Activated'
        Note ("  StoneGardenPuzzleScript props on the peer: " + (($ps.GetEnumerator() | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join ' '))
        # the pedestals take the trident key through their own Interact; the script is a
        # SavableActor, so its own state is a GUID entry the actor lane carries.
        $sgBefore = Get-Bank $H 'StoneGardenPuzzleFinished'
        $mPed = Get-Mark $P
        foreach ($pn in 'SG_Pedestal_01_BP','SG_Pedestal_01_BP2','SG_Pedestal_01_BP3','SG_Pedestal_01_BP4','SG_Pedestal_01_BP5') {
            Invoke-Go $P $pn 220
            Rest 2
            Invoke-Act $P ('near:' + $pn + '!Interact')
            Rest 2
        }
        Check $P 'forced verb Interact on SG_Pedestal' 20 'the PEER inserted a key at the pedestals' $mPed | Out-Null
        $mScr = Get-Mark $H
        Invoke-Go $P 'StoneGardenPuzzleScript' 220
        Rest 2
        Invoke-Act $P 'near:StoneGardenPuzzleScript!Activate'
        Check $H '(actor-state intent applied|object intent applied: StoneGardenPuzzleScript)' 30 'the HOST received the puzzle script''s own state' $mScr | Out-Null
        # play-p4b: `StoneGardenPuzzleScript_C` is a SavableActor_C, so its own state is a
        # `SavableActorStates` ENTRY -- read both machines' index/value for it (the GUID lane
        # is the carrier only when that row exists; `dev state:` says which it is).
        $mStH = Get-Mark $H
        Invoke-Act $H 'near:StoneGardenPuzzleScript!?Opened,Activated'
        Check $H 'dev state: StoneGardenPuzzleScript' 15 'host: the puzzle script''s own savable entry (index/value)' $mStH | Out-Null
        $mStP = Get-Mark $P
        Invoke-Act $P 'near:StoneGardenPuzzleScript!?Opened,Activated'
        Check $P 'dev state: StoneGardenPuzzleScript' 15 'peer: the same entry on the peer' $mStP | Out-Null
        $flagH = Get-Bank $H 'StoneGardenPuzzleFinished'
        $flagP = Get-Bank $P 'StoneGardenPuzzleFinished'
        Verdict $H ($flagH -eq $flagP) 'the Stone Garden completion flag agrees on both machines (a skip is not a solve)' ("host=$flagH peer=$flagP (before=$sgBefore)")

        # the puzzle pedestals / runes are savable/activatable: prove the mechanism on one
        $m12 = Get-Mark $H
        Invoke-Go $P 'Jigeki_Rune_BP' 200
        Rest 3
        Invoke-Act $P 'near:Jigeki_Rune_BP!Activate'
        Check $H '(actor-state intent applied|object intent applied: Jigeki_Rune)' 30 'the HOST received the SG rune activation' $m12 | Out-Null

        # the second Whale King placement
        $m13 = Get-Mark $H
        Invoke-Go $H 'WhaleKing_SZ_Spawner' 600
        Rest 3
        Invoke-Act $H 'near:WhaleKing_SZ_Spawner!Activate'
        Rest 10
        $dh = Get-Enemies $H 'wk-a' 70
        $dp = Get-Enemies $P 'wk-a' 70
        $wh = @($dh.Values | Where-Object { $_.Cls -match 'Whaleking|WhaleKing' -and $_.State -eq 'ALIVE' })
        $wp = @($dp.Values | Where-Object { $_.Cls -match 'Whaleking|WhaleKing' -and $_.State -eq 'ALIVE' })
        Verdict $H ($wh.Count -ge 1) 'the Stone Garden Whale King is ALIVE on the HOST' (($wh | ForEach-Object { "$($_.Key) hp=$($_.Hp)" }) -join ' ')
        Verdict $P ($wp.Count -ge 1) 'the Stone Garden Whale King is ALIVE on the PEER' (($wp | ForEach-Object { "$($_.Key) hp=$($_.Hp)" }) -join ' ')
        Save-Shots 'R45-whale'
        $m14 = Get-Mark $H
        Set-Control $H 'qd.flag' ('WhaleKingIsDead#' + (Get-Date -Format 'HHmmssfff'))
        Rest 3
        Check $H 'rig: flag dump -- WhaleKingIsDead' 15 'the whale placement does not write WhaleKingIsDead on its own' $m14 | Out-Null
        Invoke-Audit 'R45'
        Save-Shots 'R45-after'
    }

    # =================================================================================
    # R46 -- 800_FinalZone: the Secret Skull Door quest + ritual
    # =================================================================================
    if ($Part -in @('final', 'all') -and (-not $Only -or $Only -match 'R46')) {
        Step 'R46 800_FinalZone -- SkullDoorQuestStateManager, SecretSkullDoor NPC, SecretSkullRitual'
        [void](Enter-Area $H '800_FinalZone' 1 $P)
        Rest 7
        Save-Shots 'R46-arrive'
        # the manager's InternalState is a quest-state machine (rides the flag refresh)
        $qs = Get-Props $P 'SkullDoorQuestState0' 'InternalState,DesiredState,Phase'
        Note ("  SkullDoorQuestState0 props on the peer: " + (($qs.GetEnumerator() | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join ' '))
        $m15 = Get-Mark $P
        Invoke-Go $P 'SecretSkullDoor_NPC_BP' 200
        Rest 3
        Invoke-Act $P 'near:SecretSkullDoor_NPC_BP!Interact'
        Check $P 'dialogue: |dev interact: forced' 25 'the PEER talked to the Secret Skull Door NPC' $m15 | Out-Null
        Check $H 'dialogue: BLOCK|dialogue: PREVENTED' 25 'the HOST''s dialogue lane saw the conversation' $m15 | Out-Null
        Rest 6
        # the ritual: an interactable placed actor
        $m16 = Get-Mark $H
        Invoke-Go $P 'SecretSkullRitual_BP' 200
        Rest 3
        Invoke-Act $P 'near:SecretSkullRitual_BP!Interact'
        Check $H '(object intent applied: SecretSkullRitual|actor-state intent applied|inventory grant applied: /Game/Items/KeyItems/)' 35 'the HOST saw the ritual (object/actor/intent)' $m16 | Out-Null
        $skH = Get-Bank $H 'SkullKeyRelic_Data'
        $skP = Get-Bank $P 'SkullKeyRelic_Data'
        Note ("  SkullKeyRelic_Data bank: host=$skH peer=$skP")
        # the wall
        $m17 = Get-Mark $H
        Invoke-Go $P 'SecretWall4' 200
        Rest 3
        Invoke-Act $P 'near:SecretWall4!SetToFinalState'
        Check $H 'object intent applied: SecretWall4' 30 'the HOST''s SecretWall4 opened' $m17 | Out-Null
        Invoke-Audit 'R46'
        Save-Shots 'R46-after'
    }

    # ---------------------------------------------------------------- summary
    Note ''
    Note '================ SUMMARY ================'
    $pass = @($script:verdicts | Where-Object { $_.Ok }).Count
    $fail = @($script:verdicts | Where-Object { -not $_.Ok }).Count
    foreach ($v in $script:verdicts) {
        $t = 'FAIL'; if ($v.Ok) { $t = 'PASS' }
        Note ("{0} [{1}] {2} {3}" -f $t, $v.Inst, $v.Step, $v.What)
    }
    Note ("{0} pass, {1} fail" -f $pass, $fail)
    # process health
    foreach ($i in 1, 2) {
        $alive = Test-Alive $i
        Verdict $i $alive ("inst{0} survived the run" -f $i) $(if ($alive) { 'still running' } else { 'PROCESS EXITED' })
        $fault = @(Get-RunLines $i | Where-Object { $_ -match '(^|[^A-Za-z])FAULT|FATAL|=== Critical|Access violation' })
        Verdict $i ($fault.Count -eq 0) ("inst{0}: no crash/FAULT line" -f $i) $(if ($fault.Count) { ([string]$fault[-1]).Trim() } else { 'none' })
    }
    $stamp = Get-Date -Format 'HHmmss'
    foreach ($i in 1, 2) {
        foreach ($suffix in '', '.prev') {
            $src = $inst[$i].Log + $suffix
            if (Test-Path -LiteralPath $src) {
                $dst = Join-Path $outDir ("run-{0}-{1}-qd{2}{3}.log" -f $Part, $stamp, $i, $suffix)
                try { Copy-Item -LiteralPath $src -Destination $dst -Force; Say ("archived " + $dst) } catch { }
            }
        }
    }
    $script:lines | Set-Content -LiteralPath $Report
    Say "report: $Report"
}
finally {
    Stop-All 'run complete'
    if (-not (Test-Path -LiteralPath $Report)) { $script:lines | Set-Content -LiteralPath $Report -ErrorAction SilentlyContinue }
    Release-Live
}
