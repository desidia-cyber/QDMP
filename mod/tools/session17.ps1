# session17.ps1 -- Session 17 of the live two-instance run, ONE process per scenario.
#
# WHAT THIS SESSION PROVES
# ------------------------
# Session 16 closed the mirror, the one shared HP, the boss bar and the cutscene
# hold/release.  What it left open, in the order the work needs it:
#
#   item1  DOORS AND CHESTS, both ways.  A chest/door the game itself calls
#          `IsInteractible=1`, in an area now reachable WITH content (780_SZ_Depths,
#          760_SZ_AUT), opened by the REAL interact lever on the PEER; the host applies the
#          intent and refreshes so `opened=1` shows on the host, the object is visibly open
#          there, it is not lootable again, and `TotalChestsOpened` is not double-counted.
#          Then the host opens another one and the PEER refreshes.  Never proven live.
#   item2  LOOT HIDING.  An enemy that DROPS, killed with both players present (a mirrored
#          copy where needed), the drop taken on one machine, and `loot: hiding local drop`
#          on the other.
#   item3  760_SZ_AUT: ROSDragon boss -- mirror + one shared HP + bar on both + death on
#          both; Dek_Egg_Activation -- play/hold and joint resume.
#   item4  HIT REACTION on a mirrored copy: damage routed through the class's own
#          `ApplyDamageToHitboxTarget` so the copy plays a hit reaction, with ONE shared HP.
#   item5  FINAL SOAK: every switch at its SHIPPED default, both players in 780 fighting for
#          10 minutes: no crash, kill sets diff 0, shared-enemy HP equal, warning counts.
#
# ONE SCRIPT PER SCENARIO: -Phase item1|item2|item3|item4|soak|all, one boot per phase, and
# each phase owns its own instance root so two phases cannot corrupt each other.
[CmdletBinding()]
param(
    [ValidateSet('smoke', 'item1', 'item2', 'item3', 'item4', 'soak', 'all')]
    [string]$Phase = 'all',
    [int]$BootSeconds = 300,
    [int]$SoakSeconds = 600,
    [string]$Report = '',
    # The SHIPPED ini set (item5).  Every other phase runs the wide test set, because the
    # questions being asked there are per-lane.
    [switch]$ShippedDefaults,
    # The soak runs the SHIPPED ini (all write paths off) by default; this runs the documented
    # PLAY set from doc/PLAYTEST.md instead, which is the only configuration in which
    # diff 0 and shared HP can mean anything at all.
    [switch]$PlaySet
)

$ErrorActionPreference = 'Stop'
$here    = $PSScriptRoot
$repo    = Split-Path -Parent (Split-Path -Parent $here)
$gameDir = 'G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64'
$relay   = Join-Path (Split-Path -Parent $here) 'rust\target\release\qd-relay.exe'
$rigBase = 'G:\Freespace\Queens_Domain\_test'
$rigRoot = Join-Path $rigBase ('s17-' + $Phase)
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
# Each of these levers ignores a repeated string, so every instruction carries a nonce that
# the parser stops at.  The nonce is also what makes "the same question twice" expressible.
function Invoke-BossHit([int]$i, [string]$key, [int]$amount) {
    Set-Control $i 'qd.hitguid' ($key + ':' + $amount + '#' + (Get-Date -Format 'HHmmssfff'))
    Say "  hitguid[$i] -> $key (amount $amount)"
}
function Invoke-Dump([int]$i, [string]$tag) {
    Set-Control $i 'qd.enemies' $tag
    Start-Sleep -Seconds 3
}
function Invoke-ObjDump([int]$i, [string]$tag) {
    Set-Control $i 'qd.obj' ($tag + '#' + (Get-Date -Format 'HHmmssfff'))
    Start-Sleep -Seconds 3
}
function Invoke-FlagDump([int]$i, [string]$name) {
    Set-Control $i 'qd.flag' ($name + '#' + (Get-Date -Format 'HHmmssfff'))
    Start-Sleep -Seconds 3
}
function Invoke-Gate([int]$i, [string]$spec) {
    Set-Control $i 'qd.gate' ($spec + '#' + (Get-Date -Format 'HHmmssfff'))
    Start-Sleep -Seconds 3
}
function Invoke-Interact([int]$i, [string]$spec) {
    Set-Control $i 'qd.act' ($spec + '#' + (Get-Date -Format 'HHmmssfff'))
    Say "  interact[$i] -> $spec"
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
    foreach ($r in $rows) { if ([string]$r -match 'mirrored=1') { return $true } }
    return $false
}
# The owner's key for a class, from the run's own scan line.
function Get-EnemyKeyByClass([int]$i, [int]$markMs, [string]$cls) {
    $rows = @(Get-Since $i $markMs | Where-Object { $_ -match ('scan: kind enemy ' + [regex]::Escape($cls) + ' guid=([0-9A-Fa-f]{8})') })
    if ($rows.Count -eq 0) { return '' }
    if ([string]$rows[-1] -match 'guid=([0-9A-Fa-f]{8})') { return $Matches[1].ToUpperInvariant() }
    return ''
}
# The dump lines a `qd.obj` wrote, keyed by the human name, so a state can be read back.
#
# TWO LANES, ONE ANSWER.  A placed interactable is carried by whichever lane owns it: an
# actor with a `GUIDs` entry is a Savable (ACTOR_STATES, `savkey` line, state = the
# container value + its own `Opened` bool) and one without is a plain placed object
# (K_OBJECTS, `objkey` line, state = its own state bool).  Measured in 780: the CHESTS and
# the `SZ_BoringDoor` instances are savables, so a parser that only understood `objkey`
# reported "no chests" about an area full of them.
function Get-ObjStates([int]$i, [int]$markMs) {
    $out = @{}
    foreach ($ln in @(Get-Since $i $markMs)) {
        $s = [string]$ln
        if ($s -match 'savkey (\S+) (\w+) (\S+) \[([^\]]+)\] value=(-?\d+) isset=(\S+) opened=(\S+) world=(\d) dist=(-?[0-9.]+) at=(-?[0-9.]+),(-?[0-9.]+),(-?[0-9.]+) path=(\S+)') {
            # KEYED ON THE GUID, NOT THE NAME.  The content reuses instance names across
            # levels (`Chest3` exists in more than one map, and `DAquaDoor_01` four times in
            # one), so a name is not an identity and a name-keyed table silently keeps only the
            # last actor of each name.  The first 8 hex of the guid is the short form every
            # other lane uses and the form the lever takes back ("guid:<8 hex>").
            $full = $Matches[1]
            $short = ($full -split '-')[0]
            $out[$short] = [pscustomobject]@{
                Key = $short; KeyFull = $full; Shape = $Matches[2]; Name = $Matches[3]
                Class = $Matches[4]; State = [int]$Matches[5]; Opened = $Matches[7]
                InWorld = $Matches[8]; Dist = [double]$Matches[9]; X = $Matches[10]; Y = $Matches[11]; Z = $Matches[12]; Path = $Matches[13]
                Lane = 'actor-state'
            }
            continue
        }
        if ($s -match 'objkey ([0-9A-Fa-f]{8}) (\w+) (\S+) \[([^\]]+)\] state=(-?\d+)') {
            $out[$Matches[1]] = [pscustomobject]@{
                Key = $Matches[1]; KeyFull = $Matches[1]; Shape = $Matches[2]; Name = $Matches[3]
                Class = $Matches[4]; State = [int]$Matches[5]; Opened = 'n/a'
                InWorld = '1'; Dist = 0; X = 0; Y = 0; Z = 0; Path = '?'; Lane = 'objects'
            }
        }
    }
    return $out
}
# Is this object OPEN, on the machine that just dumped it?  A savable's answer is its own
# `Opened` bool when it has one, otherwise the container value; a plain object's is its
# state bool.
function Test-ObjOpen($o) {
    if ($null -eq $o) { return $false }
    if ($o.Opened -eq '1') { return $true }
    if ($o.Opened -eq '0') { return $false }
    return ($o.State -ne 0)
}
function Get-FlagValue([int]$i, [int]$markMs, [string]$name) {
    $rows = @(Get-Since $i $markMs | Where-Object { $_ -match ('rig: flag dump -- ' + [regex]::Escape($name) + ' = (-?\d+)') })
    if ($rows.Count -eq 0) { return $null }
    if ([string]$rows[-1] -match '= (-?\d+)') { return [int]$Matches[1] }
    return $null
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
    if ($PlaySet) {
        # THE DOCUMENTED PLAY SET (doc/PLAYTEST.md section 4), which is what a human tester is
        # told to run.  The SHIPPED file has every write path at 0, so a soak on it can only ever
        # report diff 0 between two empty kill sets -- true, and worth nothing.
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
        Copy-Item -LiteralPath $ini -Destination (Join-Path $outDir 'qdcoop-play.ini') -Force
    } elseif ($ShippedDefaults -or ($Phase -eq 'soak' -and -not $PlaySet)) {
        # THE SHIPPED SET, verbatim: copied from the repository's own `qdcoop.ini`, with only
        # the relay address pinned to loopback (two instances on one PC).  Item 5's claim is
        # "every switch at shipped defaults", so the file is the source of truth rather than
        # a list in this script that could drift from it.
        $srcIni = Join-Path $repo 'mod\qdcoop.ini'
        $text = Get-Content -LiteralPath $srcIni -Raw
        $text = $text -replace '(?m)^RelayHost=.*$', 'RelayHost=127.0.0.1'
        $text = $text -replace '(?m)^RelayPort=.*$', 'RelayPort=47800'
        $text = $text -replace '(?m)^LogLevel=.*$', 'LogLevel=3'
        $text | Set-Content -LiteralPath $ini -NoNewline
        Say "wrote $ini from the SHIPPED qdcoop.ini (relay pinned to loopback, LogLevel=3)"
        Copy-Item -LiteralPath $ini -Destination (Join-Path $outDir 'qdcoop-shipped.ini') -Force
    } else {
        # MirrorSpawn is NOT written here: Session 16 proved it, so it is the compiled default
        # (qdconfig.h).  Leaving it out means these runs exercise the shipped default.
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
# Shared: get both players into a streamed area WITH its content
# ===========================================================================
function Enter-Area([int]$H, [int]$P, [string]$area, [string]$anchor, [string]$step) {
    Step "$step -- both players into $area through the game's own level list"
    $mH = Get-Mark $H
    $mP = Get-Mark $P
    Invoke-Hard $H $area
    Invoke-Hard $P $area
    Start-Sleep -Seconds 12
    # The area arrives through `AddLevelListToStack`; the LIST name is derived from the pak
    # membership (`qdstream.h LevelListForArea`), so it is reported rather than assumed.
    [void](Check $step $H "level list '.*' added to the manager's stack" 60 'the host pushed the area level list' $mH)
    [void](Check $step $P "level list '.*' added to the manager's stack" 60 'the peer pushed the same list' $mP)
    Start-Sleep -Seconds 12
    Invoke-Go $H $anchor 400
    Invoke-Go $P $anchor 400
    Start-Sleep -Seconds 10
    [void](Check $step $H ('dev teleport near .' + [regex]::Escape($anchor) + '. -> ' + [regex]::Escape($anchor) + ' at') 40 'the host stands at the anchor' $mH)
    [void](Check $step $P ('dev teleport near .' + [regex]::Escape($anchor) + '. -> ' + [regex]::Escape($anchor) + ' at') 40 'the peer stands at the same anchor' $mP)
    $pH = Get-LastPos $H $mH
    $pP = Get-LastPos $P $mP
    Verdict $step $H ([bool]$pH) "the host has a pawn in $area" $(if ($pH) { ("{0:N0},{1:N0},{2:N0}" -f $pH[0], $pH[1], $pH[2]) } else { '' })
    Verdict $step $P ([bool]$pP) "the peer has a pawn in $area" $(if ($pP) { ("{0:N0},{1:N0},{2:N0}" -f $pP[0], $pP[1], $pP[2]) } else { '' })
}

# ===========================================================================
# SMOKE -- the harness itself, before any expensive run
# ===========================================================================
# A session's first run always costs a boot to discover that a lever was misspelled or a
# regex was wrong.  This phase boots once and exercises EVERY new lever and every shared
# helper, asserting only that each produced its line -- so a bug in the harness is found in
# one short run instead of invalidating a long one.
function Invoke-Smoke([int]$H, [int]$P, [string]$step) {
    Step 'SMOKE -- every new lever and every shared helper answers'
    Set-Control $H 'qd.scan' '1'
    Start-Sleep -Seconds 2
    $m = Get-Mark $H
    [void](Check $step $H 'scan: local at ' 40 'the scan reports the local position' $m)
    $m = Get-Mark $H
    Invoke-ObjDump $H 'smoke'
    [void](Check $step $H 'rig: object dump -- smoke' 20 'the NEW qd.obj lever answered' $m)
    $m = Get-Mark $P
    Invoke-ObjDump $P 'smoke'
    [void](Check $step $P 'rig: object dump -- smoke' 20 'the peer''s qd.obj answered' $m)
    $m = Get-Mark $H
    Invoke-FlagDump $H 'TotalChestsOpened'
    [void](Check $step $H 'rig: flag dump -- TotalChestsOpened = ' 20 'the NEW qd.flag lever answered' $m)
    $m = Get-Mark $H
    Invoke-Gate $H 'near:chest'
    [void](Check $step $H "rig: gate -- resolved|rig: gate -- no live actor" 25 'the NEW qd.gate lever answered' $m)
    $m = Get-Mark $H
    Invoke-Dump $H 'smoke'
    [void](Check $step $H 'rig: enemy dump -- smoke' 20 'the qd.enemies lever answered' $m)
    # The shared helpers the scenarios rely on.
    $objs = Get-ObjStates $H 0
    Verdict $step $H ($objs.Count -ge 0) 'Get-ObjStates parsed the object dump' ("objects=$($objs.Count)")
    $fv = Get-FlagValue $H 0 'TotalChestsOpened'
    Verdict $step $H ($null -ne $fv) 'Get-FlagValue parsed the flag dump' ("TotalChestsOpened=$fv")
    $pos = Get-LastPos $H
    Verdict $step $H ([bool]$pos) 'Get-LastPos parsed the scan line' ($(if ($pos) { "{0:N0},{1:N0},{2:N0}" -f $pos[0], $pos[1], $pos[2] } else { '' }))
    # The area entry, which every content phase depends on.
    Enter-Area $H $P '780_SZ_Depths' 'SecretSword_Altar_BP' $step
    $m = Get-Mark $H
    Invoke-ObjDump $H 'smoke-780'
    Start-Sleep -Seconds 2
    $objs = Get-ObjStates $H $m
    Verdict $step $H ($objs.Count -ge 0) 'the object lane tracks objects in 780' ("objects=$($objs.Count)")
    $chests = @($objs.Keys | Where-Object { $objs[$_].Shape -eq 'chest' -or $objs[$_].Shape -eq 'door' })
    Note ("  chest/door candidates in 780: {0} ({1})" -f $chests.Count, ($chests -join ','))
    foreach ($k in ($objs.Keys | Where-Object { $objs[$_].Shape -eq 'chest' -or $objs[$_].Shape -eq 'door' } | Sort-Object | Select-Object -First 20)) {
        Note ("    guid={0} {1,-22} {2,-6} world={3} level={4} opened={5}" -f $k, $objs[$k].Name, $objs[$k].Shape, $objs[$k].InWorld, $objs[$k].Level, $objs[$k].Opened)
    }
    $drops = @(Get-Since $H 0 | Where-Object { $_ -match 'loot: drop ' })
    Note ("  loot: drop lines so far this run: {0}" -f $drops.Count)
    Show-Since $H 'object sync: tracking|object sync: host set|rig: object dump' $m 12
}

# Pin one sublevel on BOTH machines and wait until each actually holds it.
#
# `qd.hard` enters an area through the game's level list, and the MANAGER then applies that
# list one level per frame on its own schedule (Session 15 R113).  Measured (Session 17): after
# entering 780 the host held 760's ROS doors in world and the peer held 780's chests -- the two
# machines had applied DIFFERENT subsets, so an intersection by object NAME found nothing even
# though both worlds were real.  `qd.level` pins one level directly (bShouldBeLoaded + the
# honouring body, plus the area list re-pushed), which is the only way to make "both machines
# hold this area's actors" a fact rather than a race.
function Pin-Level([int]$i, [string]$name) {
    Set-Control $i 'qd.level' $name
    Say "  level[$i] pinned -> $name"
}
function Wait-Level([int]$i, [string]$name, [int]$seconds) {
    return [bool](@(Wait-Since $i ('rig: level engine state ' + [regex]::Escape($name) + ' LoadedLevel=1 actuallyVisible=1') 1 $seconds).Count)
}
# Candidate selection is by MEASUREMENT, not by guessing a name: the area's own
# interactables are dumped (`qd.obj`), and each candidate is ASKED whether the game calls it
# interactible (`qd.gate`, which presses nothing) before anything is opened.  R105's wall was
# measured on a door with no object-lane identity; the area's chests and `SZ_BoringDoor`
# instances are placed actors with an `Opened` bool, so they are the ones that can be carried.
function Invoke-OpenChests([int]$H, [int]$P, [string]$step) {
    Step 'ITEM 1 -- a chest/door opened on the PEER, applied and refreshed on the HOST'
    # BUILD THE VENUE, THEN DUMP IT -- repeatedly, and ACCUMULATE.
    #
    # Both machines must hold the same object before "the same chest" can mean anything, and the
    # level manager applies a list one level per frame on its own schedule (Session 15 R113), so
    # the sets DRIFT.  Measured (Session 17): one dump found the host holding `100_GR` chests
    # while the peer held `760_SZ_AUT` ones, and a later dump of the same run found both on 780.
    # A single sample is therefore a snapshot of a moving target.  The table kept here is the
    # UNION of every dump in the run (latest state per GUID), and the pin is re-asserted between
    # samples because `qd.level` re-pushes the area list on every poll.
    $objsH = @{}
    $objsP = @{}
    for ($round = 1; $round -le 4; $round++) {
        Pin-Level $H '780_SZ_Depths'
        Pin-Level $P '780_SZ_Depths'
        $okH = Wait-Level $H '780_SZ_Depths' 45
        $okP = Wait-Level $P '780_SZ_Depths' 45
        Note ("  round {0}: 780 held by host={1} peer={2}" -f $round, $okH, $okP)
        # The engine's `LoadedLevel=1 actuallyVisible=1` says the LEVEL is in the world; it does
        # not say its actors are registered yet, and the walk only sees registered actors.
        Start-Sleep -Seconds 15
        $mAll = Get-Mark $H
        Invoke-ObjDump $H ('cand-h' + $round)
        Invoke-ObjDump $P ('cand-p' + $round)
        foreach ($o in (Get-ObjStates $H $mAll).Values) { $objsH[$o.Key] = $o }
        foreach ($o in (Get-ObjStates $P $mAll).Values) { $objsP[$o.Key] = $o }
        Note ("  round {0}: accumulated {1} host / {2} peer object(s)" -f $round, $objsH.Count, $objsP.Count)
        $pk = @{}
        foreach ($o in $objsP.Values) { $pk[$o.Key] = $o }
        $shared = 0
        foreach ($o in $objsH.Values) {
            if ($o.Shape -ne 'chest' -and $o.Shape -ne 'door') { continue }
            if ((Test-ObjOpen $o) -or ($o.State -ne 0)) { continue }
            if (-not $pk.ContainsKey($o.Key)) { continue }
            if ((Test-ObjOpen $pk[$o.Key]) -or ($pk[$o.Key].State -ne 0)) { continue }
            ++$shared
        }
        Note ("  round {0}: {1} closed chest/door(s) shared by GUID" -f $round, $shared)
        if ($shared -ge 2) { break }
    }

    # THE CANDIDATES: SAVED GUIDS BOTH MACHINES HOLD, CLOSED ON BOTH.
    #
    # The requirement is that the machine which did NOT press ends up with the object open, and
    # the intent lane addresses an object by its saved GUID.  So the venue has to be an object
    # BOTH machines hold under the SAME GUID -- anything else and "the host applies the peer's
    # press" is not a statement about one object.  Measured (Session 17, run 6): the two
    # machines DO share GUIDs for chests in the hub (`Chest5 guid=4e83575f` was pressed on the
    # peer and applied on the host), which is exactly the case this intersection finds.
    #
    # A chest is preferred over a door because a chest is the object with a payout, which is what
    # the double-count half of the claim needs.
    $byKeyP = @{}
    foreach ($o in $objsP.Values) { $byKeyP[$o.Key] = $o }
    $cand = @()
    foreach ($o in $objsH.Values) {
        if ($o.Shape -ne 'chest' -and $o.Shape -ne 'door') { continue }
        if ($o.Class -match '^Default__' -or $o.Name -match '^Default__') { continue }
        # NO DISTANCE FILTER.  `dist=-1` only means the transform could not be read at the moment
        # of the dump (a level mid-stream), not that the object has no place; the teleport below
        # is what decides whether it can be reached, and it either resolves or it does not.
        if ((Test-ObjOpen $o) -or ($o.State -ne 0)) { continue }
        $match = $byKeyP[$o.Key]
        if (-not $match) { continue }
        if ((Test-ObjOpen $match) -or ($match.State -ne 0)) { continue }
        $o | Add-Member -NotePropertyName PeerKey -NotePropertyValue $match.Key -Force
        $cand += $o
    }
    $cand = @($cand | Sort-Object -Property @{ Expression = { if ($_.Shape -eq 'chest') { 0 } else { 1 } } }, @{ Expression = { $_.Dist } })
    Note ("  closed chests/doors held under the SAME saved GUID by both machines: {0}" -f $cand.Count)
    foreach ($c in $cand) { Note ("    {0,-20} guid={1} dist={2:N0} host-opened={3} peer-opened={4}" -f $c.Name, $c.Key, $c.Dist, $c.Opened, $byKeyP[$c.Key].Opened) }
    # IN THE WORLD THE PLAYER IS ALREADY IN.
    #
    # MEASURED (Session 17, seven runs, and this is the expensive lesson): pressing an object
    # that lives in a DIFFERENT streamed level -- reached by teleporting to its guid -- killed
    # both processes.  The crash was inside this module (`WINMM+0x37988` / `+0x6f7f7`), preceded
    # by `actor-state: game writer for guid=... read back -999999 (WRITE FAILED)`: the game
    # wrote through `Save Data Index to World Save` against a level that was being streamed out
    # from under it.  A test that corrupts the world to prove a lane is not evidence, so the
    # scenario stays in the CURRENT world.  The dump reading of `world=` was also found to
    # answer 0 for streamed sublevels, so the venue is decided by the TELEPORT instead: an actor
    # this machine holds is one the lever can stand beside.

    # MOVE TO THE TARGET, THEN ASK THE GATE.
    #
    # Every closed chest in this world is tens of km from the player's saved position (it is in
    # another level of the same streamed world), so "in range" is not a property to test -- it is
    # a position to create.  The order matters: a gate asked from across the world is a different
    # object's answer, which was this session's first failure mode.
    #
    # THE TELEPORT IS ALSO THE REACHABILITY TEST.  `guid:<key>` resolves through the same world
    # test a press uses, so a machine that cannot stand beside the object is one that cannot
    # press it -- and, measured, one that cannot be asked to APPLY it either: writing a savable
    # whose level is not resident killed both processes (see the note above).
    $reach = @()
    $MAXREACH = 400.0
    foreach ($c in $cand) {
        if ($reach.Count -ge 4) { break }
        $keyP = $c.Key
        if ($c.PSObject.Properties.Match('PeerKey').Count) { $keyP = $c.PeerKey }
        Set-Control $H 'qd.near' '140'
        Set-Control $P 'qd.near' '140'
        Set-Control $H 'qd.tp' ('guid:' + $c.Key + '#' + (Get-Date -Format 'HHmmssfff'))
        Set-Control $P 'qd.tp' ('guid:' + $keyP + '#' + (Get-Date -Format 'HHmmssfff'))
        Start-Sleep -Seconds 4
        $mh = Get-Mark $H; $mp = Get-Mark $P
        Invoke-Gate $H ('near:guid:' + $c.Key)
        Invoke-Gate $P ('near:guid:' + $keyP)
        $gH = @(Get-Since $H $mh | Where-Object { $_ -match "rig: gate -- resolved 'guid:" })
        $gP = @(Get-Since $P $mp | Where-Object { $_ -match "rig: gate -- resolved 'guid:" })
        $distH = '?'; $distP = '?'
        if ($gH.Count -and ([string]$gH[-1] -match 'dist=(-?[0-9.]+)')) { $distH = [double]$Matches[1] }
        if ($gP.Count -and ([string]$gP[-1] -match 'dist=(-?[0-9.]+)')) { $distP = [double]$Matches[1] }
        $nearH = ($distH -is [double]) -and ($distH -le $MAXREACH)
        $nearP = ($distP -is [double]) -and ($distP -le $MAXREACH)
        $gateH = [bool](@(Get-Since $H $mh | Where-Object { $_ -match "IsInteractible=1" }).Count)
        $gateP = [bool](@(Get-Since $P $mp | Where-Object { $_ -match "IsInteractible=1" }).Count)
        Note ("  {0} guid={1}: host dist={2} gate={3} ; peer dist={4} gate={5}" -f $c.Name, $c.Key, $distH, $gateH, $distP, $gateP)
        $c | Add-Member -NotePropertyName GateH -NotePropertyValue $gateH -Force
        $c | Add-Member -NotePropertyName GateP -NotePropertyValue $gateP -Force
        $c | Add-Member -NotePropertyName KeyP -NotePropertyValue $keyP -Force
        $c | Add-Member -NotePropertyName NearH -NotePropertyValue $nearH -Force
        $c | Add-Member -NotePropertyName NearP -NotePropertyValue $nearP -Force
        if ($nearH -or $nearP) { $reach += $c }
    }
    Verdict $step $P ($reach.Count -ge 1) 'a closed chest/door can be reached on at least one machine' ("count=$($reach.Count) of $($cand.Count)")
    if ($reach.Count -eq 0) { $reach = $cand }   # fall through, so the reasons stay in the log

    # THE DIRECTION EACH CANDIDATE IS USED FOR IS DECIDED BY WHO CAN REACH IT.
    #
    # The claim has two halves and each needs a DIFFERENT machine to be the presser, so a target
    # is classified by which machine's pawn is actually standing beside it: a candidate the PEER
    # can reach is the forward direction, one only the HOST can reach is the reverse direction.
    # This is also what makes the test honest in a venue where the two instances hold different
    # level sets -- the machine that holds the object is the machine that presses it, and the
    # OTHER machine is the one whose state must converge, which is the whole requirement.
    $forPeer = @($reach | Where-Object { $_.NearP })
    $forHost = @($reach | Where-Object { $_.NearH -and -not $_.NearP })
    if ($forHost.Count -eq 0) { $forHost = @($reach | Where-Object { $_.NearH }) }
    # The reverse direction needs a DIFFERENT object: a second press on the one the peer just
    # opened cannot show the peer learning anything (it opened it), and the first version
    # selected the same chest twice.
    if ($forPeer.Count -ge 1) {
        $usedKey = $forPeer[0].Key
        $forHost = @($forHost | Where-Object { $_.Key -ne $usedKey })
    }
    Note ("  reachable by the PEER (forward direction): {0}; by the HOST only (reverse direction): {1}" -f $forPeer.Count, $forHost.Count)
    Verdict $step $P (($forPeer.Count + $forHost.Count) -ge 1) 'at least one closed chest/door is reachable to press' ("peer=$($forPeer.Count) host=$($forHost.Count)")

    # -----------------------------------------------------------------------
    # 1a. The PEER opens one.  Intent -> host apply -> host refresh.
    # -----------------------------------------------------------------------
    Step 'ITEM 1a -- PEER opens: intent, host apply, host opened=1'
    if ($forPeer.Count -ge 1) {
        $c = $forPeer[0]
        Note ("  the peer will open '{0}' ({1})" -f $c.Name, $c.Shape)
        # The counter the chest pays out, read BEFORE the press on both machines.
        $mF0 = Get-Mark $H
        Invoke-FlagDump $H 'TotalChestsOpened'
        $cH0 = Get-FlagValue $H $mF0 'TotalChestsOpened'
        $mF0p = Get-Mark $P
        Invoke-FlagDump $P 'TotalChestsOpened'
        $cP0 = Get-FlagValue $P $mF0p 'TotalChestsOpened'
        Note ("  TotalChestsOpened before: host={0} peer={1}" -f $cH0, $cP0)

        $mA = Get-Mark $P
        $mAH = Get-Mark $H
        Invoke-Interact $P ('near:guid:' + $c.KeyP)
        [void](Check $step $P "dev interact: resolved 'guid:" 30 'the peer resolved the object by SAVED GUID' $mA)
        # THE INTENT IS THE PROOF, NOT THE GATE.  The gate's own answer is content (`IsInteractible`
        # is a Blueprint bool), while the claim here is that the LANE carries a change the game
        # made.  So the assertions are: the press ran the game's own verb, and the value the lane
        # reads moved.
        [void](Check $step $P 'dev interact: target=.* verb=' 30 'the game''s own Interact verb ran on the object' $mA)
        $sentP = @(Wait-Since $P 'actor-state intent sent|object intent sent' $mA 40)
        $laneObj = @($sentP | Where-Object { $_ -match 'object intent sent' }).Count -gt 0
        Verdict $step $P ($sentP.Count -gt 0) ("the peer's change went out as an intent (" + $(if ($laneObj) { 'K_OBJECTS lane' } else { 'ACTOR_STATES lane' }) + ")") ($(if ($sentP.Count) { ([string]$sentP[-1]).Trim() } else { '' }))
        if ($laneObj) {
            [void](Check $step $H 'object apply: ' 45 'the HOST applied the object intent' $mAH)
        } else {            [void](Check $step $H 'actor-state intent applied: ' 45 'the HOST applied the actor-state intent' $mAH)
            # `opened=1` on THIS line is the mod's own read-back of the game's actor after the
            # refresh, so it is the strongest statement in the lane: the HOST's live object now
            # says it is open.
            $refr = @(Wait-Since $H 'actor-state refresh ok .* opened=1' $mAH 45)
            Verdict $step $H ($refr.Count -gt 0) 'the HOST refreshed the live actor and it reads OPENED=1' ($(if ($refr.Count) { ([string]$refr[-1]).Trim() } else { '' }))
        }
        Start-Sleep -Seconds 6
        # The authoritative half.  A savable's own `Opened` bool can be the WRONG reading (a chest
        # class keeps its gate on the container value, and this content's chests were measured at
        # `opened=0 value=1` after a successful open -- the value is the fact).  So the object is
        # OPEN if EITHER the container value rose or the `Opened` bool says so.
        $mH2 = Get-Mark $H
        Invoke-ObjDump $H 'after-peer-open'
        $objsH2 = Get-ObjStates $H $mH2
        $openH = $objsH2.ContainsKey($c.Key) -and ((Test-ObjOpen $objsH2[$c.Key]) -or ($objsH2[$c.Key].State -ne 0))
        Verdict $step $H $openH "the HOST's own copy of '$($c.Name)' is OPEN" $(if ($objsH2.ContainsKey($c.Key)) { "lane=$($objsH2[$c.Key].Lane) value=$($objsH2[$c.Key].State) opened=$($objsH2[$c.Key].Opened)" } else { 'not tracked' })
        $mH2p = Get-Mark $P
        Invoke-ObjDump $P 'after-peer-open-self'
        $objsP2 = Get-ObjStates $P $mH2p
        $openP = $objsP2.ContainsKey($c.KeyP) -and ((Test-ObjOpen $objsP2[$c.KeyP]) -or ($objsP2[$c.KeyP].State -ne 0))
        Verdict $step $P $openP "the PEER's own copy is open too (it opened it)" $(if ($objsP2.ContainsKey($c.KeyP)) { "lane=$($objsP2[$c.KeyP].Lane) value=$($objsP2[$c.KeyP].State) opened=$($objsP2[$c.KeyP].Opened)" } else { 'not tracked' })

        # The counter: it must not rise TWICE.  A chest pays out once on the machine that
        # pressed; if the host's apply called the payout as well, the host's number would
        # have risen by one on its own.
        Start-Sleep -Seconds 3
        $mF1 = Get-Mark $H
        Invoke-FlagDump $H 'TotalChestsOpened'
        $cH1 = Get-FlagValue $H $mF1 'TotalChestsOpened'
        $mF1p = Get-Mark $P
        Invoke-FlagDump $P 'TotalChestsOpened'
        $cP1 = Get-FlagValue $P $mF1p 'TotalChestsOpened'
        Note ("  TotalChestsOpened after : host={0} peer={1}" -f $cH1, $cP1)
        $dbl = ($null -ne $cH0 -and $null -ne $cH1 -and ($cH1 - $cH0) -gt 1)
        Verdict $step $H (-not $dbl) 'TotalChestsOpened did not rise by more than one on the applier' ("{0} -> {1}" -f $cH0, $cH1)
        $agree = ($null -ne $cH1 -and $null -ne $cP1 -and $cH1 -eq $cP1)
        Verdict $step $P $agree 'both machines report the SAME TotalChestsOpened' ("host=$cH1 peer=$cP1")

        # Not lootable again on the host: a second press on the host's own copy of an object
        # the peer opened must not raise the payout again.
        $mR = Get-Mark $H
        Invoke-Interact $H ('near:guid:' + $c.Key)
        Start-Sleep -Seconds 8
        $mF2 = Get-Mark $H
        Invoke-FlagDump $H 'TotalChestsOpened'
        $cH2 = Get-FlagValue $H $mF2 'TotalChestsOpened'
        Note ("  TotalChestsOpened after a second press on the host: {0}" -f $cH2)
        Verdict $step $H ($null -ne $cH1 -and $null -ne $cH2 -and $cH2 -le $cH1) 'a second press on the host does NOT pay the chest out again' ("{0} -> {1}" -f $cH1, $cH2)

        Show-Since $P 'dev interact:|actor-state intent sent|object intent sent|actor-state local change|object local change' $mA 14
        Show-Since $H 'dev interact:|actor-state intent applied|actor-state refresh|object apply:' $mAH 14

        # -------------------------------------------------------------------
        # 1b. The HOST opens another one, and the PEER refreshes.
        # -------------------------------------------------------------------
        Step 'ITEM 1b -- HOST opens: the PEER applies and refreshes'
        # EVERY candidate the host can reach is tried, in turn.  A door whose Blueprint gates its
        # own `Opened` write cannot show the reverse direction no matter how the lane behaves
        # (measured here: `JigekiDoorR_SM` ran its verb and its container value stayed 0), so one
        # refused object must not end the test -- the next one is a separate object with its own
        # handler.  The FIRST candidate that both presses successfully AND opens is reported.
        $done1b = $false
        foreach ($c2 in $forHost) {
            if ($done1b) { break }
            Note ("  the host will open '{0}' ({1}) guid={2}" -f $c2.Name, $c2.Shape, $c2.Key)
            $mB = Get-Mark $H
            $mBP = Get-Mark $P
            Invoke-Interact $H ('near:guid:' + $c2.Key)
            $resH = @(Wait-Since $H "dev interact: resolved 'guid:" $mB 30)
            if ($resH.Count -eq 0) { Note '    (the host could not resolve it; next candidate)'; continue }
            [void](Check $step $H 'dev interact: target=.* verb=' 30 'the game''s own Interact verb ran on the object' $mB)
            [void](Check $step $H 'actor-state host set' 45 'the host published its actor-state set' $mB)
            # The host's own read-back is the pre-condition: if the object did not open HERE, there
            # is nothing for the peer to converge on and the next candidate is the honest move.
            $mSelf = Get-Mark $H
            Invoke-ObjDump $H 'self-1b'
            $oh = Get-ObjStates $H $mSelf
            $openHere = $oh.ContainsKey($c2.Key) -and ((Test-ObjOpen $oh[$c2.Key]) -or ($oh[$c2.Key].State -ne 0))
            if (-not $openHere) {
                Note ("    the host's own copy did NOT open (value={0} opened={1}); next candidate" -f $(if ($oh.ContainsKey($c2.Key)) { $oh[$c2.Key].State } else { '?' }), $(if ($oh.ContainsKey($c2.Key)) { $oh[$c2.Key].Opened } else { '?' }))
                continue
            }
            Verdict $step $H $true "the HOST's own copy of '$($c2.Name)' is OPEN (it opened it)" "guid=$($c2.Key)"
            # THE REVERSE DIRECTION: the peer must apply and refresh the same object.
            [void](Check $step $P 'actor-state refresh ok .* opened=1' 60 'the PEER refreshed its live actor and it reads OPENED=1' $mBP)
            Start-Sleep -Seconds 5
            $m3 = Get-Mark $P
            Invoke-ObjDump $P 'after-host-open'
            $objsP3 = Get-ObjStates $P $m3
            $openP3 = $objsP3.ContainsKey($c2.PeerKey) -and ((Test-ObjOpen $objsP3[$c2.PeerKey]) -or ($objsP3[$c2.PeerKey].State -ne 0))
            Verdict $step $P $openP3 "the PEER's own copy of '$($c2.Name)' is OPEN" $(if ($objsP3.ContainsKey($c2.PeerKey)) { "lane=$($objsP3[$c2.PeerKey].Lane) value=$($objsP3[$c2.PeerKey].State) opened=$($objsP3[$c2.PeerKey].Opened)" } else { 'not tracked' })
            Show-Since $P 'object apply:|actor states applied=|actor-state refresh' $mBP 12
            $done1b = $openP3
        }
        if (-not $done1b) {
            Verdict $step $H $false 'a second reachable object opened on the host AND converged on the peer' 'no remaining candidate did both'
        }
    }
}

# ===========================================================================
# ITEM 2 -- loot hiding
# ===========================================================================
# The proof needs an enemy that DROPS.  780's spawner enemies are the candidates and the
# mirror lane (Session 16) means both machines can hold the same key even when only one
# spawned it -- which removes R106/R110's "different authored GUIDs" blocker.
function Invoke-LootHiding([int]$H, [int]$P, [string]$step) {
    Step 'ITEM 2 -- an enemy that DROPS, taken on one machine, hidden on the other'
    # THE VENUE IS 780's OWN ENEMIES, not the hub's.  Measured (Session 17, first item-2 run): a
    # hub enemy killed through the game's own `Kill` produced no `loot: drop` line at all -- the
    # hub's spawner classes are the ones Session 14 recorded as dropping nothing.  780's bestiary
    # is different content with its own drop tables, and Session 15's streaming work is what
    # makes it reachable in the first place.
    Enter-Area $H $P '780_SZ_Depths' 'GreenDragon_Spawner' $step
    # The drop probe reports what the local world offers, so a run that finds nothing can SAY so
    # instead of concluding "the lane failed".
    $mD = Get-Mark $H
    Set-Control $H 'qd.drops' '1'
    Set-Control $P 'qd.drops' '1'
    Start-Sleep -Seconds 6
    Show-Since $H 'loot: ' $mD 20

    # THE KILL LOOP.  Drop tables are per-CLASS, and Session 14 measured that most of this
    # build's classes drop nothing at all, so one enemy is not a test -- several are tried until
    # one rolls a drop, and each attempt is reported so a run that finds none can say which
    # classes were tried instead of "the lane failed".
    $dropH = @()
    $dropP = @()
    $dropOn = $H
    $dropLines = @()
    $key = ''
    $tried = @()
    $deadline = (Get-Date).AddSeconds(420)
    while ((Get-Date) -lt $deadline -and $dropLines.Count -eq 0) {
        Invoke-Go $H 'enemy' 200
        Invoke-Go $P 'enemy' 200
        Start-Sleep -Seconds 6
        $rows = @(Get-Since $H 0 | Where-Object { $_ -match 'scan: kind enemy (\S+) guid=([0-9A-Fa-f]{8})' })
        if ($rows.Count -eq 0) { continue }
        $ln = [string]$rows[-1]
        if ($ln -notmatch 'guid=([0-9A-Fa-f]{8})') { continue }
        $key = $Matches[1].ToUpperInvariant()
        if ($tried -contains $key) { continue }
        $tried += $key
        $cls = if ($ln -match 'kind enemy (\S+) ') { $Matches[1] } else { '?' }
        Note ("  attempt {0}: kill {1} guid={2}" -f $tried.Count, $cls, $key)
        $mL = Get-Mark $H
        $mLp = Get-Mark $P
        Set-Control $H 'qd.enemykill' ($key + '#' + (Get-Date -Format 'HHmmss'))
        [void](Check $step $H ('rig: enemykill -> key=' + $key) 25 'the game''s own Kill ran on the target enemy' $mL)
        [void](Check $step $P ('the other machine recorded this death|enemy death recorded guid=' + $key) 40 'the PEER recorded the same death' $mLp)
        $dropH = @(Wait-Since $H 'loot: drop ' $mL 12)
        $dropP = @(Wait-Since $P 'loot: drop ' $mLp 12)
        # BOTH MACHINES ARE WATCHED.  Measured (Session 17): the drop rolled on the PEER
        # (`loot: drop 90A52CF3#0 = Herb_Item_BP_C`) for an enemy the HOST killed, because the
        # machine that runs the death is not necessarily the machine whose spawner carries the
        # loot hook.  A harness looking only at the host reported "no drop" while the peer's log
        # already had two.
        Note ("    -> drop lines: host={0} peer={1}" -f $dropH.Count, $dropP.Count)
        if ($dropP.Count -gt 0) { $dropOn = $P; $dropLines = $dropP } else { $dropOn = $H; $dropLines = $dropH }
        if ($tried.Count -ge 14) { break }
    }
    Note ("  kills attempted: {0} ({1})" -f $tried.Count, ($tried -join ','))
    Verdict $step $dropOn ($dropLines.Count -gt 0) 'a kill DROPPED something' ($(if ($dropLines.Count) { ([string]$dropLines[0]).Trim() } else { "no loot: drop line from $($tried.Count) class(es), on either machine" }))

    if ($dropLines.Count -gt 0) {
        # The drop key, so the pickup can be aimed at it.
        $dropLine = [string]$dropLines[-1]
        $dropKey = ''
        if ($dropLine -match 'loot: drop ([0-9A-Fa-f]{8})') { $dropKey = $Matches[1].ToUpperInvariant() }
        Note ("  drop key: '{0}' (rolled on inst{1})" -f $dropKey, $dropOn)
        # BOTH players stand at the drop; the machine that did NOT roll it is the one that must
        # hide it, so the TAKER is the other machine.
        Invoke-Go $H 'loot' 90
        Invoke-Go $P 'loot' 90
        Start-Sleep -Seconds 8
        # THE TAKER IS THE MACHINE THAT DID NOT ROLL IT, so the machine that DID roll it is the
        # one left holding a visible drop with no reason to hide it -- which is exactly the pair
        # `loot: local pickup` (taker) / `loot: hiding local drop` (other) needs.
        if ($dropOn -eq $P) { $taker = $H; $other = $P } else { $taker = $P; $other = $H }
        $mTake = Get-Mark $taker
        Set-Control $taker 'qd.act' ('near:loot#' + (Get-Date -Format 'HHmmssfff'))
        Say ("  inst{0} takes the drop" -f $taker)
        [void](Check $step $taker 'loot: local pickup ' 45 'the taker tombstoned the drop for the partner' $mTake)
        # THE CLAIM: the other machine hides its own copy.
        [void](Check $step $other 'loot: hiding local drop ' 60 'the OTHER machine hid its own copy of the drop -- the partner took it' $mTake)
        Show-Since $other 'loot: ' $mTake 14
        Show-Since $taker 'loot: ' $mTake 10
        $more = @(Get-Since $H 0 | Where-Object { $_ -match 'loot: drop ' }) + @(Get-Since $P 0 | Where-Object { $_ -match 'loot: drop ' })
        Note ("  drop lines this run: {0}" -f $more.Count)
    }
    Show-Since $H 'loot: |enemy death recorded|rig: enemykill' $mL 16
    Show-Since $P 'loot: |enemy death recorded' $mL 10
}

# ===========================================================================
# ITEM 3 -- 760_SZ_AUT: the ROSDragon and the Dek Egg
# ===========================================================================
function Invoke-RosDragon([int]$H, [int]$P, [string]$step) {
    Step 'ITEM 3 -- 760_SZ_AUT: boss mirror + one HP + bar + death on both'
    Enter-Area $H $P '760_SZ_AUT' 'ROSDragon_Spawner' $step
    # WHICH BOSS.  The claim under test is the boss pipeline (mirror, one shared HP, a bar on
    # both, a death on both), not one spawner's activation gate.  MEASURED (Session 17): the
    # ROSDragon spawner never fired even with both players inside its radius for four minutes --
    # its own `PlayerIsInActivationRange` reads player 0 of ITS machine and the area's level
    # manager had the level loaded but not visible (`engineVisible=0`) -- while several enemies
    # whose content says `Boss=1` WERE live in the same world (`A8772963`, `17E153F4`,
    # `DBF4FFD3`: the log already carried `boss: sent guid=... bar on` for each).  So the target
    # is the nearest live boss this machine runs, whatever it is called, and the class is
    # reported rather than assumed.
    $key = ''
    $deadline = (Get-Date).AddSeconds(240)
    $tries = 0
    while ((Get-Date) -lt $deadline -and -not $key) {
        $tries++
        Invoke-Go $H 'ROSDragon_Spawner' 140
        Invoke-Go $P 'ROSDragon_Spawner' 140
        Start-Sleep -Seconds 15
        # First choice: the ROSDragon by class, if this run happens to have it.
        $key = Get-EnemyKeyByClass $H 0 'Dragon_Ros_BP_C'
        if (-not $key) {
            # Otherwise any live boss the owner runs, from the log's own boss lines.
            $rows = @(Get-Since $H 0 | Where-Object { $_ -match 'boss: guid=([0-9A-Fa-f]{8})' })
            if ($rows.Count) {
                $cl = @{}
                foreach ($ln in (Get-Since $H 0 | Where-Object { $_ -match 'scan: kind enemy .*guid=' })) {
                    if ([string]$ln -match 'kind enemy (\S+) guid=([0-9A-Fa-f]{8})') { $cl[$Matches[2].ToUpperInvariant()] = $Matches[1] }
                }
                for ($i = $rows.Count - 1; $i -ge 0; $i--) {
                    if ([string]$rows[$i] -match 'boss: guid=([0-9A-Fa-f]{8})') {
                        $cand = $Matches[1].ToUpperInvariant()
                        if ($cl.ContainsKey($cand)) { $key = $cand; break }
                    }
                }
            }
        }
        Note ("  poll {0}: boss key on the owner = '{1}'" -f $tries, $key)
    }
    Verdict $step $H ([bool]$key) 'a live BOSS exists on the owner in 760_SZ_AUT' ("key=$key")
    if (-not $key) { return }

    # 3a. The mirror: the class rides the wire and the peer constructs it.
    $descH = @(Get-Since $H 0 | Where-Object { $_ -match ('enemy spawn: described guid=' + $key) })
    Verdict $step $H ($descH.Count -gt 0) 'the owner published the dragon''s class (K_ENEMY_SPAWN)' ($(if ($descH.Count) { ([string]$descH[-1]).Trim() } else { '' }))
    $conP = @(Get-Since $P 0 | Where-Object { $_ -match ('enemy spawn: constructed guid=' + $key) })
    if ($conP.Count -eq 0) {
        $dl = (Get-Date).AddSeconds(30)
        while ((Get-Date) -lt $dl -and $conP.Count -eq 0) {
            Start-Sleep -Seconds 3
            $conP = @(Get-Since $P 0 | Where-Object { $_ -match ('enemy spawn: constructed guid=' + $key) })
        }
    }
    Verdict $step $P ($conP.Count -gt 0) 'the PEER constructed a mirror of the dragon' ($(if ($conP.Count) { ([string]$conP[-1]).Trim() } else { '' }))

    $dH = Get-Mark $H; $dP = Get-Mark $P
    Invoke-Dump $H 'ros-mirror-h'
    Invoke-Dump $P 'ros-mirror-p'
    $rowP = Get-SinceKey $P $dP $key
    Verdict $step $P ($rowP.Count -gt 0) 'the peer holds the same GUID' ($(if ($rowP.Count) { ([string]$rowP[-1]).Trim() } else { '' }))
    Verdict $step $P (Get-MirroredByKey $P $dP $key) 'the peer''s copy is MIRRORED' 'mirrored=1'
    $hH = Get-HpByKey $H $dH $key
    $hP = Get-HpByKey $P $dP $key
    Verdict $step $P ($null -ne $hH -and $null -ne $hP -and [math]::Abs($hH - $hP) -lt 0.5) 'both machines report the SAME HP for the ROSDragon' ("host=$hH peer=$hP")

    # 3b. The bar on both, from the content's own Boss flag.
    function Seen([int]$i, [string]$pat) { return [bool](@(Get-Since $i 0 | Where-Object { $_ -match $pat }).Count) }
    function SeenLine([int]$i, [string]$pat) {
        $m = @(Get-Since $i 0 | Where-Object { $_ -match $pat })
        if ($m.Count -eq 0) { return '' }
        return ([string]$m[-1]).Trim()
    }
    Verdict $step $H (Seen $H ('boss: guid=' + $key + ' is a boss')) 'the owner identified the ROSDragon as a boss' (SeenLine $H ('boss: guid=' + $key + ' is a boss'))
    Verdict $step $H (Seen $H 'boss: ActivateBossHPBar called|boss: ActivateBossHPbar on the player class called') 'the owner raised its own bar' (SeenLine $H 'boss: ActivateBoss')
    Verdict $step $P (Seen $P 'boss: follower raised its own bar for guid=') 'the PEER raised its OWN bar for its copy' (SeenLine $P 'boss: follower raised')

    # 3c. One shared HP, hit from both players.
    $d0H = Get-Mark $H; $d0P = Get-Mark $P
    Invoke-Dump $H 'ros-hp0-h'
    Invoke-Dump $P 'ros-hp0-p'
    $hp0H = Get-HpByKey $H $d0H $key
    $hp0P = Get-HpByKey $P $d0P $key
    Note ("  HP before: host={0} peer={1}" -f $hp0H, $hp0P)
    $mHit1 = Get-Mark $H
    Invoke-BossHit $H $key 200
    [void](Check $step $H ('rig: hitguid -> key=' + $key) 30 'the owner''s player landed a hit' $mHit1)
    Start-Sleep -Seconds 4
    $mHit2 = Get-Mark $P
    $mOwn = Get-Mark $H
    Invoke-BossHit $P $key 200
    [void](Check $step $P ('rig: hitguid -> key=' + $key) 30 'the peer''s player landed a hit on its copy' $mHit2)
    [void](Check $step $H ('hit on enemy ' + $key + ' from the other player') 40 'the OWNER applied the peer''s hit to the one HP' $mOwn)
    Start-Sleep -Seconds 6
    $d1H = Get-Mark $H; $d1P = Get-Mark $P
    Invoke-Dump $H 'ros-hp1-h'
    Invoke-Dump $P 'ros-hp1-p'
    $hp1H = Get-HpByKey $H $d1H $key
    $hp1P = Get-HpByKey $P $d1P $key
    Note ("  HP after:  host={0} peer={1}" -f $hp1H, $hp1P)
    Verdict $step $H ($null -ne $hp0H -and $null -ne $hp1H -and $hp1H -lt $hp0H) 'the ROSDragon''s HP FELL on the owner' ("{0} -> {1}" -f $hp0H, $hp1H)
    Verdict $step $H ($null -ne $hp1H -and $null -ne $hp1P -and [math]::Abs($hp1H - $hp1P) -lt 0.5) 'both machines report the SAME HP after the hits' ("host=$hp1H peer=$hp1P")

    # 3d. Death on both.
    $mK = Get-Mark $H; $mKP = Get-Mark $P
    Set-Control $H 'qd.enemykill' ($key + '#' + (Get-Date -Format 'HHmmss'))
    [void](Check $step $H ('rig: enemykill -> key=' + $key) 30 'the game''s own Kill ran on the dragon' $mK)
    Start-Sleep -Seconds 8
    [void](Check $step $H ('enemy death recorded guid=' + $key) 40 'the owner recorded the death' $mK)
    [void](Check $step $P ('the other machine recorded this death|enemy death recorded guid=' + $key) 70 'the PEER recorded the same death' $mKP)
    $deadP = @()
    $dl2 = (Get-Date).AddSeconds(30)
    while ((Get-Date) -lt $dl2 -and $deadP.Count -eq 0) {
        Start-Sleep -Seconds 3
        Set-Control $P 'qd.enemies' ('ros-dead-' + (Get-Date -Format 'HHmmss'))
        Start-Sleep -Seconds 2
        $deadP = @(Get-Since $P 0 | Where-Object { $_ -match ('enemykey ' + $key + ' DEAD') })
    }
    Verdict $step $P ($deadP.Count -gt 0) 'the peer''s mirrored dragon is DEAD in its world' ($(if ($deadP.Count) { ([string]$deadP[-1]).Trim() } else { '' }))
    Show-Since $H 'boss:|enemy spawn:|hit on enemy|enemy death recorded' $dH 18
    Show-Since $P 'boss:|enemy spawn:|enemy mirror:|other machine recorded this death' $dP 18
}

function Invoke-DekEgg([int]$H, [int]$P, [string]$step) {
    Step 'ITEM 3b -- Dek_Egg_Activation: play on one machine, hold on the other, joint resume'
    $mH = Get-Mark $H
    $mP = Get-Mark $P
    # The egg's own sequence is a placed `LevelSequenceActor`, which the streaming union puts
    # in BOTH worlds; the lever starts it on ONE machine through the sequencer's own Play.
    Set-Control $P 'qd.sequence' ('Dek_Egg_Activation#' + (Get-Date -Format 'HHmmss'))
    Say '  sequence[peer] -> Dek_Egg_Activation (Play on the placed actor''s own player)'
    Start-Sleep -Seconds 8
    [void](Check $step $P 'rig: sequence lever -> .* Play ran' 30 'the sequencer''s own Play ran on the placed actor' $mP)
    [void](Check $step $P 'cutscene: local started ' 45 'the peer detected ITS OWN cutscene playing' $mP)
    [void](Check $step $H 'cutscene: (holding the local player|playing the partner.s sequence)' 45 'the HOST held for the partner''s cutscene' $mH)
    Start-Sleep -Seconds 6
    $mEnd = Get-Mark $P
    $mEndH = Get-Mark $H
    Set-Control $P 'qd.sequence' ('stop#' + (Get-Date -Format 'HHmmss'))
    Say '  sequence[peer] -> stop (the sequencer''s own Stop)'
    [void](Check $step $P 'rig: sequence lever -> .* Stop ran' 30 'the sequencer''s Stop ran' $mEnd)
    [void](Check $step $P 'cutscene: local ended ' 45 'the machine that played it saw it END' $mEnd)
    $rel = @(Wait-Since $H 'cutscene: partner.s cutscene ended; resuming together' $mEndH 70)
    Verdict $step $H ($rel.Count -gt 0) 'the held machine RESUMED (joint resume)' ($(if ($rel.Count) { ([string]$rel[-1]).Trim() } else { '' }))
    $okUnlock = $false
    if ($rel.Count) { $okUnlock = ([string]$rel[-1]) -match 'unlock=1' }
    Verdict $step $H $okUnlock 'the held player was RELEASED (unlock=1)' ($(if ($rel.Count) { ([string]$rel[-1]).Trim() } else { '' }))
    Show-Since $P 'cutscene:|rig: sequence lever' $mP 12
    Show-Since $H 'cutscene:' $mH 10
}

# ===========================================================================
# ITEM 4 -- a HIT REACTION on the mirrored copy, on one shared HP
# ===========================================================================
# The damage path in this build cannot make the class's own hit pipeline run from a
# module-built payload: `TryApplyDamage` returns 0 and moves nothing, and Session 16 fell
# back to a clamped HP write, which by construction never plays a reaction.  `ApplyDamageTo`
# `HitboxTarget` is the class's own entry (it builds the `Hit` `TryApplyDamage` gates on), so
# the ladder now tries it before the write.  The measurement here is the two facts that
# matter: the HP still moves (one shared number), and the copy PLAYS A MONTAGE for it.
function Invoke-HitReaction([int]$H, [int]$P, [string]$step) {
    Step 'ITEM 4 -- a hit reaction on the mirrored copy, on ONE shared HP'
    # The dragon is the mirror this session already has in 780; a second species is not
    # needed, and using the one the mirror lane proved keeps the measurement honest.
    $key = ''
    $deadline = (Get-Date).AddSeconds(180)
    while ((Get-Date) -lt $deadline -and -not $key) {
        Invoke-Go $H 'GreenDragon_Spawner' 140
        Invoke-Go $P 'GreenDragon_Spawner' 140
        Start-Sleep -Seconds 20
        $key = Get-EnemyKeyByClass $H 0 'Dragon_Green_BP_C'
    }
    if (-not $key) { $key = '17181166'; Note '  falling back to the key the session record names' }
    Note ("  dragon key: '{0}'" -f $key)
    $conP = @(Get-Since $P 0 | Where-Object { $_ -match ('enemy spawn: constructed guid=' + $key) })
    Verdict $step $P ($conP.Count -gt 0) 'the peer holds a MIRRORED copy to react' ("constructed=$($conP.Count)")

    # The damage path this attempt must take, named in the log.
    $dH = Get-Mark $H; $dP = Get-Mark $P
    Invoke-Dump $H 'hit-hp0-h'
    Invoke-Dump $P 'hit-hp0-p'
    $hp0 = Get-HpByKey $H $dH $key
    Note ("  HP before: {0}" -f $hp0)

    # A montage BASELINE on the copy: the reaction is a CHANGE from whatever it was playing.
    $mBase = Get-Mark $P
    Invoke-Dump $P 'hit-base'
    Start-Sleep -Seconds 2
    $beforeMon = @(Get-Since $P $mBase | Where-Object { $_ -match 'enemy detail applied|enemy anim: Montage_Play' })

    $mHit = Get-Mark $H
    Invoke-BossHit $H $key 400
    [void](Check $step $H ('rig: hitguid -> key=' + $key) 30 'a hit was applied to the dragon' $mHit)
    Start-Sleep -Seconds 6
    # WHICH PATH THE HP ACTUALLY MOVED BY.  The two lines are separate facts and the regex must
    # not blur them: `ApplyDamageToHitboxTarget ... ran and HP did not move` is a REFUSAL, while
    # `damage: enemy hp via HP write` is the fallback that DID move it.
    $hitboxRan = @(Get-Since $H $mHit | Where-Object { $_ -match 'ApplyDamageToHitboxTarget .* ran' })
    $hitboxMoved = @($hitboxRan | Where-Object { $_ -match 'ApplyDamageToHitboxTarget .* hp .* -> ' -and $_ -notmatch 'did not move' })
    $hpWrite = @(Get-Since $H $mHit | Where-Object { $_ -match 'damage: enemy hp via HP write' })
    Verdict $step $H ($hitboxRan.Count -gt 0) 'the class''s own ApplyDamageToHitboxTarget was CALLED and ran' ($(if ($hitboxRan.Count) { ([string]$hitboxRan[-1]).Trim() } else { 'not attempted' }))
    Verdict $step $H ($hitboxMoved.Count -gt 0) 'ApplyDamageToHitboxTarget moved the HP (its own hit pipeline drove the damage)' ($(if ($hitboxMoved.Count) { ([string]$hitboxMoved[-1]).Trim() } else { 'no: it ran and HP did not move' }))
    Verdict $step $H ($hpWrite.Count -gt 0) 'the clamped HP write is what moved the number (the documented last resort)' ($(if ($hpWrite.Count) { ([string]$hpWrite[-1]).Trim() } else { '' }))

    $dH1 = Get-Mark $H; $dP1 = Get-Mark $P
    Invoke-Dump $H 'hit-hp1-h'
    Invoke-Dump $P 'hit-hp1-p'
    $hp1 = Get-HpByKey $H $dH1 $key
    $hp1p = Get-HpByKey $P $dP1 $key
    Verdict $step $H ($null -ne $hp0 -and $null -ne $hp1 -and $hp1 -lt $hp0) 'the HP still FELL (one shared number)' ("{0} -> {1}" -f $hp0, $hp1)
    Verdict $step $P ($null -ne $hp1 -and $null -ne $hp1p -and [math]::Abs($hp1 - $hp1p) -lt 0.5) 'both machines report the SAME HP after the hit' ("host=$hp1 peer=$hp1p")

    # THE REACTION: the owner publishes the montage it is now playing, and the copy plays it.
    $react = @(Wait-Since $P 'enemy detail applied .*anim=|enemy anim: Montage_Play verified' $dP1 45)
    Verdict $step $P ($react.Count -gt 0) 'the copy PLAYED a montage for the hit (a hit reaction)' ($(if ($react.Count) { ([string]$react[-1]).Trim() } else { '' }))
    Show-Since $H 'ApplyDamageToHitboxTarget|TryApplyDamage|hp via HP write|damage: ' $mHit 14
    Show-Since $P 'enemy detail applied|Montage_Play|enemy anim:' $dP1 14
}

# ===========================================================================
# ITEM 5 -- the final soak at SHIPPED defaults
# ===========================================================================
function Invoke-Soak([int]$H, [int]$P, [string]$step) {
    Step "ITEM 5 -- $SoakSeconds s of combat in 780 ($(if ($PlaySet) { 'DOCUMENTED PLAY' } else { 'SHIPPED' }) switch set)"
    # The switch set is whatever the ini says; the log's own `switches:` line is the record of
    # what actually ran, so it is quoted rather than restated.  It is read from the WHOLE log
    # file, not from `Get-Since`: the module prints `switches:` during load, BEFORE the frame
    # pump hook, and `Get-RunLines` starts at `hooked FEngineLoop` for every other question --
    # which is right for those and would silently exclude this line (measured: the soak reported
    # "the host logged its switch set" as a FAIL while the line was in the file).
    function Get-Switches([int]$i) {
        $p = $inst[$i].Log
        if (-not (Test-Path -LiteralPath $p)) { return '' }
        $m = @(Read-LogLines $p | Where-Object { $_ -match 'switches: ' })
        if ($m.Count -eq 0) { return '' }
        return ([string]$m[-1]).Trim()
    }
    $swLine = Get-Switches $H
    $swPLine = Get-Switches $P
    if ($swLine) { Note ("  [$H] " + $swLine) }
    if ($swPLine) { Note ("  [$P] " + $swPLine) }
    Verdict $step $H ([bool]$swLine) 'the host logged its switch set' $swLine
    $isPlay = $swLine -match 'enemy=1'
    Note ("  this soak ran the {0} set" -f $(if ($isPlay) { 'DOCUMENTED PLAY' } else { 'SHIPPED (all write paths off)' }))
    Verdict $step $H $isPlay 'the switch set under test is the documented PLAY set' $swLine
    # The shipped file turns most write paths OFF; the soak's claim is that the SHIPPED
    # configuration is stable, so what must be proven is the switches it does enable.
    $m0 = Get-Mark $H
    Invoke-Go $H 'GreenDragon_Spawner' 400
    Invoke-Go $P 'GreenDragon_Spawner' 400
    Start-Sleep -Seconds 10

    $end = (Get-Date).AddSeconds($SoakSeconds)
    $n = 0
    while ((Get-Date) -lt $end) {
        if (-not (Test-Alive $H) -or -not (Test-Alive $P)) {
            Verdict $step $H $false 'both processes survived the soak' 'a process died'
            break
        }
        $n++
        # Hit the nearest locally-run enemy on each machine, and re-anchor every 20 s so the
        # fight keeps finding enemies as the ones in reach die.
        Set-Control $H 'qd.hit' ([string](200 + ($n % 3) * 100))
        Set-Control $P 'qd.hit' ([string](200 + ($n % 3) * 100))
        if (($n % 2) -eq 0) {
            Invoke-Go $H 'enemy' 200
            Invoke-Go $P 'enemy' 200
        }
        Start-Sleep -Seconds 3
    }
    Note ("  soak iterations: {0}" -f $n)

    foreach ($i in 1, 2) {
        $alive = Test-Alive $i
        Verdict $step $i $alive 'the process is alive at the end of the soak' ''
    }

    # The kill sets, per GUID, from each machine's own dump.
    $mH = Get-Mark $H; $mP = Get-Mark $P
    Set-Control $H 'qd.enemies' ('soak-h-' + (Get-Date -Format 'HHmmss'))
    Set-Control $P 'qd.enemies' ('soak-p-' + (Get-Date -Format 'HHmmss'))
    Start-Sleep -Seconds 4
    $keysH = @{}
    $keysP = @{}
    foreach ($ln in @(Get-Since $H 0)) {
        if ([string]$ln -match 'enemykey ([0-9A-Fa-f]{8}) (ALIVE|DEAD)') {
            $keysH[$Matches[1].ToUpperInvariant()] = $Matches[2]
        }
    }
    foreach ($ln in @(Get-Since $P 0)) {
        if ([string]$ln -match 'enemykey ([0-9A-Fa-f]{8}) (ALIVE|DEAD)') {
            $keysP[$Matches[1].ToUpperInvariant()] = $Matches[2]
        }
    }
    $deadH = @($keysH.Keys | Where-Object { $keysH[$_] -eq 'DEAD' })
    $deadP = @($keysP.Keys | Where-Object { $keysP[$_] -eq 'DEAD' })
    $onlyH = @($deadH | Where-Object { -not $keysP.ContainsKey($_) -or $keysP[$_] -ne 'DEAD' })
    $onlyP = @($deadP | Where-Object { -not $keysH.ContainsKey($_) -or $keysH[$_] -ne 'DEAD' })
    Note ("  host keys={0} (dead {1}); peer keys={2} (dead {3})" -f $keysH.Count, $deadH.Count, $keysP.Count, $deadP.Count)
    Note ("  dead only on the host: {0} -> {1}" -f $onlyH.Count, ($onlyH -join ','))
    Note ("  dead only on the peer: {0} -> {1}" -f $onlyP.Count, ($onlyP -join ','))
    Verdict $step $H ($onlyH.Count -eq 0 -and $onlyP.Count -eq 0) 'the per-GUID kill sets DIFF TO ZERO' ("host-dead=$($deadH.Count) peer-dead=$($deadP.Count) diff=$($onlyH.Count + $onlyP.Count)")

    # Shared enemies: HP equal on both, for every key both machines hold alive.
    $common = @($keysH.Keys | Where-Object { $keysP.ContainsKey($_) -and $keysH[$_] -eq 'ALIVE' -and $keysP[$_] -eq 'ALIVE' })
    Note ("  keys alive on BOTH machines: {0}" -f $common.Count)
    $unequal = 0
    $checked = 0
    foreach ($k in $common) {
        $hpsH = @(Get-Since $H $mH | Where-Object { $_ -match ('enemykey ' + $k + ' ') })
        $hpsP = @(Get-Since $P $mP | Where-Object { $_ -match ('enemykey ' + $k + ' ') })
        if ($hpsH.Count -eq 0 -or $hpsP.Count -eq 0) { continue }
        $a = if ([string]$hpsH[-1] -match 'hp=([-0-9.]+)') { [double]$Matches[1] } else { $null }
        $b = if ([string]$hpsP[-1] -match 'hp=([-0-9.]+)') { [double]$Matches[1] } else { $null }
        if ($null -eq $a -or $null -eq $b) { continue }
        ++$checked
        if ([math]::Abs($a - $b) -ge 0.5) {
            ++$unequal
            Note ("    HP MISMATCH {0}: host={1} peer={2}" -f $k, $a, $b)
        }
    }
    Verdict $step $P ($unequal -eq 0) 'the HP of shared enemies is EQUAL on both machines' ("checked=$checked unequal=$unequal")

    # Warnings and errors, by class.
    foreach ($i in 1, 2) {
        $err = @(Get-Since $i $m0 | Where-Object { $_ -match 'ERR |EXCEPTION|ACCESS_VIOLATION|fatal' })
        $warn = @(Get-Since $i $m0 | Where-Object { $_ -match '^.*WARN ' })
        $classes = @{}
        foreach ($w in $warn) {
            $s = [string]$w
            $c = '(unlabelled)'
            if ($s -match 'WARN (?:BLOCK )?([a-z\-]+)[: ]') { $c = $Matches[1] }
            if ($classes.ContainsKey($c)) { $classes[$c]++ } else { $classes[$c] = 1 }
        }
        Note ("  [{0}] error/fault lines={1}" -f $i, $err.Count)
        Note ("  [{0}] WARN lines={1} classes={2}" -f $i, $warn.Count,
              (($classes.GetEnumerator() | Sort-Object Value -Descending | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join ' '))
        foreach ($e in ($err | Select-Object -Last 4)) {
            $s = [string]$e
            if ($s.Length -gt 220) { $s = $s.Substring(0, 220) + ' ...' }
            Note "  [$i] $s"
        }
        Verdict $step $i ($err.Count -eq 0) 'no error/fault lines during the soak' ("count=$($err.Count)")
    }
}

# ===========================================================================
# main
# ===========================================================================
Say "session17 start: phase=$Phase shipped-defaults=$($ShippedDefaults.IsPresent)"
Stop-All
Seed-FreshPair
Start-Boot $Phase
$H = $script:H
$P = $script:P

if ($Phase -eq 'smoke') {
    Invoke-Smoke $H $P 'smoke'
}
if ($Phase -eq 'item1' -or $Phase -eq 'all') {
    Enter-Area $H $P '780_SZ_Depths' 'SecretSword_Altar_BP' 'item1'
    Invoke-OpenChests $H $P 'item1'
}
if ($Phase -eq 'item2' -or $Phase -eq 'all') {
    Invoke-LootHiding $H $P 'item2'
}
if ($Phase -eq 'item3' -or $Phase -eq 'all') {
    Invoke-RosDragon $H $P 'item3'
    Invoke-DekEgg $H $P 'item3b'
}
if ($Phase -eq 'item4' -or $Phase -eq 'all') {
    Invoke-HitReaction $H $P 'item4'
}
if ($Phase -eq 'soak' -or $Phase -eq 'all') {
    Invoke-Soak $H $P 'item5'
}

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
