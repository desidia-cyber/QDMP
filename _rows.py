import re
for line in open('analysis/10-gaps-register.md',encoding='utf-8',errors='replace'):
    m=re.match(r'\| (C9|D1|D8|E2) \|',line)
    if m: print(line.rstrip()[:400]); print('---')
