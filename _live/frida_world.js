'use strict';
// Read both world slots, and GMalloc + its vtable slots.
setImmediate(function () {
  const mod = Process.findModuleByName('QueensDomain-Win64-Shipping.exe');
  if (!mod) { send({ err: 'no module' }); return; }
  const base = mod.base, end = base.add(mod.size);
  function readWorld(p) {
    try {
      if (p.isNull()) return null;
      return { ptr: p.toString(), ts: p.add(0x5A0).readFloat(), real: p.add(0x5A8).readFloat(),
               delta: p.add(0x5B0).readFloat() };
    } catch (e) { return { ptr: p.toString(), err: String(e) }; }
  }
  rpc.exports = {
    w: function () {
      return { slot_4BA86B0: readWorld(base.add(0x4BA86B0).readPointer()),
               slot_4BA4D60: readWorld(base.add(0x4BA4D60).readPointer()) };
    },
    gm: function () {
      const g = base.add(0x49DFB30).readPointer();
      if (g.isNull()) return { g: null };
      const vt = g.readPointer();
      const slots = {};
      for (const off of [0x00, 0x08, 0x10, 0x18, 0x20, 0x30]) {
        try {
          const fp = vt.add(off).readPointer();
          slots['0x' + off.toString(16)] = {
            fp: fp.toString(),
            inModule: !fp.isNull() && fp.compare(base) >= 0 && fp.compare(end) < 0
          };
        } catch (e) { slots['0x' + off.toString(16)] = { err: String(e) }; }
      }
      return { g: g.toString(), vtable: vt.toString(), slots: slots };
    }
  };
});
