import csv,sys
rows=[]; marks=[]
for line in open('frida_clock.csv',encoding='utf-8',errors='replace'):
    line=line.rstrip('\n')
    if line.startswith('# MARKER'): marks.append((len(rows),line[9:].strip())); continue
    if line.startswith('#') or line.startswith('t_ms') or not line: continue
    p=line.split(',')
    if len(p)<8: continue
    try: rows.append((float(p[6]),float(p[1].split(':')[1])*60+float(p[1].split(':')[2]),float(p[2]),float(p[4]),int(p[0])))
    except: pass
print('rows',len(rows),'markers',len(marks))
for idx,name in marks:
    seg=rows[idx:idx+30]
    if not seg: continue
    ds=[r[0] for r in seg]
    print('\nMARKER %-20s  n=%d  delta min=%.4f max=%.4f  timesec_slope=%.3f  real_slope=%.3f'%(
        name,len(seg),min(ds),max(ds),(seg[-1][2]-seg[0][2])/max(0.001,(seg[-1][4]-seg[0][4])/1000.0),
        (seg[-1][3]-seg[0][3])/max(0.001,(seg[-1][4]-seg[0][4])/1000.0)))
    for r in seg[:4]: print('    delta=%.4f t=%.3f real=%.3f'%(r[0],r[2],r[3]))
