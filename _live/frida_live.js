'use strict';
// Resolve FName via the FNamePool the disassembly gave us, then answer the open questions:
//   - what classes are live, and the game's own class names
//   - which actors own skeletal meshes (the FP rig vs the full body)
//   - what object pointers live inside the player pawn

const GOA_RVA   = 0x4A64250;
const POOL_RVA  = 0x4A27F00;   // FNamePool object
const BLOCKS_OFF = 0x10;       // Blocks[] inside FNamePool

setImmediate(function () {
  try {
    const mod = Process.findModuleByName('QueensDomain-Win64-Shipping.exe');
    const base = mod.base, modEnd = base.add(mod.size);
    const goa = base.add(GOA_RVA);
    const objects = goa.add(0x10).readPointer();
    const numElements = goa.add(0x24).readS32();
    send({ base: base.toString(), numElements });

    const blocks = base.add(POOL_RVA).add(BLOCKS_OFF);

    function fname(idx) {
      try {
        const blk = blocks.add((idx >>> 16) * 8).readPointer();
        if (blk.isNull()) return null;
        const e = blk.add(idx & 0xffff);
        const h = e.readU16();
        const wide = h & 1;
        const len = h >> 6;
        if (wide || len < 1 || len > 1024) return null;
        const arr = new Uint8Array(e.add(2).readByteArray(len));
        let s = '';
        for (let j = 0; j < len; j++) {
          const c = arr[j];
          if (c < 0x20 || c >= 0x7f) return null;
          s += String.fromCharCode(c);
        }
        return s;
      } catch (e2) { return null; }
    }

    // sanity: the pool should resolve a known index to a sane identifier
    const probe = [];
    for (let i = 1; i < 24; i++) { const s = fname(i); if (s) probe.push(i + '=' + s); }
    send({ probeNames: probe });

    if (probe.length === 0) { send({ err: 'FName pool did not resolve' }); return; }

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
    function nameOf(o) { try { return fname(o.add(0x18).readS32()); } catch (e) { return null; } }
    function classOf(o) { try { return o.add(0x10).readPointer(); } catch (e) { return null; } }
    function outerOf(o) { try { return o.add(0x20).readPointer(); } catch (e) { return null; } }
    function className(o) {
      const c = classOf(o);
      if (!c || c.isNull()) return null;
      return nameOf(c);
    }

    // ---- pass 1: class census + collect actors that own skeletal meshes ----
    const census = new Map();
    const gameClasses = new Set();
    const actors = [];              // objects whose class name looks like a pawn/character/controller
    for (let i = 0; i < numElements; i++) {
      const o = objAt(i);
      if (!o) continue;
      const cn = className(o);
      if (!cn) continue;
      census.set(cn, (census.get(cn) || 0) + 1);
      if (/^(A?FS_|U?FS_|QD_|AFS|UFS)/.test(cn)) gameClasses.add(cn);
      if (/PlayerCharacter|PlayerController|Character$|Pawn$|FS_Player|FS_Enemy/.test(cn)) {
        actors.push({ addr: o.toString(), cls: cn, name: nameOf(o) });
      }
    }
    send({ totalClasses: census.size, gameClasses: [...gameClasses].slice(0, 120) });
    send({ actors: actors.slice(0, 40), actorCount: actors.length });

    const top = [...census.entries()].sort((a, b) => b[1] - a[1]).slice(0, 30);
    send({ topClasses: top });

    // ---- pass 2: skeletal mesh components, and who owns them ----
    const smcs = [];
    const occupants = [];
    for (let i = 0; i < numElements; i++) {
      const o = objAt(i);
      if (!o) continue;
      if (className(o) !== 'SkeletalMeshComponent') continue;
      const own = outerOf(o);                 // component's Outer is the actor
      const ownCls = own && !own.isNull() ? className(own) : null;
      const ownName = own && !own.isNull() ? nameOf(own) : null;
      occupants.push({ comp: nameOf(o), owner: ownName, ownerClass: ownCls });
    }
    send({ skeletalMeshComponents: occupants.slice(0, 60), count: occupants.length });
  } catch (e) {
    send({ fatal: String(e) });
  }
});


