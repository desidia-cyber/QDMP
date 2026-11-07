# capture-hidden.ps1 -- screenshot an off-screen game window WITHOUT touching focus.
#
# WHY NOT CopyFromScreen
# ----------------------
# `_live\shot.ps1` captures the screen at the window's rectangle. That works only while
# the window is on the desktop. `run-hidden.ps1` deliberately parks the window outside
# the desktop so live runs do not take over the operator's screen, so a screen grab
# captures empty desktop. PrintWindow renders the window's own composition into a
# bitmap instead, regardless of where it sits, and never calls SetForegroundWindow.
#
# PW_RENDERFULLCONTENT (0x2) is required: without it a DWM-composited window returns a
# blank bitmap.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Out,
    [int]$ProcessId = 0,
    [string]$ProcessName = 'QueensDomain-Win64-Shipping'
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class QdCap {
  [DllImport("user32.dll")] public static extern bool EnumWindows(EP cb, IntPtr l);
  public delegate bool EP(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  public struct RECT { public int L, T, R, B; }
}
"@

if ($ProcessId -eq 0) {
    $p = Get-Process $ProcessName -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $p) { Write-Host 'NO GAME PROCESS'; exit 1 }
    $ProcessId = $p.Id
}

$script:targetPid = $ProcessId
$script:best = [IntPtr]::Zero
$script:bestArea = 0
$cb = [QdCap+EP]{
    param($hw, $l)
    $q = 0
    [QdCap]::GetWindowThreadProcessId($hw, [ref]$q) | Out-Null
    if ($q -eq $script:targetPid -and [QdCap]::IsWindowVisible($hw)) {
        $sb = New-Object System.Text.StringBuilder 512
        [QdCap]::GetWindowText($hw, $sb, 512) | Out-Null
        $r = New-Object QdCap+RECT
        [QdCap]::GetWindowRect($hw, [ref]$r) | Out-Null
        $a = ($r.R - $r.L) * ($r.B - $r.T)
        # Pick the largest visible window: the game frame, not a tooltip or splash.
        if ($a -gt $script:bestArea -and ($sb.ToString() -match 'Queen' -or $a -gt 200000)) {
            $script:bestArea = $a
            $script:best = $hw
        }
    }
    return $true
}
[QdCap]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null

$h = $script:best
if ($h -eq [IntPtr]::Zero) { Write-Host 'NO GAME WINDOW'; exit 2 }

$cr = New-Object QdCap+RECT
[QdCap]::GetClientRect($h, [ref]$cr) | Out-Null
$w = $cr.R - $cr.L; $hh = $cr.B - $cr.T
if ($w -le 0 -or $hh -le 0) { Write-Host 'ZERO-SIZED CLIENT RECT'; exit 3 }

$bmp = New-Object System.Drawing.Bitmap $w, $hh
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
$ok = [QdCap]::PrintWindow($h, $hdc, 2)   # PW_RENDERFULLCONTENT
$g.ReleaseHdc($hdc)
if (-not $ok) {
    $g.Dispose(); $bmp.Dispose()
    Write-Host 'PrintWindow FAILED'
    exit 4
}
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
Write-Host ("window 0x{0:X} {1}x{2} -> {3}" -f $h.ToInt64(), $w, $hh, $Out)
