#!/usr/bin/env python3
"""Self-test: every real save must parse to an exact byte fit."""
import os
import gvas

FILES = ["System0.sav", "Challenges", "Challenges_Backup",
         "World0.sav", "World0_backup.sav"]

ok = True
for f in FILES:
    h = gvas.load(os.path.join(gvas.SAVES, f))
    cls = h["class_path"].split(".")[-1]
    exact = h["trailing"] == 4
    ok = ok and exact
    print("%-22s %-24s stream_end=%-6d size=%-6d trailing=%d props=%-3d %s"
          % (f, cls, h["stream_end"], h["file_size"], h["trailing"],
             len(h["props"]), "OK" if exact else "MISFIT"))
print("\nall exact:", ok)
