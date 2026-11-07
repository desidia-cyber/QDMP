#!/usr/bin/env python3
"""Category roll-up for the report: counts, package bytes, largest assets."""
import json
import os
from collections import Counter, defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DATA = os.path.join(ROOT, "data")

doc = json.load(open(os.path.join(ROOT, "pak-files.json"), encoding="utf-8"))
files = doc["files"]
classes = json.load(open(os.path.join(DATA, "asset-classes.json"), encoding="utf-8"))

GROUPS = {
    "Maps / levels": {"World", "Level"},
    "Blueprints (BPGC)": {"BlueprintGeneratedClass", "AnimBlueprintGeneratedClass",
                          "WidgetBlueprintGeneratedClass"},
    "SkeletalMesh": {"SkeletalMesh"},
    "StaticMesh": {"StaticMesh"},
    "Skeletons": {"Skeleton"},
    "PhysicsAsset": {"PhysicsAsset"},
    "Animations": {"AnimSequence", "AnimMontage", "BlendSpace", "BlendSpace1D",
                   "AimOffsetBlendSpace", "AimOffsetBlendSpace1D"},
    "Materials": {"Material", "MaterialInstanceConstant", "MaterialFunction",
                  "MaterialParameterCollection"},
    "Textures": {"Texture2D", "TextureCube", "VolumeTexture", "TextureRenderTarget2D",
                 "MediaTexture", "LightmapTexture2D", "ShadowMapTexture2D",
                 "Texture2DArray", "CurveLinearColorAtlas"},
    "Audio": {"SoundWave", "SoundCue", "SoundAttenuation", "SoundClass",
              "SoundConcurrency", "SoundMix", "SoundSubmix", "ReverbEffect",
              "AudioImpulseResponse", "SoundNode"},
    "ForceFeedback": {"ForceFeedbackEffect", "ForceFeedbackAttenuation"},
    "Data assets / tables": {"DataTable", "CurveTable", "CurveFloat", "CurveVector",
                             "CurveLinearColor", "UserDefinedStruct",
                             "UserDefinedEnum", "StringTable"},
    "Fonts": {"Font", "FontFace"},
    "Niagara VFX": {"NiagaraSystem", "NiagaraEmitter", "NiagaraScript",
                    "NiagaraEffectType"},
    "Sequences": {"LevelSequence"},
}

# package bytes: .uasset + .uexp + .ubulk share a stem
stem_bytes = defaultdict(int)
for p, v in files.items():
    stem = p.rsplit(".", 1)[0]
    stem_bytes[stem] += v["size"] or 0


def pkg_bytes(p):
    return stem_bytes[p.rsplit(".", 1)[0]]


def main():
    print("PAK:", doc["pak"], doc["pak_size"], "bytes,", doc["num_entries"], "entries")
    print()
    assigned = set()
    rows = []
    for label, kinds in GROUPS.items():
        ps = [p for p, c in classes.items() if c["class"] in kinds]
        assigned.update(ps)
        b = sum(pkg_bytes(p) for p in ps)
        rows.append((label, len(ps), b))
    for label, n, b in sorted(rows, key=lambda r: -r[1]):
        print("  %-26s %5d pkgs  %12s bytes  (%5.1f%% of pak)"
              % (label, n, "{:,}".format(b), 100.0 * b / doc["pak_size"]))
    print()

    # game-specific data classes
    other = Counter()
    for p, c in classes.items():
        if p in assigned:
            continue
        k = c["class"] or "<null>"
        other[k] += 1
    print("  ungrouped classes (top 20):")
    for k, v in other.most_common(20):
        print("     %-40s %4d" % (k, v))
    print()

    print("  largest 15 packages:")
    big = sorted(((pkg_bytes(p), p) for p in classes), reverse=True)
    for b, p in big[:15]:
        print("     %12s  %-32s %s" % ("{:,}".format(b), classes[p]["class"], p))
    print()

    print("  per-root totals:")
    roots = Counter()
    for p, v in files.items():
        parts = p.split("/")
        roots["/".join(parts[:3]) if p.startswith("/") else parts[0]] += v["size"] or 0
    for k, v in roots.most_common(18):
        print("     %-42s %12s" % (k, "{:,}".format(v)))


if __name__ == "__main__":
    main()
