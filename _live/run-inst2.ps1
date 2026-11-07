# Launch a second instance with its own -UserDir, held by a sleep loop.
$ErrorActionPreference = 'Stop'
$exe = 'G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64\QueensDomain-Win64-Shipping.exe'
$wd  = Split-Path $exe
$live = 'G:\Freespace\Queens_Domain\_live'
$ud  = "$live\inst2saved"
if (-not (Test-Path "$ud\Saved\SaveGames\World0.sav")) {
  New-Item -ItemType Directory -Force "$ud" | Out-Null
  Copy-Item -Recurse -Force "$live\inst1saved\Saved" "$ud\Saved"
}
Remove-Item "$live\inst2_pid.txt" -ErrorAction SilentlyContinue
$p = Start-Process -FilePath $exe -WorkingDirectory $wd -ArgumentList `
    "-UserDir=`"$ud`"",'-SaveToUserDir','-windowed','-nosplash' -PassThru
"$($p.Id)" | Set-Content "$live\inst2_pid.txt"
Write-Output "LAUNCHED2 pid=$($p.Id) userdir=$ud"
for ($i = 0; $i -lt 720; $i++) {
    Start-Sleep -Seconds 5
    $q = Get-Process -Id $p.Id -ErrorAction SilentlyContinue
    if (-not $q) { Write-Output "EXITED2 pid=$($p.Id)"; break }
    if ($i % 12 -eq 0) { Write-Output ("ALIVE2 {0}s ws={1:N2}GB priv={2:N2}GB" -f ($i*5), ($q.WorkingSet64/1GB), ($q.PrivateMemorySize64/1GB)) }
}
