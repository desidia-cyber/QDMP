import struct, sys
sys.path.insert(0,'.')
import pakread
pk=pakread.Pak()
for p in ("QueensDomain/Content/Blueprints/ActorQuestState.uasset",
          "QueensDomain/Content/Blueprints/SaveSystem/FS_SaveSlot.uasset"):
    d=pk.payload(p)
    print("\n####",p,"size",len(d))
    o=4
    legacy,=struct.unpack('<i',d[o:o+4]); o+=4
    ue3,=struct.unpack('<i',d[o:o+4]); o+=4
    v4,v4l=struct.unpack('<ii',d[o:o+8]); o+=8
    ncv,=struct.unpack('<i',d[o:o+4]); o+=4
    ths,=struct.unpack('<i',d[o:o+4]); o+=4
    flen,=struct.unpack('<i',d[o:o+4]); o+=4
    folder=d[o:o+abs(flen)]; o+=abs(flen)
    flags,=struct.unpack('<I',d[o:o+4]); o+=4
    nc,no=struct.unpack('<ii',d[o:o+8]); o+=8
    print("legacy",legacy,"ue3",ue3,"v4",v4,"v4l",v4l,"custom",ncv,"hdrsize",ths,"folder",folder,"flags",hex(flags),"names",nc,"nameoff",no)
    print("next fields:", d[o:o+40].hex(' '))
    r=pakread.uasset_names(d)
    print("names(%d):"%len(r['names']), r['names'][:40])
