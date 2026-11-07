'use strict';
// frida_clock.js -- sample UWorld's clock fields directly from known RVAs.
// Layout (proven in analysis/13b-pause-surface.md):
//   +0x5A0 TimeSeconds  +0x5A4 UnpausedTimeSeconds  +0x5A8 RealTimeSeconds
//   +0x5AC AudioTimeSeconds  +0x5B0 DeltaTimeSeconds  +0x5B4 PauseDelay
const GWORLD = 0x4BA86B0;
setImmediate(function () {
  const mod = Process.findModuleByName('QueensDomain-Win64-Shipping.exe');
  if (!mod) { send({ err: 'no module' }); return; }
  const base = mod.base;
  send({ ready: true, base: base.toString(), size: mod.size });
  function f(p, off) { try { return p.add(off).readFloat(); } catch (e) { return null; } }
  function sample() {
    try {
      const w = base.add(GWORLD).readPointer();
      if (w.isNull()) { send({ t: Date.now(), world: null }); return; }
      send({
        t: Date.now(), world: w.toString(),
        timesec: f(w, 0x5A0), unpaused: f(w, 0x5A4), real: f(w, 0x5A8),
        audio: f(w, 0x5AC), delta: f(w, 0x5B0), pausedelay: f(w, 0x5B4)
      });
    } catch (e) { send({ t: Date.now(), err: String(e) }); }
  }
  setInterval(sample, 100);
});
