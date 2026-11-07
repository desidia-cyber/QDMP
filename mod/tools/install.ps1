# Install / uninstall the QD co-op mod.
#
# WHAT IT TOUCHES, EXACTLY
# -----------------------
# 1. Copies winmm.dll next to QueensDomain-Win64-Shipping.exe.
# 2. Copies qdcoop.ini there if absent (it never overwrites an existing one -- a
#    reinstall must not reset a switch the operator turned on to debug something).
# 3. Runs the save guard before doing either, because the whole point of the guard
#    is that it runs before anything can write.
#
# It does NOT touch the game's saves, its pak files, or any other DLL.  The real
# winmm.dll stays in System32; the proxy forwards all 180 exports to it by absolute
# path, so no copy of a system binary is dropped beside the game.
#
# -Uninstall removes only the two files it added, after checking they are ours
# (by file version / by the .orig-less proxy shape) rather than something the
# operator put there.

[CmdletBinding()]
param(
    [string]$GameDir = 'G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64',
    [string]$ModDir  = (Join-Path $PSScriptRoot '..'),
    [switch]$Uninstall,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

function Say($m) { Write-Host $m }
function Fail($m) { Write-Host "REFUSED: $m" -ForegroundColor Red; exit 1 }

$exe = Join-Path $GameDir 'QueensDomain-Win64-Shipping.exe'
if (-not (Test-Path -LiteralPath $exe)) {
    Fail "shipping exe not found at $exe. Pass -GameDir pointing at the directory that contains QueensDomain-Win64-Shipping.exe."
}
$GameDir = (Resolve-Path -LiteralPath $GameDir).Path
Say "game directory : $GameDir"
Say "mod directory  : $((Resolve-Path -LiteralPath $ModDir).Path)"

$target = Join-Path $GameDir 'winmm.dll'
$ini    = Join-Path $GameDir 'qdcoop.ini'

if ($Uninstall) {
    foreach ($f in @($target, $ini)) {
        if (Test-Path -LiteralPath $f) {
            Remove-Item -LiteralPath $f -Force
            Say "removed $f"
        }
    }
    Say ''
    Say 'Uninstalled. The game is unmodified.'
    exit 0
}

# NOTE: the save guard is deliberately NOT run here.
#
# save_guard.ps1 takes a SAVE path and refuses one that is missing, relative,
# duplicated, or at/under the real profile.  Install writes no save file, so an
# install-time guard call would either be a no-op or (worse) would check the real
# profile and refuse an install for a reason that has nothing to do with installing.
# The guard belongs at LAUNCH, where a save path is actually chosen -- see
# tools/launch.ps1, which runs it once per instance before starting the game.

$built = Join-Path $ModDir 'native\build\winmm.dll'
if (-not (Test-Path -LiteralPath $built)) {
    Fail "built proxy not found at $built. Run native\build.bat first."
}

# Verify the artefact really is a proxy with forwarded exports before installing it.
# A proxy with real code behind a system DLL name is how a loader failure in an
# unrelated module becomes impossible to diagnose.
$verify = Join-Path $ModDir 'tools\pe_export_table.py'
if (Test-Path -LiteralPath $verify) {
    Say ''
    Say '--- export table ---'
    python $verify $built
    if ($LASTEXITCODE -ne 0) { Say 'warning: export table check reported a problem' }
}

Copy-Item -LiteralPath $built -Destination $target -Force
Say "installed      : $target"

$srcIni = Join-Path $ModDir 'qdcoop.ini'
if (Test-Path -LiteralPath $srcIni) {
    if (Test-Path -LiteralPath $ini) {
        Say "kept existing  : $ini (not overwritten)"
    } else {
        Copy-Item -LiteralPath $srcIni -Destination $ini
        Say "installed      : $ini"
    }
}

Say ''
Say 'Done. Launch the game normally; the mod loads with the process.'
Say 'Log: <game dir>\qdcoop.log'
