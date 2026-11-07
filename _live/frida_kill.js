'use strict';
// frida_kill.js -- issue one lethal ApplyDamage on the game thread and log the
// enemy-persistence write it causes.
const STORE = 0x00E3BF10, APPLY = 0x028D2190, TICK = 0x007753A0, ONDIED = 0x00E36240;

setImmediate(function () {
  const mod = Process.findModuleByName('QueensDomain-Win64-Shipping.exe');
  if (!mod) { send({ err: 'no module' }); return; }
  const base = mod.base;

  function guid(p) {
    try {
      if (p.isNull()) return '';
      const b = new Uint8Array(p.readByteArray(16));
      const d = new DataView(b.buffer);
      return [d.getUint32(0, true), d.getUint32(4, true), d.getUint32(8, true), d.getUint32(12, true)]
        .map(function (x) { return ('00000000' + x.toString(16)).slice(-8); }).join('-');
    } catch (e) { return '<err>'; }
  }
  function xmmLow(ctx, reg) {
    try {
      var v = ctx[reg]; var lo = ((v && v.toNumber) ? v.toNumber() : v) >>> 0;
      var b = new ArrayBuffer(4); new DataView(b).setUint32(0, lo, true);
      return new Float32Array(b)[0];
    } catch (e) { return NaN; }
  }
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
  function clsName(o) { try { const c = o.add(0x10).readPointer(); return c.isNull() ? null : fname(c.add(0x18).readS32()); } catch (e) { return null; } }

  let pending = null, applied = 0;
  const applyFn = new NativeFunction(base.add(APPLY), 'float', ['pointer', 'float', 'pointer', 'pointer', 'pointer']);

  Interceptor.attach(base.add(TICK), {
    onEnter: function () {
      if (pending === null) return;
      const t = pending; pending = null;
      try {
        const cn = clsName(t);
        send({ event: 'kill-attempt', target: t.toString(), cls: cn });
        const r = applyFn(t, 99999.0, ptr(0), ptr(0), ptr(0));
        applied++;
        send({ event: 'apply-damage-returned', ret: r, target: t.toString() });
      } catch (e) { send({ event: 'kill-error', err: String(e) }); }
    }
  });

  Interceptor.attach(base.add(STORE), {
    onEnter: function (args) {
      send({ event: 'StoreEnemySaveState', at: Date.now(), worldctx: args[0].toString(),
             guid: guid(args[1]), respawn: xmmLow(this.context, 'xmm2'),
             hp: xmmLow(this.context, 'xmm3'), lr: this.returnAddress.toString() });
    }
  });
  Interceptor.attach(base.add(APPLY), {
    onEnter: function (args) {
      send({ event: 'ApplyDamage', at: Date.now(), actor: args[0].toString(),
             dmg: xmmLow(this.context, 'xmm1'), causer: args[3].toString() });
    }
  });
  Interceptor.attach(base.add(ONDIED), {
    onEnter: function (args) { send({ event: 'OnEnemyDied', at: Date.now(), self: args[0].toString() }); }
  });

  rpc.exports = {
    settarget: function (addr) { pending = ptr(addr); return 'queued ' + addr; },
    status: function () { return { applied: applied, pending: pending ? pending.toString() : null }; }
  };
  send({ ready: true, base: base.toString() });
});
