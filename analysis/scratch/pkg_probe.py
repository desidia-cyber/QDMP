import sys, struct, marshal, re
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.path.insert(0, r'G:\Freespace\Queens_Domain\analysis\re')
import qdlib as q
import hunt

# 1) what is at 0x039390F8 (the PackageName passed by every Z_Construct)?
b = q.read_rva(0x039390F8, 80)
print('raw @039390F8:', b)
# try ansi + wide interpretations
print('ansi:', b.split(b'\0')[0])
print('wide:', b.decode('utf-16-le', 'replace').split('\x00')[0])
b2 = q.read_rva(0x0376A628, 80)
print('arg9 @0376A628 raw:', b2[:40], 'wide:', b2.decode('utf-16-le', 'replace').split('\x00')[0])
b3 = q.read_rva(0x0376A5D8, 80)
print('@0376A5D8 wide:', b3.decode('utf-16-le', 'replace').split('\x00')[0])

xr = hunt.refs_to(0x039390F8)
print('\nxrefs to 0x039390F8:', len(xr))
funcs = sorted(set(f for s, f, m in xr))
print('distinct functions:', len(funcs))
for f in funcs[:80]:
    print('   FUN_%08X' % f)
