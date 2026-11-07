<#
  mute.ps1 -- mute the rig's test instances in the Windows mixer (per process).

  Only processes whose exe lives under testgame\instance* are touched, so a normal copy of
  the game the owner is playing is never muted.  The game's own audio engine keeps running
  (unlike -nosound), so nothing in the game that waits on a sound behaves differently.

    mute.ps1 -Once          mute every test-instance audio session there is now, print them
    mute.ps1 -Check         print each test-instance session and whether it is muted (exit 1 if one is not)
    mute.ps1 -Watch         keep muting new sessions every 0.5 s until no test instance is running
#>
param([switch]$Once, [switch]$Check, [switch]$Watch, [int]$Seconds = 0,
      # doc 62: the exe path pattern of the games to mute (default: the rig's instances)
      [string]$Match = '\\testgame\\instance\d+\\')

$src = @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace DmpMute {
  [Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
  interface IMMDeviceEnumerator {
    int EnumAudioEndpoints(int dataFlow, int stateMask, out IMMDeviceCollection devices);
    int GetDefaultAudioEndpoint(int dataFlow, int role, out IMMDevice dev);
  }
  [Guid("0BD7A1BE-7A1A-44DB-8397-CC5392387B5E"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
  interface IMMDeviceCollection {
    int GetCount(out int n);
    int Item(int i, out IMMDevice dev);
  }
  [Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
  interface IMMDevice {
    int Activate(ref Guid iid, int ctx, IntPtr p, [MarshalAs(UnmanagedType.IUnknown)] out object o);
  }
  [Guid("77AA99A0-1BD6-484F-8BC7-2C654C9A9B6F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
  interface IAudioSessionManager2 {
    int GetAudioSessionControl(IntPtr a, int b, out IntPtr c);
    int GetSimpleAudioVolume(IntPtr a, int b, out IntPtr c);
    int GetSessionEnumerator(out IAudioSessionEnumerator e);
  }
  [Guid("E2F5BB11-0570-40CA-ACDD-3AA01277DEE8"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
  interface IAudioSessionEnumerator {
    int GetCount(out int n);
    int GetSession(int i, out IAudioSessionControl2 s);
  }
  [Guid("bfb7ff88-7239-4fc9-8fa2-07c950be9c6d"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
  interface IAudioSessionControl2 {
    int GetState(out int s);
    int GetDisplayName(out IntPtr p);
    int SetDisplayName(IntPtr a, IntPtr b);
    int GetIconPath(out IntPtr p);
    int SetIconPath(IntPtr a, IntPtr b);
    int GetGroupingParam(out Guid g);
    int SetGroupingParam(ref Guid g, IntPtr c);
    int RegisterAudioSessionNotification(IntPtr n);
    int UnregisterAudioSessionNotification(IntPtr n);
    int GetSessionIdentifier(out IntPtr p);
    int GetSessionInstanceIdentifier(out IntPtr p);
    int GetProcessId(out uint pid);
  }
  [Guid("87CE5498-68D6-44E5-9215-6DA47EF883D8"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
  interface ISimpleAudioVolume {
    int SetMasterVolume(float v, ref Guid ctx);
    int GetMasterVolume(out float v);
    int SetMute(bool m, ref Guid ctx);
    int GetMute(out bool m);
  }
  [ComImport, Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")] class MMDeviceEnumerator { }

  public class Session { public uint Pid; public bool Muted; }

  public static class Mixer {
    // mute (when doMute) every session of a process in pids, on every active output device
    public static List<Session> Apply(uint[] pids, bool doMute) {
      var res = new List<Session>();
      var set = new HashSet<uint>(pids);
      var en = (IMMDeviceEnumerator)(new MMDeviceEnumerator());
      IMMDeviceCollection col; en.EnumAudioEndpoints(0 /*render*/, 1 /*active*/, out col);
      int nd; col.GetCount(out nd);
      Guid iidMgr = typeof(IAudioSessionManager2).GUID;
      Guid ctx = Guid.Empty;
      for (int d = 0; d < nd; d++) {
        IMMDevice dev; col.Item(d, out dev);
        object o; if (dev.Activate(ref iidMgr, 1 /*CLSCTX_INPROC*/, IntPtr.Zero, out o) != 0) continue;
        var mgr = (IAudioSessionManager2)o;
        IAudioSessionEnumerator se; if (mgr.GetSessionEnumerator(out se) != 0) continue;
        int ns; se.GetCount(out ns);
        for (int i = 0; i < ns; i++) {
          IAudioSessionControl2 s; se.GetSession(i, out s);
          uint pid; s.GetProcessId(out pid);
          if (!set.Contains(pid)) continue;
          var vol = (ISimpleAudioVolume)s;
          if (doMute) vol.SetMute(true, ref ctx);
          bool m; vol.GetMute(out m);
          res.Add(new Session { Pid = pid, Muted = m });
        }
      }
      return res;
    }
  }
}
"@
if (-not ('DmpMute.Mixer' -as [type])) { Add-Type -TypeDefinition $src -Language CSharp }

function Get-TestPids {
    # Get-Process rather than CIM: WMI can be refused under a restricted token, which made the
    # watcher see no game at all and leave both rig instances audible.
    @(Get-Process -Name 'QueensDomain-Win64-Shipping' -ErrorAction SilentlyContinue |
      ForEach-Object { $q = $_; $p = $null; try { $p = $q.Path } catch { }
                      if ($p -and $p -match $Match) { [uint32]$q.Id } })
}

function Show($sessions, $pids) {
    foreach ($p in $pids) {
        $mine = @($sessions | Where-Object { $_.Pid -eq $p })
        if ($mine.Count -eq 0) { Write-Output ("pid {0}: no audio session yet" -f $p) }
        else { Write-Output ("pid {0}: {1} session(s), muted={2}" -f $p, $mine.Count, (($mine | ForEach-Object { $_.Muted }) -join ',')) }
    }
}

if ($Watch) {
    $end = if ($Seconds -gt 0) { (Get-Date).AddSeconds($Seconds) } else { [datetime]::MaxValue }
    $seen = 0
    $giveUp = (Get-Date).AddSeconds(300)    # no test instance ever started: stop waiting
    while ((Get-Date) -lt $end) {
        $pids = Get-TestPids
        if ($pids.Count -eq 0) { if ($seen -or (Get-Date) -gt $giveUp) { break } }
        else { $seen = 1; [void][DmpMute.Mixer]::Apply($pids, $true) }
        Start-Sleep -Milliseconds 500
    }
    exit 0
}

$pids = Get-TestPids
if ($pids.Count -eq 0) { Write-Output 'no test instance is running'; exit 0 }
$s = [DmpMute.Mixer]::Apply($pids, [bool]$Once)
Show $s $pids
if ($Check) {
    $bad = @($s | Where-Object { -not $_.Muted }).Count
    $none = @($pids | Where-Object { $p = $_; -not ($s | Where-Object { $_.Pid -eq $p }) }).Count
    if ($bad -gt 0 -or $none -gt 0) { exit 1 }
}
exit 0

