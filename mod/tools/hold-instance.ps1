# hold-instance.ps1 -- launch one test instance off-screen and keep it alive.
#
# The harness tears down a process tree when the command that started it ends
# (LIVE-TEST-LOG R14).  Run THIS in the background: it owns the game for -Seconds,
# while separate, short commands drive it (send-input.ps1), look at it
# (capture-hidden.ps1) and read qdcoop.log.  The PID is written to <UserDir>.pid.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$UserDir,
    [string]$Suffix = 'qd1',
    [int]$Seconds = 900
)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$launch = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'run-hidden.ps1') `
            -UserDir $UserDir -Suffix $Suffix 2>&1
$gamePid = ($launch | Where-Object { $_ -match '^\d+$' } | Select-Object -Last 1)
if (-not $gamePid) { Write-Output "LAUNCH FAILED"; $launch; exit 1 }
Set-Content -Path ($UserDir.TrimEnd('\') + '.pid') -Value $gamePid
Write-Output "pid=$gamePid"
$deadline = (Get-Date).AddSeconds($Seconds)
while ((Get-Date) -lt $deadline) {
    if (-not (Get-Process -Id $gamePid -ErrorAction SilentlyContinue)) {
        Write-Output "game exited"
        exit 0
    }
    Start-Sleep -Seconds 2
}
Write-Output "hold expired; stopping pid=$gamePid"
Stop-Process -Id $gamePid -Force -ErrorAction SilentlyContinue
