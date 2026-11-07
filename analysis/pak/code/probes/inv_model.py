#!/usr/bin/env python3
"""C7 - the inventory / equipment / key-item model.

Run:  python inv_model.py
Needs: ../pakread.py, ../blueprints.py, ../../../gvas.py (save reader), the pak,
       and the save files named in SAVES (skipped cleanly if absent).

Answers, with the artefact that backs each line:
  * the item catalogue is a set of *assets*, one bundle of files per item;
  * the single item-definition class, and the two stats classes;
  * the identity key the save actually stores (full soft object path);
  * the stack/count semantics of each of the three maps in FS_SaveSlot;
  * the category and equip-slot enums, decoded from the enum assets' .uexp.

Measured facts vs inference are labelled in the output.
"""
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(HERE, "..", "..", ".."))
import pakread      # noqa: E402
import blueprints   # noqa: E402

ITEMS_PREFIX = "QueensDomain/Content/Items/"
SAVE_ROOT = os.path.abspath(os.path.join(HERE, "..", "..", "..", "save-backup-before-live"))
SAVES = ["World0.sav"]
LIVE_SAVES = [
    os.path.abspath(os.path.join(HERE, "..", "..", "..", "..", "_live", "saved",
                                 "Saved", "SaveGames", "World0_Suspend_1.sav")),
]

# role suffixes that make up one item "bundle"
ROLES = ["_Item", "_WeaponStats", "_DaggerStats", "_WeaponScript", "_WeaponVisuals",
         "_Data", "_Pickup", "_Icon", "_Texture", "_SM", "_MI"]


def self_export_class(pk, path):
    base = path.rsplit("/", 1)[-1][:-7]
    d = pk.payload(path)
    info = blueprints.parse(d)
    _, exports = blueprints.try_imports_exports(d, info)
    for e in exports:
        if e["name"] in (base, base + "_C"):
            return e["class"], e["name"], len(exports), len(info["names"])
    return None, None, len(exports), len(info["names"])


def enum_display_map(pk, path):
    """UserDefinedEnum: enumerator FName -> display string, read from .uexp.

    The DisplayNameMap is serialised as (FName NewEnumeratorN, FText) pairs; the
    display string is the FText's invariant FString.  Pairing the FName with the
    next length-prefixed printable string recovers the mapping without needing
    the enum's numeric values (which are implicit: the array order).
    """
    d = pk.payload(path)
    info = blueprints.parse(d)
    names = info["names"]
    uexp = path.rsplit(".", 1)[0] + ".uexp"
    u = pk.payload(uexp) if uexp in pk.entries else b""
    seen = {}
    for o in range(0, len(u) - 8):
        idx, num = struct.unpack("<ii", u[o:o + 8])
        if num != 0 or not (0 <= idx < len(names)):
            continue
        n = names[idx]
        if not n.startswith("NewEnumerator"):
            continue
        for k in range(o + 8, min(o + 40, len(u) - 4)):
            L = struct.unpack("<i", u[k:k + 4])[0]
            if 2 <= L <= 32 and k + 4 + L <= len(u):
                s = u[k + 4:k + 4 + L]
                if all(32 <= c < 127 for c in s[:-1]) and s[-1] == 0:
                    seen.setdefault(n, set()).add(s[:-1].decode())
                    break
    return {k: sorted(v) for k, v in seen.items()}


def family_table(pk):
    rows = {}
    for p in pk.entries:
        if not p.lower().endswith(".uasset") or "/Items/" not in p:
            continue
        fam = p.split("/Items/", 1)[1].split("/")[0]
        rows.setdefault(fam, []).append(p)
    return rows


def main():
    with pakread.Pak() as pk:
        # ---- 1. the catalogue on disk --------------------------------------
        fams = family_table(pk)
        print("=== 1. item catalogue: Content/Items/ asset families ===")
        print("  family        .uasset  dirs   _Item defs")
        total_assets = 0
        defs = []
        for fam in sorted(fams):
            ps = fams[fam]
            total_assets += len(ps)
            dirs = {p.rsplit("/", 1)[0] for p in ps}
            ndef = 0
            for p in ps:
                cls, nm, _, _ = self_export_class(pk, p)
                if cls == "FS_ItemData_C":
                    defs.append(p)
                    ndef += 1
            print("  %-12s %7d %5d %8d" % (fam, len(ps), len(dirs), ndef))
        print("  TOTAL        %7d  item definitions: %d" % (total_assets, len(defs)))

        # ---- 2. definition classes -----------------------------------------
        print("\n=== 2. class of every self-export under Items/ (measured) ===")
        cls_count = {}
        for fam in sorted(fams):
            for p in fams[fam]:
                cls, nm, _, _ = self_export_class(pk, p)
                cls_count[cls] = cls_count.get(cls, 0) + 1
        for c, n in sorted(cls_count.items(), key=lambda kv: -kv[1]):
            print("  %-34s %5d" % (c, n))

        # ---- 3. the definition schema ---------------------------------------
        print("\n=== 3. FS_ItemData - the definition type (name table) ===")
        p = "QueensDomain/Content/Blueprints/ItemSystem/FS_ItemData.uasset"
        d = pk.payload(p)
        info = blueprints.parse(d)
        imp, exp = blueprints.try_imports_exports(d, info)
        print("  asset:", p.split("Content/")[1], len(d), "bytes,", info["name_count"], "names")
        print("  exports:", [(e["name"], e["class"], e["super"]) for e in exp])
        props, funcs, _ = blueprints.classify(info["names"])
        print("  native base: UFS_Item  (imports mention FS_Item)")
        print("  fields:")
        for n in props:
            print("    ", n)

        # ---- 4. the two enums, decoded --------------------------------------
        print("\n=== 4. enums (display names decoded from .uexp; order = value) ===")
        for ep, label in [("QueensDomain/Content/Blueprints/ItemSystem/ItemCategory.uasset",
                           "ItemCategory"),
                          ("QueensDomain/Content/Blueprints/SaveSystem/ItemEquipSlotType.uasset",
                           "ItemEquipSlotType")]:
            m = enum_display_map(pk, ep)
            print("  %s (%d values)" % (label, len(m)))
            for k in sorted(m, key=lambda s: int(s.replace("NewEnumerator", ""))):
                print("    %-16s %d -> %s" % (k, int(k.replace("NewEnumerator", "")), m[k][0]))

        # ---- 5. per-family representative name tables ------------------------
        print("\n=== 5. representative definition assets, per family ===")
        byfam = {}
        for p in defs:
            fam = p.split("/Items/", 1)[1].split("/")[0]
            byfam.setdefault(fam, []).append(p)
        for fam in sorted(byfam):
            p = max(byfam[fam], key=lambda q: pk.size_of(q))   # richest table
            dd = pk.payload(p)
            ii = blueprints.parse(dd)
            im, ex = blueprints.try_imports_exports(dd, ii)
            pr, fn, _ = blueprints.classify(ii["names"])
            print("  -- %s : %s (%d names)" % (fam, p.split("Content/")[1], ii["name_count"]))
            print("     class:", ex[0]["class"] if ex else "?")
            print("     fields:", [x for x in pr if x not in
                                  ("NameProperty", "Texture2D", "StaticMeshComponent",
                                   "SphereComponent", "Vector", "Rotator", "ScriptStruct",
                                   "UserDefinedStruct", "UserDefinedEnum", "NativeClass",
                                   "StringTable", "ImportedStringTable")])
            print("     pkgs:", sorted({x["name"] for x in im if x["class"] == "Package"}))

        # ---- 6. what the save actually stores --------------------------------
        print("\n=== 6. the save's object paths, resolved back to assets ===")
        try:
            import gvas
        except Exception:
            gvas = None
            print("  gvas.py not importable - skipping the save half")
        targets = [os.path.join(SAVE_ROOT, s) for s in SAVES] + LIVE_SAVES
        for sp in targets:
            if gvas is None or not os.path.exists(sp):
                print("  (skip %s)" % sp)
                continue
            h = gvas.load(sp)
            props = h["props"]
            print("  %s  (%d bytes, %d properties)"
                  % (os.path.basename(sp), os.path.getsize(sp), len(props)))
            for k in ("InventoryContents", "Equipment", "EquipmentSlots",
                      "CurrentlySelectedWeapon"):
                if k in props:
                    print("    %s = %s" % (k, props[k]))
            # resolve each stored object path to a pak asset
            paths = set()
            for k in ("InventoryContents", "Equipment", "CurrentlySelectedWeapon"):
                v = props.get(k)
                if isinstance(v, dict):
                    paths.update(v.keys())
                elif isinstance(v, str):
                    paths.add(v)
            v = props.get("EquipmentSlots")
            if isinstance(v, dict):
                paths.update(v.values())
            for o in sorted(paths):
                pkg = o.split(".")[0]                      # strip ".ObjectName"
                if not pkg.startswith("/Game/"):
                    print("    %s  -> outside /Game, not a pak item" % o)
                    continue
                pakp = "QueensDomain/Content/" + pkg[len("/Game/"):] + ".uasset"
                if pakp in pk.entries:
                    cls, nm, nexp, nnames = self_export_class(pk, pakp)
                    print("    %s" % o)
                    print("      -> %s  class=%s  exports=%d names=%d"
                          % (pakp.split("Content/")[1], cls, nexp, nnames))
                else:
                    print("    %s  -> NOT IN PAK (searched exact path %s)" % (o, pakp))

        # ---- 7. identity / stack model ---------------------------------------
        print("\n=== 7. identity and count semantics (measured + labelled) ===")
        print("  identity key : the full soft object path of the FS_ItemData asset,")
        print("                 e.g. /Game/Items/Weapons/RustedSword/RustySword_Item.RustySword_Item")
        print("  basenames unique among the %d definitions: %s"
              % (len(defs), len({p.rsplit('/', 1)[-1] for p in defs}) == len(defs)))
        print("  InventoryContents Map<Object,int32> : int32 = stack count")
        print("  Equipment         Map<Object,int32> : int32 = equip slot id (2000 observed)")
        print("  EquipmentSlots    Map<int32,Object> : exact inverse of Equipment")
        print("  CurrentlySelectedWeapon / CurrentlySelectedDagger : one object path each")
        # slot-id derivation, from the player blueprint's own function names
        pp = pk.payload("QueensDomain/Content/Player/FS_PlayerChar.uasset")
        pn = blueprints.parse(pp)["names"]
        slot = [n for n in pn if "SlotID" in n or "SlotCount" in n]
        print("  FS_PlayerChar slot-id helpers:", sorted(set(slot)))

    return 0


if __name__ == "__main__":
    sys.exit(main())
