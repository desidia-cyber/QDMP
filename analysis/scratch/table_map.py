import sys, struct, json, collections
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.path.insert(0, r'G:\Freespace\Queens_Domain\analysis\re')
import qdlib as q

recs = json.load(open(r'G:\Freespace\Queens_Domain\analysis\scratch\game_classes.json'))
M = json.load(open(r'G:\Freespace\Queens_Domain\analysis\scratch\game_classes.json'))
# rebuild runs map from nativepairs
import marshal
NP = marshal.load(open(r'G:\Freespace\Queens_Domain\analysis\re\nativepairs.bin', 'rb'))
run_bases = {b: n for b, n in NP['runs']}
base_to_names = collections.defaultdict(list)
for nm, lst in NP['pairs'].items():
    for b, c, nr in lst:
        base_to_names[b].append(nm)

STUB = 0x007746E0
rows = []
for r in recs:
    r9 = r.get('r9')
    if not r9 or r9 == STUB:
        continue
    # decode the register function: collect lea targets + immediates
    f = q.func_at(r9)
    end = f[1] if f else r9 + 0x120
    leas = []
    imms = []
    for ins in q.disasm(r9, end_rva=end):
        t = q.insn_target(ins)
        if t is not None and ins.mnemonic == 'lea':
            leas.append(t)
        if ins.mnemonic == 'mov' and 'r8d' in ins.op_str and ',' in ins.op_str:
            try:
                imms.append(int(ins.op_str.split(',')[-1], 16))
            except Exception:
                pass
        if ins.mnemonic in ('jmp',) and ins.operands and ins.operands[0].type == 2:
            t2 = ins.operands[0].imm & 0xFFFFFFFF
            if 0x1000 <= t2 < 0x373B000:
                leas.append(('jmp', t2))
    # the function-table base is a lea target not in .data
    cand = [x for x in leas if not isinstance(x, tuple)]
    rows.append((r['name'], r9, cand, imms))

for name, r9, cand, imms in sorted(rows):
    hit = []
    for c in cand:
        if c in run_bases:
            hit.append('%08X(entries=%d)' % (c, run_bases[c]))
    print('%-42s r9=%08X leas=%s imm=%s  TABLE %s'
          % (name, r9, ['%08X' % x for x in cand[:5]], imms[:3], hit))

print('\n=== mapping table base -> game class ===')
base2class = {}
for r in recs:
    r9 = r.get('r9')
    if not r9 or r9 == STUB:
        continue
    f = q.func_at(r9)
    end = f[1] if f else r9 + 0x120
    for ins in q.disasm(r9, end_rva=end):
        t = q.insn_target(ins)
        if t is not None and ins.mnemonic == 'lea' and t in run_bases:
            base2class.setdefault(t, []).append(r['name'])
print('mapped tables:', len(base2class))
for b in sorted(base2class):
    print('  table %08X entries=%d  class=%s' % (b, run_bases[b], base2class[b]))
json.dump({str(k): v for k, v in base2class.items()},
          open(r'G:\Freespace\Queens_Domain\analysis\scratch\table2class.json', 'w'), indent=1)
