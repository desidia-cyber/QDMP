param([Parameter(Mandatory=$true)][string]$UserDir, [string]$Tag = 'hd')
$ErrorActionPreference = 'Stop'
$exe = 'G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64\QueensDomain-Win64-Shipping.exe'
$wd  = Split-Path $exe
$live = 'G:\Freespace\Queens_Domain\_live'
$p = Start-Process -FilePath $exe -WorkingDirectory $wd -ArgumentList `
    "-UserDir=`"$UserDir`"",'-SaveToUserDir','-windowed','-nosplash' -PassThru
"$($p.Id)" | Set-Content "$live\$Tag`_pid.txt"
Write-Output "LAUNCHED $Tag pid=$($p.Id) userdir=$UserDir"
for ($i = 0; $i -lt 360; $i++) {
    Start-Sleep -Seconds 5
    $q = Get-Process -Id $p.Id -ErrorAction SilentlyContinue
    if (-not $q) { Write-Output "EXITED $Tag pid=$($p.Id)"; break }
    if ($i % 12 -eq 0) { Write-Output ("ALIVE {0} {1}s ws={2:N2}GB priv={3:N2}GB" -f $Tag, ($i*5), ($q.WorkingSet64/1GB), ($q.PrivateMemorySize64/1GB)) }
}
