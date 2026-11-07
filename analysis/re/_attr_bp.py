"""Attribute pause-surface names to blueprint assets using the generated report.

Read-only over analysis/pak/blueprint-surface.md, which build_reports.py produced
from the shipped pak. For each target name, report which asset sections mention it
and in which category (UFUNCTION export / imported C++ / property name).
"""
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
MD = os.path.join(ROOT, "analysis", "pak", "blueprint-surface.md")

TARGETS = [
    "SetGamePaused", "SetPause", "IsGamePaused", "CanPause",
    "SetGlobalTimeDilation", "GetGlobalTimeDilation",
    "SetTimeDilation", "GetActorTimeDilation",
    "SetActorTickEnabled", "SetComponentTickEnabled", "SetTickableWhenPaused",
    "CustomTimeDilation", "Set Pause Time Dilation", "SetPauseTimeDilation",
    "IsTimeDilationPaused", "SetPausedEnemyTimeDilation",
    "ForceNoOverlayPause", "IsInGameplayModal", "PauserPlayerState",
]


def load_sections():
    lines = open(MD, encoding="utf-8", errors="replace").read().splitlines()
    secs = []
    cur = None
    for l in lines:
        if l.startswith("## "):
            cur = {"name": l[3:].strip(), "body": [], "cats": []}
            secs.append(cur)
        elif cur is not None:
            cur["body"].append(l)
            m = re.match(r"\*\*(.+?)\*\*", l.strip())
            if m:
                cur["cats"].append((len(cur["body"]) - 1, m.group(1)))
    return secs


def cat_for(sec, idx):
    best = "?"
    for pos, name in sec["cats"]:
        if pos <= idx:
            best = name
        else:
            break
    return best


def main():
    secs = load_sections()
    print("sections parsed:", len(secs))
    for t in TARGETS:
        print("\n=== %s" % t)
        n = 0
        for s in secs:
            for j, line in enumerate(s["body"]):
                if t not in line:
                    continue
                cat = cat_for(s, j)
                snippet = line.strip()
                if len(snippet) > 90:
                    # hit inside a long comma-joined properties line
                    snippet = "<in long list: %s>" % cat
                print("    %-38s [%s]  %s" % (s["name"], cat, snippet))
                n += 1
                break
        if not n:
            print("    (no section mentions this name)")


if __name__ == "__main__":
    main()
