"""Query the blueprint function index: declarations, callers, and per-package exports.

Answers the questions the state table needs:
  * who declares the custom pause/dilation helpers,
  * who calls each of them,
  * what a given UI package declares (its entry points).
"""
import os
import sys
import json
import re

HERE = os.path.dirname(os.path.abspath(__file__))
IDX = os.path.join(HERE, "_bp_fn_index.json")


def main():
    d = json.load(open(IDX))
    decl, bind, ex = d["decl"], d["bind"], d["exports_by_pkg"]

    print("### every function this build binds that matches the pause/modal vocabulary")
    for name in sorted(bind):
        print("  %-42s %d" % (name, len(bind[name])))
        for p in bind[name]:
            print("        ", p.split("/Content/")[-1])
    print()

    print("### custom helpers: who declares, who calls")
    for name in ["SetPauseTimeDilation", "Set Pause Time Dilation", "SetPausedEnemyTimeDilation",
                 "IsTimeDilationPaused", "Set BaseTimeDilation", "IsPausedTimeDilationEnabled",
                 "IsEnemyPauseTDLEnabled", "Bind To Enemy Pause TDL", "OpenPauseMenu",
                 "TryAutoPause", "CheckForOverlayPause", "Paused", "Pause",
                 "SetEnemiesPaused", "DoItemPickupPause", "StartPickupPause", "EndPickupPause",
                 "EndItemPickupPause"]:
        dcl = decl.get(name, [])
        cbs = bind.get(name, [])
        print("  %-30s decl=%s" % (name, [p.split("/Content/")[-1] for p in dcl] or "-"))
        print("  %-30s call=%s" % ("", [p.split("/Content/")[-1] for p in cbs] or "-"))
    print()

    print("### exports of the timing / menu packages")
    for p in sorted(ex):
        if re.search(r"(TimeDilation|PauseMenu|FS_PlayerChar|Photomode_Pawn|WeaponUpgradeMachine|"
                     r"FS_GameInstance|Base_LD45_GM|FS_CommonGM|CutsceneManager)", p):
            fns = ex[p]
            print("\n-- %s  (%d exports)" % (p.split("/Content/")[-1], len(fns)))
            for i in range(0, len(fns), 6):
                print("     " + ", ".join(fns[i:i + 6]))


if __name__ == "__main__":
    main()
