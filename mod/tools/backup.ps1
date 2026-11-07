#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Timestamped backup of a Queen's Domain save directory, verified after copy.

.DESCRIPTION
    Backups that are never verified are guesses. This script hashes every source
    file before the copy, hashes it again after the copy, hashes the copy, and
    then compares all three. Anything that does not match is reported and the
    run exits 1, so a caller can refuse to launch the game on a bad backup.

    The source is hashed twice on purpose. The game writes suspend saves on its
    own schedule (`World0_Suspend_N.sav`), so a backup taken while the game runs
    can race a writer: the pre-copy hash catches "the file changed while we were
    copying it", which a single hash pair would silently accept.

    Snapshots are plain directories under <BackupRoot>, named for the time they
    were taken, holding the files plus a manifest.json with the hashes. `-List`
    prints them. Restoring is a deliberate manual copy -- this script does not
    overwrite a live save directory, because getting that wrong is exactly the
    class of failure the rest of these tools exist to prevent.

.PARAMETER SaveDir
    The save directory to back up. Read only; nothing in it is modified.

.PARAMETER BackupRoot
    Where snapshots are created. Defaults to <repo>\mod\backups.

.PARAMETER Keep
    Number of snapshots to retain; the oldest beyond this are pruned.

.PARAMETER List
    Print existing snapshots and exit without copying anything.

.EXAMPLE
    ./backup.ps1 -SaveDir 'G:\Freespace\Queens_Domain\mod\testgame\inst1\Saved\SaveGames'

.EXAMPLE
    ./backup.ps1 -SaveDir "$env:LOCALAPPDATA\QueensDomain\Saved\SaveGames" -Keep 20

.EXAMPLE
    ./backup.ps1 -List
#>
[CmdletBinding()]
param(
    [string]$SaveDir = '',

    [string]$BackupRoot = '',

    [ValidateRange(1, 1000)]
    [int]$Keep = 10,

    [switch]$List
)

$ErrorActionPreference = 'Stop'

function Write-Head([string]$t) { Write-Host "`n=== $t ===" -ForegroundColor Cyan }
function Write-Ok([string]$t)   { Write-Host "  [ok]   $t" -ForegroundColor Green }
function Write-Warn2([string]$t){ Write-Host "  [warn] $t" -ForegroundColor Yellow }
function Write-Err2([string]$t) { Write-Host "  [FAIL] $t" -ForegroundColor Red }
function Write-Info([string]$t) { Write-Host "  [info] $t" -ForegroundColor Gray }

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if ([string]::IsNullOrWhiteSpace($BackupRoot)) {
    $BackupRoot = Join-Path $repoRoot 'mod\backups'
}

function Get-Sha256([string]$Path) {
    (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

function Get-RealPath([string]$Path) {
    $full = [System.IO.Path]::GetFullPath($Path)
    $item = Get-Item -LiteralPath $full -Force -ErrorAction SilentlyContinue
    if ($null -eq $item) { return $full.TrimEnd('\', '/') }
    return [System.IO.Path]::GetFullPath($item.FullName).TrimEnd('\', '/')
}

$BackupRoot = Get-RealPath $BackupRoot

# ------------------------------------------------------------------ list
if ($List) {
    Write-Head 'Snapshots'
    if (-not (Test-Path -LiteralPath $BackupRoot -PathType Container)) {
        Write-Info "no backup root yet: $BackupRoot"
        exit 0
    }
    $snaps = @(Get-ChildItem -LiteralPath $BackupRoot -Directory -Force |
        Sort-Object Name -Descending)
    if ($snaps.Count -eq 0) {
        Write-Info "no snapshots under $BackupRoot"
        exit 0
    }
    foreach ($s in $snaps) {
        $manifest = Join-Path $s.FullName 'manifest.json'
        if (-not (Test-Path -LiteralPath $manifest)) {
            Write-Warn2 ("{0}  (no manifest)" -f $s.Name)
            continue
        }
        $m = Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
        $verified = if ($m.verified) { 'verified' } else { 'NOT VERIFIED' }
        $bytes = ($m.files | Measure-Object -Property bytes -Sum).Sum
        Write-Host ("  {0}  files={1,-3} bytes={2,-9} {3}  from {4}" -f `
            $s.Name, @($m.files).Count, $bytes, $verified, $m.sourcePath)
    }
    exit 0
}

# ------------------------------------------------------------------ preflight
Write-Head 'Backup preflight'

if ([string]::IsNullOrWhiteSpace($SaveDir)) {
    Write-Err2 'no -SaveDir given'
    exit 1
}
if (-not [System.IO.Path]::IsPathRooted($SaveDir)) {
    Write-Err2 "save directory must be absolute: $SaveDir"
    exit 1
}
if (-not (Test-Path -LiteralPath $SaveDir -PathType Container)) {
    Write-Err2 "save directory does not exist: $SaveDir"
    exit 1
}
$SaveDir = Get-RealPath $SaveDir
Write-Ok "save directory: $SaveDir"

# A backup root inside the save directory would try to copy its own output.
if ($BackupRoot.Equals($SaveDir, [StringComparison]::OrdinalIgnoreCase) -or
    $BackupRoot.StartsWith($SaveDir.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
    Write-Err2 "backup root is inside the save directory: $BackupRoot"
    exit 1
}
Write-Ok "backup root   : $BackupRoot"

$sourceFiles = @(Get-ChildItem -LiteralPath $SaveDir -File -Force -Recurse)
if ($sourceFiles.Count -eq 0) {
    Write-Err2 "save directory holds no files: $SaveDir"
    exit 1
}
Write-Ok ("source files  : {0}" -f $sourceFiles.Count)

# ------------------------------------------------------------------ copy
Write-Head 'Copying'

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$snapDir = Join-Path $BackupRoot $stamp
# Two backups in the same second must not merge into one snapshot.
if (Test-Path -LiteralPath $snapDir) {
    $stamp = $stamp + '-' + (Get-Random -Minimum 1000 -Maximum 9999)
    $snapDir = Join-Path $BackupRoot $stamp
}
New-Item -ItemType Directory -Force -Path $snapDir | Out-Null
Write-Ok "snapshot      : $snapDir"

$entries = @()
$bad = 0

foreach ($f in $sourceFiles) {
    $rel = $f.FullName.Substring($SaveDir.Length).TrimStart('\')
    $before = Get-Sha256 $f.FullName
    $sizeBefore = $f.Length

    $dest = Join-Path $snapDir $rel
    $destParent = Split-Path -Parent $dest
    if ($destParent -and -not (Test-Path -LiteralPath $destParent)) {
        New-Item -ItemType Directory -Force -Path $destParent | Out-Null
    }
    Copy-Item -LiteralPath $f.FullName -Destination $dest -Force

    # Verify after the copy: re-hash the source (did it change under us?) and
    # hash the copy (did the copy reproduce it?). Both must equal the first hash.
    $sourceAfter = Get-Sha256 $f.FullName
    $copyHash = Get-Sha256 $dest
    $sizeCopy = (Get-Item -LiteralPath $dest).Length

    $ok = ($before -eq $sourceAfter) -and ($before -eq $copyHash) -and ($sizeBefore -eq $sizeCopy)
    if ($ok) {
        Write-Ok ("{0,-22} {1,9} bytes  {2}" -f $rel, $sizeCopy, $before)
    } else {
        $bad++
        Write-Err2 ("{0} NOT VERIFIED" -f $rel)
        if ($before -ne $sourceAfter) {
            Write-Err2 "  source changed during the copy (the game may be writing it)"
        }
        if ($before -ne $copyHash) {
            Write-Err2 "  copy hash differs: source $before, copy $copyHash"
        }
        if ($sizeBefore -ne $sizeCopy) {
            Write-Err2 "  copy size differs: source $sizeBefore, copy $sizeCopy"
        }
    }

    $entries += [ordered]@{
        path            = $f.FullName
        storedAs        = $rel
        bytes           = $sizeCopy
        sha256          = $copyHash
        sourceSha256    = $before
        sourceChanged   = ($before -ne $sourceAfter)
        verified        = $ok
        modifiedUtc     = $f.LastWriteTimeUtc.ToString('o')
    }
}

$manifest = [ordered]@{
    createdUtc  = (Get-Date).ToUniversalTime().ToString('o')
    sourcePath  = $SaveDir
    backupRoot  = $BackupRoot
    host        = $env:COMPUTERNAME
    verified    = ($bad -eq 0)
    fileCount   = $entries.Count
    files       = $entries
}
$manifest | ConvertTo-Json -Depth 6 |
    Set-Content -LiteralPath (Join-Path $snapDir 'manifest.json') -Encoding UTF8

# ------------------------------------------------------------------ prune
Write-Head 'Prune'
$all = @(Get-ChildItem -LiteralPath $BackupRoot -Directory -Force |
    Sort-Object Name -Descending)
if ($all.Count -gt $Keep) {
    foreach ($d in ($all | Select-Object -Skip $Keep)) {
        Remove-Item -LiteralPath $d.FullName -Recurse -Force
        Write-Info "pruned $($d.Name)"
    }
} else {
    Write-Info ("{0} snapshot(s) kept, limit {1}" -f $all.Count, $Keep)
}

# ------------------------------------------------------------------ verdict
Write-Head 'Result'
if ($bad -gt 0) {
    Write-Err2 ("{0} of {1} files did not verify" -f $bad, $entries.Count)
    Write-Host "  snapshot: $snapDir" -ForegroundColor Red
    exit 1
}
Write-Ok ("all {0} files verified (hash compared on both sides)" -f $entries.Count)
Write-Host "  snapshot: $snapDir" -ForegroundColor Green
exit 0
