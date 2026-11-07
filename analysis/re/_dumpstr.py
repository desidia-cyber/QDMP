import sys; sys.path.insert(0,r"G:\Freespace\Queens_Domain\analysis\re")
import qdlib as q
for r in [0x03F955F0,0x03F94D98,0x03F0D470,0x040E6210]:
    b=q.read_rva(r,48)
    print("%08X  %s  |%s|"%(r,b.hex(' '),''.join(chr(c) if 32<=c<127 else '.' for c in b)))
    print("   back16:",q.read_rva(r-16,16))
# where is this section
for s in q.sections(): print(s[0],hex(s[1]),hex(s[1]+s[2]))
