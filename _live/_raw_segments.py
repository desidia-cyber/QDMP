rows=[]; marks=[]
for line in open('frida_clock.csv',encoding='utf-8',errors='replace'):
    line=line.rstrip('\n')
    if line.startswith('# MARKER'): marks.append((len(rows),line[9:].strip())); continue
    if line.startswith('#') or line.startswith('t_ms') or not line: continue
    p=line.split(',')
    if len(p)<8: continue
    try: rows.append((int(p[0]),float(p[2]),float(p[4]),float(p[6])))
    except: pass
def dump(name, n=18):
    idx=next((i for i,nm in marks if nm==name), None)
    if idx is None: print('no marker',name); return
    print('\n== %s =='%name)
    for r in rows[idx:idx+n]:
        print('   t=%9.3f real=%9.3f delta=%.4f'%(r[1],r[2],r[3]))
for m in ['open-status-Tab','interact-E-normal','open-pause-Esc','open-photo-P']:
    dump(m)
