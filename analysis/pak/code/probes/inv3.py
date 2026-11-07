paths=[l.rstrip('\n').split('\t')[0] for l in open(r"G:\Freespace\Queens_Domain\analysis\pak\code\entries.tsv",encoding='utf-8')]
qs=[p for p in paths if 'quest' in p.lower() or 'Quest' in p]
print("=== quest-named assets: %d ==="%len(qs))
for p in sorted(qs): print("  "+p)
print()
bp=[p for p in paths if p.startswith('QueensDomain/Content/Blueprints/')]
print("=== Blueprints/ root-level assets (%d of %d) ==="%(len([p for p in bp if p.count('/')==4]),len(bp)))
for p in sorted(p for p in bp if p.count('/')==4): print("  "+p)
