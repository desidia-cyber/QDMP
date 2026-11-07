'use strict';
// Diagnose exactly where FName resolution breaks, per block.

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
      const blkIdx = idx >>> 16, off = idx & 0xffff;
      try {
        const blk = blocks.add(blkIdx * 8).readPointer();
        if (blk.isNull()) return { err: 'nullblock', blkIdx };
        const e = blk.add(off);
        const h = e.readU16();
        if (h & 1) return { err: 'wide', blkIdx, off, h };
        const len = h >> 6;
        if (len < 1 || len > 1024) return { err: 'badlen', blkIdx, off, h, len };
        const arr = new Uint8Array(e.add(2).readByteArray(len));
        let s = '';
        for (let j = 0; j < len; j++) {
          const c = arr[j];
          if (c < 0x20 || c >= 0x7f) return { err: 'nonascii', blkIdx, off, h, len, first: arr[0] };
          s += String.fromCharCode(c);
        }
        return { ok: s, blkIdx };
      } catch (e2) { return { err: 'exc', blkIdx, off, msg: String(e2) }; }
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

    // --- sample 20 objects with full detail ---
    const detail = [];
    for (let i = 0, seen = 0; i < numElements && seen < 20; i++) {
      const o = objAt(i);
      if (!o) continue;
      seen++;
      const ni = o.add(0x18).readS32();
      const nr = fname(ni);
      const cls = o.add(0x10).readPointer();
      let cni = null, cr = null;
      if (cls && !cls.isNull()) {
        try { cni = cls.add(0x18).readS32(); cr = fname(cni); } catch (e) { }
      }
      detail.push({
        i, ni, name: nr.ok || nr.err,
        cls: cls ? cls.toString() : null,
        cni, clsname: cr ? (cr.ok || cr.err) : null
      });
    }
    send({ detail });

    // --- resolve stats per block over a prefix of the array ---
    const perBlock = {};
    const N = Math.min(numElements, 120000);
    for (let i = 0; i < N; i++) {
      const o = objAt(i);
      if (!o) continue;
      const ni = o.add(0x18).readS32();
      const b = ni >>> 16;
      const r = fname(ni);
      if (!perBlock[b]) perBlock[b] = { ok: 0, wide: 0, badlen: 0, nonascii: 0, nullblock: 0, exc: 0 };
      const k = r.ok ? 'ok' : r.err;
      perBlock[b][k] = (perBlock[b][k] || 0) + 1;
    }
    send({ perBlock });
  } catch (e) { send({ fatal: String(e) }); }
});
