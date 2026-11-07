#!/usr/bin/env python3
"""Engine identity strings in the shipping image."""
import re
import collections

PATH = r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64\QueensDomain-Win64-Shipping.exe"
raw = open(PATH, "rb").read()


def find(needle, wide=True, limit=40):
    a = [m.start() for m in re.finditer(re.escape(needle.encode("latin1")), raw)]
    w = [m.start() for m in re.finditer(re.escape(needle.encode("utf-16-le")), raw)] if wide else []
    return a, w


def show(label, needle, wide=True, limit=12):
    a, w = find(needle, wide)
    print("%-52s ansi=%-4d wide=%-4d %s" % (label, len(a), len(w),
          ("@ " + ", ".join("0x%X" % x for x in a[:3])) if a else ""))


print("=" * 78)
print("ENGINE VERSION IDENTITY")
print("=" * 78)
for s in ["++UE4+Release-4.27-CL-0", "++UE4+Release-4.27", "++UE4+",
          "+UE4+Release-4.27", "Release-4.27-CL-0", "4.27-CL-", "++UE5+",
          "++UE4+Main", "++UE4+Dev-", "++UE4+Release-4.26", "++UE4+Release-5.0"]:
    show(s, s)

print()
print("-- all occurrences of 'Release-4.' with context")
for m in re.finditer(rb"\+\+UE4\+[A-Za-z0-9._\-]{1,64}", raw):
    pass
ctx = set()
for m in re.finditer(rb"\+\+UE4\+[ -~]{0,60}", raw):
    ctx.add(m.group().decode("latin1"))
for c in sorted(ctx):
    print("   ", repr(c))

print()
print("-- 'CL-' numeric build ids near engine strings")
for m in re.finditer(rb"CL[- ]?(\d{4,8})", raw):
    pass

print()
print("=" * 78)
print("ENGINE MODULE NAMES (UE4 vs UE5 tells)")
print("=" * 78)
MODS = [
    # UE5-only / UE5-renamed tells
    "MotionWarping", "AnimNext", "Chooser", "PoseSearch", "Iris", "ReplicationSystem",
    "MassEntity", "Mass", "GeometryScripting", "ChaosVehicles", "Substrate",
    "Lumen", "Nanite", "VirtualShadowMaps", "WorldPartition",
    "GameplayCameras", "ModularGameplay", "CommonUI", "EnhancedInput",
    "Metasound", "AudioModulation", "AudioMixer", "SignalProcessing",
    "EngineSettings", "PhysicsCore", "FieldSystemEngine", "GeometryCollectionEngine",
    "ChaosSolverEngine", "ChaosCloth", "Chaos",
    # UE4 era
    "Engine", "Core", "CoreUObject", "UnrealEd", "AndroidRuntimeSettings",
    "ApexDestruction", "PhysXCore", "APEX", "OculusHMD", "SteamVR",
    "OnlineSubsystemSteam", "OnlineSubsystem", "RealtimeMeshComponent",
    "CableComponent", "SRT", "UdpMessaging", "TcpMessaging",
]
for mname in MODS:
    show(mname, mname, wide=False)

print()
print("=" * 78)
print("BUILD / ENGINE STRINGS WITH CONTEXT")
print("=" * 78)
PATS = [rb"[-A-Za-z_./\\]+/Engine/[-A-Za-z0-9_./\\]+",
        rb"[-A-Za-z_./\\]*(?:Engine|Project|Game)/Config/[-A-Za-z0-9_.]+",
        rb"\.uproject", rb"\.uplugin", rb"UE4Editor", rb"UnrealEditor",
        rb"UnrealEngine[/\\][A-Za-z0-9._\-]+",
        rb"[-A-Za-z0-9_]+\.Build\.cs", rb"[-A-Za-z0-9_]+\.Target\.cs"]
for p in PATS:
    hits = set()
    for m in re.finditer(p, raw):
        try:
            hits.add(m.group().decode("latin1"))
        except Exception:
            pass
    print("\nPATTERN %s  (%d unique)" % (p.decode("latin1"), len(hits)))
    for h in sorted(hits)[:40]:
        print("   ", h)


def strings_with(pat, minlen=6, maxn=60):
    out = []
    for m in re.finditer(rb"[ -~]{%d,}" % minlen, raw):
        s = m.group().decode("latin1")
        if re.search(pat, s):
            out.append((m.start(), s))
    return out


print()
print("=" * 78)
print("VERSION-LIKE STRINGS")
print("=" * 78)
seen = set()
for off, s in strings_with(r"^(?:4\.|5\.)\d{1,2}\.\d{1,3}", minlen=5):
    if s not in seen:
        seen.add(s)
print("count:", len(seen))
for s in sorted(seen)[:80]:
    print("   ", repr(s))

print()
print("=" * 78)
print("'Branch' / build metadata")
print("=" * 78)
for pat in [rb"BuildSettings", rb"BuildVersion", rb"BranchName",
            rb"CompatibleChangelist", rb"BuildId", rb"IsLicenseeVersion",
            rb"EngineVersion", rb"NetCL", rb"Changelist"]:
    a, w = find(pat)
    print("%-24s ansi=%-4d wide=%-4d" % (pat.decode(), len(a), len(w)))
