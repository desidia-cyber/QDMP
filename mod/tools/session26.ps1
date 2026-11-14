# session26.ps1 -- Session 26: every boss, started by one player, fought by both.
#
# WHAT IS NEW FROM session25.ps1
# ------------------------------
# The `bosses` phase.  A boss fight begins when ONE player walks into a placed trigger (or a
# proximity boss spawner's own trap volume), and that volume answers only its own machine's
# player -- so until the trigger lane (K_TRIGGER) a fight started on one machine only.  For
# each boss the content places (table: $script:Bosses) the phase enters the boss's area with
# both machines, walks the HOST into the start volume with `qd.trigger`, and then measures,
# line by line: the PEER replayed the entry, both machines hold the SAME boss key alive, the
# shared bar is up, a hit from each player moves ONE HP, the death lands on both, both bars
# come down, and the death flag is in both machines' banks.  `-Only GreenDragon,ROSDragon`
# narrows it.  Everything below the header is session25.ps1's harness, unchanged in kind.
#
# session25.ps1 -- Session 25: the rig stays on screen, R129's take is measured in both
# directions, and a 1800 s soak that does not walk into a cutscene world.
#
# WHAT IS DIFFERENT FROM session24.ps1
# -----------------------------------
# 1. THE RIG.  User report: "spawns player in darkness, no map visible, or in empty space
#    where he just falls".  Session 24's own logs carry the defect as a number: 265 hub hops,
#    *18 of them ended more than 30 m below the target*, worst 259 m -- the rig had teleported
#    the pawn onto actors of a sublevel that is resident but NOT SHOWN (z -14047, -15755).
#    The build under test refuses such a target and arms a fall guard.  The `rig` phase drives
#    60+ hops per machine in the hub and counts, per hop: landed / refused-not-shown /
#    no-target, the pawn's z against the target's, and how many times the guard restored it.
#    It also screenshots both windows and measures their brightness, because "lit, with
#    geometry visible" is a claim about pixels.
# 2. THE TAKE (R129).  Session 24 measured that the game's own `Interact` on a drop sets
#    `Acquired` 0 -> 1 and grants the item, and that the pickup then waits for a dismiss press
#    before it hides.  The build under test tombstones on the Acquired flip, makes the
#    partner's copy untouchable, and presses Interact BEFORE the dismiss.  The `loot` phase
#    proves both directions and measures each machine's INVENTORY LINES, not just the actor.
# 3. THE HARNESS.  Four defects this session fixes in this file:
#    (a) the soak no longer does hard round trips to the cutscene worlds
#        (`997_FinalZoneCutscene` and friends are dark, empty cutscene maps, not gameplay);
#        they stay only in the dedicated `hardloop` phase, which is what they are for;
#    (b) every Stop-All logs WHY first -- the phase, the exception message, or the timeout.
#        Session 24 had "both processes gone in the same second, logs cut mid-line, no
#        fault", which is the shape of the harness' own cleanup after an error;
#    (c) WORLD DIVERGENCE is counted only OUTSIDE a travel transition (a +/- 20 s window
#        around a hard load or an area entry/leave), because inside one it is the transition,
#        not a desync -- Session 24's strict `count == 0` failed on 4 transient lines;
#    (d) shared HP is not sampled for 40 s after a hard load or an area change, while the
#        enemy tables are still rebuilding (Session 24: `shared-alive=0` then `enemykey n=28
#        alive=5` 40 s after a load).
#
# PHASES (one .ps1 invocation per phase; a launched game does not survive the command that
# started it -- R14)
#   probe     how long each machine's file-poll levers take (the number the rest waits on).
#   rig       the A proof: 60+ hub hops per machine, counted and screenshotted.
#   hardloop  N consecutive hard world loads, both machines alive and in the same world
#             after each one.  THIS is where the cutscene worlds live now.
#   loot      R129's take half, both directions, with the inventory lines counted.
#   smoke     the two-direction loot proof and two area changes.  No cutscene world.
#   soak      >= 1800 s of measured body: hub fighting with rig hops, two area changes
#             (780 inside the loot step, 770 and the leave), and two loot takes (one each
#             direction).  No hard travel: it is the hardloop phase's job, and every hard
#             load costs a world teardown this soak does not need to measure.
#   all       smoke then soak.
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
    [ValidateSet('probe', 'rig', 'hardloop', 'loot', 'smoke', 'soak', 'all', 'bosses', 'gaps', 'story')]
    [string]$Phase = 'all',
    [int]$BootSeconds = 300,
    [int]$SoakSeconds = 1800,
    [int]$HardLoads = 8,
    [int]$WarmSeconds = 0,
    [int]$LootRounds = 12,
    [int]$RigHops = 64,
    [string]$Report = '',
    [switch]$ShippedDefaults,
    [string]$Only = ''
)

$ErrorActionPreference = 'Stop'
$here    = $PSScriptRoot
$repo    = Split-Path -Parent (Split-Path -Parent $here)
$gameDir = 'G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64'
$relay   = Join-Path (Split-Path -Parent $here) 'rust\target\release\qd-relay.exe'
$rigBase = 'G:\Freespace\Queens_Domain\_test'
$rigRoot = Join-Path $rigBase ('s26-' + $Phase)
$seed    = Join-Path $repo 'analysis\save-backup-before-live'
$outDir  = Join-Path $rigRoot 'out'
if (-not $Report) { $Report = Join-Path $outDir ('report-' + $Phase + '.txt') }
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force -Path $outDir | Out-Null }

$script:lines = New-Object System.Collections.Generic.List[string]
# The run-log cache Get-RunLines keeps, keyed by machine index.  See its comment: this is
# what makes a 90 k-line end-of-run scan finish instead of stalling the soak's last step.
$script:runCache = @{}
trap {
    Write-Output ("UNHANDLED: " + $_.Exception.Message)
    Write-Output ("AT: " + $_.InvocationInfo.PositionMessage)
    $script:lines.Add("UNHANDLED: " + $_.Exception.Message) | Out-Null
    $script:lines.Add("AT: " + $_.InvocationInfo.PositionMessage) | Out-Null
    # (b) THE KILL HAS TO SAY WHY.  Session 24 recorded "both processes gone in the same
    # second, logs cut mid-line, no fault" -- which is exactly what this harness' own cleanup
    # looks like from the outside.  The reason is written BEFORE any process is stopped, and
    # it names the phase and the exception.
    try { Stop-All ("UNHANDLED exception in phase '" + $Phase + "': " + $_.Exception.Message) } catch {}
    try { $script:lines | Set-Content -LiteralPath $Report } catch {}
    exit 1
}
# NOTE: these write to the HOST, not to the pipeline, and that is not cosmetic.
#
# MEASURED this session: `$key = Find-SharedDrop ...` returned an ARRAY of the note strings
# instead of the key, because every `Note` call inside the function was emitted as function
# output -- the same trap session20.ps1 warns about ("Check writes its note to the pipeline,
# so [bool](Check ...) would be true").  The drop phase then probed 'near:drop::0' and the
# log said `no live actor matches 'drop::0'` about a drop it had just found.  A helper that
# both narrates and returns a value cannot afford a single stray object on the pipeline, so
# narration goes to the host stream and the report file (built from $script:lines) and the
# pipeline is reserved for return values.
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
    # CACHED (Session 22).  Session 21's end-of-run kill diff "did not finish" on a 92 k-line
    # log because every `Get-Since` re-read and re-split the whole file, and the soak made
    # hundreds of those calls.  The cache is keyed on the file LENGTH, so a log that grew
    # invalidates it and a log that did not is read once -- exact, not a timeout.
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
    # LINEAR APPEND.  `$out += $ln` is O(n) per append, so this function is O(n^2) -- and
    # Session 21's end-of-run scan "did not finish" on a 92 k-line log for exactly that
    # reason, twice: once for the log re-read (fixed by the cache) and once for the append
    # (fixed here).  A 90 k-line window is 4 billion copies with `+=`.
    $src = Get-RunLines $i
    $out = New-Object System.Collections.Generic.List[string]
    foreach ($ln in $src) {
        $t = Get-LineMs ([string]$ln)
        if ($t -ge 0 -and $t -ge $markMs) { $out.Add([string]$ln) }
    }
    return $out.ToArray()
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
function Invoke-GoDrop([int]$i, [string]$hash, [int]$index, [int]$near = 400, [int]$above = 0, [switch]$On) {
    $script:goSeq++
    $yaw = ($script:goSeq * 37) % 360
    Set-Control $i 'qd.near' ([string]$near)
    $spec = 'near:drop:' + $hash + ':' + $index
    if ($above -gt 0) { $spec += (':z' + [string]$above) }
    # :on is the only way to ask for offset 0: qd.near treats a non-positive value as "not
    # set" and substitutes its default, so every on-drop leg the old harness asked for stood
    # 140 cm away (measured nine times in one take: standing offset=140 move=teleport).
    if ($On) { $spec += ':on' }
    Set-Control $i 'qd.tp' ($spec + ',' + $yaw + '#' + (Get-Date -Format 'HHmmssfff'))
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

# ===========================================================================
# ONE RIG HOP, AND WHAT IT ACTUALLY DID
# ===========================================================================
# The report has to answer three questions per hop, so each hop is classified from its own
# log window, never from the lever's optimism:
#   landed     `dev teleport near '<kind>' -> ... at X,Y,Z ... standing at X,Y,Z`
#   refused    the target's level is not shown here (RigActorVisible refused it)
#   no-target  nothing visible matched at all
# The `#nonce` is required: the lever applies a file whose content CHANGED, so two identical
# hops in a row would otherwise be one hop (Session 22 measured exactly that with `qd.level`).
# After a landing, the pawn's own `scan: local at` (10 s cadence) is read, because "standing
# at z=<target>" is where the rig PUT it -- the question is where it ENDED UP.
function Invoke-Hop([int]$i, [string]$kind, [int]$nearCm = 200, [int]$answerSecs = 20) {
    $script:goSeq++
    $yaw = ($script:goSeq * 37) % 360
    $mark = Get-Mark $i
    Set-Control $i 'qd.near' ([string]$nearCm)
    Set-Control $i 'qd.tp' ('near:' + $kind + ',' + $yaw + '#' + (Get-Date -Format 'HHmmssfff'))
    $res = [pscustomobject]@{
        Inst = $i; Kind = $kind; Answer = 'no-answer'; Line = ''
        TargetZ = $null; PlacedZ = $null; PostZ = $null; Delta = $null; Restored = $false
    }
    $landPrefix = "dev teleport near '" + $kind + "' -> "
    $deadline = (Get-Date).AddSeconds($answerSecs)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 500
        foreach ($ln in @(Get-Since $i $mark)) {
            $s = [string]$ln
            if ($s -match 'level is not shown here; refused') { $res.Answer = 'refused'; $res.Line = $s.Trim(); break }
            if ($s.Contains($landPrefix) -and
                $s -match ' at (-?[0-9.]+),(-?[0-9.]+),(-?[0-9.]+) dist=') {
                $tz = [double]$Matches[3]
                $pz = $null
                if ($s -match 'standing at (-?[0-9.]+),(-?[0-9.]+),(-?[0-9.]+)') { $pz = [double]$Matches[3] }
                $res.TargetZ = $tz; $res.PlacedZ = $pz
                $res.Answer = $(if ($s -match 'FAILED') { 'failed' } else { 'landed' })
                $res.Line = $s.Trim(); break
            }
            if ($s -match 'dev teleport near: no live actor matches') { $res.Answer = 'no-target'; $res.Line = $s.Trim(); break }
        }
        if ($res.Answer -ne 'no-answer') { break }
        if (-not (Test-Alive $i)) { $res.Answer = 'dead'; break }
    }
    if ($res.Answer -eq 'landed' -or $res.Answer -eq 'failed') {
        $ansMs = Get-LineMs $res.Line
        $deadline = (Get-Date).AddSeconds(14)
        while ((Get-Date) -lt $deadline) {
            Start-Sleep -Milliseconds 600
            $found = $null
            foreach ($ln in @(Get-Since $i $mark)) {
                $s = [string]$ln
                if ($s -notmatch 'scan: local at (-?[0-9.]+),(-?[0-9.]+),(-?[0-9.]+)') { continue }
                if ($ansMs -lt 0 -or (Get-LineMs $s) -ge $ansMs) { $found = [double]$Matches[3] }
            }
            if ($null -ne $found) { $res.PostZ = $found; break }
            if (-not (Test-Alive $i)) { break }
        }
        if ($null -ne $res.TargetZ -and $null -ne $res.PostZ) { $res.Delta = $res.TargetZ - $res.PostZ }
        # Did the GUARD put the pawn back?  A restore is the fix working, not a fall: the
        # pawn ends up where it started, so its z is not a measurement of the target.
        $res.Restored = (@(Get-Since $i $mark | Where-Object { $_ -match 'rig: FELL after a rig move' }).Count -gt 0)
    }
    return $res
}

# The run-level rig counters: how often the guard restored the pawn, and how often a resolver
# refused because the level it found is not on screen.
function Get-RigCounters([int]$m0) {
    $fell = 0; $refused = 0; $landed = 0; $noTarget = 0; $cand = 0
    foreach ($i in 1, 2) {
        foreach ($ln in @(Get-Since $i $m0)) {
            $s = [string]$ln
            if ($s -match 'rig: FELL after a rig move') { $fell++ }
            if ($s -match 'is in a level that is not shown here; refused') { $refused++ }
            if ($s -match 'the target.s level is not shown here; refused') { $refused++ }
            if ($s -match 'candidate\(s\) refused -- their level is not shown here') { $cand++ }
            if ($s -match "dev teleport near '[^']+' -> ") { $landed++ }
            if ($s -match 'dev teleport near: no live actor matches') { $noTarget++ }
        }
    }
    return [pscustomobject]@{ Fell = $fell; Refused = $refused; RefusedCalls = $cand; Landed = $landed; NoTarget = $noTarget }
}


# (b) EVERY KILL SAYS WHY FIRST.  A Stop-All with no reason is how a run acquires an
# unexplained death: Session 24's "both processes gone in the same second, logs cut
# mid-line, 0 FAULT lines, no crash folder" is what this function looks like when it runs
# after an error.  So the reason is logged before anything is stopped, together with the
# phase, the pid, and the LAST LINE each machine wrote -- a process that is about to be
# killed has to leave its own last words in the report.
$script:stopCount = 0
function Stop-All([string]$Reason) {
    if (-not $Reason) { $Reason = 'NO REASON GIVEN (that is a harness defect, not a crash)' }
    $script:stopCount++
    Say ("STOP-ALL #{0} phase={1} reason={2}" -f $script:stopCount, $Phase, $Reason)
    foreach ($i in 1, 2) {
        $alive = $false
        if ($script:pids -and $script:pids.ContainsKey($i)) {
            $alive = [bool](Get-Process -Id $script:pids[$i] -ErrorAction SilentlyContinue)
        }
        if (-not $alive) { continue }
        $last = ''
        try {
            $l = @(Get-RunLines $i)
            if ($l.Count -gt 0) { $last = ([string]$l[-1]).Trim() }
        } catch { $last = '' }
        if (-not $last) {
            # Get-RunLines can come back empty if the file was mid-rewrite; a plain tail
            # read is a second, independent way to get the machine's last words.
            try {
                $tail = @(Get-Content -LiteralPath $inst[$i].Log -Tail 3 -ErrorAction Stop)
                if ($tail.Count -gt 0) { $last = (($tail | Where-Object { $_ }) -join ' | ') }
            } catch { $last = '(the log could not be read: ' + $_.Exception.Message + ')' }
        }
        if ($last.Length -gt 300) { $last = $last.Substring(0, 300) + ' ...' }
        Say ("  killing inst{0} pid={1}; its last log line was: {2}" -f $i, $script:pids[$i], $last)
    }
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
        # A debugger that has a dump open maps the installed PDB, so the copy can fail the
        # same way the DLL does.  Move the old one aside and keep going: a stale PDB beside a
        # new DLL is worse than none, because a dump's stack would name the wrong function.
        $dstPdb = Join-Path $gameDir 'winmm.pdb'
        try { Copy-Item -LiteralPath $pdb -Destination $dstPdb -Force }
        catch {
            $heldPdb = $dstPdb + '.held-' + (Get-Date -Format 'HHmmss')
            try { Rename-Item -LiteralPath $dstPdb -NewName (Split-Path -Leaf $heldPdb) -Force } catch {}
            Copy-Item -LiteralPath $pdb -Destination $dstPdb -Force
            Say "the old PDB was mapped; moved it aside and installed the new symbols"
        }
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
                   'FlagSync=1', 'InventorySync=1', 'ActorStateSync=1', 'ObjectSync=1', 'TriggerSync=1', 'BossPullIn=1',
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
    Stop-All ("boot: replacing any process left over from an earlier phase (starting the '$tag' boot)")
    Install-And-Configure
    foreach ($i in 1, 2) {
        foreach ($leaf in 'qd.tp','qd.act','qd.hit','qd.cheat','qd.cheatarg','qd.travel','qd.hard',
                          'qd.level','qd.list','qd.near','qd.drops','qd.menu','qd.efunc','qd.eattack',
                          'qd.use','qd.pdmg','qd.kill','qd.petrace','qd.enemies','qd.sequence',
                          'qd.enemykill','qd.hitguid','qd.obj','qd.flag','qd.gate','qd.take','qd.trigger','qd.selfhp','qd.nohit') {
            Set-Control $i $leaf $null
        }
        Set-Control $i 'qd.scan' '1'
        Set-Control $i 'qd.god' '400'
        Set-Control $i 'qd.near' '140'
        if (Test-Path -LiteralPath $inst[$i].Pid) { Remove-Item -LiteralPath $inst[$i].Pid -ErrorAction SilentlyContinue }
        $old = $inst[$i].Log
        if (Test-Path -LiteralPath $old) { Move-Item -LiteralPath $old -Destination ($old + '.prev') -Force }
    }
    $env:QDCOOP_AUTOLOAD = '0'
    # NOT the environment (Session 29): the mod reads the environment BEFORE the control file, so
    # this one line made every Invoke-Go distance 140 cm -- the 15 m / 25 m placements never
    # happened, and players were set down on ledges.  The default lives in qd.near instead.
    $env:QDCOOP_TEST_NEAR = $null
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
        Stop-All ("boot refused: no HOST/PEER pair was established within $BootSeconds s " +
                  "(phase '$Phase'; the timeout is the reason, not a crash)")
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

# WAIT FOR THE WORLD, NOT FOR A PAWN.  Measured on this phase's first run: `scan: local at`
# is printed on the TITLE SCREEN too (the save is still loading), so the old gameplay wait
# returned true with the world at `title_screen` and the first hops landed at 0,0,0.  A rig
# test has to start in the world the player plays in.
function Wait-Hub([int]$i, [int]$seconds = 300) {
    $deadline = (Get-Date).AddSeconds($seconds)
    $last = [pscustomobject]@{ World = ''; Area = ''; Partner = ''; Agree = '' }
    while ((Get-Date) -lt $deadline) {
        $last = Get-WorldState $i
        if ($last.World -and $last.World -ne 'title_screen') { return $last }
        if (-not (Test-Alive $i)) { return $last }
        Start-Sleep -Seconds 2
    }
    return $last
}

# ---------------------------------------------------------------------------
# The per-key enemy dump, parsed (Session 21: `why=` is the new field)
# ---------------------------------------------------------------------------
# A dump is triggered by REWRITING the control file (its content is the trigger), and the
# lines it prints are `enemykey <key> <state> local=<0|1> src=<n> hp=<v>|? [why=<reason>
# seen=<0|1> inworld=<0|1>] [mirrored=1] cls=<class>`.  Only the dump prints `enemykey`,
# so a mark-to-now window is exactly one dump -- which is why the soak can now sample it
# repeatedly instead of reading one end-of-run answer.
function Invoke-EnemyDump([int]$i, [string]$tag, [int]$timeoutSec = 60) {
    $mark = Get-Mark $i
    $t0 = Get-Date
    Set-Control $i 'qd.enemies' ('s24-' + $tag + '-' + (Get-Date -Format 'HHmmssfff'))
    # WAIT FOR THE LINES, NOT A FIXED SLEEP.  Measured this session (the `probe` phase): a
    # control-file lever answers in 0.5-1.1 s on both machines ONCE WARM, but the FIRST
    # enemy dump of a boot cost 33.1 s -- the per-class UFunction resolution that makes
    # `GetHP` readable is paid once, per class, by whichever dump happens first.  A fixed
    # 3 s read therefore lost the peer's first dump and the loot phase reported "no enemy
    # key alive on both machines" about two machines that each had hundreds.
    $deadline = (Get-Date).AddSeconds($timeoutSec)
    $rows = @()
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 700
        $rows = @(Get-Since $i $mark | Where-Object { $_ -match 'enemykey [0-9A-Fa-f]{8} (ALIVE|DEAD|GONE)' })
        if ($rows.Count -gt 0) { break }
    }
    $elapsed = [math]::Round(((Get-Date) - $t0).TotalSeconds, 1)
    if ($rows.Count -eq 0) {
        Note ("    [{0}] WARN: no enemykey dump within {1}s of the trigger '{2}'" -f $i, $timeoutSec, $tag)
    } elseif ($elapsed -gt 5) {
        Note ("    [{0}] (the dump for '{1}' took {2}s -- first-use cost)" -f $i, $tag, $elapsed)
    }
    $out = @{}
    foreach ($ln in $rows) {
        $s = [string]$ln
        if ($s -notmatch 'enemykey ([0-9A-Fa-f]{8}) (ALIVE|DEAD|GONE)') { continue }
        $k = $Matches[1].ToUpperInvariant(); $st = $Matches[2]
        $hp = $null
        if ($s -match '\shp=([-0-9.]+)') { $hp = [double]$Matches[1] }
        $why = ''
        if ($s -match ' why=(\w+)') { $why = $Matches[1] }
        $seen = ''; if ($s -match ' seen=(\d)') { $seen = $Matches[1] }
        $iw = ''; if ($s -match ' inworld=(\d)') { $iw = $Matches[1] }
        $cls = ''; if ($s -match ' cls=(\S+)') { $cls = $Matches[1] }
        # Session 23: `mx` (the copy's own MaxHP) and `peers` (how many OTHER records answer
        # for this key here) are what make a duplicate visible.  Two bodies for one key with
        # different MaxHP sit at the same fraction, so they cannot converge and their absolute
        # HP is permanently different -- that is not a desync, and the comparison must skip it.
        $mx = $null; if ($s -match '\smx=([-0-9.]+)') { $mx = [double]$Matches[1] }
        $peers = 0;  if ($s -match '\speers=(\d+)')  { $peers = [int]$Matches[1] }
        # THE FULL KEY WHEN THE DUMP GIVES ONE (Session 23, R154).  `enemykey <8hex>` is the
        # LOW WORD of a 128-bit key; two different keys can share it, and one measured pair
        # under `55E0EA46` was a 250-HP Boner_Black_Berserker_BP_C on one machine and a 440-HP
        # one on the other.  `id=` is the whole key, so a comparison keyed on it cannot be a
        # collision.
        $id = $k
        if ($s -match '\sid=([0-9A-Fa-f]{32})') { $id = $Matches[1].ToUpperInvariant() }
        $out[$id] = [pscustomobject]@{
            Id = $id;
            Key = $k; State = $st; Hp = $hp; Why = $why; Seen = $seen; InWorld = $iw
            Cls = $cls; Mx = $mx; Peers = $peers; Line = $s.Trim()
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
        Unreadable = 0; Ghosts = 0; Duplicates = 0; DupKeys = @(); MxDiffer = 0; MxKeys = @(); Whys = @{}; Mismatches = @()
    }
    foreach ($k in @($dh.Keys)) {
        if (-not $dp.ContainsKey($k)) { continue }
        # GONE (the engine has marked the copy for destruction) is neither alive nor dead:
        # it has no HP to compare and it is not a death, so it is counted and skipped.
        if ($dh[$k].State -eq 'GONE' -or $dp[$k].State -eq 'GONE') { $out.Ghosts++; continue }
        if ($dh[$k].State -ne 'ALIVE' -or $dp[$k].State -ne 'ALIVE') { continue }
        $out.AliveBoth++
        # A KEY WITH MORE THAN ONE BODY IS NOT COMPARABLE (Session 23, R151).  `peers` is the
        # module's own count of OTHER records answering for the key, so this is the module's
        # answer, not a guess made here: two bodies with one key and different MaxHP cannot
        # converge (the convergence rule compares fractions) and their absolute difference is
        # permanent.  Counting that as a mismatch is what made Session 22 report a standing
        # `530F7FD0: host=140 peer=30` about one enemy that never disagreed with itself.
        if (($dh[$k].Peers -gt 0) -or ($dp[$k].Peers -gt 0)) {
            # A DIFFERENT MAX IS A DIFFERENT BODY (Session 23, R154).  Same key, same class,
            # `mx=250` here and `440` there, both at full HP: the key does not name one enemy
            # across the two machines, so their absolute HP is not a shared quantity.  Counted
            # and named, never counted as unequal -- that would be a false desync.
            if ($null -ne $dh[$k].Mx -and $null -ne $dp[$k].Mx -and
                [math]::Abs($dh[$k].Mx - $dp[$k].Mx) -ge 0.5) {
                $out.MxDiffer++
                if ($out.MxKeys -notcontains $k) { $out.MxKeys += $k }
                continue
            }
            $out.Duplicates++
            if ($out.DupKeys -notcontains $k) { $out.DupKeys += $k }
            continue
        }
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
# INVENTORY LINES -- the "the item was granted exactly once" half of R129
# ===========================================================================
# The lane is asymmetric and that is the whole point: the HOST owns the inventory and logs
# `inventory host set <n> entries ... rev=<r>` when its own snapshot CHANGES; the PEER logs
# `item intent queued <path> x<n>` when ITS OWN inventory gains something (that intent is what
# the host then applies) and `inventory grant applied: <path> +<n>` when the host's body adds
# an item to it.  So "A's inventory changes once" is measured on the machine that took the
# drop, and "B did not change" is measured on B's own lines -- not inferred from the actor.
function Get-InvEvents([int]$i, [int]$mark) {
    $gain = @(Get-Since $i $mark | Where-Object {
        $_ -match 'inventory host set |item intent queued |inventory grant applied' })
    $bad = @(Get-Since $i $mark | Where-Object {
        $_ -match 'inventory grant duplicate|inventory grant superseded|inventory consume duplicate|inventory consume superseded|inventory grant failed|inventory consume failed' })
    $consume = @(Get-Since $i $mark | Where-Object { $_ -match 'inventory consume applied' })
    return [pscustomobject]@{ Gain = $gain; Bad = $bad; Consumed = $consume }
}

# ===========================================================================
# (c) WORLD DIVERGENCE, COUNTED OUTSIDE A TRAVEL TRANSITION
# ===========================================================================
# Session 24's end check was a strict `count == 0` and it FAILED on four transient lines, one
# per hard world load, each of which resolved by itself: `local map 997_FinalZoneCutscene,
# partner map Ruins_Level_00` is the window between one machine loading and the other
# arriving.  That is the transition, not a desync.  So a divergence is counted only when it
# is NOT within +/- 20 s of a travel event on either machine.
$script:travelPattern = 'travel: HARD transition to |travel: hard transition: loading |' +
    'travel: both machines arrived in |travel: entered the partner.s area |' +
    'travel: left the area |travel: the partner left |travel: area announcement |' +
    'rig: hard lever -> .*streamed=1'
function Get-TravelMarks([int]$m0) {
    $marks = New-Object System.Collections.Generic.List[int]
    foreach ($i in 1, 2) {
        foreach ($ln in @(Get-Since $i $m0)) {
            $s = [string]$ln
            if ($s -notmatch $script:travelPattern) { continue }
            $t = Get-LineMs $s
            if ($t -ge 0) { $marks.Add($t) }
        }
    }
    return $marks
}
function Get-DivergenceReport([int]$m0, $travelMarks, [int]$windowMs = 20000) {
    $all = 0; $excluded = 0
    $kept = New-Object System.Collections.Generic.List[string]
    foreach ($i in 1, 2) {
        foreach ($ln in @(Get-Since $i $m0)) {
            $s = [string]$ln
            if ($s -notmatch 'WORLD DIVERGENCE') { continue }
            $all++
            $t = Get-LineMs $s
            $near = $false
            if ($t -ge 0) {
                foreach ($tm in $travelMarks) {
                    if ([math]::Abs($t - $tm) -le $windowMs) { $near = $true; break }
                }
            }
            if ($near) { $excluded++ } else { $kept.Add($s.Trim()) }
        }
    }
    return [pscustomobject]@{ All = $all; InTransition = $excluded; Kept = @($kept) }
}

# ===========================================================================
# (d) THE HP SAMPLING GATE
# ===========================================================================
# Session 24 measured the reason for this: `hub-final`'s first samples reported
# `shared-alive=0` because the peer's table had 5 live keys 40 s after a world load
# (`enemykey n=28 alive=5`) while the host had many.  A sample taken while the tables are
# rebuilding measures the rebuild, not the world, so sampling is suppressed for 40 s after a
# hard load or an area change and the skip is reported rather than silently dropped.
$script:hpGateUntil = [datetime]::MinValue
$script:hpSkips = New-Object System.Collections.Generic.List[object]
function Set-HpGate([string]$why, [int]$seconds = 40) {
    $script:hpGateUntil = (Get-Date).AddSeconds($seconds)
    Note ("  HP sampling gated for {0} s: {1}" -f $seconds, $why)
}
function Test-HpGate { return ((Get-Date) -lt $script:hpGateUntil) }

# ===========================================================================
# SCREENSHOTS -- "the scene must be lit, with geometry visible" is a claim about pixels
# ===========================================================================
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
        return [pscustomobject]@{
            W = $bmp.Width; H = $bmp.Height; N = $vals.Count
            Mean = [math]::Round($mean, 1); Std = [math]::Round($sd, 1)
        }
    } finally { $bmp.Dispose() }
}
$script:shots = New-Object System.Collections.Generic.List[object]
function Save-Shots([string]$tag) {
    $dir = Join-Path $rigRoot 'shots'
    if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
    foreach ($i in 1, 2) {
        if (-not $script:pids -or -not $script:pids.ContainsKey($i)) { continue }
        if (-not (Test-Alive $i)) { continue }
        $out = Join-Path $dir ("s25-{0}-{1}-inst{2}.png" -f $Phase, $tag, $i)
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
    # Disk discipline (drive C: ran out of space this week): at most the 8 newest files.
    $keep = @(Get-ChildItem -LiteralPath $dir -Filter '*.png' -ErrorAction SilentlyContinue |
              Sort-Object LastWriteTime -Descending)
    if ($keep.Count -gt 8) {
        $keep | Select-Object -Skip 8 | ForEach-Object {
            Remove-Item -LiteralPath $_.FullName -Force -ErrorAction SilentlyContinue
        }
    }
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
function Test-DropHeld([int]$i, [string]$hash, [int]$index, [int]$timeoutSec = 20) {
    $mark = Get-Mark $i
    Invoke-GoDrop $i $hash $index 400
    $held = @(); $gone = @()
    $deadline = (Get-Date).AddSeconds($timeoutSec)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 700
        $held = @(Get-Since $i $mark | Where-Object { $_ -match ('rig: drop ' + $hash + '#' + $index + ' resolved to a live actor') })
        if ($held.Count -gt 0) { break }
        $gone = @(Get-Since $i $mark | Where-Object { $_ -match ('rig: drop ' + $hash + '#' + $index + ' is already gone') })
        if ($gone.Count -gt 0) { break }
    }
    return [pscustomobject]@{
        Held = ($held.Count -gt 0); Gone = ($gone.Count -gt 0)
        Line = $(if ($held.Count) { ([string]$held[-1]).Trim() } elseif ($gone.Count) { ([string]$gone[-1]).Trim() } else { '' })
    }
}

# Stand ON the drop (a pickup is an overlap, not a verb) and then run the game's own
# Interact input event on that exact actor as the fallback the interaction lever documents.
function Invoke-TakeDrop([int]$i, [string]$hash, [int]$index) {
    # A PICKUP ANSWERS ITS OWN INPUT EVENT (Session 24).
    #
    # Session 23 tried six mechanisms and every one was refused: a teleport into the volume,
    # three out-and-back teleports, a fall from 3 m, the class' `Interact`/`Pickup` verbs, a
    # real swept move through the volume, and a direct `ReceiveActorBeginOverlap`.  What it
    # also did, at the end, was dump the drop's class chain -- and the answer is on that list:
    # `ItemPickup_Base_C` declares `InpActEvt_Interact_K2Node_InputActionEvent` ITSELF, with
    # `InpActEvt_Attack`/`Jump`/`Pause`, `PickupItemAnim__AllowInput__EventFunc` and the
    # `DoItemPickupPause`/`EndItemPickupPause` pair.  The pickup takes input WHILE its own
    # flow plays, so the entry is on the PICKUP; the act lever was pressing the PLAYER's
    # compiled event, which is a different Blueprint and consumes nothing.
    #
    # THE ORDER IS THE FIX (b26dec7).  Session 24's log, read again: the game's own `Interact`
    # on the drop sets `Acquired` 0 -> 1 and the item is GRANTED ~20 ms later, and the pickup's
    # showcase then waits for a dismiss press before `PutAway` hides the actor.  Every earlier
    # attempt pressed the showcase's dismiss input BEFORE Interact, i.e. against a pickup that
    # had nothing to dismiss.  The lever now presses `Interact` first and the pickup's own
    # dismiss events (`InpActEvt_Interact`/`Attack`/`Jump`) after it, one per 1.5 s stage.
    # What is waited on is the LANE's own line and the lever's own verdict, never optimism.
    $key = $hash + '#' + [string]$index
    $mark = Get-Mark $i
    Set-Control $i 'qd.take' ('drop:' + $hash + ':' + [string]$index + '#' + (Get-Date -Format 'HHmmssfff'))
    $pick = @(Wait-Since $i ('loot: local pickup ' + $key) $mark 30)
    $taken = @(Wait-Since $i 'take: .*TAKEN|take: THE GAME CONSUMED THE DROP' $mark 15)
    $stage = @(Get-Since $i $mark | Where-Object { $_ -match 'take: ' })
    $ok = (($pick.Count + $taken.Count) -gt 0)
    if ($ok) {
        Say ("  act[{0}] -> {1} (the game TOOK it)" -f $i, $key)
    } else {
        Say ("  act[{0}] -> near:drop:{1}:{2} (nothing consumed it)" -f $i, $hash, $index)
    }
    foreach ($s in ($stage | Select-Object -Last 8)) { Say ("      " + ([string]$s).Trim()) }
    if (-not $ok) {
        # The class surface stays on the record for the next session: read, not guessed.
        Set-Control $i 'qd.efunc' ('drop:' + $hash + ':' + [string]$index)
        Start-Sleep -Seconds 3
    }
    if ($ok) {
        # THE TAKEN ACTOR LEAVES (after Session 26): the lever's stage 2 runs the pickup's own
        # PutAway (the showcase dismiss); the loot lane's 15 s safety net runs it otherwise.
        $gone = @(Wait-Since $i 'take: THE GAME CONSUMED THE DROP|loot: taken drop .*(PutAway ran|is gone from this screen)' $mark 30)
        Verdict 'loot' $i ([bool]($gone.Count -gt 0)) "the taken drop $key left the TAKER's screen" $(if ($gone.Count) { ([string]$gone[-1]).Trim() } else { 'the actor still stands' })
    }
    Set-Control $i 'qd.take' $null
    $line = ''
    if ($pick.Count -gt 0) { $line = ([string]$pick[-1]).Trim() }
    elseif ($taken.Count -gt 0) { $line = ([string]$taken[-1]).Trim() }
    return [pscustomobject]@{ Ok = $ok; Line = $line; Pickup = @($pick); Taken = @($taken); Stages = @($stage) }
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
        # Probe BOTH machines for every key: a key only one of them holds is the state the
        # phase has to report, and skipping the second probe hides which side is missing.
        $rh = Test-DropHeld $H $k.Hash $k.Index
        $rp = Test-DropHeld $P $k.Hash $k.Index
        if ($rh.Held -and $rp.Held) {
            Note ("    key {0}: BOTH machines hold it" -f $id)
            return $k
        }
        Note ("    key {0}: host={1} peer={2} ({3})" -f $id,
              $(if ($rh.Held) { 'HELD' } elseif ($rh.Gone) { 'gone' } else { 'absent' }),
              $(if ($rp.Held) { 'HELD' } elseif ($rp.Gone) { 'gone' } else { 'absent' }),
              $(if ($rh.Line) { $rh.Line } else { $rp.Line }))
    }
    return $null
}

# Roll more drops by killing: the drop is the dying enemy's own Blueprint's, so both
# machines roll it locally and hold a copy -- which is exactly the precondition hiding
# needs.
#
# WHY `qd.enemykill` AND NOT `qd.hit`.  Measured (Session 14, recorded in the lever's own
# comment): 123 `qd.hit` swings over 120 s in the hub produced `ran=0 hp 0.0 -> 0.0` every
# time -- most hub enemy classes have no `TryApplyDamage` this module can build a frame
# for -- so there was no death and therefore no drop.  The first loot attempt of this
# session reproduced it exactly:
#
#     rig: hit enemy guid=A8D5C9E1 amount=400 ran=0 hp 0.0 -> 0.0 (local-run)
#
# `qd.enemykill <8hex>` calls the enemy's OWN `Kill`/`Die`, which runs the game's death
# path, and the drop roll is part of that path.  It is applied on BOTH machines so each
# copy dies through its own Blueprint and rolls its own local drop under the same key --
# which is what the hiding half needs to have anything to hide.
function Invoke-KillRounds([int]$H, [int]$P, [int]$rounds, [int]$mark) {
    for ($r = 1; $r -le $rounds; $r++) {
        # THE DAMAGE PATH FIRST.  Measured this session in the hub: `qd.enemykill` (the
        # enemy's own Kill) runs the death but NOT the drop roll on the machine that calls
        # it -- the drop always appeared on the OTHER machine, whose copy died through the
        # mod's damage convergence.  So a kill that rolls a drop HERE has to be a real
        # damage hit (`qd.hit`), which needs a class that implements TryApplyDamage: the
        # hub's mostly do not (`ran=0 hp 0.0 -> 0.0`, Session 14), 780_SZ_Depths' do.
        Note ("    round {0}: hitting the nearest enemy on both machines (damage, the path the drop roll runs on)" -f $r)
        # STAND BACK BEFORE THE BLOW.  Twice measured this session: the drop actor appears
        # only on the machine that did NOT land the killing blow (4 drops from a host kill
        # all on the peer; 3 hub drops from a peer kill all on the host).  The obvious
        # explanation is that the killer's pawn is standing 200 cm from the corpse, the drop
        # spawns at its feet, and the killer's OWN overlap consumes it before the 1/30 s
        # scan slice ever sees the actor -- so the killer never holds the drop it caused.
        # A pickup is an overlap: at 9 m there is nothing to overlap, and the drop has to
        # wait for whoever walks over it.
        Invoke-Go $H 'enemy' 900
        Invoke-Go $P 'enemy' 900
        Start-Sleep -Seconds 6
        Set-Control $H 'qd.hit' '100000'
        Set-Control $P 'qd.hit' '100000'
        Start-Sleep -Seconds 7
        Set-Control $H 'qd.hit' $null
        Set-Control $P 'qd.hit' $null
        Start-Sleep -Seconds 5
        $all = @(Get-DropKeys $H $mark) + @(Get-DropKeys $P $mark)
        $uniq = @{}
        foreach ($k in $all) { $uniq["$($k.Hash)#$($k.Index)"] = $true }
        Note ("    after round {0}: {1} distinct drop key(s); loot: drop lines host={2} peer={3}; merged host={4} peer={5}" -f
              $r, $uniq.Count,
              @(Get-Since $H $mark | Where-Object { $_ -match 'loot: drop ' }).Count,
              @(Get-Since $P $mark | Where-Object { $_ -match 'loot: drop ' }).Count,
              @(Get-Since $H $mark | Where-Object { $_ -match 'loot: merged ' }).Count,
              @(Get-Since $P $mark | Where-Object { $_ -match 'loot: merged ' }).Count)
        if ($uniq.Count -ge 2) { return $uniq.Count }
        Invoke-Go $H 'enemy' 200
        Invoke-Go $P 'enemy' 200
        Start-Sleep -Seconds 4
    }
    # The `Kill` fallback, for a class whose TryApplyDamage this module cannot build a frame
    # for.  It rolls the drop on the OTHER machine, which is still a drop both machines can
    # end up holding once that machine's copy dies.
    for ($r = 1; $r -le 2; $r++) {
        $dh = Invoke-EnemyDump $H ('kill{0}-h' -f $r)
        $hostKeys = @($dh.Keys | Where-Object { $dh[$_].State -eq 'ALIVE' } | Select-Object -First 3)
        if ($hostKeys.Count -eq 0) { break }
        foreach ($k in $hostKeys) {
            Set-Control $H 'qd.enemykill' $k
            Note ("      enemykill {0} on the HOST (fallback for a class with no usable TryApplyDamage)" -f $k)
            Start-Sleep -Seconds 5
        }
        $all = @(Get-DropKeys $H $mark) + @(Get-DropKeys $P $mark)
        $uniq = @{}
        foreach ($k in $all) { $uniq["$($k.Hash)#$($k.Index)"] = $true }
        Note ("    fallback round {0}: {1} distinct drop key(s)" -f $r, $uniq.Count)
        if ($uniq.Count -gt 0) { return $uniq.Count }
    }
    return 0
}

function Invoke-LootDirection([int]$H, [int]$P, [int]$taker, [int]$other, [string]$step, [string]$dir, $skip, [int]$mark) {
    Step ("R129 {0} -- the {1} takes a drop both machines hold; the {2} must hide its copy" -f
          $dir, $(if ($taker -eq $H) { 'HOST' } else { 'PEER' }), $(if ($other -eq $H) { 'HOST' } else { 'PEER' })) `
         'The tombstone is the lane''s own key {source_hash, drop_index}; the lever that addresses it is qd.tp near:drop:<8hex>:<index>.'
    $key = $null
    for ($attempt = 1; $attempt -le 3 -and -not $key; $attempt++) {
        $key = Find-SharedDrop $H $P $skip $mark
        if ($key) { break }
        # A drop only ONE machine holds is the normal first state: the machine that did not
        # do the Kill rolls its own copy when the death reaches it, and its object scan needs
        # a pass to notice the actor.  So wait, look again, and only then kill more.
        Note ("  attempt {0}: no drop both machines hold yet; waiting 10 s for the death to reach the other copy" -f $attempt)
        Start-Sleep -Seconds 10
        $key = Find-SharedDrop $H $P $skip $mark
        if ($key) { break }
        if ($attempt -lt 3) { Invoke-KillRounds $H $P 2 $mark }
    }
    if (-not $key) {
        Verdict $step $H $false 'a drop BOTH machines hold was found' 'no shared drop key exists; hiding cannot be measured'
        return $null
    }
    $id = "$($key.Hash)#$($key.Index)"
    Note ("  the drop under test is {0}" -f $id)

    $mTake = Get-Mark $taker
    $mOther = Get-Mark $other
    $take = Invoke-TakeDrop $taker $key.Hash $key.Index
    Verdict $step $taker $take.Ok 'the taker TOOK the drop (Acquired 0->1, the item granted, the lane tombstoned it)' $take.Line
    if (-not $take.Ok) {
        Show-Since $taker 'loot:|dev interact|rig: drop|rig: near|take:' $mTake 20
        return $id
    }
    # ---- the inventory half, on the machine that took it -------------------
    # MEASURED on A's own lines, in A's own take window: a take is a grant, and a grant is
    # exactly one of `inventory host set` (A is the host: its snapshot changed) or
    # `item intent queued` (A is the peer: its inventory gained and the intent carries it).
    $invT = Get-InvEvents $taker $mTake
    foreach ($s in @($invT.Gain | Select-Object -First 4)) { Note ("    taker inventory: " + ([string]$s).Trim()) }
    Verdict $step $taker ($invT.Gain.Count -eq 1) "the taker's INVENTORY changed exactly once" `
            ("gain lines={0}; duplicates/refusals={1}" -f $invT.Gain.Count, $invT.Bad.Count)
    Verdict $step $taker ($invT.Bad.Count -eq 0) 'the grant was not duplicated and not refused' `
            ("bad lines={0}" -f $invT.Bad.Count)


    # @(...) IS NOT COSMETIC: Wait-Since returns a SCALAR when it has one match, and
    # `$scalar[-1]` indexes CHARACTERS, so the report read "the OTHER machine hid its own
    # copy -- t" (the last letter of the line) in the first live run of this phase.
    $hide = @(Wait-Since $other ('loot: hiding local drop ' + $key.Hash + '#' + [string]$key.Index) $mOther 40)
    Verdict $step $other ($hide.Count -gt 0) 'the OTHER machine hid its own copy (the measured hiding half)' $(if ($hide.Count) { ([string]$hide[-1]).Trim() } else { '' })
    if ($hide.Count -eq 0) {
        Show-Since $other 'loot:|rig: drop|rig: near|dev interact' $mOther 20
    }
    # The hiding half must be a HIDE, not a disappearance that happened anyway: the same
    # machine still resolves the key and now says it is gone.
    $after = Test-DropHeld $other $key.Hash $key.Index
    Verdict $step $other $after.Gone 'the other machine''s copy is GONE to its own resolver' $after.Line

    # ---- taken twice? -----------------------------------------------------
    # The claim is not "the line is missing": a second take must be REFUSED, the ledger must
    # still hold one tombstone, and -- the half Session 22-24 never measured -- the second
    # machine's INVENTORY must not move.  `LootHide` now also disables collision and sets
    # `Acquired` on the partner's copy, so a second `Interact` on it cannot pay out twice.
    $m2 = Get-Mark $other
    $before = @(Get-Since $other $m2 | Where-Object { $_ -match ('loot: local pickup ' + $key.Hash + '#' + [string]$key.Index) }).Count
    $take2 = Invoke-TakeDrop $other $key.Hash $key.Index
    Start-Sleep -Seconds 8
    $after2 = @(Get-Since $other $m2 | Where-Object { $_ -match ('loot: local pickup ' + $key.Hash + '#' + [string]$key.Index) }).Count
    $refused = @(Get-Since $other $m2 | Where-Object { $_ -match ('rig: drop ' + $key.Hash + '#' + [string]$key.Index + ' is already gone') })
    $invO = Get-InvEvents $other $m2
    Verdict $step $other (($after2 - $before) -eq 0) 'the other machine CANNOT take the drop a second time' ("new pickup lines=$($after2 - $before); second take ok=$($take2.Ok)")
    Verdict $step $other ($invO.Gain.Count -eq 0) 'the other machine''s INVENTORY did not change on the second take' ("gain lines=$($invO.Gain.Count)" + $(if ($invO.Gain.Count) { ' -> ' + (($invO.Gain | ForEach-Object { ([string]$_).Trim() }) -join ' | ') } else { '' }))
    Verdict $step $other ($refused.Count -gt 0) 'the second attempt was refused by name (already gone)' $(if ($refused.Count) { ([string]$refused[-1]).Trim() } else { '' })
    # And the drop was not resurrected by the taker's own 1 Hz body.
    $m3 = Get-Mark $other
    Start-Sleep -Seconds 6
    $reoff = @(Get-Since $other $m3 | Where-Object { $_ -match ('loot: drop ' + $key.Hash + '#' + [string]$key.Index) })
    Verdict $step $other ($reoff.Count -eq 0) 'the tombstone was not resurrected by the next state body' ("re-offer lines=$($reoff.Count)")
    return $id
}

function Invoke-LootDrop([int]$H, [int]$P) {
    Step 'R129 -- loot hiding, both directions, in 780_SZ_Depths' `
         'A drop both machines hold; one takes it; the other must hide its copy and refuse a second take.'
    # WHY THE AREA AND NOT THE HUB.  A drop's key is {source_hash, drop_index}, and the
    # source is the enemy.  In the hub each machine runs its OWN spawner enemies with its own
    # authored GUIDs, so the two machines' drop keys are disjoint -- measured this session:
    # three drops the host held and two the peer held, not one key in common
    # (`rig: no live loot-ledger drop matches ... (the scan has 2 drop(s))` on the peer for
    # every host key).  PLAYTEST section 8 already says loot hiding needs the SAME enemy on
    # both machines.  `780_SZ_Depths` is where both machines hold the same authored enemy
    # GUIDs (Sessions 14-20), so that is where this proof has to be made.
    $enterOk = Enter-Area $H $P '780_SZ_Depths' 'SecretSword_Altar_BP' 'loot-area'
    foreach ($v in @($script:verdicts | Where-Object { $_.Step -eq 'loot-area' })) { if (-not $v.Ok) { $enterOk = $false } }
    if (-not $enterOk) {
        Verdict 'r129-roll' $H $false 'the loot proof was run' 'area entry did not converge; refusing to measure drops in two different places'
        return
    }
    $mark = Get-Mark $H
    Invoke-Go $H 'enemy' 200
    Invoke-Go $P 'enemy' 200
    # SETTLE.  Measured: a dump 10 s after gameplay can find 0 callable enemies -- the
    # level's initial spawn set is still being torn down and rebuilt.
    Note '  settling 20 s so the area''s spawn set is live and callable before the first hit'
    Start-Sleep -Seconds 20
    # The gaps phase measures the hit reaction HERE, before the kill rounds take the area's
    # enemies (and the green dragon) down.
    if ($script:ReactInLoot) { Invoke-HitReaction $H $P; Invoke-Go $H 'enemy' 200; Invoke-Go $P 'enemy' 200; Start-Sleep -Seconds 4 }
    $rolled = Invoke-KillRounds $H $P $LootRounds $mark
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
# PROBE -- how long each machine's file-poll levers actually take
# ===========================================================================
# Measured the hard way this session: a 3 s read after writing `qd.enemies` found the
# host's dump and nothing on the peer, and a `qd.tp` written at 11:58:49 was applied at
# 11:59:05.  Every lever in the harness is a file whose CONTENT the game polls, so the
# latency is a property of the run, not of the scenario -- and a harness that assumes
# 3 s turns a slow machine into a failed claim.  This phase measures it, for both kinds
# of lever, and prints the number the rest of the harness waits on.
function Invoke-Probe([int]$H, [int]$P) {
    Step 'PROBE -- file-lever latency on each machine' `
         'qd.enemies (a dump) and qd.tp (a teleport) are both control files the game polls; the 3 s assumption is what lost the peer''s dump.'
    foreach ($i in @($H, $P)) {
        for ($k = 1; $k -le 3; $k++) {
            $mark = Get-Mark $i
            $t0 = Get-Date
            Set-Control $i 'qd.enemies' ('probe-{0}-{1}' -f $k, (Get-Date -Format 'HHmmssfff'))
            $secs = -1
            $deadline = (Get-Date).AddSeconds(40)
            while ((Get-Date) -lt $deadline) {
                Start-Sleep -Milliseconds 400
                $hit = @(Get-Since $i $mark | Where-Object { $_ -match 'enemykey [0-9A-Fa-f]{8} (ALIVE|DEAD|GONE)' })
                if ($hit.Count -gt 0) { $secs = [math]::Round(((Get-Date) - $t0).TotalSeconds, 1); break }
            }
            Note ("  [{0}] dump {1}: answered after {2}s{3}" -f $i, $k, $secs, $(if ($secs -lt 0) { ' (NEVER within 40 s)' } else { '' }))
            Verdict 'probe' $i ($secs -ge 0 -and $secs -le 20) 'the enemy dump answers within 20 s of the trigger' ("dump $k after ${secs}s")

            $mark2 = Get-Mark $i
            $t1 = Get-Date
            Invoke-Go $i 'enemy' 200
            $secs2 = -1
            $deadline = (Get-Date).AddSeconds(40)
            while ((Get-Date) -lt $deadline) {
                Start-Sleep -Milliseconds 400
                $hit = @(Get-Since $i $mark2 | Where-Object { $_ -match 'dev teleport near |dev teleport file ->' })
                if ($hit.Count -gt 0) { $secs2 = [math]::Round(((Get-Date) - $t1).TotalSeconds, 1); break }
            }
            Note ("  [{0}] teleport {1}: answered after {2}s{3}" -f $i, $k, $secs2, $(if ($secs2 -lt 0) { ' (NEVER within 40 s)' } else { '' }))
            Verdict 'probe' $i ($secs2 -ge 0 -and $secs2 -le 20) 'the teleport lever answers within 20 s of the write' ("teleport $k after ${secs2}s")
        }
    }
}

# ===========================================================================
# The soak body: fight, move, and SAMPLE the per-key dump through the run
# ===========================================================================
$script:hpSamples = New-Object System.Collections.Generic.List[object]
# The MEASURED length of each segment, so the soak can report "body" as a sum of what ran
# rather than as a budget it hoped for.  A segment that a dead process cut short records the
# short number, which is the only honest one.
$script:segmentSeconds = @{}
# How many rig hops the soak actually drove (2 per movement iteration: one per machine).
$script:soakHops = 0

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
        # can walk to, so the run is not a stationary turret test.  These are the SAME hops
        # the `rig` phase counts: hub fighting with rig hops is the shape the user's
        # "in darkness / he just falls" report came from.
        if (($n % 2) -eq 0) {
            $kind = $anchors[[int](($n / 2) % $anchors.Count)]
            Invoke-Go $H $kind 200
            Invoke-Go $P $kind 200
            $script:soakHops += 2
        }
        if (((Get-Date) - $lastSample).TotalSeconds -ge $sampleEvery) {
            if (Test-HpGate) {
                # (d) THE GATE, AND IT IS REPORTED.  Session 24's `hub-final` began with
                # `shared-alive=0` because the peer's table held 5 live keys 40 s after a world
                # load (`enemykey n=28 alive=5`).  A sample taken while the tables rebuild
                # measures the rebuild, so it is skipped -- and every skip is a line in the
                # report, not a silent omission that would flatter the numbers.
                $left = [int](($script:hpGateUntil - (Get-Date)).TotalSeconds)
                $tag = ("{0} iteration {1}: t+{2}s, {3}s of the gate left" -f $label, $n,
                        [int]((Get-Date) - $start).TotalSeconds, $left)
                $script:hpSkips.Add($tag) | Out-Null
                Note ("    hp sample SKIPPED ({0})" -f $tag)
            } else {
            $lastSample = Get-Date
            $dh = Invoke-EnemyDump $H ($label + '-h-' + $n)
            $dp = Invoke-EnemyDump $P ($label + '-p-' + $n)
            $cmp = Compare-SharedHp $dh $dp ("{0} t+{1}s" -f $label, [int]((Get-Date) - $start).TotalSeconds)
            $script:hpSamples.Add($cmp) | Out-Null
            # The label/index pairs are spelled out one per line because the first version
            # had them off by one and every printed number was its neighbour -- a sample read
            # "shared-alive=378 compared=0 unequal=0" when what it meant was elapsed=378 s,
            # shared-alive=0. A soak number nobody can attribute is worse than no number.
            Note ("    hp sample {0}: shared-alive={1} compared={2} unequal={3} unreadable={4} ghosts={5} duplicates={7} mx-differ={8} why={6}" -f
                  $cmp.Tag, $cmp.AliveBoth, $cmp.Compared,
                  $cmp.Unequal, $cmp.Unreadable, $cmp.Ghosts, (Format-Whys $cmp.Whys), $cmp.Duplicates, $cmp.MxDiffer)
            foreach ($mm in ($cmp.Mismatches | Select-Object -First 4)) { Note ("      HP MISMATCH " + $mm) }
            }
        }
        Start-Sleep -Seconds 3
    }
    $elapsed = [int]((Get-Date) - $start).TotalSeconds
    $script:segmentSeconds[$label] = $elapsed
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
         'R147/R150a: entering an area is a streamed sublevel, so the signal is TP_AREA on the travel lane. A machine that only re-announces the area it ALREADY holds is not a follow, which is why "already pinned" is not accepted here (Session 21 counted it and reported a pass for a pair that never moved).'
    $mH = Get-Mark $H
    $mP = Get-Mark $P
    Invoke-Hard $H $area
    [void](Check $step $H ('rig: hard lever -> ' + [regex]::Escape($area) + ' streamed=1') 90 'the HOST streamed the area locally' $mH)
    $hostOk = [bool]$script:verdicts[$script:verdicts.Count - 1].Ok
    if (-not $hostOk) {
        Verdict $step $P $false 'the PEER follows the host into the area' 'the host never entered it'
        return $false
    }
    # ONLY the follow counts.  `travel: area <x> is already pinned` is the refusal R150a named.
    $follow = @(Wait-Since $P ('travel: entered the partner.s area ' + [regex]::Escape($area)) $mP 90)
    Verdict $step $P ($follow.Count -gt 0) 'the PEER followed the host into the area' $(if ($follow.Count) { ([string]$follow[-1]).Trim() } else { '' })
    if ($follow.Count -eq 0) {
        Show-Since $P 'travel:|level union|rig: hard' $mP 10
        return $false
    }
    [void](Check $step $H ('rig: level engine state ' + [regex]::Escape($area) + ' LoadedLevel=1 actuallyVisible=1') 90 'the area is LOADED and SHOWN on the host' $mH)
    [void](Check $step $P ('rig: level engine state ' + [regex]::Escape($area) + ' LoadedLevel=1 actuallyVisible=1') 90 'the area is LOADED and SHOWN on the peer' $mP)
    # (d) an area change rebuilds both enemy tables -- do not sample shared HP through it.
    Set-HpGate ("area entry: " + $area) 40
    Start-Sleep -Seconds 6
    if ($anchor) {
        Invoke-Go $H $anchor 400
        Invoke-Go $P $anchor 400
        Start-Sleep -Seconds 8
    }
    return $true
}

# R150a: leaving an area.  The pin used to be a one-way latch and a leave was never
# announced, so "back to the hub" could not happen at all.
function Leave-Area([int]$H, [int]$P, [string]$step) {
    Step "$step -- the HOST leaves its area; the PEER must leave it too" `
         'R150a: the announcement used to run only while the pin was non-empty, so a machine could enter an area and never come out. The leave is announced as dest="-" and ClearAreaPin hides the sublevel through the same lever the pin used.'
    $mH = Get-Mark $H
    $mP = Get-Mark $P
    # `#nonce` so a second leave in one session is not ignored as "the same instruction"
    # (measured: the soak's leave was a no-op because the smoke phase had already written
    # exactly `-`).
    Set-Control $H 'qd.level' ('-#' + (Get-Date -Format 'HHmmssfff'))
    $left = @(Wait-Since $H 'travel: left the area ' $mH 60)
    Verdict $step $H ($left.Count -gt 0) 'the HOST left its area' $(if ($left.Count) { ([string]$left[-1]).Trim() } else { '' })
    $ann = @(Wait-Since $H 'travel: area announcement - ' $mH 60)
    Verdict $step $H ($ann.Count -gt 0) 'the HOST announced the leave' $(if ($ann.Count) { ([string]$ann[-1]).Trim() } else { '' })
    $follow = @(Wait-Since $P 'travel: left the area |travel: the partner left' $mP 90)
    Verdict $step $P ($follow.Count -gt 0) 'the PEER left its area' $(if ($follow.Count) { ([string]$follow[-1]).Trim() } else { '' })
    Set-HpGate 'area leave (back to the hub)' 40
    Start-Sleep -Seconds 4
    $wH = Get-WorldState $H; $wP = Get-WorldState $P
    Verdict $step $P ($wH.Area -eq $wP.Area) 'both machines hold the SAME area after the leave' ("host='$($wH.Area)' peer='$($wP.Area)'")
    return ($follow.Count -gt 0)
}

function Invoke-HardTravel([int]$H, [int]$P, [string]$map, [string]$step) {
    Step "$step -- HARD travel to $map, and the partner must land in it too" `
         'G6: the sender announces HARD_BEGIN, the partner loads the same world through GameInstance.LoadMapWithLoadingScreen, and the host releases the pair only when both arrived.'
    # A NAME THE HUB STREAMS IS NOT A HARD TRAVEL.  `FollowHardTravel` checks the level table
    # first (StreamTravelTo) and a name that is a sublevel of this world is loaded and SHOWN
    # instead of being loaded as a world.  So the step reports which happened: `streamed=1`
    # is a named refusal of the hard path, not a failure of the handshake.  Callers pass the
    # candidates in order, and only a candidate that really announced HARD_BEGIN counts.
    foreach ($cand in @($map -split ',')) {
        $m = $cand.Trim()
        if (-not $m) { continue }
        $mH = Get-Mark $H
        $mP = Get-Mark $P
        Invoke-Hard $H $m
        # (d) a world load rebuilds both enemy tables; no HP sample until the rebuild settles.
        Set-HpGate ("hard world load requested: " + $m) 40
        # `auto` (R150b) makes the MOD pick the world: every name the rig used to try was a
        # sublevel of the world it stood in, so the hard path was never reached.  The lever
        # prints which candidate it picked; the step reads it and checks THAT name.
        $lever = @(Wait-Since $H ('rig: hard lever -> ' + [regex]::Escape($m)) $mH 90)
        if ($lever.Count -eq 0) {
            Note ("  [{0}] the hard lever printed nothing for {1}" -f $H, $m)
            continue
        }
        $leverLine = ([string]$lever[-1]).Trim()
        Note ("  [$H] " + $leverLine)
        $chosen = $m
        if ($m -eq 'auto') {
            $chosen = ''
            foreach ($ln in $lever) {
                if ([string]$ln -match 'auto picked (\S+)') { $chosen = $Matches[1]; break }
            }
            if (-not $chosen) {
                Verdict $step $H $false 'the HOST performed a HARD transition' 'auto found no real UWorld this machine is not already in'
                continue
            }
            Note ("  [$H] auto chose {0}" -f $chosen)
        }
        if ($leverLine -match 'streamed=1') {
            Verdict $step $H $false "the HOST performed a HARD transition to $chosen" 'streamed as a sublevel of this world -- not a hard travel; trying the next candidate'
            continue
        }
        $ann = @(Wait-Since $H ('travel: HARD transition to ' + [regex]::Escape($chosen)) $mH 60)
        Verdict $step $H ($ann.Count -gt 0) "the host announced the hard transition to $chosen" $(if ($ann.Count) { ([string]$ann[-1]).Trim() } else { '' })
        $loaded = @(Wait-Since $P ('travel: hard transition: loading ' + [regex]::Escape($chosen)) $mP 120)
        Verdict $step $P ($loaded.Count -gt 0) "the peer loaded $chosen" $(if ($loaded.Count) { ([string]$loaded[-1]).Trim() } else { '' })
        $both = @(Wait-Since $H ('travel: both machines arrived in ' + [regex]::Escape($chosen)) $mH 120)
        Verdict $step $H ($both.Count -gt 0) "both machines arrived in $chosen (the host released the pair)" $(if ($both.Count) { ([string]$both[-1]).Trim() } else { '' })
        Start-Sleep -Seconds 12
        $wH = Get-WorldState $H; $wP = Get-WorldState $P
        $ok = ($wH.World -eq $chosen -and $wP.World -eq $chosen)
        if ($ok) { Set-HpGate ("the pair is back in " + $chosen + " after a hard load") 40 }
        Verdict $step $P $ok "both machines report $chosen" ("host=$($wH.World) peer=$($wP.World)")
        if ($ok) { return $true }
        Show-Since $H 'travel:|arrive:' $mH 10
        Show-Since $P 'travel:|arrive:' $mP 10
        return $false
    }
    Verdict $step $H $false 'a HARD transition was performed' 'no candidate name reached the hard path'
    return $false
}

# ---------------------------------------------------------------------------
# The end-of-run checks: kill sets, HP, world, area, faults and ERR
# ---------------------------------------------------------------------------
function Invoke-EndChecks([int]$H, [int]$P, [string]$step, [int]$m0) {
    Start-Sleep -Seconds 12
    $wH = Get-WorldState $H; $wP = Get-WorldState $P
    Note ("  world at end: host='{0}' partner='{1}' agree='{2}' area='{3}'" -f $wH.World, $wH.Partner, $wH.Agree, $wH.Area)
    Note ("                peer='{0}' partner='{1}' agree='{2}' area='{3}'" -f $wP.World, $wP.Partner, $wP.Agree, $wP.Area)
    # (c) WORLD DIVERGENCE, outside a travel transition.  A strict `count == 0` was Session
    # 24's check and it failed on four lines that were each the window between one machine
    # loading a world and the other arriving.  Those are the transition; they are counted and
    # reported, and only the ones OUTSIDE a +/- 20 s travel window are a finding.
    $travel = Get-TravelMarks $m0
    $div = Get-DivergenceReport $m0 $travel 20000
    Note ("  travel events seen (both machines): {0}; WORLD DIVERGENCE lines: {1} total, {2} inside a transition, {3} outside" -f
          $travel.Count, $div.All, $div.InTransition, $div.Kept.Count)
    foreach ($s in ($div.Kept | Select-Object -First 6)) { Note ("    DIVERGENCE " + $s) }
    Verdict $step $H ($div.Kept.Count -eq 0) 'no WORLD DIVERGENCE outside a travel transition' `
            ("total={0} in-transition={1} outside={2}" -f $div.All, $div.InTransition, $div.Kept.Count)
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
    Note ("  shared live keys at the end: {0}; comparable={1} unequal={2} unreadable={3} ghosts={4} why={5}" -f
          $end.AliveBoth, $end.Compared, $end.Unequal, $end.Unreadable, $end.Ghosts, (Format-Whys $end.Whys))
    foreach ($mm in ($end.Mismatches | Select-Object -First 8)) { Note ("    HP MISMATCH " + $mm) }
    if ($end.MxDiffer -gt 0) {
        Note ("  keys whose MAX HP differs between the machines (a different body under one key, not a desync): {0} -> {1}" -f
              $end.MxDiffer, ($end.MxKeys -join ','))
    }
    $script:hpSamples.Add($end) | Out-Null

    $cmpTotal = 0; $uneqTotal = 0; $unreadTotal = 0; $aliveTotal = 0; $ghostTotal = 0
    $whys = @{}
    foreach ($s in $script:hpSamples) {
        $cmpTotal += $s.Compared; $uneqTotal += $s.Unequal
        $unreadTotal += $s.Unreadable; $aliveTotal += $s.AliveBoth; $ghostTotal += $s.Ghosts
        foreach ($k in @($s.Whys.Keys)) {
            if ($whys.ContainsKey($k)) { $whys[$k] += $s.Whys[$k] } else { $whys[$k] = $s.Whys[$k] }
        }
    }
    Note ("  HP ACROSS THE RUN: {0} sample(s); shared-alive {1}; COMPARED {2}; unequal {3}; unreadable {4}; ghosts {6}; why {5}" -f
          $script:hpSamples.Count, $aliveTotal, $cmpTotal, $uneqTotal, $unreadTotal, (Format-Whys $whys), $ghostTotal)
    if ($script:hpSkips.Count -gt 0) {
        Note ("  HP samples SUPPRESSED by the gate (d): {0}" -f $script:hpSkips.Count)
        foreach ($s in ($script:hpSkips | Select-Object -First 5)) { Note ("    suppressed: " + $s) }
    }
    Verdict $step $P ($cmpTotal -gt 0) 'shared HP was actually MEASURED during the run' ("compared=$cmpTotal of $aliveTotal shared-alive observations, $unreadTotal unreadable (never counted as equal)")
    Verdict $step $P ($cmpTotal -gt 0 -and $uneqTotal -eq 0) 'the HP of shared enemies is EQUAL wherever it was readable' ("compared=$cmpTotal unequal=$uneqTotal")

    # ---- the rig, over the whole run ---------------------------------------
    # The user's report ("in darkness, no map, he just falls") is answered by three counts,
    # and they are printed for every phase that ends here, not only for `rig`.
    $rig = Get-RigCounters $m0
    Note (("  RIG: hops that landed={0}; refusals because the level is not shown={1} " +
           "(of which {2} was a call where nothing visible matched); no-target={3}; " +
           "'rig: FELL after a rig move'={4}") -f
          $rig.Landed, $rig.Refused, $rig.RefusedCalls, $rig.NoTarget, $rig.Fell)
    # A FELL is the GUARD WORKING, not a failure: it means a placement the game then moved was
    # undone.  What the run must not have is a hop that was left displaced, and that is the
    # `rig` phase's verdict (it measures the position after every hop).  Here the counters are
    # reported so a soak can be compared with the phase.

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
             Where-Object { $_.LastWriteTime -gt $script:RunStartedAt } |
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
# RIG -- the A proof: 60+ hub hops per machine, counted, and screenshotted
# ===========================================================================
# The user's report was "spawns player in darkness, no map visible, or in empty space where
# he just falls".  The unfixed build's own log says it as numbers: 265 hub hops in Session
# 24, 18 of them ending more than 30 m below the target, worst 259 m.  This phase re-runs
# that shape on the fixed build and reports, per hop, where it landed and where the pawn
# actually ended up, so the answer is a count and not an impression.
function Invoke-RigPhase([int]$H, [int]$P) {
    Step ("RIG -- {0} hops per machine in the hub; every one must land on a level that is SHOWN" -f $RigHops) `
         'Session 24 on the unfixed build: 265 hops, 18 ended >30 m below the target, worst 259 m (sublevel resident but not rendered). Every resolver now requires ULevel::bIsVisible and every move arms a 15 m / 6 s fall guard.'
    # THE HUB, NOT THE TITLE SCREEN.  Measured on this phase's first run: a `scan: local at`
    # line exists on the title screen too, so "the machine reached gameplay" was true 2 s
    # before the save finished loading and the first hops landed at 0,0,0 in `title_screen`.
    # A hop test has to start where the player actually is.
    $wH = Wait-Hub $H 300
    $wP = Wait-Hub $P 300
    Note ("  start: host world='{0}' area='{1}'; peer world='{2}' area='{3}'" -f
          $wH.World, $wH.Area, $wP.World, $wP.Area)
    Verdict 'rig' $H ($wH.World -ne '' -and $wH.World -ne 'title_screen') 'the HOST is in the game world, not the title screen' ("world='$($wH.World)'")
    Verdict 'rig' $P ($wP.World -ne '' -and $wP.World -ne 'title_screen') 'the PEER is in the game world, not the title screen' ("world='$($wP.World)'")
    Verdict 'rig' $H (($wH.Area -eq '') -or ($wH.Area -eq '-')) 'the hops are run in the hub (no area pinned)' ("host area='$($wH.Area)'")
    $m0 = Get-Mark $H
    $kinds = @('enemy', 'chest', 'npc')
    $tally = @{ landed = 0; refused = 0; 'no-target' = 0; failed = 0; 'no-answer' = 0; dead = 0 }
    $measured = 0; $unmeasured = 0; $dropped = 0; $worst = 0.0; $worstLine = ''; $restored = 0
    $deep = New-Object System.Collections.Generic.List[string]
    $attempted = 0
    for ($n = 1; $n -le $RigHops; $n++) {
        $kind = $kinds[($n - 1) % $kinds.Count]
        # NOT $h / $p: PowerShell variable names are case-insensitive, so `$h = ...` would
        # OVERWRITE the `$H` parameter and the next call would pass a record as an Int32.
        # (Measured: the first run of this phase died on exactly that.)
        $hopH = Invoke-Hop $H $kind 200
        $hopP = Invoke-Hop $P $kind 200
        foreach ($r in @($hopH, $hopP)) {
            $attempted++
            if ($tally.ContainsKey($r.Answer)) { $tally[$r.Answer] = [int]$tally[$r.Answer] + 1 }
            if ($r.Restored) { $restored++ }
            if ($null -ne $r.Delta) {
                $measured++
                if ($r.Delta -gt $worst) {
                    $worst = $r.Delta
                    $worstLine = ("inst{0} hop {1} '{2}': target z={3} pawn z={4}{5}" -f
                                  $r.Inst, $n, $r.Kind, $r.TargetZ, $r.PostZ,
                                  $(if ($r.Restored) { ' (guarded back)' } else { '' }))
                }
                if ($r.Delta -gt 3000 -and -not $r.Restored) {
                    $dropped++
                    $deep.Add(("inst{0} hop {1} '{2}': target z={3} pawn z={4} dz={5}" -f
                               $r.Inst, $n, $r.Kind, $r.TargetZ, $r.PostZ, [int]$r.Delta)) | Out-Null
                }
            } else {
                $unmeasured++
            }
        }
        if (($n % 8) -eq 0) {
            # THE WHOLE CONCATENATION IS PARENTHESISED: `-f` binds tighter than `+`, so
            # `"a" + "b" -f args` formats only the LAST literal and leaves the first one's
            # placeholders on screen (measured twice in this phase's own console output).
            $prog = ("  {0} hops/machine done: landed={1} refused={2} no-target={3} " +
                     "no-answer={4} guarded-back={5} dropped>30m={6} worst={7} cm") -f $n,
                    $tally['landed'], $tally['refused'], $tally['no-target'],
                    $tally['no-answer'], $restored, $dropped, [int]$worst
            Note $prog
        }
        if (($n % 20) -eq 0) { Save-Shots ("hop{0}" -f $n) }
    }
    $fell = Get-RigCounters $m0
    Note ''
    Note (("  HOPS: {0} attempted ({1} per machine); landed={2} refused-not-shown={3} " +
           "no-target={4} failed={5} no-answer={6} dead={7}") -f
          $attempted, $RigHops, $tally['landed'], $tally['refused'], $tally['no-target'],
          $tally['failed'], $tally['no-answer'], $tally['dead'])
    Note (("  POST-HOP POSITION: measured={0} unmeasured={1}; LEFT more than 3000 cm below " +
           "the target={2}; guarded back={3}; worst dz={4} cm ({5})") -f
          $measured, $unmeasured, $dropped, $restored, [int]$worst,
          $(if ($worstLine) { $worstLine } else { '-' }))
    Note ("  GUARD: 'rig: FELL after a rig move' lines={0}; refusal lines={1}" -f $fell.Fell, $fell.Refused)
    foreach ($d in ($deep | Select-Object -First 6)) { Note ("    DEEP " + $d) }

    Verdict 'rig' $H ($attempted -ge 120) 'at least 60 hops were driven on each machine' ("hops=$attempted of 2x$RigHops")
    Verdict 'rig' $H (($tally['landed'] + $tally['refused'] + $tally['no-target'] +
                       $tally['failed'] + $tally['no-answer'] + $tally['dead']) -eq $attempted) `
            'every hop was answered and classified (nothing is silently unaccounted for)' ("classified=$attempted")
    Verdict 'rig' $H ($tally['landed'] -gt 0) 'hops actually land (the resolver reaches visible actors)' ("landed=$($tally['landed'])")
    Verdict 'rig' $H ($dropped -eq 0) 'no hop was LEFT more than 3000 cm below its target' ("left-dropped=$dropped guarded-back=$restored measured=$measured")

    # ---- the scene, in pixels -------------------------------------------------
    Save-Shots 'final'
    if ($script:shots.Count -gt 0) {
        foreach ($i in 1, 2) {
            $mine = @($script:shots | Where-Object { $_.Inst -eq $i -and $_.Mean -ge 0 })
            if ($mine.Count -eq 0) {
                Verdict 'rig' $i $false 'a screenshot of the game window was captured and is not black' 'no shot could be captured'
                continue
            }
            $lit = @($mine | Where-Object { $_.Mean -ge 8.0 -and $_.Std -ge 2.5 })
            $avgMean = [math]::Round((($mine | Measure-Object -Property Mean -Average).Average), 1)
            $avgStd = [math]::Round((($mine | Measure-Object -Property Std -Average).Average), 1)
            Verdict 'rig' $i ($lit.Count -gt 0) 'the captured scene is lit and non-uniform (geometry on screen)' `
                ("{0} shot(s), mean luminance {1}, stddev {2}, lit {3}/{0}" -f $mine.Count, $avgMean, $avgStd, $lit.Count)
        }
    }
    Note ("  the screenshots are in " + (Join-Path $rigRoot 'shots'))
}

# ===========================================================================
# SMOKE -- the loot proof and the area changes, in one short run
# ===========================================================================
function Invoke-Smoke([int]$H, [int]$P) {
    Step 'SMOKE -- loot both ways, then three consecutive area changes' `
         'No cutscene world is entered here (3a): a hard round trip is the `hardloop` phase''s job, and 997_FinalZoneCutscene is a dark, empty cutscene map -- it is not gameplay and it is not what a play soak should spend a world teardown on.'

    # ---- 1. LOOT -----------------------------------------------------------
    Invoke-LootDrop $H $P

    # ---- 2. THREE CONSECUTIVE AREA CHANGES (R150a) --------------------------
    # 780 -> 770 is the second entry Session 21 saw refused; the leave is the third change
    # and the one the old pin could not express at all.
    Step 'R150a -- three consecutive area changes must all follow' `
         'Session 21: the host streamed 770_SZ_Swamp and the peer answered "area 780_SZ_Depths is already pinned on this machine". A -> B -> hub has to work, not just the first entry.'
    $okB = Enter-Area $H $P '770_SZ_Swamp' 'enemy' 'area2'
    Verdict 'r150a' $P $okB 'the second TP_AREA entry followed on both machines' ''
    if ($okB) {
        $okLeave = Leave-Area $H $P 'area-leave'
        Verdict 'r150a' $P $okLeave 'leaving the area followed on both machines (780 -> 770 -> hub)' ''
    } else {
        Verdict 'r150a' $P $false 'leaving the area followed on both machines' 'the second entry did not follow, so there is nothing to leave'
    }
}

# ===========================================================================
# HARDLOOP -- the repeated hard world load, back to back, in ONE pair of processes
# ===========================================================================
# A hard load is the only event in this game that tears a UWorld down
# (`LoadMapWithLoadingScreen`).  Session 22 lost the peer on the FOURTH load in a process and
# a 30-minute soak could only afford four, so the defect needed a loop that does nothing
# else.  This phase is that loop AND the proof: both machines alive after every load, and
# both machines reporting the same world after every load.
function Invoke-HardLoop([int]$H, [int]$P, [int]$loads) {
    Step ("HARDLOOP -- {0} consecutive hard world loads, both machines alive after every one" -f $loads) `
         'Session 22: the peer died on the fourth world load in one process, inside the game''s own object destruction, with the mod present only as the ProcessEvent forward. The only way to find out whether a world teardown leaves something dangling is to do it N times in one process and look after every one.'
    # R150b: a name this machine only STREAMS is not a hard travel, so `auto` is the mod's
    # own pick of a real UWorld and `Ruins_Level_00` is the world it has to come back to.
    # The second leg of every round trip is therefore the exact load that killed the peer.
    $legs = @('auto', 'Ruins_Level_00')
    $aliveAll = $true
    $sameAll = $true
    # A BARE LOOP IS NOT THE CRASH'S REPRODUCTION.  Session 22's peer died on the fourth load
    # after twenty-five minutes of play in that process, and six bare loads on the unfixed
    # build all survived (measured this session, recorded in the log).  So the loop can be
    # given a warm-up: a real soak segment first, which is what puts mirrors, loot and
    # partner-body state into the world that the next load tears down.
    if ($WarmSeconds -gt 0) {
        Step ("HARDLOOP WARM-UP -- {0} s of play before the loads" -f $WarmSeconds) `
             'The crash needs state the loop alone does not create, so the loop runs on a world that has been played in.'
        Invoke-SoakSegment $H $P 'warmup' $WarmSeconds 'enemy' 60
    }
    $m0 = Get-Mark $H
    for ($n = 1; $n -le $loads; $n++) {
        $leg = $legs[($n - 1) % 2]
        $step = "load$n"
        Note ''
        Note ("--- hard load {0} of {1}: {2} ---" -f $n, $loads, $leg)
        $ok = Invoke-HardTravel $H $P $leg $step
        if (-not $ok) { $sameAll = $false }
        $aH = Test-Alive $H; $aP = Test-Alive $P
        $alive = ($aH -and $aP)
        if (-not $alive) { $aliveAll = $false }
        Verdict $step $P $alive ("both machines are alive after hard load {0} of {1}" -f $n, $loads) ("host=$aH peer=$aP")
        if (-not $alive) {
            Note ("  REFUSING to continue the loop: a machine died on load {0}" -f $n)
            foreach ($i in @($H, $P)) {
                if (-not (Test-Alive $i)) {
                    Note ("  [{0}] the last 25 lines before the death:" -f $i)
                    Show-Since $i 'travel|arrive|ERR|fault|loot' 0 25
                }
            }
            foreach ($f in (@(Get-CrashFolders $H) + @(Get-CrashFolders $P))) { Show-CrashSummary $f }
            break
        }
        # A load is also the moment a world-scoped handle is most likely to be stale, so the
        # per-key dump is taken after every one: a lane that reads a freed actor would
        # otherwise look like "no faults" until the next soak sample.
        $null = Invoke-EnemyDump $P ('load' + $n)
    }
    $faults = 0
    foreach ($i in @($H, $P)) {
        $f = @(Get-Since $i $m0 | Where-Object { $_ -match 'ACCESS_VIOLATION|EXCEPTION|fatal|\bfault' })
        $faults += $f.Count
        foreach ($e in ($f | Select-Object -Last 3)) { Note ("  [$i] " + ([string]$e).Trim()) }
    }
    Verdict 'hardloop' $P $aliveAll ("both machines survived {0} consecutive hard world loads" -f $loads) ("loads={0}" -f $loads)
    Verdict 'hardloop' $P $sameAll 'every hard load landed both machines in the same world' ''
    Verdict 'hardloop' $H ($faults -eq 0) 'no FAULT lines were logged during the loop' ("faults=$faults")
    return $aliveAll
}

# ===========================================================================
# Soak -- >= 1800 s of body: hub fighting, two area changes, two hard round trips
# ===========================================================================
# The order is chosen so every requirement is reached even if a later one fails:
#   hub -> the loot take (which is what entering 780_SZ_Depths is for) -> 770 -> leave ->
#   hub-final.
# Area changes are `780` (inside the loot step), `770` and the leave.  There is NO hard travel
# in this phase (3a): the cutscene worlds are the `hardloop` phase's job, and every hard load
# costs a whole-world teardown the soak has no reason to spend on a cutscene map.
function Invoke-Soak([int]$H, [int]$P) {
    Step ("SOAK -- {0} s of measured body on the documented play set" -f $SoakSeconds) `
         'Session 22 ran 1581 s and 1496 s of body and said so; Session 24 reached 1816 s. This one budgets the BODY at the full duration, reports the wall clock separately, and leaves the cutscene worlds to the hardloop phase (3a): a hard load is a world teardown, and this phase is here to measure PLAY.'
    $m0 = Get-Mark $H
    $wall0 = Get-Date
    $swm = @(Read-LogLines $inst[$H].Log | Where-Object { $_ -match 'switches: ' })
    $swLine = if ($swm.Count -gt 0) { ([string]$swm[-1]).Trim() } else { '' }
    if ($swLine) { Note ("  [$H] " + $swLine) }
    Verdict 'soak' $H ($swLine -match 'enemy=1') 'the switch set under test is the documented PLAY set' $swLine

    $wH0 = Wait-Hub $H 300; $wP0 = Wait-Hub $P 300
    Note ("  world at start: host='{0}' area='{1}' peer='{2}' area='{3}'" -f $wH0.World, $wH0.Area, $wP0.World, $wP0.Area)
    Verdict 'soak' $H ($wH0.World -ne '' -and $wH0.World -ne 'title_screen') 'the soak starts in the game world, not the title screen' ("world='$($wH0.World)'")

    # The body is the budget.  Transitions are between the segments and their cost is
    # reported as wall clock, never folded into the soak number.
    $hub    = [int]($SoakSeconds * 0.32)
    $area2  = [int]($SoakSeconds * 0.16)
    $hub2   = [int]($SoakSeconds * 0.14)
    $final  = $SoakSeconds - $hub - $area2 - $hub2

    Invoke-SoakSegment $H $P 'hub' $hub 'enemy' 75

    # ---- area change 1 (780) with the loot take inside it -------------------
    $areaChanges = 0
    $lootAlive = @()
    if (Test-Alive $H -and (Test-Alive $P)) {
        Invoke-LootDrop $H $P
        $areaChanges++          # 780, entered by the loot step itself
        $lootAlive = @($script:verdicts | Where-Object { $_.Step -like 'r129*' -and $_.Ok })
    } else {
        Verdict 'soak' $P $false 'a loot take was performed and checked on both machines' 'a process died before the loot step'
    }

    # ---- area change 2 (770) ------------------------------------------------
    $ok2 = Enter-Area $H $P '770_SZ_Swamp' 'enemy' 'area2'
    Verdict 'soak' $P $ok2 'area change 2 of 2 followed on both machines (770_SZ_Swamp)' ''
    if ($ok2) { Invoke-SoakSegment $H $P 'area770' $area2 'enemy' 75; $areaChanges++ }

    $ok3 = Leave-Area $H $P 'area-leave'
    Verdict 'soak' $P $ok3 'leaving the area followed on both machines (back to the hub)' ''
    if ($ok3) { Invoke-SoakSegment $H $P 'hub-again' $hub2 'enemy' 75 }

    # ---- NO HARD TRAVEL IN THIS SOAK (3a) -----------------------------------
    # Session 22-24 spent two round trips (four world loads) inside the soak on
    # `997_FinalZoneCutscene` and back.  That world is a CUTSCENE map: dark, empty, no
    # gameplay, and every load costs a whole-asset world teardown whose only dividend here is
    # a crash risk the soak is not the right phase to measure.  Hard loads are covered by the
    # `hardloop` phase, which does nothing else; the soak spends that time on play.
    Note '  (no hard travel in the soak: cutscene worlds are the hardloop phase''s job -- 3a)'

    Invoke-SoakSegment $H $P 'hub-final' $final 'enemy' 60

    $body = 0
    foreach ($k in @('hub','area770','hub-again','hub-final')) {
        if ($script:segmentSeconds.ContainsKey($k)) { $body += [int]$script:segmentSeconds[$k] }
    }
    Note ("  segment seconds actually measured: " + (($script:segmentSeconds.GetEnumerator() | Sort-Object Name |
          ForEach-Object { "$($_.Key)=$($_.Value)s" }) -join ' '))
    Note ("  area changes that FOLLOWED: {0}; hard travels: 0 by design (3a); rig hops driven: {1}; MEASURED BODY: {2} s of {3} s" -f
          $areaChanges, $script:soakHops, $body, $SoakSeconds)
    $wall = [int]((Get-Date) - $wall0).TotalSeconds
    Note ("  soak BODY {0} s; wall clock including transitions {1} s" -f $body, $wall)
    Verdict 'soak' $H ($body -ge $SoakSeconds) "the measured body lasted at least {0} s" ("{0} s of {1} s" -f $body, $SoakSeconds)
    Verdict 'soak' $H ($areaChanges -ge 2) 'at least two area changes followed on both machines' ("followed={0}" -f $areaChanges)
    Verdict 'soak' $H ($script:soakHops -ge 100) 'the soak drove rig hops throughout (hub fighting with movement)' ("hops={0}" -f $script:soakHops)
    Verdict 'soak' $P ($lootAlive.Count -gt 0) 'a loot take was performed and checked on both machines' ("r129 verdicts passed=$($lootAlive.Count)")

    Invoke-EndChecks $H $P 'soak' $m0
}

# ===========================================================================
# LOOT -- R129's take half on its own, so it can be iterated without a soak
# ===========================================================================
function Invoke-LootPhase([int]$H, [int]$P) {
    # The mark is taken at the START: EndChecks counts the rig over [mark, now], and a mark
    # taken at the end measures nothing (measured: "hops that landed=0" after a phase that
    # had hopped all the way into 780_SZ_Depths).
    $m0 = Get-Mark $H
    Invoke-LootDrop $H $P
    Invoke-EndChecks $H $P 'loot' $m0
}


# ===========================================================================
# THE SMALL GAPS (after Session 26): partner vitals, the shop door, hit reactions, loot
# ===========================================================================
function Invoke-GapsPhase([int]$H, [int]$P) {
    $m0 = Get-Mark $H
    Step 'GAP: the partner''s HP on screen' 'Each machine sends its own HP on the STATUS tail; the other draws it on its HUD after AHUD.ReceiveDrawHUD.'
    [void](Check 'vitals' $H 'vitals: HUD .* ReceiveDrawHUD resolved' 60 'the HOST resolved its HUD draw' 0)
    [void](Check 'vitals' $P 'vitals: HUD .* ReceiveDrawHUD resolved' 60 'the PEER resolved its HUD draw' 0)
    [void](Check 'vitals' $H 'vitals: drew the partner bar PARTNER  \d+ / \d+' 60 'the HOST drew the partner bar' 0)
    [void](Check 'vitals' $P 'vitals: drew the partner bar PARTNER  \d+ / \d+' 60 'the PEER drew the partner bar' 0)
    $mH = Get-Mark $H
    $mP = Get-Mark $P
    Set-Control $P 'qd.selfhp' ('37#' + (Get-Date -Format 'HHmmssfff'))
    [void](Check 'vitals' $P 'rig: selfhp -> 37' 15 'the PEER dropped its own HP to 37' $mP)
    [void](Check 'vitals' $H 'vitals: partner hp 3[7-9]/' 15 'the HOST received the partner''s new HP' $mH)
    [void](Check 'vitals' $H 'vitals: drew the partner bar PARTNER  3[7-9] / ' 15 'the HOST drew the new HP on screen' $mH)
    Save-Shots 'vitals'
    Start-Sleep -Seconds 22      # the selfhp hold ends; god mode restores the peer

    Step 'GAP: the shop door' 'Shop_Door_00_BP_C is one-sided; it opens through its own Activate(ActorThatTriggered).  The host opens it; the peer must see it open and get ShopDoorOpened.'
    # BOTH STAND AT THE DOOR.  Measured (run 2): from 458 m away `Activate` ran and nothing
    # moved -- the door's level was not shown, so its open timeline (whose finish saves the
    # state) never ticked, and the savable owner table did not even list it.
    Invoke-Go $H 'Shop_Door_00_SM' 250
    Invoke-Go $P 'Shop_Door_00_SM' 400
    Start-Sleep -Seconds 8
    $mH = Get-Mark $H
    $mP = Get-Mark $P
    Set-Control $H 'qd.act' ('near:Shop_Door_00_SM!GetInteractInfo#' + (Get-Date -Format 'HHmmssfff'))
    Start-Sleep -Seconds 1
    # THE DOOR'S OWN VERBS, in the order Door_Base names them.  Measured (run 3): standing at
    # the door, `Activate` runs and the state stays 0.  `OpenDoor` is Door_Base's activation
    # verb; `Open` is the chest-style name.  Each is followed by a state read 5 s later.
    $mH = Get-Mark $H
    $opened = $false
    $tries = @()
    foreach ($v in @('OpenDoor', 'Activate')) { foreach ($sd in 0..3) { $tries += ($v + '@' + $sd) } }
    foreach ($verb in $tries) {
        $mv = Get-Mark $H
        Set-Control $H 'qd.act' ('near:Shop_Door_00_SM!' + $verb + '#' + (Get-Date -Format 'HHmmssfff'))
        $ran = @(Wait-Since $H ('dev interact: forced verb ' + ($verb -replace '@\d$', '') + ' on Shop_Door_00_SM') $mv 15)
        Note ("    verb {0}: {1}" -f $verb, $(if ($ran.Count) { ([string]$ran[-1]).Trim() } else { 'no answer' }))
        Start-Sleep -Seconds 5
        $mv = Get-Mark $H
        Set-Control $H 'qd.act' ('near:Shop_Door_00_SM!GetInteractInfo#' + (Get-Date -Format 'HHmmssfff'))
        $st = @(Wait-Since $H 'dev state: Shop_Door_00_SM ' $mv 15)
        Note ("    after {0}: {1}" -f $verb, $(if ($st.Count) { ([string]$st[-1]).Trim() } else { 'no state' }))
        if (@(Get-Since $H $mH | Where-Object { $_ -match 'flag NEW ShopDoorOpened = 1' }).Count) { $opened = $true; break }
    }

    [void](Check 'shopdoor' $H 'flag NEW ShopDoorOpened = 1' 20 'the HOST door saved ShopDoorOpened (its open state)' $mH)
    [void](Check 'shopdoor' $P 'flag refresh: ShopDoorOpened=1 -> .* ran .* yaw' 30 'the PEER door ran its own load path on the partner''s flag' $mP)
    Show-Since $P 'flag refresh:' $mP 4

    Show-Since $H 'Shop_Door|ShopDoorOpened' $mH 6
    Show-Since $P 'Shop_Door|ShopDoorOpened' $mP 6
    Start-Sleep -Seconds 4
    foreach ($i in @($H, $P)) {
        $m = Get-Mark $i
        Set-Control $i 'qd.flag' ('ShopDoorOpened#' + (Get-Date -Format 'HHmmssfff'))
        $ln = @(Wait-Since $i 'rig: flag dump -- ShopDoorOpened = ' $m 15)
        $ok = $ln.Count -gt 0 -and ([string]$ln[-1]) -match (' = ([1-9]\d*)( \(game bank.*\))?$')
        Verdict 'shopdoor' $i $ok 'ShopDoorOpened >= 1 in this machine''s bank' $(if ($ln.Count) { ([string]$ln[-1]).Trim() } else { 'no answer' })
    }
    Save-Shots 'shopdoor'

    # THE LOOT: the taken actor leaves the taker's screen (780_SZ_Depths).
    # THE BOSS IN ITS FIGHT.  Measured (run 5): a hit on the green dragon OUTSIDE its fight is
    # refused by the game's own gate (its override calls the parent TryApplyDamage, then
    # CheckForSplit; the idle dragon is still in its intro).  So the dragon is measured inside
    # the started fight: Invoke-OneBoss lands one hit from each player and records which path
    # moved its HP.
    $b = $script:Bosses | Where-Object { $_.Name -eq 'GreenDragon' } | Select-Object -First 1
    if ($b -and (Test-Alive $H) -and (Test-Alive $P)) { Invoke-OneBoss $H $P $b }

    # THE HIT REACTION runs inside it, before the kill rounds: a NON-lethal hit from the player
    # who does not run the enemy, applied by the owner through the class's own entry.
    $script:ReactInLoot = $true
    if ((Test-Alive $H) -and (Test-Alive $P)) { Invoke-LootDrop $H $P }
    $script:ReactInLoot = $false
    Invoke-EndChecks $H $P 'gaps' $m0
}

function Invoke-HitReaction([int]$H, [int]$P) {
    Step 'GAP: the enemy reacts to the partner''s hit' 'The non-owner lands a small hit on a shared live enemy; the owner applies it through the class''s own damage entry.'
    # 'near:enemy' can be any enemy in the union of loaded areas; in the swamp and on the
    # bridge it put the host over nothing and it fell out of the world (Session 29 run 5).
    if (-not ($b.PSObject.Properties['Stay'] -and $b.Stay)) {
        Invoke-Go $H 'enemy' 600
        Invoke-Go $P 'enemy' 600
    }
    Start-Sleep -Seconds 8
    $dh = Invoke-EnemyDump $H 'react-h'
    $dp = Invoke-EnemyDump $P 'react-p'
    $pick = $null
    foreach ($k in $dh.Keys) {
        $r = $dh[$k]
        if ($r.State -ne 'ALIVE' -or $null -eq $r.Hp -or $r.Hp -lt 60) { continue }
        if (-not $dp.ContainsKey($k) -or $dp[$k].State -ne 'ALIVE') { continue }
        if ($r.Cls -match 'Dragon|Josami.*Lord|Boss') { continue }
        $pick = $r; break
    }
    if (-not $pick) { Verdict 'react' $H $false 'a shared live ordinary enemy to hit' 'none alive on both'; return }
    Note ("  target {0} {1} hp={2}" -f $pick.Key, $pick.Cls, $pick.Hp)
    $amt = [int][math]::Max(5, [math]::Floor($pick.Hp * 0.2))
    $mH = Get-Mark $H
    $mP = Get-Mark $P
    # Both machines send it; whichever does NOT run the enemy sends the intent, the owner's own
    # lever applies locally -- one of the two is the partner's hit.
    Set-Control $P 'qd.hitguid' ($pick.Key + ':' + $amt + '#' + (Get-Date -Format 'HHmmssfff'))
    Start-Sleep -Seconds 6
    $all = @(@(Get-Since $H $mH) + @(Get-Since $P $mP))
    $intent = @($all | Where-Object { $_ -match ('hit on enemy ' + $pick.Key + ' from the other player') })
    $local = @($all | Where-Object { $_ -match ('rig: hitguid -> key=' + $pick.Key + ' LOCAL hit') })
    $path = @($all | Where-Object { $_ -match 'damage: enemy hp via TryApplyDamage|reaction: fallback presentation' })
    Verdict 'react' $P ([bool](($intent.Count + $local.Count) -gt 0)) 'the PEER''s hit reached the enemy''s owner' $(if ($intent.Count) { ([string]$intent[-1]).Trim() } elseif ($local.Count) { ([string]$local[-1]).Trim() } else { 'no line' })
    # Either the game's own pipeline ran (its reaction is the game's), or the class refused
    # every payload and the owner played the class's own damaged FX (measured on
    # Guardian_Fishman_BP_C, run 4).  Which one is recorded in the line.
    $ok = $path.Count -gt 0 -and ([string]$path[-1]) -match 'via TryApplyDamage|customFX=played|flash=played'
    Verdict 'react' $H ([bool]$ok) 'the owner played a reaction (game pipeline, or the class''s own damaged FX)' $(if ($path.Count) { ([string]$path[-1]).Trim() } else { 'no damage line' })
    Start-Sleep -Seconds 3
    $dh2 = Invoke-EnemyDump $H 'react-h2'
    $dp2 = Invoke-EnemyDump $P 'react-p2'
    $a = $dh2[$pick.Id]; $b2 = $dp2[$pick.Id]
    $hpOk = $a -and $b2 -and $null -ne $a.Hp -and $a.Hp -lt $pick.Hp -and [math]::Abs($a.Hp - $b2.Hp) -le [math]::Max(1.0, 0.02 * $pick.Hp)
    Verdict 'react' $H ([bool]$hpOk) ("one HP on both after the hit: start={0} host={1} peer={2}" -f $pick.Hp, $(if ($a) { $a.Hp }), $(if ($b2) { $b2.Hp })) ''
    Save-Shots 'react'

    # THE MOUNT.  The green dragon forwards a hit to its MountedLord; the partner's hit must
    # take the same path (rider HP moves), not the clamped write on the dragon.
    $drag = $null
    foreach ($k in $dh2.Keys) {
        $r = $dh2[$k]
        if ($r.State -eq 'ALIVE' -and $r.Cls -match 'Dragon_Green' -and $dp2.ContainsKey($k) -and $dp2[$k].State -eq 'ALIVE') { $drag = $r; break }
    }
    if (-not $drag) { Note '  no live green dragon on both machines; the mount case is not measured this run'; return }
    Note ("  mount target {0} {1} hp={2}" -f $drag.Key, $drag.Cls, $drag.Hp)
    $mH = Get-Mark $H
    $mP = Get-Mark $P
    Set-Control $P 'qd.hitguid' ($drag.Key + ':50#' + (Get-Date -Format 'HHmmssfff'))
    Start-Sleep -Seconds 6
    $all = @(@(Get-Since $H $mH) + @(Get-Since $P $mP))
    $path = @($all | Where-Object { $_ -match 'damage: enemy hp via|reaction: fallback presentation' })
    $ok = $path.Count -gt 0 -and ([string]$path[0]) -match 'via TryApplyDamage'
    Verdict 'react' $H ([bool]$ok) 'the dragon took the partner''s hit through its own forward to the rider' $(if ($path.Count) { ([string]$path[0]).Trim() } else { 'no damage line' })
}


# ===========================================================================
# THE BOSS TOUR (Session 26)
# ===========================================================================
# One row per boss the content places, decoded from the shipped maps (spawner, the volume
# that starts the fight, the class to look for, and the NamedSaveData key its death writes).
# `Start` is the instance name `qd.trigger` walks the HOST into: a placed ActivationTrigger
# for the triggered bosses, the boss SPAWNER itself (its own TrapActivateTrigger) for the
# proximity bosses.  The Queen and Sipeha end the game, so they are last and opt-in.
$script:Bosses = @(
    [pscustomobject]@{ Name = 'GreenDragon';  Area = '780_SZ_Depths';      Start = 'ActivationTrigger9';          Cls = 'Dragon_Green';     Flag = 'DragonLordDead'; Group = 'Dragon_Green|JosamiLord_Green' }
    [pscustomobject]@{ Name = 'ROSDragon';    Area = '760_SZ_AUT';         Start = 'ActivationTrigger11';         Cls = 'Dragon_Ros';       Flag = 'ROSDragonDead'; Talk = 'RewardDialogueEnded'; Near = 200 }
    [pscustomobject]@{ Name = 'RedLords';     Area = '740_SZ_Prison';      Start = 'ActivationTrigger4';          Cls = 'JosamiBloodLord';  Flag = 'RedLordsDead' }
    [pscustomobject]@{ Name = 'DemonBosses';  Area = '450_MAUS_darkmaze';  Start = 'ActivationTrigger4';          Cls = 'DemonSpider';      Flag = 'DemonBossesKilled'; Group = 'DemonSpider|Hellbat' }
    [pscustomobject]@{ Name = 'ShrineBoss';   Area = '100_GR';             Start = 'ShrinebossActivationTrigger'; Cls = 'Shrineboss';       Flag = '' }
    [pscustomobject]@{ Name = 'WhaleKing';    Area = '210_AQ_underground'; Start = 'WhaleKingSpawner';            Cls = 'Whaleking_Boss';   Flag = 'WhaleKingIsDead'; Group = 'Whaleking_Boss'; Talk = 'RewardDialogueEnded' }
    [pscustomobject]@{ Name = 'Necronomicon'; Area = '300_LIB';            Start = 'Necronomicon_Boss_Spawner';   Cls = 'Necronomicon_BP';  Flag = 'NecronimiconDefeated'; Talk = @('Necronomicon_BP!Interact', 'dialogue!End'); TalkWait = 6; Begin = @('Necronomicon_Boss_Spawner!CanEverSpawn', 'Necronomicon_Boss_Spawner!CheckSpawnConditions', 'Necronomicon_Boss_Spawner!Check+Spawn+Visibility', 'Necronomicon_Boss_Spawner!Try+Spawning+Enemy', 'Necronomicon_Boss_Spawner!CustomEvent'); BeginSync = $true; Stay = $true }
    [pscustomobject]@{ Name = 'BridgeLord';   Area = '500_AUT';            Start = 'JosamiLord_Spawner';          Cls = 'JosamiLord_BP';    Flag = 'BridgeLordDead'; Talk = 'EndedDeathDialogue'; Begin = @('JosamiLord_Spawner!~BndEvt__JosamiLord_Spawner_BridgeCenterTrigger'); Stay = $true }
    [pscustomobject]@{ Name = 'Queen';        Area = '600_Castle';         Start = 'Queen_Spawner_BP';            Cls = 'Queen_BP';         Flag = 'Ending1Complete'; Begin = @('Queen_BP!Interact', 'dialogue!End'); BeginSync = $true; Stay = $true; OptIn = $true; GrabTest = $true; Phase = @('Queen_BP!?CurrentStateIndex,DoingPhaseAnim,DeathIsSuspended,bDead,HP,MaxHP,DoinAOrb,IntroWasAbbrev,SpecialSwordTriggerHP', 'Queen_BP!?CurrentStateIndex,DoingPhaseAnim,DeathIsSuspended,bDead,HP,DoinAOrb', 'Queen_BP!OrbProjectileHitPlayer', 'Queen_BP!?CurrentStateIndex,DoingPhaseAnim,DeathIsSuspended,bDead,HP,DoinAOrb', 'Queen_BP!?CurrentStateIndex,DoingPhaseAnim,DeathIsSuspended,bDead,HP,DoinAOrb') }
    [pscustomobject]@{ Name = 'GiantFish';    Area = '770_SZ_Swamp';       Start = 'GiantFish_Spawner_BP';        Cls = 'GiantFish_Enemy';  Flag = 'GiantSwampFish_IsDead'; Stay = $true; Near = 600; Wait = 12 }
)

function Find-BossKey([hashtable]$dump, [string]$cls) {
    foreach ($k in $dump.Keys) {
        $r = $dump[$k]
        if ($r.State -eq 'ALIVE' -and $r.Cls -match [regex]::Escape($cls)) { return $r }
    }
    return $null
}

# One boss, end to end.  Every verdict reads a line; nothing is inferred from a lever's
# optimism.  The order is the player's: enter the area together, one player walks into the
# start volume, the partner's game starts the same fight, both see one boss and one bar,
# damage moves the same HP on both, the death lands on both, and the death flag is in both
# banks.
function Invoke-OneBoss([int]$H, [int]$P, $b) {
    $step = 'boss-' + $b.Name
    Step "$step -- $($b.Area): walk into $($b.Start), fight, kill" `
         'Trigger lane (K_TRIGGER): the host walks in, the peer must replay it; then one shared boss, one bar, the same HP, one death, the flag on both.'
    $mH0 = Get-Mark $H
    $mP0 = Get-Mark $P
    # Where the HOST stands in the hub BEFORE the area: the leave walks it back here first,
    # because a real player LEAVES by walking out -- hiding the area under a player is a
    # fall into the stacked levels (measured: the follower fell into the hub teleporter and
    # the world reload took both processes down).
    $hubPos = Get-LastPos $H 0
    if (-not (Enter-Area $H $P $b.Area '' $step)) {
        Verdict $step $H $false 'reached the area on both machines' 'area did not follow; boss skipped'
        return
    }
    # The start volume.  The rig stands the HOST in it and dispatches the begin event the
    # overlap would; the lane must carry it to the peer.
    $mH = Get-Mark $H
    $mP = Get-Mark $P
    Set-Control $H 'qd.trigger' ($b.Start + '@' + $b.Area + '#' + (Get-Date -Format 'HHmmssfff'))
    [void](Check $step $H ("rig: trigger '" + [regex]::Escape($b.Start) + "' begin event: dispatched") 30 'the HOST walked into the start volume' $mH)
    [void](Check $step $H ('trigger: local player entered ' + [regex]::Escape($b.Start) + '.*sent to the partner') 15 'the HOST sent the entry to the partner' $mH)
    [void](Check $step $P ("trigger: replayed the partner's entered " + [regex]::Escape($b.Start) + '.*: dispatched') 20 "the PEER replayed the entry on its own copy" $mP)
    Show-Since $P 'trigger: |pulled in' $mP 6
    # STAY IN THE ARENA (Session 28 run 4): a player placed at a volume's centre falls out of
    # it, and a proximity boss (the Whale King) then never spawns.  The host stands at the
    # start actor again, as a player who walked in would still be standing there.
    # A spawner refuses to spawn in the player's view (`Check Spawn Visibility`), so the host
    # stands 15 m off the start actor rather than on it (run 5: no whale with the host on it).
    # Per boss: the ROS dragon's arena is a ledge, and 15 m off its start sphere is over the
    # drop (Session 29 runs 2-3: the host fell 47 m, z -6227 -> -10906, and the dragon left).
    $near = if ($b.PSObject.Properties['Near'] -and $b.Near) { [int]$b.Near } else { 1500 }
    Invoke-Go $H $b.Start $near
    Start-Sleep -Seconds 12
    if ($b.PSObject.Properties['Begin'] -and $b.Begin) {
        # A boss whose fight starts from a conversation (the Queen's greeting): the HOST's
        # player talks; the dialogue's end event runs on the host's copy only.
        $mB = Get-Mark $P
        Invoke-Go $H $b.Cls 600
        foreach ($act in @($b.Begin)) {
            Set-Control $H 'qd.act' ('near:' + $act + '#' + (Get-Date -Format 'HHmmssfff'))
            Start-Sleep -Seconds 5
        }
        Start-Sleep -Seconds 5
        Show-Since $H 'rig: act|rig: verb|forced verb|boss begin' $mH 14
        if ($b.PSObject.Properties['BeginSync'] -and $b.BeginSync) {
            [void](Check $step $H 'boss begin: (this player finished|.* left its rest spot|.* plays its intro)' 30 'the HOST began the fight and told the partner' $mH)
            [void](Check $step $P 'boss begin: .* ran on this copy' 30 "the PEER's copy began from the partner's start" $mB)
        }
    }

    # Both machines must hold the SAME boss (one key, alive on both).
    # 'near:enemy' can be any enemy in the union of loaded areas: in the swamp and on the bridge
    # it put the host over nothing (Session 29 run 5), and in the castle it teleported the host
    # 29 km out of the Queen's arena, whose spawner then reset her (run 16).  A `Stay` boss
    # keeps the host where the start put it.
    if (-not ($b.PSObject.Properties['Stay'] -and $b.Stay)) {
        Invoke-Go $H 'enemy' 600
        Invoke-Go $P 'enemy' 600
    }
    Start-Sleep -Seconds 8
    $dh = Invoke-EnemyDump $H ($b.Name + '-h1')
    $dp = Invoke-EnemyDump $P ($b.Name + '-p1')
    $bh = Find-BossKey $dh $b.Cls
    # A boss with an intro (the ROS dragon flies in) or a proximity spawn exists seconds later.
    $tries = if ($b.PSObject.Properties['Wait'] -and $b.Wait) { [int]$b.Wait } else { 3 }
    for ($w = 0; $w -lt $tries -and -not $bh; $w++) {
        Start-Sleep -Seconds 10
        $dh = Invoke-EnemyDump $H ($b.Name + '-h1r' + $w)
        $dp = Invoke-EnemyDump $P ($b.Name + '-p1r' + $w)
        $bh = Find-BossKey $dh $b.Cls
    }
    $bp = $null
    if ($bh -and $dp.ContainsKey($bh.Id)) { $bp = $dp[$bh.Id] }
    if (-not $bh) { $bh2 = Find-BossKey $dp $b.Cls; if ($bh2) { Note "    the PEER holds $($b.Cls) $($bh2.Key) but the HOST does not" } }
    Verdict $step $H ([bool]$bh) "the HOST holds a live $($b.Cls)" $(if ($bh) { $bh.Line } else { '' })
    Verdict $step $P ([bool]($bp -and $bp.State -eq 'ALIVE')) "the PEER holds the SAME key alive" $(if ($bp) { $bp.Line } else { '' })
    if (-not $bh) { Show-Since $H 'boss|spawn|trigger' $mH 12; Leave-Area $H $P $step | Out-Null; Start-Sleep -Seconds 5; return }
    $key = $bh.Key

    # One bar, for a member of this boss's group (the Red Lords are two actors and the
    # game's bar names one of them).
    $grpRx = if ($b.PSObject.Properties['Group'] -and $b.Group) { $b.Group } else { [regex]::Escape($b.Cls) }
    $keys = @($dh.Values | Where-Object { $_.Cls -match $grpRx } | ForEach-Object { $_.Key })
    if ($keys.Count -eq 0) { $keys = @($key) }
    $keyRx = '(' + ($keys -join '|') + ')'
    [void](Check $step $H ('boss: (bar key=|active key -> )' + $keyRx) 40 'the HOST raised the shared bar for this boss' $mH)
    [void](Check $step $P ('ActivateBossHPBar called|boss: follower raised its own bar for guid=' + $keyRx + '|boss: bar key=' + $keyRx) 40 'the PEER shows a bar' $mP)

    if ($b.PSObject.Properties['GrabTest'] -and $b.GrabTest) {
        # THE GRAB REACHES THE PARTNER (Session 29): the peer stands at the boss and the host
        # 25 m back, so the boss targets the partner's body on the host; an orb that reaches it
        # must be relayed and played on the peer against its own player.
        $mG = Get-Mark $H
        $mGp = Get-Mark $P
        # The peer may not hold a copy yet (its copy is constructed from the host's rows once it is
        # near): its own spawner is the place every machine has (run 22: 'near:Queen_BP' on the
        # peer found nothing and it stayed in the hidden hub).
        Invoke-Go $P $b.Start 300
        Start-Sleep -Seconds 6
        Invoke-Go $P $b.Cls 400
        Invoke-Go $H $b.Cls 2500
        # EQUAL CO-OP BY OWNERSHIP: with the peer nearer, the PEER's machine runs her, and her
        # orbs (and grabs) are its own -- published by the peer as local effects.
        [void](Check $step $P 'boss owner: grab boss .* RUN HERE' 40 "the PEER runs the Queen while its player is the nearer one" $mGp)
        [void](Check $step $P 'effect: local #.*Queen_Orb_Projectile' 75 "the Queen's orbs are fired at the PEER's own player, on the peer" $mGp)
        Show-Since $P 'boss owner|effect: local #.*Queen|cutscene' $mGp 12
        Show-Since $H 'boss owner|TargetPlayer' $mG 8
        Invoke-Go $H $b.Cls 600
    }
    # Damage from BOTH players moves one HP.
    $hp0 = $bh.Hp
    $mHh = Get-Mark $H
    $mPh = Get-Mark $P
    Set-Control $H 'qd.hitguid' ($key + ':200#' + (Get-Date -Format 'HHmmssfff'))
    Start-Sleep -Seconds 3
    Set-Control $P 'qd.hitguid' ($key + ':200#' + (Get-Date -Format 'HHmmssfff'))
    Start-Sleep -Seconds 6
    $dh2 = Invoke-EnemyDump $H ($b.Name + '-h2')
    $dp2 = Invoke-EnemyDump $P ($b.Name + '-p2')
    $h2 = $dh2[$bh.Id]; $p2 = $dp2[$bh.Id]
    $hpOk = $h2 -and $p2 -and $null -ne $h2.Hp -and $null -ne $p2.Hp -and $h2.Hp -lt $hp0 -and [math]::Abs($h2.Hp - $p2.Hp) -le [math]::Max(1.0, 0.02 * $hp0)
    Verdict $step $H ([bool]$hpOk) ("both hits landed on ONE HP: start={0} host={1} peer={2}" -f $hp0, $(if ($h2) { $h2.Hp }), $(if ($p2) { $p2.Hp })) ''
    # THE HIT REACTION.  The owner applies both hits; the line says which path moved the HP --
    # the game's own TryApplyDamage with a built hit (its whole on-hit pipeline) or the clamped
    # write plus the class's own flash/FX.
    $rx = 'damage: enemy hp via TryApplyDamage|reaction: fallback presentation'
    $rl = @(@(Get-Since $H $mHh) + @(Get-Since $P $mPh) | Where-Object { $_ -match $rx })
    $rOk = $rl.Count -gt 0 -and ([string]$rl[-1]) -match 'hit=built|flash=played|customFX=played'
    Verdict $step $H ([bool]$rOk) 'the owner played the hit reaction for the hits' $(if ($rl.Count) { ([string]$rl[-1]).Trim() } else { 'no reaction line' })
    Show-Since $H 'damage: hit built|damage: FHitResult|damage: enemy hp via|reaction:' $mHh 6
    Show-Since $P 'damage: hit built|damage: FHitResult|damage: enemy hp via|reaction:' $mPh 6

    # The death, on both, and the flag in both banks.
    $mH = Get-Mark $H
    $mP = Get-Mark $P
    # THE KILL IS DAMAGE, NOT `Kill`.  Measured in Session 26: the enemy's own `Kill` ends
    # the actor without the spawner's death path, so `DragonLordDead` was never written on
    # either machine.  A real player kills through damage, so the rig does too: one hit
    # larger than any boss' HP, from the HOST (routed to whichever machine owns it).
    # A FIGHT, NOT ONE BLOW (Session 28).  A boss's death flag is written at the END of its own
    # death sequence (a montage callback, a death dialogue), and a phase boss changes form at
    # HP thresholds; a single 1,000,000 hit skipped every threshold and no flag came.  The rig
    # stands next to the boss and strikes a quarter of its max HP every 3 s.
    Invoke-Go $H $b.Cls 500
    $mxk = if ($bh.Mx) { [double]$bh.Mx } else { [double]$bh.Hp }
    $chunk = [int][math]::Ceiling([math]::Max(50, $mxk / 4))
    for ($k = 0; $k -lt 8; $k++) {
        Set-Control $H 'qd.hitguid' ($key + ':' + $chunk + '#' + (Get-Date -Format 'HHmmssfff'))
        Start-Sleep -Seconds 3
        $dk = Invoke-EnemyDump $H ($b.Name + '-k' + $k)
        $rk = $dk[$bh.Id]
        if (-not $rk -or $rk.State -ne 'ALIVE') { Note "    the boss fell after strike $($k + 1) of $chunk"; break }
        Note ("    strike {0}: hp={1}" -f ($k + 1), $rk.Hp)
    }
    [void](Check $step $H ('rig: hitguid -> key=' + $key) 20 'the HOST struck the killing blow' $mH)
    # A GROUP DIES TOGETHER.  The Red Lords revive each other: one killed alone is stood up
    # again by her sister within seconds (measured, run 13), and RedLordsDead needs both
    # down.  Every other live member is struck at once, not after the 8 s settle below.
    $grpNow = if ($b.PSObject.Properties['Group'] -and $b.Group) { $b.Group } else { [regex]::Escape($b.Cls) }
    foreach ($e in @($dh2.Values | Where-Object { $_.State -eq 'ALIVE' -and $_.Cls -match $grpNow -and $_.Key -ne $key })) {
        Note "    group member $($e.Key) $($e.Cls) -- struck with the main kill"
        Set-Control $H 'qd.hitguid' ($e.Key + ':1000000#' + (Get-Date -Format 'HHmmssfff'))
        Start-Sleep -Milliseconds 700
    }
    Start-Sleep -Seconds 8
    if ($b.PSObject.Properties['Phase'] -and $b.Phase) {
        # A BOSS WITH A SECOND PHASE (Session 29: the Queen's phase 1 at HP 0 transitions, it
        # does not end).  Each step is an act on the host ('X!Verb' or 'X!?Props'), 6 s apart.
        $mPh = Get-Mark $H
        $mPp = Get-Mark $P
        foreach ($act in @($b.Phase)) {
            Set-Control $H 'qd.act' ('near:' + $act + '#' + (Get-Date -Format 'HHmmssfff'))
            Start-Sleep -Seconds 6
        }
        Show-Since $H 'dev props|forced verb|rig: verb|boss|cutscene' $mPh 24
        Show-Since $P 'boss|cutscene' $mPp 12
    }
    # LATER PHASES ARE OTHER ACTORS.  The green dragon's rider (JosamiLord_Green) jumps off
    # and fights on; the spawner writes DragonLordDead only when BOTH are dead.  Kill every
    # still-live member of the boss's group the same way, until none is left.
    $grp = if ($b.PSObject.Properties['Group'] -and $b.Group) { $b.Group } else { [regex]::Escape($b.Cls) }
    for ($round = 0; $round -lt 4; $round++) {
        $dx = Invoke-EnemyDump $H ($b.Name + '-g' + $round)
        $left = @($dx.Values | Where-Object { $_.State -eq 'ALIVE' -and $_.Cls -match $grp })
        if ($left.Count -eq 0) { break }
        foreach ($e in $left) {
            Note "    later phase still alive: $($e.Key) $($e.Cls) hp=$($e.Hp) -- striking it"
            Set-Control $H 'qd.hitguid' ($e.Key + ':1000000#' + (Get-Date -Format 'HHmmssfff'))
            Start-Sleep -Seconds 3
        }
        Start-Sleep -Seconds 6
    }
    $dh3 = Invoke-EnemyDump $H ($b.Name + '-h3')
    $dp3 = Invoke-EnemyDump $P ($b.Name + '-p3')
    $h3 = $dh3[$bh.Id]; $p3 = $dp3[$bh.Id]
    # No row is "not alive" -- a mirrored copy is retired with its key on death.
    Verdict $step $H ([bool](-not $h3 -or $h3.State -ne 'ALIVE')) 'the boss is dead on the HOST' $(if ($h3) { $h3.Line } else { 'no row (the copy was retired)' })
    Verdict $step $P ([bool](-not $p3 -or $p3.State -ne 'ALIVE')) 'the boss is dead on the PEER' $(if ($p3) { $p3.Line } else { 'no row (no live copy)' })
    [void](Check $step $H ('boss: (sent bar off|bar lowered|DeactivateBossHPBar called)') 30 'the HOST bar came down' $mH)
    [void](Check $step $P ('boss: (bar lowered|DeactivateBossHPBar called)|ActivateBossHPBar called.*off') 30 'the PEER bar came down' $mP)
    if ($b.Talk) {
        # THE DOWNED BOSS IS CONCLUDED BY A CONVERSATION (Session 29).  The host's player "talks"
        # to it: the rig runs the dialogue's end event on the host's copy (the conversation's
        # own effect); the peer must conclude its copy from the flag (`boss conclusion:`).
        $mT = Get-Mark $P
        # The death sequence first (the Necronomicon plays its death montage, then leaves its
        # dialogue NPC; talking during the montage did nothing, Session 29 run 7).
        if ($b.PSObject.Properties['TalkWait'] -and $b.TalkWait) { Start-Sleep -Seconds ([int]$b.TalkWait) }
        foreach ($tk in @($b.Talk)) {
            $talkAt = if ($tk -match '!') { $tk } else { $b.Cls + '!' + $tk }
            Set-Control $H 'qd.act' ('near:' + $talkAt + '#' + (Get-Date -Format 'HHmmssfff'))
            Start-Sleep -Seconds 4
        }
        Start-Sleep -Seconds 4
        Show-Since $H 'rig: act|boss death watch' $mH 6
        [void](Check $step $P 'boss conclusion: .* ran on the downed copy' 60 'the PEER concluded its downed copy from the partner''s flag' $mT)
        Start-Sleep -Seconds 6
        Show-Since $P 'boss death watch|boss conclusion' $mT 8
    }
    if ($b.Flag) {
        [void](Check $step $H ('flag (NEW |CHANGE )?' + $b.Flag + '( =| :)') 90 "the HOST's own save holds $($b.Flag)" $mH0)
        [void](Check $step $P ('flag (NEW |CHANGE )?' + $b.Flag + '( =| :)|flags applied=.*' ) 40 "the PEER's own save received the flag lane" $mP0)
        foreach ($i in @($H, $P)) {
            $m = Get-Mark $i
            Set-Control $i 'qd.flag' ($b.Flag + '#' + (Get-Date -Format 'HHmmssfff'))
            $ln = @(Wait-Since $i ('rig: flag dump -- ' + $b.Flag + ' = ') $m 15)
            $ok = $ln.Count -gt 0 -and ([string]$ln[-1]) -match (' = ([1-9]\d*)( \(game bank.*\))?$')
            Verdict $step $i $ok ("$($b.Flag) >= 1 in this machine's bank") $(if ($ln.Count) { ([string]$ln[-1]).Trim() } else { 'no answer' })
        }
    }
    if ($hubPos) {
        Set-Control $H 'qd.tp' (("{0:F0},{1:F0},{2:F0},0#" -f $hubPos[0], $hubPos[1], ($hubPos[2] + 50)) + (Get-Date -Format 'HHmmssfff'))
        Note ("    the HOST walks back to its hub spot {0:F0},{1:F0},{2:F0} before leaving the area" -f $hubPos[0], $hubPos[1], $hubPos[2])
        Start-Sleep -Seconds 4
    }
    Leave-Area $H $P $step | Out-Null
    $mv = @(Wait-Since $P 'travel: the partner left .* moved beside the partner' $mP0 10)
    Note ("    PEER on the leave: " + $(if ($mv.Count) { ([string]$mv[-1]).Trim() } else { 'no move line (it was already beside the partner, or the position was unknown)' }))
    Start-Sleep -Seconds 5
}

function Invoke-BossTour([int]$H, [int]$P) {
    $m0 = Get-Mark $H
    $want = @($script:Bosses)
    if ($Only) { $want = @($want | Where-Object { $Only -split ',' -contains $_.Name }) }
    foreach ($b in $want) {
        if (-not (Test-Alive $H) -or -not (Test-Alive $P)) { Say 'an instance died; the tour stops'; break }
        try { Invoke-OneBoss $H $P $b }
        catch { Say ("boss {0}: harness exception {1}" -f $b.Name, $_.Exception.Message) }
    }
    Invoke-EndChecks $H $P 'bosses' $m0
}


# ===========================================================================
# STORY AND THE BOSSES THAT NEVER SPAWNED (Session 28)
# ===========================================================================
# (1) A flag the partner sets reaches this machine's world through the game's own quest
#     refresh (GI_UpdateQuestState + GI_NotifyWorldStateChanged): the host writes it the way a
#     quest node does (SaveFunctions.SaveNamedData), the peer's quest manager keyed on it must
#     change state.
# (2) For each boss whose start trigger spawned nothing in Session 26, the probe tells which of
#     the spawner's own verbs starts it, and whether the partner gets the boss.
$script:ProbeBosses = @(
    [pscustomobject]@{ Name = 'Necronomicon'; Area = '300_LIB'; Start = 'Necronomicon_Boss_Spawner'; Spawner = 'Necronomicon_Boss_Spawner'; Cls = 'Necronomicon_BP'; Flag = 'NecronimiconDefeated'; Verbs = @('Try+Spawning+Enemy', 'CustomEvent', 'Interact') }
    [pscustomobject]@{ Name = 'BridgeLord';   Area = '500_AUT'; Start = 'JosamiLord_Spawner';        Spawner = 'JosamiLord_Spawner';        Cls = 'JosamiLord_BP';   Flag = 'BridgeLordDead';       Verbs = @('~BndEvt__JosamiLord_Spawner_BridgeCenterTrigger', 'Try+Spawning+Enemy', 'Activate') }
)

function Test-Presence([int]$i, [string]$cls) {
    # The enemy table first (a dormant boss may not match by actor name), then the name.
    $d = Invoke-EnemyDump $i ('pres-' + $cls)
    $r = Find-BossKey $d $cls
    if ($r) { return ('resolved by the enemy table: ' + $r.Line) }
    $m = Get-Mark $i
    Set-Control $i 'qd.gate' ('near:' + $cls + '#' + (Get-Date -Format 'HHmmssfff'))
    $ln = @(Wait-Since $i ('rig: gate -- (resolved|no live actor matches)') $m 15)
    if (-not $ln.Count) { return 'no answer' }
    return ([string]$ln[-1]).Trim()
}

function Invoke-QuestRefresh([int]$H, [int]$P) {
    $step = 'quest-refresh'
    if (-not (Enter-Area $H $P '300_LIB' '' $step)) { return }
    Start-Sleep -Seconds 6
    $mH = Get-Mark $H
    $mP = Get-Mark $P
    Set-Control $H 'qd.flag' ('set:LibrarianQuestDone=1#' + (Get-Date -Format 'HHmmssfff'))
    [void](Check $step $H 'rig: flag set -- SaveFunctions.SaveNamedData\(LibrarianQuestDone, 1, RefreshQuestStates=1\) ran' 20 "the HOST wrote the flag through the game's own SaveNamedData" $mH)
    [void](Check $step $H 'flag (NEW |CHANGE )?LibrarianQuestDone' 20 "the HOST's mirror saw its own game write" $mH)
    [void](Check $step $P 'quest refresh: GI_UpdateQuestState ran for [1-9]\d* of .*LibrarianQuestDone.* GI_NotifyWorldStateChanged ran' 30 "the PEER ran the game's quest refresh for the partner's flag" $mP)
    [void](Check $step $P 'quest state: LibrarianQuestStateManager \(LibrarianQuestDone\) InternalState .* -- the quest followed' 30 "the PEER's Librarian quest manager changed state" $mP)
    Show-Since $P 'quest refresh|quest state|flag refresh' $mP 8
    Save-Shots 'quest'
    Leave-Area $H $P $step | Out-Null
    Start-Sleep -Seconds 4
}

function Invoke-BossProbe([int]$H, [int]$P, $b) {
    $step = 'probe-' + $b.Name
    Step "$step -- what starts $($b.Name) in $($b.Area)" "The trigger first (as Session 26), then each of the spawner's own verbs on the HOST; after each, both machines are asked whether the boss exists."
    $hubPos = Get-LastPos $H 0
    if (-not (Enter-Area $H $P $b.Area '' $step)) { return }
    $mH = Get-Mark $H
    $mP0 = Get-Mark $P
    Set-Control $H 'qd.gate' ('near:' + $b.Spawner + '#' + (Get-Date -Format 'HHmmssfff'))
    Start-Sleep -Seconds 3
    Show-Since $H 'rig: gate|state' $mH 30
    $mH = Get-Mark $H; $mP = Get-Mark $P
    Set-Control $H 'qd.trigger' ($b.Start + '@' + $b.Area + '#' + (Get-Date -Format 'HHmmssfff'))
    Start-Sleep -Seconds 12
    Show-Since $H 'rig: trigger|trigger:|gate' $mH 10
    Show-Since $P 'trigger:' $mP 6
    $ph = Test-Presence $H $b.Cls; $pp = Test-Presence $P $b.Cls
    Note "    after the trigger: HOST $ph"
    Note "    after the trigger: PEER $pp"
    $got = $ph -match 'resolved'
    foreach ($v in $b.Verbs) {
        if ($got) { break }
        $mH = Get-Mark $H; $mP = Get-Mark $P
        Invoke-Go $H $b.Spawner 300
        Start-Sleep -Seconds 2
        Set-Control $H 'qd.act' ('near:' + $b.Spawner + '!' + $v + '#' + (Get-Date -Format 'HHmmssfff'))
        $ran = @(Wait-Since $H ('dev interact: forced verb ' + [regex]::Escape($v)) $mH 15)
        Note ("    verb {0}: {1}" -f $v, $(if ($ran.Count) { ([string]$ran[-1]).Trim() } else { 'no answer' }))
        Start-Sleep -Seconds 10
        $ph = Test-Presence $H $b.Cls; $pp = Test-Presence $P $b.Cls
        Note "      HOST $ph"
        Note "      PEER $pp"
        Show-Since $H 'boss:|spawn|enemykey' $mH 6
        Show-Since $P 'boss:|spawn|trigger:' $mP 6
        $got = $ph -match 'resolved'
        if ($got) { Note "    ==> $($b.Name) started on the HOST by $v" }
    }
    if ($got -and $pp -notmatch 'resolved') {
        Start-Sleep -Seconds 15
        $pp = Test-Presence $P $b.Cls
        Note "      PEER after the partner-boss start: $pp"
    }
    Show-Since $P 'boss start:' $mP0 8
    Verdict $step $H ([bool]$got) "the HOST has a $($b.Cls)" $ph
    Verdict $step $P ([bool]($pp -match 'resolved')) "the PEER has a $($b.Cls)" $pp
    $dh = Invoke-EnemyDump $H ($b.Name + '-ph')
    $dp = Invoke-EnemyDump $P ($b.Name + '-pp')
    $bh = Find-BossKey $dh $b.Cls; $bp = Find-BossKey $dp $b.Cls
    Note ("    enemy rows: HOST {0}; PEER {1}" -f $(if ($bh) { $bh.Line } else { '-' }), $(if ($bp) { $bp.Line } else { '-' }))
    Save-Shots ('probe-' + $b.Name)
    if ($bh) {
        Verdict $step $P ([bool]($bp -and $bp.Key -eq $bh.Key)) 'the PEER holds the SAME boss key' $(if ($bp) { $bp.Line } else { '' })
        $mK = Get-Mark $H
        Set-Control $H 'qd.hitguid' ($bh.Key + ':1000000#' + (Get-Date -Format 'HHmmssfff'))
        [void](Check $step $H ('rig: hitguid -> key=' + $bh.Key) 20 'the HOST struck the killing blow' $mK)
        Start-Sleep -Seconds 15
        foreach ($i in @($H, $P)) {
            $m = Get-Mark $i
            Set-Control $i 'qd.flag' ($b.Flag + '#' + (Get-Date -Format 'HHmmssfff'))
            $ln = @(Wait-Since $i ('rig: flag dump -- ' + $b.Flag + ' = ') $m 15)
            $ok = $ln.Count -gt 0 -and ([string]$ln[-1]) -match (' = ([1-9]\d*)( \(game bank.*\))?$')
            Verdict $step $i $ok ("$($b.Flag) >= 1 in this machine's bank") $(if ($ln.Count) { ([string]$ln[-1]).Trim() } else { 'no answer' })
        }
    }
    if ($hubPos) {
        Set-Control $H 'qd.tp' (("{0:F0},{1:F0},{2:F0},0#" -f $hubPos[0], $hubPos[1], ($hubPos[2] + 50)) + (Get-Date -Format 'HHmmssfff'))
        Start-Sleep -Seconds 4
    }
    Leave-Area $H $P $step | Out-Null
    Start-Sleep -Seconds 5
}

function Invoke-StoryPhase([int]$H, [int]$P) {
    $m0 = Get-Mark $H
    if (-not $Only -or ($Only -split ',' -contains 'Quest')) { Invoke-QuestRefresh $H $P }
    # The probes (kill, no conversation) are opt-in by 'probe:<Name>' since Session 29: the tour
    # now fights these bosses through their conversation, and a probe kill first would leave
    # the tour nothing to fight.
    $want = @($script:ProbeBosses)
    if ($Only) { $want = @($want | Where-Object { $Only -split ',' -contains ('probe:' + $_.Name) }) } else { $want = @() }
    foreach ($b in $want) {
        if (-not (Test-Alive $H) -or -not (Test-Alive $P)) { Say 'an instance died; the probe stops'; break }
        try { Invoke-BossProbe $H $P $b } catch { Say ("probe {0}: harness exception {1}" -f $b.Name, $_.Exception.Message) }
    }
    $tour = @($script:Bosses | Where-Object { $_.Name -in @('ROSDragon', 'GiantFish', 'WhaleKing') })
    if ($Only) { $tour = @($script:Bosses | Where-Object { $Only -split ',' -contains $_.Name }) }
    foreach ($b in $tour) {
        if (-not (Test-Alive $H) -or -not (Test-Alive $P)) { break }
        try { Invoke-OneBoss $H $P $b } catch { Say ("boss {0}: harness exception {1}" -f $b.Name, $_.Exception.Message) }
    }
    Invoke-EndChecks $H $P 'story' $m0
}

# ===========================================================================
# main
# ===========================================================================
$script:RunStartedAt = Get-Date   # a crash folder older than this run is not this run's crash
Say ("session26 start: phase={0} switches={1} soak={2}s rigHops={3}" -f $Phase,
     $(if ($ShippedDefaults) { 'SHIPPED' } else { 'DOCUMENTED PLAY' }), $SoakSeconds, $RigHops)
Stop-All ("phase start: clearing anything left running before the pair is seeded (phase '$Phase')")
Seed-FreshPair
Start-Boot $Phase
$H = $script:H
$P = $script:P

if ($Phase -eq 'probe' -or $Phase -eq 'all') {
    Invoke-Probe $H $P
}

if ($Phase -eq 'hardloop' -or $Phase -eq 'all') {
    Invoke-HardLoop $H $P $HardLoads
}

if ($Phase -eq 'loot') {
    Invoke-LootPhase $H $P
}

if ($Phase -eq 'rig') {
    Invoke-RigPhase $H $P
}

if ($Phase -eq 'smoke' -or $Phase -eq 'all') {
    Invoke-Smoke $H $P
}

if ($Phase -eq 'soak' -or $Phase -eq 'all') {
    Invoke-Soak $H $P
}

if ($Phase -eq 'bosses') {
    Invoke-BossTour $H $P
}

if ($Phase -eq 'gaps') {
    Invoke-GapsPhase $H $P
}

if ($Phase -eq 'story') {
    Invoke-StoryPhase $H $P
}

Step 'SESSION SURVIVAL'
foreach ($i in @($H, $P)) {
    $alive = Test-Alive $i
    $script:verdicts.Add([pscustomobject]@{ Step = 'survival'; Inst = $i; What = 'alive at the end'; Ok = $alive; Line = '' }) | Out-Null
    Note "  $(if ($alive) { 'PASS' } else { 'FAIL' }) [$i] process alive=$alive"
    $fault = @(Get-Matches $i 'ACCESS_VIOLATION|EXCEPTION|fatal|\bfault')
    Note "  [$i] FAULT lines (ACCESS_VIOLATION/EXCEPTION/fatal): $($fault.Count)"
}
Stop-All ("end of phase '" + $Phase + "': the run finished normally, so this kill is the " +
          "harness' own teardown and not a fault")

Note ''
Note '=== VERDICTS ==='
foreach ($v in $script:verdicts) {
    Note ("  {0,-12} inst{1}  {2,-6}  {3}  {4}" -f $v.Step, $v.Inst, $(if ($v.Ok) { 'PASS' } else { 'FAIL' }), $v.What, $v.Line)
}
$pass = @($script:verdicts | Where-Object { $_.Ok }).Count
$fail = @($script:verdicts | Where-Object { -not $_.Ok }).Count
Note ("=== {0} PASS, {1} FAIL ===" -f $pass, $fail)
$script:lines | Set-Content -LiteralPath $Report
