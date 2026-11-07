#!/usr/bin/env python3
"""Deep dive: FS_PlayerChar vs FS_Full_PlayerChar, and exe multiplayer surface."""
import sys
sys.stdout.reconfigure(encoding="utf-8", errors="backslashreplace")

import json
import re
import struct

PAK = (r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Content\Paks"
       r"\QueensDomain-WindowsNoEditor.pak")
EXE = (r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Binaries\Win64"
       r"\QueensDomain-Win64-Shipping.exe")
IDX = json.load(open(r"G:\Freespace\Queens_Domain\analysis\scratch\pak_paths.json",
                     encoding="utf-8"))["entries"]
F = open(PAK, "rb")


def read_entry(p):
    e = IDX[p]
    F.seek(e["off"] + 53)
    return F.read(e["usize"])


def name_table(blob):
    """UE4.27 .uasset summary -> name table.  Layout measured on this build:
       0 tag | 4 legacy(-7) | 8 ue3 | 12 ue4 | 16 licensee | 20 customver count
       24 TotalHeaderSize | FolderName FString | PackageFlags | NameCount | NameOffset
    Name entries are FString (int32 len, chars incl NUL) + 2x uint16 hashes.
    """
    if struct.unpack_from("<I", blob, 0)[0] != 0x9E2A83C1:
        return []
    legacy, = struct.unpack_from("<i", blob, 4)
    ue3, = struct.unpack_from("<i", blob, 8)
    ue4, = struct.unpack_from("<i", blob, 12)
    lic, = struct.unpack_from("<i", blob, 16)
    o = 20
    ncv, = struct.unpack_from("<i", blob, o)
    o += 4 + ncv * 20
    if ue4 >= 516 and ue4 != 0:
        pass
    total_hdr, = struct.unpack_from("<i", blob, o)
    o += 4
    folder_len, = struct.unpack_from("<i", blob, o)
    o += 4
    o += abs(folder_len) * (2 if folder_len < 0 else 1)
    pkg_flags, = struct.unpack_from("<I", blob, o)
    o += 4
    name_count, = struct.unpack_from("<i", blob, o)
    name_off, = struct.unpack_from("<i", blob, o + 4)
    print("    legacy=%d ue3=%d ue4=%d lic=%d customver=%d" %
          (legacy, ue3, ue4, lic, ncv))
    print("    TotalHeaderSize=%d PackageFlags=0x%X NameCount=%d NameOffset=%d"
          % (total_hdr, pkg_flags, name_count, name_off))
    names = []
    p = name_off
    for _ in range(name_count):
        if p + 4 > len(blob):
            break
        ln, = struct.unpack_from("<i", blob, p)
        p += 4
        if ln == 0:
            names.append("")
            p += 4
            continue
        if ln > 0:
            if p + ln > len(blob):
                break
            s = blob[p:p + ln].split(b"\0")[0].decode("latin1", "replace")
            p += ln
        else:
            nb = (-ln) * 2
            if p + nb > len(blob):
                break
            s = blob[p:p + nb].decode("utf-16-le", "replace").rstrip("\0")
            p += nb
        p += 4  # two uint16 hashes
        names.append(s)
    return names


TARGETS = [
    "QueensDomain/Content/Player/FS_PlayerChar.uasset",
    "QueensDomain/Content/Player/FS_Full_PlayerChar.uasset",
]
for t in TARGETS:
    blob = read_entry(t)
    print("=" * 78)
    print(t, "(%d bytes)" % len(blob))
    print("=" * 78)
    names = name_table(blob)
    print("    names parsed: %d" % len(names))
    INTEREST = re.compile(
        r"fullchar|fparms|arms|body|head|hair|skeletalmesh|skeleton|mesh|hidden|"
        r"owner|nofollow|firstperson|camera|capsule|character|pawn|replicat|"
        r"net|onlyowner|shadow|depth", re.I)
    for n in names:
        if INTEREST.search(n):
            print("      *", n)
    print()
    print("    --- FULL NAME TABLE ---")
    for i, n in enumerate(names):
        print("      %3d %s" % (i, n))
    print()

# ------------------------------------------------------------- exe net surface
print("#" * 78)
print("EXE: MULTIPLAYER / ANTICHEAT NAME SURFACE")
print("#" * 78)
data = open(EXE, "rb").read()
GROUPS = {
    "NetDriver / netcode": ["IpNetDriver", "SteamNetDriver", "NetDriver",
                            "DemoNetDriver", "PendingNetGame", "NetConnection",
                            "SteamNetConnection", "PackageMapClient",
                            "ServerReplicateActors", "TickFlush", "InitListen",
                            "CreateNetDriver", "MaxClientRate"],
    "replication core": ["ReplicateSubobjects", "UActorChannel", "FObjectReplicator",
                         "GetLifetimeReplicatedProps", "PreReplication",
                         "OnRep_ReplicatedMovement", "ReplicatedMovement",
                         "bReplicates", "NetUpdateFrequency", "NetPriority",
                         "NetCullDistanceSquared", "SetNetDormancy",
                         "IsNetRelevantFor", "ForceNetUpdate", "Role_Authority",
                         "SimulatedProxy", "AutonomousProxy", "NetMulticast",
                         "ServerRPC", "ClientRPC", "FRepMovement", "FClassNetCache"],
    "RPC handlers": ["Server_", "Client_", "Multicast_", "_Implementation",
                     "ServerMove", "ClientAdjustPosition", "ServerUpdateCamera",
                     "ReplicateMoveToServer", "FSavedMove_Character"],
    "online subsystem": ["OnlineSubsystem", "OnlineSubsystemSteam",
                         "OnlineSubsystemUtils", "SessionInterface",
                         "CreateSession", "FindSessions", "JoinSession",
                         "IOnlineSubsystem", "SteamSockets"],
    "splitscreen / local mp": ["Splitscreen", "LocalPlayer", "LocalPlayers",
                               "CreatePlayer", "GameViewportClient",
                               "ULocalPlayer", "MaxSplitscreenPlayers"],
    "ANTI-CHEAT / DRM": ["EasyAntiCheat", "EasyAntiCheat_", "EAC", "BattlEye",
                         "BEClient", "BEService", "Denuvo", "SteamStub",
                         "CPADinfo", "Vanguard", "PunkBuster", "Arxan",
                         "VMProtect", "Themida", "obsidium"],
    "Steam API": ["steam_api64", "SteamAPI_", "Steamworks", "SteamUser",
                  "SteamFriends", "SteamMatchmaking", "SteamNetworking"],
    "CONTROL (expect present)": ["ProcessEvent", "StaticConstructObject",
                                 "SkeletalMeshComponent", "CharacterMovement",
                                 "PlayerController", "GameModeBase", "PlayerState"],
    "CONTROL (expect absent)": ["UnrealEd", "EditorEngine", "FunctionalTest",
                                "AUTPlayerController"],
}
for g, needles in GROUPS.items():
    print("\n" + "=" * 78)
    print(g)
    print("=" * 78)
    for n in needles:
        a = len(re.findall(re.escape(n.encode("latin1")), data))
        w = len(re.findall(re.escape(n.encode("utf-16-le")), data))
        print("  %-4s %-34s ansi=%-5d wide=%-5d"
              % ("yes" if (a or w) else "NO ", n, a, w))
