"""Decode the bytecode of the death/respawn/rest-point functions.

Why: the co-op death lane needs to know, before writing a line of C++, WHICH
game functions make up the death flow (does `Kill` open the game-over screen
itself?  does `Respawn` reload the world?  what does `CalculateMaxHP` read?).
Guessing here costs a live session per guess.

The cooked package is uasset + uexp; export ranges are offsets into the
concatenated original package (see bp_bytecode_refs.combined).
"""
import sys, struct
sys.path.insert(0, r"G:\Freespace\Queens_Domain\.worktrees\sync-fix\analysis\pak\code")
import pakread
import importlib.util

spec = importlib.util.spec_from_file_location(
    "bpr", r"G:\Freespace\Queens_Domain\.worktrees\sync-fix\analysis\pak\code\probes\bp_bytecode_refs.py")
bpr = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bpr)

# UE4 4.27 EExprToken (CoreUObject/Public/UObject/Script.h)
EX = {
    0x00: "LocalVariable", 0x01: "InstanceVariable", 0x02: "DefaultVariable",
    0x04: "Return", 0x06: "Jump", 0x07: "JumpIfNot", 0x08: "Assert",
    0x09: "Nothing/DeprecatedOp", 0x0B: "Nothing", 0x0F: "Let", 0x12: "ClassContext",
    0x13: "MetaCast", 0x15: "LetBool", 0x16: "EndParmValue", 0x17: "EndFunctionParms",
    0x18: "Self", 0x19: "Skip", 0x1A: "Context", 0x1B: "Context_FailSilent",
    0x1C: "VirtualFunction", 0x1D: "FinalFunction", 0x1E: "IntConst",
    0x1F: "FloatConst", 0x20: "StringConst", 0x21: "ObjectConst", 0x22: "NameConst",
    0x23: "RotationConst", 0x24: "VectorConst", 0x25: "ByteConst", 0x26: "IntZero",
    0x27: "IntOne", 0x28: "True", 0x29: "False", 0x2A: "TextConst",
    0x2B: "NoObject", 0x2C: "TransformConst", 0x2D: "IntConstByte",
    0x2E: "NoInterface", 0x2F: "DynamicCast", 0x30: "StructConst",
    0x31: "EndStructConst", 0x32: "SetArray", 0x33: "EndArray",
    0x34: "PropertyConst", 0x35: "UnicodeStringConst", 0x36: "Int64Const",
    0x37: "UInt64Const", 0x38: "PrimitiveCast", 0x39: "SetSet", 0x3A: "EndSet",
    0x3B: "SetMap", 0x3C: "EndMap", 0x3D: "SetConst", 0x3E: "EndSetConst",
    0x3F: "MapConst", 0x40: "EndMapConst", 0x42: "StructMemberContext",
    0x43: "LetMulticastDelegate", 0x44: "LetDelegate", 0x45: "LocalOutVariable",
    0x46: "InstanceDelegate", 0x47: "PushExecutionFlow", 0x48: "PopExecutionFlow",
    0x49: "ComputedJump", 0x4A: "DeprecatedOp4A", 0x4B: "PopExecutionFlowIfNot",
    0x4C: "Breakpoint", 0x4D: "InterfaceContext", 0x4E: "ObjToInterfaceCast",
    0x4F: "EndOfScript", 0x50: "CrossInterfaceCast", 0x51: "InterfaceToObjCast",
    0x5A: "WireTracepoint", 0x5B: "SkipOffsetConst", 0x5C: "AddMulticastDelegate",
    0x5D: "ClearMulticastDelegate", 0x5E: "Tracepoint", 0x5F: "LetObj",
    0x60: "LetWeakObjPtr", 0x61: "BindDelegate", 0x62: "RemoveMulticastDelegate",
    0x63: "CallMulticastDelegate", 0x64: "LetValueOnPersistentFrame",
    0x65: "ArrayConst", 0x66: "EndArrayConst", 0x67: "SoftObjectConst",
    0x68: "CallMath", 0x69: "SwitchValue", 0x6A: "InstrumentationEvent",
    0x6B: "ArrayGetByRef", 0x6C: "ClassSparseDataVariable", 0x6D: "FieldContext",
}

# opcodes that carry an FPackageIndex (property or function)
IDX_OPS = {0x00, 0x01, 0x02, 0x45, 0x1D, 0x61, 0x1C}


def fname(r, o):
    a = r.i32(o); b = r.i32(o + 4)
    return a, b, o + 8


def decode(data, names, imports, exports, lo, hi, indent=""):
    """Walk one function's script, printing calls / property references."""
    r = pakread.Stream(None) if False else None

    def i32(o):
        return struct.unpack("<i", data[o:o + 4])[0]

    def nm(idx):
        if idx == 0:
            return "None"
        if idx < 0:
            k = -idx - 1
            if 0 <= k < len(imports):
                return "IMPORT:%s.%s" % (imports[k][1], imports[k][2])
            return "IMPORT#%d" % k
        k = idx - 1
        if 0 <= k < len(exports):
            return "EXPORT:%s" % exports[k]["name"]
        return "EXPORT#%d" % k

    o = lo
    steps = 0
    while o < hi and steps < 4000:
        steps += 1
        op = data[o]
        o += 1
        name = EX.get(op, "OP_0x%02X" % op)
        if op in (0x00, 0x01, 0x02, 0x45):
            idx = i32(o); o += 4
            print("%s%-14s %s" % (indent, name, nm(idx)))
        elif op == 0x1D:            # FinalFunction(package index) + params
            idx = i32(o); o += 4
            print("%s%-14s %s" % (indent, name, nm(idx)))
        elif op == 0x1C:            # VirtualFunction(FScriptName)
            a, b, o = fname(r, o)
            print("%s%-14s %s" % (indent, name,
                                  names[a] if 0 <= a < len(names) else "name#%d" % a))
        elif op == 0x22:            # NameConst
            a, b, o = fname(r, o)
            print("%s%-14s %s" % (indent, name,
                                  names[a] if 0 <= a < len(names) else "name#%d" % a))
        elif op == 0x21:            # ObjectConst
            idx = i32(o); o += 4
            print("%s%-14s %s" % (indent, name, nm(idx)))
        elif op == 0x1E or op == 0x25:
            print("%s%-14s %d" % (indent, name, i32(o))); o += 4
        elif op == 0x1F:
            print("%s%-14s %r" % (indent, name, struct.unpack("<f", data[o:o + 4])[0])); o += 4
        elif op == 0x2D:
            print("%s%-14s %d" % (indent, name, data[o])); o += 1
        elif op == 0x06 or op == 0x07 or op == 0x19 or op == 0x47:
            print("%s%-14s +0x%X" % (indent, name, i32(o))); o += 4
        elif op == 0x0F or op == 0x15 or op == 0x5F:
            print("%s%-14s (assign)" % (indent, name))
        elif op == 0x42:            # StructMemberContext
            idx = i32(o); o += 4
            print("%s%-14s %s" % (indent, name, nm(idx)))
        elif op == 0x38:            # PrimitiveCast
            print("%s%-14s ->%s" % (indent, name, EX.get(data[o], "0x%02X" % data[o]))); o += 2
        elif op in (0x26, 0x27, 0x28, 0x29, 0x2B, 0x2E, 0x18, 0x17, 0x31, 0x33,
                    0x3A, 0x3C, 0x3E, 0x40, 0x66, 0x16, 0x48, 0x4B, 0x5A, 0x5E,
                    0x4C, 0x09):
            print("%s%-14s" % (indent, name))
        elif op == 0x04:
            print("%s%-14s" % (indent, name))
        elif op == 0x08 or op == 0x49 or op == 0x4A:
            print("%s%-14s +0x%X" % (indent, name, i32(o))); o += 4
        elif op == 0x20 or op == 0x35:
            n = struct.unpack("<i", data[o:o + 4])[0]
            raw = data[o + 4:o + 4 + max(n, 0)]
            print("%s%-14s %r" % (indent, name, raw[:60])); o += 4 + n
        elif op == 0x4F:
            print("%s%-14s" % (indent, name))
            return
        else:
            print("%s%-14s ??? stopping (operand layout unknown)" % (indent, name))
            return


def run(target, fns):
    pk = pakread.Pak()
    for path in target:
        data, _ = bpr.combined(pk, path)
        names, imports, exports = bpr.tables(data)
        print("\n" + "=" * 78)
        print(path, "exports=%d imports=%d" % (len(exports), len(imports)))
        for fn in fns:
            hit = next((e for e in exports
                        if e["name"] == fn and bpr.cls_of(exports, imports, e["cls"]) == "Function"),
                       None)
            if not hit:
                print("  %-28s : no Function export" % fn)
                continue
            print("\n  --- %s  (%d B at +0x%X) ---" % (fn, hit["serial_size"], hit["serial_off"]))
            lo = hit["serial_off"]
            hi = lo + hit["serial_size"]
            if lo < 0 or hi > len(data):
                print("    bad range")
                continue
            decode(data, names, imports, exports, lo, hi, "    ")
    pk.close()


if __name__ == "__main__":
    run([r"QueensDomain/Content/Player/FS_PlayerChar.uasset"],
        ["Kill", "ShowGameOverScreen", "Respawn", "CalculateMaxHP", "ModifyMaxHP",
         "UseFallbackCheckpoint", "FindSavedCheckpoint", "RestoreFromSave", "ResetFully"])
