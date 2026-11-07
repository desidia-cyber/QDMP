'use strict';
// Enumerate live UObjects and resolve their FName, using the discovered GUObjectArray.

const GOA_RVA = 0x4A64250;

setImmediate(function () {
  try {
    const mod = Process.findModuleByName('QueensDomain-Win64-Shipping.exe');
    const base = mod.base, modEnd = base.add(mod.size);
    const goa = base.add(GOA_RVA);
    const objects = goa.add(0x10).readPointer();
    const numElements = goa.add(0x24).readS32();
    const maxElements = goa.add(0x20).readS32();
    send({ goa: goa.toString(), numElements, maxElements });

    // ---- locate FName blocks: they live in .data; accept full user pointer range ----
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

    function walkBlock(b) {
      for (const hdr of [0, 2, 4, 8, 12, 16, 24]) {
        let p = b.add(hdr), ok = true, first = null;
        for (let i = 0; i < 8; i++) {
          let h;
          try { h = p.readU16(); } catch (e) { ok = false; break; }
          const wide = h & 1, len = h >> 1;
          if (len < 1 || len > 512 || wide) { ok = false; break; }
          let s = '';
          try {
            const arr = new Uint8Array(p.add(2).readByteArray(len));
            for (let j = 0; j < len; j++) { const c = arr[j]; if (c < 0x20 || c >= 0x7f) { ok = false; break; } s += String.fromCharCode(c); }
          } catch (e) { ok = false; break; }
          if (!ok) break;
          if (i === 0) first = s;
          let adv = 2 + len; if (adv & 1) adv++;
          p = p.add(adv);
        }
        if (ok && first) return first;
      }
      return null;
    }

    let blocksAddr = null, samples = [];
    let scanned = 0;
    for (let off = 0; off + 0x40 <= span; off += 8) {
      const lo = dv.getUint32(off, true), hi = dv.getUint32(off + 4, true);
      if (hi > 0xffff || (lo === 0 && hi === 0)) continue;
      const p0 = ptr(lo).add(ptr(hi).shl(32));
      scanned++;
      if (scanned > 300000) break;
      const s0 = walkBlock(p0);
      if (!s0) continue;
      const lo1 = dv.getUint32(off + 8, true), hi1 = dv.getUint32(off + 12, true);
      if (hi1 > 0xffff) continue;
      const s1 = walkBlock(ptr(lo1).add(ptr(hi1).shl(32)));
      if (!s1) continue;
      blocksAddr = d0.add(off); samples = [s0, s1];
      break;
    }
    send({ fnameBlocks: blocksAddr ? blocksAddr.toString() : null, samples });
    if (!blocksAddr) return;

    function fname(idx) {
      try {
        const blk = blocksAddr.add((idx >>> 16) * 8).readPointer();
        if (blk.isNull()) return null;
        const e = blk.add(idx & 0xffff);
        const h = e.readU16();
        const wide = h & 1, len = h >> 1;
        if (len === 0 || len > 512) return null;
        if (wide) return null;
        const arr = new Uint8Array(e.add(2).readByteArray(len));
        let s = ''; for (let j = 0; j < len; j++) s += String.fromCharCode(arr[j]);
        return s;
      } catch (e2) { return null; }
    }

    // ---- enumerate objects ----
    const classCount = new Map();
    const interesting = [];
    let resolved = 0;
    const CHUNK = 65536;
    for (let i = 0; i < numElements; i++) {
      let item, obj;
      try {
        const chunk = objects.add(Math.floor(i / CHUNK) * 8).readPointer();
        if (chunk.isNull()) continue;
        item = chunk.add((i % CHUNK) * 24);
        obj = item.readPointer();
        if (obj.isNull()) continue;
      } catch (e) { continue; }
      let cls, nameIdx, nm;
      try {
        cls = obj.add(0x10).readPointer();
        nameIdx = obj.add(0x18).readS32();
        nm = fname(nameIdx);
      } catch (e) { continue; }
      if (!nm) continue;
      resolved++;
      let clsName = null;
      try { clsName = fname(cls.add(0x18).readS32()); } catch (e) { }
      if (clsName) classCount.set(clsName, (classCount.get(clsName) || 0) + 1);
      if (/Player|Pawn|Controller|Mesh|World|Level|Character|Save|Quest|Dialogue|Enemy/i.test(nm) ||
          (clsName && /Player|Pawn|Controller|Character/i.test(clsName))) {
        if (interesting.length < 120) {
          interesting.push({ i, obj: obj.toString(), name: nm, class: clsName });
        }
      }
    }
    send({ resolved, interesting });

    const top = [...classCount.entries()].sort((a, b) => b[1] - a[1]).slice(0, 40);
    send({ topClasses: top });
  } catch (e) {
    send({ fatal: String(e), stack: (e && e.stack) ? String(e.stack) : null });
  }
});
