param([int]$Key = 87, [int]$Ms = 1000, [int]$ProcessId = 0)
$ErrorActionPreference = 'Stop'
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class QdHold {
  [DllImport("user32.dll")] public static extern bool EnumWindows(EP cb, IntPtr l);
  public delegate bool EP(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
  public struct RECT { public int L, T, R, B; }
}
"@
if ($ProcessId -eq 0) {
  $ProcessId = (Get-Process QueensDomain-Win64-Shipping -ErrorAction Stop | Select-Object -First 1).Id
}
$script:gpid = $ProcessId
$script:h = [IntPtr]::Zero
$cb = [QdHold+EP]{
  param($w,$l)
  $q = 0; [QdHold]::GetWindowThreadProcessId($w,[ref]$q) | Out-Null
  if ($q -eq $script:gpid) {
    $sb = New-Object System.Text.StringBuilder 512; [QdHold]::GetWindowText($w,$sb,512) | Out-Null
    $r = New-Object QdHold+RECT; [QdHold]::GetClientRect($w,[ref]$r) | Out-Null
    if (($r.R-$r.L) -gt 200) { $script:h = $w }
  }
  return $true
}
[QdHold]::EnumWindows($cb,[IntPtr]::Zero) | Out-Null
if ($script:h -eq [IntPtr]::Zero) { Write-Output 'NO WINDOW'; exit 2 }
$WM_KEYDOWN=0x0100; $WM_KEYUP=0x0101
[QdHold]::PostMessage($script:h,$WM_KEYDOWN,[IntPtr]$Key,[IntPtr]0x001C0001) | Out-Null
Start-Sleep -Milliseconds $Ms
[QdHold]::PostMessage($script:h,$WM_KEYUP,[IntPtr]$Key,[IntPtr]0xC01C0001) | Out-Null
Write-Output ("held VK={0} for {1} ms" -f $Key,$Ms)
