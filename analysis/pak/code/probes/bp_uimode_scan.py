"""Which packages reference the UI-mode / auto-pause entry points.

This answers the per-state question: a widget that calls EnterUIMode/TryAutoPause
inherits the pause; one that only calls SetInputMode_*UIOnly does not.
"""
import sys, time
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\pak\code")
import pakread, blueprints

NEEDLES = ["TryAutoPause", "EnterUIMode", "ExitUIMode", "CacheInputStatePreUIMode",
           "UpdateInputComponentsPostUIMode", "SetInputMode_UIOnlyEx",
           "SetInputMode_GameOnly", "SetGamePaused", "SetGlobalTimeDilation",
           "IsInModalMenu", "OpenPauseMenu", "IsGamePaused"]

pk = pakread.Pak()
paths = [p for p in pk.entries if p.lower().endswith((".uasset", ".umap"))]
hits = {}
t0 = time.time()
for i, p in enumerate(paths):
    d = pk.payload(p)
    found = [n for n in NEEDLES if n.encode() in d]
    if found:
        hits[p] = found
print("scanned %d packages in %.1fs; %d hits\n" % (len(paths), time.time() - t0, len(hits)))

# group by needle -> packages, but show per-package lines for UI trees
for p in sorted(hits):
    if any(k in p for k in ("/UI/", "/Menus/", "/PhotoMode/", "/Player/", "/Blueprints/")):
        print("%-78s %s" % (p.split("Content/", 1)[-1], hits[p]))

print("\n--- needle -> package count ---")
for n in NEEDLES:
    c = [p for p, f in hits.items() if n in f]
    print("  %-34s %d" % (n, len(c)))
    for p in sorted(c)[:14]:
        print("        " + p.split("Content/", 1)[-1])
pk.close()
