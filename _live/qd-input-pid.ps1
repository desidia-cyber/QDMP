param(
  [Parameter(Mandatory=$true)][int]$ProcessId,
  [int[]]$Click = @(),
  [int]$Key = 0
)
$ErrorActionPreference = 'Stop'
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class QdIn2 {
  [DllImport("user32.dll")] public static extern bool EnumWindows(EP cb, IntPtr l);
  public delegate bool EP(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
  public struct RECT { public int L, T, R, B; }
}
"@
$script:gpid = $ProcessId
$script:h = [IntPtr]::Zero
$cb = [QdIn2+EP]{
  param($w,$l)
  $q = 0; [QdIn2]::GetWindowThreadProcessId($w,[ref]$q) | Out-Null
  if ($q -eq $script:gpid) {
    $sb = New-Object System.Text.StringBuilder 512; [QdIn2]::GetWindowText($w,$sb,512) | Out-Null
    $r = New-Object QdIn2+RECT; [QdIn2]::GetClientRect($w,[ref]$r) | Out-Null
    if (($r.R-$r.L) -gt 200) { $script:h = $w }
  }
  return $true
}
[QdIn2]::EnumWindows($cb,[IntPtr]::Zero) | Out-Null
$h = $script:h
if ($h -eq [IntPtr]::Zero) { Write-Output 'NO WINDOW'; exit 2 }
$WM_MOUSEMOVE=0x0200; $WM_LBUTTONDOWN=0x0201; $WM_LBUTTONUP=0x0202
$WM_KEYDOWN=0x0100; $WM_KEYUP=0x0101
if ($Click.Count -eq 2) {
  $x=$Click[0]; $y=$Click[1]; $lp=[IntPtr](($y -shl 16) -bor ($x -band 0xFFFF))
  [QdIn2]::PostMessage($h,$WM_MOUSEMOVE,[IntPtr]::Zero,$lp) | Out-Null
  Start-Sleep -Milliseconds 60
  [QdIn2]::PostMessage($h,$WM_LBUTTONDOWN,[IntPtr]1,$lp) | Out-Null
  Start-Sleep -Milliseconds 60
  [QdIn2]::PostMessage($h,$WM_LBUTTONUP,[IntPtr]::Zero,$lp) | Out-Null
}
if ($Key -gt 0) {
  [QdIn2]::PostMessage($h,$WM_KEYDOWN,[IntPtr]$Key,[IntPtr]0x001C0001) | Out-Null
  Start-Sleep -Milliseconds 80
  [QdIn2]::PostMessage($h,$WM_KEYUP,[IntPtr]$Key,[IntPtr]0xC01C0001) | Out-Null
}
Write-Output ("pid={0} hwnd={1} key={2}" -f $ProcessId,$h,$Key)
