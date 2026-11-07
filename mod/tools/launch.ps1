# launch.ps1 -- start a co-op session.
#
# WHAT IT DOES, IN ORDER
# ---------------------
# 1. Confirms the mod is actually installed (winmm.dll beside the shipping exe).
#    Launching two instances of a vanilla game looks exactly like "the mod does not
#    work", and the log would be empty rather than wrong -- so this is a refusal, not
#    a warning.
# 2. Runs save_guard.ps1 once per instance, with the other instance's path to
#    compare against, BEFORE starting anything. This is the ordering the previous
#    project learned the hard way: a save editor that ignored its --root edited the
#    operator's real save.
# 3. Starts each instance with its own -UserDir so the two never share a save file.
# 4. Prints the relay command, which must be running first.
#
# WHY -UserDir AND NOT A COPIED GAME FOLDER
# ----------------------------------------
# Both isolation levers were verified on this binary (analysis gap E1): a full session
# ran under -UserDir with the real save untouched (mtime-verified). Sharing Content is
# what keeps two instances from costing 2 x 1.9 GB of pak.
#
# -DryRun prints every command without starting anything.

[CmdletBinding()]
param(
    [string]$GameDir = 'G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64',
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [string]$ExeName = 'QueensDomain-Win64-Shipping.exe',
    [int]$Instances = 2,
    [string]$RelayHost = '127.0.0.1',
    [int]$RelayPort = 47800,
    [switch]$DryRun,
    [switch]$AllowRealProfile
)

$ErrorActionPreference = 'Stop'
function Say($m)  { Write-Host $m }
function Head($m) { Write-Host "`n=== $m ===" -ForegroundColor Cyan }
function Ok($m)   { Write-Host "  [ok]   $m" -ForegroundColor Green }
function Warn($m) { Write-Host "  [warn] $m" -ForegroundColor Yellow }
function Fail($m) { Write-Host "REFUSED: $m" -ForegroundColor Red; exit 1 }

$exe = Join-Path $GameDir $ExeName
if (-not (Test-Path -LiteralPath $exe)) {
    Fail "shipping exe not found at $exe. Pass -GameDir with the directory that holds $ExeName."
}

$proxy = Join-Path $GameDir 'winmm.dll'
if (-not (Test-Path -LiteralPath $proxy)) {
    Fail "the mod is not installed: no winmm.dll beside $ExeName. Run tools\install.ps1 first."
}
Ok "mod installed : $proxy"

$ini = Join-Path $GameDir 'qdcoop.ini'
if (Test-Path -LiteralPath $ini) {
    Ok "config        : $ini"
} else {
    Warn "no qdcoop.ini beside the exe; built-in defaults apply (all write paths OFF)"
}

$guard = Join-Path $PSScriptRoot 'save_guard.ps1'
if (-not (Test-Path -LiteralPath $guard)) {
    Fail "save_guard.ps1 not found next to this script"
}

if ($Instances -lt 1 -or $Instances -gt 2) {
    # Two is the mod's design point: one host, one peer. More would need a session
    # model the wire format does not have.
    Fail "-Instances must be 1 or 2"
}

# Build the instance list first so each guard call can see the others.
$dirs = @()
for ($i = 1; $i -le $Instances; $i++) {
    $dirs += (Join-Path $Root "inst$i")
}

Head 'save guard'
foreach ($d in $dirs) {
    $saves = Join-Path $d 'Saved\SaveGames'
    if (-not (Test-Path -LiteralPath $saves)) {
        Warn "save directory does not exist yet: $saves"
        Warn "  (the game creates it on first run; create instances with tools\make_test_instance.ps1)"
    }
    $others = @($dirs | Where-Object { $_ -ne $d } | ForEach-Object { Join-Path $_ 'Saved\SaveGames' })
    $args = @('-SavePath', $saves, '-InstanceRoot', $d)
    if ($others.Count -gt 0) { $args += @('-OtherInstancePath', ($others -join ';')) }
    if ($AllowRealProfile)   { $args += '-AllowRealProfile' }

    if ($DryRun) {
        Say "  would run: save_guard.ps1 $($args -join ' ')"
    } else {
        & powershell -NoProfile -ExecutionPolicy Bypass -File $guard @args
        if ($LASTEXITCODE -ne 0) { Fail "save guard did not pass for $saves" }
    }
}

Head 'relay'
Say "  start this first, in its own window:"
Say "    $PSScriptRoot\..\rust\target\release\qd-relay.exe --bind ${RelayHost}:${RelayPort}"
Say "  (or --two <addr> --two <addr> for a pinned LAN test with no discovery)"

Head 'instances'
$started = 0
foreach ($d in $dirs) {
    $ude = "-UserDir=`"$d`""
    Say "  $exe $ude"
    if (-not $DryRun) {
        if (-not (Test-Path -LiteralPath $d)) { New-Item -ItemType Directory -Force -Path $d | Out-Null }
        Start-Process -FilePath $exe -ArgumentList $ude -WorkingDirectory $GameDir
        ++$started
    }
}

Say ''
if ($DryRun) {
    Say "dry run: nothing was started."
} else {
    Say "started $started instance(s)."
    Say "logs: $GameDir\qdcoop.log  (both instances append to the same file;"
    Say "      the pid line at the top of each session separates them)"
}
