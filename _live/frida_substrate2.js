'use strict';
// Last substrate hunt: GWorld / GEngine / GGameInstance globals in .data (found by resolving
// each candidate pointer's class name with the now-working FName decoder), plus the UObject
// vtable slot table, plus a byte-diff of the pawn's Arms vs CharacterMesh0 components.

const GOA_RVA = 0x4A64250, POOL_RVA = 0x4A27F00, DATA_RVA = 0x46F1000, DATA_VSIZE = 0x516174;

setImmediate(function () {
  try {
    const mod = Process.findModuleByName('QueensDomain-Win64-Shipping.exe');
    const base = mod.base, modEnd = base.add(mod.size);
    const goa = base.add(GOA_RVA);
    const objects = goa.add(0x10).readPointer();
    const numElements = goa.add(0x24).readS32();
    const blocks = base.add(POOL_RVA).add(0x10);
    send({ numElements, base: base.toString() });

    function fname(idx) {
      try {
        const blk = blocks.add((idx >>> 16) * 8).readPointer();
        if (blk.isNull()) return null;
        const e = blk.add((idx & 0xffff) * 2);
        const h = e.readU16();
        const len = h >> 6;
        if (len < 1 || len > 400) return null;
        if (h & 1) {
          const a = new Uint8Array(e.add(2).readByteArray(len * 2));
          let s = ''; for (let j = 0; j < len; j++) { const c = a[j*2] | (a[j*2+1] << 8); if (c < 0x20) return null; s += String.fromCharCode(c); } return s;
        }
        const a = new Uint8Array(e.add(2).readByteArray(len));
        let s = ''; for (let j = 0; j < len; j++) { const c = a[j]; if (c < 0x20 || c >= 0x7f) return null; s += String.fromCharCode(c); }
        return s;
      } catch (e2) { return null; }
    }
    function validObj(p) {
      if (!p || p.isNull()) return false;
      try { const vt = p.readPointer(); return !vt.isNull() && vt.compare(base) >= 0 && vt.compare(modEnd) < 0; } catch (e) { return false; }
    }
    function clsNameOf(objAddr) {
      try { const c = ptr(objAddr).add(0x10).readPointer(); return c.isNull() ? null : fname(c.add(0x18).readS32()); } catch (e) { return null; }
    }

    // ---- 1. scan .data for globals pointing at interesting objects ----
    const d0 = base.add(DATA_RVA);
    const want = { World: [], GameEngine: [], GameInstance: [], PlayerController: [], PlayerCharacter: [] };
    const dv = new DataView(Memory.readByteArray(d0, DATA_VSIZE));
    let tested = 0;
    for (let off = 0; off + 8 <= DATA_VSIZE; off += 8) {
      const lo = dv.getUint32(off, true), hi = dv.getUint32(off + 4, true);
      if (hi > 0xffff || (lo === 0 && hi === 0)) continue;
      const p = ptr(lo).add(ptr(hi).shl(32));
      if (!validObj(p)) continue;
      tested++;
      if (tested > 300000) break;
      const cn = clsNameOf(p);
      if (!cn) continue;
      for (const k of Object.keys(want)) {
        if (cn === k || cn === 'Default__' + k) {
          want[k].push({ dataRva: '0x' + (DATA_RVA + off).toString(16), obj: p.toString(), cls: cn });
        }
      }
      if (want.World.length > 6 && want.GameEngine.length > 6) break;
    }
    send({ globals: want, tested });

    // ---- 2. UObject vtable slots (ProcessEvent hunt) ----
    const CHUNK = 65536;
    let any = null;
    for (let i = 0; i < numElements && !any; i++) {
      try {
        const chunk = objects.add(Math.floor(i / CHUNK) * 8).readPointer();
        if (chunk.isNull()) continue;
        const o = chunk.add((i % CHUNK) * 24).readPointer();
        if (validObj(o)) any = o;
      } catch (e) { }
    }
    if (any) {
      const vt = any.readPointer();
      const slots = [];
      for (let s = 0; s < 90; s++) {
        let fp;
        try { fp = vt.add(s * 8).readPointer(); } catch (e) { break; }
        if (!validObj(fp) && !(fp.compare(base) >= 0 && fp.compare(modEnd) < 0)) continue;
        slots.push({ slot: s, rva: '0x' + fp.sub(base).toString(16) });
      }
      send({ uobject: any.toString(), vtable: vt.toString(), vtRva: '0x' + vt.sub(base).toString(16), slots });
    }

    // ---- 3. Arms vs CharacterMesh0 byte diff (B4) ----
    const comps = {};
    for (let i = 0; i < numElements; i++) {
      let o;
      try {
        const chunk = objects.add(Math.floor(i / CHUNK) * 8).readPointer();
        if (chunk.isNull()) continue;
        o = chunk.add((i % CHUNK) * 24).readPointer();
      } catch (e) { continue; }
      if (!validObj(o)) continue;
      const cn = clsNameOf(o);
      if (cn !== 'SkeletalMeshComponent') continue;
      const nm = fname(o.add(0x18).readS32());
      if (nm === 'Arms' || nm === 'CharacterMesh0') {
        let own = null;
        try { own = o.add(0x20).readPointer(); } catch (e) { }
        const ocn = (own && !own.isNull()) ? clsNameOf(own) : null;
        if (ocn === 'FS_Full_PlayerChar_C') {
          comps[nm] = o;
          if (comps.Arms && comps.CharacterMesh0) break;
        }
      }
    }
    if (comps.Arms && comps.CharacterMesh0) {
      const a = new Uint8Array(Memory.readByteArray(comps.Arms, 0x800));
      const b = new Uint8Array(Memory.readByteArray(comps.CharacterMesh0, 0x800));
      const diffs = [];
      for (let k = 0; k < 0x800; k++) {
        if (a[k] !== b[k] && a[k] <= 0x0f && b[k] <= 0x0f) {
          diffs.push({ off: '0x' + k.toString(16), arms: a[k], body: b[k] });
        }
      }
      send({ arms: comps.Arms.toString(), body: comps.CharacterMesh0.toString(), lowByteDiffs: diffs.slice(0, 40), n: diffs.length });
    } else {
      send({ meshDiff: 'pawn components not found (not in gameplay?)', got: Object.keys(comps) });
    }
  } catch (e) { send({ fatal: String(e) }); }
});
