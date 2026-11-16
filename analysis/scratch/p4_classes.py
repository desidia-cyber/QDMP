#!/usr/bin/env python3
"""p4_classes.py -- for one or more SSEZ/skull classes, print the pak path, the super chain
and the names the state lane would read (property/function names containing state words).

Usage: python p4_classes.py Pax_Prison_key ...   (names may omit the _C suffix)
"""
import re
import sys

sys.path.insert(0, r"G:\Freespace\Queens_Domain\.worktrees\sync-fix\analysis\pak\code")
import pakread          # noqa: E402
import blueprints       # noqa: E402

WORDS = re.compile(r'(Opened|Broken|Acquired|Picked|Activat|Open|Close|Interact|Activate|'
                   r'Restore|Save|State|Solved|Finished|Complete|Consumed|Insert|Key)', re.I)


def main():
    want = [w.lower() for w in sys.argv[1:]]
    if not want:
        print(__doc__)
        return 2
    with pakread.Pak() as pk:
        for path in sorted(pk.entries):
            if not path.lower().endswith('.uasset'):
                continue
            base = path.rsplit('/', 1)[-1][:-7].lower()
            if not any(base == w or base == w + '_bp' or base.startswith(w) for w in want):
                continue
            if '/Maps/' in path or 'Default__' in path:
                continue
            try:
                d = pk.payload(path)
                info = blueprints.parse(d)
                imp, exp = blueprints.try_imports_exports(d, info)
            except Exception as exc:                      # noqa: BLE001
                print('!! %s: %s' % (path, exc))
                continue
            print('== %s' % path)
            print('   exports=%d' % (len(exp) if exp else 0))
            sup = None
            for e in (exp or []):
                if e.get('class') == 'BlueprintGeneratedClass':
                    sup = e.get('super') or sup
                    print('   class %s  super=%s' % (e.get('name'), e.get('super')))
            names = []
            for e in (exp or []):
                nm = e.get('name') or ''
                if nm and not nm.startswith('Default__') and WORDS.search(nm):
                    names.append('%s:%s' % (e.get('class'), nm))
            for n in sorted(set(names))[:40]:
                print('     %s' % n)
            print()
    return 0


if __name__ == '__main__':
    sys.exit(main())
