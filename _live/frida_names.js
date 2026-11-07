'use strict';
// The FName block table is a RUN of consecutive heap pointers in .data. Find runs first
// (cheap, from the snapshot), then test only those against real FName indices.

const GOA_RVA = 0x4A64250;

setImmediate(function () {
  try {
    const mod = Process.findModuleByName('QueensDomain-Win64-Shipping.exe');
    const base = mod.base, modEnd = base.add(mod.size);
    const goa = base.add(GOA_RVA);
    const objects = goa.add(0x10).readPointer();
    const numElements = goa.add(0x24).readS32();

    const idxs = [];
    const CHUNK = 65536;
    for (let i = 0; i < numElements && idxs.length < 60; i++) {
      try {
        const chunk = objects.add(Math.floor(i / CHUNK) * 8).readPointer();
        if (chunk.isNull()) continue;
        const obj = chunk.add((i % CHUNK) * 24).readPointer();
        if (obj.isNull()) continue;
        const vt = obj.readPointer();
        if (vt.isNull() || vt.compare(base) < 0 || vt.compare(modEnd) > 0) continue;
        const ni = obj.add(0x18).readS32();
        if (ni > 0) idxs.push(ni);
      } catch (e) { }
    }
    const uniq = [...new Set(idxs)];
    send({ uniqCount: uniq.length, sample: uniq.slice(0, 20) });
    if (uniq.length < 5) return;

    const e_lfanew = base.add(0x3C).readU32();
    const nt = base.add(e_lfanew);
    const nsec = nt.add(6).readU16();
    const optSize = nt.add(20).readU16();
    const secOff = nt.add(24).add(optSize);
    let dataSec = null;
    for (let i = 0; i < nsec; i++) {
      const s = secOff.add(i * 40);
      let nm = '';
      for (let k = 0; k < 8; k++) { const c = s.add(k).readU8(); if (c === 0) break; nm += String.fromCharCode(c); }
      if (nm === '.data') dataSec = { va: s.add(12).readU32(), vsize: s.add(8).readU32() };
    }
    const d0 = base.add(dataSec.va), span = dataSec.vsize;
    const dv = new DataView(Memory.readByteArray(d0, span));

    function isHeap(off) {
      const lo = dv.getUint32(off, true), hi = dv.getUint32(off + 4, true);
      if (lo === 0 && hi === 0) return false;
      if (hi > 0xffff) return false;
      return true;
    }

    // collect run starts with >= 6 consecutive heap pointers
    const runs = [];
    for (let off = 0; off + 0x40 <= span; off += 8) {
      if (!isHeap(off)) continue;
      if (isHeap(off - 8)) continue;          // only record the start of a run
      let n = 0;
      while (off + n * 8 + 8 <= span && isHeap(off + n * 8)) n++;
      if (n >= 6) runs.push({ off, n });
    }
    send({ runs: runs.slice(0, 25), totalRuns: runs.length });

    function ptrAt(off) {
      const lo = dv.getUint32(off, true), hi = dv.getUint32(off + 4, true);
      return ptr(lo).add(ptr(hi).shl(32));
    }

    function nameAt(blk, idx, nameOff) {
      try {
        const e = blk.add(idx & 0xffff);
        const h = e.readU16();
        const wide = h & 1, len = h >> 1;
        if (len < 1 || len > 256 || wide) return null;
        const arr = new Uint8Array(e.add(nameOff).readByteArray(len));
        let s = '';
        for (let j = 0; j < len; j++) { const c = arr[j]; if (c < 0x20 || c >= 0x7f) return null; s += String.fromCharCode(c); }
        return s;
      } catch (e2) { return null; }
    }

    const results = [];
    for (const r of runs) {
      // test every pointer in the run as block 0..k for the ids that live in that block
      for (let k = 0; k < Math.min(r.n, 64); k++) {
        const P = ptrAt(r.off + k * 8);
        const inBlock = uniq.filter(v => (v >>> 16) === k);
        if (inBlock.length < 3) continue;
        const tests = inBlock.slice(0, 5);
        for (const nameOff of [2, 6, 10, 14]) {
          let names = [], ok = true;
          for (const idx of tests) {
            const s = nameAt(P, idx, nameOff);
            if (!s) { ok = false; break; }
            names.push(s);
          }
          if (ok && names.length === tests.length) {
            results.push({
              tableAddr: d0.add(r.off).toString(),
              tableRva: d0.add(r.off).sub(base).toString(),
              blockIndex: k, nameOff, names, runLen: r.n
            });
            break;
          }
        }
        if (results.length >= 5) break;
      }
      if (results.length >= 5) break;
    }
    send({ found: results });
  } catch (e) {
    send({ fatal: String(e) });
  }
});
