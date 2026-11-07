paths=[l.rstrip('\n').split('\t')[0] for l in open(r"G:\Freespace\Queens_Domain\analysis\pak\code\entries.tsv",encoding='utf-8')]
def show(title, pred, limit=400):
    sel=sorted(p for p in paths if pred(p))
    print("\n=== %s : %d ==="%(title,len(sel)))
    for p in sel[:limit]: print("  "+p)
show("contains 'table' (case-insens)", lambda p: 'table' in p.lower())
show("contains 'data/DataTable'", lambda p: '/data/' in p.lower() or 'datatable' in p.lower())
