import re,glob
seen={}
for line in open('analysis/10-gaps-register.md',encoding='utf-8',errors='replace'):
    m=re.match(r'\| ([A-E]\d+) \|',line)
    if not m: continue
    cells=[c.strip() for c in line.strip().strip('|').split('|')]
    if len(cells)<3: continue
    st=cells[2]
    if 'CLOSED' in st and 'PARTIAL' not in st: b='CLOSED'
    elif 'PARTIAL' in st: b='PARTIAL'
    elif 'OPEN' in st: b='OPEN'
    else: continue
    seen.setdefault(cells[0],b)
from collections import Counter
print('REGISTRY:',len(seen),dict(Counter(seen.values())))
print()
print('--- rows mentioning residual / single source / MEDIUM ---')
for line in open('analysis/10-gaps-register.md',encoding='utf-8',errors='replace'):
    if re.match(r'\| [A-E]\d+ \|',line) and re.search(r'[Rr]esidual|single source|MEDIUM|not observed|structural|untested|UNRESOLVED',line):
        c=[x.strip() for x in line.strip().strip('|').split('|')]
        print(' ', c[0], '|', c[2][:60])
