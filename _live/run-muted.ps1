$ErrorActionPreference = 'Stop'
$exe = 'G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64\QueensDomain-Win64-Shipping.exe'
$wd  = Split-Path $exe
$live = 'G:\Freespace\Queens_Domain\_live'
$p = Start-Process -FilePath $exe -WorkingDirectory $wd -ArgumentList `
    '-UserDir="G:\Freespace\Queens_Domain\_live\inst1saved"','-SaveToUserDir','-windowed','-nosplash' -PassThru
"$($p.Id)" | Set-Content "$live\real_pid.txt"
Write-Output "LAUNCHED pid=$($p.Id)"
# park + mute as soon as a window exists
for ($i = 0; $i -lt 100; $i++) {
    Start-Sleep -Milliseconds 250
    if ($p.HasExited) { break }
    $w = Get-Process -Id $p.Id -ErrorAction SilentlyContinue
    if ($w -and $w.MainWindowHandle -ne 0) {
        & powershell -ExecutionPolicy Bypass -File "$live\hide-qd.ps1" -Mode offscreen -Mute
        Write-Output "PARKED plus MUTED"
        break
    }
}
while (-not $p.HasExited) { Start-Sleep -Seconds 5 }
Write-Output "EXITED $($p.ExitCode)"
