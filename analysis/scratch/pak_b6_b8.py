import sys, os
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.path.insert(0, r'G:\Freespace\Queens_Domain\analysis\pak\code')
import pakread
import blueprints as bp

pk = pakread.Pak()
want = [p for p in pk.entries if p.endswith(('.uasset', '.umap')) and
        any(k in p for k in ('FS_PlayerChar', 'FS_Full_PlayerChar',
                             'FS_StationarySkeletalMeshActor',
                             'FS_ConfigurableStaticMeshComponent'))]
print('candidate assets:', len(want))
for p in sorted(want):
    print('  %8d  %s' % (pk.size_of(p), p))

print('\n=== name-table scan for replication / visibility properties ===')
KEYS = ['bReplicates', 'bReplicateMovement', 'RemoteRole', 'Role',
        'bOwnerNoSee', 'bOnlyOwnerSee', 'bHidden', 'bHiddenInGame',
        'SetOwnerNoSee', 'SetOnlyOwnerSee', 'OwnerNoSee', 'OnlyOwnerSee',
        'CharacterMesh0', 'Arms', 'SkeletalMesh', 'Mesh']
for p in sorted(want):
    try:
        data = pk.payload(p)
        info = bp.parse(data)
    except Exception as e:
        print('%-70s PARSE FAIL %s' % (p, e))
        continue
    names = info['names']
    hits = [n for n in names if any(k in n for k in KEYS)]
    print('\n%-70s names=%d size=%d' % (p, len(names), info['size']))
    for n in hits:
        print('    *', n)
pk.close()
