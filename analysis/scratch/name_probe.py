import sys, struct, marshal
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.path.insert(0, r'G:\Freespace\Queens_Domain\analysis\re')
import qdlib as q
import hunt

# 1) verify pair-table name strings
for rva in (0x03F08970, 0x03F08988, 0x04061C10, 0x04061C20):
    b = q.read_rva(rva, 40)
    s = b.split(b'\0')[0]
    print('name @%08X = %r' % (rva, s))

# 2) function boundaries around the two impls
for r in (0x02664630, 0x02664640, 0x026599F0, 0x02651580):
    print('func_at(%08X) = %s' % (r, q.func_at(r)))

# 3) find wide strings for player classes in .rdata and their xrefs
d = q.data()
needles = ['FS_PlayerCharacter', 'FS_Full_PlayerChar', 'FS_PlayerChar',
           'FS_StationarySkeletalMeshActor', 'FS_ConfigurableStaticMeshComponent']
secs = []
for name, va, vsz, raw, rawsz, ch in q.sections():
    if name in ('.rdata', '.data', '_RDATA'):
        secs.append((name, va, d[raw:raw + min(vsz, rawsz)]))
for n in needles:
    w = n.encode('utf-16-le')
    print('\n== %s (wide) ==' % n)
    for name, va, blob in secs:
        p = 0
        while True:
            p = blob.find(w, p)
            if p < 0:
                break
            rva = va + p
            xr = hunt.refs_to(rva)
            print('  %s:%08X xrefs=%d %s' % (name, rva, len(xr),
                  ['%08X/FUN_%08X' % (s, f) for s, f, m in xr[:6]]))
            p += 2
    a = n.encode('latin-1')
    print('  -- ascii --')
    for name, va, blob in secs:
        p = 0
        cnt = 0
        while cnt < 12:
            p = blob.find(a, p)
            if p < 0:
                break
            rva = va + p
            xr = hunt.refs_to(rva)
            if xr:
                print('  %s:%08X xrefs=%d %s' % (name, rva, len(xr),
                      ['%08X/FUN_%08X' % (s, f) for s, f, m in xr[:6]]))
                cnt += 1
            p += 1
