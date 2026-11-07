'use strict';
// Settle FName decoding: try {scale 1, scale 2} x {ANSI, UTF-16} and report which combination
// actually produces printable names for the live object array.

const GOA_RVA = 0x4A64250, POOL_RVA = 0x4A27F00;

setImmediate(function () {
  try {
    const mod = Process.findModuleByName('QueensDomain-Win64-Shipping.exe');
    const base = mod.base, modEnd = base.add(mod.size);
    const goa = base.add(GOA_RVA);
    const objects = goa.add(0x10).readPointer();
    const numElements = goa.add(0x24).readS32();
    const blocks = base.add(POOL_RVA).add(0x10);

    function decode(idx, scale) {
      try {
        const blk = blocks.add((idx >>> 16) * 8).readPointer();
        if (blk.isNull()) return null;
        const e = blk.add((idx & 0xffff) * scale);
        const h = e.readU16();
        const len = h >> 6;
        if (len < 1 || len > 400) return null;
        if (h & 1) {
          const arr = new Uint8Array(e.add(2).readByteArray(len * 2));
          let s = '';
          for (let j = 0; j < len; j++) {
            const c = arr[j*2] | (arr[j*2+1] << 8);
            if (c < 0x20) return null;
            s += String.fromCharCode(c);
          }
          return s;
        } else {
          const arr = new Uint8Array(e.add(2).readByteArray(len));
          let s = '';
          for (let j = 0; j < len; j++) {
            const c = arr[j];
            if (c < 0x20 || c >= 0x7f) return null;
            s += String.fromCharCode(c);
          }
          return s;
        }
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

    const N = Math.min(numElements, 60000);
    for (const scale of [1, 2]) {
      let ok = 0, tot = 0;
      const sample = [];
      for (let i = 0; i < N; i++) {
        const o = objAt(i);
        if (!o) continue;
        tot++;
        const ni = o.add(0x18).readS32();
        const s = decode(ni, scale);
        if (s) { ok++; if (sample.length < 12) sample.push(s); }
      }
      send({ scale, ok, tot, sample });
    }

    // with the winning scale, also resolve class names
    for (const scale of [1, 2]) {
      let ok = 0, tot = 0;
      const classes = [];
      for (let i = 0; i < Math.min(numElements, 20000); i++) {
        const o = objAt(i);
        if (!o) continue;
        const cls = o.add(0x10).readPointer();
        if (!cls || cls.isNull()) continue;
        tot++;
        let cni;
        try { cni = cls.add(0x18).readS32(); } catch (e) { continue; }
        const s = decode(cni, scale);
        if (s) { ok++; if (classes.length < 12) classes.push(s); }
      }
      send({ scale, classOk: ok, classTot: tot, classes });
    }
  } catch (e) { send({ fatal: String(e) }); }
});
