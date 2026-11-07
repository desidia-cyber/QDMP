import sys; sys.path.insert(0,'.')
import pakread, struct
pk=pakread.Pak()
t="QueensDomain/Content/Blueprints/QuestStateInterface.uasset"
d=pk.payload(t)
print("size",len(d))
for o in (650, 902):
    print("at",o, d[o:o+64].hex(' '))
    print("  ints:", struct.unpack('<10i', d[o:o+40]))
print("name table region: off 193, count 18")
# names offsets
names=['/Script/CoreUObject','/Script/Engine','/Game/Blueprints/QuestStateInterface','Actor']
print("tail after names:", d[560:660].hex(' '))
