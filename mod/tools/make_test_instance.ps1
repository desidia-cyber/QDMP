#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Create one isolated instance of Queen's Domain for the two-machine test rig.

.DESCRIPTION
    An instance is a directory that owns a private save directory. The game is
    pointed at it with -UserDir, so two instances on two machines (or two users
    on one machine) never read or write the same save file.

    What this creates under <Root>\<Name>\:

        Saved\SaveGames\      a private copy of the seeded save files
        instance.json         identity: name, index, source hash, save hashes

    The source save is copied with read-only semantics and hashed before and
    after the copy. If the source bytes change, the script fails: the one rule
    of this rig is that seeding never touches the seed.

    Why -UserDir is the lever, and why -saveddirsuffix is also mentioned:
    UE4 resolves the save directory from FPaths::ProjectUserDir(), which
    -UserDir overrides; saves then land in <UserDir>\Saved\SaveGames. DollsMP
    measured that junctioning %LOCALAPPDATA%\...\SaveGames "would do nothing"
    because FPaths::ShouldSaveToUserDir() is already true, so -UserDir is the
    only thing that actually moves the save directory. The engine also accepts
    -saveddirsuffix=<suffix>, which renames the Saved directory itself to
    Saved_<suffix> inside the same user dir. Both are printed below; the rig
    uses -UserDir because it also moves config and logs.

.PARAMETER Name
    Instance name. Also the directory name, so it must be a safe identifier.

.PARAMETER Index
    Instance number, 1-based. Must be unique among the instances already present
    under -Root. It is recorded rather than used as a path, so a rig can assert
    "this is instance 2" instead of trusting whatever directory it was launched
    from.

.PARAMETER SourceSave
    File or directory to seed from. A directory contributes every regular file
    in it (the real save set is `World0.sav`, `System0.sav`, `Challenges`).

.PARAMETER Root
    Rig root. Defaults to <repo>\mod\testgame.

.PARAMETER Force
    Re-seed an existing instance, replacing its save files.

.EXAMPLE
    ./make_test_instance.ps1 -Name inst1 -Index 1 `
        -SourceSave 'G:\Freespace\Queens_Domain\analysis\save-backup-before-live'

.EXAMPLE
    ./make_test_instance.ps1 -Name inst1 -Index 1 -SourceSave ..\..\analysis\save-backup-before-live -Force
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Name,

    [Parameter(Mandatory = $true)]
    [ValidateRange(1, 64)]
    [int]$Index,

    [Parameter(Mandatory = $true)]
    [string]$SourceSave,

    [string]$Root = '',

    # The -saveddirsuffix the rig launches with (run-hidden.ps1 -Suffix).  The engine
    # then reads Saved_<suffix>\SaveGames, NOT Saved\SaveGames -- seeding only the
    # latter left the instance with no world save, so "Play" started a fresh game
    # (LIVE-TEST-LOG R19).  Empty = no suffix.
    [string]$Suffix = 'qd1',

    [switch]$Force
)

$ErrorActionPreference = 'Stop'

function Write-Head([string]$t) { Write-Host "`n=== $t ===" -ForegroundColor Cyan }
function Write-Ok([string]$t)   { Write-Host "  [ok]   $t" -ForegroundColor Green }
function Write-Warn2([string]$t){ Write-Host "  [warn] $t" -ForegroundColor Yellow }
function Write-Err2([string]$t) { Write-Host "  [FAIL] $t" -ForegroundColor Red }
function Write-Info([string]$t) { Write-Host "  [info] $t" -ForegroundColor Gray }

function Get-Sha256([string]$Path) {
    (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

function Get-RealPath([string]$Path) {
    $full = [System.IO.Path]::GetFullPath($Path)
    $item = Get-Item -LiteralPath $full -Force -ErrorAction SilentlyContinue
    if ($null -eq $item) { return $full.TrimEnd('\', '/') }
    return [System.IO.Path]::GetFullPath($item.FullName).TrimEnd('\', '/')
}

# The packaged layout nests the project one level deeper than the install root
# on this build, so the shipping exe is found rather than assumed. Candidate
# paths are tried first; a shallow scan of the install is the fallback.
function Find-ShippingExe([string]$RepoRoot) {
    $name = 'QueensDomain-Win64-Shipping.exe'
    $candidates = @(
        (Join-Path $RepoRoot "QueensDomain\Binaries\Win64\$name"),
        (Join-Path $RepoRoot "QueensDomain\QueensDomain\Binaries\Win64\$name"),
        (Join-Path $RepoRoot "QueensDomain\Binaries\Win64\QueensDomain.exe")
    )
    foreach ($c in $candidates) {
        if (Test-Path -LiteralPath $c -PathType Leaf) { return (Get-RealPath $c) }
    }
    $install = Join-Path $RepoRoot 'QueensDomain'
    if (Test-Path -LiteralPath $install -PathType Container) {
        $hit = Get-ChildItem -LiteralPath $install -Recurse -File -Filter $name `
            -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($hit) { return $hit.FullName }
    }
    return $null
}

# The real profile. Reading it to seed a rig is legitimate, writing into it is
# not, and this script only ever reads from -SourceSave.
$RealProfile = $null
if ($env:LOCALAPPDATA) {
    $RealProfile = Join-Path $env:LOCALAPPDATA 'QueensDomain\Saved\SaveGames'
}

# ------------------------------------------------------------------ preflight
Write-Head 'Preflight'

if ($Name -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]*$') {
    Write-Err2 "'$Name' is not a safe instance name (use letters, digits, dot, underscore, dash)"
    exit 1
}

if ([string]::IsNullOrWhiteSpace($Root)) {
    $Root = Join-Path (Split-Path -Parent $PSScriptRoot) 'testgame'
}
$Root = Get-RealPath $Root
Write-Ok "rig root      : $Root"

if (-not (Test-Path -LiteralPath $SourceSave)) {
    Write-Err2 "source save does not exist: $SourceSave"
    exit 1
}
$sourceResolved = Get-RealPath $SourceSave
Write-Ok "source save   : $sourceResolved"

if ($RealProfile) {
    $realResolved = Get-RealPath $RealProfile
    if ($sourceResolved.Equals($realResolved, [StringComparison]::OrdinalIgnoreCase) -or
        $sourceResolved.StartsWith($realResolved.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
        Write-Warn2 "source is the operator's REAL profile; it will be read, never written"
    }
}

# Collect the source files. Only regular files are taken: a stray subdirectory
# in a save folder is not part of the save set.
$sourceFiles = @()
if (Test-Path -LiteralPath $sourceResolved -PathType Container) {
    $sourceFiles = @(Get-ChildItem -LiteralPath $sourceResolved -File -Force |
        Where-Object { $_.Name -notlike '*.bak' })
} else {
    $sourceFiles = @(Get-Item -LiteralPath $sourceResolved -Force)
}
if ($sourceFiles.Count -eq 0) {
    Write-Err2 "source contains no files: $sourceResolved"
    exit 1
}
Write-Ok ("source files  : {0} ({1})" -f $sourceFiles.Count, (($sourceFiles | ForEach-Object { $_.Name }) -join ', '))

# ------------------------------------------------------------------ identity
Write-Head 'Instance identity'

$InstanceDir = Join-Path $Root $Name
$SavedName = if ($Suffix) { "Saved_$Suffix" } else { 'Saved' }
$SaveDir = Join-Path $InstanceDir "$SavedName\SaveGames"
$MetaPath = Join-Path $InstanceDir 'instance.json'

Write-Ok "instance dir  : $InstanceDir"
Write-Ok "save dir      : $SaveDir"

# An index identifies one actor in the rig. Two directories claiming it makes
# every later comparison ambiguous, which is the DollsMP lesson about silently
# loading the wrong slot, so it is a refusal rather than a warning.
if (Test-Path -LiteralPath $Root -PathType Container) {
    foreach ($other in (Get-ChildItem -LiteralPath $Root -Directory -Force)) {
        if ($other.Name -eq $Name) { continue }
        $otherMeta = Join-Path $other.FullName 'instance.json'
        if (-not (Test-Path -LiteralPath $otherMeta)) { continue }
        try {
            $doc = Get-Content -LiteralPath $otherMeta -Raw | ConvertFrom-Json
        } catch {
            Write-Warn2 "unreadable instance.json in $($other.Name); ignored"
            continue
        }
        if ([int]$doc.index -eq $Index) {
            Write-Err2 "index $Index is already used by instance '$($other.Name)'"
            exit 1
        }
    }
}
Write-Ok "index         : $Index (unique under the rig root)"

# ------------------------------------------------------------------ build
Write-Head 'Creating instance'

if (Test-Path -LiteralPath $InstanceDir) {
    if (-not $Force) {
        Write-Err2 "instance already exists: $InstanceDir (pass -Force to re-seed)"
        exit 1
    }
    Write-Info 'instance exists; re-seeding the save directory (-Force)'
    if (Test-Path -LiteralPath $SaveDir) {
        Remove-Item -LiteralPath $SaveDir -Recurse -Force
    }
}
New-Item -ItemType Directory -Force -Path $SaveDir | Out-Null
Write-Ok "created $SaveDir"

$entries = @()
foreach ($f in $sourceFiles) {
    $before = Get-Sha256 $f.FullName
    $dest = Join-Path $SaveDir $f.Name
    # Copy-Item only reads the source. The hash around the copy is the proof,
    # not an assumption: a source that changes under us must stop the rig.
    Copy-Item -LiteralPath $f.FullName -Destination $dest -Force
    $after = Get-Sha256 $f.FullName
    if ($before -ne $after) {
        Write-Err2 "SOURCE CHANGED during copy: $($f.FullName)"
        exit 1
    }
    $copied = Get-Sha256 $dest
    if ($copied -ne $before) {
        Write-Err2 "copy does not match source: $dest"
        exit 1
    }
    $entries += [ordered]@{
        name   = $f.Name
        bytes  = (Get-Item -LiteralPath $dest).Length
        sha256 = $copied
    }
    Write-Ok ("seeded {0,-16} {1,8} bytes  {2}" -f $f.Name, $f.Length, $copied.Substring(0, 16))
}

$meta = [ordered]@{
    name        = $Name
    index       = $Index
    createdUtc  = (Get-Date).ToUniversalTime().ToString('o')
    host        = $env:COMPUTERNAME
    rigRoot     = $Root
    instanceDir = $InstanceDir
    saveDir     = $SaveDir
    sourcePath  = $sourceResolved
    sourceFiles = @($sourceFiles | ForEach-Object { $_.FullName })
    saves       = $entries
}
$meta | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $MetaPath -Encoding UTF8
Write-Ok "wrote $MetaPath"

# ------------------------------------------------------------------ save guard
Write-Head 'Save guard'

$guard = Join-Path $PSScriptRoot 'save_guard.ps1'
$otherSaves = @()
if (Test-Path -LiteralPath $Root -PathType Container) {
    $otherSaves = @(Get-ChildItem -LiteralPath $Root -Directory -Force |
        Where-Object { $_.Name -ne $Name } |
        ForEach-Object { Join-Path $_.FullName 'Saved\SaveGames' } |
        Where-Object { Test-Path -LiteralPath $_ })
}
if (Test-Path -LiteralPath $guard) {
    $guardArgs = @{
        SavePath          = $SaveDir
        OtherInstancePath = $otherSaves
        InstanceRoot      = $InstanceDir
    }
    & $guard @guardArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Err2 'save_guard.ps1 refused this instance directory'
        exit 1
    }
} else {
    Write-Warn2 "save_guard.ps1 not found next to this script; skipped"
}

# ------------------------------------------------------------------ launch line
Write-Head 'Instance ready'

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$exe = Find-ShippingExe $repoRoot
if (-not $exe) {
    Write-Warn2 "shipping exe not found under $repoRoot; fix the path in the command below"
    $exe = '<QueensDomain-Win64-Shipping.exe>'
} else {
    Write-Ok "shipping exe  : $exe"
}

Write-Host ''
Write-Host 'Launch command (exact):' -ForegroundColor Yellow
Write-Host ('  & "{0}" -UserDir="{1}"' -f $exe, $InstanceDir) -ForegroundColor White
Write-Host ''
Write-Host 'From cmd.exe:' -ForegroundColor Yellow
Write-Host ('  "{0}" -UserDir="{1}"' -f $exe, $InstanceDir) -ForegroundColor White
Write-Host ''
Write-Host 'The game also accepts -saveddirsuffix=<suffix>, which renames the Saved' -ForegroundColor Gray
Write-Host 'directory to Saved_<suffix> inside the same user dir. This rig uses' -ForegroundColor Gray
Write-Host '-UserDir because it also moves config and logs, and because two instances' -ForegroundColor Gray
Write-Host 'must not share a Saved directory or config.' -ForegroundColor Gray
Write-Host ''
Write-Host ("Saves for this instance will appear in: {0}" -f $SaveDir) -ForegroundColor Gray
Write-Host ("Identity for the rig to assert   : index {0}, name {1}" -f $Index, $Name) -ForegroundColor Gray
Write-Host ''
Write-Host 'The source save was read only and is unchanged (hash checked around the copy).' -ForegroundColor Green

exit 0
