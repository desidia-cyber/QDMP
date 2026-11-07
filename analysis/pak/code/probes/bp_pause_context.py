"""For each Blueprint that imports a pause/dilation node, dump the export (function)
names and the name-table entries that reveal the calling context."""
import sys, re
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\pak\code")
import pakread, blueprints

TARGETS = [
 "QueensDomain/Content/Player/FS_PlayerChar.uasset",
 "QueensDomain/Content/UI/PhotoMode/Photomode_Pawn.uasset",
 "QueensDomain/Content/Blueprints/UpgradeMachine/WeaponUpgradeMachine.uasset",
 "QueensDomain/Content/LD45/Blueprints/Base_LD45_GM.uasset",
 "QueensDomain/Content/Blueprints/Utils/ShaderCompileManager.uasset",
 "QueensDomain/Content/LD45/Blueprints/FS_CommonGM.uasset",
]
CTX = re.compile(r"(Pause|Menu|Photo|Inventor|Item|Dialogue|Time|Slow|Death|Open|Close|Toggle|UI|Widget|Input|Map|Journal|Shop|PhotoMode)", re.I)

pk = pakread.Pak()
for path in TARGETS:
    d = pk.payload(path)
    info = blueprints.parse(d)
    imp, exp = blueprints.try_imports_exports(d, info)
    print("\n" + "=" * 78)
    print(path)
    print("  names=%d imports=%d exports=%d" % (info["name_count"], len(imp), len(exp)))
    print("  --- exports (functions/components/widgets) ---")
    for e in exp:
        print("     %-48s class=%s" % (e["name"], e["class"]))
    print("  --- context names ---")
    seen = []
    for n in info["names"]:
        if n and CTX.search(n) and n not in seen:
            seen.append(n)
    print("     " + ", ".join(seen[:160]))
    print("  --- pause/dilation imports ---")
    for i in imp:
        if i["name"] in ("SetGamePaused", "SetGlobalTimeDilation", "IsGamePaused",
                         "SetPause", "GetGlobalTimeDilation", "SetTickableWhenPaused",
                         "SetInputModeUIOnly", "SetInputModeGameAndUI", "SetIgnoreMoveInput",
                         "SetIgnoreLookInput"):
            print("     class=%s name=%s" % (i["class"], i["name"]))
pk.close()
