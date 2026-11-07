import sys; sys.path.insert(0,r"G:\Freespace\Queens_Domain\analysis\pak\code")
import pakread
pk=pakread.Pak()
for p in pk.entries:
    if "Player/FS_PlayerChar" in p:
        print("%9d %s"%(pk.entries[p][1],p))
pk.close()
