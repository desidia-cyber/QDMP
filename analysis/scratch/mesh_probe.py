import sys, bisect
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.path.insert(0, r'G:\Freespace\Queens_Domain\analysis\pak\code')
import pakread, blueprints as bp

# ---- A: scan pak bytes for visibility tokens (no leading b)
pk = pakread.Pak()
iv = sorted((off + pakread.ENTRY_HEADER_SIZE, off + pakread.ENTRY_HEADER_SIZE + size, path)
            for path, (off, size, flags) in pk.entries.items())
starts = [x[0] for x in iv]

def owner(pos):
    i = bisect.bisect_right(starts, pos) - 1
    if i < 0:
        return None
    s, e, p = iv[i]
    return p if s <= pos < e else None

TOK = [b'OwnerNoSee', b'OnlyOwnerSee', b'bHiddenInGame', b'SetHiddenInGame',
       b'SetVisibility', b'bVisible', b'bHidden', b'SetOwnerNoSee', b'SetOnlyOwnerSee']
res = {t: {} for t in TOK}
F = open(pakread.PAK_PATH, 'rb')
pos = 0
prev = b''
while True:
    chunk = F.read(8 * 1024 * 1024)
    if not chunk:
        break
    data = prev + chunk
    base = pos - len(prev)
    for t in TOK:
        p = 0
        while True:
            p = data.find(t, p)
            if p < 0:
                break
            o = owner(base + p)
            if o:
                res[t][o] = res[t].get(o, 0) + 1
            p += 1
    prev = data[-64:]
    pos += len(chunk)
F.close()
for t in TOK:
    print('\n=== %s : %d assets ===' % (t.decode(), len(res[t])))
    for p in sorted(res[t])[:40]:
        print('   %-92s x%d' % (p, res[t][p]))

# ---- B: name tables of the two player BPs
for path in ('QueensDomain/Content/Player/FS_Full_PlayerChar.uasset',
             'QueensDomain/Content/Player/FS_PlayerChar.uasset'):
    data = pk.payload(path)
    info = bp.parse(data)
    names = info['names']
    print('\n' + '=' * 90)
    print('%s  names=%d' % (path, len(names)))
    print('=' * 90)
    if 'Full' in path:
        for i, n in enumerate(names):
            print('   %3d %s' % (i, n))
    else:
        for n in names:
            if any(k in n for k in ('Player_FullChar', 'FPArms', 'SkeletalMesh',
                                    'CharacterMesh', 'Mesh', 'Arms', 'Skeleton')):
                print('   ', n)
pk.close()
