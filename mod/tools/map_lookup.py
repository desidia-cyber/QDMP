"""Resolve an RVA (module base offset) to a symbol in an MSVC .map file.

Why this exists: the soak crash is reported as `WINMM+0x3b357`, which is an RVA,
and the module is a stripped release DLL, so the only symbol the process carries
is whatever the linker emitted.  build.bat already passes /MAP, so the answer is
on disk; what was missing was a way to read it without opening Visual Studio.

The map's Address column is `section_number:offset` where the "section number" is
a 1-based index into the PE section table, NOT a section name -- reading it as a
name silently matches nothing.  Each row also carries Rva+Base, which already is
the preferred-base address, so `rva = Rva+Base - image_base`.  Symbol lengths are
not in the map, so a symbol "owns" an address up to the next one in its section --
the same rule a debugger uses when a PDB has no line info.

Usage:
    python tools/map_lookup.py native/build/winmm.dll native/build/winmm.map 0x3b357 0x6f4d7
    python tools/map_lookup.py --near ...        # show the 3 symbols either side
"""

import bisect
import struct
import sys


def read_sections(pe_path):
    """[(name, virtual_address, virtual_size)] from the PE section table."""
    with open(pe_path, 'rb') as f:
        data = f.read()
    if data[:2] != b'MZ':
        raise SystemExit('%s is not an MZ image' % pe_path)
    e_lfanew = struct.unpack_from('<I', data, 0x3C)[0]
    if data[e_lfanew:e_lfanew + 4] != b'PE\0\0':
        raise SystemExit('%s has no PE header' % pe_path)
    coff = e_lfanew + 4
    n_sections = struct.unpack_from('<H', data, coff + 2)[0]
    opt_size = struct.unpack_from('<H', data, coff + 16)[0]
    sect_table = coff + 20 + opt_size
    out = []
    for i in range(n_sections):
        off = sect_table + i * 40
        raw = data[off:off + 8].split(b'\0', 1)[0].decode('ascii', 'replace')
        virt_size, virt_addr = struct.unpack_from('<II', data, off + 8)
        out.append((raw, virt_addr, virt_size))
    return out


def read_image_base(pe_path):
    """Preferred load address from the PE optional header."""
    with open(pe_path, 'rb') as f:
        data = f.read()
    e_lfanew = struct.unpack_from('<I', data, 0x3C)[0]
    opt = e_lfanew + 4 + 20
    magic = struct.unpack_from('<H', data, opt)[0]
    if magic == 0x20B:
        return struct.unpack_from('<Q', data, opt + 24)[0]
    if magic == 0x10B:
        return struct.unpack_from('<I', data, opt + 28)[0]
    raise SystemExit('%s has an unknown optional-header magic 0x%x' % (pe_path, magic))


def read_map(map_path):
    """[(section_number, offset, symbol)] from the public-symbol block of an MSVC map."""
    rows = []
    with open(map_path, 'r', errors='replace') as f:
        for line in f:
            # e.g. " 0001:000003a0  ?hookInit@@YAXXZ        1800003a0 f   proxy.obj"
            parts = line.split()
            if len(parts) < 3 or ':' not in parts[0]:
                continue
            head = parts[0].split(':')
            if len(head) != 2 or not head[1]:
                continue
            try:
                off = int(head[1], 16)
            except ValueError:
                continue
            rows.append((head[0], off, parts[1]))
    return rows


def build_index(pe_path, map_path):
    """[(rva, symbol)] sorted, from Rva+Base minus the preferred image base."""
    base = read_image_base(pe_path)
    known = {'%04X' % (i + 1) for i in range(len(read_sections(pe_path)))}
    index = []
    with open(map_path, 'r', errors='replace') as f:
        for line in f:
            parts = line.split()
            if len(parts) < 3 or ':' not in parts[0]:
                continue
            head = parts[0].split(':')
            if len(head) != 2 or head[0] not in known:
                continue
            try:
                rva_base = int(parts[2], 16)
            except ValueError:
                continue
            if rva_base < base:
                continue  # <absolute>, or a section-relative address
            index.append((rva_base - base, parts[1]))
    index.sort()
    return index


def lookup(index, rva, near=False):
    if not index:
        return []
    keys = [a for a, _ in index]
    i = bisect.bisect_right(keys, rva) - 1
    if not near:
        if i < 0:
            return []
        return [(index[i][0], index[i][1], rva - index[i][0])]
    lo = max(0, i - 3)
    return [(addr, sym, rva - addr) for addr, sym in index[lo:i + 4]]


def main(argv):
    if len(argv) < 4:
        print(__doc__)
        return 2
    near = '--near' in argv
    argv = [a for a in argv if a != '--near']
    pe_path, map_path = argv[1], argv[2]
    index = build_index(pe_path, map_path)
    print('symbols: %d' % len(index))
    rc = 0
    for raw in argv[3:]:
        try:
            rva = int(raw, 0)
        except ValueError:
            print('%s: not a number' % raw)
            rc = 1
            continue
        hits = lookup(index, rva, near)
        if not hits:
            print('0x%x: no symbol at or below this address' % rva)
            rc = 1
            continue
        if not near:
            addr, sym, delta = hits[0]
            print('0x%-8x -> %s + 0x%x' % (rva, sym, delta))
        else:
            print('0x%-8x near:' % rva)
            for addr, sym, delta in hits:
                print('    %s + 0x%x   (0x%x)' % (sym, delta, addr))
    return rc


if __name__ == '__main__':
    sys.exit(main(sys.argv))
