'use strict';
// Level-manager / streaming probe:
//  - world + manager (+0x3B0), both list stacks (+0x298/+0x2A8)
//  - FS_LevelList objects by name
//  - LevelStreamingDynamic flags (+0xBA: visible 0x08, loaded 0x10)
//  - shop actors (class name contains "Shop")
//  - add a list to the stack on the game thread + flush
const GWORLD = 0x4BA86B0, POOL = 0x4A27F00, GOA = 0x4A64250;
const GETMGR = 0x00E2EDC0, ADD = 0x00E37800, FLUSH = 0x00E2C270, TICK = 0x007753A0;
setImmediate(function () {
  const mod = Process.findModuleByName('QueensDomain-Win64-Shipping.exe');
  if (!mod) { send({ err: 'no module' }); return; }
  const base = mod.base, end = base.add(mod.size);

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
  function loc(o) {
    try { const r = o.add(0x130).readPointer(); if (r.isNull()) return null;
      return { x: r.add(0x1D0).readFloat(), y: r.add(0x1D4).readFloat(), z: r.add(0x1D8).readFloat() }; } catch (e) { return null; }
  }
  function walk(cb) {
    const goa = base.add(GOA); const objects = goa.add(0x10).readPointer(); const n = goa.add(0x24).readS32();
    const CH = 65536;
    for (let i = 0; i < n; i++) {
      let o;
      try { const ch = objects.add(Math.floor(i / CH) * 8).readPointer(); if (ch.isNull()) continue; o = ch.add((i % CH) * 0x18).readPointer(); }
      catch (e) { continue; }
      if (!o || o.isNull()) continue;
      if (cb(o) === false) return;
    }
  }
  function world() { try { return base.add(GWORLD).readPointer(); } catch (e) { return null; } }
  let MGR = null;
  function manager() {
    try {
      if (MGR) return MGR;
      const w = world(); if (!w || w.isNull()) return null;
      let m = w.add(0x3B0).readPointer();
      if (m.isNull()) {
        try { const gi = w.add(0x118).readPointer(); if (!gi.isNull()) m = gi.add(0x3B0).readPointer(); } catch (e) { }
      }
      return m.isNull() ? null : m;
    } catch (e) { return null; }
  }
  function stack(mgr, off) {
    const out = [];
    try {
      const data = mgr.add(off).readPointer();
      const n = mgr.add(off + 8).readS32();
      if (n <= 0 || n > 64 || data.isNull()) return out;
      for (let i = 0; i < n; i++) {
        const e = data.add(i * 0x10);
        const p = e.readPointer();
        out.push({ addr: p.toString(), name: p.isNull() ? null : objName(p), prio: e.add(8).readS32() });
      }
    } catch (e) { }
    return out;
  }
  function levels() {
    const out = [];
    walk(function (o) {
      const cn = clsName(o);
      if (!cn || cn.indexOf('LevelStreaming') < 0) return;
      let flags = -1;
      try { flags = o.add(0xBA).readU8(); } catch (e) { }
      out.push({ cls: cn, name: objName(o), flags: flags, visible: !!(flags & 0x08), loaded: !!(flags & 0x10) });
    });
    return out;
  }
  function shops() {
    const out = [];
    walk(function (o) {
      const cn = clsName(o);
      const nm = objName(o);
      const hay = ((cn || '') + ' ' + (nm || '')).toLowerCase();
      if (hay.indexOf('shop') < 0 && hay.indexOf('merchant') < 0 && hay.indexOf('vendor') < 0) return;
      const l = loc(o);
      out.push({ cls: cn, name: nm, addr: o.toString(), loc: l });
    });
    return out;
  }

  let pending = null, wantMgr = false;
  Interceptor.attach(base.add(TICK), {
    onEnter: function () {
      if (wantMgr) {
        wantMgr = false;
        try {
          const g = new NativeFunction(base.add(GETMGR), 'pointer', ['pointer']);
          MGR = g(world());
          send({ event: 'manager', addr: MGR ? MGR.toString() : null });
        } catch (e) { send({ event: 'manager-error', err: String(e) }); }
      }
      if (!pending) return;
      const p = pending; pending = null;
      try {
        const mgr = manager();
        if (!mgr) { send({ event: 'add-error', err: 'no manager' }); return; }
        const entry = Memory.alloc(16);
        entry.writePointer(ptr(p.list));
        entry.add(8).writeS32(p.prio);
        const addFn = new NativeFunction(base.add(ADD), 'void', ['pointer', 'pointer', 'uint8', 'uint8', 'int']);
        addFn(mgr, entry, p.sel, 1, p.prio);
        const flushFn = new NativeFunction(base.add(FLUSH), 'void', ['pointer', 'uint8']);
        flushFn(world(), 1);
        send({ event: 'added', list: p.list, sel: p.sel, prio: p.prio });
      } catch (e) { send({ event: 'add-error', err: String(e) }); }
    }
  });

  rpc.exports = {
    find: function (substr) {
      const out = [];
      walk(function (o) {
        const cn = clsName(o);
        if (cn && cn.toLowerCase().indexOf(substr.toLowerCase()) >= 0)
          out.push({ cls: cn, name: objName(o), addr: o.toString() });
      });
      return out;
    },
    owners: function () {
      const out = [];
      const w = world();
      function tryObj(label, o) {
        if (!o || o.isNull()) { out.push({ label: label, obj: null }); return; }
        let m = null;
        try { m = o.add(0x3B0).readPointer(); } catch (e) { }
        out.push({ label: label, obj: o.toString(), cls: clsName(o), at3b0: m ? m.toString() : null,
                   mgrCls: m && !m.isNull() ? clsName(m) : null });
      }
      tryObj('world', w);
      walk(function (o) {
        const cn = clsName(o);
        if (cn === 'FS_GameInstance_C' || cn === 'FS_GameInstanceBase' || cn === 'GameInstance') tryObj(cn, o);
        if (cn === 'FS_PlayerController_BP_C' || cn === 'PlayerController') tryObj(cn, o);
        if (cn === 'FS_Full_PlayerChar_C') tryObj(cn, o);
      });
      return out;
    },
    info: function () {
      const w = world(), m = manager();
      const lists = [];
      walk(function (o) { if (clsName(o) === 'FS_LevelList') lists.push({ name: objName(o), addr: o.toString() }); });
      return {
        world: w ? w.toString() : null, manager: m ? m.toString() : null,
        stacks: m ? { a: stack(m, 0x298), b: stack(m, 0x2A8) } : null,
        lists: lists,
        shops: shops().slice(0, 30),
        levelCount: levels().length,
        levels: levels().filter(function (x) { return x.visible; }).map(function (x) { return x.name; }).slice(0, 60)
      };
    },
    levels: function () { return levels(); },
    shops: function () { return shops(); },
    add: function (list, sel, prio) { pending = { list: list, sel: sel, prio: prio }; return 'queued'; },
    getmgr: function () { wantMgr = true; return 'queued'; },
    stack: function () {
      const m = manager();
      return m ? { manager: m.toString(), a: stack(m, 0x298), b: stack(m, 0x2A8) } : { manager: null };
    }
  };
  send({ ready: true, base: base.toString() });
});
