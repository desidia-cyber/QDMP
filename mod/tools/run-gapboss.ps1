# run-gapboss.ps1 -- wait for the LIVE lock, run the boss-phase session, release it.
#
# THE LOCK RULE (COMMON-BRIEF §4): the LIVE lock is an atomic mkdir; hold it for the whole
# duration of a session that launches the game/relay.  Poll every 25 s; treat an owner older
# than 25 min as stale.  Release before exiting, and kill everything this script started.
#
# ONE SESSION, ONE PHASE: a launched game does not survive the command that started it (R14),
# so the whole proof is one invocation of session-bossphase.ps1 -Phase bphase.
param(
    [string]$Only    = '',
    [int]$LockWaitSec = 3000
)
$ErrorActionPreference = 'Stop'
$lock   = 'G:\Freespace\Queens_Domain\_test\locks\live'
$tools  = 'G:\Freespace\Queens_Domain\.worktrees\sync-fix\mod\tools'
$log    = 'G:\Freespace\Queens_Domain\_test\gapboss-run.txt'

function Say([string]$m) {
    $line = (Get-Date -Format 'HH:mm:ss') + ' ' + $m
    Write-Output $line
    Add-Content -LiteralPath $log -Value $line
}

Set-Content -LiteralPath $log -Value "gapboss runner start $(Get-Date -Format o)"

$deadline = (Get-Date).AddSeconds($LockWaitSec)
$got = $false
while ((Get-Date) -lt $deadline) {
    if (-not (Test-Path $lock)) {
        try {
            New-Item -ItemType Directory -Path $lock -ErrorAction Stop | Out-Null
            Set-Content -LiteralPath (Join-Path $lock 'owner.txt') -Value ("gap-boss " + (Get-Date -Format o))
            $got = $true
            Say "LIVE lock acquired"
            break
        } catch {
            Say "race on the lock; retrying"
        }
    } else {
        $owner = ''
        try { $owner = (Get-Content -LiteralPath (Join-Path $lock 'owner.txt') -Raw).Trim() } catch {}
        $ageMin = [math]::Round(((Get-Date) - (Get-Item $lock).LastWriteTime).TotalMinutes, 1)
        $games = @(Get-Process -Name 'QueensDomain-Win64-Shipping', 'qd-relay' -ErrorAction SilentlyContinue)
        if ($ageMin -gt 25 -and $games.Count -eq 0) {
            Say ("LIVE lock looks STALE (owner='{0}', age {1} min, no game/relay); removing" -f $owner, $ageMin)
            try { Remove-Item -Recurse -Force $lock } catch {}
            continue
        }
        Say ("LIVE lock held by '{0}' for {1} min ({2} game/relay process(es)); waiting 25 s" -f $owner, $ageMin, $games.Count)
    }
    Start-Sleep -Seconds 25
}

if (-not $got) {
    Say 'gave up waiting for the LIVE lock -- NOT running (no session was started)'
    exit 3
}

$rc = 2
try {
    Say ("running session-bossphase.ps1 -Phase bphase" + $(if ($Only) { " -Only $Only" } else { '' }))
    $args2 = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $tools 'session-bossphase.ps1'),
               '-Phase', 'bphase')
    if ($Only) { $args2 += @('-Only', $Only) }
    & powershell @args2 2>&1 | ForEach-Object {
        $s = [string]$_
        Write-Output $s
        Add-Content -LiteralPath $log -Value $s
    }
    $rc = $LASTEXITCODE
    Say "session exit code $rc"
} catch {
    Say ("session threw: " + $_.Exception.Message)
    $rc = 1
} finally {
    # Kill everything this run started, then release the lock.  A leftover game holds the
    # save directory and the next delegate's boot fails for a reason that is ours.
    foreach ($n in @('QueensDomain-Win64-Shipping', 'qd-relay')) {
        Get-Process -Name $n -ErrorAction SilentlyContinue | ForEach-Object {
            Say ("  stopping leftover {0} pid={1}" -f $_.ProcessName, $_.Id)
            Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue
        }
    }
    Start-Sleep -Seconds 3
    try { Remove-Item -Recurse -Force $lock } catch {}
    Say 'LIVE lock released'
}
exit $rc
