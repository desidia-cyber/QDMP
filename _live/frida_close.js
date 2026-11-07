'use strict';
// Close-out probe: level-manager apply, shop level, multi-GUID spawner, boss.
const GWORLD = 0x4BA86B0, POOL = 0x4A27F00, GOA = 0x4A64250;
const TICK = 0x007753A0;
const ADD = 0x00E215E0, FLUSH = 0x00E2C270, SETVIS = 0x029ACB20;
const APPLY = 0x028D2190, STORE = 0x00E3BF10, ONDIED = 0x00E36240, SETLOC = 0x0265B400;
const SETTICK = 0x026636F0, APPLYLIST = 0x00E21E40, MANAGERUPD = 0x00E3CF9E;
const HOOK_MGR = false;   // manager hooks were implicated in a crash; enable only when needed

setImmediate(function () {
  const mod = Process.findModuleByName('QueensDomain-Win64-Shipping.exe');
  if (!mod) { send({ err: 'no module' }); return; }
  const base = mod.base, end = base.add(mod.size);

  const fnCache = {};
  function fname(idx) {
    if (fnCache[idx] !== undefined) return fnCache[idx];
    try {
      const blocks = base.add(POOL).add(0x10);
      const blk = blocks.add((idx >>> 16) * 8).readPointer();
      let s = null;
      if (!blk.isNull()) {
        const e = blk.add((idx & 0xffff) * 2);
        const h = e.readU16(); const len = h >> 6;
        if (len >= 1 && len <= 400) {
          const a = new Uint8Array(e.add(2).readByteArray((h & 1) ? len * 2 : len));
          s = ''; for (let j = 0; j < len; j++) { const c = (h & 1) ? (a[j * 2] | (a[j * 2 + 1] << 8)) : a[j]; if (c < 0x20) { s = null; break; } s += String.fromCharCode(c); }
        }
      }
      fnCache[idx] = s; return s;
    } catch (e) { fnCache[idx] = null; return null; }
  }
  const clsCache = {}, isACache = {};
  function nameIdx(o) { try { return o.add(0x18).readS32(); } catch (e) { return -1; } }
  function objName(o) { return fname(nameIdx(o)); }
  function clsNameOfPtr(c) {
    const k = c.toString();
    if (clsCache[k] !== undefined) return clsCache[k];
    let v = null;
    try { if (!c.isNull()) v = fname(c.add(0x18).readS32()); } catch (e) { }
    clsCache[k] = v; return v;
  }
  function clsName(o) { try { return clsNameOfPtr(o.add(0x10).readPointer()); } catch (e) { return null; } }
  function isActorCls(c) {
    const k = c.toString();
    if (isACache[k] !== undefined) return isACache[k];
    let v = false;
    try {
      let p = c;
      for (let i = 0; i < 40 && !p.isNull(); i++) {
        if (clsNameOfPtr(p) === 'Actor') { v = true; break; }
        p = p.add(0x40).readPointer();
      }
    } catch (e) { }
    isACache[k] = v; return v;
  }
  function loc(o) {
    try { const r = o.add(0x130).readPointer(); if (r.isNull()) return null;
      return { x: r.add(0x1D0).readFloat(), y: r.add(0x1D4).readFloat(), z: r.add(0x1D8).readFloat() }; } catch (e) { return null; }
  }
  function guid(p) {
    try { if (p.isNull()) return ''; const b = new Uint8Array(p.readByteArray(16)); const d = new DataView(b.buffer);
      return [d.getUint32(0, true), d.getUint32(4, true), d.getUint32(8, true), d.getUint32(12, true)]
        .map(function (x) { return ('00000000' + x.toString(16)).slice(-8); }).join('-'); } catch (e) { return '<err>'; }
  }
  function xmmLow(ctx, reg) {
    try { var v = ctx[reg]; var lo = ((v && v.toNumber) ? v.toNumber() : v) >>> 0;
      var b = new ArrayBuffer(4); new DataView(b).setUint32(0, lo, true); return new Float32Array(b)[0]; } catch (e) { return NaN; }
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
  function manager() {
    try { const w = world(); if (!w || w.isNull()) return null;
      let m = w.add(0x3B0).readPointer();
      if (m.isNull()) { const gi = w.add(0x118).readPointer(); if (!gi.isNull()) m = gi.add(0x3B0).readPointer(); }
      return m.isNull() ? null : m; } catch (e) { return null; }
  }
  function stack(mgr, off) {
    const out = [];
    try { const data = mgr.add(off).readPointer(); const n = mgr.add(off + 8).readS32();
      if (n <= 0 || n > 128 || data.isNull()) return out;
      for (let i = 0; i < n; i++) { const e = data.add(i * 0x10); const p = e.readPointer();
        out.push({ addr: p.toString(), name: p.isNull() ? null : objName(p), prio: e.add(8).readS32() }); } } catch (e) { }
    return out;
  }
  function levels() {
    const out = [];
    walk(function (o) {
      const cn = clsName(o);
      if (!cn || cn.indexOf('LevelStreaming') < 0) return;
      if (objName(o) && objName(o).indexOf('Default__') === 0) return;
      let flags = -1; try { flags = o.add(0xBA).readU8(); } catch (e) { }
      out.push({ cls: cn, name: objName(o), addr: o.toString(), flags: flags,
                 visible: !!(flags & 0x08), loaded: !!(flags & 0x10) });
    });
    return out;
  }
  function ulevels() {
    const out = [];
    walk(function (o) {
      if (clsName(o) !== 'Level') return;
      const nm = objName(o);
      out.push({ name: nm, addr: o.toString(), nameIdx: nameIdx(o) });
    });
    return out;
  }
  function actors(substr) {
    const out = [];
    const s = (substr || '').toLowerCase();
    walk(function (o) {
      const cn = clsName(o) || '';
      const nm = objName(o) || '';
      if (nm.indexOf('Default__') === 0) return;
      if (s && (cn.toLowerCase().indexOf(s) < 0 && nm.toLowerCase().indexOf(s) < 0)) return;
      try { if (!isActorCls(o.add(0x10).readPointer())) return; } catch (e) { return; }
      out.push({ cls: cn, name: nm, addr: o.toString(), loc: loc(o) });
    });
    return out;
  }
  function streamingNames(o) {
    // collect plausible level-package FNames inside the streaming object
    const seen = {}, out = [];
    for (let off = 0; off < 0x140; off += 4) {
      let idx; try { idx = o.add(off).readS32(); } catch (e) { break; }
      if (idx <= 0 || idx > 0x200000) continue;
      const s = fname(idx);
      if (!s) continue;
      if (s.indexOf('/') < 0 && s.indexOf('_') < 0) continue;
      if (!(/RuinsLevel|Maps\//.test(s) || /^[0-9]{3}_/.test(s) || /^(AquaRuins|GreenRuins|Library|Mausoleum|SZ_|Prison|Autumn|DropTunnel|StoneGarden|ROS|Sipeha|Intro|BirdsTest)/.test(s))) continue;
      if (seen[s]) continue;
      seen[s] = 1; out.push(s);
    }
    return out;
  }
  function streaming() {
    const out = [];
    walk(function (o) {
      const cn = clsName(o) || '';
      if (cn.indexOf('LevelStreaming') < 0) return;
      if (objName(o) && objName(o).indexOf('Default__') === 0) return;
      let flags = -1; try { flags = o.add(0xBA).readU8(); } catch (e) { }
      out.push({ cls: cn, addr: o.toString(), names: streamingNames(o), flags: flags,
                 visible: !!(flags & 8), loaded: !!(flags & 0x10) });
    });
    return out;
  }
  function pawn() {
    let p = null;
    walk(function (o) {
      if (clsName(o) !== 'FS_Full_PlayerChar_C') return;
      const nm = objName(o) || '';
      if (nm.indexOf('Default__') === 0) return;
      const l = loc(o);
      if (l && (l.x !== 0 || l.y !== 0 || l.z !== 0)) { p = { addr: o.toString(), loc: l, obj: o }; return false; }
    });
    return p;
  }
  function findStreamingForLevel(levelName) {
    const want = levelName.toLowerCase();
    let found = null;
    walk(function (o) {
      const cn = clsName(o) || '';
      if (cn.indexOf('LevelStreaming') < 0) return;
      if (objName(o) && objName(o).indexOf('Default__') === 0) return;
      const names = streamingNames(o);
      for (const n of names) {
        if (n.toLowerCase().indexOf(want) >= 0 || n.toLowerCase().split('/').pop() === want) {
          found = { addr: o.toString(), cls: cn, name: n }; return false;
        }
      }
    });
    return found || { err: 'no streaming object for ' + levelName };
  }
  function findListByName(nm) {
    let found = null;
    walk(function (o) {
      if (clsName(o) !== 'FS_LevelList') return;
      if (objName(o) === nm) { found = o; return false; }
    });
    return found;
  }

  let q = [];            // game-thread work queue: {fn, resolve}
  function onGameThread(fn) { return new Promise(function (res) { q.push({ fn: fn, res: res }); }); }

  const addFn = new NativeFunction(base.add(ADD), 'void', ['pointer', 'pointer', 'uint8', 'uint8', 'int']);
  const flushFn = new NativeFunction(base.add(FLUSH), 'void', ['pointer', 'uint8']);
  const setVis = new NativeFunction(base.add(SETVIS), 'void', ['pointer', 'uint8']);
  const applyFn = new NativeFunction(base.add(APPLY), 'float', ['pointer', 'float', 'pointer', 'pointer', 'pointer']);
  const setLoc = new NativeFunction(base.add(SETLOC), 'uint8', ['pointer', 'pointer', 'uint8', 'pointer', 'uint32']);
  const setTick = new NativeFunction(base.add(SETTICK), 'void', ['pointer', 'uint8']);

  const events = [];
  function note(e) { events.push(e); if (events.length > 4000) events.shift(); }

  function hook(addr, label, cb) {
    try { Interceptor.attach(addr, cb); send({ hook: label, at: addr.toString() }); }
    catch (e) { send({ hook: label, err: String(e) }); }
  }
  hook(base.add(TICK), 'tick', {
    onEnter: function () {
      while (q.length) {
        const j = q.shift();
        try { j.res({ ok: true, v: j.fn() }); } catch (e) { j.res({ ok: false, err: String(e) }); }
      }
    }
  });
  hook(base.add(STORE), 'store', {
    onEnter: function (args) {
      note({ ev: 'StoreEnemySaveState', at: Date.now(), worldctx: args[0].toString(),
             guid: guid(args[1]), respawn: xmmLow(this.context, 'xmm2'), hp: xmmLow(this.context, 'xmm3'),
             lr: this.returnAddress.toString() });
    }
  });
  hook(base.add(APPLY), 'apply', {
    onEnter: function (args) {
      note({ ev: 'ApplyDamage', at: Date.now(), actor: args[0].toString(), dmg: xmmLow(this.context, 'xmm1'),
             causer: args[3].toString() });
    }
  });
  hook(base.add(ONDIED), 'died', {
    onEnter: function (args) { note({ ev: 'OnEnemyDied', at: Date.now(), self: args[0].toString(), cls: clsName(args[0]) }); }
  });
  if (HOOK_MGR) {
    hook(base.add(ADD), 'add', {
      onEnter: function (args) { note({ ev: 'AddLevelListToStack', at: Date.now(), mgr: args[0].toString(),
        list: args[1].toString(), listName: args[1].isNull() ? null : objName(args[1]), sel: args[2].toInt32(),
        usePrio: args[3].toInt32() }); }
    });
    hook(base.add(FLUSH), 'flush', {
      onEnter: function (args) { note({ ev: 'FlushLevelManagerActions', at: Date.now(), ctx: args[0].toString(), imm: args[1].toInt32() }); }
    });
    hook(base.add(APPLYLIST), 'applylist', {
      onEnter: function (args) { note({ ev: 'ApplyLevelSet', at: Date.now(), mgr: args[0].toString() }); }
    });
    hook(base.add(MANAGERUPD), 'mgrudp', {
      onEnter: function (args) { note({ ev: 'LevelManagerUpdate', at: Date.now(), mgr: args[0].toString() }); }
    });
  }

  rpc.exports = {
    info: function () {
      const w = world(), m = manager(), p = pawn();
      return { world: w ? w.toString() : null, manager: m ? m.toString() : null,
               player: p ? p.addr : null, playerLoc: p ? p.loc : null,
               stacks: m ? { a: stack(m, 0x298), b: stack(m, 0x2A8) } : null,
               levelCount: levels().length, levels: levels(),
               ulevelCount: ulevels().length, ulevels: ulevels(),
               storeEvents: events.filter(function (x) { return x.ev === 'StoreEnemySaveState'; }).length };
    },
    levels: function () { return levels(); },
    ulevels: function () { return ulevels(); },
    streaming: function () { return streaming(); },
    actors: function (s) { return actors(s); },
    lists: function () {
      const out = []; walk(function (o) { if (clsName(o) === 'FS_LevelList') out.push({ name: objName(o), addr: o.toString() }); });
      return out;
    },
    events: function (n) { return events.slice(-(n || 100)); },
    clearevents: function () { events.length = 0; return 'cleared'; },
    addlist: function (name, sel, prio) {
      return onGameThread(function () {
        const m = manager(), w = world();
        if (!m || !w) return { err: 'no manager/world' };
        const list = findListByName(name);
        if (!list) return { err: 'no list ' + name };
        const before = levels().filter(function (x) { return x.visible; }).length;
        const beforeU = ulevels().length;
        addFn(m, list, sel | 0, 1, prio | 0);
        const mid = levels().filter(function (x) { return x.visible; }).length;
        flushFn(w, 1);
        const after = levels().filter(function (x) { return x.visible; }).length;
        return { list: list.toString(), sel: sel | 0, prio: prio | 0, visBefore: before, visAfterAdd: mid, visAfterFlush: after,
                 ulevelsBefore: beforeU, ulevelsAfter: ulevels().length };
      });
    },
    showlevel: function (name, load, vis) {
      return onGameThread(function () {
        const f = findStreamingForLevel(name);
        if (f.err) return f;
        const lvl = ptr(f.addr);
        try { if (load) { const fn = new NativeFunction(lvl.readPointer().add(0x270).readPointer(), 'void', ['pointer', 'uint8']); fn(lvl, 1); } } catch (e) { }
        if (vis) setVis(lvl, 1);
        let flags = -1; try { flags = lvl.add(0xBA).readU8(); } catch (e) { }
        return { level: f.addr, off: f.off, cls: f.cls, flags: flags, visible: !!(flags & 8), loaded: !!(flags & 0x10) };
      });
    },
    hidelevel: function (name) {
      return onGameThread(function () {
        const f = findStreamingForLevel(name);
        if (f.err) return f;
        setVis(ptr(f.addr), 0); return f;
      });
    },
    tpplayer: function (x, y, z) {
      return onGameThread(function () {
        const p = pawn(); if (!p) return { err: 'no pawn' };
        const v = Memory.alloc(16); v.writeFloat(x); v.add(4).writeFloat(y); v.add(8).writeFloat(z); v.add(12).writeFloat(0);
        setLoc(p.obj, v, 0, ptr(0), 0);
        return { after: loc(p.obj) };
      });
    },
    tpa: function (addr, x, y, z) {
      return onGameThread(function () {
        const v = Memory.alloc(16); v.writeFloat(x); v.add(4).writeFloat(y); v.add(8).writeFloat(z); v.add(12).writeFloat(0);
        const b = loc(ptr(addr));
        setLoc(ptr(addr), v, 0, ptr(0), 0);
        return { before: b, after: loc(ptr(addr)) };
      });
    },
    tickoff: function (addr) { return onGameThread(function () { setTick(ptr(addr), 0); return 'off'; }); },
    tickon: function (addr) { return onGameThread(function () { setTick(ptr(addr), 1); return 'on'; }); },
    hurt: function (addr, dmg) {
      return onGameThread(function () {
        const a = ptr(addr);
        const r = applyFn(a, dmg || 99999.0, ptr(0), ptr(0), ptr(0));
        return { ret: r, cls: clsName(a), loc: loc(a) };
      });
    },
    findobj: function (s) {
      const out = []; const q = (s || '').toLowerCase();
      walk(function (o) {
        const nm = objName(o); if (!nm) return;
        if (nm.toLowerCase().indexOf(q) < 0) return;
        out.push({ cls: clsName(o), name: nm, addr: o.toString() });
      });
      return out.slice(0, 60);
    },
    funcs: function (clsAddr) {
      const out = []; let cls = ptr(clsAddr);
      for (let d = 0; d < 40 && !cls.isNull(); d++) {
        let f = null; try { f = cls.add(0x48).readPointer(); } catch (e) { break; }
        let g = 0;
        while (!f.isNull() && g++ < 8000) {
          try { const nm = fname(f.add(0x18).readS32()); if (nm) out.push(nm); f = f.add(0x28).readPointer(); }
          catch (e) { break; }
        }
        try { cls = cls.add(0x40).readPointer(); } catch (e) { break; }
      }
      return out;
    },
    fnameidx: function (i) { return fname(i); },
    arrnames: function (addr, off) {
      try {
        const o = ptr(addr); const d = o.add(off).readPointer(); const n = o.add(off + 8).readS32();
        const out = []; for (let i = 0; i < Math.min(n, 128); i++) out.push(fname(d.add(i * 8).readS32()));
        return { count: n, values: out };
      } catch (e) { return { err: String(e) }; }
    },
    arrints: function (addr, off) {
      try {
        const o = ptr(addr); const d = o.add(off).readPointer(); const n = o.add(off + 8).readS32();
        const out = []; for (let i = 0; i < Math.min(n, 128); i++) out.push(d.add(i * 4).readS32());
        return { count: n, values: out };
      } catch (e) { return { err: String(e) }; }
    },
    objat: function (addr, off) {
      try { const p = ptr(addr).add(off).readPointer();
        return { ptr: p.toString(), name: p.isNull() ? null : objName(p), cls: p.isNull() ? null : clsName(p) }; }
      catch (e) { return { err: String(e) }; }
    },
    spawn: function (clsAddr, x, y, z) {
      return onGameThread(function () {
        const w = world();
        const loc = Memory.alloc(16); loc.writeFloat(x); loc.add(4).writeFloat(y); loc.add(8).writeFloat(z); loc.add(12).writeFloat(0);
        const rot = Memory.alloc(16); rot.writeFloat(0); rot.add(4).writeFloat(0); rot.add(8).writeFloat(0); rot.add(12).writeFloat(0);
        const parms = Memory.alloc(0x100); parms.writeByteArray(new Array(0x100).fill(0));
        const fn = new NativeFunction(base.add(0x029AD950), 'pointer', ['pointer', 'pointer', 'pointer', 'pointer', 'pointer']);
        let a = null;
        try { a = fn(w, ptr(clsAddr), loc, rot, parms); } catch (e) { return { err: String(e) }; }
        return { actor: a.isNull() ? null : a.toString(), cls: a.isNull() ? null : clsName(a), loc: a.isNull() ? null : loc(a) };
      });
    },
    maxdist: function (addr, d) {
      return onGameThread(function () {
        try { const fn = new NativeFunction(base.add(0x00E26B20), 'uint8', ['pointer', 'float']); return { ret: fn(ptr(addr), d) }; }
        catch (e) { return { err: String(e) }; }
      });
    },
    callfunc: function (addr, name) {
      return onGameThread(function () {
        const o = ptr(addr);
        let cls = o.add(0x10).readPointer();
        let fn = null, owner = null;
        for (let d = 0; d < 40 && !cls.isNull() && !fn; d++) {
          let f = cls.add(0x48).readPointer(); let g = 0;
          while (!f.isNull() && g++ < 8000) {
            try { if (fname(f.add(0x18).readS32()) === name) { fn = f; owner = cls; break; } } catch (e) { break; }
            f = f.add(0x28).readPointer();
          }
          cls = cls.add(0x40).readPointer();
        }
        if (!fn) return { err: 'function not found: ' + name };
        const pe = new NativeFunction(base.add(0x011A7AA0), 'void', ['pointer', 'pointer', 'pointer']);
        const parms = Memory.alloc(0x200);
        parms.writeByteArray(new Array(0x200).fill(0));
        try { pe(o, fn, parms); } catch (e) { return { err: 'ProcessEvent: ' + e, fn: fn.toString() }; }
        return { ok: true, fn: fn.toString(), owner: owner.toString() };
      });
    },
    spawner: function (addr) {
      try {
        const o = ptr(addr);
        const data = o.add(0x238).readPointer();
        const n = o.add(0x240).readS32();
        const guids = [];
        for (let i = 0; i < Math.min(n, 16); i++) guids.push(guid(data.add(i * 16)));
        const my = o.add(0x250).readPointer();
        const myN = o.add(0x258).readS32();
        let myBytes = null;
        try { myBytes = Array.prototype.slice.call(new Uint8Array(my.readByteArray(Math.min(64, myN * 16 || 64)))); } catch (e) { }
        const ed = o.add(696).readPointer();
        return { addr: addr, guidCount: n, guids: guids, saveDataCount: o.add(0x248).readS32(),
                 mySaveData: my ? my.toString() : null, mySaveDataN: myN,
                 respawnTimer: o.add(752).readFloat(), spawnDisabled: o.add(995).readU8(),
                 maxKill: o.add(692).readS32(), useMaxKill: o.add(689).readU8(),
                 initted: o.add(1012).readU8(), enemySpawned: o.add(1056).readU8(),
                 readyForSpawn: o.add(994).readU8(), playerInRange: o.add(920).readU8(),
                 spawnOnActivate: o.add(992).readU8(), levelIsRemoved: o.add(1040).readU8(),
                 currentKill: o.add(744).readS32(), enemyWasKilled: o.add(960).readU8(),
                 maxSpawnDist: o.add(768).readFloat(), minSpawnDist: o.add(772).readFloat(),
                 enemyData: ed.isNull() ? null : ed.toString(),
                 enemyDataName: ed.isNull() ? null : objName(ed),
                 enemyDataCls: ed.isNull() ? null : clsName(ed),
                 activationTrigger: o.add(919).readU8(), behaviourMode: o.add(918).readU8(),
                 trapOnActivation: o.add(993).readU8(), myStreamingLevel: o.add(1024).readPointer().toString() };
      } catch (e) { return { err: String(e) }; }
    },
    read: function (addr, off, size) {
      try { const p = ptr(addr).add(off); const b = new Uint8Array(p.readByteArray(size));
        return Array.prototype.slice.call(b).map(function (x) { return ('0' + x.toString(16)).slice(-2); }).join(' '); }
      catch (e) { return 'ERR ' + e; }
    },
    dumpclass: function (addr) {
      // property names of the object's class and parents (FField chain)
      const out = [];
      let cls = ptr(addr).add(0x10).readPointer();
      for (let d = 0; d < 40 && !cls.isNull(); d++) {
        let p = cls.add(0x50).readPointer();
        let g = 0;
        while (!p.isNull() && g++ < 4000) {
          try { const nm = fname(p.add(0x28).readS32()); if (nm) out.push({ name: nm, off: p.add(0x4c).readS32(), type: fname(p.add(0x10).readS32()) }); } catch (e) { }
          p = p.add(0x20).readPointer();
        }
        cls = cls.add(0x40).readPointer();
      }
      return out;
    },
    worldclock: function () {
      try { const w = world(); return { ts: w.add(0x5A0).readFloat(), real: w.add(0x5A8).readFloat() }; } catch (e) { return null; }
    }
  };
  send({ ready: true, base: base.toString(), manager: manager() ? manager().toString() : null });
});
