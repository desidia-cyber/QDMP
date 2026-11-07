#!/usr/bin/env python3
"""Extract selected pak entries and dump their UE4 name tables / ini text."""
import json
import re
import struct
import sys

PAK = (r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Content\Paks"
       r"\QueensDomain-WindowsNoEditor.pak")
IDX = json.load(open(r"G:\Freespace\Queens_Domain\analysis\scratch\pak_paths.json",
                     encoding="utf-8"))["entries"]

F = open(PAK, "rb")


def read_entry(path):
    e = IDX[path]
    # uncompressed entry: FPakEntry header is 53 bytes prepended to data
    off = e["off"] + 53
    F.seek(off)
    return F.read(e["usize"])


def strings(blob, minlen=3, limit=400):
    out = []
    for m in re.finditer(rb"[\x20-\x7e]{%d,}" % minlen, blob):
        out.append(m.group().decode("latin1"))
        if len(out) >= limit:
            break
    return out


print("=" * 78)
print("PAK .ini FILES")
print("=" * 78)
inis = sorted(p for p in IDX if p.endswith(".ini"))
for p in inis:
    print("  ", p, IDX[p]["usize"])

for want in ["QueensDomain/Config/DefaultEngine.ini",
             "QueensDomain/Config/DefaultGame.ini",
             "QueensDomain/Config/DefaultInput.ini",
             "QueensDomain/Config/DefaultScalability.ini"]:
    if want not in IDX:
        print("\n--- %s : NOT IN PAK" % want)
        continue
    blob = read_entry(want)
    print("\n" + "=" * 78)
    print(want, "(%d bytes)" % len(blob))
    print("=" * 78)
    txt = blob.decode("latin1", "replace")
    print(txt)

print()
print("=" * 78)
print("ASSET NAME TABLES (evidence for player model)")
print("=" * 78)
TARGETS = [
    "QueensDomain/Content/Player/FS_PlayerChar.uasset",
    "QueensDomain/Content/Player/FS_Full_PlayerChar.uasset",
    "QueensDomain/Content/Player/FS_PlayerController_BP.uasset",
    "QueensDomain/Content/Player/Assets/FPModel/Player_FPArms_Arms.uasset",
    "QueensDomain/Content/Player/Assets/FPModel/Player2026/Player_FullChar_2026.uasset",
    "QueensDomain/Content/Player/FaceFollowComp.uasset",
    "QueensDomain/Content/Blueprints/emptycharacter.uasset",
    "QueensDomain/Content/Blueprints/TitleScreen_Character.uasset",
]
for t in TARGETS:
    if t not in IDX:
        print("\n--- %s : NOT IN PAK" % t)
        continue
    blob = read_entry(t)
    print("\n### %s  (%d bytes)" % (t, len(blob)))
    print("    header:", blob[:32].hex(" "))
    # package tag + versions
    if len(blob) > 24:
        tag, = struct.unpack_from("<I", blob, 0)
        print("    tag 0x%08X  legacyFileVersion %d" % (
            tag, struct.unpack_from("<i", blob, 4)[0]))
    S = strings(blob, 4, 250)
    keys = ("SkeletalMesh", "SkeletalMeshComponent", "StaticMesh", "Skeleton",
            "PhysicsAsset", "AnimBlueprint", "AnimMontage", "Character",
            "Arms", "Body", "Head", "Mesh", "hidden", "bHidden", "FirstPerson",
            "FP", "Camera", "Player")
    print("    --- names of interest:")
    seen = set()
    for s in S:
        if any(k.lower() in s.lower() for k in keys) and s not in seen:
            seen.add(s)
            print("       ", s)
    print("    --- all names (%d shown):" % min(len(S), 60))
    print("       ", " | ".join(S[:60]))
