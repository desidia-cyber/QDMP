import sys, time, collections
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.path.insert(0, r'G:\Freespace\Queens_Domain\analysis\re')
import qdlib as q
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_REG_RIP

d = q.data()
name, va, vsz, raw, rawsz, ch = q.sec_by_name('.text')
code = d[raw:raw + rawsz]
print('text rva %08X size %08X' % (va, rawsz))

md = Cs(CS_ARCH_X86, CS_MODE_64)
md.detail = False
md2 = Cs(CS_ARCH_X86, CS_MODE_64)
md2.detail = True

WANT = {'mov', 'and', 'or', 'test', 'cmp', 'xor', 'add', 'sub', 'bts', 'btr',
        'btc', 'xchg', 'not', 'inc', 'dec', 'setne', 'sete', 'setb', 'seta',
        'shl', 'shr', 'sar', 'movzx', 'movsx'}
t0 = time.time()
cands = []
CH = 8192
pos = 0
n = len(code)
tot = 0
while pos < n:
    chunk = code[pos:pos + CH]
    consumed = 0
    for ins in md.disasm(chunk, va + pos):
        tot += 1
        consumed = ins.address - va + ins.size - pos
        if '0x5b]' in ins.op_str and ins.mnemonic in WANT:
            cands.append(ins.address)
    if consumed <= 0:
        pos += 1
    else:
        pos += consumed
print('insns %d, candidate touches %d, %.1fs' % (tot, len(cands), time.time() - t0))

# validate with detail
hits = []
for a in sorted(set(cands)):
    off = q.rva_to_off(a)
    for ins in md2.disasm(d[off:off + 15], a):
        ok = False
        for op in ins.operands:
            if op.type == X86_OP_MEM and op.mem.disp == 0x5b and op.size == 1 \
               and op.mem.base not in (0, X86_REG_RIP):
                ok = True
        if ok:
            hits.append((a, ins.mnemonic, ins.op_str))
        break
print('validated byte-[reg+0x5b] touches: %d' % len(hits))
byfn = collections.defaultdict(list)
for a, mn, ops in hits:
    f = q.func_at(a)
    byfn[f[0] if f else 0].append((a, mn, ops))
print('distinct functions:', len(byfn))
for fn in sorted(byfn):
    print('FUN_%08X  [%d]' % (fn, len(byfn[fn])))
    for a, mn, ops in byfn[fn]:
        print('    %08X %-8s %s' % (a, mn, ops))
