import sys, os, marshal
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.path.insert(0, r'G:\Freespace\Queens_Domain\analysis\re')
import qdlib as q

for root, dirs, files in os.walk(r'G:\Freespace\Queens_Domain\analysis'):
    if '__pycache__' in root:
        continue
    for fn in files:
        if fn.endswith(('.md', '.py', '.h', '.tsv')):
            p = os.path.join(root, fn)
            try:
                t = open(p, encoding='utf-8', errors='replace').read()
            except Exception:
                continue
            for pat in ('bReplicates', 'bReplicateMovement', '0x5b', '0x5B'):
                if pat in t:
                    for line in t.splitlines():
                        if pat in line:
                            print('%s | %s' % (os.path.relpath(p, r'G:\Freespace\Queens_Domain'), line.strip()[:180]))
