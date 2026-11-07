setImmediate(function () {
  try {
    const mod = Process.findModuleByName('QueensDomain-Win64-Shipping.exe');
    const base = mod.base;
    const pool = base.add(0x4A27F00);
    send({ pool: pool.toString() });
    const head = new Uint8Array(Memory.readByteArray(pool, 0x40));
    let hx = ''; for (let i = 0; i < head.length; i++) hx += head[i].toString(16).padStart(2,'0') + ' ';
    send({ poolHeadHex: hx });
    // dump 4 qwords at pool and at pool+0x10
    const q = [];
    for (let i = 0; i < 6; i++) q.push(pool.add(i*8).readPointer().toString());
    send({ poolQwords: q });
    const q2 = [];
    for (let i = 0; i < 6; i++) q2.push(pool.add(0x10 + i*8).readPointer().toString());
    send({ poolPlus10Qwords: q2 });
    // pick block 0 and dump its head
    for (const off of [0, 0x10]) {
      const p0 = pool.add(off).readPointer();
      if (p0.isNull()) continue;
      let ok = true, buf;
      try { buf = new Uint8Array(Memory.readByteArray(p0, 0x40)); } catch (e) { ok = false; }
      if (!ok) continue;
      let s = ''; for (let i = 0; i < buf.length; i++) s += buf[i].toString(16).padStart(2,'0') + ' ';
      send({ blockAt: 'pool+0x' + off.toString(16), ptr: p0.toString(), hex: s });
    }
  } catch (e) { send({ fatal: String(e) }); }
});
