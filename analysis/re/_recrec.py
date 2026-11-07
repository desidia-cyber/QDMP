import sys, struct
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
import qdlib as q

BASE = 0x140000000

def ascii_at(rva, n=64):
    b = q.read_rva(rva, n) or b""
    return "".join(chr(c) if 32 <= c < 127 else "." for c in b)

def qwords(rva, n):
    b = q.read_rva(rva, n) or b""
    return [struct.unpack_from("<Q", b, i)[0] for i in range(0, len(b) - 7, 8)]

def dump_region(rva, n, label):
    print("=== %s %08X ===" % (label, rva))
    b = q.read_rva(rva, n) or b""
    for off in range(0, len(b), 16):
        chunk = b[off:off + 16]
        asc = "".join(chr(c) if 32 <= c < 127 else "." for c in chunk)
        print("  %08X  %s |%s|" % (rva + off, chunk.hex(" "), asc))

# SpawnActor record region
dump_region(0x040E61C0, 0xE0, "around SpawnActor name")
print()
# interpret as qwords, flag any that point into .text or .rdata
b = q.read_rva(0x040E61C0, 0xE0)
for i, v in enumerate(qwords(0x040E61C0, 0xE0)):
    rva = v - BASE if v >= BASE else None
    where = ""
    if rva is not None:
        if 0x1000 <= rva < 0x373A62B:
            where = " .text FUN_%08X" % rva
        elif 0x373B000 <= rva < 0x46F0DC2:
            where = " .rdata '%s'" % ascii_at(rva, 32)
    print("  +%02X  %08X%s" % (i * 8, v, where))
