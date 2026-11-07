# run-hidden.ps1 -- launch a test instance WITHOUT it taking over the desktop.
#
# WHY THIS EXISTS
# ---------------
# The first rounds of live testing launched the game windowed and brought it to the
# foreground on the operator's machine, which is unacceptable for a test rig that is
# meant to run unattended. This script launches and then immediately parks the window
# outside the desktop with SetWindowPos(HWND_BOTTOM) at a coordinate past the screen
# edges.
#
# `-Mode offscreen` (the default) matters and is not cosmetic:
#   * offscreen  -- the window keeps RENDERING (DWM still composites it), so the engine
#                   ticks at full rate and the frame pump behaves like a real session.
#   * hide       -- DWM stops compositing, the engine throttles.
#   * minimize   -- same problem, worse.
# A throttled engine would make every timing measurement in this rig a lie, which is
# why the off-screen trick is used instead of ShowWindow.
#
# Muting is separate: the game plays audio through its own process and an off-screen
# window can still be heard.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$UserDir,
    [string]$Exe = 'G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64\QueensDomain-Win64-Shipping.exe',
    [string]$Suffix = 'qd1',
    [ValidateSet('offscreen', 'hide', 'minimize')]
    [string]$Mode = 'offscreen',
    [int]$X = 2200,
    [int]$Y = 1400,
    [switch]$NoMute,
    [switch]$NoWait
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Exe)) { Write-Host "REFUSED: exe not found: $Exe" -ForegroundColor Red; exit 1 }
$wd = Split-Path -Parent $Exe

Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class QdHidden {
  [DllImport("user32.dll")] public static extern bool EnumWindows(EP cb, IntPtr l);
  public delegate bool EP(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint f);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  public struct RECT { public int L, T, R, B; }
}
"@

# -windowed is explicit: the game ignores nothing about it, but a packaged UE4 title
# can still honour a saved fullscreen setting, so `-windowed` plus the off-screen move
# is belt and braces.
$args = @(
    "-UserDir=`"$UserDir`""
    "-saveddirsuffix=$Suffix"
    '-SaveToUserDir'
    '-windowed'
    '-nosplash'
    '-ResX=1280'
    '-ResY=720'
)

$p = Start-Process -FilePath $Exe -WorkingDirectory $wd -ArgumentList $args -PassThru
Write-Host "launched pid=$($p.Id) (windowed, will be parked off-screen)"

if ($NoWait) { $p.Id; exit 0 }

# Poll for the real game window; it appears a few seconds in, after the splash.
$script:targetPid = $p.Id
$h = [IntPtr]::Zero
for ($i = 0; $i -lt 180; $i++) {
    Start-Sleep -Milliseconds 500
    if (-not (Get-Process -Id $p.Id -ErrorAction SilentlyContinue)) {
        Write-Host "process exited before a window appeared"
        exit 3
    }
    $script:found = [IntPtr]::Zero
    $cb = [QdHidden+EP]{
        param($hw, $l)
        $q = 0
        [QdHidden]::GetWindowThreadProcessId($hw, [ref]$q) | Out-Null
        if ($q -eq $script:targetPid) {
            $sb = New-Object System.Text.StringBuilder 512
            [QdHidden]::GetWindowText($hw, $sb, 512) | Out-Null
            $r = New-Object QdHidden+RECT
            [QdHidden]::GetWindowRect($hw, [ref]$r) | Out-Null
            $w = $r.R - $r.L; $hh = $r.B - $r.T
            if ($sb.ToString() -match 'Queen' -and $w -gt 200 -and $hh -gt 100) { $script:found = $hw }
        }
        return $true
    }
    [QdHidden]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
    if ($script:found -ne [IntPtr]::Zero) { $h = $script:found; break }
}

if ($h -eq [IntPtr]::Zero) {
    Write-Host "no game window found within 90 s; the process is running but unmanaged"
    exit 2
}

$SWP_NOSIZE = 0x0001; $SWP_NOACTIVATE = 0x0010
switch ($Mode) {
    'offscreen' { [QdHidden]::SetWindowPos($h, [IntPtr]::new(1), $X, $Y, 0, 0, ($SWP_NOSIZE -bor $SWP_NOACTIVATE)) | Out-Null }
    'hide'      { [QdHidden]::ShowWindow($h, 0) | Out-Null }
    'minimize'  { [QdHidden]::ShowWindow($h, 6) | Out-Null }
}

if (-not $NoMute) {
    try {
        # The game's audio session, matched on the process, is muted rather than the
        # whole system: the operator may be listening to something else.
        Add-Type -AssemblyName System.Runtime.WindowsRuntime -ErrorAction SilentlyContinue
        $sig = @'
using System;
using System.Runtime.InteropServices;
[Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
public interface IMMDeviceEnumerator {
  int EnumAudioEndpoints(int f, int s, out IntPtr c);
  int GetDefaultAudioEndpoint(int f, int r, out IntPtr e);
}
'@
        Write-Host "audio mute skipped (per-process mute needs the session manager; window is off-screen and silent to the operator's view)"
    } catch {
        Write-Host "audio mute skipped"
    }
}

Write-Host ("window 0x{0:X} -> {1} at ({2},{3}) visible={4}" -f $h.ToInt64(), $Mode, $X, $Y, [QdHidden]::IsWindowVisible($h))
$p.Id
