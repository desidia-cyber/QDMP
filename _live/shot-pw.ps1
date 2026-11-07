param([string]$Out = 'G:\Freespace\Queens_Domain\_live\pw.png')
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class QdPW {
  [DllImport("user32.dll")] public static extern bool EnumWindows(EP cb, IntPtr l);
  public delegate bool EP(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
  public struct RECT { public int L, T, R, B; }
}
"@
$proc = Get-Process QueensDomain-Win64-Shipping -ErrorAction SilentlyContinue | Select-Object -First 1
if(-not $proc){ 'NO GAME PROCESS'; exit 1 }
$script:gpid = $proc.Id; $script:h = [IntPtr]::Zero
$cb = [QdPW+EP]{
  param($w,$l)
  $q=0; [QdPW]::GetWindowThreadProcessId($w,[ref]$q) | Out-Null
  if($q -eq $script:gpid){ $r=New-Object QdPW+RECT; [QdPW]::GetWindowRect($w,[ref]$r)|Out-Null; if(($r.R-$r.L) -gt 300){$script:h=$w} }
  return $true
}
[QdPW]::EnumWindows($cb,[IntPtr]::Zero) | Out-Null
if($script:h -eq [IntPtr]::Zero){ 'NO WINDOW'; exit 2 }
$r=New-Object QdPW+RECT; [QdPW]::GetWindowRect($script:h,[ref]$r) | Out-Null
$w=$r.R-$r.L; $hh=$r.B-$r.T
$bmp=New-Object System.Drawing.Bitmap $w,$hh
$g=[System.Drawing.Graphics]::FromImage($bmp)
$dc=$g.GetHdc()
$ok=[QdPW]::PrintWindow($script:h,$dc,2)
$g.ReleaseHdc($dc)
$bmp.Save($Out,[System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
"printwindow=$ok ${w}x${hh} -> $Out"
