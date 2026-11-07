'use strict';
// frida_d5.js -- observe the enemy-persistence write on a real kill.
// StoreEnemySaveState(UObject* WorldContext, const FGuid* Key, float RespawnTime, float RemainingHP)
const STORE = 0x00E3BF10;
const APPLY = 0x028D2190;
const ONDIED = 0x00E36240;
const CLEARALL = 0x00E27460;

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
    } catch (e) { return '<' + e + '>'; }
  }
  function xmmLow(ctx, reg) {
    try {
      var v = ctx[reg];
      var lo = (v && typeof v.toNumber === 'function') ? v.toNumber() : v;
      lo = lo >>> 0;
      var b = new ArrayBuffer(4); new DataView(b).setUint32(0, lo, true);
      return new Float32Array(b)[0];
    } catch (e) { return NaN; }
  }
  function xmmRaw(ctx, reg) {
    try { var v = ctx[reg]; return ((v && v.toNumber) ? v.toNumber() : v) >>> 0; } catch (e) { return 0; }
  }

  Interceptor.attach(base.add(STORE), {
    onEnter: function (args) {
      send({
        hook: 'StoreEnemySaveState', at: Date.now(),
        worldctx: args[0].toString(), guid: guid(args[1]),
        respawn: xmmLow(this.context, 'xmm2'), hp: xmmLow(this.context, 'xmm3'),
        respawn_raw: '0x' + xmmRaw(this.context, 'xmm2').toString(16),
        hp_raw: '0x' + xmmRaw(this.context, 'xmm3').toString(16),
        lr: this.returnAddress.toString()
      });
    }
  });
  Interceptor.attach(base.add(APPLY), {
    onEnter: function (args) {
      send({ hook: 'ApplyDamage', at: Date.now(), actor: args[0].toString(),
             dmg: xmmLow(this.context, 'xmm1'), instigator: args[2].toString(),
             causer: args[3].toString() });
    }
  });
  Interceptor.attach(base.add(ONDIED), {
    onEnter: function (args) { send({ hook: 'OnEnemyDied', at: Date.now(), self: args[0].toString() }); }
  });
  Interceptor.attach(base.add(CLEARALL), {
    onEnter: function () { send({ hook: 'ClearAllEnemySaveStates', at: Date.now() }); }
  });
  send({ ready: true, base: base.toString() });
});
