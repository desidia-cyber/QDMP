# Launch the clean install (no UE4SS) against an isolated -UserDir, held by a sleep loop.
$ErrorActionPreference = 'Stop'
$exe = 'G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64\QueensDomain-Win64-Shipping.exe'
$wd  = Split-Path $exe
$live = 'G:\Freespace\Queens_Domain\_live'
Remove-Item "$live\frida_clock.csv", "$live\key_marker.txt", "$live\real_pid.txt" -ErrorAction SilentlyContinue
$p = Start-Process -FilePath $exe -WorkingDirectory $wd -ArgumentList `
    '-UserDir="G:\Freespace\Queens_Domain\_live\inst1saved"','-SaveToUserDir','-windowed','-nosplash' -PassThru
"$($p.Id)" | Set-Content "$live\real_pid.txt"
Write-Output "LAUNCHED pid=$($p.Id)"
for ($i = 0; $i -lt 720; $i++) {
    Start-Sleep -Seconds 5
    $q = Get-Process -Id $p.Id -ErrorAction SilentlyContinue
    if (-not $q) { Write-Output "EXITED pid=$($p.Id)"; "EXITED" | Add-Content "$live\real_pid.txt"; break }
    if ($i % 12 -eq 0) { Write-Output ("ALIVE {0}s ws={1:N2}GB" -f ($i*5), ($q.WorkingSet64/1GB)) }
}
