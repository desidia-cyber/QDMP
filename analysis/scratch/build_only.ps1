$ErrorActionPreference = "Continue"
$w = "G:\Freespace\Queens_Domain\.worktrees\sync-fix"
$b = "G:\Freespace\Queens_Domain\_test\locks\build"
Start-Sleep -Seconds 90
$got = $false
for ($i = 0; $i -lt 600; $i++) {
    if (-not (Test-Path $b)) {
        try {
            New-Item -ItemType Directory -Path $b -ErrorAction Stop | Out-Null
            Set-Content -Path "$b\owner.txt" -Value "world-interact"
            $got = $true
            break
        } catch { }
    }
    Start-Sleep -Seconds 2
}
if (-not $got) { Write-Output "LOCK NOT ACQUIRED"; exit 3 }
try {
    Write-Output "LOCK ACQUIRED $(Get-Date -Format o)"
    Set-Location $w
    cmd /c "mod\native\build.bat" 2>&1 | Select-Object -Last 16
    Write-Output "BUILD_EXIT=$LASTEXITCODE"
}
finally {
    if (Test-Path $b) {
        if ((Get-Content "$b\owner.txt" -ErrorAction SilentlyContinue) -eq "world-interact") {
            Remove-Item $b -Recurse -Force
            Write-Output "LOCK RELEASED"
        }
    }
}
