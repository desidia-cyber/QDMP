param(
  [string]$Out = 'G:\Freespace\Queens_Domain\analysis\live-shot.png',
  [int]$Focus = 1
)
Add-Type -AssemblyName System.Windows.Forms,System.Drawing
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class WW {
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr l);
  public delegate bool EnumWindowsProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint f);
  public struct RECT { public int L,T,R,B; }
}
"@
$proc = Get-Process QueensDomain-Win64-Shipping -ErrorAction SilentlyContinue | Select-Object -First 1
if(-not $proc){ 'NO GAME PROCESS'; exit 1 }
$script:gpid = $proc.Id
$script:target = [IntPtr]::Zero
$script:rect = $null
$cb = [WW+EnumWindowsProc]{
  param($h,$l)
  $pid2 = 0; [WW]::GetWindowThreadProcessId($h,[ref]$pid2) | Out-Null
  if($pid2 -eq $script:gpid -and [WW]::IsWindowVisible($h)){
    $sb = New-Object System.Text.StringBuilder 512; [WW]::GetWindowText($h,$sb,512) | Out-Null
    $r = New-Object WW+RECT; [WW]::GetWindowRect($h,[ref]$r) | Out-Null
    $w = $r.R-$r.L; $hh = $r.B-$r.T
    if($w -gt 300 -and $hh -gt 300){
      $script:target = $h; $script:rect = $r
    }
  }
  return $true
}
[WW]::EnumWindows($cb,[IntPtr]::Zero) | Out-Null
if($script:target -eq [IntPtr]::Zero){ 'NO GAME WINDOW'; exit 1 }
if($Focus){
  [WW]::ShowWindow($script:target,9) | Out-Null
  [WW]::SetWindowPos($script:target,[IntPtr]::Zero,0,0,0,0,0x0043) | Out-Null
  [WW]::SetForegroundWindow($script:target) | Out-Null
  Start-Sleep -Milliseconds 700
}
$r = $script:rect
$w = $r.R-$r.L; $h2 = $r.B-$r.T
$bmp = New-Object System.Drawing.Bitmap $w, $h2
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen(([System.Drawing.Point]::new($r.L,$r.T)), [System.Drawing.Point]::Empty, ([System.Drawing.Size]::new($w,$h2)))
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
"window ${w}x${h2} at ($($r.L),$($r.T)) -> $Out"
