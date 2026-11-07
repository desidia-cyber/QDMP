import re
rows=[]
for line in open('analysis/10-gaps-register.md',encoding='utf-8',errors='replace'):
    m=re.match(r'\| ([A-E]\d+) \|', line)
    if not m: continue
    cells=[c.strip() for c in line.strip().strip('|').split('|')]
    if len(cells)<3: continue
    rows.append((cells[0], cells[2]))
from collections import Counter
def band(s):
    if 'CLOSED' in s: return 'CLOSED'
    if 'PARTIAL' in s: return 'PARTIAL'
    if 'OPEN' in s: return 'OPEN'
    return '?'
c=Counter(band(s) for _,s in rows)
print('items:',len(rows),dict(c))
print('\nNON-CLOSED:')
for k,s in rows:
    if band(s)!='CLOSED': print('  %-4s %s'%(k, s[:150]))
