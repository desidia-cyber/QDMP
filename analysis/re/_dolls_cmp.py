import sys
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
from extlib import Image

D = r"G:\Freespace\dollsMP\tools\builddiff\DollsNest-Win64-Shipping.exe"
d = Image(D)
print("dolls sha256:", d.sha256())
print("expected   : 82776A61B1F5CA9A89CBD73A8545A6193DB4802EE9A70F7487851CAEF4225360")
print("sections:", [(s[0], hex(s[1])) for s in d.sections][:4])
print()
d.show(0x29235E0, 18, "Dolls GetPlayerCameraManager")
d.show(0x2923660, 30, "Dolls GetPlayerController")
d.show(0x2923970, 30, "Dolls GetPlayerPawn")
