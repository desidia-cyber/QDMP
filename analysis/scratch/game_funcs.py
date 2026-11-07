import sys, marshal, collections, json
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.path.insert(0, r'G:\Freespace\Queens_Domain\analysis\re')
import qdlib as q

NP = marshal.load(open(r'G:\Freespace\Queens_Domain\analysis\re\nativepairs.bin', 'rb'))
run_bases = {b: n for b, n in NP['runs']}
base_names = collections.defaultdict(list)
for nm, lst in NP['pairs'].items():
    for b, c, nr in lst:
        base_names[b].append((nm, c, nr))

T2C = json.load(open(r'G:\Freespace\Queens_Domain\analysis\scratch\table2class.json'))
T2C = {int(k): v for k, v in T2C.items()}

for b in sorted(T2C):
    names = sorted(set(n for n, c, nr in base_names.get(b, [])))
    print('\n=== table %08X  entries_in_run=%d  class=%s ===' % (b, run_bases.get(b, 0), T2C[b]))
    for n in names:
        execs = ['%08X' % c for nm, c, nr in base_names[b] if nm == n]
        print('   %-40s %s' % (n, execs))
