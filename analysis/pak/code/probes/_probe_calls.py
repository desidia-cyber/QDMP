import sys, struct, importlib.util
sys.path.insert(0, r"..")
import pakread, blueprints
spec = importlib.util.spec_from_file_location("bpr", "bp_bytecode_refs.py")
bpr = importlib.util.module_from_spec(spec); spec.loader.exec_module(bpr)
pk = pakread.Pak()

def dump(path, funcs):
    data,_ = bpr.combined(pk, path)
    names, imports, exports = bpr.tables(data)
    # map index value -> label
    label = {}
    for i,imp in enumerate(imports):
        label[-(i+1)] = "IMP:"+imp[2]
    for i,e in enumerate(exports):
        label[i+1] = "EXP:"+e["name"]
    print("="*78); print(path)
    for fn in funcs:
        e = next((x for x in exports if x["name"]==fn), None)
        if not e:
            print("  %-34s MISSING" % fn); continue
        lo,hi=e["serial_off"], e["serial_off"]+e["serial_size"]
        seg=data[lo:hi]
        found={}
        # scan every 4-byte window
        for off in range(0, len(seg)-3):
            v=struct.unpack("<i", seg[off:off+4])[0]
            if v in label and label[v].startswith(("EXP:","IMP:")):
                lab=label[v]
                # skip self
                if lab=="EXP:"+fn: continue
                found.setdefault(lab, off)
        print("  %-34s size=%d ->" % (fn, e["serial_size"]))
        for lab,off in sorted(found.items(), key=lambda kv: kv[1]):
            print("       +0x%04X %s" % (off, lab))

dump("QueensDomain/Content/Blueprints/Door_Base.uasset",
     ["Restore State From Save Data","SetToOpenState","OpenDoor","CloseDoor","UpdateDoorStateBasedOnActivation","Activate","Deactivate","ActivateByLoad","DeactivateByLoad","Interact","TryUseKey"])
dump("QueensDomain/Content/Props/Containers/Permabreakable_base.uasset",
     ["Break","AlreadyBroke","Restore State From Save Data","Activate","Deactivate","TryApplyDamage","SetPartsVisibility"])
dump("QueensDomain/Content/Blueprints/SecretWall.uasset",
     ["Restore State From Save Data","ForceOpen","SetToFinalState","Activate","Deactivate","DoSecretWallAnim","UpdateSecretWallData"])
pk.close()
