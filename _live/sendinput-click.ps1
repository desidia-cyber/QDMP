param([int]$ProcessId = 0, [int]$ScreenX = 360, [int]$ScreenY = 220, [int]$Count = 10, [int]$Gap = 220)
$ErrorActionPreference = 'Stop'
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class QdClick {
  [DllImport("user32.dll")] public static extern bool EnumWindows(EP cb, IntPtr l);
  public delegate bool EP(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, UIntPtr e);
}
"@
if ($ProcessId -eq 0) {
  $ProcessId = (Get-Process QueensDomain-Win64-Shipping -ErrorAction Stop | Select-Object -First 1).Id
}
$script:gpid = $ProcessId
$script:h = [IntPtr]::Zero
$cb = [QdClick+EP]{
  param($w,$l)
  $q = 0; [QdClick]::GetWindowThreadProcessId($w,[ref]$q) | Out-Null
  if ($q -eq $script:gpid) { $script:h = $w }
  return $true
}
[QdClick]::EnumWindows($cb,[IntPtr]::Zero) | Out-Null
if ($script:h -eq [IntPtr]::Zero) { Write-Output 'NO WINDOW'; exit 2 }
[QdClick]::ShowWindow($script:h, 9) | Out-Null
[QdClick]::SetForegroundWindow($script:h) | Out-Null
Start-Sleep -Milliseconds 250
[QdClick]::SetCursorPos($ScreenX, $ScreenY) | Out-Null
Start-Sleep -Milliseconds 120
for ($i = 0; $i -lt $Count; $i++) {
  [QdClick]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)   # LEFTDOWN
  Start-Sleep -Milliseconds 45
  [QdClick]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)   # LEFTUP
  Start-Sleep -Milliseconds $Gap
}
Write-Output ("sendinput LMB x{0} at ({1},{2}) hwnd={3}" -f $Count, $ScreenX, $ScreenY, $script:h)
