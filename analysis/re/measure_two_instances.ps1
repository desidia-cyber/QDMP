<#
.SYNOPSIS
    E2 measurement rig: launch TWO isolated QueensDomain instances and sample
    their memory every N seconds into a CSV.

.DESCRIPTION
    PREPARED, NOT RUN.  Nothing here is executed by the analysis pass; run it
    yourself on the target machine.

    Isolation technique is `_live\make-inst1.ps1`'s, extended to two roots:
      * junction Engine, Content\Paks, Content\Movies from the real install
        (read-only reuse, no 1.9 GB duplication),
      * hard-link the shipping exe (same volume, no copy),
      * one separate -UserDir per instance, so neither can touch
        %LOCALAPPDATA%\QueensDomain\Saved,
      * -saveddirsuffix on top, which the binary also understands.

    The script records the real save directory before launch and re-checks it
    after, so a failed isolation is reported rather than discovered later.

    Sampling writes one CSV row per instance per tick:
      WorkingSet64, PrivateMemorySize64, PeakWorkingSet64, threads, handles,
      plus machine-wide available physical memory.

    Memory figures come from GlobalMemoryStatusEx, because WMI/CIM is denied to
    this session (Get-CimInstance Win32_ComputerSystem -> Access denied), which
    is how the earlier "22 GB machine" reading went wrong.

.EXAMPLE
    # dry run: build the two roots, seed the configs, print the command lines,
    # launch nothing
    .\measure_two_instances.ps1 -DryRun

.EXAMPLE
    # the real measurement: 15 minutes, one sample every 2 s
    .\measure_two_instances.ps1 -DurationSeconds 900 -SampleSeconds 2

.NOTES
    Acceptance criterion is printed at the end and encoded in the exit code:
      0 = all criteria met
      1 = both ran but memory headroom failed
      2 = preflight refused (build guard / isolation / missing files)
      3 = at least one instance died during the window
#>
[CmdletBinding()]
param(
    [string] $Root      = 'G:\Freespace\Queens_Domain\_live',
    [string] $RealInstall = 'G:\Freespace\Queens_Domain\QueensDomain',
    [string] $Exe       = 'G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64\QueensDomain-Win64-Shipping.exe',
    [string] $CsvPath,
    [int]    $SampleSeconds   = 2,
    [int]    $DurationSeconds = 900,
    # free-memory margin required at peak, as a fraction of physical RAM
    [double] $MarginFraction  = 0.25,
    # a process counts as "in gameplay" once its working set exceeds this
    [double] $GameplayWorkingSetGb = 8.0,
    [switch] $DryRun,
    [switch] $Force,           # rebuild instance roots if they already exist
    [switch] $SkipBuildGuard,
    [switch] $KeepInstances
)

$ErrorActionPreference = 'Stop'
$script:Failures = @()

function Write-Step([string]$m) { Write-Host ("==> " + $m) -ForegroundColor Cyan }
function Write-Note([string]$m) { Write-Host ("    " + $m) }
function Add-Failure([string]$m) {
    $script:Failures += $m
    Write-Host ("!!  " + $m) -ForegroundColor Yellow
}

# --------------------------------------------------------------- memory
if (-not ('QdMem' -as [type])) {
    Add-Type -Namespace Qd -Name Mem -MemberDefinition @'
[System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
public struct MEMSTATUS {
    public uint dwLength; public uint dwMemoryLoad;
    public ulong ullTotalPhys; public ulong ullAvailPhys;
    public ulong ullTotalPageFile; public ulong ullAvailPageFile;
    public ulong ullTotalVirtual; public ulong ullAvailVirtual;
    public ulong ullAvailExtendedVirtual;
}
[System.Runtime.InteropServices.DllImport("kernel32.dll", SetLastError=true)]
public static extern bool GlobalMemoryStatusEx(ref MEMSTATUS s);
'@
}
function Get-MemStatus {
    $s = New-Object Qd.Mem+MEMSTATUS
    $s.dwLength = [uint32][System.Runtime.InteropServices.Marshal]::SizeOf($s)
    if (-not [Qd.Mem]::GlobalMemoryStatusEx([ref]$s)) { throw 'GlobalMemoryStatusEx failed' }
    [pscustomobject]@{
        TotalBytes = [double]$s.ullTotalPhys
        AvailBytes = [double]$s.ullAvailPhys
        LoadPct    = [int]$s.dwMemoryLoad
    }
}

# --------------------------------------------------------------- snapshots
function Get-SaveSnapshot([string]$dir) {
    if (-not (Test-Path $dir)) { return @() }
    Get-ChildItem $dir -Recurse -File -ErrorAction SilentlyContinue |
        Sort-Object FullName |
        ForEach-Object {
            [pscustomobject]@{
                Path  = $_.FullName.Substring($dir.Length)
                Bytes = $_.Length
                Write = $_.LastWriteTimeUtc.ToString('o')
            }
        }
}

function Compare-SaveSnapshot($before, $after) {
    $b = @{}; foreach ($x in $before) { $b[$x.Path] = $x }
    $changed = @()
    foreach ($x in $after) {
        if (-not $b.ContainsKey($x.Path)) { $changed += ("new:" + $x.Path) }
        elseif ($b[$x.Path].Write -ne $x.Write -or $b[$x.Path].Bytes -ne $x.Bytes) {
            $changed += ("modified:" + $x.Path)
        }
    }
    $seen = @{}; foreach ($x in $after) { $seen[$x.Path] = $true }
    foreach ($k in $b.Keys) { if (-not $seen.ContainsKey($k)) { $changed += ("removed:" + $k) } }
    ,$changed
}

# --------------------------------------------------------------- preflight
Write-Step 'preflight'

if (-not (Test-Path $Exe)) { Write-Host "exe not found: $Exe" -ForegroundColor Red; exit 2 }
$exeItem = Get-Item $Exe
Write-Note ("exe      {0}" -f $exeItem.FullName)
Write-Note ("size     {0:N0} bytes" -f $exeItem.Length)
try {
    $sha = (Get-FileHash -LiteralPath $Exe -Algorithm SHA256).Hash
    Write-Note ("sha256   {0}" -f $sha)
} catch { Write-Note "sha256   <unavailable: $($_.Exception.Message)>" }

if (-not $SkipBuildGuard) {
    $guard = Join-Path $PSScriptRoot 'build_guard.py'
    if ((Test-Path $guard) -and (Get-Command python -ErrorAction SilentlyContinue)) {
        $gj = Join-Path $Root 'build_guard.e2.json'
        & python $guard $Exe --json $gj --quiet | Out-Null
        $grc = $LASTEXITCODE
        if ($grc -ne 0) {
            Write-Note "build guard: REFUSE (rc=$grc) - the hook table does not match this image"
            Write-Host 'refusing to measure a build the mod cannot hook' -ForegroundColor Red
            exit 2
        }
        Write-Note 'build guard: PASS'
    } else {
        Write-Note 'build guard: skipped (python or build_guard.py unavailable)'
    }
}

$mem0 = Get-MemStatus
$ramGb = $mem0.TotalBytes / 1GB
$availGb = $mem0.AvailBytes / 1GB
Write-Note ("physical {0:N2} GiB   available {1:N2} GiB   load {2}%" -f $ramGb, $availGb, $mem0.LoadPct)
if ($ramGb -lt 1) { Write-Host 'could not read physical memory' -ForegroundColor Red; exit 2 }

$realSaved = Join-Path $env:LOCALAPPDATA 'QueensDomain\Saved'
Write-Note ("real save dir: {0}" -f $realSaved)
$realBefore = Get-SaveSnapshot $realSaved
Write-Note ("real save files before: {0}" -f $realBefore.Count)

# --------------------------------------------------------------- instances
$instances = @(
    [pscustomobject]@{ Name = 'A'; Root = (Join-Path $Root 'instA'); UserDir = (Join-Path $Root 'instAsaved'); Suffix = 'qdA'; Proc = $null },
    [pscustomobject]@{ Name = 'B'; Root = (Join-Path $Root 'instB'); UserDir = (Join-Path $Root 'instBsaved'); Suffix = 'qdB'; Proc = $null }
)

$settings = @'
[Internationalization]
Culture=en

[ScalabilityGroups]
sg.ResolutionQuality=50.000000
sg.ViewDistanceQuality=0
sg.AntiAliasingQuality=0
sg.ShadowQuality=0
sg.PostProcessQuality=0
sg.TextureQuality=0
sg.EffectsQuality=0
sg.FoliageQuality=0
sg.ShadingQuality=0

[/Script/Engine.GameUserSettings]
bUseVSync=False
bUseDynamicResolution=False
ResolutionSizeX=640
ResolutionSizeY=360
LastUserConfirmedResolutionSizeX=640
LastUserConfirmedResolutionSizeY=360
WindowPosX=2200
WindowPosY=1400
FullscreenMode=2
LastConfirmedFullscreenMode=2
'@

Write-Step 'building isolated instance roots'
foreach ($i in $instances) {
    $wd = Join-Path $i.Root 'QueensDomain\Binaries\Win64'
    if (Test-Path $i.Root) {
        if (-not $Force) {
            Write-Note ("exists, reusing: {0}" -f $i.Root)
        } else {
            Write-Note ("removing: {0}" -f $i.Root)
            Remove-Item $i.Root -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
    New-Item -ItemType Directory -Force $wd | Out-Null
    New-Item -ItemType Directory -Force (Join-Path $i.Root 'QueensDomain\Content') | Out-Null

    foreach ($j in @(
        @{ Link = (Join-Path $i.Root 'Engine');                  Target = (Join-Path $RealInstall 'Engine') },
        @{ Link = (Join-Path $i.Root 'QueensDomain\Content\Paks');   Target = (Join-Path $RealInstall 'QueensDomain\Content\Paks') },
        @{ Link = (Join-Path $i.Root 'QueensDomain\Content\Movies'); Target = (Join-Path $RealInstall 'QueensDomain\Content\Movies') }
    )) {
        if (-not (Test-Path $j.Link)) {
            cmd /c mklink /J "$($j.Link)" "$($j.Target)" | Out-Null
        }
    }

    $dst = Join-Path $wd 'QueensDomain-Win64-Shipping.exe'
    if (-not (Test-Path $dst)) {
        cmd /c mklink /H "$dst" "$Exe" | Out-Null
        if (-not (Test-Path $dst)) { Copy-Item $Exe $dst }
    }

    $cfg = Join-Path $i.UserDir 'Saved\Config\WindowsNoEditor\GameUserSettings.ini'
    New-Item -ItemType Directory -Force (Split-Path $cfg) | Out-Null
    if (-not (Test-Path $cfg)) { Set-Content -LiteralPath $cfg -Value $settings -Encoding UTF8 }
    New-Item -ItemType Directory -Force (Join-Path $i.UserDir 'Saved\SaveGames') | Out-Null

    $i | Add-Member -NotePropertyName BinDir -NotePropertyValue $wd -Force
    $i | Add-Member -NotePropertyName ExePath -NotePropertyValue $dst -Force
    Write-Note ("{0}: {1}  userdir={2}" -f $i.Name, $i.Root, $i.UserDir)
}

# isolation must be provable before anything launches
foreach ($i in $instances) {
    if ($i.UserDir.TrimEnd('\') -eq $realSaved.TrimEnd('\')) {
        Add-Failure ("instance {0} -UserDir resolves to the real save dir" -f $i.Name)
    }
    if (-not (Test-Path $i.ExePath)) { Add-Failure ("instance {0}: exe missing" -f $i.Name) }
    if (-not (Test-Path (Join-Path $i.Root 'QueensDomain\Content\Paks'))) {
        Add-Failure ("instance {0}: Content\Paks junction missing" -f $i.Name)
    }
}
if ($script:Failures.Count) { Write-Host 'preflight refused' -ForegroundColor Red; exit 2 }

$argsFor = {
    param($i)
    @(
        "-UserDir=`"$($i.UserDir)`"",
        "-saveddirsuffix=$($i.Suffix)",
        '-SaveToUserDir',
        '-windowed'
    )
}

Write-Step 'launch plan'
foreach ($i in $instances) {
    $a = (& $argsFor $i) -join ' '
    Write-Note ("{0}: `"{1}`" {2}" -f $i.Name, $i.ExePath, $a)
    Write-Note ("   -WorkingDirectory `"{0}`"" -f $i.BinDir)
}

if ($DryRun) {
    Write-Step 'dry run - nothing launched'
    Write-Note 'append-only instance cost on disk: two roots of ~116 MB each (the exe hard-link is free); save dirs start empty'
    Write-Note ("sampling would write {0} rows/s to {1}" -f (2 / $SampleSeconds), $CsvPath)
    exit 0
}

# --------------------------------------------------------------- launch
if (-not $CsvPath) {
    $CsvPath = Join-Path $Root ("two-instances-{0}.csv" -f (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
$csvDir = Split-Path -Parent $CsvPath
if ($csvDir -and -not (Test-Path $csvDir)) { New-Item -ItemType Directory -Force $csvDir | Out-Null }

Write-Step 'launching'
foreach ($i in $instances) {
    $a = & $argsFor $i
    $i.Proc = Start-Process -FilePath $i.ExePath -ArgumentList $a `
                            -WorkingDirectory $i.BinDir -PassThru
    Write-Note ("{0}: pid {1}" -f $i.Name, $i.Proc.Id)
    Start-Sleep -Seconds 3
}

# --------------------------------------------------------------- sample
Write-Step ("sampling every {0}s for up to {1}s -> {2}" -f $SampleSeconds, $DurationSeconds, $CsvPath)
$cols = 'timestamp','sample','instance','pid','alive','working_set_mb','private_mb',
        'peak_ws_mb','threads','handles','responding',
        'total_phys_gb','avail_phys_gb','load_pct','saved_files','savegames'
$deadline = (Get-Date).AddSeconds($DurationSeconds)
$sample = 0
$peaks = @{}
$dead = @()

while ((Get-Date) -lt $deadline) {
    $sample++
    $stamp = (Get-Date).ToString('o')
    $m = Get-MemStatus
    foreach ($i in $instances) {
        $p = Get-Process -Id $i.Proc.Id -ErrorAction SilentlyContinue
        $alive = $null -ne $p
        if ($alive) {
            $p.Refresh()
            $row = [pscustomobject]@{
                timestamp      = $stamp
                sample         = $sample
                instance       = $i.Name
                pid            = $p.Id
                alive          = $true
                working_set_mb = [math]::Round($p.WorkingSet64 / 1MB, 1)
                private_mb     = [math]::Round($p.PrivateMemorySize64 / 1MB, 1)
                peak_ws_mb     = [math]::Round($p.PeakWorkingSet64 / 1MB, 1)
                threads        = $p.Threads.Count
                handles        = $p.HandleCount
                responding     = $p.Responding
                total_phys_gb  = [math]::Round($m.TotalBytes / 1GB, 2)
                avail_phys_gb  = [math]::Round($m.AvailBytes / 1GB, 2)
                load_pct       = $m.LoadPct
                saved_files    = (Get-ChildItem (Join-Path $i.UserDir 'Saved') -Recurse -File -ErrorAction SilentlyContinue | Measure-Object).Count
                savegames      = (Get-ChildItem (Join-Path $i.UserDir 'Saved\SaveGames') -File -ErrorAction SilentlyContinue | Measure-Object).Count
            }
            $k = $i.Name
            if (-not $peaks.ContainsKey($k)) { $peaks[$k] = @{ ws = 0.0; priv = 0.0; peak = 0.0 } }
            $peaks[$k].ws   = [math]::Max($peaks[$k].ws,   $row.working_set_mb)
            $peaks[$k].priv = [math]::Max($peaks[$k].priv, $row.private_mb)
            $peaks[$k].peak = [math]::Max($peaks[$k].peak, $row.peak_ws_mb)
        } else {
            if ($dead -notcontains $i.Name) { $dead += $i.Name }
            $row = [pscustomobject]@{
                timestamp = $stamp; sample = $sample; instance = $i.Name; pid = $i.Proc.Id
                alive = $false; working_set_mb = 0; private_mb = 0; peak_ws_mb = 0
                threads = 0; handles = 0; responding = $false
                total_phys_gb = [math]::Round($m.TotalBytes / 1GB, 2)
                avail_phys_gb = [math]::Round($m.AvailBytes / 1GB, 2)
                load_pct = $m.LoadPct; saved_files = 0; savegames = 0
            }
        }
        if ($sample -eq 1) {
            $row | Export-Csv -LiteralPath $CsvPath -NoTypeInformation -Encoding UTF8
        } else {
            $row | Export-Csv -LiteralPath $CsvPath -NoTypeInformation -Encoding UTF8 -Append
        }
    }
    Start-Sleep -Seconds $SampleSeconds
}

# --------------------------------------------------------------- teardown
Write-Step 'stopping instances'
foreach ($i in $instances) {
    $p = Get-Process -Id $i.Proc.Id -ErrorAction SilentlyContinue
    if ($p) {
        Write-Note ("{0}: stop pid {1}" -f $i.Name, $p.Id)
        Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
    }
}
Start-Sleep -Seconds 4

# --------------------------------------------------------------- verdict
Write-Step 'results'
$combinedPeakGb = 0.0
foreach ($i in $instances) {
    $k = $i.Name
    if ($peaks.ContainsKey($k)) {
        $combinedPeakGb += $peaks[$k].priv / 1024.0
        $pk = $peaks[$k]
        Write-Note ("{0}: peak WS {1:N0} MB   peak private {2:N0} MB   peak-of-PeakWS {3:N0} MB" -f $k, $pk.ws, $pk.priv, $pk.peak)
    } else {
        Write-Note ("{0}: no samples" -f $k)
    }
}
$ramGb = [math]::Round((Get-MemStatus).TotalBytes / 1GB, 2)
$freeRequiredGb = $ramGb * $MarginFraction
$budgetGb = $ramGb - $freeRequiredGb
Write-Note ("combined peak private {0:N1} GB of {1:N1} GB physical" -f $combinedPeakGb, $ramGb)
Write-Note ("headroom required {0:N1} GB (margin {1:P0}) -> combined must stay below {2:N1} GB" -f $freeRequiredGb, $MarginFraction, $budgetGb)

$realAfter = Get-SaveSnapshot $realSaved
$realChanged = Compare-SaveSnapshot $realBefore $realAfter
if ($realChanged.Count) {
    Add-Failure ("the REAL save directory changed: " + (($realChanged | Select-Object -First 5) -join '; '))
} else {
    Write-Note 'real save directory: unchanged'
}

foreach ($i in $instances) {
    if ($dead -contains $i.Name) { Add-Failure ("instance {0} died during the window" -f $i.Name) }
    if (-not $peaks.ContainsKey($i.Name) -or $peaks[$i.Name].ws -lt ($GameplayWorkingSetGb * 1024)) {
        Write-Note ("{0}: peak working set below {1:N0} GB - did not reach gameplay (or never loaded a level)" -f $i.Name, $GameplayWorkingSetGb)
    } else {
        Write-Note ("{0}: reached the gameplay working-set threshold" -f $i.Name)
    }
}
if ($combinedPeakGb -gt $budgetGb) {
    Add-Failure ("combined private {0:N1} GB exceeds the {1:N1} GB budget" -f $combinedPeakGb, $budgetGb)
}

if (-not $KeepInstances) {
    Write-Note 'instance roots kept (use -Force on the next run to rebuild); no user data is inside them'
}

Write-Step ('CSV: ' + $CsvPath)
if ($script:Failures.Count) {
    Write-Host ("RESULT: FAIL ({0} issue(s))" -f $script:Failures.Count) -ForegroundColor Red
    if ($dead.Count) { exit 3 } else { exit 1 }
}
Write-Host 'RESULT: PASS - both instances ran, memory stayed inside the margin, real save untouched' -ForegroundColor Green
exit 0
