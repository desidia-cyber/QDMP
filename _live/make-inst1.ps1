$ErrorActionPreference = 'Stop'
$real = 'G:\Freespace\Queens_Domain\QueensDomain'
$inst = 'G:\Freespace\Queens_Domain\_live\inst1'
$u4   = 'G:\Freespace\dollsMP\tools\thirdparty\ue4ss'

if (Test-Path $inst) { Remove-Item $inst -Recurse -Force -ErrorAction SilentlyContinue }
New-Item -ItemType Directory -Force "$inst\QueensDomain\Binaries\Win64" | Out-Null

# 1. junction the heavy read-only trees so we do not duplicate 1.9 GB
cmd /c mklink /J "$inst\Engine" "$real\Engine" | Out-Null
New-Item -ItemType Directory -Force "$inst\QueensDomain\Content" | Out-Null
cmd /c mklink /J "$inst\QueensDomain\Content\Paks" "$real\QueensDomain\Content\Paks" | Out-Null
cmd /c mklink /J "$inst\QueensDomain\Content\Movies" "$real\QueensDomain\Content\Movies" | Out-Null
Write-Output 'junctions: Engine, Content\Paks, Content\Movies'

# 2. hard-link the exe (same volume -> no copy)
$exe = "$real\QueensDomain\Binaries\Win64\QueensDomain-Win64-Shipping.exe"
$dst = "$inst\QueensDomain\Binaries\Win64\QueensDomain-Win64-Shipping.exe"
cmd /c mklink /H "$dst" "$exe" | Out-Null
if (-not (Test-Path $dst)) { Copy-Item $exe $dst }
Write-Output ("exe: {0:N0} bytes" -f (Get-Item $dst).Length)

# 3. UE4SS into the same folder as the exe
Copy-Item "$u4\*" "$inst\QueensDomain\Binaries\Win64\" -Recurse -Force
Write-Output 'UE4SS copied'

$s = "$inst\QueensDomain\Binaries\Win64\UE4SS-settings.ini"
$c = Get-Content $s -Raw
$c = $c -replace 'ConsoleEnabled = 1', 'ConsoleEnabled = 0'
$c = $c -replace 'GuiConsoleVisible = 1', 'GuiConsoleVisible = 0'
$c = $c -replace 'SecondsToScanBeforeGivingUp = 30', 'SecondsToScanBeforeGivingUp = 120'
Set-Content -LiteralPath $s -Value $c -Encoding UTF8
Write-Output 'settings: console off, gui off, scan window 120s'

Get-ChildItem "$inst\QueensDomain\Binaries\Win64" | Select-Object @{n='KB';e={[math]::Round($_.Length/1KB,1)}},Name | Format-Table -AutoSize
