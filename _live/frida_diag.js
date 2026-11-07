'use strict';
// Diagnostic: parse the module's own PE headers at runtime to get true section ranges,
// then report what the .data scan actually sees.

setImmediate(function () {
  try {
    const mod = Process.findModuleByName('QueensDomain-Win64-Shipping.exe');
    const base = mod.base;
    send({ base: base.toString(), size: mod.size });

    // --- PE headers ---
    const e_lfanew = base.add(0x3C).readU32();
    const nt = base.add(e_lfanew);
    const nsec = nt.add(6).readU16();
    const optSize = nt.add(20).readU16();
    const opt = nt.add(24);
    const magic = opt.readU16();
    const secOff = nt.add(24).add(optSize);

    const sections = [];
    for (let i = 0; i < nsec; i++) {
      const s = secOff.add(i * 40);
      let name = '';
      for (let k = 0; k < 8; k++) { const c = s.add(k).readU8(); if (c === 0) break; name += String.fromCharCode(c); }
      const vsize = s.add(8).readU32();
      const va = s.add(12).readU32();
      const rawSize = s.add(16).readU32();
      const rawPtr = s.add(20).readU32();
      sections.push({ name, va, vsize, rawSize, rawPtr });
    }
    send({ magic: magic.toString(16), sections });

    const data = sections.find(s => s.name === '.data');
    const bss = sections.find(s => s.name === '.bss');
    const target = data || sections.find(s => /data/i.test(s.name));
    send({ dataSection: target, bssSection: bss });

    const d0 = base.add(target.va);
    const span = target.vsize;
    const buf = Memory.readByteArray(d0, span);
    const dv = new DataView(buf);

    // sanity: how many bytes look printable in this section?
    let printable = 0;
    for (let i = 0; i < Math.min(span, 200000); i++) {
      const c = dv.getUint8(i);
      if (c >= 0x20 && c < 0x7f) printable++;
    }
    send({ sectionSpan: span, printableSample: printable });

    // --- how many offsets have a plausible NumElements at +0x24 (relaxed, single test) ---
    // FUObjectArray.ObjObjects is at +0x10; NumElements at +0x24, MaxElements at +0x20.
    let hits = [];
    let best = -1, bestAt = -1;
    for (let off = 0; off + 0x30 <= span; off += 8) {
      const numE = dv.getInt32(off + 0x24, true);
      if (numE > best) { best = numE; bestAt = off; }
      if (numE < 1000 || numE > 4000000) continue;
      const maxE = dv.getInt32(off + 0x20, true);
      if (maxE < numE || maxE > 8000000) continue;
      hits.push({ off, numE, maxE,
                  numC: dv.getInt32(off + 0x2C, true),
                  maxC: dv.getInt32(off + 0x28, true) });
      if (hits.length > 12) break;
    }
    send({ relaxedGoaHits: hits, maxNumESeen: best, maxNumEAt: bestAt });

    // --- pointer histogram: what do heap pointers look like here? ---
    const samples = [];
    for (let off = 0; off + 8 <= span && samples.length < 12; off += 8) {
      const lo = dv.getUint32(off, true), hi = dv.getUint32(off + 4, true);
      if (hi === 0 && lo > 0x10000 && lo < 0x7fffffff) samples.push({ off, v: '0x' + lo.toString(16) });
    }
    send({ lowPtrSamples: samples });
  } catch (e) {
    send({ fatal: String(e) });
  }
});
