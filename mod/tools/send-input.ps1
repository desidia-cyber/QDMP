# send-input.ps1 -- post input to the game's REAL window without taking focus.
#
# WHY THIS IS NOT _live\press-pid.ps1
# -----------------------------------
# That script picks the window with the largest client rect, which on this game is not
# always the frame the player sees: with the session parked off-screen there are several
# windows owned by the process, and posting keys at the wrong one does nothing (measured:
# Enter at the main menu produced no state change). This picks the same window that
# tools\capture-hidden.ps1 captures -- largest VISIBLE window with a Queen's Domain title
# -- so "what we key" and "what we see" are the same surface.
#
# Mouse messages carry client coordinates, so a click needs no cursor movement and no
# focus. That is the only reason this is safe to run against an off-screen window.
[CmdletBinding()]
param(
    [int]$ProcessId = 0,
    [string]$ProcessName = 'QueensDomain-Win64-Shipping',
    [int]$Vk = 0,
    [int]$ClickX = -1,
    [int]$ClickY = -1,
    # Fractional coordinates are computed from the measured client rect, so a session
    # does not break when the game comes up at a different resolution. Measured: this
    # build produced 1920x1080 on one launch and 1280x720 on the next from the same
    # arguments, so absolute pixels are not a stable way to address a menu item.
    [double]$ClickFracX = 0.0,
    [double]$ClickFracY = 0.0,
    [switch]$StickyKeys,  # leave the key down (for held actions)
    [int]$HoldMs = 60,    # how long the key stays down before the key-up
    # In-game mouse input at the client centre: 'left' / 'right' button held for
    # -ButtonHoldMs (a held attack charges), and wheel notches (+ up / - down).
    [ValidateSet('', 'left', 'right')]
    [string]$Button = '',
    [int]$ButtonHoldMs = 80,
    [int]$Wheel = 0
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
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint type);
  public struct RECT { public int L, T, R, B; }
}
"@

if ($ProcessId -eq 0) {
    $p = Get-Process $ProcessName -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $p) { Write-Host 'NO GAME PROCESS'; exit 1 }
    $ProcessId = $p.Id
}

$script:tp = $ProcessId
$script:best = [IntPtr]::Zero
$script:bestArea = 0
$cb = [QdIn+EP]{
    param($hw, $l)
    $q = 0
    [QdIn]::GetWindowThreadProcessId($hw, [ref]$q) | Out-Null
    if ($q -eq $script:tp -and [QdIn]::IsWindowVisible($hw)) {
        $sb = New-Object System.Text.StringBuilder 512
        [QdIn]::GetWindowText($hw, $sb, 512) | Out-Null
        $r = New-Object QdIn+RECT
        [QdIn]::GetClientRect($hw, [ref]$r) | Out-Null
        $a = ($r.R - $r.L) * ($r.B - $r.T)
        if ($a -gt $script:bestArea -and ($sb.ToString() -match 'Queen' -or $a -gt 200000)) {
            $script:bestArea = $a
            $script:best = $hw
        }
    }
    return $true
}
[QdIn]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null

$h = $script:best
if ($h -eq [IntPtr]::Zero) { Write-Host 'NO GAME WINDOW'; exit 2 }
$cr = New-Object QdIn+RECT
[QdIn]::GetClientRect($h, [ref]$cr) | Out-Null
Write-Host ("window 0x{0:X} client {1}x{2}" -f $h.ToInt64(), ($cr.R - $cr.L), ($cr.B - $cr.T))

$WM_KEYDOWN = 0x0100; $WM_KEYUP = 0x0101
$WM_MOUSEMOVE = 0x0200; $WM_LBUTTONDOWN = 0x0201; $WM_LBUTTONUP = 0x0202

if ($Vk -ne 0) {
    $scan = [QdIn]::MapVirtualKey([uint32]$Vk, 0)
    # lParam layout: repeat(1) | scancode(8)<<16 | extended<<24 | context<<29 | previous<<30 | transition<<31
    $down = [IntPtr]([int64](1 -bor ($scan -shl 16)))
    $up = [IntPtr]([int64](1 -bor ($scan -shl 16) -bor (1 -shl 30) -bor (1 -shl 31)))
    [QdIn]::PostMessage($h, $WM_KEYDOWN, [IntPtr]$Vk, $down) | Out-Null
    if (-not $StickyKeys) { Start-Sleep -Milliseconds $HoldMs; [QdIn]::PostMessage($h, $WM_KEYUP, [IntPtr]$Vk, $up) | Out-Null }
    Write-Host ("posted VK=0x{0:X} scan={1}" -f $Vk, $scan)
}

if ($Button -ne '') {
    $cx = [int](($cr.R - $cr.L) / 2); $cy = [int](($cr.B - $cr.T) / 2)
    $lp = [IntPtr]([int64](($cy -shl 16) -bor ($cx -band 0xFFFF)))
    $dn = if ($Button -eq 'left') { 0x0201 } else { 0x0204 }
    $mk = if ($Button -eq 'left') { 1 } else { 2 }
    # The game only takes a mouse PRESS as gameplay input while it believes its window
    # is active (measured: after OnWindowUnfocused the press went to a UI overlay and
    # only the release reached the pawn).  WM_ACTIVATE tells it so without taking the
    # real focus away from anything.
    [QdIn]::PostMessage($h, 0x0006, [IntPtr]1, [IntPtr]0) | Out-Null
    Start-Sleep -Milliseconds 50
    [QdIn]::PostMessage($h, $WM_MOUSEMOVE, [IntPtr]0, $lp) | Out-Null
    [QdIn]::PostMessage($h, $dn, [IntPtr]$mk, $lp) | Out-Null
    Start-Sleep -Milliseconds $ButtonHoldMs
    [QdIn]::PostMessage($h, $dn + 1, [IntPtr]0, $lp) | Out-Null
    Write-Host "posted $Button button held $ButtonHoldMs ms"
}

if ($Wheel -ne 0) {
    $n = [math]::Abs($Wheel); $d = if ($Wheel -gt 0) { 120 } else { -120 }
    for ($i = 0; $i -lt $n; $i++) {
        $wp = [IntPtr]([int64](($d -band 0xFFFF) -shl 16))
        [QdIn]::PostMessage($h, 0x020A, $wp, [IntPtr]0) | Out-Null
        Start-Sleep -Milliseconds 150
    }
    Write-Host "posted wheel $Wheel"
}

if ($ClickFracX -gt 0.0 -and $ClickFracY -gt 0.0) {
    $ClickX = [int](($cr.R - $cr.L) * $ClickFracX)
    $ClickY = [int](($cr.B - $cr.T) * $ClickFracY)
    Write-Host "resolved fractional click to ($ClickX,$ClickY)"
}

if ($ClickX -ge 0 -and $ClickY -ge 0) {
    $lp = [IntPtr]([int64](($ClickY -shl 16) -bor ($ClickX -band 0xFFFF)))
    # A move first: Slate needs the hit-test to have run at that point before the press
    # is meaningful. Without it the press can land on whatever was hovered before.
    [QdIn]::PostMessage($h, $WM_MOUSEMOVE, [IntPtr]0, $lp) | Out-Null
    Start-Sleep -Milliseconds 120
    [QdIn]::PostMessage($h, $WM_LBUTTONDOWN, [IntPtr]1, $lp) | Out-Null
    Start-Sleep -Milliseconds 80
    [QdIn]::PostMessage($h, $WM_LBUTTONUP, [IntPtr]0, $lp) | Out-Null
    Write-Host "posted click at client ($ClickX,$ClickY)"
}
