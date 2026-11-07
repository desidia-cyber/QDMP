'use strict';
// A5 is solved: entry = Blocks[idx>>16] + 2*(idx & 0xFFFF). Now answer B3/B5/D4/D5.

const GOA_RVA = 0x4A64250, POOL_RVA = 0x4A27F00;

setImmediate(function () {
  try {
    const mod = Process.findModuleByName('QueensDomain-Win64-Shipping.exe');
    const base = mod.base, modEnd = base.add(mod.size);
    const goa = base.add(GOA_RVA);
    const objects = goa.add(0x10).readPointer();
    const numElements = goa.add(0x24).readS32();
    const blocks = base.add(POOL_RVA).add(0x10);
    send({ numElements });

    function fname(idx) {
      try {
        const blk = blocks.add((idx >>> 16) * 8).readPointer();
        if (blk.isNull()) return null;
        const e = blk.add((idx & 0xffff) * 2);
        const h = e.readU16();
        const len = h >> 6;
        if (len < 1 || len > 400) return null;
        if (h & 1) {
          const a = new Uint8Array(e.add(2).readByteArray(len * 2));
          let s = '';
          for (let j = 0; j < len; j++) { const c = a[j*2] | (a[j*2+1] << 8); if (c < 0x20) return null; s += String.fromCharCode(c); }
          return s;
        }
        const a = new Uint8Array(e.add(2).readByteArray(len));
        let s = '';
        for (let j = 0; j < len; j++) { const c = a[j]; if (c < 0x20 || c >= 0x7f) return null; s += String.fromCharCode(c); }
        return s;
      } catch (e2) { return null; }
    }

    const CHUNK = 65536;
    function objAt(i) {
      try {
        const chunk = objects.add(Math.floor(i / CHUNK) * 8).readPointer();
        if (chunk.isNull()) return null;
        const o = chunk.add((i % CHUNK) * 24).readPointer();
        if (o.isNull()) return null;
        const vt = o.readPointer();
        if (vt.isNull() || vt.compare(base) < 0 || vt.compare(modEnd) > 0) return null;
        return o;
      } catch (e) { return null; }
    }
    const nameOf = o => { try { return fname(o.add(0x18).readS32()); } catch (e) { return null; } };
    const classOf = o => { try { return o.add(0x10).readPointer(); } catch (e) { return null; } };
    const outerOf = o => { try { return o.add(0x20).readPointer(); } catch (e) { return null; } };
    const clsName = o => { const c = classOf(o); return (c && !c.isNull()) ? nameOf(c) : null; };

    const census = new Map();
    const gameClasses = new Set();
    const pawns = [];
    for (let i = 0; i < numElements; i++) {
      const o = objAt(i);
      if (!o) continue;
      const cn = clsName(o);
      if (!cn) continue;
      census.set(cn, (census.get(cn) || 0) + 1);
      if (/^(A?FS_|U?FS_|QD_|UQD_|AFS|UFS)/.test(cn)) gameClasses.add(cn);
      if (/PlayerCharacter|PlayerController/i.test(cn)) {
        pawns.push({ i, addr: o.toString(), cls: cn, name: nameOf(o) });
      }
    }
    send({ topClasses: [...census.entries()].sort((a,b)=>b[1]-a[1]).slice(0, 25) });
    send({ gameClasses: [...gameClasses].sort().slice(0, 150) });
    send({ playerActors: pawns.slice(0, 20), count: pawns.length });

    // components of the first PlayerController's pawn candidates: list objects whose Outer is one of them
    const candidates = pawns.filter(p => /PlayerCharacter|Character$/i.test(p.cls));
    const comps = [];
    for (const p of candidates.slice(0, 3)) {
      const pa = ptr(p.addr);
      const own = [];
      for (let i = 0; i < numElements; i++) {
        const o = objAt(i);
        if (!o) continue;
        const ou = outerOf(o);
        if (ou && !ou.isNull() && ou.equals(pa)) {
          own.push({ cls: clsName(o), name: nameOf(o) });
        }
      }
      comps.push({ pawn: p, components: own.slice(0, 60), n: own.length });
    }
    send({ pawnComponents: comps });
  } catch (e) { send({ fatal: String(e) }); }
});
