<#
qd-input.ps1 -- drive the (off-screen) game window without stealing focus.

Uses PostMessage to the window handle, so the window never has to be visible, focused,
or moved. Works for UMG/Slate buttons because Slate dispatches WM_* mouse messages.

  -Click x,y     client coordinates
  -Key VK        virtual-key code (13 = Enter)
#>
param(
  [int[]]$Click = @(),
  [int]$Key = 0,
  [switch]$BringOnScreen
)
$ErrorActionPreference = 'Stop'
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class QdIn {
  [DllImport("user32.dll")] public static extern bool EnumWindows(EP cb, IntPtr l);
  public delegate bool EP(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint f);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  public struct RECT { public int L, T, R, B; }
}
"@
$p = Get-Process QueensDomain-Win64-Shipping -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $p) { Write-Output 'NO GAME PROCESS'; exit 1 }
$script:gpid = $p.Id
$script:h = [IntPtr]::Zero
$cb = [QdIn+EP]{
  param($w,$l)
  $q = 0; [QdIn]::GetWindowThreadProcessId($w,[ref]$q) | Out-Null
  if ($q -eq $script:gpid) {
    $sb = New-Object System.Text.StringBuilder 512; [QdIn]::GetWindowText($w,$sb,512) | Out-Null
    $r = New-Object QdIn+RECT; [QdIn]::GetWindowRect($w,[ref]$r) | Out-Null
    if ($sb.ToString() -match 'Queen' -and ($r.R-$r.L) -gt 200) { $script:h = $w }
  }
  return $true
}
[QdIn]::EnumWindows($cb,[IntPtr]::Zero) | Out-Null
$h = $script:h
if ($h -eq [IntPtr]::Zero) { Write-Output 'NO WINDOW'; exit 2 }

$cr = New-Object QdIn+RECT
[QdIn]::GetClientRect($h,[ref]$cr) | Out-Null
$cw = $cr.R - $cr.L; $ch = $cr.B - $cr.T
Write-Output ("hwnd={0} client={1}x{2} pid={3}" -f $h, $cw, $ch, $p.Id)

if ($BringOnScreen) {
  [QdIn]::SetWindowPos($h, [IntPtr]::Zero, 40, 40, 0, 0, 0x0001 -bor 0x0010) | Out-Null
  Write-Output 'moved on-screen at (40,40)'
}

$WM_MOUSEMOVE=0x0200; $WM_LBUTTONDOWN=0x0201; $WM_LBUTTONUP=0x0202
$WM_KEYDOWN=0x0100; $WM_KEYUP=0x0101

if ($Click.Count -eq 2) {
  $x = $Click[0]; $y = $Click[1]
  $lp = [IntPtr](($y -shl 16) -bor ($x -band 0xFFFF))
  [QdIn]::PostMessage($h, $WM_MOUSEMOVE, [IntPtr]::Zero, $lp) | Out-Null
  Start-Sleep -Milliseconds 60
  [QdIn]::PostMessage($h, $WM_LBUTTONDOWN, [IntPtr]1, $lp) | Out-Null
  Start-Sleep -Milliseconds 60
  [QdIn]::PostMessage($h, $WM_LBUTTONUP, [IntPtr]::Zero, $lp) | Out-Null
  Write-Output ("clicked client ({0},{1})" -f $x, $y)
}

if ($Key -gt 0) {
  [QdIn]::PostMessage($h, $WM_KEYDOWN, [IntPtr]$Key, [IntPtr]0x001C0001) | Out-Null
  Start-Sleep -Milliseconds 80
  [QdIn]::PostMessage($h, $WM_KEYUP, [IntPtr]$Key, [IntPtr]0xC01C0001) | Out-Null
  Write-Output ("sent key VK={0}" -f $Key)
}
