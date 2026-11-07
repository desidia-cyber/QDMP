'use strict';
// Residual verification: A6 GMalloc, A7 StaticConstructObject hook, B6 replicate bits,
// B8 mesh assignment, D1 clock.
const GMALLOC = 0x49DFB30, SCO = 0x11AF050, GWORLD = 0x4BA86B0,
      POOL = 0x4A27F00, GOA = 0x4A64250;
setImmediate(function () {
  const mod = Process.findModuleByName('QueensDomain-Win64-Shipping.exe');
  if (!mod) { send({ err: 'no module' }); return; }
  const base = mod.base, end = base.add(mod.size);

  let scoHits = 0, scoSamples = [];
  try {
    Interceptor.attach(base.add(SCO), {
      onEnter: function () {
        scoHits++;
        if (scoSamples.length < 4) send({ event: 'A7-sco-hit', at: Date.now(), lr: this.returnAddress.toString() });
      }
    });
  } catch (e) { send({ event: 'A7-hook-error', err: String(e) }); }

  function fname(idx) {
    try {
      const blocks = base.add(POOL).add(0x10);
      const blk = blocks.add((idx >>> 16) * 8).readPointer();
      if (blk.isNull()) return null;
      const e = blk.add((idx & 0xffff) * 2);
      const h = e.readU16(); const len = h >> 6;
      if (len < 1 || len > 300) return null;
      const a = new Uint8Array(e.add(2).readByteArray((h & 1) ? len * 2 : len));
      let s = ''; for (let j = 0; j < len; j++) { const c = (h & 1) ? (a[j * 2] | (a[j * 2 + 1] << 8)) : a[j]; if (c < 0x20) return null; s += String.fromCharCode(c); } return s;
    } catch (e) { return null; }
  }
  function clsName(o) { try { const c = o.add(0x10).readPointer(); return c.isNull() ? null : fname(c.add(0x18).readS32()); } catch (e) { return null; } }
  function objName(o) { try { return fname(o.add(0x18).readS32()); } catch (e) { return null; } }
  function validObj(p) { try { const vt = p.readPointer(); return !vt.isNull() && vt.compare(base) >= 0 && vt.compare(end) < 0; } catch (e) { return false; } }

  let pawnCache = null;
  function walk(cb) {
    const goa = base.add(GOA);
    const objects = goa.add(0x10).readPointer();
    const n = goa.add(0x24).readS32();
    const CH = 65536;
    for (let i = 0; i < n; i++) {
      let o;
      try {
        const chunk = objects.add(Math.floor(i / CH) * 8).readPointer();
        if (chunk.isNull()) continue;
        o = chunk.add((i % CH) * 0x18).readPointer();
      } catch (e) { continue; }
      if (!o || o.isNull()) continue;
      if (cb(o) === false) return;
    }
  }
  function findPawn() {
    if (pawnCache && validObj(pawnCache)) return pawnCache;
    walk(function (o) {
      if (clsName(o) !== 'FS_Full_PlayerChar_C') return;
      const nm = objName(o);
      if (nm && nm.indexOf('Default__') === 0) return;
      try {
        const root = o.add(0x130).readPointer();
        if (root.isNull()) return;
        const x = root.add(0x1D0).readFloat();
        if (x !== 0) { pawnCache = o; return false; }
      } catch (e) { }
    });
    return pawnCache;
  }

  function readClock() {
    try {
      const w = base.add(GWORLD).readPointer();
      if (w.isNull()) return null;
      return { ts: w.add(0x5A0).readFloat(), unp: w.add(0x5A4).readFloat(),
               real: w.add(0x5A8).readFloat(), delta: w.add(0x5B0).readFloat() };
    } catch (e) { return null; }
  }

  rpc.exports = {
    am: function () {
      const g = base.add(GMALLOC).readPointer();
      if (g.isNull()) return { ok: false, g: 'null' };
      const vt = g.readPointer();
      const inMod = !vt.isNull() && vt.compare(base) >= 0 && vt.compare(end) < 0;
      let malloc = 'not-called';
      try {
        const fn = new NativeFunction(vt.add(0x10).readPointer(), 'pointer', ['pointer', 'uint64']);
        const p = fn(g, 16);
        malloc = p.toString();
        try {
          const free = new NativeFunction(vt.add(0x30).readPointer(), 'void', ['pointer', 'pointer']);
          free(g, p);
        } catch (e2) { }
      } catch (e) { malloc = 'ERR ' + e; }
      return { ok: true, g: g.toString(), vtable: vt.toString(), inModule: inMod, malloc16: malloc };
    },
    a7: function () { return { hits: scoHits, samples: scoSamples }; },
    clock: function () { return readClock(); },
    b6: function () {
      const p = findPawn(); if (!p) return { err: 'no pawn' };
      const b58 = p.add(0x58).readU8(), b5b = p.add(0x5B).readU8();
      return { pawn: p.toString(), b58: b58, b5b: b5b,
               bReplicates: !!(b5b & 2), bReplicateMovement: !!(b58 & 0x10) };
    },
    b8: function () {
      const p = findPawn(); if (!p) return { err: 'no pawn' };
      const comps = [];
      walk(function (o) {
        let outer; try { outer = o.add(0x20).readPointer(); } catch (e) { return; }
        if (outer.isNull() || !outer.equals(p)) return;
        const cn = clsName(o);
        if (!cn || cn.indexOf('SkeletalMeshComponent') < 0) return;
        comps.push({ name: objName(o), cls: cn, addr: o });
      });
      const out = [];
      for (const c of comps) {
        const hits = [];
        for (let off = 0; off < 0x600; off += 8) {
          let v;
          try { v = c.addr.add(off).readPointer(); } catch (e) { break; }
          if (!v || v.isNull() || !validObj(v)) continue;
          const vcn = clsName(v);
          if (vcn === 'SkeletalMesh') hits.push({ off: '0x' + off.toString(16), obj: v.toString() });
        }
        out.push({ name: c.name, cls: c.cls, addr: c.addr.toString(), skeletalMeshPointers: hits });
      }
      return { pawn: p.toString(), components: out };
    }
  };
  send({ ready: true, base: base.toString(), size: mod.size });
});
