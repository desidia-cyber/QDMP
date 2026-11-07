'use strict';

const MODNAME = 'QueensDomain-Win64-Shipping.exe';

// .data section from the PE parse
const DATA_RVA = 0x046F1000;
const DATA_VSIZE = 0x00516174;

const mod = Process.findModuleByName(MODNAME);
if (!mod) { send({ err: 'module not found', mods: Process.enumerateModules().filter(m => /Queens/.test(m.name)).map(m => m.name) }); }
else {
  send({ module: mod.name, base: mod.base.toString(), size: mod.size });

  const base = mod.base;
  const dataStart = base.add(DATA_RVA);
  const dataEnd = dataStart.add(DATA_VSIZE);

  function u32(p) { try { return p.readU32(); } catch (e) { return -1; } }
  function s32(p) { try { return p.readS32(); } catch (e) { return -999999999; } }
  function ptr(p) { try { return p.readPointer(); } catch (e) { return ptr('0'); } }

  function checkGoa(cand) {
    try {
      const oo = cand.add(0x10);
      const objects = ptr(oo);
      if (objects.isNull()) return null;
      const maxE = s32(oo.add(0x10));
      const numE = s32(oo.add(0x14));
      const maxC = s32(oo.add(0x18));
      const numC = s32(oo.add(0x1C));
      if (numE < 20000 || numE > 3000000) return null;
      if (maxE < numE || maxE > 8000000) return null;
      if (numC < 1 || numC > 128) return null;
      if (maxC < numC) return null;
      // chunk count must match MaxElements / 64K
      const expectC = Math.ceil(maxE / 65536);
      if (Math.abs(expectC - numC) > 1) return null;
      // first chunk must be mapped and hold a live-looking object
      const item0 = ptr(objects);
      if (item0.isNull()) return null;
      const obj0 = ptr(item0);
      if (obj0.isNull()) return null;
      const vt = ptr(obj0);
      if (vt.isNull()) return null;
      if (vt.compare(base) < 0) return null;
      if (vt.compare(base.add(mod.size)) > 0) return null;
      return { numE: numE, maxE: maxE, numC: numC, maxC: maxC };
    } catch (e) { return null; }
  }

  let found = [];
  for (let a = dataStart; a.compare(dataEnd) < 0; a = a.add(8)) {
    const r = checkGoa(a);
    if (r) {
      found.push({ addr: a.toString(), rva: a.sub(base).toString(), ...r });
      if (found.length >= 6) break;
    }
  }
  send({ goaCandidates: found });
}
