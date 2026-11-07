'use strict';
// Nearest placed actors to the player, with class names. Filters out class defaults
// and non-actor objects by requiring a plausible root-component translation.
const GOA = 0x4A64250, POOL = 0x4A27F00;
setImmediate(function () {
  const mod = Process.findModuleByName('QueensDomain-Win64-Shipping.exe');
  if (!mod) { send({ err: 'no module' }); return; }
  const base = mod.base;

  function fname(idx) {
    try {
      const blocks = base.add(POOL).add(0x10);
      const blk = blocks.add((idx >>> 16) * 8).readPointer();
      if (blk.isNull()) return null;
      const e = blk.add((idx & 0xffff) * 2);
      const h = e.readU16();
      const len = h >> 6;
      if (len < 1 || len > 300) return null;
      if (h & 1) {
        const a = new Uint8Array(e.add(2).readByteArray(len * 2));
        let s = ''; for (let j = 0; j < len; j++) { const c = a[j * 2] | (a[j * 2 + 1] << 8); if (c < 0x20) return null; s += String.fromCharCode(c); } return s;
      }
      const a = new Uint8Array(e.add(2).readByteArray(len));
      let s = ''; for (let j = 0; j < len; j++) { const c = a[j]; if (c < 0x20 || c >= 0x7f) return null; s += String.fromCharCode(c); } return s;
    } catch (e) { return null; }
  }
  function clsName(o) { try { const c = o.add(0x10).readPointer(); return c.isNull() ? null : fname(c.add(0x18).readS32()); } catch (e) { return null; } }
  function objName(o) { try { return fname(o.add(0x18).readS32()); } catch (e) { return null; } }
  function loc(o) {
    try {
      const root = o.add(0x130).readPointer();
      if (root.isNull()) return null;
      const x = root.add(0x1D0).readFloat(), y = root.add(0x1D4).readFloat(), z = root.add(0x1D8).readFloat();
      if (!isFinite(x) || !isFinite(y) || !isFinite(z)) return null;
      if (Math.abs(x) > 200000 || Math.abs(y) > 200000 || Math.abs(z) > 200000) return null;
      if (x === 0 && y === 0 && z === 0) return null;
      return { x: x, y: y, z: z };
    } catch (e) { return null; }
  }

  const goa = base.add(GOA);
  const objects = goa.add(0x10).readPointer();
  const n = goa.add(0x24).readS32();
  const CH = 65536;
  let pawn = null;
  const actors = [];
  for (let i = 0; i < n; i++) {
    let o;
    try {
      const chunk = objects.add(Math.floor(i / CH) * 8).readPointer();
      if (chunk.isNull()) continue;
      o = chunk.add((i % CH) * 0x18).readPointer();
    } catch (e) { continue; }
    if (!o || o.isNull()) continue;
    const cn = clsName(o);
    if (!cn) continue;
    if (cn === 'FS_Full_PlayerChar_C') {
      const pn = objName(o);
      if (!(pn && pn.indexOf('Default__') === 0)) { const pl0 = loc(o); if (pl0) pawn = o; }
      continue;
    }
    const nm = objName(o);
    if (nm && nm.indexOf('Default__') === 0) continue;
    const l = loc(o);
    if (!l) continue;
    actors.push({ cls: cn, name: nm, addr: o.toString(), loc: l });
  }
  const pl = pawn ? loc(pawn) : null;
  function dist(a) { if (!pl) return null; const dx = a.x - pl.x, dy = a.y - pl.y, dz = a.z - pl.z; return Math.round(Math.sqrt(dx * dx + dy * dy + dz * dz)); }
  actors.sort(function (a, b) { const da = dist(a.loc), db = dist(b.loc); return (da === null ? 1e12 : da) - (db === null ? 1e12 : db); });
  const RE = /enemy|boss|wasp|bat_|boner|skeleton|zombie|spider|knight|dragon|josami|crow|rat_|ghost|slime|demon|necro|swamp|fish/i;
  const sameLevel = pl ? actors.filter(function (a) {
    return Math.abs(a.loc.z - pl.z) < 220 && dist(a.loc) < 6000;
  }).sort(function (a, b) { return dist(a.loc) - dist(b.loc); }).slice(0, 70) : [];
  send({
    objects: n, actors: actors.length, pawn: pawn ? pawn.toString() : null, pawnLoc: pl,
    nearest: actors.slice(0, 40).map(function (a) { return { cls: a.cls, dist: dist(a.loc), addr: a.addr, enemyish: RE.test(a.cls), loc: { x: Math.round(a.loc.x), y: Math.round(a.loc.y), z: Math.round(a.loc.z) } }; }),
    enemies: actors.filter(function (a) { return RE.test(a.cls) && a.cls.slice(-2) === '_C'; }).slice(0, 25).map(function (a) { return { cls: a.cls, dist: dist(a.loc), addr: a.addr, loc: { x: Math.round(a.loc.x), y: Math.round(a.loc.y), z: Math.round(a.loc.z) } }; }),
    sameLevel: sameLevel.map(function (a) { return { cls: a.cls, dist: dist(a.loc), addr: a.addr, loc: { x: Math.round(a.loc.x), y: Math.round(a.loc.y), z: Math.round(a.loc.z) } }; })
  });
});
