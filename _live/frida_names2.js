'use strict';
// Now that a level is loaded: re-read GUObjectArray, take live FName indices, and test the
// candidate 64KB name-block addresses we found earlier against the real entry layout
// [u32 hash][u32 len][chars].

const GOA_RVA = 0x4A64250;
const CANDIDATE_BLOCKS = [
  '0x220420f0000', '0x22042120000', '0x22042210000', '0x22042350000',
  '0x220426e0000', '0x22042098000', '0x220420d0000', '0x220420d6000'
];

setImmediate(function () {
  try {
    const mod = Process.findModuleByName('QueensDomain-Win64-Shipping.exe');
    const base = mod.base, modEnd = base.add(mod.size);
    const goa = base.add(GOA_RVA);
    const objects = goa.add(0x10).readPointer();
    const numElements = goa.add(0x24).readS32();
    send({ goa: goa.toString(), numElements });

    // gather live object name indices + object pointers
    const objs = [];
    for (let i = 0; i < numElements && objs.length < 400; i++) {
      try {
        const chunk = objects.add(Math.floor(i / 65536) * 8).readPointer();
        if (chunk.isNull()) continue;
        const obj = chunk.add((i % 65536) * 24).readPointer();
        if (obj.isNull()) continue;
        const vt = obj.readPointer();
        if (vt.isNull() || vt.compare(base) < 0 || vt.compare(modEnd) > 0) continue;
        const ni = obj.add(0x18).readS32();
        const ci = obj.add(0x10).readPointer();
        let cni = -1;
        try { cni = ci.add(0x18).readS32(); } catch (e) { }
        objs.push({ i, o: obj.toString(), ni, cni });
      } catch (e) { }
    }
    const blocks = [...new Set(objs.map(o => o.ni >>> 16))].sort((a, b) => a - b);
    send({ sampled: objs.length, blockIds: blocks, sampleIdx: objs.slice(0, 10).map(o => o.ni) });

    function nameAt(blk, idx) {
      try {
        const e = blk.add(idx & 0xffff);
        const len = e.add(4).readU32();
        if (len < 1 || len > 512) return null;
        const arr = new Uint8Array(e.add(8).readByteArray(len));
        let s = '';
        for (let j = 0; j < len - 1; j++) { const c = arr[j]; if (c < 0x20 || c >= 0x7f) return null; s += String.fromCharCode(c); }
        return s;
      } catch (e2) { return null; }
    }

    const results = [];
    for (const cand of CANDIDATE_BLOCKS) {
      const B = ptr(cand);
      let best = null;
      for (let k = 0; k < 24; k++) {
        const inBlock = objs.filter(o => (o.ni >>> 16) === k);
        if (inBlock.length < 3) continue;
        let names = [], ok = true;
        for (const o of inBlock.slice(0, 8)) {
          const s = nameAt(B, o.ni);
          if (!s) { ok = false; break; }
          names.push(s);
        }
        if (ok && names.length === inBlock.slice(0, 8).length) { best = { k, names }; break; }
      }
      results.push({ block: cand, matched: best });
    }
    send({ results });
  } catch (e) {
    send({ fatal: String(e) });
  }
});
