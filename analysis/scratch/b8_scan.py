import sys, time, collections
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.path.insert(0, r'G:\Freespace\Queens_Domain\analysis\re')
import qdlib as q
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_REG_RIP

# --- 1) resolve SetOwnerNoSee / SetOnlyOwnerSee exec thunks
import marshal
NP = marshal.load(open(r'G:\Freespace\Queens_Domain\analysis\re\nativepairs.bin', 'rb'))
P = NP['pairs']
for nm in ('SetOwnerNoSee', 'SetOnlyOwnerSee'):
    print('==', nm)
    for b, c, nr in P[nm]:
        calls = []
        for i in q.disasm(c, end_rva=c + 0x120):
            if i.mnemonic == 'call' and i.operands and i.operands[0].type == 2:
                t = i.operands[0].imm & 0xFFFFFFFF
                if 0x1000 <= t < 0x373B000:
                    calls.append(t)
            if i.mnemonic == 'ret':
                break
        print('   exec %08X  last_calls=%s' % (c, ['%08X' % x for x in calls[-3:]]))

# --- 2) chunked sweep for byte accesses at +0x212 / +0x213
d = q.data()
name, va, vsz, raw, rawsz, ch = q.sec_by_name('.text')
code = d[raw:raw + rawsz]
print('\ntext %08X size %08X' % (va, rawsz))
md = Cs(CS_ARCH_X86, CS_MODE_64)
md.detail = False
md2 = Cs(CS_ARCH_X86, CS_MODE_64)
md2.detail = True

WANT = {'mov', 'and', 'or', 'test', 'cmp', 'xor', 'add', 'sub', 'bts', 'btr',
        'btc', 'xchg', 'not', 'inc', 'dec', 'setne', 'sete', 'shl', 'shr', 'sar'}
FOR = ('0x212]', '0x213]')
t0 = time.time()
cands = []
CH = 8192
pos = 0
n = len(code)
while pos < n:
    chunk = code[pos:pos + CH]
    consumed = 0
    for ins in md.disasm(chunk, va + pos):
        consumed = ins.address - va + ins.size - pos
        if ins.mnemonic in WANT and any(f in ins.op_str for f in FOR):
            cands.append(ins.address)
    pos += consumed if consumed > 0 else 1
print('candidates %d  %.1fs' % (len(cands), time.time() - t0))

hits = []
for a in sorted(set(cands)):
    off = q.rva_to_off(a)
    for ins in md2.disasm(d[off:off + 15], a):
        for op in ins.operands:
            if op.type == X86_OP_MEM and op.size == 1 and op.mem.base not in (0, X86_REG_RIP) \
               and op.mem.disp in (0x212, 0x213):
                hits.append((a, ins.mnemonic, ins.op_str, op.mem.disp, op.size))
        break
print('validated %d' % len(hits))
byfn = collections.defaultdict(list)
for a, mn, ops, disp, sz in hits:
    f = q.func_at(a)
    byfn[f[0] if f else 0].append((a, mn, ops, disp))
for fn in sorted(byfn):
    print('FUN_%08X [%d]' % (fn, len(byfn[fn])))
    for a, mn, ops, disp in byfn[fn]:
        print('    %08X %-8s %-44s  (+0x%X)' % (a, mn, ops, disp))
