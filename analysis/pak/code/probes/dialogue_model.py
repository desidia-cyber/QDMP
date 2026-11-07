#!/usr/bin/env python3
"""D2 - the dialogue data model.

Run:  python dialogue_model.py
Needs: ../pakread.py, ../blueprints.py, the pak, and ../../../exe_strings_all.txt
       (the shipping exe's string pool, produced by analysis/stringsx.py).

What this recovers, and from where
----------------------------------
* the per-NPC dialogue asset class (FS_DialogueData) and the count of them;
* the field vocabulary of FS_DialogueData: fields shared by *every* dialogue
  asset (the struct/type schema) vs names only some assets carry (sequence
  labels and NamedSaveData keys) - a set difference over name tables, which is
  exact for "this name is present in this asset";
* the four condition-related enums, from the native enum entry strings in the
  exe (EDCT_/EDCB_/EC_/ENSDCM_);
* the top-level tagged properties of one representative asset, decoded from its
  .uexp, and the localisation keys its sequence entries point at;
* the runtime driver's field vocabulary (UFS_DialogueManager blueprint).

The tag-stream model used for the .uexp decode was pinned down and then checked
to consume the asset exactly (value ends on the "None" terminator).  It is
stated here rather than assumed silently:
    tag = Name(8) Type(8) Size(4) ArrayIndex(4) [type-specific FName(s)] + 1 byte
    then Size bytes of value; a stream ends with the FName "None" (8 bytes).
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

EXE_STRINGS = os.path.abspath(os.path.join(HERE, "..", "..", "..", "exe_strings_all.txt"))

# tag extras per UE property type (extra FNames serialised after ArrayIndex)
TAG_EXTRA = {"ArrayProperty": 8, "SetProperty": 8, "StructProperty": 8,
             "ByteProperty": 8, "EnumProperty": 8, "MapProperty": 16}

ENUMS = ["EFS_DialogueConditionType", "EFS_DialogueConditionBehaviour",
         "EFS_Comparator", "EFS_NamedSaveDataChangeMode"]


def load(pk, p):
    d = pk.payload(p)
    info = blueprints.parse(d)
    imp, exp = blueprints.try_imports_exports(d, info)
    uexp = p.rsplit(".", 1)[0] + ".uexp"
    blob = pk.payload(uexp) if uexp in pk.entries else b""
    return d, info, imp, exp, blob


def parse_tags(blob, names):
    """Top-level tagged property list. Returns (props, end, terminated)."""
    def nm(k):
        return names[k] if 0 <= k < len(names) else None
    o, props = 0, []
    while o + 8 <= len(blob):
        ni, nn = struct.unpack("<ii", blob[o:o + 8])
        N = nm(ni)
        if N is None:
            return props, o, False
        if N == "None":
            return props, o + 8, True
        if o + 24 > len(blob):
            return props, o, False
        ti, tn = struct.unpack("<ii", blob[o + 8:o + 16])
        sz, ai = struct.unpack("<ii", blob[o + 16:o + 24])
        T = nm(ti)
        if T is None:
            return props, o, False
        vo = o + 24 + TAG_EXTRA.get(T, 0) + 1
        props.append({"name": N, "type": T, "size": sz, "index": ai,
                      "value_off": vo, "value": blob[vo:vo + sz]})
        o = vo + sz
    return props, o, False


def enum_entries(exe_strings, enum):
    if not os.path.exists(exe_strings):
        return []
    out = set()
    with open(exe_strings, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            line = line.rstrip("\r\n")
            if line.startswith(enum + "::"):
                out.add(line.split("::", 1)[1])
    return sorted(out)


def localisation_keys(blob):
    """ASCII FString runs that look like string-table keys (d_*, cha_* ...)."""
    runs = [s.decode() for s in re.findall(rb"[\x20-\x7e]{3,}", blob)]
    return [r for r in runs if re.match(r"^[a-z0-9]+(_[a-z0-9]+)+$", r)]


def main():
    with pakread.Pak() as pk:
        # ---- 1. inventory of dialogue assets --------------------------------
        all_dlg = sorted(p for p in pk.entries
                         if "Dialogue" in p and p.endswith(".uasset"))
        data_assets, ui_assets = [], []
        for p in all_dlg:
            d, info, imp, exp, blob = load(pk, p)
            base = p.rsplit("/", 1)[-1][:-7]
            top = [e for e in exp if e["name"] in (base, base + "_C")]
            cls = top[0]["class"] if top else None
            if cls == "FS_DialogueData":
                data_assets.append(p)
            else:
                ui_assets.append((p, cls))
        print("=== 1. dialogue assets ===")
        print("  uassets matching 'Dialogue': %d" % len(all_dlg))
        print("  FS_DialogueData data assets : %d" % len(data_assets))
        print("  everything else (UI/actions): %d" % len(ui_assets))
        for p, c in ui_assets:
            print("    %-58s top-export class=%s" % (p.split("Content/")[1], c))

        # ---- 2. shared vs per-asset vocabulary ------------------------------
        per_asset = {}
        for p in data_assets:
            d, info, imp, exp, blob = load(pk, p)
            props, funcs, _ = blueprints.classify(info["names"])
            per_asset[p] = set(props)
        common = set.intersection(*per_asset.values()) if per_asset else set()
        print("\n=== 2. FS_DialogueData field vocabulary ===")
        print("  names shared by all %d assets (the type's own schema):" % len(per_asset))
        for n in sorted(common):
            print("    ", n)

        print("\n  per-asset literals - sequence labels (contain a space):")
        labels = set()
        for p, s in per_asset.items():
            labels |= {x for x in s - common if " " in x}
        for n in sorted(labels):
            print("    ", n)

        print("\n  per-asset literals - NamedSaveData condition keys (no space):")
        keys = {}
        for p, s in per_asset.items():
            ks = {x for x in s - common
                  if " " not in x and "_" in x and not x.startswith("/")
                  and x not in (p.rsplit("/", 1)[-1][:-7],)}
            if ks:
                keys[p] = ks
        for p in sorted(keys):
            print("    %-62s %s"
                  % (p.split("Content/")[1][-62:], sorted(keys[p])))

        # ---- 3. the condition enums -----------------------------------------
        print("\n=== 3. condition enums (native UENUM entries, from the exe) ===")
        print("  source:", EXE_STRINGS)
        for e in ENUMS:
            vals = enum_entries(EXE_STRINGS, e)
            print("  %-32s %d value(s): %s" % (e, len(vals), ", ".join(vals)))

        # ---- 4. one asset decoded -------------------------------------------
        rep = "QueensDomain/Content/NPCs/Knight/Knight_Arjaot/Knight_Arjaot_Dialogue_00.uasset"
        d, info, imp, exp, blob = load(pk, rep)
        props, end, ok = parse_tags(blob, info["names"])
        print("\n=== 4. decoded example: %s ===" % rep.split("Content/")[1])
        print("  uexp %d bytes; top-level tags consume %d, terminated=%s (remainder %d)"
              % (len(blob), end, ok, len(blob) - end))
        for pr in props:
            extra = ""
            if pr["type"] == "ArrayProperty":
                cnt = struct.unpack("<i", pr["value"][:4])[0] if len(pr["value"]) >= 4 else -1
                extra = "  element count=%d" % cnt
            print("    %-22s %-14s size=%-5d%s" % (pr["name"], pr["type"], pr["size"], extra))
        print("  localisation keys in this asset:",
              [k for k in localisation_keys(blob)][:12])
        print("  referenced packages:",
              sorted({x["name"] for x in imp if x["class"] == "Package"}))

        # ---- 5. the runtime driver -------------------------------------------
        dm = "QueensDomain/Content/UI/DialogueSystem/DialogueManager.uasset"
        d, info, imp, exp, blob = load(pk, dm)
        props, funcs, _ = blueprints.classify(info["names"])
        print("\n=== 5. runtime driver: %s ===" % dm.split("Content/")[1])
        print("  %d names, %d imports, %d exports" % (info["name_count"], len(imp), len(exp)))
        interesting = [n for n in props if any(k in n for k in
                       ("Dialogue", "Option", "Condition", "Action", "NamedSave",
                        "Sequence", "Entry", "Running", "Finished"))]
        for n in sorted(set(interesting)):
            print("    ", n)
        print("  UFUNCTION/event exports:",
              sorted({e["name"] for e in exp if e["class"] == "Function"}))

    return 0


if __name__ == "__main__":
    sys.exit(main())
