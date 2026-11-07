import re
rows=[]
for line in open('analysis/10-gaps-register.md',encoding='utf-8',errors='replace'):
    m=re.match(r'\| ([A-E]\d+) \|',line)
    if not m: continue
    cells=[c.strip() for c in line.strip().strip('|').split('|')]
    if len(cells)<3: continue
    st=cells[2]
    if 'CLOSED' in st and 'PARTIAL' not in st: b='CLOSED'
    elif 'PARTIAL' in st: b='PARTIAL'
    elif 'OPEN' in st: b='OPEN'
    else: b='?'
    rows.append((cells[0],b))
# dedupe: the closure table also has ids; keep the first occurrence (the registry)
seen={}
for k,b in rows: seen.setdefault(k,b)
from collections import Counter
print('registry items:',len(seen),dict(Counter(seen.values())))
print('non-closed:',[k for k,b in seen.items() if b!='CLOSED'])
