'use strict';
// Sweep the FName block-address band in 64KB steps and find the base that resolves the
// live indices we actually observed. RVA 0x4A64250 is GUObjectArray (stable across states).

const GOA_RVA = 0x4A64250;
const BAND_LO = 0x22040000000n, BAND_HI = 0x22048000000n, STEP = 0x10000n;

setImmediate(function () {
  try {
    const mod = Process.findModuleByName('QueensDomain-Win64-Shipping.exe');
    const base = mod.base, modEnd = base.add(mod.size);
    const goa = base.add(GOA_RVA);
    const objects = goa.add(0x10).readPointer();
    const numElements = goa.add(0x24).readS32();

    const objs = [];
    for (let i = 0; i < numElements && objs.length < 600; i++) {
      try {
        const chunk = objects.add(Math.floor(i / 65536) * 8).readPointer();
        if (chunk.isNull()) continue;
        const obj = chunk.add((i % 65536) * 24).readPointer();
        if (obj.isNull()) continue;
        const vt = obj.readPointer();
        if (vt.isNull() || vt.compare(base) < 0 || vt.compare(modEnd) > 0) continue;
        objs.push({ o: obj, ni: obj.add(0x18).readS32() });
      } catch (e) { }
    }
    send({ sampled: objs.length });

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

    // pick the most common block id
    const hist = {};
    for (const o of objs) { const k = o.ni >>> 16; hist[k] = (hist[k] || 0) + 1; }
    const topK = Object.keys(hist).sort((a, b) => hist[b] - hist[a])[0];
    const testIdx = objs.filter(o => (o.ni >>> 16) === +topK).slice(0, 6).map(o => o.ni);
    send({ dominantBlock: topK, testIdx });

    const matches = [];
    let tested = 0;
    for (let a = BAND_LO; a < BAND_HI && matches.length < 4; a += STEP) {
      const B = ptr(a.toString());
      // cheap gate: does it decode at all?
      const first = nameAt(B, testIdx[0]);
      if (!first) continue;
      tested++;
      const names = [first];
      let ok = true;
      for (let i = 1; i < testIdx.length; i++) {
        const s = nameAt(B, testIdx[i]);
        if (!s) { ok = false; break; }
        names.push(s);
      }
      if (ok) matches.push({ base: B.toString(), names });
    }
    send({ matches, tested });
  } catch (e) {
    send({ fatal: String(e) });
  }
});
