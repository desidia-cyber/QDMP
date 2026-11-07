#!/usr/bin/env python3
import json, os, sys
import gvas

h = gvas.load(os.path.join(gvas.SAVES, "World0.sav"))
p = h["props"]

print("=== NamedSaveData (all) ===")
for k, v in sorted(p["NamedSaveData"].items()):
    print(f"  {k:<45} {v}")
print("count:", len(p["NamedSaveData"]))

print("\n=== SavableActorStates (all) ===")
for k, v in p["SavableActorStates"].items():
    print(f"  {k} = {v}")

print("\n=== EnemySaveStates (raw) ===")
print(json.dumps({k: v for k, v in p["EnemySaveStates"].items()}))

print("\n=== Equipment / Inventory / Slots ===")
print(json.dumps(p["Equipment"], indent=1))
print(json.dumps(p["InventoryContents"], indent=1))
print(json.dumps(p["EquipmentSlots"], indent=1))
print("\n=== WeaponCharges non-zero ===")
nz = {k: v for k, v in p["WeaponCharges"].items() if v}
print(len(nz), "of", len(p["WeaponCharges"]))
for k, v in list(nz.items())[:40]:
    print(f"  {v:>8}  {k}")
print("\n=== DaggerCharges non-zero ===")
nz = {k: v for k, v in p["DaggerCharges"].items() if v}
print(len(nz), "of", len(p["DaggerCharges"]))
for k, v in list(nz.items())[:10]:
    print(f"  {v:>10}  {k}")
