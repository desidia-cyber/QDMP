import collections
rows=[l.rstrip('\n').split('\t') for l in open(r"G:\Freespace\Queens_Domain\analysis\pak\code\entries.tsv",encoding='utf-8')]
paths=[r[0] for r in rows]
print("total", len(paths))
def show(title, pred, limit=60):
    sel=[p for p in paths if pred(p)]
    print("\n=== %s : %d ==="%(title,len(sel)))
    for p in sorted(sel)[:limit]: print("  "+p)
show("DataTable paths", lambda p: '/DataTable' in p or '/DataTables/' in p)
show("DT_ names", lambda p: '/DT_' in p or p.rsplit('/',1)[-1].startswith('DT_'))
show("locres", lambda p: p.lower().endswith('.locres'))
show("locmeta", lambda p: p.lower().endswith('.locmeta'))
show("StringTable", lambda p: 'StringTable' in p)
