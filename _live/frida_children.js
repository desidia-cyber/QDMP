'use strict';
// List the children (components/subobjects) of an actor.
setImmediate(function () {
  const mod = Process.findModuleByName('QueensDomain-Win64-Shipping.exe');
  if (!mod) { send({ err: 'no module' }); return; }
  const base = mod.base;
  function fname(idx) {
    try {
      const blocks = base.add(0x4A27F00).add(0x10);
      const blk = blocks.add((idx >>> 16) * 8).readPointer();
      if (blk.isNull()) return null;
      const e = blk.add((idx & 0xffff) * 2);
      const h = e.readU16(); const len = h >> 6;
      if (len < 1 || len > 300) return null;
      const a = new Uint8Array(e.add(2).readByteArray((h & 1) ? len * 2 : len));
      let s = ''; for (let j = 0; j < len; j++) { const c = (h & 1) ? (a[j * 2] | (a[j * 2 + 1] << 8)) : a[j]; if (c < 0x20) return null; s += String.fromCharCode(c); } return s;
    } catch (e) { return null; }
  }
  rpc.exports = {
    children: function (actorAddr) {
      const target = ptr(actorAddr);
      const goa = base.add(0x4A64250);
      const objects = goa.add(0x10).readPointer();
      const n = goa.add(0x24).readS32();
      const CH = 65536;
      const out = [];
      for (let i = 0; i < n; i++) {
        let o;
        try {
          const chunk = objects.add(Math.floor(i / CH) * 8).readPointer();
          if (chunk.isNull()) continue;
          o = chunk.add((i % CH) * 0x18).readPointer();
        } catch (e) { continue; }
        if (!o || o.isNull()) continue;
        let outer;
        try { outer = o.add(0x20).readPointer(); } catch (e) { continue; }
        if (outer.isNull() || !outer.equals(target)) continue;
        let cn = null, nm = null;
        try { const c = o.add(0x10).readPointer(); if (!c.isNull()) cn = fname(c.add(0x18).readS32()); } catch (e) { }
        try { nm = fname(o.add(0x18).readS32()); } catch (e) { }
        out.push({ cls: cn, name: nm, addr: o.toString() });
      }
      return out;
    }
  };
  send({ ready: true });
});
