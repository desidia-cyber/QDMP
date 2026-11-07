import sys, re, marshal, collections
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.path.insert(0, r'G:\Freespace\Queens_Domain\analysis\re')
import qdlib as q

M = marshal.load(open(r'G:\Freespace\Queens_Domain\analysis\re\nativepairs.bin', 'rb'))
P = M['pairs']
DISPATCH = 0x0119D280

def last_direct_call(exec_rva, maxn=80):
    ins = q.disasm(exec_rva, count=maxn)
    calls = []
    for i in ins:
        if i.mnemonic == 'call' and i.operands and i.operands[0].type == 2:
            t = i.operands[0].imm & 0xFFFFFFFF
            calls.append(t)
        if i.mnemonic == 'ret':
            break
    return calls

def impl_of(exec_rva):
    if exec_rva == DISPATCH:
        return None, 'DISPATCH'
    calls = last_direct_call(exec_rva)
    if not calls:
        return None, 'no-direct-call'
    # last call that is in .text
    for t in reversed(calls):
        if 0x1000 <= t < 0x373B000:
            return t, None
    return None, 'no-text-call'

PAT = re.compile(r'Spawn|Construct|Create|Possess|Merge|Restart|Pawn|Body|Doll|Statue|Station|Configur|Pose|Merge', re.I)
rows = []
for name in sorted(P):
    if not PAT.search(name):
        continue
    for (base, exec_rva, name_rva) in P[name]:
        impl, why = impl_of(exec_rva)
        rows.append((name, base, exec_rva, impl, why))

# group by impl
print('total (name,table,exec) rows:', len(rows))
print('\n=== rows with a real impl ===')
seen = set()
for name, base, exec_rva, impl, why in rows:
    key = (name, impl)
    if key in seen:
        continue
    seen.add(key)
    print('%-42s exec %08X -> %s %s' % (name, exec_rva,
          ('FUN_%08X' % impl) if impl else '-', why or ''))
