'use strict';
// We know a real FName block base. Search memory for that pointer value -> that finds the
// block table (Blocks[]). Then map index -> block and resolve names for live UObjects.

const GOA_RVA = 0x4A64250;
const KNOWN_BLOCK = ptr('0x220420f0000');   // from the "None" heap scan

setImmediate(function () {
  try {
    const mod = Process.findModuleByName('QueensDomain-Win64-Shipping.exe');
    const base = mod.base, modEnd = base.add(mod.size);

    // little-endian bytes of the block address
    const le = [];
    const hex = KNOWN_BLOCK.toString(16).replace(/^0x/, '').padStart(16, '0');
    const be = [];
    for (let i = 0; i < 16; i += 2) be.push(hex.substr(i, 2));
    for (let i = be.length - 1; i >= 0; i--) le.push(parseInt(be[i], 16));
    const pat = le.map(b => b.toString(16).padStart(2, '0')).join(' ');
    send({ searchingFor: KNOWN_BLOCK.toString(), pattern: pat });

    const ranges = Process.enumerateRanges('r--');
    const hits = [];
    for (const r of ranges) {
      if (r.size < 4096 || r.size > 0x20000000) continue;
      let m;
      try { m = Memory.scanSync(r.base, r.size, pat); } catch (e) { continue; }
      for (const x of m) hits.push(x.address);
      if (hits.length >= 12) break;
    }
    send({ blockPtrOccurrences: hits.map(h => h.toString()) });
    if (hits.length === 0) return;

    // assume the table is a run of block pointers; from the first hit, walk left/right
    const h = hits[0];
    let start = h;
    function looksLikePtr(p) {
      try {
        const q = p.readPointer();
        if (q.isNull()) return false;
        const hi = q.shr(32).toUInt32();
        return hi > 0 && hi <= 0xffff;
      } catch (e) { return false; }
    }
    while (looksLikePtr(start.sub(8))) start = start.sub(8);
    let end = h;
    while (looksLikePtr(end.add(8))) end = end.add(8);
    const count = end.sub(start).toInt32() / 8 + 1;
    send({ tableStart: start.toString(), tableRva: start.sub(base).toString(), count });

    // resolve a name for a known index
    function nameAt(blk, idx) {
      try {
        const e = blk.add(idx & 0xffff);
        const len = e.add(4).readU32();          // layout: [u32 hash][u32 len][chars]
        if (len < 1 || len > 512) return null;
        const arr = new Uint8Array(e.add(8).readByteArray(len));
        let s = '';
        for (let j = 0; j < len - 1; j++) { const c = arr[j]; if (c < 0x20 || c >= 0x7f) return null; s += String.fromCharCode(c); }
        return s;
      } catch (e2) { return null; }
    }

    // live object name indices
    const goa = base.add(GOA_RVA);
    const objects = goa.add(0x10).readPointer();
    const numElements = goa.add(0x24).readS32();
    const idxs = [];
    for (let i = 0; i < numElements && idxs.length < 40; i++) {
      try {
        const chunk = objects.add(Math.floor(i / 65536) * 8).readPointer();
        if (chunk.isNull()) continue;
        const obj = chunk.add((i % 65536) * 24).readPointer();
        if (obj.isNull()) continue;
        const vt = obj.readPointer();
        if (vt.isNull() || vt.compare(base) < 0 || vt.compare(modEnd) > 0) continue;
        const ni = obj.add(0x18).readS32();
        if (ni > 0) idxs.push(ni);
      } catch (e) { }
    }

    // find the offset within the table where index>>16 resolves
    const trials = [];
    for (let k = 0; k < Math.min(count, 64); k++) {
      const blk = start.add(k * 8).readPointer();
      if (blk.isNull()) continue;
      const inBlock = idxs.filter(n => (n >>> 16) === k);
      if (inBlock.length < 2) continue;
      let names = [], ok = true;
      for (const n of inBlock.slice(0, 6)) {
        const s = nameAt(blk, n);
        if (!s) { ok = false; break; }
        names.push(s);
      }
      if (ok && names.length) trials.push({ blockIndex: k, block: blk.toString(), names });
    }
    send({ trials });
  } catch (e) {
    send({ fatal: String(e) });
  }
});

