import sys, struct, marshal, re
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.path.insert(0, r'G:\Freespace\Queens_Domain\analysis\re')
import qdlib as q
import hunt

PKG = 0x039390F8
xrefs = hunt.refs_to(PKG)
funcs = sorted(set(f for s, f, m in xrefs))


def wide(rva, n=120):
    b = q.read_rva(rva, n)
    if not b:
        return None
    try:
        s = b.decode('utf-16-le', 'replace')
    except Exception:
        return None
    s = s.split('\x00')[0]
    return s if s else None


def parse_zc(fn):
    f = q.func_at(fn)
    end = f[1] if f else fn + 0x200
    rec = {'fn': fn, 'name': None, 'r9': None, 'ctor': None,
           'size': None, 'align': None, 'flags': None, 'cfg': None,
           'super': None, 'arg14': None, 'retclass': None}
    last_lea = None
    for ins in q.disasm(fn, end_rva=end):
        t = q.insn_target(ins)
        if t is not None:
            last_lea = t
            if ins.mnemonic == 'lea':
                if ins.op_str.startswith('rdx'):
                    nm = wide(t)
                    if nm and rec['name'] is None:
                        rec['name'] = nm
                if ins.op_str.startswith('r9'):
                    rec['r9'] = t
                if ins.op_str.startswith('r8'):
                    rec['retclass'] = t
                if ins.op_str.startswith('rcx'):
                    pass
        if ins.mnemonic == 'mov' and ins.op_str.startswith('qword ptr [r11'):
            off = int(ins.op_str.split('r11 - 0x')[1].split(']')[0], 16)
            if ins.op_str.endswith('rax') and last_lea is not None:
                if off == 0x40:
                    rec['ctor'] = last_lea
                elif off == 0x48:
                    rec['cfg'] = last_lea
                elif off == 0x28:
                    rec['super'] = last_lea
                elif off == 0x20:
                    rec['arg14'] = last_lea
        if ins.mnemonic == 'mov' and 'dword ptr [rsp + 0x20]' in ins.op_str:
            try:
                rec['size'] = int(ins.op_str.split(',')[-1], 16)
            except Exception:
                pass
        if ins.mnemonic == 'mov' and 'dword ptr [rsp + 0x28]' in ins.op_str:
            try:
                rec['align'] = int(ins.op_str.split(',')[-1], 16)
            except Exception:
                pass
        if ins.mnemonic == 'mov' and 'dword ptr [rsp + 0x30]' in ins.op_str:
            try:
                rec['flags'] = int(ins.op_str.split(',')[-1], 16)
            except Exception:
                pass
    return rec


recs = []
for f in funcs:
    try:
        r = parse_zc(f)
    except Exception as e:
        continue
    if r['name']:
        recs.append(r)
print('parsed Z_Construct records:', len(recs))
for r in sorted(recs, key=lambda x: x['name']):
    print('%-40s fn=%08X size=%-6s ctor=%s r9=%s super=%s'
          % (r['name'], r['fn'], hex(r['size']) if r['size'] else '?',
             ('%08X' % r['ctor']) if r['ctor'] else '?',
             ('%08X' % r['r9']) if r['r9'] else '?',
             ('%08X' % r['super']) if r['super'] else '?'))

import json
json.dump(recs, open(r'G:\Freespace\Queens_Domain\analysis\scratch\game_classes.json', 'w'), indent=1)
