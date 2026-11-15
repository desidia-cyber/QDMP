$ErrorActionPreference = "Continue"
$w = "G:\Freespace\Queens_Domain\.worktrees\sync-fix"
$b = "G:\Freespace\Queens_Domain\_test\locks\build"
$got = $false
for ($i = 0; $i -lt 600; $i++) {          # up to 20 minutes of 2 s polls
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
    python analysis/scratch/apply_world_interact2.py 2>&1
    Write-Output "PATCH_EXIT=$LASTEXITCODE"
    cmd /c "mod\native\build.bat" 2>&1 | Select-Object -Last 12
    Write-Output "BUILD_EXIT=$LASTEXITCODE"
    cmd /c "mod\native\test\run_tests.bat" 2>&1 | Tee-Object -FilePath "$w\analysis\scratch\tests_wi3.log" | Select-Object -Last 6
    Write-Output "TEST_EXIT=$LASTEXITCODE"
}
finally {
    if (Test-Path $b) {
        if ((Get-Content "$b\owner.txt" -ErrorAction SilentlyContinue) -eq "world-interact") {
            Remove-Item $b -Recurse -Force
            Write-Output "LOCK RELEASED"
        }
    }
}
