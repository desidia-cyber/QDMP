import sys
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.path.insert(0, r'G:\Freespace\Queens_Domain\analysis\pak\code')
import pakread, blueprints as bp

pk = pakread.Pak()
targets = [p for p in pk.entries if p.endswith('.umap') and
           ('Intro_Level' in p or '997_FinalZone' in p or 'title_screen' in p)]
targets += [p for p in pk.entries if p.endswith('.uasset') and
            ('Ending1Cutscene' in p or 'CutsceneSkeletalMeshActor' in p)]
for t in sorted(set(targets)):
    data = pk.payload(t)
    info = bp.parse(data)
    imp, exp = bp.try_imports_exports(data, info)
    print('\n' + '=' * 90)
    print('%s  size=%d names=%d exports=%d' % (t, info['size'], len(info['names']), len(exp)))
    print('=' * 90)
    if exp:
        from collections import Counter
        c = Counter()
        interesting = []
        for e in exp:
            cls = e['class'] or '?'
            c[cls] += 1
            if any(k in cls for k in ('SkeletalMesh', 'Cutscene', 'Sequence', 'Camera',
                                      'Player', 'Character', 'StaticMesh')) or \
               any(k in (e['name'] or '') for k in ('Player', 'Cutscene', 'Seq', 'Camera', 'Prince')):
                interesting.append(e)
        print('top classes:')
        for k, v in c.most_common(25):
            print('   %-42s %d' % (k, v))
        print('interesting exports (%d):' % len(interesting))
        for e in interesting[:80]:
            print('   %-42s class=%-40s super=%s' % (e['name'], e['class'], e['super']))
    else:
        print('exports did not validate')
pk.close()
