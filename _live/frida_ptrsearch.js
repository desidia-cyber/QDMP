setImmediate(function () {
  try {
    const targets = ['0x220420f0000', '0x22042210000', '0x22042350000', '0x22042359e7d', '0x220420f126a'];
    const ranges = Process.enumerateRanges('r--');
    const found = [];
    for (const t of targets) {
      const hex = t.toString().replace(/^0x/, '').padStart(16, '0');
      const be = []; for (let i = 0; i < 16; i += 2) be.push(hex.substr(i, 2));
      const le = []; for (let i = be.length - 1; i >= 0; i--) le.push(be[i]);
      const pat = le.join(' ');
      let hits = [];
      for (const r of ranges) {
        if (r.size < 8 || r.size > 0x80000000) continue;
        let m; try { m = Memory.scanSync(r.base, r.size, pat); } catch (e) { continue; }
        for (const x of m) hits.push(x.address.toString());
        if (hits.length > 8) break;
      }
      found.push({ target: t, hits: hits.slice(0, 8) });
    }
    send({ found });
  } catch (e) { send({ fatal: String(e) }); }
});
