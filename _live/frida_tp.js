'use strict';
// Teleport an enemy in front of the player (game thread) and log the persistence write.
const TICK = 0x007753A0, SETLOC = 0x0265B400, STORE = 0x00E3BF10,
      APPLY = 0x028D2190, ONDIED = 0x00E36240;
setImmediate(function () {
  const mod = Process.findModuleByName('QueensDomain-Win64-Shipping.exe');
  if (!mod) { send({ err: 'no module' }); return; }
  const base = mod.base;
  const setLoc = new NativeFunction(base.add(SETLOC), 'uint8', ['pointer', 'pointer', 'uint8', 'pointer', 'uint32']);

  function guid(p) {
    try { const b = new Uint8Array(p.readByteArray(16)); const d = new DataView(b.buffer);
      return [d.getUint32(0, true), d.getUint32(4, true), d.getUint32(8, true), d.getUint32(12, true)]
        .map(function (x) { return ('00000000' + x.toString(16)).slice(-8); }).join('-'); } catch (e) { return '<err>'; }
  }
  function xmmLow(ctx, reg) {
    try { var v = ctx[reg]; var lo = ((v && v.toNumber) ? v.toNumber() : v) >>> 0;
      var b = new ArrayBuffer(4); new DataView(b).setUint32(0, lo, true); return new Float32Array(b)[0]; } catch (e) { return NaN; }
  }
  function locOf(a) {
    try { const r = a.add(0x130).readPointer();
      return { x: r.add(0x1D0).readFloat(), y: r.add(0x1D4).readFloat(), z: r.add(0x1D8).readFloat() }; } catch (e) { return null; }
  }

  let pending = null, pin = null, pinTicks = 0, pendingTickOff = null;
  const pinVec = Memory.alloc(16);
  const setTick = new NativeFunction(base.add(0x026636F0), 'void', ['pointer', 'uint8']);
  Interceptor.attach(base.add(TICK), {
    onEnter: function () {
      if (pending) {
        const p = pending; pending = null;
        try {
          const v = Memory.alloc(16);
          v.writeFloat(p.x); v.add(4).writeFloat(p.y); v.add(8).writeFloat(p.z); v.add(12).writeFloat(0);
          const before = locOf(p.actor);
          const ok = setLoc(p.actor, v, 0, ptr(0), 0);
          send({ event: 'teleport', actor: p.actor.toString(), ok: ok, before: before, dest: { x: p.x, y: p.y, z: p.z }, after: locOf(p.actor) });
        } catch (e) { send({ event: 'teleport-error', err: String(e) }); }
      }
      if (pendingTickOff) {
        try { setTick(pendingTickOff, 0); send({ event: 'tickoff', actor: pendingTickOff.toString() }); }
        catch (e) { send({ event: 'tickoff-error', err: String(e) }); }
        pendingTickOff = null;
      }
      if (pin) {
        pinTicks++;
        try {
          pinVec.writeFloat(pin.x); pinVec.add(4).writeFloat(pin.y);
          pinVec.add(8).writeFloat(pin.z); pinVec.add(12).writeFloat(0);
          setLoc(pin.actor, pinVec, 0, ptr(0), 0);
          if (pinTicks % 60 === 0) send({ event: 'pin', tick: pinTicks, at: locOf(pin.actor) });
        } catch (e) { send({ event: 'pin-error', err: String(e) }); }
      }
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
    tp: function (addr, x, y, z) { pending = { actor: ptr(addr), x: x, y: y, z: z }; return 'queued'; },
    pin: function (addr, x, y, z) { pin = { actor: ptr(addr), x: x, y: y, z: z }; return 'pinned'; },
    unpin: function () { pin = null; return 'unpinned'; },
    tickoff: function (addr) { pendingTickOff = ptr(addr); return 'queued'; }
  };
  send({ ready: true, base: base.toString() });
});
