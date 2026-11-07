# live-session.ps1 -- run one co-op test session: launch, drive, observe, report.
#
# WHY THIS IS ONE LONG-RUNNING PROCESS
# ------------------------------------
# A launched game does not survive the command that started it: the process tree is torn
# down when that invocation ends. Measured -- the game stayed alive for a 70-second poll
# inside one call and was gone immediately after. Every earlier "the game exits after
# ~30-60 s" observation was that artefact, not a crash, which is worth stating because it
# sent one round of debugging at the wrong target.
#
# So a session is one process that launches the game, drives it, samples it, and only
# then exits. Run it in the background and read its output as it goes.
#
# The window is parked off-screen by run-hidden.ps1 and never takes the desktop.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$UserDir,
    [string]$Suffix = 'qd1',
    [int]$Seconds = 180,
    [string]$OutDir = 'G:\Freespace\Queens_Domain\_test',
    [int[]]$PressVk = @(0x0D),          # Enter
    [int]$PressAtSeconds = 14,
    [int]$ShotsEverySeconds = 45
)

$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$wd = 'G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64'
$log = Join-Path $wd 'qdcoop.log'
$stamp = Get-Date -Format 'HHmmss'
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Force -Path $OutDir | Out-Null }

function Say($m) { Write-Output ("[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $m) }

Say "session start; log will be $log"
Remove-Item $log -ErrorAction SilentlyContinue

$launch = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'run-hidden.ps1') `
            -UserDir $UserDir -Suffix $Suffix 2>&1
$gamePid = ($launch | Where-Object { $_ -match '^\d+$' } | Select-Object -Last 1)
if (-not $gamePid) { Say "LAUNCH FAILED"; $launch; exit 1 }
Say "launched pid=$gamePid (off-screen)"

$pressed = $false
$shotAt = $ShotsEverySeconds
$shotN = 0
for ($t = 0; $t -lt $Seconds; $t += 2) {
    Start-Sleep -Seconds 2

    if (-not (Get-Process -Id $gamePid -ErrorAction SilentlyContinue)) {
        Say "GAME EXITED at ~${t}s"
        break
    }

    if (-not $pressed -and $t -ge $PressAtSeconds) {
        foreach ($vk in $PressVk) {
            & powershell -NoProfile -ExecutionPolicy Bypass -File 'G:\Freespace\Queens_Domain\_live\press-pid.ps1' -ProcessId $gamePid -Vk $vk 2>&1 | Out-Null
            Start-Sleep -Milliseconds 400
        }
        Say "sent keys: $($PressVk -join ',')"
        $pressed = $true
    }

    if ($t -ge $shotAt) {
        ++$shotN
        $shot = Join-Path $OutDir ("shot-{0}-{1:D2}.png" -f $stamp, $shotN)
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'capture-hidden.ps1') -Out $shot -ProcessId $gamePid 2>&1 | ForEach-Object { Say "cap: $_" }
        $shotAt += $ShotsEverySeconds
    }

    # Every 20 s, summarise the interesting lines rather than dumping the log.
    if (($t % 20) -eq 0) {
        if (Test-Path $log) {
            $flags = Select-String -Path $log -Pattern 'flag |flag probe' | Select-Object -Last 6
            $probe = Select-String -Path $log -Pattern 'PROBE ' | Select-Object -Last 8
            $frames = Select-String -Path $log -Pattern 'frame pump armed|substrate:' | Select-Object -Last 2
            foreach ($x in $frames) { Say "  $($x.Line)" }
            foreach ($x in $probe) { Say "  $($x.Line)" }
            foreach ($x in $flags) { Say "  $($x.Line)" }
        }
    }
}

Say "session end"
if (Get-Process -Id $gamePid -ErrorAction SilentlyContinue) {
    Say "leaving pid=$gamePid running"
} else {
    Say "game not running"
}
