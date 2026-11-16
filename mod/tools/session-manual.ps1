# session-manual.ps1 -- two VISIBLE co-op instances for a human tester, shipped settings.
#
# Deploys the built winmm.dll and the shipped qdcoop.ini (relay pinned to loopback, LogLevel=3),
# starts the relay, launches inst1 (host) and inst2 (peer) windowed side by side, autoloads the
# gameplay save, then WAITS until the tester closes both windows.  A launched game dies with the
# command that started it, so this script must stay running for the whole session.  At the end
# it copies both mod logs to _test\manual\out and releases the LIVE lock.
param(
    [string]$RigRoot = 'G:\Freespace\Queens_Domain\_test\joinrun',
    [int]$ResX = 960,
    [int]$ResY = 540
)
$ErrorActionPreference = 'Stop'
$here    = $PSScriptRoot
$repo    = Split-Path -Parent (Split-Path -Parent $here)
$gameDir = 'G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64'
$exe     = Join-Path $gameDir 'QueensDomain-Win64-Shipping.exe'
$relay   = Join-Path (Split-Path -Parent $here) 'rust\target\release\qd-relay.exe'
$outDir  = 'G:\Freespace\Queens_Domain\_test\manual\out'
$lock    = 'G:\Freespace\Queens_Domain\_test\locks\live'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
function Say($m) { Write-Output ("[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $m) }

if (Get-Process -Name 'QueensDomain-Win64-Shipping', 'qd-relay' -ErrorAction SilentlyContinue) {
    Say 'REFUSED: a game or relay is already running'; exit 2
}
New-Item -ItemType Directory -Force -Path (Split-Path $lock) | Out-Null
try { New-Item -ItemType Directory -Path $lock -ErrorAction Stop | Out-Null } catch { Say 'REFUSED: LIVE lock held'; exit 2 }
Set-Content -LiteralPath (Join-Path $lock 'owner.txt') -Value 'manual'

try {
    Copy-Item -LiteralPath (Join-Path $repo 'mod\native\build\winmm.dll') -Destination (Join-Path $gameDir 'winmm.dll') -Force
    $text = Get-Content -LiteralPath (Join-Path $repo 'mod\qdcoop.ini') -Raw
    $text = $text -replace '(?m)^RelayHost=.*$', 'RelayHost=127.0.0.1'
    $text = $text -replace '(?m)^RelayPort=.*$', 'RelayPort=47800'
    $text = $text -replace '(?m)^LogLevel=.*$', 'LogLevel=3'
    $text | Set-Content -LiteralPath (Join-Path $gameDir 'qdcoop.ini') -NoNewline
    Say 'deployed winmm.dll + shipped qdcoop.ini (loopback relay, LogLevel=3)'

    $relayProc = Start-Process -FilePath $relay -ArgumentList @('--bind', '127.0.0.1:47800') `
        -RedirectStandardOutput (Join-Path $outDir 'relay.log') -RedirectStandardError (Join-Path $outDir 'relay.err') `
        -WindowStyle Hidden -PassThru
    Say "relay pid=$($relayProc.Id)"
    Start-Sleep -Seconds 2

    # NO autoload: the rig's Load-Slot shortcut skips the title screen's own Continue path, which
    # is what switches the game to gameplay input -- the tester could not move or look.  A human
    # starts the game the way a player does: Continue / Load on the title screen.
    Remove-Item Env:\QDCOOP_AUTOLOAD -ErrorAction SilentlyContinue
    $env:QDCOOP_INTERACTIVE = '1'      # a human plays: no cursor/focus hooks
    # Death-penalty sidecars left by earlier rig runs would start a player at 10 % max HP.
    Get-ChildItem -LiteralPath $gameDir -Filter 'qdcoop-*-respawn.ini' -ErrorAction SilentlyContinue |
        ForEach-Object { Remove-Item -LiteralPath $_.FullName -Force; Say "cleared stale death stacks: $($_.Name)" }
    $pids = @()
    foreach ($i in 1, 2) {
        $ud = Join-Path $RigRoot ("inst{0}" -f $i)
        $x = if ($i -eq 1) { 0 } else { $ResX }
        $args = @("-UserDir=`"$ud`"", "-saveddirsuffix=qd$i", '-SaveToUserDir', '-windowed', '-nosplash',
                  "-ResX=$ResX", "-ResY=$ResY", "-WinX=$x", '-WinY=40')
        $p = Start-Process -FilePath $exe -WorkingDirectory $gameDir -ArgumentList $args -PassThru
        $pids += $p.Id
        Say ("inst{0} pid={1} ({2})" -f $i, $p.Id, $(if ($i -eq 1) { 'HOST, left' } else { 'PEER, right' }))
        Start-Sleep -Seconds 4
    }
    Say 'running -- play in either window; close BOTH game windows to end the session'
    $tick = 0
    while ($true) {
        $alive = @($pids | Where-Object { Get-Process -Id $_ -ErrorAction SilentlyContinue })
        if ($alive.Count -eq 0) { break }
        Start-Sleep -Seconds 10
        if ((++$tick % 30) -eq 0) { Set-Content -LiteralPath (Join-Path $lock 'owner.txt') -Value 'manual' }
    }
    Say 'both game windows closed'
}
finally {
    if ($relayProc) { Stop-Process -Id $relayProc.Id -Force -ErrorAction SilentlyContinue }
    foreach ($f in 'qdcoop-qd1.log', 'qdcoop-qd2.log') {
        $src = Join-Path $gameDir $f
        if (Test-Path $src) { Copy-Item -LiteralPath $src -Destination (Join-Path $outDir $f) -Force }
    }
    Remove-Item -LiteralPath $lock -Recurse -Force -ErrorAction SilentlyContinue
    Say "logs copied to $outDir; LIVE lock released"
}
