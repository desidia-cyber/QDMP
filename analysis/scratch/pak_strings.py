import sys, os, bisect
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.path.insert(0, r'G:\Freespace\Queens_Domain\analysis\pak\code')
import pakread

PAK = pakread.PAK_PATH
pk = pakread.Pak()
# build (start,end,path) intervals over the payloads
iv = []
for path, (off, size, flags) in pk.entries.items():
    iv.append((off + pakread.ENTRY_HEADER_SIZE, off + pakread.ENTRY_HEADER_SIZE + size, path))
iv.sort()
starts = [x[0] for x in iv]
print('entries:', len(iv))

import bisect
def owner(pos):
    i = bisect.bisect_right(starts, pos) - 1
    if i < 0:
        return None
    s, e, p = iv[i]
    return p if s <= pos < e else None

TOKENS = [b'SetOwnerNoSee', b'SetOnlyOwnerSee', b'bOwnerNoSee', b'bOnlyOwnerSee',
          b'bOwnerNoSee', b'Player_FullChar_2026_IntroAnimation',
          b'Player_FullChar_2026_coolPose', b'Prince_Eat_MA',
          b'CutsceneSkeletalMeshActor', b'IntroCutscene', b'LevelSequenceActor',
          b'bReplicates', b'bReplicateMovement']

# stream the pak once
buf_size = 8 * 1024 * 1024
overlap = 64
results = {t: {} for t in TOKENS}
f = open(PAK, 'rb')
pos = 0
prev = b''
while True:
    chunk = f.read(buf_size)
    if not chunk:
        break
    data = prev + chunk
    base = pos - len(prev)
    for t in TOKENS:
        p = 0
        while True:
            p = data.find(t, p)
            if p < 0:
                break
            abs_pos = base + p
            own = owner(abs_pos)
            if own:
                results[t][own] = results[t].get(own, 0) + 1
            p += 1
    prev = data[-overlap:]
    pos += len(chunk)
f.close()
pk.close()

for t in TOKENS:
    print('\n=== %s : %d asset(s) ===' % (t.decode(), len(results[t])))
    for p in sorted(results[t]):
        print('   %-90s x%d' % (p, results[t][p]))
