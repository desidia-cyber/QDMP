param([int]$ProcessId = 0, [int]$Vk = 0x45)
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class QdKey {
  [DllImport("user32.dll")] public static extern bool EnumWindows(EP cb, IntPtr l);
  public delegate bool EP(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint type);
  [DllImport("user32.dll")] public static extern int GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  public struct RECT { public int L, T, R, B; }
}
"@
if ($ProcessId -eq 0) { $ProcessId = (Get-Process QueensDomain-Win64-Shipping | Select-Object -First 1).Id }
$script:gpid = $ProcessId; $script:h = [IntPtr]::Zero; $script:area = 0
$cb = [QdKey+EP]{
  param($w,$l)
  $q=0; [QdKey]::GetWindowThreadProcessId($w,[ref]$q) | Out-Null
  if ($q -eq $script:gpid) {
    $r = New-Object QdKey+RECT; [QdKey]::GetClientRect($w,[ref]$r) | Out-Null
    $a = ($r.R-$r.L)*($r.B-$r.T)
    if ($a -gt $script:area) { $script:area = $a; $script:h = $w }
  }
  return $true
}
[QdKey]::EnumWindows($cb,[IntPtr]::Zero) | Out-Null
if ($script:h -eq [IntPtr]::Zero) { 'NO WINDOW'; exit 2 }
$scan = [QdKey]::MapVirtualKey([uint32]$Vk, 0)
$down = [IntPtr]([int64](1 -bor ($scan -shl 16)))
$up   = [IntPtr]([int64](0xC0000001 -bor ($scan -shl 16)))
[QdKey]::PostMessage($script:h, 0x0100, [IntPtr]$Vk, $down) | Out-Null
Start-Sleep -Milliseconds 90
[QdKey]::PostMessage($script:h, 0x0101, [IntPtr]$Vk, $up) | Out-Null
"vk=$Vk scan=$scan hwnd=$($script:h)"
