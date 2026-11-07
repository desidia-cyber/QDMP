<#
hide-qd.ps1 -- park the test instance off-screen, behind everything, and mute it.

Same technique as the DollsMP rig's dmpbot.py: SetWindowPos to a coordinate outside
the desktop and push to HWND_BOTTOM, so the game keeps rendering (DWM still composites
an off-screen window) while the owner never sees it. Capture then uses PrintWindow.

  -Mode offscreen  (default) small window parked outside the desktop, behind everything
  -Mode hide       ShowWindow(SW_HIDE)  -- fully invisible, but DWM stops compositing
  -Mode minimize   SW_MINIMIZE          -- engine usually stops rendering
#>
param(
  [ValidateSet('offscreen','hide','minimize')][string]$Mode = 'offscreen',
  [int]$X = 2200, [int]$Y = 1400,
  [switch]$Mute
)
$ErrorActionPreference = 'Stop'
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class QdWin {
  [DllImport("user32.dll")] public static extern bool EnumWindows(EP cb, IntPtr l);
  public delegate bool EP(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint f);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h, int x, int y, int w, int hh, bool repaint);
  public struct RECT { public int L, T, R, B; }
}
"@
$SWP_NOSIZE=0x0001; $SWP_NOACTIVATE=0x0010; $SWP_NOZORDER=0x0004
$HWND_BOTTOM = [IntPtr]::new(1)

$proc = Get-Process QueensDomain-Win64-Shipping -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $proc) { Write-Output 'NO GAME PROCESS'; exit 1 }
$script:gpid = $proc.Id
$script:found = [IntPtr]::Zero
$cb = [QdWin+EP]{
  param($h,$l)
  $p = 0; [QdWin]::GetWindowThreadProcessId($h,[ref]$p) | Out-Null
  if ($p -eq $script:gpid) {
    $sb = New-Object System.Text.StringBuilder 512
    [QdWin]::GetWindowText($h,$sb,512) | Out-Null
    $r = New-Object QdWin+RECT
    [QdWin]::GetWindowRect($h,[ref]$r) | Out-Null
    $w = $r.R - $r.L; $hh = $r.B - $r.T
    if ($sb.ToString() -match 'Queen' -and $w -gt 200 -and $hh -gt 100) { $script:found = $h }
  }
  return $true
}
[QdWin]::EnumWindows($cb,[IntPtr]::Zero) | Out-Null
if ($script:found -eq [IntPtr]::Zero) { Write-Output 'NO GAME WINDOW (yet)'; exit 2 }
$h = $script:found

switch ($Mode) {
  'offscreen' {
    [QdWin]::SetWindowPos($h, $HWND_BOTTOM, $X, $Y, 0, 0, ($SWP_NOSIZE -bor $SWP_NOACTIVATE)) | Out-Null
  }
  'hide' { [QdWin]::ShowWindow($h, 0) | Out-Null }
  'minimize' { [QdWin]::ShowWindow($h, 6) | Out-Null }
}
Write-Output ("window {0} -> mode {1} at ({2},{3}) visible={4}" -f $h, $Mode, $X, $Y, [QdWin]::IsWindowVisible($h))

if ($Mute) {
  & 'G:\Freespace\Queens_Domain\_live\mute-qd.ps1' -Once -Match '\\Queens_Domain\\QueensDomain\\'
}
