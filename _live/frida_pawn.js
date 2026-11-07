'use strict';
// Find the LIVE player character (not the CDO) and dump its components + mesh assets.

const GOA_RVA = 0x4A64250, POOL_RVA = 0x4A27F00;

setImmediate(function () {
  try {
    const mod = Process.findModuleByName('QueensDomain-Win64-Shipping.exe');
    const base = mod.base, modEnd = base.add(mod.size);
    const goa = base.add(GOA_RVA);
    const objects = goa.add(0x10).readPointer();
    const numElements = goa.add(0x24).readS32();
    const blocks = base.add(POOL_RVA).add(0x10);

    function fname(idx) {
      try {
        const blk = blocks.add((idx >>> 16) * 8).readPointer();
        if (blk.isNull()) return null;
        const e = blk.add((idx & 0xffff) * 2);
        const h = e.readU16();
        const len = h >> 6;
        if (len < 1 || len > 400) return null;
        if (h & 1) {
          const a = new Uint8Array(e.add(2).readByteArray(len * 2));
          let s = ''; for (let j = 0; j < len; j++) { const c = a[j*2] | (a[j*2+1] << 8); if (c < 0x20) return null; s += String.fromCharCode(c); } return s;
        }
        const a = new Uint8Array(e.add(2).readByteArray(len));
        let s = ''; for (let j = 0; j < len; j++) { const c = a[j]; if (c < 0x20 || c >= 0x7f) return null; s += String.fromCharCode(c); }
        return s;
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
    const nameOf = o => { try { return fname(o.add(0x18).readS32()); } catch (e) { return null; } };
    const classOf = o => { try { return o.add(0x10).readPointer(); } catch (e) { return null; } };
    const outerOf = o => { try { return o.add(0x20).readPointer(); } catch (e) { return null; } };
    const clsName = o => { const c = classOf(o); return (c && !c.isNull()) ? nameOf(c) : null; };
    const validObj = p => {
      if (!p || p.isNull()) return false;
      try { const vt = p.readPointer(); return !vt.isNull() && vt.compare(base) >= 0 && vt.compare(modEnd) < 0; } catch (e) { return false; }
    };

    send({ numElements });

    // 1. every object whose class looks like a player character/controller, excluding CDOs
    const live = [];
    for (let i = 0; i < numElements; i++) {
      const o = objAt(i);
      if (!o) continue;
      const nm = nameOf(o);
      if (nm && nm.startsWith('Default__')) continue;
      const cn = clsName(o);
      if (!cn) continue;
      if (/PlayerChar|PlayerController|Full_Player/i.test(cn)) {
        live.push({ i, addr: o.toString(), cls: cn, name: nm });
      }
    }
    send({ livePlayerObjects: live.slice(0, 25), count: live.length });

    // 2. for each live player character, dump components and any mesh asset references
    const out = [];
    for (const p of live.filter(x => /PlayerChar/i.test(x.cls)).slice(0, 4)) {
      const pa = ptr(p.addr);
      const comps = [];
      for (let i = 0; i < numElements; i++) {
        const o = objAt(i);
        if (!o) continue;
        const ou = outerOf(o);
        if (ou && !ou.isNull() && ou.equals(pa)) {
          const cd = { cls: clsName(o), name: nameOf(o) };
          // scan the component's first 0x400 bytes for object pointers and name their classes
          const refs = [];
          try {
            for (let off = 0; off < 0x400; off += 8) {
              const q = o.add(off).readPointer();
              if (!validObj(q)) continue;
              const cn2 = clsName(q);
              if (cn2 && /SkeletalMesh$|AnimSequence|AnimMontage|AnimBlueprint|PhysicsAsset|Skeleton$/.test(cn2)) {
                refs.push({ off, cls: cn2, name: nameOf(q) });
              }
            }
          } catch (e) { }
          if (refs.length) cd.meshRefs = refs;
          comps.push(cd);
        }
      }
      out.push({ pawn: p, components: comps, n: comps.length });
    }
    send({ livePawnComponents: out });

    // 3. any SkeletalMeshComponent in the world whose owner is a player char
    const smc = [];
    for (let i = 0; i < numElements && smc.length < 80; i++) {
      const o = objAt(i);
      if (!o) continue;
      if (clsName(o) !== 'SkeletalMeshComponent') continue;
      const own = outerOf(o);
      const ocn = (own && !own.isNull()) ? clsName(own) : null;
      if (ocn && /PlayerChar|Full_Player/i.test(ocn)) {
        smc.push({ comp: nameOf(o), owner: nameOf(own), ownerClass: ocn });
      }
    }
    send({ playerSkeletalMeshes: smc });
  } catch (e) { send({ fatal: String(e) }); }
});
