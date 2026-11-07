#!/usr/bin/env python3
"""Settle the FObjectExport record size and the FPackageIndex direction by data.

Two unknowns are tested against ground truth rather than memory:

1. Record size. A cooked .uasset is only the header, and every export's
   SerialOffset points at the start of that export's data in the .uexp, i.e. the
   first export's SerialOffset should equal the .uasset file size. So the correct
   record stride is the one that makes that true for multi-export packages.

2. FPackageIndex direction. Candidate A (UE text convention): >0 -> import[ci-1],
   <0 -> export[-ci-1]. Candidate B: >0 -> export[ci-1], <0 -> import[-ci-1].
   The winner is the one that resolves to real class names.
"""
import os
import struct
import sys
from collections import Counter

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import queenspak  # noqa: E402
import classes    # noqa: E402

PAK = (r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Content\Paks"
       r"\QueensDomain-WindowsNoEditor.pak")

KNOWN = {
    "Texture2D", "StaticMesh", "SkeletalMesh", "Skeleton", "AnimSequence",
    "AnimMontage", "AnimBlueprint", "BlendSpace", "BlendSpace1D", "Material",
    "MaterialInstanceConstant", "MaterialFunction", "MaterialParameterCollection",
    "SoundWave", "SoundCue", "SoundAttenuation", "SoundClass", "SoundConcurrency",
    "DataTable", "CurveTable", "CurveFloat", "CurveVector", "CurveLinearColor",
    "WidgetBlueprint", "Blueprint", "World", "Font", "FontFace", "TextureRenderTarget2D",
    "NiagaraSystem", "NiagaraEmitter", "ParticleSystem", "PhysicsAsset",
    "PhysicalMaterial", "StringTable", "MediaPlayer", "FileMediaSource",
    "LevelSequence", "Sequencer", "SubsurfaceProfile", "CanvasRenderTarget2D",
    "RuntimeVirtualTexture", "VolumeTexture", "TextureCube", "LightmapTexture2D",
    "ShadowMapTexture2D", "Texture2DArray", "Enum", "UserDefinedStruct",
    "BlueprintGeneratedClass", "WidgetBlueprintGeneratedClass",
}


def parse(data, s, names, stride, direction):
    """Return list of exports for one package."""
    p = s["export_offset"]
    out = []
    for _ in range(s["export_count"]):
        if p + 44 > len(data):
            break
        ci, si, ti, oi = struct.unpack_from("<4i", data, p)
        on = struct.unpack_from("<i", data, p + 16)[0]
        oflags = struct.unpack_from("<I", data, p + 24)[0]
        ssize, soff = struct.unpack_from("<2q", data, p + 28)
        out.append(dict(ci=ci, si=si, ti=ti, oi=oi,
                        name=names[on] if 0 <= on < len(names) else "?",
                        size=ssize, off=soff))
        p += stride
    return out


def resolve(idx, imports, exports, names, direction):
    if idx == 0:
        return None
    if direction == "A":
        imp, exp = idx > 0, idx < 0
    else:
        imp, exp = idx < 0, idx > 0
    if imp:
        j = abs(idx) - 1
        if j < len(imports):
            return names[imports[j][4]] if False else imports[j]["object_name"]
        return "?"
    j = abs(idx) - 1
    if j < len(exports):
        return exports[j]["name"]
    return "?"


def read_imports(data, s, names):
    p = s["import_offset"]
    out = []
    for _ in range(s["import_count"]):
        if p + 28 > len(data):
            break
        cp, _cpn, cn, _cnn, outer, on, _onn = struct.unpack_from("<7i", data, p)
        out.append(dict(class_package=names[cp] if 0 <= cp < len(names) else "?",
                        class_name=names[cn] if 0 <= cn < len(names) else "?",
                        outer=outer,
                        object_name=names[on] if 0 <= on < len(names) else "?"))
        p += 28
    return out


def main():
    with queenspak.Pak(PAK) as pk:
        items = [(queenspak.game_path(pk.mount, r), r, e)
                 for r, e in pk.entries.items()
                 if queenspak.game_path(pk.mount, r).endswith(".uasset")]

        # ---- test 1: record stride, using multi-export packages -------------
        print("=== record stride: does export[0].SerialOffset == .uasset size? ===")
        multi = [it for it in items if it[2].get("uncompressed_size")]
        sample = [it for it in items][:4000]
        for stride in (88, 92, 96, 100, 104, 108, 112):
            good = tot = 0
            for gp, raw, e in sample:
                try:
                    data = pk.read_file(raw)
                    r = classes.R(data)
                    s = classes.read_summary(r)
                    if s["export_count"] < 2:
                        continue
                    names = classes.read_names(r, s["name_count"], s["name_offset"])
                    ex = parse(data, s, names, stride, "A")
                    if len(ex) < 2:
                        continue
                    tot += 1
                    # every export's offset should be >= uasset size and monotonic
                    if all(x["off"] >= len(data) - 4 for x in ex) and \
                       all(ex[i]["off"] <= ex[i + 1]["off"] for i in range(len(ex) - 1)):
                        good += 1
                except Exception:                             # noqa: BLE001
                    continue
            print("  stride %3d : %5d / %5d multi-export packages consistent"
                  % (stride, good, tot))

        # ---- test 2: index direction, using class names ---------------------
        print("\n=== FPackageIndex direction: which yields real class names? ===")
        for stride in (104, 108):
            for direction in ("A", "B"):
                hist = Counter()
                for gp, raw, e in sample:
                    try:
                        data = pk.read_file(raw)
                        r = classes.R(data)
                        s = classes.read_summary(r)
                        names = classes.read_names(r, s["name_count"], s["name_offset"])
                        imp = read_imports(data, s, names)
                        ex = parse(data, s, names, stride, direction)
                        if not ex:
                            hist[None] += 1
                            continue
                        hist[resolve(ex[0]["ci"], imp, ex, names, direction)] += 1
                    except Exception:                         # noqa: BLE001
                        hist["<err>"] += 1
                known = sum(v for k, v in hist.items() if k in KNOWN)
                total = sum(hist.values())
                print("\n  stride=%d direction=%s : %.1f%% known class names"
                      % (stride, direction, 100.0 * known / max(1, total)))
                for k, v in hist.most_common(12):
                    print("      %-40r %5d" % (k, v))


if __name__ == "__main__":
    main()
