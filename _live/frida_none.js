'use strict';
// Find the FName pool on the HEAP: scan readable private ranges for "None\0" and read the
// surrounding bytes to recover the real FNameEntry layout.

setImmediate(function () {
  try {
    const ranges = Process.enumerateRanges('r--');
    send({ rangeCount: ranges.length });

    const patterns = ['4e 6f 6e 65 00', '05 00 4e 6f 6e 65 00', '04 00 4e 6f 6e 65'];
    const findings = [];

    for (const r of ranges) {
      if (r.size < 4096 || r.size > 0x30000000) continue;
      // skip the module image BSS we already covered and huge mapped files
      for (const pat of patterns) {
        let matches;
        try { matches = Memory.scanSync(r.base, r.size, pat); } catch (e) { continue; }
        if (!matches || matches.length === 0) continue;
        for (const m of matches.slice(0, 2)) {
          let ctx, hex = '', asc = '';
          try { ctx = new Uint8Array(Memory.readByteArray(m.address.sub(48), 144)); } catch (e) { continue; }
          for (let j = 0; j < ctx.length; j++) {
            hex += ctx[j].toString(16).padStart(2, '0') + ' ';
            const c = ctx[j];
            asc += (c >= 0x20 && c < 0x7f) ? String.fromCharCode(c) : '.';
          }
          findings.push({
            addr: m.address.toString(),
            range: r.base.toString() + '+' + r.size.toString(16) + ' ' + r.protection,
            pat, hex, asc
          });
        }
        if (findings.length >= 10) break;
      }
      if (findings.length >= 10) break;
    }
    send({ findings });
  } catch (e) {
    send({ fatal: String(e) });
  }
});
