import sys
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.path.insert(0, r'G:\Freespace\Queens_Domain\analysis\pak\code')
import pakread, blueprints as bp
from collections import Counter

pk = pakread.Pak()
for t in [p for p in pk.entries if p.endswith('.umap') and 'Intro_Level' in p]:
    data = pk.payload(t)
    info = bp.parse(data)
    imp, exp = bp.try_imports_exports(data, info)
    print('\n' + '=' * 96)
    print('%s  size=%d names=%d exports=%d' % (t, info['size'], len(info['names']), len(exp)))
    print('=' * 96)
    if not exp:
        print('exports did not validate')
        continue
    c = Counter(e['class'] or '?' for e in exp)
    for k, v in c.most_common(60):
        print('   %-48s %d' % (k, v))
    print('\n-- non-engine-looking names --')
    for e in exp:
        cls = e['class'] or ''
        nm = e['name'] or ''
        if any(k in cls for k in ('Skeletal', 'Cutscene', 'Sequence', 'Player', 'Camera',
                                  'Character')) or any(k in nm for k in ('Player', 'Cutscene',
                                  'Seq', 'Cam', 'Prince', 'Charas', 'Mesh')):
            print('   %-46s class=%-40s super=%s' % (nm, cls, e['super']))
    print('\n-- name table tokens of interest --')
    for n in info['names']:
        if any(k.lower() in n.lower() for k in ('owner', 'nofollow', 'cutscene', 'intro',
                                                'fullchar', 'coolpose', 'sequence', 'levelseq',
                                                'player_char', 'hidden')):
            print('   ', n)
pk.close()
