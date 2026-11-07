import os, struct
PAK = r"G:\Freespace\Queens_Domain\QueensDomain\QueensDomain\Content\Paks\QueensDomain-WindowsNoEditor.pak"
size = os.path.getsize(PAK)
with open(PAK,'rb') as f:
    f.seek(size-4096); tail=f.read()
MAGIC=b'\xe1\x12\x6f\x5a'
p=tail.rfind(MAGIC)
print("footer at abs", size-4096+p)
o=size-4096+p
f=open(PAK,'rb')
f.seek(o); foot=f.read(200)
print("footer bytes:", foot[:64].hex(' '))
ver,=struct.unpack('<i', foot[4:8])
idx_off,idx_sz = struct.unpack('<qq', foot[8:24])
print("ver",ver,"idx_off",idx_off,"idx_sz",idx_sz)
print("hash", foot[24:44].hex())
print("bEncryptedIndex", foot[44])
print("guid", foot[45:61].hex())
print("compression methods raw", foot[61:61+64].hex(' '))
f.seek(idx_off); blob=f.read(96)
print("index head:", blob.hex(' '))
i32=lambda o: struct.unpack('<i', blob[o:o+4])[0]
n=i32(0)
print("mount len", n, blob[4:4+abs(n)])
off=4+abs(n)
print("numEntries", i32(off))
off+=4
print("seed", struct.unpack('<Q', blob[off:off+8])[0]); off+=8
print("byte at flag pos", blob[off], "off", off)
for k in range(0,16):
    print("  blob[%d]=%02x"%(off+k, blob[off+k]))
