# rig.ps1 -- drive one instance of the two-machine rig from a short command.
#
# WHY THIS EXISTS
# ---------------
# The earlier rounds could only launch, wait and grep: reaching gameplay needed a dev
# autoload, and every "does this lane actually carry a change" test was left unrun because
# there was no way to walk a player to a door, a pickup or an enemy inside a live session.
# The mod now reads a teleport instruction from a file (QDCOOP_TEST_TPFILE) and logs what
# is nearest to the player (QDCOOP_TEST_SCAN), so a scenario is a sequence of these calls:
#
#   rig.ps1 -Action ps -Instance 1                      what the player's position is now
#   rig.ps1 -Action near -Instance 1                    the last scan block
#   rig.ps1 -Action tp -Instance 1 -Value "x,y,z,yaw"   put the player there
#   rig.ps1 -Action key -Instance 1 -Value E            post a key to the game window
#   rig.ps1 -Action click -Instance 1 -Value left       post a click at the client centre
#   rig.ps1 -Action log -Instance 1 -Value damage       last matches from that run's log
#
# It posts to the same window capture-hidden.ps1 reads, so what is keyed and what is
# screenshotted are the same surface, and nothing here takes the desktop.
#
# NOTE ON THE PARAMETER NAME: it is `-Action`, not `-Do`.  `-Do` is a prefix of the common
# parameter `-Debug`, which [CmdletBinding] adds, so PowerShell bound it there and then
# tried to take the next positional argument for an [int] -- measured as
# "Cannot convert the System.Object[] value to type System.Int32".

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('ps', 'near', 'tp', 'key', 'click', 'log', 'wait', 'shot', 'act')]
    [string]$Action,

    # 1 = inst1 (suffix qd1), 2 = inst2 (suffix qd2)
    [int]$Instance = 1,

    [string]$Value = '',
    [int]$Lines = 20,
    [int]$Seconds = 0
)

$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot

# The instance root and the suffix are the rig's contract: the launcher passes
# -UserDir <root> -saveddirsuffix <suffix>, and the mod names its log after the suffix.
$root    = 'G:\Freespace\Queens_Domain\_test\inst' + $Instance
$suffix  = 'qd' + $Instance
$gameDir = 'G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64'
$log     = Join-Path $gameDir ('qdcoop-' + $suffix + '.log')
$tpFile  = $root + '\qd.tp'
$actFile = $root + '\qd.act'
$pidFile = $root + '.pid'

function Get-GamePid {
    if (Test-Path -LiteralPath $pidFile) {
        $p = Get-Content -LiteralPath $pidFile -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($p -and (Get-Process -Id ([int]$p) -ErrorAction SilentlyContinue)) { return [int]$p }
    }
    throw "instance $Instance is not running (no live pid in $pidFile)"
}

# The log is appended across launches, so every query is bounded to the current run: the
# last "hooked FEngineLoop" line marks where this launch began.
function Get-CurrentRunLines {
    $all = @(Get-Content -LiteralPath $log -ErrorAction SilentlyContinue)
    if ($all.Count -eq 0) { return @() }
    $start = 0
    for ($i = $all.Count - 1; $i -ge 0; $i--) {
        if ($all[$i] -match 'hooked FEngineLoop') { $start = $i; break }
    }
    return @($all[$start..($all.Count - 1)])
}

switch ($Action) {

    'wait' {
        if ($Seconds -le 0) { $Seconds = 1 }
        Start-Sleep -Seconds $Seconds
        Write-Output "waited $Seconds s"
    }

    'ps' {
        $rows = @(Get-CurrentRunLines | Where-Object { $_ -match 'local pos' })
        if ($rows.Count -eq 0) { Write-Output 'no local pos yet'; return }
        Write-Output ($rows[-1])
    }

    'near' {
        $rows = @(Get-CurrentRunLines | Where-Object { $_ -match 'scan: ' })
        if ($rows.Count -eq 0) { Write-Output 'no scan yet (is QDCOOP_TEST_SCAN=1?)'; return }
        $start = 0
        for ($i = $rows.Count - 1; $i -ge 0; $i--) {
            if ($rows[$i] -match 'scan: local at') { $start = $i; break }
        }
        $rows[$start..($rows.Count - 1)] | ForEach-Object { Write-Output $_ }
    }

    'tp' {
        if ($Value -notmatch '^-?[0-9.]+\s*,\s*-?[0-9.]+\s*,\s*-?[0-9.]+') {
            throw "tp needs x,y,z[,yaw]; got '$Value'"
        }
        # The mod acts on a change, so an identical instruction is a no-op by design;
        # writing the same coordinates twice is harmless rather than sticky.
        Set-Content -LiteralPath $tpFile -Value $Value -NoNewline
        Write-Output "teleport $Instance -> $Value"
    }

    'act' {
        $n = 0
        if (Test-Path -LiteralPath $actFile) { $n = [int](Get-Content -LiteralPath $actFile -ErrorAction SilentlyContinue | Select-Object -First 1) }
        Set-Content -LiteralPath $actFile -Value ([string]($n + 1)) -NoNewline
        Write-Output "interact $Instance -> $($n + 1)"
    }

    'key' {
        $gamePid = Get-GamePid
        $vk = switch ($Value.ToUpper()) {
            'E'     { 0x45 }
            'F'     { 0x46 }
            'SPACE' { 0x20 }
            'ENTER' { 0x0D }
            'ESC'   { 0x1B }
            'W'     { 0x57 }
            'S'     { 0x53 }
            'A'     { 0x41 }
            'D'     { 0x44 }
            default {
                if ($Value -match '^0x[0-9a-fA-F]+$') { [Convert]::ToInt32($Value, 16) }
                else { throw "unknown key '$Value'" }
            }
        }
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'send-input.ps1') `
            -ProcessId $gamePid -Vk $vk 2>&1 | Select-Object -Last 1 | ForEach-Object { Write-Output $_ }
    }

    'click' {
        $gamePid = Get-GamePid
        $button = if ($Value -eq 'right') { 'right' } else { 'left' }
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'send-input.ps1') `
            -ProcessId $gamePid -Button $button 2>&1 | Select-Object -Last 1 | ForEach-Object { Write-Output $_ }
    }

    'shot' {
        $gamePid = Get-GamePid
        $out = if ($Value) { $Value } else { 'G:\Freespace\Queens_Domain\_test\rig-inst' + $Instance + '.png' }
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'capture-hidden.ps1') `
            -ProcessId $gamePid -Out $out 2>&1 | Select-Object -Last 1 | ForEach-Object { Write-Output $_ }
    }

    'log' {
        $pattern = if ($Value) { $Value } else { '.' }
        $rows = @(Get-CurrentRunLines | Where-Object { $_ -match $pattern })
        if ($rows.Count -eq 0) { Write-Output "(no match for '$pattern' in this run)"; return }
        $rows | Select-Object -Last $Lines | ForEach-Object { Write-Output $_ }
    }
}


