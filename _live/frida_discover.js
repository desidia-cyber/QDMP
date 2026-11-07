'use strict';
// Structural scan, no layout assumptions:
//   candidate P -> P[0] (chunk) -> chunk[0] (FUObjectItem) -> item.Object -> vtable in module
// Also finds the FName block array by walking several entries in a block.

setImmediate(function () {
  try {
    const mod = Process.findModuleByName('QueensDomain-Win64-Shipping.exe');
    const base = mod.base, modEnd = base.add(mod.size);
    const e_lfanew = base.add(0x3C).readU32();
    const nt = base.add(e_lfanew);
    const nsec = nt.add(6).readU16();
    const optSize = nt.add(20).readU16();
    const secOff = nt.add(24).add(optSize);
    let dataSec = null;
    for (let i = 0; i < nsec; i++) {
      const s = secOff.add(i * 40);
      let name = '';
      for (let k = 0; k < 8; k++) { const c = s.add(k).readU8(); if (c === 0) break; name += String.fromCharCode(c); }
      if (name === '.data') dataSec = { va: s.add(12).readU32(), vsize: s.add(8).readU32() };
    }
    const d0 = base.add(dataSec.va), span = dataSec.vsize;
    const dv = new DataView(Memory.readByteArray(d0, span));
    send({ dataVa: dataSec.va.toString(16), span });

    function inMod(p) { return !p.isNull() && p.compare(base) >= 0 && p.compare(modEnd) < 0; }
    function heapAt(off) {
      const lo = dv.getUint32(off, true), hi = dv.getUint32(off + 4, true);
      if (lo === 0 && hi === 0) return null;
      if (hi > 0xffff) return null;
      return ptr(lo).add(ptr(hi).shl(32));
    }

    // ---------- GUObjectArray by structure ----------
    const hits = [];
    let tested = 0;
    for (let off = 0; off + 0x30 <= span && hits.length < 5; off += 8) {
      const P = heapAt(off);
      if (!P) continue;
      // cheap gate: MaxElements at (array+0x20) = (off-0x10+0x20) = off+0x10 must be sane
      const maxE = dv.getInt32(off + 0x10, true);
      const numE = dv.getInt32(off + 0x14, true);
      if (maxE < 1000 || maxE > 8000000) continue;
      if (numE < 1000 || numE > maxE) continue;
      tested++;
      try {
        const chunk0 = P.readPointer();
        if (chunk0.isNull()) continue;
        const obj0 = chunk0.readPointer();
        if (obj0.isNull()) continue;
        const vt = obj0.readPointer();
        if (!inMod(vt)) continue;
        const cls = obj0.add(0x10).readPointer();
        if (cls.isNull()) continue;
        const clsVt = cls.readPointer();
        if (!inMod(clsVt)) continue;
        const maybeOuter = obj0.add(0x20).readPointer();
        hits.push({
          arrayAddr: d0.add(off - 0x10).toString(),
          arrayRva: d0.add(off - 0x10).sub(base).toString(),
          numE, maxE,
          obj0: obj0.toString(), obj0Vt: vt.toString(),
          tested
        });
      } catch (e) { }
    }
    send({ goaHits: hits, tested });

    // ---------- FName blocks ----------
    function walkBlock(b) {
      // try several plausible header offsets; require 6 consecutive plausible entries
      for (const hdr of [0, 4, 8, 12, 16]) {
        let p = b.add(hdr), ok = true, first = null;
        for (let i = 0; i < 6; i++) {
          let h;
          try { h = p.readU16(); } catch (e) { ok = false; break; }
          const wide = h & 1, len = h >> 1;
          if (len < 1 || len > 512) { ok = false; break; }
          if (wide) { ok = false; break; }
          let s = '';
          try {
            const arr = new Uint8Array(p.add(2).readByteArray(len));
            for (let j = 0; j < len; j++) {
              const c = arr[j];
              if (c < 0x20 || c >= 0x7f) { ok = false; break; }
              s += String.fromCharCode(c);
            }
          } catch (e) { ok = false; break; }
          if (!ok) break;
          if (i === 0) first = s;
          let adv = 2 + len;
          if (adv & 1) adv++;
          p = p.add(adv);
        }
        if (ok && first) return first;
      }
      return null;
    }

    const pools = [];
    let ptested = 0;
    for (let off = 0; off + 0x30 <= span && pools.length < 5; off += 8) {
      const lo = dv.getUint32(off, true), hi = dv.getUint32(off + 4, true);
      if (hi !== 0 || lo < 0x10000) continue;
      const p0 = ptr(lo);
      ptested++;
      if (ptested > 400000) break;
      const s0 = walkBlock(p0);
      if (!s0) continue;
      const lo1 = dv.getUint32(off + 8, true);
      if (lo1 < 0x10000) continue;
      const s1 = walkBlock(ptr(lo1));
      if (!s1) continue;
      pools.push({ addr: d0.add(off).toString(), rva: d0.add(off).sub(base).toString(), b0: s0, b1: s1 });
    }
    send({ fnameBlocks: pools, ptested });
  } catch (e) {
    send({ fatal: String(e) });
  }
});
