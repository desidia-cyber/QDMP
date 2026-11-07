rows=[]; marks=[]
for line in open('frida_clock.csv',encoding='utf-8',errors='replace'):
    line=line.rstrip('\n')
    if line.startswith('# MARKER'): marks.append((len(rows),line[9:].strip())); continue
    if line.startswith('#') or line.startswith('t_ms') or not line: continue
    p=line.split(',')
    if len(p)<8: continue
    try: rows.append((float(p[6]),float(p[2]),float(p[4]),int(p[0])))
    except: pass
print('%-24s %8s %8s %8s %s'%('marker','d_min','d_max','t_slope','verdict'))
for i,(idx,name) in enumerate(marks):
    end = marks[i+1][0] if i+1<len(marks) else min(len(rows),idx+40)
    seg=rows[idx:end]
    if len(seg)<3: continue
    ds=[r[0] for r in seg]; dt=(seg[-1][3]-seg[0][3])/1000.0
    slope=(seg[-1][1]-seg[0][1])/max(0.001,dt)
    verdict='PAUSED' if slope<0.15 else ('running' if slope>0.5 else '?')
    print('%-24s %8.4f %8.4f %8.3f %s'%(name,min(ds),max(ds),slope,verdict))
