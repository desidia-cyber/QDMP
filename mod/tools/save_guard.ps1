#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Refuse to let a game instance save somewhere it must not.

.DESCRIPTION
    One authority per save, and never the operator's real profile. This script
    is the gate that enforces both, and it is meant to run BEFORE the game is
    launched rather than after it has already written a file.

    It refuses (exit 1) when the candidate save directory is:

      * missing            - the path was never created, or a typo, or a drive
                             that is not mounted. Creating it here would hide the
                             mistake, so it is a refusal instead.
      * relative           - a relative path resolves against whatever the
                             working directory happens to be. DollsMP's guard
                             treated empty/relative as fatal for that reason.
      * a duplicate        - equal to, or nested with, another instance's save
                             directory. Two instances writing one directory
                             corrupts the save, and "nested" is the same hazard
                             because both instances see each other's files.
      * at/above/inside    - the real profile at
                             %LOCALAPPDATA%\QueensDomain\Saved\SaveGames. "At"
                             is the profile itself, "above" is any ancestor (a
                             -UserDir of ...\QueensDomain\Saved would have the
                             game recreate SaveGames beneath it), and "inside"
                             is any descendant. All three end up editing the
                             operator's real save, so all three are refused
                             unless -AllowRealProfile is passed deliberately.

    Every check prints the values it compared, so a refusal says exactly what
    was wrong rather than "invalid path". Exit 0 means proceed, exit 1 means
    refuse.

.PARAMETER SavePath
    The directory the instance will save into. Absolute, already existing,
    distinct from every other instance and from the real profile.

.PARAMETER OtherInstancePath
    Save directories belonging to the other instances in the rig. Each is
    compared against -SavePath in both directions so neither equality nor
    nesting gets through.

.PARAMETER InstanceRoot
    Optional. When given, -SavePath must live inside it. This catches a rig
    script that computes an instance directory but passes the wrong save path.

.PARAMETER AllowRealProfile
    Permit a path at/above/inside the real profile. Only for an explicit,
    one-off recovery operation; a test rig must never need it.

.EXAMPLE
    ./save_guard.ps1 -SavePath 'G:\Coop\inst1\SaveGames' `
                     -OtherInstancePath 'G:\Coop\inst2\SaveGames' `
                     -InstanceRoot 'G:\Coop\inst1'

.EXAMPLE
    ./save_guard.ps1 -SavePath "$env:LOCALAPPDATA\QueensDomain\Saved\SaveGames"
    # -> REFUSED, exit 1
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$SavePath,

    [string[]]$OtherInstancePath = @(),

    [string]$InstanceRoot = '',

    [switch]$AllowRealProfile
)

$ErrorActionPreference = 'Stop'

# The one directory this game writes its real saves to. Derived from the
# environment rather than hardcoded so a rename of the profile does not silently
# disable the guard; when LOCALAPPDATA is unavailable the check is reported as
# skipped instead of pretending it passed.
$RealProfile = $null
if ($env:LOCALAPPDATA) {
    $RealProfile = Join-Path $env:LOCALAPPDATA 'QueensDomain\Saved\SaveGames'
}

function Write-Head([string]$t) { Write-Host "`n$t" -ForegroundColor Cyan }
function Write-Ok([string]$t)   { Write-Host "  [ok]   $t" -ForegroundColor Green }
function Write-Bad([string]$t)  { Write-Host "  [FAIL] $t" -ForegroundColor Red }
function Write-Skip2([string]$t){ Write-Host "  [skip] $t" -ForegroundColor DarkGray }
function Write-Note([string]$t) { Write-Host "  $t" -ForegroundColor DarkGray }
# A resolved path is not a pass/fail check, but it still gets a status token so
# its column matches the checks around it.
function Write-Path([string]$t) { Write-Host ("  [path] {0,-29}: {1}" -f 'resolved', $t) -ForegroundColor DarkGray }

# Resolve to an absolute path with its real casing, following a junction or
# symlink at the leaf. Containment tests on the unresolved spelling would let
# "<root>\link" point anywhere and still look inside <root>.
function Get-RealPath([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path)) { return '' }
    $full = [System.IO.Path]::GetFullPath($Path)
    $item = Get-Item -LiteralPath $full -Force -ErrorAction SilentlyContinue
    if ($null -eq $item) { return $full.TrimEnd('\', '/') }
    if ($item.LinkType -in @('Junction', 'SymbolicLink') -and $item.Target) {
        $target = @($item.Target)[0]
        if (-not [System.IO.Path]::IsPathRooted($target)) {
            $target = Join-Path (Split-Path -Parent $full) $target
        }
        return [System.IO.Path]::GetFullPath($target).TrimEnd('\', '/')
    }
    return [System.IO.Path]::GetFullPath($item.FullName).TrimEnd('\', '/')
}

function Test-SamePath([string]$A, [string]$B) {
    return [string]::Equals($A, $B, [StringComparison]::OrdinalIgnoreCase)
}

# True when $Child is $Parent or lives underneath it.
function Test-PathWithin([string]$Child, [string]$Parent) {
    if (Test-SamePath $Child $Parent) { return $true }
    $prefix = $Parent.TrimEnd('\', '/') + '\'
    return $Child.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)
}

$failures = [System.Collections.Generic.List[string]]::new()
$resolved = ''
$isDirectory = $false

Write-Head 'save guard'

# ------------------------------------------------------------------ echo
Write-Note ("candidate     : {0}" -f $(if ($SavePath) { $SavePath } else { '<empty>' }))
Write-Note ("real profile  : {0}" -f $(if ($RealProfile) { $RealProfile } else { '<LOCALAPPDATA not set>' }))
Write-Note ("instance root : {0}" -f $(if ($InstanceRoot) { $InstanceRoot } else { '<not given>' }))
if ($OtherInstancePath.Count -gt 0) {
    Write-Note ("other instance: {0}" -f ($OtherInstancePath -join '; '))
} else {
    Write-Note "other instance: <none given>"
}

Write-Head 'checks'

# ------------------------------------------------------------------ empty
if ([string]::IsNullOrWhiteSpace($SavePath)) {
    Write-Bad 'not empty                    : the value is empty or whitespace'
    $failures.Add('save path is empty')
} else {
    Write-Ok ("not empty                   : '{0}'" -f $SavePath)
}

# ------------------------------------------------------------------ absolute
$isAbsolute = $false
if (-not [string]::IsNullOrWhiteSpace($SavePath)) {
    $isAbsolute = [System.IO.Path]::IsPathRooted($SavePath)
}
if ($isAbsolute) {
    Write-Ok ("absolute path               : rooted at '{0}'" -f ([System.IO.Path]::GetPathRoot($SavePath)))
} else {
    Write-Bad ("absolute path               : '{0}' is relative (resolves against the working directory)" -f $SavePath)
    $failures.Add('save path is relative')
}

# ------------------------------------------------------------------ exists
if ($isAbsolute) {
    $resolved = Get-RealPath $SavePath
    Write-Path $resolved
    $isDirectory = Test-Path -LiteralPath $resolved -PathType Container
    if ($isDirectory) {
        Write-Ok ("directory exists            : {0}" -f $resolved)
        Write-Ok ("is a directory              : yes")
    } elseif (Test-Path -LiteralPath $resolved -PathType Leaf) {
        Write-Bad ("directory exists            : {0} is a FILE, not a directory" -f $resolved)
        $failures.Add('save path is a file')
    } else {
        Write-Bad ("directory exists            : {0} is missing" -f $resolved)
        $failures.Add('save path does not exist')
    }
} else {
    Write-Skip2 'directory exists            : skipped (path is not absolute)'
}

# ------------------------------------------------------------------ real profile
$realResolved = if ($RealProfile) { Get-RealPath $RealProfile } else { $null }
if (-not $RealProfile) {
    Write-Skip2 'real profile                : skipped (LOCALAPPDATA is not set, cannot locate it)'
} elseif (-not $isAbsolute) {
    Write-Skip2 'real profile                : skipped (path is not absolute)'
} else {
    $atReal = Test-SamePath $resolved $realResolved
    $aboveReal = Test-PathWithin $realResolved $resolved   # resolved is an ancestor of the profile
    $insideReal = Test-PathWithin $resolved $realResolved  # resolved lives under the profile

    if ($atReal -or $aboveReal -or $insideReal) {
        $relation = if ($atReal) { 'IS the real profile' }
                    elseif ($aboveReal) { 'is ABOVE the real profile (an ancestor of it)' }
                    else { 'is INSIDE the real profile' }
        if ($AllowRealProfile) {
            Write-Ok ("real profile                : {0} {1}, permitted by -AllowRealProfile" -f $resolved, $relation)
            Write-Host "         WARNING: this writes the operator's real save. Only do this on purpose." -ForegroundColor Yellow
        } else {
            Write-Bad ("real profile                : {0} {1}" -f $resolved, $relation)
            Write-Host ("{0}profile: {1}" -f (' ' * 40), $realResolved) -ForegroundColor DarkGray
            Write-Host ("{0}pass -AllowRealProfile only for an explicit recovery" -f (' ' * 40)) -ForegroundColor DarkGray
            $failures.Add("save path $relation")
        }
    } else {
        Write-Ok ("real profile                : distinct from {0}" -f $realResolved)
    }
}

# ------------------------------------------------------------------ duplicates
if ($OtherInstancePath.Count -eq 0) {
    Write-Skip2 'duplicate of another instance: skipped (no -OtherInstancePath given)'
} elseif (-not $isAbsolute) {
    Write-Skip2 'duplicate of another instance: skipped (path is not absolute)'
} else {
    $clash = $null
    $relation = ''
    foreach ($other in $OtherInstancePath) {
        if ([string]::IsNullOrWhiteSpace($other)) { continue }
        $otherResolved = Get-RealPath $other
        if (Test-SamePath $resolved $otherResolved) {
            $clash = $otherResolved
            $relation = 'is the SAME directory as'
            break
        }
        if (Test-PathWithin $resolved $otherResolved) {
            $clash = $otherResolved
            $relation = 'lives INSIDE'
            break
        }
        if (Test-PathWithin $otherResolved $resolved) {
            $clash = $otherResolved
            $relation = 'CONTAINS'
            break
        }
    }
    if ($clash) {
        Write-Bad ("duplicate of another instance: {0} {1} {2}" -f $resolved, $relation, $clash)
        Write-Host ("{0}two instances must not share or nest save directories" -f (' ' * 40)) -ForegroundColor DarkGray
        $failures.Add("save path $relation another instance")
    } else {
        $compared = ($OtherInstancePath | ForEach-Object { Get-RealPath $_ }) -join '; '
        Write-Ok ("duplicate of another instance: distinct from {0}" -f $compared)
    }
}

# ------------------------------------------------------------------ instance root
if ([string]::IsNullOrWhiteSpace($InstanceRoot)) {
    Write-Skip2 'inside the instance root    : skipped (no -InstanceRoot given)'
} elseif (-not $isAbsolute) {
    Write-Skip2 'inside the instance root    : skipped (path is not absolute)'
} else {
    $rootResolved = Get-RealPath $InstanceRoot
    if (Test-PathWithin $resolved $rootResolved) {
        Write-Ok ("inside the instance root    : {0} is within {1}" -f $resolved, $rootResolved)
    } else {
        Write-Bad ("inside the instance root    : {0} is OUTSIDE {1}" -f $resolved, $rootResolved)
        $failures.Add('save path is outside the instance root')
    }
}

# ------------------------------------------------------------------ verdict
Write-Head 'verdict'
if ($failures.Count -gt 0) {
    Write-Host "REFUSED: do not launch an instance with this save path." -ForegroundColor Red
    foreach ($f in $failures) { Write-Host "  - $f" -ForegroundColor Red }
    Write-Host ''
    Write-Host "  candidate: $SavePath"
    exit 1
}

Write-Host "PROCEED: '$resolved' passed every check." -ForegroundColor Green
exit 0
